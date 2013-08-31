/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=78: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHtml5TreeOperation.h"
#include "nsContentUtils.h"
#include "nsNodeUtils.h"
#include "nsAttrName.h"
#include "nsHtml5TreeBuilder.h"
#include "nsIDOMMutationEvent.h"
#include "mozAutoDocUpdate.h"
#include "nsBindingManager.h"
#include "nsXBLBinding.h"
#include "nsHtml5DocumentMode.h"
#include "nsHtml5HtmlAttributes.h"
#include "nsContentCreatorFunctions.h"
#include "nsIScriptElement.h"
#include "nsIDTD.h"
#include "nsTraceRefcnt.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsIFormControl.h"
#include "nsIStyleSheetLinkingElement.h"
#include "nsIDOMDocumentType.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "nsIMutationObserver.h"
#include "nsIFormProcessor.h"
#include "nsIServiceManager.h"
#include "nsEscape.h"
#include "mozilla/dom/Element.h"
#include "nsHtml5SVGLoadDispatcher.h"
#include "nsIURI.h"
#include "nsIProtocolHandler.h"
#include "nsNetUtil.h"
#include "nsIHTMLDocument.h"
#include "mozilla/Likely.h"

namespace dom = mozilla::dom;

static NS_DEFINE_CID(kFormProcessorCID, NS_FORMPROCESSOR_CID);

/**
 * Helper class that opens a notification batch if the current doc
 * is different from the executor doc.
 */
class NS_STACK_CLASS nsHtml5OtherDocUpdate {
  public:
    nsHtml5OtherDocUpdate(nsIDocument* aCurrentDoc, nsIDocument* aExecutorDoc)
    {
      NS_PRECONDITION(aCurrentDoc, "Node has no doc?");
      NS_PRECONDITION(aExecutorDoc, "Executor has no doc?");
      if (MOZ_LIKELY(aCurrentDoc == aExecutorDoc)) {
        mDocument = nullptr;
      } else {
        mDocument = aCurrentDoc;
        aCurrentDoc->BeginUpdate(UPDATE_CONTENT_MODEL);        
      }
    }

    ~nsHtml5OtherDocUpdate()
    {
      if (MOZ_UNLIKELY(mDocument)) {
        mDocument->EndUpdate(UPDATE_CONTENT_MODEL);
      }
    }
  private:
    nsIDocument* mDocument;
};

nsHtml5TreeOperation::nsHtml5TreeOperation()
#ifdef DEBUG
 : mOpCode(eTreeOpUninitialized)
#endif
{
  MOZ_COUNT_CTOR(nsHtml5TreeOperation);
}

nsHtml5TreeOperation::~nsHtml5TreeOperation()
{
  MOZ_COUNT_DTOR(nsHtml5TreeOperation);
  NS_ASSERTION(mOpCode != eTreeOpUninitialized, "Uninitialized tree op.");
  switch(mOpCode) {
    case eTreeOpAddAttributes:
      delete mTwo.attributes;
      break;
    case eTreeOpCreateElementNetwork:
    case eTreeOpCreateElementNotNetwork:
      delete mThree.attributes;
      break;
    case eTreeOpAppendDoctypeToDocument:
      delete mTwo.stringPair;
      break;
    case eTreeOpFosterParentText:
    case eTreeOpAppendText:
    case eTreeOpAppendComment:
    case eTreeOpAppendCommentToDocument:
    case eTreeOpAddViewSourceHref:
      delete[] mTwo.unicharPtr;
      break;
    case eTreeOpSetDocumentCharset:
    case eTreeOpNeedsCharsetSwitchTo:
      delete[] mOne.charPtr;
      break;
    case eTreeOpProcessOfflineManifest:
      nsMemory::Free(mOne.unicharPtr);
      break;
    default: // keep the compiler happy
      break;
  }
}

