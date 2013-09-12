/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et tw=78: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"

#include "nsDebug.h"
#include "nsIAtom.h"
#include "CNavDTD.h"
#include "nsHTMLTokens.h"
#include "nsCRT.h"
#include "nsParser.h"
#include "nsIHTMLContentSink.h"
#include "nsScanner.h"
#include "prenv.h"
#include "prtypes.h"
#include "prio.h"
#include "plstr.h"
#include "nsDTDUtils.h"
#include "nsHTMLTokenizer.h"
#include "nsParserNode.h"
#include "nsHTMLEntities.h"
#include "nsLinebreakConverter.h"
#include "nsIFormProcessor.h"
#include "nsTArray.h"
#include "nsReadableUtils.h"
#include "nsUnicharUtils.h"
#include "nsIServiceManager.h"
#include "nsParserConstants.h"

using namespace mozilla;

/*
 * Ignore kFontStyle and kPhrase tags when the stack is deep, bug 58917.
 */
#define FONTSTYLE_IGNORE_DEPTH (MAX_REFLOW_DEPTH * 80 / 100)
#define PHRASE_IGNORE_DEPTH    (MAX_REFLOW_DEPTH * 90 / 100)

static NS_DEFINE_CID(kFormProcessorCID, NS_FORMPROCESSOR_CID);

#ifdef DEBUG
static const  char kNullToken[] = "Error: Null token given";
static const  char kInvalidTagStackPos[] = "Error: invalid tag stack position";
#endif

#include "nsElementTable.h"

// Some flags for use by the DTD.
#define NS_DTD_FLAG_NONE                   0x00000000
#define NS_DTD_FLAG_HAS_OPEN_HEAD          0x00000001
#define NS_DTD_FLAG_HAS_OPEN_BODY          0x00000002
#define NS_DTD_FLAG_HAS_OPEN_FORM          0x00000004
#define NS_DTD_FLAG_HAS_EXPLICIT_HEAD      0x00000008
#define NS_DTD_FLAG_HAD_BODY               0x00000010
#define NS_DTD_FLAG_HAD_FRAMESET           0x00000020
#define NS_DTD_FLAG_ENABLE_RESIDUAL_STYLE  0x00000040
#define NS_DTD_FLAG_ALTERNATE_CONTENT      0x00000080 // NOFRAMES, NOSCRIPT
#define NS_DTD_FLAG_MISPLACED_CONTENT      0x00000100
#define NS_DTD_FLAG_IN_MISPLACED_CONTENT   0x00000200
#define NS_DTD_FLAG_STOP_PARSING           0x00000400

#define NS_DTD_FLAG_HAS_MAIN_CONTAINER     (NS_DTD_FLAG_HAD_BODY |            \
                                            NS_DTD_FLAG_HAD_FRAMESET)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CNavDTD)
  NS_INTERFACE_MAP_ENTRY(nsIDTD)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDTD)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(CNavDTD)
NS_IMPL_CYCLE_COLLECTING_RELEASE(CNavDTD)

NS_IMPL_CYCLE_COLLECTION_1(CNavDTD, mSink)

CNavDTD::CNavDTD()
  : mMisplacedContent(0),
    mTokenAllocator(0),
    mBodyContext(new nsDTDContext()),
    mTempContext(0),
    mCountLines(true),
    mTokenizer(0),
    mDTDMode(eDTDMode_quirks),
    mDocType(eHTML_Quirks),
    mParserCommand(eViewNormal),
    mLineNumber(1),
    mOpenMapCount(0),
    mHeadContainerPosition(-1),
    mFlags(NS_DTD_FLAG_NONE)
{
}

CNavDTD::~CNavDTD()
{
  delete mBodyContext;
  delete mTempContext;
}

NS_IMETHODIMP
CNavDTD::WillBuildModel(const CParserContext& aParserContext,
                        nsITokenizer* aTokenizer,
                        nsIContentSink* aSink)
{
  nsresult result = NS_OK;

  mFilename = aParserContext.mScanner->GetFilename();
  mFlags = NS_DTD_FLAG_ENABLE_RESIDUAL_STYLE;
  mLineNumber = 1;
  mDTDMode = aParserContext.mDTDMode;
  mParserCommand = aParserContext.mParserCommand;
  mMimeType = aParserContext.mMimeType;
  mDocType = aParserContext.mDocType;
  mTokenizer = aTokenizer;
  mBodyContext->SetNodeAllocator(&mNodeAllocator);

  if (!aParserContext.mPrevContext && aSink) {

    if (!mSink) {
      mSink = do_QueryInterface(aSink, &result);
      if (NS_FAILED(result)) {
        mFlags |= NS_DTD_FLAG_STOP_PARSING;
        return result;
      }
    }

    mFlags |= nsHTMLTokenizer::GetFlags(aSink);

  }

  return result;
}

NS_IMETHODIMP
CNavDTD::BuildModel(nsITokenizer* aTokenizer,
                    bool aCountLines,
                    const nsCString*)
{
  NS_PRECONDITION(mBodyContext != nullptr,
                  "Create a context before calling build model");

  nsresult result = NS_OK;

  if (!aTokenizer) {
    return NS_OK;
  }

  nsITokenizer* const oldTokenizer = mTokenizer;

  mCountLines     = aCountLines;
  mTokenizer      = aTokenizer;
  mTokenAllocator = mTokenizer->GetTokenAllocator();
  
  if (!mSink) {
    return (mFlags & NS_DTD_FLAG_STOP_PARSING)
           ? NS_ERROR_HTMLPARSER_STOPPARSING
           : result;
  }

  if (mBodyContext->GetCount() == 0) {
    CToken* tempToken;
    if (ePlainText == mDocType) {
      tempToken =
        mTokenAllocator->CreateTokenOfType(eToken_start, eHTMLTag_pre);
      if (tempToken) {
        mTokenizer->PushTokenFront(tempToken);
      }
    }

    // Always open a body if frames are disabled.
    if (!(mFlags & NS_IPARSER_FLAG_FRAMES_ENABLED)) {
      tempToken =
        mTokenAllocator->CreateTokenOfType(eToken_start,
                                           eHTMLTag_body,
                                           NS_LITERAL_STRING("body"));
      if (tempToken) {
        mTokenizer->PushTokenFront(tempToken);
      }
    }

    // If the content model is empty, then begin by opening <html>.
    CStartToken* theToken = (CStartToken*)mTokenizer->GetTokenAt(0);
    if (theToken) {
      eHTMLTags theTag = (eHTMLTags)theToken->GetTypeID();
      eHTMLTokenTypes theType = eHTMLTokenTypes(theToken->GetTokenType());
      if (theTag != eHTMLTag_html || theType != eToken_start) {
        tempToken =
          mTokenAllocator->CreateTokenOfType(eToken_start,
                                             eHTMLTag_html,
                                             NS_LITERAL_STRING("html"));
        if (tempToken) {
          mTokenizer->PushTokenFront(tempToken);
        }
      }
    } else {
      tempToken =
        mTokenAllocator->CreateTokenOfType(eToken_start,
                                           eHTMLTag_html,
                                           NS_LITERAL_STRING("html"));
      if (tempToken) {
        mTokenizer->PushTokenFront(tempToken);
      }
    }
  }

  while (NS_SUCCEEDED(result)) {
    if (!(mFlags & NS_DTD_FLAG_STOP_PARSING)) {
      CToken* theToken = mTokenizer->PopToken();
      if (!theToken) {
        break;
      }
      result = HandleToken(theToken);
    } else {
      result = NS_ERROR_HTMLPARSER_STOPPARSING;
      break;
    }

    if (NS_ERROR_HTMLPARSER_INTERRUPTED == mSink->DidProcessAToken()) {
      // The content sink has requested that DTD interrupt processing tokens
      // We need to make sure that an interruption does not override
      // a request to block the parser.
      if (NS_SUCCEEDED(result)) {
        result = NS_ERROR_HTMLPARSER_INTERRUPTED;
        break;
      }
    }
  }

  mTokenizer = oldTokenizer;
  return result;
}

nsresult
CNavDTD::BuildNeglectedTarget(eHTMLTags aTarget,
                              eHTMLTokenTypes aType)
{ 
  NS_ASSERTION(mTokenizer, "tokenizer is null! unable to build target.");
  NS_ASSERTION(mTokenAllocator, "unable to create tokens without an allocator.");
  if (!mTokenizer || !mTokenAllocator) {
    return NS_OK;
  }

  CToken* target = mTokenAllocator->CreateTokenOfType(aType, aTarget);
  NS_ENSURE_TRUE(target, NS_ERROR_OUT_OF_MEMORY);
  mTokenizer->PushTokenFront(target);
  // Also, BuildModel
  // doesn't seem to care about the charset, and at this point we have no idea
  // what the charset was, so 0 can and must suffice.
  return BuildModel(mTokenizer, mCountLines, 0);
}

NS_IMETHODIMP
CNavDTD::DidBuildModel(nsresult anErrorCode)
{
  nsresult result = NS_OK;

  if (mSink) {
    if (NS_OK == anErrorCode) {
      if (!(mFlags & NS_DTD_FLAG_HAS_MAIN_CONTAINER)) {
        // This document is not a frameset document, however, it did not contain
        // a body tag either. So, make one!. Note: Body tag is optional per spec..
        // Also note: We ignore the return value of BuildNeglectedTarget, we
        // can't reasonably respond to errors (or requests to block) at this
        // point in the parsing process.
        BuildNeglectedTarget(eHTMLTag_body, eToken_start);
      }
      if (mFlags & NS_DTD_FLAG_MISPLACED_CONTENT) {
        // Looks like the misplaced contents are not processed yet.
        // Here is our last chance to handle the misplaced content.

        // Keep track of the top index.
        int32_t topIndex = mBodyContext->mContextTopIndex;
        
        // Loop until we've really consumed all of our misplaced content.
        do {
          mFlags &= ~NS_DTD_FLAG_MISPLACED_CONTENT; 

          // mContextTopIndex refers to the misplaced content's legal parent index.
          result = HandleSavedTokens(mBodyContext->mContextTopIndex);
          if (NS_FAILED(result)) {
            NS_ERROR("Bug in the DTD");
            break;
          }

          // If we start handling misplaced content while handling misplaced
          // content, mContextTopIndex gets modified. However, this new index
          // necessarily points to the middle of a closed tag (since we close
          // new tags after handling the misplaced content). So we restore the
          // insertion point after every iteration.
          mBodyContext->mContextTopIndex = topIndex;
        } while (mFlags & NS_DTD_FLAG_MISPLACED_CONTENT);

        mBodyContext->mContextTopIndex = -1; 
      }

      // Now let's disable style handling to save time when closing remaining
      // stack members.
      mFlags &= ~NS_DTD_FLAG_ENABLE_RESIDUAL_STYLE;
      while (mBodyContext->GetCount() > 0) { 
        result = CloseContainersTo(mBodyContext->Last(), false);
        NS_ENSURE_SUCCESS(result, result);
      } 
    } else {
      // If you're here, then an error occurred, but we still have nodes on the stack.
      // At a minimum, we should grab the nodes and recycle them.
      // Just to be correct, we'll also recycle the nodes.
      while (mBodyContext->GetCount() > 0) { 
        nsEntryStack* theChildStyles = 0;
        nsCParserNode* theNode = mBodyContext->Pop(theChildStyles);
        IF_DELETE(theChildStyles, &mNodeAllocator);
        IF_FREE(theNode, &mNodeAllocator);
      } 
    }

    // Now make sure the misplaced content list is empty,
    // by forcefully recycling any tokens we might find there.
    CToken* theToken = 0;
    while ((theToken = (CToken*)mMisplacedContent.Pop())) {
      IF_FREE(theToken, mTokenAllocator);
    }
  }

  return result;
}

NS_IMETHODIMP_(void) 
CNavDTD::Terminate() 
{ 
  mFlags |= NS_DTD_FLAG_STOP_PARSING; 
}


NS_IMETHODIMP_(int32_t) 
CNavDTD::GetType() 
{ 
  return NS_IPARSER_FLAG_HTML; 
}

NS_IMETHODIMP_(nsDTDMode)
CNavDTD::GetMode() const
{
  return mDTDMode;
}

/**
 * Text and some tags require a body when they're added, this function returns
 * true for those tags.
 *
 * @param aToken The current token that we care about.
 * @param aTokenizer A tokenizer that we can get the tags attributes off of.
 * @return true if aToken does indeed force the body to open.
 */
