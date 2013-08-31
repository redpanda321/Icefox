/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * MODULE NOTES:
 * @update  gess 4/1/98
 *
 * This file contains the declarations for all the HTML specific token types that
 * our DTD's understand. In fact, the same set of token types are used for XML.
 * Currently we have tokens for text, comments, start and end tags, entities,
 * attributes, style, script and skipped content. Whitespace and newlines also
 * have their own token types, but don't count on them to stay forever.
 *
 * If you're looking for the html tags, they're in a file called nsHTMLTag.h/cpp.
 *
 * Most of the token types have a similar API. They have methods to get the type
 * of token (GetTokenType); those that represent HTML tags also have a method to
 * get type tag type (GetTypeID). In addition, most have a method that causes the
 * token to help in the parsing process called (Consume). We've also thrown in a
 * few standard debugging methods as well.
 */

#ifndef HTMLTOKENS_H
#define HTMLTOKENS_H

#include "nsToken.h"
#include "nsHTMLTags.h"
#include "nsString.h"
#include "nsScannerString.h"

class nsScanner;

  /*******************************************************************
   * This enum defines the set of token types that we currently support.
   *******************************************************************/

enum eHTMLTokenTypes {
  eToken_unknown=0,
  eToken_start=1,      eToken_end,          eToken_comment,         eToken_entity,
  eToken_whitespace,   eToken_newline,      eToken_text,            eToken_attribute,
  eToken_instruction,  eToken_cdatasection, eToken_doctypeDecl,     eToken_markupDecl,
  eToken_last //make sure this stays the last token...
};

nsresult      ConsumeQuotedString(PRUnichar aChar,nsString& aString,nsScanner& aScanner);
nsresult      ConsumeAttributeText(PRUnichar aChar,nsString& aString,nsScanner& aScanner);
const PRUnichar* GetTagName(int32_t aTag);
//int32_t     FindEntityIndex(nsString& aString,int32_t aCount=-1);



/**
 *  This declares the basic token type used in the HTML DTD's.
 *  @update  gess 3/25/98
 */
class CHTMLToken : public CToken {
public:
  virtual ~CHTMLToken();
  CHTMLToken(eHTMLTags aTag);

  virtual eContainerInfo GetContainerInfo(void) const {return eFormUnknown;}
  virtual void SetContainerInfo(eContainerInfo aInfo) { }

protected:
};

/**
 *  This declares start tokens, which always take the form <xxxx>.
 *  This class also knows how to consume related attributes.
 *
 *  @update  gess 3/25/98
 */
class CStartToken: public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CStartToken(eHTMLTags aTag=eHTMLTag_unknown);
  CStartToken(const nsAString& aString);
  CStartToken(const nsAString& aName,eHTMLTags aTag);

  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTypeID(void);
  virtual int32_t GetTokenType(void);

  virtual bool IsEmpty(void);
  virtual void SetEmpty(bool aValue);

  virtual const nsSubstring& GetStringValue();
  virtual void GetSource(nsString& anOutputString);
  virtual void AppendSourceTo(nsAString& anOutputString);

  // the following info is used to set well-formedness state on start tags...
  virtual eContainerInfo GetContainerInfo(void) const {return mContainerInfo;}
  virtual void SetContainerInfo(eContainerInfo aContainerInfo) {
    if (eFormUnknown==mContainerInfo) {
      mContainerInfo=aContainerInfo;
    }
  }
  virtual bool IsWellFormed(void) const {
    return eWellFormed == mContainerInfo;
  }

  nsString mTextValue;
protected:
  eContainerInfo mContainerInfo;
  bool mEmpty;
#ifdef DEBUG
  bool mAttributed;
#endif
};


/**
 *  This declares end tokens, which always take the
 *  form </xxxx>. This class also knows how to consume
 *  related attributes.
 *
 *  @update  gess 3/25/98
 */
class CEndToken: public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CEndToken(eHTMLTags aTag);
  CEndToken(const nsAString& aString);
  CEndToken(const nsAString& aName,eHTMLTags aTag);
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTypeID(void);
  virtual int32_t GetTokenType(void);

  virtual const nsSubstring& GetStringValue();
  virtual void GetSource(nsString& anOutputString);
  virtual void AppendSourceTo(nsAString& anOutputString);

protected:
  nsString mTextValue;
};


/**
 *  This declares comment tokens. Comments are usually
 *  thought of as tokens, but we treat them that way
 *  here so that the parser can have a consistent view
 *  of all tokens.
 *
 *  @update  gess 3/25/98
 */
class CCommentToken: public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CCommentToken();
  CCommentToken(const nsAString& aString);
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTokenType(void);
  virtual const nsSubstring& GetStringValue(void);
  virtual void AppendSourceTo(nsAString& anOutputString);

  nsresult ConsumeStrictComment(nsScanner& aScanner);
  nsresult ConsumeQuirksComment(nsScanner& aScanner);

protected:
  nsScannerSubstring mComment; // does not include MDO & MDC
  nsScannerSubstring mCommentDecl; // includes MDO & MDC
};


/**
 *  This class declares entity tokens, which always take
 *  the form &xxxx;. This class also offers a few utility
 *  methods that allow you to easily reduce entities.
 *
 *  @update  gess 3/25/98
 */
class CEntityToken : public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CEntityToken();
  CEntityToken(const nsAString& aString);
  virtual int32_t GetTokenType(void);
  int32_t TranslateToUnicodeStr(nsString& aString);
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  static nsresult ConsumeEntity(PRUnichar aChar, nsString& aString,
                                nsScanner& aScanner);
  static int32_t TranslateToUnicodeStr(int32_t aValue,nsString& aString);

  virtual const nsSubstring& GetStringValue(void);
  virtual void GetSource(nsString& anOutputString);
  virtual void AppendSourceTo(nsAString& anOutputString);