nsresult
nsHtml5TreeOperation::AppendTextToTextNode(const PRUnichar* aBuffer,
                                           uint32_t aLength,
                                           nsIContent* aTextNode,
                                           nsHtml5TreeOpExecutor* aBuilder)
{
  NS_PRECONDITION(aTextNode, "Got null text node.");

  if (aBuilder->HaveNotified(aTextNode)) {
    // This text node has already been notified on, so it's necessary to
    // notify on the append
    nsresult rv = NS_OK;
    uint32_t oldLength = aTextNode->TextLength();
    CharacterDataChangeInfo info = {
      true,
      oldLength,
      oldLength,
      aLength
    };
    nsNodeUtils::CharacterDataWillChange(aTextNode, &info);

    rv = aTextNode->AppendText(aBuffer, aLength, false);
    NS_ENSURE_SUCCESS(rv, rv);

    nsNodeUtils::CharacterDataChanged(aTextNode, &info);
    return rv;
  }

  return aTextNode->AppendText(aBuffer, aLength, false);
}


nsresult
nsHtml5TreeOperation::AppendText(const PRUnichar* aBuffer,
                                 uint32_t aLength,
                                 nsIContent* aParent,
                                 nsHtml5TreeOpExecutor* aBuilder)
{
  nsresult rv = NS_OK;
  nsIContent* lastChild = aParent->GetLastChild();
  if (lastChild && lastChild->IsNodeOfType(nsINode::eTEXT)) {
    nsHtml5OtherDocUpdate update(aParent->OwnerDoc(),
                                 aBuilder->GetDocument());
    return AppendTextToTextNode(aBuffer, 
                                aLength, 
                                lastChild, 
                                aBuilder);
  }

  nsCOMPtr<nsIContent> text;
  NS_NewTextNode(getter_AddRefs(text), aBuilder->GetNodeInfoManager());
  NS_ASSERTION(text, "Infallible malloc failed?");
  rv = text->SetText(aBuffer, aLength, false);
  NS_ENSURE_SUCCESS(rv, rv);

  return Append(text, aParent, aBuilder);
}

nsresult
nsHtml5TreeOperation::Append(nsIContent* aNode,
                             nsIContent* aParent,
                             nsHtml5TreeOpExecutor* aBuilder)
{
  nsresult rv = NS_OK;
  nsIDocument* executorDoc = aBuilder->GetDocument();
  NS_ASSERTION(executorDoc, "Null doc on executor");
  nsIDocument* parentDoc = aParent->OwnerDoc();
  NS_ASSERTION(parentDoc, "Null owner doc on old node.");

  if (MOZ_LIKELY(executorDoc == parentDoc)) {
    // the usual case. the parent is in the parser's doc
    rv = aParent->AppendChildTo(aNode, false);
    if (NS_SUCCEEDED(rv)) {
      aBuilder->PostPendingAppendNotification(aParent, aNode);
    }
    return rv;
  }

  // The parent has been moved to another doc
  parentDoc->BeginUpdate(UPDATE_CONTENT_MODEL);

  uint32_t childCount = aParent->GetChildCount();
  rv = aParent->AppendChildTo(aNode, false);
  if (NS_SUCCEEDED(rv)) {
    nsNodeUtils::ContentAppended(aParent, aNode, childCount);
  }
  parentDoc->EndUpdate(UPDATE_CONTENT_MODEL);
  return rv;
}

nsresult
nsHtml5TreeOperation::AppendToDocument(nsIContent* aNode,
                                       nsHtml5TreeOpExecutor* aBuilder)
{
  nsresult rv = NS_OK;
  aBuilder->FlushPendingAppendNotifications();
  nsIDocument* doc = aBuilder->GetDocument();
  uint32_t childCount = doc->GetChildCount();
  rv = doc->AppendChildTo(aNode, false);
  NS_ENSURE_SUCCESS(rv, rv);
  nsNodeUtils::ContentInserted(doc, aNode, childCount);

  NS_ASSERTION(!nsContentUtils::IsSafeToRunScript(),
               "Someone forgot to block scripts");
  if (aNode->IsElement()) {
    nsContentUtils::AddScriptRunner(
        new nsDocElementCreatedNotificationRunner(doc));
  }
  return rv;
}