static bool
DoesRequireBody(CToken* aToken, nsITokenizer* aTokenizer)
{
  bool result = false;

  if (aToken) {
    eHTMLTags theTag = (eHTMLTags)aToken->GetTypeID();
    if (gHTMLElements[theTag].HasSpecialProperty(kRequiresBody)) {
      if (theTag == eHTMLTag_input) {
        // IE & Nav4x opens up a body for type=text - Bug 66985
        // XXXbz but we don't want to open one for <input> with no
        // type attribute?  That's pretty whack.
        int32_t ac = aToken->GetAttributeCount();
        for(int32_t i = 0; i < ac; ++i) {
          CAttributeToken* attr = static_cast<CAttributeToken*>
                                             (aTokenizer->GetTokenAt(i));
          const nsSubstring& name = attr->GetKey();
          const nsAString& value = attr->GetValue();
          // XXXbz note that this stupid case-sensitive comparison is
          // actually depended on by sites...
          if ((name.EqualsLiteral("type") || 
               name.EqualsLiteral("TYPE"))    
              && 
              !(value.EqualsLiteral("hidden") || 
              value.EqualsLiteral("HIDDEN"))) {
            result = true; 
            break;
          }
        }
      } else {
        result = true;
      }
    }
  }
 
  return result;
}

static bool
ValueIsHidden(const nsAString& aValue)
{
  // Having to deal with whitespace here sucks, but we have to match
  // what the content sink does.
  nsAutoString str(aValue);
  str.Trim("\n\r\t\b");
  return str.LowerCaseEqualsLiteral("hidden");
}

// Check whether aToken corresponds to a <input type="hidden"> tag.  The token
// must be a start tag token for an <input>.  This must be called at a point
// when all the attributes for the input are still in the tokenizer.
static bool
IsHiddenInput(CToken* aToken, nsITokenizer* aTokenizer)
{
  NS_PRECONDITION(eHTMLTokenTypes(aToken->GetTokenType()) == eToken_start,
                  "Must be start token");
  NS_PRECONDITION(eHTMLTags(aToken->GetTypeID()) == eHTMLTag_input,
                  "Must be <input> tag");
  
  int32_t ac = aToken->GetAttributeCount();
  NS_ASSERTION(ac <= aTokenizer->GetCount(),
               "Not enough tokens in the tokenizer");
  // But we don't really trust ourselves to get that right
  ac = NS_MIN(ac, aTokenizer->GetCount());
  
  for (int32_t i = 0; i < ac; ++i) {
    NS_ASSERTION(eHTMLTokenTypes(aTokenizer->GetTokenAt(i)->GetTokenType()) ==
                   eToken_attribute, "Unexpected token type");
    // Again, we're not sure we actually manage to guarantee that
    if (eHTMLTokenTypes(aTokenizer->GetTokenAt(i)->GetTokenType()) !=
        eToken_attribute) {
      break;
    }
    
    CAttributeToken* attrToken =
      static_cast<CAttributeToken*>(aTokenizer->GetTokenAt(i));
    if (!attrToken->GetKey().LowerCaseEqualsLiteral("type")) {
      continue;
    }

    return ValueIsHidden(attrToken->GetValue());
  }

  return false;    
}

/**
 * Returns whether or not there is a tag of type aType open on aContext.
 */
static bool
HasOpenTagOfType(int32_t aType, const nsDTDContext& aContext)
{
  int32_t count = aContext.GetCount();

  while (--count >= 0) {
    if (gHTMLElements[aContext.TagAt(count)].IsMemberOf(aType)) {
      return true;
    }
  }

  return false;
}

nsresult
CNavDTD::HandleToken(CToken* aToken)
{
  if (!aToken) {
    return NS_OK;
  }

  nsresult        result   = NS_OK;
  CHTMLToken*     theToken = static_cast<CHTMLToken*>(aToken);
  eHTMLTokenTypes theType  = eHTMLTokenTypes(theToken->GetTokenType());
  eHTMLTags       theTag   = (eHTMLTags)theToken->GetTypeID();

  aToken->SetLineNumber(mLineNumber);

  if (mCountLines) {
    mLineNumber += aToken->GetNewlineCount();
  }

  if (mFlags & NS_DTD_FLAG_MISPLACED_CONTENT) {
    // Included TD & TH to fix Bug# 20797
    static eHTMLTags gLegalElements[] = {
      eHTMLTag_table, eHTMLTag_thead, eHTMLTag_tbody,
      eHTMLTag_tr, eHTMLTag_td, eHTMLTag_th, eHTMLTag_tfoot
    };
    // Don't even try processing misplaced tokens if we're already
    // handling misplaced content. See bug 269095
    if (mFlags & NS_DTD_FLAG_IN_MISPLACED_CONTENT) {
      PushIntoMisplacedStack(theToken);
      return NS_OK;
    }

    eHTMLTags theParentTag = mBodyContext->Last();
    if (FindTagInSet(theTag, gLegalElements,
                     ArrayLength(gLegalElements)) ||
        (gHTMLElements[theParentTag].CanContain(theTag, mDTDMode) &&
         // Here's a problem.  If theTag is legal in here, we don't move it
         // out.  So if we're moving stuff out of here, the parent of theTag
         // gets closed at this point.  But some things are legal
         // _everywhere_ and hence would effectively close out misplaced
         // content in tables.  This is undesirable, so treat them as
         // illegal here so they'll be shipped out with their parents and
         // siblings.  See bug 40855 for an explanation (that bug was for
         // comments, but the same issues arise with whitespace, newlines,
         // noscript, etc).  Script is special, though.  Shipping it out
         // breaks document.write stuff.  See bug 243064.
         (!gHTMLElements[theTag].HasSpecialProperty(kLegalOpen) ||
          theTag == eHTMLTag_script)) ||
        (theTag == eHTMLTag_input && theType == eToken_start &&
         FindTagInSet(theParentTag, gLegalElements,
                      ArrayLength(gLegalElements)) &&
         IsHiddenInput(theToken, mTokenizer))) {
      // Reset the state since all the misplaced tokens are about to get
      // handled.
      mFlags &= ~NS_DTD_FLAG_MISPLACED_CONTENT;

      result = HandleSavedTokens(mBodyContext->mContextTopIndex);
      NS_ENSURE_SUCCESS(result, result);

      mBodyContext->mContextTopIndex = -1;
    } else {
      PushIntoMisplacedStack(theToken);
      return result;
    }
  }

  /*
   * This section of code is used to "move" misplaced content from one location
   * in our document model to another. (Consider what would happen if we found a
   * <P> tag in the head.) To move content, we throw it onto the
   * misplacedcontent deque until we can deal with it.
   */
  switch(theTag) {
    case eHTMLTag_html:
    case eHTMLTag_noframes:
    case eHTMLTag_script:
    case eHTMLTag_doctypeDecl:
    case eHTMLTag_instruction:
      break;

    default:
      if (!gHTMLElements[eHTMLTag_html].SectionContains(theTag, false)) {
        if (!(mFlags & (NS_DTD_FLAG_HAS_MAIN_CONTAINER |
                        NS_DTD_FLAG_ALTERNATE_CONTENT))) {
          // For bug examples from this code, see bugs: 18928, 20989.
          // At this point we know the body/frameset aren't open.
          // If the child belongs in the head, then handle it (which may open
          // the head); otherwise, push it onto the misplaced stack.

          bool isExclusive = false;
          bool theChildBelongsInHead =
            gHTMLElements[eHTMLTag_head].IsChildOfHead(theTag, isExclusive);
          if (theChildBelongsInHead &&
              !isExclusive &&
              !gHTMLElements[theTag].HasSpecialProperty(kPreferHead)) {
            if (mMisplacedContent.GetSize() == 0 &&
                (!gHTMLElements[theTag].HasSpecialProperty(kPreferBody) ||
                 (mFlags & NS_DTD_FLAG_HAS_EXPLICIT_HEAD))) {
              // This tag can either be in the body or the head. Since
              // there is no indication that the body should be open,
              // put this token in the head.
              break;
            }

            // Otherwise, we have received some indication that the body is
            // "open", so push this token onto the misplaced content stack.
            theChildBelongsInHead = false;
          }

          if (!theChildBelongsInHead) {
            eHTMLTags top = mBodyContext->Last();
            NS_ASSERTION(top != eHTMLTag_userdefined,
                         "Userdefined tags should act as leaves in the head");
            if (top != eHTMLTag_html && top != eHTMLTag_head &&
                gHTMLElements[top].CanContain(theTag, mDTDMode)) {
              // Some tags (such as <object> and <script>) are opened in the
              // head and allow other non-head content to be children.
              // Note: Userdefined tags in the head act like leaves.
              break;
            }

            // If you're here then we found a child of the body that was out of
            // place.  We're going to move it to the body by storing it
            // temporarily on the misplaced stack.  However, in quirks mode, a
            // few tags request, ambiguosly, for a BODY. - Bugs 18928, 24204.-
            PushIntoMisplacedStack(aToken);

            if (IsAlternateTag(theTag)) {
              // These tags' contents are consumed as CDATA. If we simply
              // pushed them on the misplaced content stack, the CDATA
              // contents would force us to open a body, which could be
              // wrong. So we collect the whole tag as misplaced in one
              // gulp. Note that the tokenizer guarantees that there will
              // be an end tag.
              CToken *current = aToken;
              while (current->GetTokenType() != eToken_end ||
                     current->GetTypeID() != theTag) {
                current = static_cast<CToken *>(mTokenizer->PopToken());
                NS_ASSERTION(current, "The tokenizer is not creating good "
                                      "alternate tags");
                PushIntoMisplacedStack(current);
              }

              // XXX Add code to also collect incorrect attributes on the
              // end tag.
            }

            if (DoesRequireBody(aToken, mTokenizer)) {
              CToken* theBodyToken =
                mTokenAllocator->CreateTokenOfType(eToken_start,
                                                   eHTMLTag_body,
                                                   NS_LITERAL_STRING("body"));
              result = HandleToken(theBodyToken);
            }
            return result;
          }
        }
      }
  }

  if (theToken) {
    switch (theType) {
      case eToken_text:
      case eToken_start:
      case eToken_whitespace:
      case eToken_newline:
        result = HandleStartToken(theToken);
        break;

      case eToken_end:
        result = HandleEndToken(theToken);
        break;

      case eToken_cdatasection:
      case eToken_comment:
      case eToken_markupDecl:
        result = HandleCommentToken(theToken);
        break;

      case eToken_entity:
        result = HandleEntityToken(theToken);
        break;

      case eToken_attribute:
        result = HandleAttributeToken(theToken);
        break;

      case eToken_instruction:
        result = HandleProcessingInstructionToken(theToken);
        break;

      case eToken_doctypeDecl:
        result = HandleDocTypeDeclToken(theToken);
        break;

      default:
        break;
    }

    IF_FREE(theToken, mTokenAllocator);
    if (result == NS_ERROR_HTMLPARSER_STOPPARSING) {
      mFlags |= NS_DTD_FLAG_STOP_PARSING;
    } else if (NS_FAILED(result) && result != NS_ERROR_HTMLPARSER_BLOCK) {
      result = NS_OK;
    }
  }

  return result;
}

nsresult
CNavDTD::DidHandleStartTag(nsIParserNode& aNode, eHTMLTags aChildTag)
{
  nsresult result = NS_OK;

  switch (aChildTag) {
    case eHTMLTag_pre:
    case eHTMLTag_listing:
      {
        // Skip the 1st newline inside PRE and LISTING unless this is a
        // plain text doc (for which we pushed a PRE in CNavDTD::BuildModel).

        // XXX This code is incorrect in the face of misplaced <pre> and
        // <listing> tags (as direct children of <table>).
        CToken* theNextToken = mTokenizer->PeekToken();
        if (ePlainText != mDocType && theNextToken) {
          eHTMLTokenTypes theType = eHTMLTokenTypes(theNextToken->GetTokenType());
          if (eToken_newline == theType) {
            if (mCountLines) {
              mLineNumber += theNextToken->GetNewlineCount();
            }
            theNextToken = mTokenizer->PopToken();
            IF_FREE(theNextToken, mTokenAllocator); // fix for Bug 29379
          }
        }
      }
      break;

    default:
      break;
  }

  return result;
}

int32_t
CNavDTD::LastOf(eHTMLTags aTagSet[], int32_t aCount) const
{
  for (int32_t theIndex = mBodyContext->GetCount() - 1; theIndex >= 0;
       --theIndex) {
    if (FindTagInSet((*mBodyContext)[theIndex], aTagSet, aCount)) {
      return theIndex;
    }
  }

  return kNotFound;
}