protected:
  nsString mTextValue;
};


/**
 *  Whitespace tokens are used where whitespace can be
 *  detected as distinct from text. This allows us to
 *  easily skip leading/trailing whitespace when desired.
 *
 *  @update  gess 3/25/98
 */
class CWhitespaceToken: public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CWhitespaceToken();
  CWhitespaceToken(const nsAString& aString);
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTokenType(void);
  virtual const nsSubstring& GetStringValue(void);

protected:
  nsScannerSharedSubstring mTextValue;
};

/**
 *  Text tokens contain the normalized form of html text.
 *  These tokens are guaranteed not to contain entities,
 *  start or end tags, or newlines.
 *
 *  @update  gess 3/25/98
 */
class CTextToken: public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CTextToken();
  CTextToken(const nsAString& aString);
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTokenType(void);
  virtual int32_t GetTextLength(void);
  virtual void CopyTo(nsAString& aStr);
  virtual const nsSubstring& GetStringValue(void);
  virtual void Bind(nsScanner* aScanner, nsScannerIterator& aStart,
                    nsScannerIterator& aEnd);
  virtual void Bind(const nsAString& aStr);

  nsresult ConsumeCharacterData(bool aIgnoreComments,
                                nsScanner& aScanner,
                                const nsAString& aEndTagName,
                                int32_t aFlag,
                                bool& aFlushTokens);

  nsresult ConsumeParsedCharacterData(bool aDiscardFirstNewline,
                                      bool aConservativeConsume,
                                      nsScanner& aScanner,
                                      const nsAString& aEndTagName,
                                      int32_t aFlag,
                                      bool& aFound);

protected:
  nsScannerSubstring mTextValue;
};


/**
 *  CDATASection tokens contain raw unescaped text content delimited by
 *  a ![CDATA[ and ]].
 *  XXX Not really a HTML construct - maybe we need a separation
 *
 *  @update  vidur 11/12/98
 */
class CCDATASectionToken : public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CCDATASectionToken(eHTMLTags aTag = eHTMLTag_unknown);
  CCDATASectionToken(const nsAString& aString);
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTokenType(void);
  virtual const nsSubstring& GetStringValue(void);

protected:
  nsString mTextValue;
};


/**
 *  Declaration tokens contain raw unescaped text content (not really, but
 *  right now we use this only for view source).
 *  XXX Not really a HTML construct - maybe we need a separation
 *
 */
class CMarkupDeclToken : public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CMarkupDeclToken();
  CMarkupDeclToken(const nsAString& aString);
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTokenType(void);
  virtual const nsSubstring& GetStringValue(void);

protected:
  nsScannerSubstring  mTextValue;
};


/**
 *  Attribute tokens are used to contain attribute key/value
 *  pairs whereever they may occur. Typically, they should
 *  occur only in start tokens. However, we may expand that
 *  ability when XML tokens become commonplace.
 *
 *  @update  gess 3/25/98
 */
class CAttributeToken: public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CAttributeToken();
  CAttributeToken(const nsAString& aString);
  CAttributeToken(const nsAString& aKey, const nsAString& aString);
  ~CAttributeToken() {}
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTokenType(void);
  const nsSubstring&     GetKey(void) { return mTextKey.AsString(); }
  virtual void SetKey(const nsAString& aKey);
  virtual void BindKey(nsScanner* aScanner, nsScannerIterator& aStart,
                       nsScannerIterator& aEnd);
  const nsSubstring& GetValue(void) {return mTextValue.str();}
  virtual const nsSubstring& GetStringValue(void);
  virtual void GetSource(nsString& anOutputString);
  virtual void AppendSourceTo(nsAString& anOutputString);

  bool mHasEqualWithoutValue;
protected:
  nsScannerSharedSubstring mTextValue;
  nsScannerSubstring mTextKey;
};


/**
 *  Newline tokens contain, you guessed it, newlines.
 *  They consume newline (CR/LF) either alone or in pairs.
 *
 *  @update  gess 3/25/98
 */
class CNewlineToken: public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CNewlineToken();
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTokenType(void);
  virtual const nsSubstring& GetStringValue(void);

  static void AllocNewline();
  static void FreeNewline();
};


/**
 *  Whitespace tokens are used where whitespace can be
 *  detected as distinct from text. This allows us to
 *  easily skip leading/trailing whitespace when desired.
 *
 *  @update  gess 3/25/98
 */
class CInstructionToken: public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CInstructionToken();
  CInstructionToken(const nsAString& aString);
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTokenType(void);
  virtual const nsSubstring& GetStringValue(void);

protected:
  nsString mTextValue;
};


/**
 * This token is generated by the HTML and Expat tokenizers
 * when they see the doctype declaration ("<!DOCTYPE ... >")
 *
 */

class CDoctypeDeclToken: public CHTMLToken {
  CTOKEN_IMPL_SIZEOF

public:
  CDoctypeDeclToken(eHTMLTags aTag=eHTMLTag_unknown);
  CDoctypeDeclToken(const nsAString& aString,eHTMLTags aTag=eHTMLTag_unknown);
  virtual nsresult Consume(PRUnichar aChar,nsScanner& aScanner,int32_t aMode);
  virtual int32_t GetTokenType(void);
  virtual const nsSubstring& GetStringValue(void);
  virtual void SetStringValue(const nsAString& aStr);

protected:
  nsString mTextValue;
};

#endif