nsresult
nsHtml5TreeOperation::Perform(nsHtml5TreeOpExecutor* aBuilder,
                              nsIContent** aScriptElement)
{
  nsresult rv = NS_OK;
  switch(mOpCode) {
    case eTreeOpAppend: {
      nsIContent* node = *(mOne.node);
      nsIContent* parent = *(mTwo.node);
      return Append(node, parent, aBuilder);
    }
    case eTreeOpDetach: {
      nsIContent* node = *(mOne.node);
      aBuilder->FlushPendingAppendNotifications();
      nsCOMPtr<nsIContent> parent = node->GetParent();
      if (parent) {
        nsHtml5OtherDocUpdate update(parent->OwnerDoc(),
                                     aBuilder->GetDocument());
        int32_t pos = parent->IndexOf(node);
        NS_ASSERTION((pos >= 0), "Element not found as child of its parent");
        parent->RemoveChildAt(pos, true);
      }
      return NS_OK;
    }
    case eTreeOpAppendChildrenToNewParent: {
      nsCOMPtr<nsIContent> node = *(mOne.node);
      nsIContent* parent = *(mTwo.node);
      aBuilder->FlushPendingAppendNotifications();

      nsHtml5OtherDocUpdate update(parent->OwnerDoc(),
                                   aBuilder->GetDocument());

      uint32_t childCount = parent->GetChildCount();
      bool didAppend = false;
      while (node->HasChildren()) {
        nsCOMPtr<nsIContent> child = node->GetFirstChild();
        node->RemoveChildAt(0, true);
        rv = parent->AppendChildTo(child, false);
        NS_ENSURE_SUCCESS(rv, rv);
        didAppend = true;
      }
      if (didAppend) {
        nsNodeUtils::ContentAppended(parent, parent->GetChildAt(childCount),
                                     childCount);
      }
      return rv;
    }
    case eTreeOpFosterParent: {
      nsIContent* node = *(mOne.node);
      nsIContent* parent = *(mTwo.node);
      nsIContent* table = *(mThree.node);
      nsIContent* foster = table->GetParent();

      if (foster && foster->IsElement()) {
        aBuilder->FlushPendingAppendNotifications();

        nsHtml5OtherDocUpdate update(foster->OwnerDoc(),
                                     aBuilder->GetDocument());

        uint32_t pos = foster->IndexOf(table);
        rv = foster->InsertChildAt(node, pos, false);
        NS_ENSURE_SUCCESS(rv, rv);
        nsNodeUtils::ContentInserted(foster, node, pos);
        return rv;
      }

      return Append(node, parent, aBuilder);
    }
    case eTreeOpAppendToDocument: {
      nsIContent* node = *(mOne.node);
      return AppendToDocument(node, aBuilder);
    }
    case eTreeOpAddAttributes: {
      dom::Element* node = (*(mOne.node))->AsElement();
      nsHtml5HtmlAttributes* attributes = mTwo.attributes;

      nsHtml5OtherDocUpdate update(node->OwnerDoc(),
                                   aBuilder->GetDocument());

      int32_t len = attributes->getLength();
      for (int32_t i = len; i > 0;) {
        --i;
        // prefix doesn't need regetting. it is always null or a static atom
        // local name is never null
        nsCOMPtr<nsIAtom> localName =
          Reget(attributes->getLocalNameNoBoundsCheck(i));
        int32_t nsuri = attributes->getURINoBoundsCheck(i);
        if (!node->HasAttr(nsuri, localName)) {
          // prefix doesn't need regetting. it is always null or a static atom
          // local name is never null
          node->SetAttr(nsuri,
                        localName,
                        attributes->getPrefixNoBoundsCheck(i),
                        *(attributes->getValueNoBoundsCheck(i)),
                        true);
          // XXX what to do with nsresult?
        }
      }
      
      return rv;
    }
    case eTreeOpCreateElementNetwork:
    case eTreeOpCreateElementNotNetwork: {
      nsIContent** target = mOne.node;
      int32_t ns = mFour.integer;
      nsCOMPtr<nsIAtom> name = Reget(mTwo.atom);
      nsHtml5HtmlAttributes* attributes = mThree.attributes;
      
      bool isKeygen = (name == nsHtml5Atoms::keygen && ns == kNameSpaceID_XHTML);
      if (MOZ_UNLIKELY(isKeygen)) {
        name = nsHtml5Atoms::select;
      }
      
      nsCOMPtr<nsIContent> newContent;
      nsCOMPtr<nsINodeInfo> nodeInfo = aBuilder->GetNodeInfoManager()->
        GetNodeInfo(name, nullptr, ns, nsIDOMNode::ELEMENT_NODE);
      NS_ASSERTION(nodeInfo, "Got null nodeinfo.");
      NS_NewElement(getter_AddRefs(newContent),
                    nodeInfo.forget(),
                    (mOpCode == eTreeOpCreateElementNetwork ?
                     dom::FROM_PARSER_NETWORK
                     : (aBuilder->BelongsToStringParser() ?
                        dom::FROM_PARSER_FRAGMENT :
                        dom::FROM_PARSER_DOCUMENT_WRITE)));
      NS_ASSERTION(newContent, "Element creation created null pointer.");

      aBuilder->HoldElement(*target = newContent);      

      if (MOZ_UNLIKELY(name == nsHtml5Atoms::style || name == nsHtml5Atoms::link)) {
        nsCOMPtr<nsIStyleSheetLinkingElement> ssle(do_QueryInterface(newContent));
        if (ssle) {
          ssle->InitStyleLinkElement(false);
          ssle->SetEnableUpdates(false);
        }
      } else if (MOZ_UNLIKELY(isKeygen)) {
        // Adapted from CNavDTD
        nsCOMPtr<nsIFormProcessor> theFormProcessor =
          do_GetService(kFormProcessorCID, &rv);
        NS_ENSURE_SUCCESS(rv, rv);
        
        nsTArray<nsString> theContent;
        nsAutoString theAttribute;
         
        (void) theFormProcessor->ProvideContent(NS_LITERAL_STRING("select"),
                                                theContent,
                                                theAttribute);

        newContent->SetAttr(kNameSpaceID_None, 
                            nsGkAtoms::moztype, 
                            nullptr, 
                            theAttribute,
                            false);

        nsCOMPtr<nsINodeInfo> optionNodeInfo = 
          aBuilder->GetNodeInfoManager()->GetNodeInfo(nsHtml5Atoms::option, 
                                                      nullptr, 
                                                      kNameSpaceID_XHTML,
                                                      nsIDOMNode::ELEMENT_NODE);
                                                      
        for (uint32_t i = 0; i < theContent.Length(); ++i) {
          nsCOMPtr<nsIContent> optionElt;
          nsCOMPtr<nsINodeInfo> ni = optionNodeInfo;
          NS_NewElement(getter_AddRefs(optionElt), 
                        ni.forget(),
                        (mOpCode == eTreeOpCreateElementNetwork ?
                         dom::FROM_PARSER_NETWORK
                         : (aBuilder->BelongsToStringParser() ?
                            dom::FROM_PARSER_FRAGMENT :
                            dom::FROM_PARSER_DOCUMENT_WRITE)));
          nsCOMPtr<nsIContent> optionText;
          NS_NewTextNode(getter_AddRefs(optionText), 
                         aBuilder->GetNodeInfoManager());
          (void) optionText->SetText(theContent[i], false);
          optionElt->AppendChildTo(optionText, false);
          newContent->AppendChildTo(optionElt, false);
          newContent->DoneAddingChildren(false);
        }
      }

      if (!attributes) {
        return rv;
      }

      int32_t len = attributes->getLength();
      for (int32_t i = len; i > 0;) {
        --i;
        // prefix doesn't need regetting. it is always null or a static atom
        // local name is never null
        nsCOMPtr<nsIAtom> localName =
          Reget(attributes->getLocalNameNoBoundsCheck(i));
        if (ns == kNameSpaceID_XHTML &&
            nsHtml5Atoms::a == name &&
            nsHtml5Atoms::name == localName) {
          // This is an HTML5-incompliant Geckoism.
          // Remove when fixing bug 582361
          NS_ConvertUTF16toUTF8 cname(*(attributes->getValueNoBoundsCheck(i)));
          NS_ConvertUTF8toUTF16 uv(nsUnescape(cname.BeginWriting()));
          newContent->SetAttr(attributes->getURINoBoundsCheck(i),
                              localName,
                              attributes->getPrefixNoBoundsCheck(i),
                              uv,
                              false);
        } else {
          newContent->SetAttr(attributes->getURINoBoundsCheck(i),
                              localName,
                              attributes->getPrefixNoBoundsCheck(i),
                              *(attributes->getValueNoBoundsCheck(i)),
                              false);
        }
      }

      return rv;
    }
    case eTreeOpSetFormElement: {
      nsIContent* node = *(mOne.node);
      nsIContent* parent = *(mTwo.node);
      nsCOMPtr<nsIFormControl> formControl(do_QueryInterface(node));
      // NS_ASSERTION(formControl, "Form-associated element did not implement nsIFormControl.");
      // TODO: uncomment the above line when <keygen> (bug 101019) is supported by Gecko
      nsCOMPtr<nsIDOMHTMLFormElement> formElement(do_QueryInterface(parent));
      NS_ASSERTION(formElement, "The form element doesn't implement nsIDOMHTMLFormElement.");
      // avoid crashing on <keygen>
      if (formControl &&
          !node->HasAttr(kNameSpaceID_None, nsGkAtoms::form)) {
        formControl->SetForm(formElement);
      }
      return rv;
    }
    case eTreeOpAppendText: {
      nsIContent* parent = *mOne.node;
      PRUnichar* buffer = mTwo.unicharPtr;
      uint32_t length = mFour.integer;
      return AppendText(buffer, length, parent, aBuilder);
    }
    case eTreeOpAppendIsindexPrompt: {
      nsIContent* parent = *mOne.node;
      nsXPIDLString prompt;
      nsresult rv =
          nsContentUtils::GetLocalizedString(nsContentUtils::eFORMS_PROPERTIES,
                                             "IsIndexPromptWithSpace", prompt);
      uint32_t len = prompt.Length();
      if (NS_FAILED(rv)) {
        return rv;
      }
      if (!len) {
        // Don't bother appending a zero-length text node.
        return NS_OK;
      }
      return AppendText(prompt.BeginReading(), len, parent, aBuilder);
    }
    case eTreeOpFosterParentText: {
      nsIContent* stackParent = *mOne.node;
      PRUnichar* buffer = mTwo.unicharPtr;
      uint32_t length = mFour.integer;
      nsIContent* table = *mThree.node;
      
      nsIContent* foster = table->GetParent();

      if (foster && foster->IsElement()) {
        aBuilder->FlushPendingAppendNotifications();

        nsHtml5OtherDocUpdate update(foster->OwnerDoc(),
                                     aBuilder->GetDocument());

        uint32_t pos = foster->IndexOf(table);

        nsIContent* previousSibling = table->GetPreviousSibling();
        if (previousSibling && previousSibling->IsNodeOfType(nsINode::eTEXT)) {
          return AppendTextToTextNode(buffer, 
                                      length, 
                                      previousSibling, 
                                      aBuilder);
        }
        
        nsCOMPtr<nsIContent> text;
        NS_NewTextNode(getter_AddRefs(text), aBuilder->GetNodeInfoManager());
        NS_ASSERTION(text, "Infallible malloc failed?");
        rv = text->SetText(buffer, length, false);
        NS_ENSURE_SUCCESS(rv, rv);
        
        rv = foster->InsertChildAt(text, pos, false);
        NS_ENSURE_SUCCESS(rv, rv);
        nsNodeUtils::ContentInserted(foster, text, pos);
        return rv;
      }
      
      return AppendText(buffer, length, stackParent, aBuilder);
    }
    case eTreeOpAppendComment: {
      nsIContent* parent = *mOne.node;
      PRUnichar* buffer = mTwo.unicharPtr;
      int32_t length = mFour.integer;
      
      nsCOMPtr<nsIContent> comment;
      NS_NewCommentNode(getter_AddRefs(comment), aBuilder->GetNodeInfoManager());
      NS_ASSERTION(comment, "Infallible malloc failed?");
      rv = comment->SetText(buffer, length, false);
      NS_ENSURE_SUCCESS(rv, rv);
      
      return Append(comment, parent, aBuilder);
    }
    case eTreeOpAppendCommentToDocument: {
      PRUnichar* buffer = mTwo.unicharPtr;
      int32_t length = mFour.integer;
      
      nsCOMPtr<nsIContent> comment;
      NS_NewCommentNode(getter_AddRefs(comment), aBuilder->GetNodeInfoManager());
      NS_ASSERTION(comment, "Infallible malloc failed?");
      rv = comment->SetText(buffer, length, false);
      NS_ENSURE_SUCCESS(rv, rv);
      
      return AppendToDocument(comment, aBuilder);
    }
    case eTreeOpAppendDoctypeToDocument: {
      nsCOMPtr<nsIAtom> name = Reget(mOne.atom);
      nsHtml5TreeOperationStringPair* pair = mTwo.stringPair;
      nsString publicId;
      nsString systemId;
      pair->Get(publicId, systemId);
      
      // Adapted from nsXMLContentSink
      // Create a new doctype node
      nsCOMPtr<nsIDOMDocumentType> docType;
      nsAutoString voidString;
      voidString.SetIsVoid(true);
      NS_NewDOMDocumentType(getter_AddRefs(docType),
                            aBuilder->GetNodeInfoManager(),
                            name,
                            publicId,
                            systemId,
                            voidString);
      NS_ASSERTION(docType, "Doctype creation failed.");
      nsCOMPtr<nsIContent> asContent = do_QueryInterface(docType);
      return AppendToDocument(asContent, aBuilder);
    }
    case eTreeOpMarkAsBroken: {
      aBuilder->MarkAsBroken(NS_ERROR_OUT_OF_MEMORY);
      return rv;
    }
    case eTreeOpRunScript: {
      nsIContent* node = *(mOne.node);
      nsAHtml5TreeBuilderState* snapshot = mTwo.state;
      if (snapshot) {
        aBuilder->InitializeDocWriteParserState(snapshot, mFour.integer);
      }
      *aScriptElement = node;
      return rv;
    }
    case eTreeOpRunScriptAsyncDefer: {
      nsIContent* node = *(mOne.node);
      aBuilder->RunScript(node);
      return rv;
    }
    case eTreeOpPreventScriptExecution: {
      nsIContent* node = *(mOne.node);
      nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(node);
      MOZ_ASSERT(sele);
      sele->PreventExecution();
      return rv;
    }
    case eTreeOpDoneAddingChildren: {
      nsIContent* node = *(mOne.node);
      node->DoneAddingChildren(aBuilder->HaveNotified(node));
      return rv;
    }
    case eTreeOpDoneCreatingElement: {
      nsIContent* node = *(mOne.node);
      node->DoneCreatingElement();
      return rv;
    }
    case eTreeOpFlushPendingAppendNotifications: {
      aBuilder->FlushPendingAppendNotifications();
      return rv;
    }
    case eTreeOpSetDocumentCharset: {
      char* str = mOne.charPtr;
      int32_t charsetSource = mFour.integer;
      nsDependentCString dependentString(str);
      aBuilder->SetDocumentCharsetAndSource(dependentString, charsetSource);
      return rv;
    }
    case eTreeOpNeedsCharsetSwitchTo: {
      char* str = mOne.charPtr;
      int32_t charsetSource = mFour.integer;
      int32_t lineNumber = mTwo.integer;
      aBuilder->NeedsCharsetSwitchTo(str, charsetSource, (uint32_t)lineNumber);
      return rv;    
    }
    case eTreeOpUpdateStyleSheet: {
      nsIContent* node = *(mOne.node);
      aBuilder->FlushPendingAppendNotifications();
      aBuilder->UpdateStyleSheet(node);
      return rv;
    }
    case eTreeOpProcessMeta: {
      nsIContent* node = *(mOne.node);
      rv = aBuilder->ProcessMETATag(node);
      return rv;
    }
    case eTreeOpProcessOfflineManifest: {
      PRUnichar* str = mOne.unicharPtr;
      nsDependentString dependentString(str);
      aBuilder->ProcessOfflineManifest(dependentString);
      return rv;
    }
    case eTreeOpMarkMalformedIfScript: {
      nsIContent* node = *(mOne.node);
      nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(node);
      if (sele) {
        // Make sure to serialize this script correctly, for nice round tripping.
        sele->SetIsMalformed();
      }
      return rv;
    }
    case eTreeOpStreamEnded: {
      aBuilder->DidBuildModel(false); // this causes a notifications flush anyway
      return rv;
    }
    case eTreeOpStartLayout: {
      aBuilder->StartLayout(); // this causes a notification flush anyway
      return rv;
    }
    case eTreeOpDocumentMode: {
      aBuilder->SetDocumentMode(mOne.mode);
      return rv;
    }
    case eTreeOpSetStyleLineNumber: {
      nsIContent* node = *(mOne.node);
      nsCOMPtr<nsIStyleSheetLinkingElement> ssle = do_QueryInterface(node);
      NS_ASSERTION(ssle, "Node didn't QI to style.");
      ssle->SetLineNumber(mFour.integer);
      return rv;
    }
    case eTreeOpSetScriptLineNumberAndFreeze: {
      nsIContent* node = *(mOne.node);
      nsCOMPtr<nsIScriptElement> sele = do_QueryInterface(node);
      NS_ASSERTION(sele, "Node didn't QI to script.");
      sele->SetScriptLineNumber(mFour.integer);
      sele->FreezeUriAsyncDefer();
      return rv;
    }
    case eTreeOpSvgLoad: {
      nsIContent* node = *(mOne.node);
      nsCOMPtr<nsIRunnable> event = new nsHtml5SVGLoadDispatcher(node);
      if (NS_FAILED(NS_DispatchToMainThread(event))) {
        NS_WARNING("failed to dispatch svg load dispatcher");
      }
      return rv;
    }
    case eTreeOpMaybeComplainAboutCharset: {
      char* msgId = mOne.charPtr;
      bool error = mTwo.integer;
      int32_t lineNumber = mThree.integer;
      aBuilder->MaybeComplainAboutCharset(msgId, error, (uint32_t)lineNumber);
      return rv;
    }
    case eTreeOpAddClass: {
      nsIContent* node = *(mOne.node);
      PRUnichar* str = mTwo.unicharPtr;
      nsDependentString depStr(str);
      // See viewsource.css for the possible classes
      nsAutoString klass;
      node->GetAttr(kNameSpaceID_None, nsGkAtoms::_class, klass);
      if (!klass.IsEmpty()) {
        klass.Append(' ');
        klass.Append(depStr);
        node->SetAttr(kNameSpaceID_None, nsGkAtoms::_class, klass, true);
      } else {
        node->SetAttr(kNameSpaceID_None, nsGkAtoms::_class, depStr, true);
      }
      return rv;
    }
    case eTreeOpAddLineNumberId: {
      nsIContent* node = *(mOne.node);
      int32_t lineNumber = mFour.integer;
      nsAutoString val(NS_LITERAL_STRING("line"));
      val.AppendInt(lineNumber);
      node->SetAttr(kNameSpaceID_None, nsGkAtoms::id, val, true);
      return rv;
    }
    case eTreeOpAddViewSourceHref: {
      nsIContent* node = *mOne.node;
      PRUnichar* buffer = mTwo.unicharPtr;
      int32_t length = mFour.integer;

      nsDependentString relative(buffer, length);

      nsIDocument* doc = aBuilder->GetDocument();

      const nsCString& charset = doc->GetDocumentCharacterSet();
      nsCOMPtr<nsIURI> uri;
      rv = NS_NewURI(getter_AddRefs(uri),
                     relative,
                     charset.get(),
                     aBuilder->GetViewSourceBaseURI());
      NS_ENSURE_SUCCESS(rv, rv);

      // Reuse the fix for bug 467852
      // URLs that execute script (e.g. "javascript:" URLs) should just be
      // ignored.  There's nothing reasonable we can do with them, and allowing
      // them to execute in the context of the view-source window presents a
      // security risk.  Just return the empty string in this case.
      bool openingExecutesScript = false;
      rv = NS_URIChainHasFlags(uri,
                               nsIProtocolHandler::URI_OPENING_EXECUTES_SCRIPT,
                               &openingExecutesScript);
      if (NS_FAILED(rv) || openingExecutesScript) {
        return NS_OK;
      }

      nsAutoCString viewSourceUrl;

      // URLs that return data (e.g. "http:" URLs) should be prefixed with
      // "view-source:".  URLs that don't return data should just be returned
      // undecorated.
      bool doesNotReturnData = false;
      rv = NS_URIChainHasFlags(uri,
                               nsIProtocolHandler::URI_DOES_NOT_RETURN_DATA,
                               &doesNotReturnData);
      NS_ENSURE_SUCCESS(rv, NS_OK);
      if (!doesNotReturnData) {
        viewSourceUrl.AssignLiteral("view-source:");
      }

      nsAutoCString spec;
      uri->GetSpec(spec);

      viewSourceUrl.Append(spec);

      nsAutoString utf16;
      CopyUTF8toUTF16(viewSourceUrl, utf16);

      node->SetAttr(kNameSpaceID_None, nsGkAtoms::href, utf16, true);
      return rv;
    }
    case eTreeOpAddError: {
      nsIContent* node = *(mOne.node);
      char* msgId = mTwo.charPtr;
      nsCOMPtr<nsIAtom> atom = Reget(mThree.atom);
      nsCOMPtr<nsIAtom> otherAtom = Reget(mFour.atom);
      // See viewsource.css for the possible classes in addition to "error".
      nsAutoString klass;
      node->GetAttr(kNameSpaceID_None, nsGkAtoms::_class, klass);
      if (!klass.IsEmpty()) {
        klass.Append(NS_LITERAL_STRING(" error"));
        node->SetAttr(kNameSpaceID_None, nsGkAtoms::_class, klass, true);
      } else {
        node->SetAttr(kNameSpaceID_None,
                      nsGkAtoms::_class,
                      NS_LITERAL_STRING("error"),
                      true);
      }

      nsXPIDLString message;
      if (otherAtom) {
        const PRUnichar* params[] = { atom->GetUTF16String(),
                                      otherAtom->GetUTF16String() };
        rv = nsContentUtils::FormatLocalizedString(
          nsContentUtils::eHTMLPARSER_PROPERTIES, msgId, params, message);
        NS_ENSURE_SUCCESS(rv, rv);
      } else if (atom) {
        const PRUnichar* params[] = { atom->GetUTF16String() };
        rv = nsContentUtils::FormatLocalizedString(
          nsContentUtils::eHTMLPARSER_PROPERTIES, msgId, params, message);
        NS_ENSURE_SUCCESS(rv, rv);
      } else {
        rv = nsContentUtils::GetLocalizedString(
          nsContentUtils::eHTMLPARSER_PROPERTIES, msgId, message);
        NS_ENSURE_SUCCESS(rv, rv);
      }

      nsAutoString title;
      node->GetAttr(kNameSpaceID_None, nsGkAtoms::title, title);
      if (!title.IsEmpty()) {
        title.Append('\n');
        title.Append(message);
        node->SetAttr(kNameSpaceID_None, nsGkAtoms::title, title, true);
      } else {
        node->SetAttr(kNameSpaceID_None, nsGkAtoms::title, message, true);
      }
      return rv;
    }
    default: {
      NS_NOTREACHED("Bogus tree op");
    }
  }
  return rv; // keep compiler happy
}