static bool
CanBeContained(eHTMLTags aChildTag, nsDTDContext& aContext)
{
  /* #    Interesting test cases:       Result:
   * 1.   <UL><LI>..<B>..<LI>           inner <LI> closes outer <LI>
   * 2.   <CENTER><DL><DT><A><CENTER>   allow nested <CENTER>
   * 3.   <TABLE><TR><TD><TABLE>...     allow nested <TABLE>
   * 4.   <FRAMESET> ... <FRAMESET>
   */
  bool    result = true;
  int32_t theCount = aContext.GetCount();

  if (0 < theCount) {
    const TagList* theRootTags = gHTMLElements[aChildTag].GetRootTags();
    const TagList* theSpecialParents =
      gHTMLElements[aChildTag].GetSpecialParents();

    if (theRootTags) {
      int32_t theRootIndex = LastOf(aContext, *theRootTags);
      int32_t theSPIndex = theSpecialParents
                           ? LastOf(aContext, *theSpecialParents)
                           : kNotFound;
      int32_t theChildIndex =
        nsHTMLElement::GetIndexOfChildOrSynonym(aContext, aChildTag);
      int32_t theTargetIndex = (theRootIndex > theSPIndex)
                               ? theRootIndex
                               : theSPIndex;

      if (theTargetIndex == theCount-1 ||
          (theTargetIndex == theChildIndex &&
           gHTMLElements[aChildTag].CanContainSelf())) {
        result = true;
      } else {
        result = false;

        static eHTMLTags gTableElements[] = { eHTMLTag_td, eHTMLTag_th };

        int32_t theIndex = theCount - 1;
        while (theChildIndex < theIndex) {
          eHTMLTags theParentTag = aContext.TagAt(theIndex--);
          if (gHTMLElements[theParentTag].IsMemberOf(kBlockEntity)  ||
              gHTMLElements[theParentTag].IsMemberOf(kHeading)      ||
              gHTMLElements[theParentTag].IsMemberOf(kPreformatted) ||
              gHTMLElements[theParentTag].IsMemberOf(kFormControl)  ||  //fixes bug 44479
              gHTMLElements[theParentTag].IsMemberOf(kList)) {
            if (!HasOptionalEndTag(theParentTag)) {
              result = true;
              break;
            }
          } else if (FindTagInSet(theParentTag, gTableElements,
                                  ArrayLength(gTableElements))) {
            // Added this to catch a case we missed; bug 57173.
            result = true;
            break;
          }
        }
      }
    }
  }

  return result;
}

enum eProcessRule { eNormalOp, eLetInlineContainBlock };

nsresult
CNavDTD::HandleDefaultStartToken(CToken* aToken, eHTMLTags aChildTag,
                                 nsCParserNode *aNode)
{
  NS_PRECONDITION(nullptr != aToken, kNullToken);

  nsresult  result = NS_OK;
  bool    theChildIsContainer = nsHTMLElement::IsContainer(aChildTag);

  // Client of parser is spefically trying to parse a fragment that
  // may lack required context.  Suspend containment rules if so.
  if (mParserCommand != eViewFragment) {
    bool    theChildAgrees = true;
    int32_t theIndex = mBodyContext->GetCount();
    int32_t theParentContains = 0;

    do {
      eHTMLTags theParentTag = mBodyContext->TagAt(--theIndex);
      if (theParentTag == eHTMLTag_userdefined) {
        continue;
      }

      // Figure out whether this is a hidden input inside a
      // table/tbody/thead/tfoot/tr
      static eHTMLTags sTableElements[] = {
        eHTMLTag_table, eHTMLTag_thead, eHTMLTag_tbody,
        eHTMLTag_tr, eHTMLTag_tfoot
      };

      bool isHiddenInputInsideTableElement = false;
      if (aChildTag == eHTMLTag_input &&
          FindTagInSet(theParentTag, sTableElements,
                       ArrayLength(sTableElements))) {
        int32_t attrCount = aNode->GetAttributeCount();
        for (int32_t attrIndex = 0; attrIndex < attrCount; ++attrIndex) {
          const nsAString& key = aNode->GetKeyAt(attrIndex);
          if (key.LowerCaseEqualsLiteral("type")) {
            isHiddenInputInsideTableElement =
              ValueIsHidden(aNode->GetValueAt(attrIndex));
            break;
          }
        }
      }
      
      // Precompute containment, and pass it to CanOmit()...
      theParentContains =
        isHiddenInputInsideTableElement || CanContain(theParentTag, aChildTag);
      if (!isHiddenInputInsideTableElement &&
          CanOmit(theParentTag, aChildTag, theParentContains)) {
        HandleOmittedTag(aToken, aChildTag, theParentTag, aNode);
        return NS_OK;
      }

      eProcessRule theRule = eNormalOp;

      if (!theParentContains &&
          (IsBlockElement(aChildTag, theParentTag) &&
           IsInlineElement(theParentTag, theParentTag))) {
        // Don't test for table to fix 57554.
        if (eHTMLTag_li != aChildTag) {
          nsCParserNode* theParentNode = mBodyContext->PeekNode();
          if (theParentNode && theParentNode->mToken->IsWellFormed()) {
            theRule = eLetInlineContainBlock;
          }
        }
      }

      switch (theRule) {
        case eNormalOp:
          theChildAgrees = true;
          if (theParentContains) {
            eHTMLTags theAncestor = gHTMLElements[aChildTag].mRequiredAncestor;
            if (eHTMLTag_unknown != theAncestor) {
              theChildAgrees = HasOpenContainer(theAncestor);
            }

            if (theChildAgrees && theChildIsContainer) {
              if (theParentTag != aChildTag) {
                // Double check the power structure
                // Note: The bit is currently set on tags such as <A> and <LI>.
                if (gHTMLElements[aChildTag].ShouldVerifyHierarchy()) {
                  int32_t theChildIndex =
                    nsHTMLElement::GetIndexOfChildOrSynonym(*mBodyContext,
                                                            aChildTag);

                  if (kNotFound < theChildIndex && theChildIndex < theIndex) {
                    /*
                     * 1  Here's a tricky case from bug 22596:  <h5><li><h5>
                     *    How do we know that the 2nd <h5> should close the <LI>
                     *    rather than nest inside the <LI>? (Afterall, the <h5>
                     *    is a legal child of the <LI>).
                     *
                     *    The way you know is that there is no root between the
                     *    two, so the <h5> binds more tightly to the 1st <h5>
                     *    than to the <LI>.
                     *
                     * 2  Also, bug 6148 shows this case: <SPAN><DIV><SPAN>
                     *    From this case we learned not to execute this logic if
                     *    the parent is a block.
                     *
                     * 3  Fix for 26583:
                     *    <A href=foo.html><B>foo<A href-bar.html>bar</A></B></A>
                     *    In the above example clicking on "foo" or "bar" should
                     *    link to foo.html or bar.html respectively. That is,
                     *    the inner <A> should be informed about the presence of
                     *    an open <A> above <B>..so that the inner <A> can close
                     *    out the outer <A>. The following code does it for us.
                     *
                     * 4  Fix for 27865 [ similer to 22596 ]. Ex:
                     *    <DL><DD><LI>one<DD><LI>two
                     */
                    theChildAgrees = CanBeContained(aChildTag, *mBodyContext);
                  }
                }
              }
            }
          }

          if (!(theParentContains && theChildAgrees)) {
            if (!CanPropagate(theParentTag, aChildTag, theParentContains)) {
              if (theChildIsContainer || !theParentContains) {
                if (!theChildAgrees &&
                    !gHTMLElements[aChildTag].CanAutoCloseTag(*mBodyContext,
                                                              theIndex,
                                                              aChildTag)) {
                  // Closing the tags above might cause non-compatible results.
                  // Ex. <TABLE><TR><TD><TBODY>Text</TD></TR></TABLE>.
                  // In the example above <TBODY> is badly misplaced, but
                  // we should not attempt to close the tags above it,
                  // The safest thing to do is to discard this tag.
                  // XXX We get the above example wrong anyway because of
                  // CanBeContained.
                  return result;
                } else if (mBodyContext->mContextTopIndex > 0 &&
                           theIndex <= mBodyContext->mContextTopIndex) {
                  // Looks like the parent tag does not want to contain the
                  // current tag ( aChildTag ).  However, we have to force the
                  // containment, when handling misplaced content, to avoid data
                  // loss.  Ref. bug 138577.
                  theParentContains = true;
                } else {
                  CloseContainersTo(theIndex, aChildTag, true);
                }
              } else {
                break;
              }
            } else {
              CreateContextStackFor(theParentTag, aChildTag);
              theIndex = mBodyContext->GetCount();
            }
          }
          break;

        case eLetInlineContainBlock:
          // Break out of this loop and open the block.
          theParentContains = theChildAgrees = true;
          break;

        default:
          NS_NOTREACHED("Invalid rule detected");
      }
    } while (!(theParentContains && theChildAgrees));
  }

  if (theChildIsContainer) {
    result = OpenContainer(aNode, aChildTag);
  } else {
    result = AddLeaf(aNode);
  }

  return result;
}

nsresult
CNavDTD::WillHandleStartTag(CToken* aToken, eHTMLTags aTag,
                            nsIParserNode& aNode)
{
  nsresult result = NS_OK;

  int32_t stackDepth = mBodyContext->GetCount();
  if (stackDepth >= FONTSTYLE_IGNORE_DEPTH &&
      gHTMLElements[aTag].IsMemberOf(kFontStyle)) {
    // Prevent bug 58917 by tossing the new kFontStyle start tag
    return kHierarchyTooDeep;
  }

  if (stackDepth >= PHRASE_IGNORE_DEPTH &&
      gHTMLElements[aTag].IsMemberOf(kPhrase)) {
    // Prevent bug 58917 by tossing the new kPhrase start tag
    return kHierarchyTooDeep;
  }

  /*
   * Now a little code to deal with bug #49687 (crash when layout stack gets
   * too deep) I've also opened this up to any container (not just inlines):
   * re bug 55095 Improved to handle bug 55980 (infinite loop caused when
   * DEPTH is exceeded and </P> is encountered by itself (<P>) is continuously
   * produced.
   */
  if (stackDepth > MAX_REFLOW_DEPTH) {
    if (nsHTMLElement::IsContainer(aTag) &&
        !gHTMLElements[aTag].HasSpecialProperty(kHandleStrayTag)) {
      // Ref. bug 98261,49678,55095,55980
      // Instead of throwing away the current tag close it's parent
      // such that the stack level does not go beyond the max_reflow_depth.
      // This would allow leaf tags, that follow the current tag, to find
      // the correct node.
      while (stackDepth != MAX_REFLOW_DEPTH && NS_SUCCEEDED(result)) {
        result = CloseContainersTo(mBodyContext->Last(), false);
        --stackDepth;
      }
    }
  }

  return result;
}

static void
PushMisplacedAttributes(nsIParserNode& aNode, nsDeque& aDeque)
{
  nsCParserNode& theAttrNode = static_cast<nsCParserNode &>(aNode);

  for (int32_t count = aNode.GetAttributeCount(); count > 0; --count) {
    CToken* theAttrToken = theAttrNode.PopAttributeTokenFront();
    if (theAttrToken) {
      theAttrToken->SetNewlineCount(0);
      aDeque.Push(theAttrToken);
    }
  }
}

void
CNavDTD::HandleOmittedTag(CToken* aToken, eHTMLTags aChildTag,
                          eHTMLTags aParent, nsIParserNode* aNode)
{
  NS_PRECONDITION(mBodyContext != nullptr, "need a context to work with");

  // The trick here is to see if the parent can contain the child, but prefers
  // not to. Only if the parent CANNOT contain the child should we look to see
  // if it's potentially a child of another section. If it is, the cache it for
  // later.
  int32_t theTagCount = mBodyContext->GetCount();
  bool pushToken = false;

  if (gHTMLElements[aParent].HasSpecialProperty(kBadContentWatch) &&
      !nsHTMLElement::IsWhitespaceTag(aChildTag)) {
    eHTMLTags theTag = eHTMLTag_unknown;

    // Don't bother saving misplaced stuff in the head. This can happen in
    // cases like |<head><noscript><table>foo|. See bug 401169.
    if (mFlags & NS_DTD_FLAG_HAS_OPEN_HEAD) {
      NS_ASSERTION(!(mFlags & NS_DTD_FLAG_HAS_MAIN_CONTAINER),
                   "Bad state");
      return;
    }

    // Determine the insertion point
    while (theTagCount > 0) {
      theTag = mBodyContext->TagAt(--theTagCount);
      if (!gHTMLElements[theTag].HasSpecialProperty(kBadContentWatch)) {
        // This is our insertion point.
        mBodyContext->mContextTopIndex = theTagCount;
        break;
      }
    }

    if (mBodyContext->mContextTopIndex > -1) {
      pushToken = true;

      // Remember that we've stashed some misplaced content.
      mFlags |= NS_DTD_FLAG_MISPLACED_CONTENT;
    }
  }

  if (aChildTag != aParent &&
      gHTMLElements[aParent].HasSpecialProperty(kSaveMisplaced)) {
    NS_ASSERTION(!pushToken, "A strange element has both kBadContentWatch "
                             "and kSaveMisplaced");
    pushToken = true;
  }

  if (pushToken) {
    // Hold on to this token for later use. Ref Bug. 53695
    IF_HOLD(aToken);
    PushIntoMisplacedStack(aToken);

    // If the token is attributed then save those attributes too.
    PushMisplacedAttributes(*aNode, mMisplacedContent);
  }
}

/**
 *  This method gets called when a kegen token is found.
 *   
 *  @update  harishd 05/02/00
 *  @param   aNode -- CParserNode representing keygen
 *  @return  NS_OK if all went well; ERROR if error occurred
 */
nsresult
CNavDTD::HandleKeyGen(nsIParserNode* aNode)
{
  nsresult result = NS_OK;

  nsCOMPtr<nsIFormProcessor> theFormProcessor =
           do_GetService(kFormProcessorCID, &result);
  if (NS_FAILED(result)) {
    return result;
  }

  int32_t      theAttrCount = aNode->GetAttributeCount();
  nsTArray<nsString> theContent;
  nsAutoString theAttribute;
  nsAutoString theFormType;
  CToken*      theToken = nullptr;

  theFormType.AssignLiteral("select");

  result = theFormProcessor->ProvideContent(theFormType, theContent,
                                            theAttribute);
  if (NS_FAILED(result)) {
    return result;
  }
  int32_t   theIndex = 0;

  // Populate the tokenizer with the fabricated elements in the reverse
  // order such that <SELECT> is on the top fo the tokenizer followed by
  // <OPTION>s and </SELECT>.
  theToken = mTokenAllocator->CreateTokenOfType(eToken_end,
                                                eHTMLTag_select);
  NS_ENSURE_TRUE(theToken, NS_ERROR_OUT_OF_MEMORY);
  mTokenizer->PushTokenFront(theToken);

  for (theIndex = theContent.Length()-1; theIndex > -1; --theIndex) {
    theToken = mTokenAllocator->CreateTokenOfType(eToken_text,
                                                  eHTMLTag_text,
                                                  theContent[theIndex]);
    NS_ENSURE_TRUE(theToken, NS_ERROR_OUT_OF_MEMORY);
    mTokenizer->PushTokenFront(theToken);

    theToken = mTokenAllocator->CreateTokenOfType(eToken_start,
                                                  eHTMLTag_option);
    NS_ENSURE_TRUE(theToken, NS_ERROR_OUT_OF_MEMORY);
    mTokenizer->PushTokenFront(theToken);
  }

  // The attribute ( provided by the form processor ) should be a part
  // of the SELECT.  Placing the attribute token on the tokenizer to get
  // picked up by the SELECT.
  theToken = mTokenAllocator->CreateTokenOfType(eToken_attribute,
                                                eHTMLTag_unknown,
                                                theAttribute);
  NS_ENSURE_TRUE(theToken, NS_ERROR_OUT_OF_MEMORY);

  ((CAttributeToken*)theToken)->SetKey(NS_LITERAL_STRING("_moz-type"));
  mTokenizer->PushTokenFront(theToken);

  // Pop out NAME and CHALLENGE attributes ( from the keygen NODE ) and
  // place it in the tokenizer such that the attribtues get sucked into
  // SELECT node.
  for (theIndex = theAttrCount; theIndex > 0; --theIndex) {
    mTokenizer->PushTokenFront(((nsCParserNode*)aNode)->PopAttributeToken());
  }

  theToken = mTokenAllocator->CreateTokenOfType(eToken_start,
                                                eHTMLTag_select);
  NS_ENSURE_TRUE(theToken, NS_ERROR_OUT_OF_MEMORY);

  // Increment the count because of the additional attribute from the form processor.
  theToken->SetAttributeCount(theAttrCount + 1);
  mTokenizer->PushTokenFront(theToken);

  return result;
}

bool
CNavDTD::IsAlternateTag(eHTMLTags aTag)
{
  switch (aTag) {
    case eHTMLTag_noembed:
      return true;

    case eHTMLTag_noscript:
      return (mFlags & NS_IPARSER_FLAG_SCRIPT_ENABLED) != 0;

    case eHTMLTag_iframe:
    case eHTMLTag_noframes:
      return (mFlags & NS_IPARSER_FLAG_FRAMES_ENABLED) != 0;

    default:
      return false;
  }
}

nsresult
CNavDTD::HandleStartToken(CToken* aToken)
{
  NS_PRECONDITION(nullptr != aToken, kNullToken);

  nsCParserNode* theNode = mNodeAllocator.CreateNode(aToken, mTokenAllocator);
  NS_ENSURE_TRUE(theNode, NS_ERROR_OUT_OF_MEMORY);

  eHTMLTags     theChildTag = (eHTMLTags)aToken->GetTypeID();
  int16_t       attrCount   = aToken->GetAttributeCount();
  eHTMLTags     theParent   = mBodyContext->Last();
  nsresult      result      = NS_OK;

  if (attrCount > 0) {
    result = CollectAttributes(theNode, theChildTag, attrCount);
  }

  if (NS_OK == result) {
    result = WillHandleStartTag(aToken, theChildTag, *theNode);
    if (NS_OK == result) {
      bool isTokenHandled = false;
      bool theHeadIsParent = false;

      if (nsHTMLElement::IsSectionTag(theChildTag)) {
        switch (theChildTag) {
          case eHTMLTag_html:
            if (mBodyContext->GetCount() > 0) {
              result = OpenContainer(theNode, theChildTag);
              isTokenHandled = true;
            }
            break;

          case eHTMLTag_body:
            if (mFlags & NS_DTD_FLAG_HAS_OPEN_BODY) {
              result = OpenContainer(theNode, theChildTag);
              isTokenHandled=true;
            }
            break;

          case eHTMLTag_head:
            mFlags |= NS_DTD_FLAG_HAS_EXPLICIT_HEAD;

            if (mFlags & NS_DTD_FLAG_HAS_MAIN_CONTAINER) {
              HandleOmittedTag(aToken, theChildTag, theParent, theNode);
              isTokenHandled = true;
            }
            break;

          default:
            break;
        }
      }

      bool isExclusive = false;
      theHeadIsParent = nsHTMLElement::IsChildOfHead(theChildTag, isExclusive);

      switch (theChildTag) {
        case eHTMLTag_area:
          if (!mOpenMapCount) {
            isTokenHandled = true;
          }

          if (mOpenMapCount > 0 && mSink) {
            result = mSink->AddLeaf(*theNode);
            isTokenHandled = true;
          }
	  
          break;

        case eHTMLTag_image:
          aToken->SetTypeID(theChildTag = eHTMLTag_img);
          break;

        case eHTMLTag_keygen:
          result = HandleKeyGen(theNode);
          isTokenHandled = true;
          break;

        case eHTMLTag_script:
          // Script isn't really exclusively in the head. However, we treat it
          // as such to make sure that we don't pull scripts outside the head
          // into the body.
          // XXX Where does the script go in a frameset document?
          isExclusive = !(mFlags & NS_DTD_FLAG_HAD_BODY);
          break;

        default:;
      }

      if (!isTokenHandled) {
        bool prefersBody =
          gHTMLElements[theChildTag].HasSpecialProperty(kPreferBody);

        // If this tag prefers to be in the head (when neither the head nor the
        // body have been explicitly opened) then check that we haven't seen the
        // body (true until the <body> tag has really been seen). Otherwise,
        // check if the head has been explicitly opened. See bug 307122.
        theHeadIsParent = theHeadIsParent &&
          (isExclusive ||
           (prefersBody
            ? (mFlags & NS_DTD_FLAG_HAS_EXPLICIT_HEAD) &&
              (mFlags & NS_DTD_FLAG_HAS_OPEN_HEAD)
            : !(mFlags & NS_DTD_FLAG_HAS_MAIN_CONTAINER)));

        if (theHeadIsParent) {
          // These tokens prefer to be in the head.
          result = AddHeadContent(theNode);
        } else {
          result = HandleDefaultStartToken(aToken, theChildTag, theNode);
        }
      }

      // Now do any post processing necessary on the tag...
      if (NS_OK == result) {
        DidHandleStartTag(*theNode, theChildTag);
      }
    }
  }

  if (kHierarchyTooDeep == result) {
    // Reset this error to ok; all that happens here is that given inline tag
    // gets dropped because the stack is too deep. Don't terminate parsing.
    result = NS_OK;
  }

  IF_FREE(theNode, &mNodeAllocator);
  return result;
}

/**
 *  Call this to see if you have a closeable peer on the stack that
 *  is ABOVE one of its root tags.
 *
 *  @update  gess 4/11/99
 *  @param   aRootTagList -- list of root tags for aTag
 *  @param   aTag -- tag to test for containership
 *  @return  true if given tag can contain other tags
 */
static bool
HasCloseablePeerAboveRoot(const TagList& aRootTagList, nsDTDContext& aContext,
                          eHTMLTags aTag, bool anEndTag)
{
  int32_t  theRootIndex = LastOf(aContext, aRootTagList);
  const TagList* theCloseTags = anEndTag
                                ? gHTMLElements[aTag].GetAutoCloseEndTags()
                                : gHTMLElements[aTag].GetAutoCloseStartTags();
  int32_t theChildIndex = -1;

  if (theCloseTags) {
    theChildIndex=LastOf(aContext, *theCloseTags);
  } else if (anEndTag || !gHTMLElements[aTag].CanContainSelf()) {
    theChildIndex = aContext.LastOf(aTag);
  }

  // I changed this to theRootIndex<=theChildIndex so to handle this case:
  //  <SELECT><OPTGROUP>...</OPTGROUP>
  return theRootIndex<=theChildIndex;
}


/**
 *  This method is called to determine whether or not an END tag
 *  can be autoclosed. This means that based on the current
 *  context, the stack should be closed to the nearest matching
 *  tag.
 *
 *  @param   aTag -- tag enum of child to be tested
 *  @return  true if autoclosure should occur
 */
static eHTMLTags
FindAutoCloseTargetForEndTag(eHTMLTags aCurrentTag, nsDTDContext& aContext,
                             nsDTDMode aMode)
{
  int theTopIndex = aContext.GetCount();
  eHTMLTags thePrevTag = aContext.Last();

  if (nsHTMLElement::IsContainer(aCurrentTag)) {
    int32_t theChildIndex =
      nsHTMLElement::GetIndexOfChildOrSynonym(aContext, aCurrentTag);

    if (kNotFound < theChildIndex) {
      if (thePrevTag == aContext[theChildIndex]) {
        return aContext[theChildIndex];
      }

      if (nsHTMLElement::IsBlockCloser(aCurrentTag)) {
        /*
         * Here's the algorithm:
         * Our here is sitting at aChildIndex. There are other tags above it
         * on the stack. We have to try to close them out, but we may encounter
         * one that can block us. The way to tell is by comparing each tag on
         * the stack against our closeTag and rootTag list.
         *
         * For each tag above our hero on the stack, ask 3 questions:
         * 1. Is it in the closeTag list? If so, the we can skip over it
         * 2. Is it in the rootTag list? If so, then we're gated by it
         * 3. Otherwise its non-specified and we simply presume we can close it.
         */
        const TagList* theCloseTags =
          gHTMLElements[aCurrentTag].GetAutoCloseEndTags();
        const TagList* theRootTags =
          gHTMLElements[aCurrentTag].GetEndRootTags();

        if (theCloseTags) {
          // At a mininimum, this code is needed for H1..H6
          while (theChildIndex < --theTopIndex) {
            eHTMLTags theNextTag = aContext[theTopIndex];
            if (!FindTagInSet(theNextTag, theCloseTags->mTags,
                              theCloseTags->mCount) &&
                FindTagInSet(theNextTag, theRootTags->mTags,
                             theRootTags->mCount)) {
              // We encountered a tag in root list so fail (we're gated).
              return eHTMLTag_unknown;
            }

            // Otherwise, presume it's something we can simply ignore and
            // continue searching.
          }

          eHTMLTags theTarget = aContext.TagAt(theChildIndex);
          if (aCurrentTag != theTarget) {
            aCurrentTag = theTarget;
          }
          // If you make it here, we're ungated and found a target!
          return aCurrentTag;
        } else if (theRootTags) {
          // Since we didn't find any close tags, see if there is an instance of
          // aCurrentTag above the stack from the roottag.
          if (HasCloseablePeerAboveRoot(*theRootTags, aContext, aCurrentTag,
                                        true)) {
            return aCurrentTag;
          } else {
            return eHTMLTag_unknown;
          }
        }
      } else {
        // Ok, a much more sensible approach for non-block closers; use the tag
        // group to determine closure: For example: %phrasal closes %phrasal,
        // %fontstyle and %special
        return gHTMLElements[aCurrentTag].GetCloseTargetForEndTag(aContext,
                                                                  theChildIndex,
                                                                  aMode);
      }
    }
  }

  return eHTMLTag_unknown;
}

static void
StripWSFollowingTag(eHTMLTags aChildTag, nsITokenizer* aTokenizer,
                    nsTokenAllocator* aTokenAllocator, int32_t* aNewlineCount)
{
  if (!aTokenizer || !aTokenAllocator) {
    return;
  }

  CToken* theToken = aTokenizer->PeekToken();

  int32_t newlineCount = 0;
  while (theToken) {
    eHTMLTokenTypes theType = eHTMLTokenTypes(theToken->GetTokenType());

    switch(theType) {
      case eToken_newline:
      case eToken_whitespace:
        theToken = aTokenizer->PopToken();
        newlineCount += theToken->GetNewlineCount();
        IF_FREE(theToken, aTokenAllocator);

        theToken = aTokenizer->PeekToken();
        break;

      default:
        theToken = nullptr;
        break;
    }
  }

  if (aNewlineCount) {
    *aNewlineCount += newlineCount;
  }
}

/**
 *  This method gets called when an end token has been 
 *  encountered in the parse process. If the end tag matches
 *  the start tag on the stack, then simply close it. Otherwise,
 *  we have a erroneous state condition. This can be because we
 *  have a close tag with no prior open tag (user error) or because
 *  we screwed something up in the parse process. I'm not sure
 *  yet how to tell the difference.
 *  
 *  @param   aToken -- next (start) token to be handled
 *  @return  true if all went well; false if error occurred
 */
nsresult
CNavDTD::HandleEndToken(CToken* aToken)
{
  NS_PRECONDITION(nullptr != aToken, kNullToken);

  nsresult    result = NS_OK;
  eHTMLTags   theChildTag = (eHTMLTags)aToken->GetTypeID();

  // Begin by dumping any attributes (bug 143512)
  CollectAttributes(nullptr, theChildTag, aToken->GetAttributeCount());

  switch (theChildTag) {
    case eHTMLTag_link:
    case eHTMLTag_meta:
      break;

    case eHTMLTag_head:
      StripWSFollowingTag(theChildTag, mTokenizer, mTokenAllocator,
                          !mCountLines ? nullptr : &mLineNumber);
      if (mBodyContext->LastOf(eHTMLTag_head) != kNotFound) {
        result = CloseContainersTo(eHTMLTag_head, false);
      }
      mFlags &= ~NS_DTD_FLAG_HAS_EXPLICIT_HEAD;
      break;

    case eHTMLTag_form:
      result = CloseContainer(eHTMLTag_form, false);
      break;

    case eHTMLTag_br:
      {
        // This is special NAV-QUIRKS code that allows users to use </BR>, even
        // though that isn't a legitimate tag.
        if (eDTDMode_quirks == mDTDMode) {
          // Use recycler and pass the token thro' HandleToken() to fix bugs
          // like 32782.
          CToken* theToken = mTokenAllocator->CreateTokenOfType(eToken_start,
                                                                theChildTag);
          result = HandleToken(theToken);
        }
      }
      break;

    case eHTMLTag_body:
    case eHTMLTag_html:
      StripWSFollowingTag(theChildTag, mTokenizer, mTokenAllocator,
                          !mCountLines ? nullptr : &mLineNumber);
      break;

    case eHTMLTag_script:
      // Note: we don't fall through to the default case because
      // CloseContainersTo() has the bad habit of closing tags that are opened
      // by document.write(). Fortunately, the tokenizer guarantees that no
      // actual tags appear between <script> and </script> so we won't be
      // closing the wrong tag.
      if (mBodyContext->Last() != eHTMLTag_script) {
        // Except if we're here, then there's probably a stray script tag.
        NS_ASSERTION(mBodyContext->LastOf(eHTMLTag_script) == kNotFound,
                     "Mishandling scripts in CNavDTD");
        break;
      }

      mBodyContext->Pop();
      result = CloseContainer(eHTMLTag_script, aToken->IsInError());
      break;

    default:
     {
        // Now check to see if this token should be omitted, or
        // if it's gated from closing by the presence of another tag.
        if (gHTMLElements[theChildTag].CanOmitEndTag()) {
          PopStyle(theChildTag);
        } else {
          eHTMLTags theParentTag = mBodyContext->Last();

          // First open transient styles, so that we see any autoclosed style
          // tags.
          if (nsHTMLElement::IsResidualStyleTag(theChildTag)) {
            result = OpenTransientStyles(theChildTag);
            if (NS_FAILED(result)) {
              return result;
            }
          }

          if (kNotFound ==
                nsHTMLElement::GetIndexOfChildOrSynonym(*mBodyContext,
                                                        theChildTag)) {
            // Ref: bug 30487
            // Make sure that we don't cross boundaries, of certain elements,
            // to close stylistic information.
            // <font face="helvetica"><table><tr><td></font></td></tr></table> some text...
            // In the above ex. the orphaned FONT tag, inside TD, should cross
            // TD boundary to close the FONT tag above TABLE.
            static eHTMLTags gBarriers[] = {
              eHTMLTag_thead, eHTMLTag_tbody, eHTMLTag_tfoot, eHTMLTag_table
            };

            if (!FindTagInSet(theParentTag, gBarriers,
                              ArrayLength(gBarriers)) &&
                nsHTMLElement::IsResidualStyleTag(theChildTag)) {
              // Fix bug 77746
              mBodyContext->RemoveStyle(theChildTag);
            }

            // If the bit kHandleStrayTag is set then we automatically open up a
            // matching start tag (compatibility).  Currently this bit is set on
            // P tag.  This also fixes Bug: 22623
            if (gHTMLElements[theChildTag].HasSpecialProperty(kHandleStrayTag) &&
                mDTDMode != eDTDMode_full_standards &&
                mDTDMode != eDTDMode_almost_standards) {
              // Oh boy!! we found a "stray" tag. Nav4.x and IE introduce line
              // break in such cases. So, let's simulate that effect for
              // compatibility.
              // Ex. <html><body>Hello</P>There</body></html>
              int32_t theParentContains = -1;
              if (!CanOmit(theParentTag, theChildTag, theParentContains)) {
                CToken* theStartToken =
                  mTokenAllocator->CreateTokenOfType(eToken_start, theChildTag);
                NS_ENSURE_TRUE(theStartToken, NS_ERROR_OUT_OF_MEMORY);

                // This check for NS_DTD_FLAG_IN_MISPLACED_CONTENT was added
                // to fix bug 142965.
                if (!(mFlags & NS_DTD_FLAG_IN_MISPLACED_CONTENT)) {
                  // We're not handling misplaced content right now, just push
                  // these new tokens back on the stack and handle them in the
                  // regular flow of HandleToken.
                  IF_HOLD(aToken);
                  mTokenizer->PushTokenFront(aToken);
                  mTokenizer->PushTokenFront(theStartToken);
                } else {
                  // Oops, we're in misplaced content. Handle these tokens
                  // directly instead of trying to push them onto the tokenizer
                  // stack.
                  result = HandleToken(theStartToken);
                  NS_ENSURE_SUCCESS(result, result);

                  IF_HOLD(aToken);
                  result = HandleToken(aToken);
                }
              }
            }
            return result;
          }
          if (result == NS_OK) {
            eHTMLTags theTarget =
              FindAutoCloseTargetForEndTag(theChildTag, *mBodyContext,
                                           mDTDMode);
            if (eHTMLTag_unknown != theTarget) {
              result = CloseContainersTo(theTarget, false);
            }
          }
        }
      }
      break;
  }

  return result;
}

/**
 * This method will be triggered when the end of a table is
 * encountered.  Its primary purpose is to process all the
 * bad-contents pertaining a particular table. The position
 * of the table is the token bank ID.
 *
 * @update harishd 03/24/99
 * @param  aTag - This ought to be a table tag
 *
 */
nsresult
CNavDTD::HandleSavedTokens(int32_t anIndex)
{
  NS_PRECONDITION(mBodyContext != nullptr && mBodyContext->GetCount() > 0, "invalid context");

  nsresult  result = NS_OK;

  if (mSink && (anIndex > kNotFound)) {
    int32_t theBadTokenCount = mMisplacedContent.GetSize();

    if (theBadTokenCount > 0) {
      mFlags |= NS_DTD_FLAG_IN_MISPLACED_CONTENT;

      if (!mTempContext && !(mTempContext = new nsDTDContext())) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      CToken*   theToken;
      eHTMLTags theTag;
      int32_t   attrCount;
      int32_t   theTopIndex = anIndex + 1;
      int32_t   theTagCount = mBodyContext->GetCount();

      // Pause the main context and switch to the new context.
      result = mSink->BeginContext(anIndex);
      
      NS_ENSURE_SUCCESS(result, result);

      // The body context should contain contents only upto the marked position.
      mBodyContext->MoveEntries(*mTempContext, theTagCount - theTopIndex);

      // Now flush out all the bad contents.
      while (theBadTokenCount-- > 0){
        theToken = (CToken*)mMisplacedContent.PopFront();
        if (theToken) {
          theTag       = (eHTMLTags)theToken->GetTypeID();
          attrCount    = theToken->GetAttributeCount();
          // Put back attributes, which once got popped out, into the
          // tokenizer.  Make sure we preserve their ordering, however!
          // XXXbz would it be faster to get the tokens out with ObjectAt and
          // put them in the tokenizer and then PopFront them all from
          // mMisplacedContent?
          nsDeque temp;
          for (int32_t j = 0; j < attrCount; ++j) {
            CToken* theAttrToken = (CToken*)mMisplacedContent.PopFront();
            if (theAttrToken) {
              temp.Push(theAttrToken);
            }
            theBadTokenCount--;
          }
          mTokenizer->PrependTokens(temp);

          if (eToken_end == theToken->GetTokenType()) {
            // Ref: Bug 25202
            // Make sure that the BeginContext() is ended only by the call to
            // EndContext(). Ex: <center><table><a></center>.
            // In the Ex. above </center> should not close <center> above table.
            // Doing so will cause the current context to get closed prematurely.
            eHTMLTags closed = FindAutoCloseTargetForEndTag(theTag, *mBodyContext,
                                                            mDTDMode);
            int32_t theIndex = closed != eHTMLTag_unknown
                               ? mBodyContext->LastOf(closed)
                               : kNotFound;

            if (theIndex != kNotFound &&
                theIndex <= mBodyContext->mContextTopIndex) {
              IF_FREE(theToken, mTokenAllocator);
              continue;
            }
          }

          // XXX This should go away, with this call, it becomes extremely
          // difficult to handle misplaced style and link tags, since it's hard
          // to propagate the block return all the way up and then re-enter this
          // method.
          result = HandleToken(theToken);
        }
      }

      if (theTopIndex != mBodyContext->GetCount()) {
        // CloseContainersTo does not close any forms we might have opened while
        // handling saved tokens, because the parser does not track forms on its
        // mBodyContext stack.
        CloseContainersTo(theTopIndex, mBodyContext->TagAt(theTopIndex),
                          true);
      }      

      // Bad-contents were successfully processed. Now, itz time to get
      // back to the original body context state.
      mTempContext->MoveEntries(*mBodyContext, theTagCount - theTopIndex);

      // Terminate the new context and switch back to the main context
      mSink->EndContext(anIndex);

      mFlags &= ~NS_DTD_FLAG_IN_MISPLACED_CONTENT;
    }
  }
  return result;
}


/**
 *  This method gets called when an entity token has been 
 *  encountered in the parse process. 
 *  
 *  @update  gess 3/25/98
 *  @param   aToken -- next (start) token to be handled
 *  @return  true if all went well; false if error occurred
 */
nsresult
CNavDTD::HandleEntityToken(CToken* aToken)
{
  NS_PRECONDITION(nullptr != aToken, kNullToken);

  nsresult  result = NS_OK;

  const nsSubstring& theStr = aToken->GetStringValue();

  if (kHashsign != theStr.First() &&
      -1 == nsHTMLEntities::EntityToUnicode(theStr)) {
    // If you're here we have a bogus entity.
    // Convert it into a text token.
    CToken *theToken;

    nsAutoString entityName;
    entityName.AssignLiteral("&");
    entityName.Append(theStr);
    theToken = mTokenAllocator->CreateTokenOfType(eToken_text, eHTMLTag_text,
                                                  entityName);
    NS_ENSURE_TRUE(theToken, NS_ERROR_OUT_OF_MEMORY);

    // theToken should get recycled automagically...
    return HandleToken(theToken);
  }

  eHTMLTags theParentTag = mBodyContext->Last();
  nsCParserNode* theNode = mNodeAllocator.CreateNode(aToken, mTokenAllocator);
  NS_ENSURE_TRUE(theNode, NS_ERROR_OUT_OF_MEMORY);

  int32_t theParentContains = -1;
  if (CanOmit(theParentTag, eHTMLTag_entity, theParentContains)) {
    eHTMLTags theCurrTag = (eHTMLTags)aToken->GetTypeID();
    HandleOmittedTag(aToken, theCurrTag, theParentTag, theNode);
  } else {
    result = AddLeaf(theNode);
  }
  IF_FREE(theNode, &mNodeAllocator);
  return result;
}

/**
 *  This method gets called when a comment token has been 
 *  encountered in the parse process. After making sure
 *  we're somewhere in the body, we handle the comment
 *  in the same code that we use for text.
 *  
 *  @update  gess 3/25/98
 *  @param   aToken -- next (start) token to be handled
 *  @return  true if all went well; false if error occurred
 */
nsresult
CNavDTD::HandleCommentToken(CToken* aToken)
{
  NS_PRECONDITION(nullptr != aToken, kNullToken);
  return NS_OK;
}


/**
 *  This method gets called when an attribute token has been
 *  encountered in the parse process. This is an error, since
 *  all attributes should have been accounted for in the prior
 *  start or end tokens
 *
 *  @update  gess 3/25/98
 *  @param   aToken -- next (start) token to be handled
 *  @return  true if all went well; false if error occurred
 */
nsresult
CNavDTD::HandleAttributeToken(CToken* aToken)
{
  NS_ERROR("attribute encountered -- this shouldn't happen.");
  return NS_OK;
}

/**
 *  This method gets called when an "instruction" token has been 
 *  encountered in the parse process. 
 *  
 *  @update  gess 3/25/98
 *  @param   aToken -- next (start) token to be handled
 *  @return  true if all went well; false if error occurred
 */
nsresult
CNavDTD::HandleProcessingInstructionToken(CToken* aToken)
{
  NS_PRECONDITION(nullptr != aToken, kNullToken);
  return NS_OK;
}

/**
 *  This method gets called when a DOCTYPE token has been 
 *  encountered in the parse process. 
 *  
 *  @update  harishd 09/02/99
 *  @param   aToken -- The very first token to be handled
 *  @return  true if all went well; false if error occurred
 */
nsresult
CNavDTD::HandleDocTypeDeclToken(CToken* aToken)
{
  NS_PRECONDITION(nullptr != aToken, kNullToken);

  CDoctypeDeclToken* theToken = static_cast<CDoctypeDeclToken*>(aToken);
  nsAutoString docTypeStr(theToken->GetStringValue());
  // XXX Doesn't this count the newlines twice?
  if (mCountLines) {
    mLineNumber += docTypeStr.CountChar(kNewLine);
  }

  int32_t len = docTypeStr.Length();
  int32_t pos = docTypeStr.RFindChar(kGreaterThan);
  if (pos != kNotFound) {
    // First remove '>' from the end.
    docTypeStr.Cut(pos, len - pos);
  }

  // Now remove "<!" from the begining
  docTypeStr.Cut(0, 2);
  theToken->SetStringValue(docTypeStr);
  return NS_OK;
}

/**
 * Retrieve the attributes for this node, and add then into
 * the node.
 *
 * @update  gess4/22/98
 * @param   aNode is the node you want to collect attributes for
 * @param   aCount is the # of attributes you're expecting
 * @return error code (should be 0)
 */
nsresult
CNavDTD::CollectAttributes(nsIParserNode *aNode, eHTMLTags aTag, int32_t aCount)
{
  int attr = 0;
  nsresult result = NS_OK;
  int theAvailTokenCount = mTokenizer->GetCount();

  if (aCount <= theAvailTokenCount) {
    CToken* theToken;
    for (attr = 0; attr < aCount; ++attr) {
      theToken = mTokenizer->PopToken();
      if (theToken) {
        eHTMLTokenTypes theType = eHTMLTokenTypes(theToken->GetTokenType());
        if (theType != eToken_attribute) {
          // If you're here then it means that the token does not
          // belong to this node. Put the token back into the tokenizer
          // and let it go thro' the regular path. Bug: 59189.
          mTokenizer->PushTokenFront(theToken);
          break;
        }

        if (mCountLines) {
          mLineNumber += theToken->GetNewlineCount();
        }

        if (aNode) {
          // If the key is empty, the attribute is unusable, so we should not
          // add it to the node.
          if (!((CAttributeToken*)theToken)->GetKey().IsEmpty()) {
            aNode->AddAttribute(theToken);
          } else {
            IF_FREE(theToken, mTokenAllocator);
          }
        } else {
          IF_FREE(theToken, mTokenAllocator);
        }
      }
    }
  } else {
    result = kEOF;
  }
  return result;
}

/** 
 *  This method is called to determine whether or not a tag
 *  of one type can contain a tag of another type.
 *  
 *  @update  gess 4/8/98
 *  @param   aParent -- tag enum of parent container
 *  @param   aChild -- tag enum of child container
 *  @return  true if parent can contain child
 */
NS_IMETHODIMP_(bool)
CNavDTD::CanContain(int32_t aParent, int32_t aChild) const
{
  bool result = gHTMLElements[aParent].CanContain((eHTMLTags)aChild, mDTDMode);

  if (eHTMLTag_nobr == aChild &&
      IsInlineElement(aParent, aParent) &&
      HasOpenContainer(eHTMLTag_nobr)) {
    return false;
  }

  return result;
}

/**
 *  This method is called to determine whether or not
 *  the given childtag is a block element.
 *
 *  @update  gess 6June2000
 *  @param   aChildID -- tag id of child 
 *  @param   aParentID -- tag id of parent (or eHTMLTag_unknown)
 *  @return  true if this tag is a block tag
 */
bool
CNavDTD::IsBlockElement(int32_t aTagID, int32_t aParentID) const
{
  eHTMLTags theTag = (eHTMLTags)aTagID;

  return (theTag > eHTMLTag_unknown && theTag < eHTMLTag_userdefined) &&
          (gHTMLElements[theTag].IsMemberOf(kBlock)        ||
           gHTMLElements[theTag].IsMemberOf(kBlockEntity)  ||
           gHTMLElements[theTag].IsMemberOf(kHeading)      ||
           gHTMLElements[theTag].IsMemberOf(kPreformatted) ||
           gHTMLElements[theTag].IsMemberOf(kList));
}

/**
 *  This method is called to determine whether or not
 *  the given childtag is an inline element.
 *
 *  @update  gess 6June2000
 *  @param   aChildID -- tag id of child 
 *  @param   aParentID -- tag id of parent (or eHTMLTag_unknown)
 *  @return  true if this tag is an inline tag
 */
bool
CNavDTD::IsInlineElement(int32_t aTagID, int32_t aParentID) const
{
  eHTMLTags theTag = (eHTMLTags)aTagID;

  return (theTag > eHTMLTag_unknown && theTag < eHTMLTag_userdefined) &&
          (gHTMLElements[theTag].IsMemberOf(kInlineEntity) ||
           gHTMLElements[theTag].IsMemberOf(kFontStyle)    ||
           gHTMLElements[theTag].IsMemberOf(kPhrase)       ||
           gHTMLElements[theTag].IsMemberOf(kSpecial)      ||
           gHTMLElements[theTag].IsMemberOf(kFormControl));
}

/**
 *  This method is called to determine whether or not
 *  the necessary intermediate tags should be propagated
 *  between the given parent and given child.
 *
 *  @update  gess 4/8/98
 *  @param   aParent -- tag enum of parent container
 *  @param   aChild -- tag enum of child container
 *  @return  true if propagation should occur
 */
bool
CNavDTD::CanPropagate(eHTMLTags aParent, eHTMLTags aChild,
                      int32_t aParentContains)
{
  bool result = false;
  if (aParentContains == -1) {
    aParentContains = CanContain(aParent, aChild);
  }

  if (aParent == aChild) {
    return result;
  }

  if (nsHTMLElement::IsContainer(aChild)) {
    mScratch.Truncate();
    if (!gHTMLElements[aChild].HasSpecialProperty(kNoPropagate)) {
      if (nsHTMLElement::IsBlockParent(aParent) ||
          gHTMLElements[aParent].GetSpecialChildren()) {
        result = ForwardPropagate(mScratch, aParent, aChild);
        if (!result) {
          if (eHTMLTag_unknown != aParent) {
            result = BackwardPropagate(mScratch, aParent, aChild);
          } else {
            result = BackwardPropagate(mScratch, eHTMLTag_html, aChild);
          }
        }
      }
    }
    if (mScratch.Length() - 1 > gHTMLElements[aParent].mPropagateRange) {
      result = false;
    }
  } else {
    result = !!aParentContains;
  }


  return result;
}


/**
 *  This method gets called to determine whether a given 
 *  tag can be omitted from opening. Most cannot.
 *  
 *  @param   aParent
 *  @param   aChild
 *  @param   aParentContains
 *  @return  true if given tag can contain other tags
 */
bool
CNavDTD::CanOmit(eHTMLTags aParent, eHTMLTags aChild, int32_t& aParentContains)
{
  eHTMLTags theAncestor = gHTMLElements[aChild].mExcludingAncestor;
  if (eHTMLTag_unknown != theAncestor && HasOpenContainer(theAncestor)) {
    return true;
  }

  theAncestor = gHTMLElements[aChild].mRequiredAncestor;
  if (eHTMLTag_unknown != theAncestor) {
    // If there's a required ancestor, we only omit if it isn't open and we
    // can't get to it through propagation.
    return !HasOpenContainer(theAncestor) &&
           !CanPropagate(aParent, aChild, aParentContains);
  }

  if (gHTMLElements[aParent].CanExclude(aChild)) {
    return true;
  }

  // Now the obvious test: if the parent can contain the child, don't omit.
  if (-1 == aParentContains) {
    aParentContains = CanContain(aParent, aChild);
  }

  if (aParentContains || aChild == aParent) {
    return false;
  }

  if (gHTMLElements[aParent].IsBlockEntity() &&
      nsHTMLElement::IsInlineEntity(aChild)) {
    // Feel free to drop inlines that a block doesn't contain.
    return true;
  }

  if (gHTMLElements[aParent].HasSpecialProperty(kBadContentWatch)) {
    // We can only omit this child if it does not have the kBadContentWatch
    // special property.
    return !gHTMLElements[aChild].HasSpecialProperty(kBadContentWatch);
  }

  if (gHTMLElements[aParent].HasSpecialProperty(kSaveMisplaced)) {
    return true;
  }

  if (aParent == eHTMLTag_body) {
    // There are very few tags that the body does not contain. If we get here
    // the best thing to do is just drop them.
    return true;
  }

  return false;
}

/**
 *  This method gets called to determine whether a given 
 *  tag is itself a container
 *  
 *  @update  gess 4/8/98
 *  @param   aTag -- tag to test as a container
 *  @return  true if given tag can contain other tags
 */
NS_IMETHODIMP_(bool)
CNavDTD::IsContainer(int32_t aTag) const
{
  return nsHTMLElement::IsContainer((eHTMLTags)aTag);
}


bool
CNavDTD::ForwardPropagate(nsString& aSequence, eHTMLTags aParent,
                          eHTMLTags aChild)
{
  bool result = false;

  switch(aParent) {
    case eHTMLTag_table:
      if (eHTMLTag_tr == aChild || eHTMLTag_td == aChild) {
        return BackwardPropagate(aSequence, aParent, aChild);
      }
      // Otherwise, intentionally fall through...

    case eHTMLTag_tr:
      if (CanContain(eHTMLTag_td, aChild)) {
        aSequence.Append((PRUnichar)eHTMLTag_td);
        result = BackwardPropagate(aSequence, aParent, eHTMLTag_td);
      }
      break;

    default:
      break;
  }

  return result;
}

bool
CNavDTD::BackwardPropagate(nsString& aSequence, eHTMLTags aParent,
                           eHTMLTags aChild) const
{
  eHTMLTags theParent = aParent;

  do {
    const TagList* theRootTags = gHTMLElements[aChild].GetRootTags();
    if (!theRootTags) {
      break;
    }

    theParent = theRootTags->mTags[0];
    NS_ASSERTION(CanContain(theParent, aChild),
                 "Children must be contained by their root tags");

    aChild = theParent;
    aSequence.Append((PRUnichar)theParent);
  } while (theParent != eHTMLTag_unknown && theParent != aParent);

  return aParent == theParent;
}

bool CNavDTD::HasOpenContainer(eHTMLTags aContainer) const
{
  switch (aContainer) {
    case eHTMLTag_form:
      return !(~mFlags & NS_DTD_FLAG_HAS_OPEN_FORM);
    case eHTMLTag_map:
      return mOpenMapCount > 0;
    default:
      return mBodyContext->HasOpenContainer(aContainer);
  }
}

bool
CNavDTD::HasOpenContainer(const eHTMLTags aTagSet[], int32_t aCount) const
{
  int theIndex;
  int theTopIndex = mBodyContext->GetCount() - 1;

  for (theIndex = theTopIndex; theIndex > 0; --theIndex) {
    if (FindTagInSet((*mBodyContext)[theIndex], aTagSet, aCount)) {
      return true;
    }
  }

  return false;
}

eHTMLTags
CNavDTD::GetTopNode() const
{
  return mBodyContext->Last();
}

/**
 * It is with great trepidation that I offer this method (privately of course).
 * The gets called whenever a container gets opened. This methods job is to 
 * take a look at the (transient) style stack, and open any style containers that
 * are there. Of course, we shouldn't bother to open styles that are incompatible
 * with our parent container.
 *
 * @update  gess6/4/98
 * @param   tag of the container just opened
 * @return  0 (for now)
 */
nsresult
CNavDTD::OpenTransientStyles(eHTMLTags aChildTag, bool aCloseInvalid)
{
  nsresult result = NS_OK;

  // No need to open transient styles in head context - Fix for 41427
  if ((mFlags & NS_DTD_FLAG_ENABLE_RESIDUAL_STYLE) &&
      eHTMLTag_newline != aChildTag &&
      !(mFlags & NS_DTD_FLAG_HAS_OPEN_HEAD)) {
    if (CanContain(eHTMLTag_font, aChildTag)) {
      uint32_t theCount = mBodyContext->GetCount();
      uint32_t theLevel = theCount;

      // This first loop is used to determine how far up the containment
      // hierarchy we go looking for residual styles.
      while (1 < theLevel) {
        eHTMLTags theParentTag = mBodyContext->TagAt(--theLevel);
        if (gHTMLElements[theParentTag].HasSpecialProperty(kNoStyleLeaksIn)) {
          break;
        }
      }

      mFlags &= ~NS_DTD_FLAG_ENABLE_RESIDUAL_STYLE;
      for (; theLevel < theCount; ++theLevel) {
        nsEntryStack* theStack = mBodyContext->GetStylesAt(theLevel);
        if (theStack) {
          // Don't open transient styles if it makes the stack deep, bug 58917.
          if (theCount + theStack->mCount >= FONTSTYLE_IGNORE_DEPTH) {
            break;
          }

          int32_t sindex = 0;

          nsTagEntry *theEntry = theStack->mEntries;
          bool isHeadingOpen = HasOpenTagOfType(kHeading, *mBodyContext);
          for (sindex = 0; sindex < theStack->mCount; ++sindex) {
            nsCParserNode* theNode = (nsCParserNode*)theEntry->mNode;
            if (1 == theNode->mUseCount) {
              eHTMLTags theNodeTag = (eHTMLTags)theNode->GetNodeType();
              if (gHTMLElements[theNodeTag].CanContain(aChildTag, mDTDMode)) {
                // XXX The following comment is entirely incoherent.
                // We do this too, because this entry differs from the new one
                // we're pushing.
                theEntry->mParent = theStack;
                if (isHeadingOpen) {
                  // Bug 77352
                  // The style system needs to identify residual style tags
                  // within heading tags so that heading tags' size can take
                  // precedence over the residual style tags' size info..
                  // *Note: Make sure that this attribute is transient since it
                  // should not get carried over to cases other than heading.
                  CAttributeToken theAttrToken(NS_LITERAL_STRING("_moz-rs-heading"),
                                               EmptyString());
                  theNode->AddAttribute(&theAttrToken);
                  result = OpenContainer(theNode, theNodeTag, theStack);
                  theNode->PopAttributeToken();
                } else {
                  result = OpenContainer(theNode, theNodeTag, theStack);
                }
              } else if (aCloseInvalid) {
                // If the node tag can't contain the child tag, then remove the
                // child tag from the style stack
                nsCParserNode* node = theStack->Remove(sindex, theNodeTag);
                IF_FREE(node, &mNodeAllocator);
                --theEntry;
              }
            }
            ++theEntry;
          }
        }
      }
      mFlags |= NS_DTD_FLAG_ENABLE_RESIDUAL_STYLE;
    }
  }

  return result;
}

/**
 * This method gets called when an explicit style close-tag is encountered.
 * It results in the style tag id being popped from our internal style stack.
 *
 * @update  gess6/4/98
 * @param 
 * @return  0 if all went well (which it always does)
 */
void
CNavDTD::PopStyle(eHTMLTags aTag)
{
  if ((mFlags & NS_DTD_FLAG_ENABLE_RESIDUAL_STYLE) &&
      nsHTMLElement::IsResidualStyleTag(aTag)) {
    nsCParserNode* node = mBodyContext->PopStyle(aTag);
    IF_FREE(node, &mNodeAllocator);  
  }
}


/**
 * This method does two things: 1st, help construct
 * our own internal model of the content-stack; and
 * 2nd, pass this message on to the sink.
 * 
 * @update  gess4/22/98
 * @param   aNode -- next node to be added to model
 */
nsresult
CNavDTD::OpenHTML(const nsCParserNode *aNode)
{
  NS_PRECONDITION(mBodyContext->GetCount() >= 0, kInvalidTagStackPos);

  nsresult result = mSink ? mSink->OpenContainer(*aNode) : NS_OK; 

  // Don't push more than one HTML tag into the stack.
  if (mBodyContext->GetCount() == 0)  {
    mBodyContext->Push(const_cast<nsCParserNode*>(aNode), 0, false); 
  }

  return result;
}

/**
 * This method does two things: 1st, help construct
 * our own internal model of the content-stack; and
 * 2nd, pass this message on to the sink.
 * @update  gess4/6/98
 * @param   aNode -- next node to be added to model
 * @return  TRUE if ok, FALSE if error
 */
nsresult
CNavDTD::OpenBody(const nsCParserNode *aNode)
{
  NS_PRECONDITION(mBodyContext->GetCount() >= 0, kInvalidTagStackPos);

  nsresult result = NS_OK;
  
  if (!(mFlags & NS_DTD_FLAG_HAD_FRAMESET)) {
    mFlags |= NS_DTD_FLAG_HAD_BODY;

    // Make sure the head is closed by the time the body is opened.
    CloseContainer(eHTMLTag_head, false);

    // Now we can open the body.
    result = mSink ? mSink->OpenContainer(*aNode) : NS_OK; 

    if (!HasOpenContainer(eHTMLTag_body)) {
      mBodyContext->Push(const_cast<nsCParserNode*>(aNode), 0, false);
      mTokenizer->PrependTokens(mMisplacedContent);
    }
  }

  return result;
}

/**
 * This method does two things: 1st, help construct
 * our own internal model of the content-stack; and
 * 2nd, pass this message on to the sink.
 * @update  gess4/6/98
 * @param   aNode -- next node to be added to model
 * @param   aClosedByStartTag -- ONLY TRUE if the container is being closed by opening of another container.
 * @return  TRUE if ok, FALSE if error
 */
nsresult
CNavDTD::OpenContainer(const nsCParserNode *aNode,
                       eHTMLTags aTag,
                       nsEntryStack* aStyleStack)
{
  NS_PRECONDITION(mBodyContext->GetCount() >= 0, kInvalidTagStackPos);

  nsresult   result = NS_OK;
  bool       done   = true;
  bool       rs_tag = nsHTMLElement::IsResidualStyleTag(aTag);
  // We need to open transient styles to encompass the <li> so that the bullets
  // inherit the proper colors.
  bool       li_tag = aTag == eHTMLTag_li;

  if (rs_tag || li_tag) {
    /*
     * Here's an interesting problem:
     *
     * If there's an <a> on the RS-stack, and you're trying to open
     * another <a>, the one on the RS-stack should be discarded.
     *
     * I'm updating OpenTransientStyles to throw old <a>'s away.
     */
    OpenTransientStyles(aTag, !li_tag);
  }

  switch (aTag) {
    case eHTMLTag_html:
      result = OpenHTML(aNode);
      break;

    case eHTMLTag_head:
      if (!(mFlags & NS_DTD_FLAG_HAS_OPEN_HEAD)) {
        mFlags |= NS_DTD_FLAG_HAS_OPEN_HEAD;
        done = false;
      }
      break;

    case eHTMLTag_body:
      {
        eHTMLTags theParent = mBodyContext->Last();
        if (!gHTMLElements[aTag].IsSpecialParent(theParent)) {
          mFlags |= NS_DTD_FLAG_HAS_OPEN_BODY;
          result = OpenBody(aNode);
        } else {
          done = false;
        }
      }
      break;

    case eHTMLTag_map:
      ++mOpenMapCount;
      done = false;
      break;

    case eHTMLTag_form:
      // Discard nested forms - bug 72639
      if (!(mFlags & NS_DTD_FLAG_HAS_OPEN_FORM)) {
        mFlags |= NS_DTD_FLAG_HAS_OPEN_FORM;
        result = mSink ? mSink->OpenContainer(*aNode) : NS_OK;
      }
      break;

    case eHTMLTag_frameset:
      // Make sure that the head is closed before we try to open this frameset.
      CloseContainer(eHTMLTag_head, false);

      // Now that the head is closed, continue on with opening this frameset.
      mFlags |= NS_DTD_FLAG_HAD_FRAMESET;
      done = false;
      break;

    case eHTMLTag_noembed:
      // <noembed> is unconditionally alternate content.
      done = false;
      mFlags |= NS_DTD_FLAG_ALTERNATE_CONTENT;
      break;

    case eHTMLTag_noscript:
      // We want to make sure that OpenContainer gets called below since we're
      // not doing it here
      done = false;

      if (mFlags & NS_IPARSER_FLAG_SCRIPT_ENABLED) {
        // XXX This flag doesn't currently do anything (and would be
        // insufficient if it did).
        mFlags |= NS_DTD_FLAG_ALTERNATE_CONTENT;
      }
      break;

    case eHTMLTag_iframe: // Bug 84491
    case eHTMLTag_noframes:
      done = false;
      if (mFlags & NS_IPARSER_FLAG_FRAMES_ENABLED) {
        mFlags |= NS_DTD_FLAG_ALTERNATE_CONTENT;
      }
      break;

    default:
      done = false;
      break;
  }

  if (!done) {

    result = mSink ? mSink->OpenContainer(*aNode) : NS_OK;

    // For residual style tags rs_tag will be true and hence
    // the body context will hold an extra reference to the node.
    mBodyContext->Push(const_cast<nsCParserNode*>(aNode), aStyleStack, rs_tag); 
  }

  return result;
}

nsresult
CNavDTD::CloseResidualStyleTags(const eHTMLTags aTag,
                                bool aClosedByStartTag)
{
  const int32_t count = mBodyContext->GetCount();
  int32_t pos = count;
  while (nsHTMLElement::IsResidualStyleTag(mBodyContext->TagAt(pos - 1)))
    --pos;
  if (pos < count)
    return CloseContainersTo(pos, aTag, aClosedByStartTag);
  return NS_OK;
}

/**
 * This method does two things: 1st, help construct
 * our own internal model of the content-stack; and
 * 2nd, pass this message on to the sink.
 * @update  gess4/6/98
 * @param   aTag  -- id of tag to be closed
 * @return  TRUE if ok, FALSE if error
 */
nsresult
CNavDTD::CloseContainer(const eHTMLTags aTag, bool aMalformed)
{
  nsresult   result = NS_OK;
  bool       done   = true;

  switch (aTag) {
    case eHTMLTag_head:
      if (mFlags & NS_DTD_FLAG_HAS_OPEN_HEAD) {
        mFlags &= ~NS_DTD_FLAG_HAS_OPEN_HEAD;
        if (mBodyContext->Last() == eHTMLTag_head) {
          mBodyContext->Pop();
        } else {
          // This else can happen because CloseContainer is called both directly
          // and from CloseContainersTo. CloseContainersTo pops the current tag
          // off of the stack before calling CloseContainer.
          NS_ASSERTION(mBodyContext->LastOf(eHTMLTag_head) == kNotFound,
                       "Closing the wrong tag");
        }
        done = false;
      }
      break;

    case eHTMLTag_map:
      if (mOpenMapCount) {
        mOpenMapCount--;
        done = false;
      }
      break;

    case eHTMLTag_form:
      if (mFlags & NS_DTD_FLAG_HAS_OPEN_FORM) {
        mFlags &= ~NS_DTD_FLAG_HAS_OPEN_FORM;
        done = false;
        // If we neglect to close these tags, the sink will refuse to close the
        // form because the form will not be on the top of the SinkContext stack.
        // See HTMLContentSink::CloseForm.  (XXX do this in other cases?)
        CloseResidualStyleTags(eHTMLTag_form, false);
      }
      break;

    case eHTMLTag_iframe:
    case eHTMLTag_noembed:
    case eHTMLTag_noscript:
    case eHTMLTag_noframes:
      // Switch from alternate content state to regular state.
      mFlags &= ~NS_DTD_FLAG_ALTERNATE_CONTENT;

      // falling thro' intentionally....
    default:
      done = false;
  }

  if (!done) {

    if (mSink) {
      result = !aMalformed
               ? mSink->CloseContainer(aTag)
               : mSink->CloseMalformedContainer(aTag);
    }

    // If we were dealing with a head container in the body, make sure to
    // close the head context now, so that body content doesn't get sucked
    // into the head.
    if (mBodyContext->GetCount() == mHeadContainerPosition) {
      mHeadContainerPosition = -1;
      nsresult headresult = CloseContainer(eHTMLTag_head, false);

      // Note: we could be assigning NS_OK into NS_OK here, but that's ok.
      // This test is to avoid a successful CloseHead result stomping over a
      // request to block the parser.
      if (NS_SUCCEEDED(result)) {
        result = headresult;
      }
    }
  }

  return result;
}

/**
 * This method does two things: 1st, help construct
 * our own internal model of the content-stack; and
 * 2nd, pass this message on to the sink.
 * @update  gess4/6/98
 * @param   anIndex
 * @param   aTag
 * @param   aClosedByStartTag -- if TRUE, then we're closing something because a start tag caused it
 * @return  TRUE if ok, FALSE if error
 */
nsresult
CNavDTD::CloseContainersTo(int32_t anIndex, eHTMLTags aTarget,
                           bool aClosedByStartTag)
{
  NS_PRECONDITION(mBodyContext->GetCount() > 0, kInvalidTagStackPos);
  nsresult result = NS_OK;

  if (anIndex < mBodyContext->GetCount() && anIndex >= 0) {
    int32_t count = 0;
    while ((count = mBodyContext->GetCount()) > anIndex) {
      nsEntryStack* theChildStyleStack = 0;
      eHTMLTags theTag = mBodyContext->Last();
      nsCParserNode* theNode = mBodyContext->Pop(theChildStyleStack);
      result = CloseContainer(theTag, false);

      bool theTagIsStyle = nsHTMLElement::IsResidualStyleTag(theTag);
      // If the current tag cannot leak out then we shouldn't leak out of the target - Fix 40713
      bool theStyleDoesntLeakOut = gHTMLElements[theTag].HasSpecialProperty(kNoStyleLeaksOut);
      if (!theStyleDoesntLeakOut) {
        theStyleDoesntLeakOut = gHTMLElements[aTarget].HasSpecialProperty(kNoStyleLeaksOut);
      }

      // Do not invoke residual style handling when dealing with
      // alternate content. This fixed bug 25214.
      if (theTagIsStyle && !(mFlags & NS_DTD_FLAG_ALTERNATE_CONTENT)) {
        NS_ASSERTION(theNode, "residual style node should not be null");
        if (!theNode) {
          if (theChildStyleStack) {
            mBodyContext->PushStyles(theChildStyleStack);
          }
          return NS_OK;
        }

        bool theTargetTagIsStyle = nsHTMLElement::IsResidualStyleTag(aTarget);
        if (aClosedByStartTag) {
          // Handle closure due to new start tag.
          // The cases we're handing here:
          // 1. <body><b><DIV>       //<b> gets pushed onto <body>.mStyles.
          // 2. <body><a>text<a>     //in this case, the target matches, so don't push style
          if (theNode->mUseCount == 0) {
            if (theTag != aTarget) {
              if (theChildStyleStack) {
                theChildStyleStack->PushFront(theNode);
              } else {
                mBodyContext->PushStyle(theNode);
              }
            }
          } else if (theTag == aTarget && !gHTMLElements[aTarget].CanContainSelf()) {
            //here's a case we missed:  <a><div>text<a>text</a></div>
            //The <div> pushes the 1st <a> onto the rs-stack, then the 2nd <a>
            //pops the 1st <a> from the rs-stack altogether.
            nsCParserNode* node = mBodyContext->PopStyle(theTag);
            IF_FREE(node, &mNodeAllocator);
          }

          if (theChildStyleStack) {
            mBodyContext->PushStyles(theChildStyleStack);
          }
        } else {
          /*
           * if you're here, then we're dealing with the closure of tags
           * caused by a close tag (as opposed to an open tag).
           * At a minimum, we should consider pushing residual styles up
           * up the stack or popping and recycling displaced nodes.
           *
           * Known cases:
           * 1. <body><b><div>text</DIV>
           * Here the <b> will leak into <div> (see case given above), and
           * when <div> closes the <b> is dropped since it's already residual.
           *
           * 2. <body><div><b>text</div>
           * Here the <b> will leak out of the <div> and get pushed onto
           * the RS stack for the <body>, since it originated in the <div>.
           *
           * 3. <body><span><b>text</span>
           * In this case, the the <b> get's pushed onto the style stack.
           * Later we deal with RS styles stored on the <span>
           *
           * 4. <body><span><b>text</i>
           * Here we the <b>is closed by a (synonymous) style tag.
           * In this case, the <b> is simply closed.
           */
          if (theChildStyleStack) {
            if (!theStyleDoesntLeakOut) {
              if (theTag != aTarget) {
                if (theNode->mUseCount == 0) {
                  theChildStyleStack->PushFront(theNode);
                }
              } else if (theNode->mUseCount == 1) {
                // This fixes bug 30885,29626.
                // Make sure that the node, which is about to
                // get released does not stay on the style stack...
                // Also be sure to remove the correct style off the
                // style stack. -  Ref. bug 94208.
                // Ex <FONT><B><I></FONT><FONT></B></I></FONT>
                // Make sure that </B> removes B off the style stack.
                mBodyContext->RemoveStyle(theTag);
              }
              mBodyContext->PushStyles(theChildStyleStack);
            } else{
              IF_DELETE(theChildStyleStack, &mNodeAllocator);
            }
          } else if (theNode->mUseCount == 0) {
            // The old version of this only pushed if the targettag wasn't
            // style.  But that misses this case: <font><b>text</font>, where
            // the b should leak.
            if (aTarget != theTag) {
              mBodyContext->PushStyle(theNode);
            }
          } else {
            // Ah, at last, the final case. If you're here, then we just popped
            // a style tag that got onto that tag stack from a stylestack
            // somewhere.  Pop it from the stylestack if the target is also a
            // style tag.  Make sure to remove the matching style. In the
            // following example:
            // <FONT><B><I></FONT><FONT color=red></B></I></FONT>
            // make sure that </I> does not remove
            // <FONT color=red> off the style stack. - bug 94208
            if (theTargetTagIsStyle && theTag == aTarget) {
              mBodyContext->RemoveStyle(theTag);
            }
          }
        }
      } else {
        // The tag is not a style tag.
        if (theChildStyleStack) {
          if (theStyleDoesntLeakOut) {
            IF_DELETE(theChildStyleStack, &mNodeAllocator);
          } else {
            mBodyContext->PushStyles(theChildStyleStack);
          }
        }
      }
      IF_FREE(theNode, &mNodeAllocator);
    }
  }
  return result;
}

/**
 * This method does two things: 1st, help construct
 * our own internal model of the content-stack; and
 * 2nd, pass this message on to the sink.
 * @update  gess4/6/98
 * @param   aTag --
 * @param   aClosedByStartTag -- ONLY TRUE if the container is being closed by opening of another container.
 * @return  TRUE if ok, FALSE if error
 */
nsresult
CNavDTD::CloseContainersTo(eHTMLTags aTag, bool aClosedByStartTag)
{
  NS_PRECONDITION(mBodyContext->GetCount() > 0, kInvalidTagStackPos);

  int32_t pos = mBodyContext->LastOf(aTag);

  if (kNotFound != pos) {
    // The tag is indeed open, so close it.
    return CloseContainersTo(pos, aTag, aClosedByStartTag);
  }

  eHTMLTags theTopTag = mBodyContext->Last();

  bool theTagIsSynonymous = (nsHTMLElement::IsResidualStyleTag(aTag) &&
                               nsHTMLElement::IsResidualStyleTag(theTopTag)) ||
                              (gHTMLElements[aTag].IsMemberOf(kHeading) &&
                               gHTMLElements[theTopTag].IsMemberOf(kHeading));

  if (theTagIsSynonymous) {
    // If you're here, it's because we're trying to close one tag,
    // but a different (synonymous) one is actually open. Because this is NAV4x
    // compatibililty mode, we must close the one that's really open.
    aTag = theTopTag;
    pos = mBodyContext->LastOf(aTag);
    if (kNotFound != pos) {
      // The tag is indeed open, so close it.
      return CloseContainersTo(pos, aTag, aClosedByStartTag);
    }
  }

  nsresult result = NS_OK;
  const TagList* theRootTags = gHTMLElements[aTag].GetRootTags();
  // XXX Can we just bail if !theRootTags? Does that ever happen?
  eHTMLTags theParentTag = theRootTags ? theRootTags->mTags[0] : eHTMLTag_unknown;
  pos = mBodyContext->LastOf(theParentTag);
  if (kNotFound != pos) {
    // The parent container is open, so close it instead
    result = CloseContainersTo(pos + 1, aTag, aClosedByStartTag);
  }
  return result;
}

/**
 * This method does two things: 1st, help construct
 * our own internal model of the content-stack; and
 * 2nd, pass this message on to the sink.
 * @update  gess4/6/98
 * @param   aNode -- next node to be added to model
 * @return  error code; 0 means OK
 */
nsresult
CNavDTD::AddLeaf(const nsIParserNode *aNode)
{
  nsresult result = NS_OK;

  if (mSink) {
    eHTMLTags theTag = (eHTMLTags)aNode->GetNodeType();
    OpenTransientStyles(theTag);

    result = mSink->AddLeaf(*aNode);
  }

  return result;
}

/**
 * Call this method ONLY when you want to write a leaf
 * into the head container.
 * 
 * @update  gess 03/14/99
 * @param   aNode -- next node to be added to model
 * @return  error code; 0 means OK
 */
nsresult
CNavDTD::AddHeadContent(nsIParserNode *aNode)
{
  nsresult result = NS_OK;

  static eHTMLTags gNoXTags[] = { eHTMLTag_noembed, eHTMLTag_noframes };

  eHTMLTags theTag = (eHTMLTags)aNode->GetNodeType();

  // XXX - SCRIPT inside NOTAGS should not get executed unless the pref.
  // says so.  Since we don't have this support yet..lets ignore the
  // SCRIPT inside NOTAGS.  Ref Bug 25880.
  if (eHTMLTag_meta == theTag || eHTMLTag_script == theTag) {
    if (HasOpenContainer(gNoXTags, ArrayLength(gNoXTags))) {
      return result;
    }
  }

  if (mSink) {
    // Make sure the head is opened.
    if (!(mFlags & NS_DTD_FLAG_HAS_OPEN_HEAD)) {
      result = mSink->OpenHead();
      mBodyContext->PushTag(eHTMLTag_head);
      mFlags |= NS_DTD_FLAG_HAS_OPEN_HEAD;
    }

    // Note: userdefined tags in the head are treated as leaves.
    if (!nsHTMLElement::IsContainer(theTag) || theTag == eHTMLTag_userdefined) {
      result = mSink->AddLeaf(*aNode);

      if (mFlags & NS_DTD_FLAG_HAS_MAIN_CONTAINER) {
        // Close the head now so that body content doesn't get sucked into it.
        CloseContainer(eHTMLTag_head, false);
      }
    } else {
      if ((mFlags & NS_DTD_FLAG_HAS_MAIN_CONTAINER) &&
          mHeadContainerPosition == -1) {
        // Keep track of this so that we know when to close the head, when
        // this tag is done with.
        mHeadContainerPosition = mBodyContext->GetCount();
      }

      // Note: The head context is already opened.
      result = mSink->OpenContainer(*aNode);

      mBodyContext->Push(static_cast<nsCParserNode*>(aNode), nullptr,
                         false);
    }
  }

  return result;
}

void
CNavDTD::CreateContextStackFor(eHTMLTags aParent, eHTMLTags aChild)
{
  mScratch.Truncate();

  bool      result = ForwardPropagate(mScratch, aParent, aChild);

  if (!result) {
    if (eHTMLTag_unknown == aParent) {
      result = BackwardPropagate(mScratch, eHTMLTag_html, aChild);
    } else if (aParent != aChild) {
      // Don't even bother if we're already inside a similar element...
      result = BackwardPropagate(mScratch, aParent, aChild);
    }
  }

  if (!result) {
    return;
  }

  int32_t   theLen = mScratch.Length();
  eHTMLTags theTag = (eHTMLTags)mScratch[--theLen];

  // Now, build up the stack according to the tags.
  while (theLen) {
    theTag = (eHTMLTags)mScratch[--theLen];

    // Note: These tokens should all wind up on contextstack, so don't recycle
    // them.
    CToken *theToken = mTokenAllocator->CreateTokenOfType(eToken_start, theTag);
    HandleToken(theToken);
  }
}
