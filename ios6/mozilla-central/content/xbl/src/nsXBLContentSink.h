/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsXBLContentSink_h__
#define nsXBLContentSink_h__

#include "nsXMLContentSink.h"
#include "nsXBLDocumentInfo.h"
#include "nsXBLPrototypeHandler.h"
#include "nsXBLProtoImpl.h"
#include "nsLayoutCID.h"

/*
 * Enum that describes the primary state of the parsing process
 */
typedef enum {
  eXBL_InDocument,       /* outside any bindings */
  eXBL_InBindings,       /* Inside a <bindings> element */
  eXBL_InBinding,        /* Inside a <binding> */
  eXBL_InResources,      /* Inside a <resources> */
  eXBL_InImplementation, /* Inside a <implementation> */
  eXBL_InHandlers,       /* Inside a <handlers> */
  eXBL_Error             /* An error has occurred.  Suspend binding construction */
} XBLPrimaryState;

/*
 * Enum that describes our substate (typically when parsing something
 * like <handlers> or <implementation>).
 */
typedef enum {
  eXBL_None,
  eXBL_InHandler,
  eXBL_InMethod,
  eXBL_InProperty,
  eXBL_InField,
  eXBL_InBody,
  eXBL_InGetter,
  eXBL_InSetter,
  eXBL_InConstructor,
  eXBL_InDestructor
} XBLSecondaryState;

class nsXULPrototypeElement;
class nsXBLProtoImplMember;
class nsXBLProtoImplProperty;
class nsXBLProtoImplMethod;
class nsXBLProtoImplField;
class nsXBLPrototypeBinding;

// The XBL content sink overrides the XML content sink to
// builds its own lightweight data structures for the <resources>,
// <handlers>, <implementation>, and 

class nsXBLContentSink : public nsXMLContentSink {
public:
  nsXBLContentSink();
  ~nsXBLContentSink();

  NS_DECL_AND_IMPL_ZEROING_OPERATOR_NEW

  nsresult Init(nsIDocument* aDoc,
                nsIURI* aURL,
                nsISupports* aContainer);

  // nsIContentSink overrides
  NS_IMETHOD HandleStartElement(const PRUnichar *aName, 
                                const PRUnichar **aAtts, 
                                uint32_t aAttsCount, 
                                int32_t aIndex, 
                                uint32_t aLineNumber);

  NS_IMETHOD HandleEndElement(const PRUnichar *aName);
  
  NS_IMETHOD HandleCDataSection(const PRUnichar *aData, 
                                uint32_t aLength);

protected:
    // nsXMLContentSink overrides
    virtual void MaybeStartLayout(bool aIgnorePendingSheets);

    bool OnOpenContainer(const PRUnichar **aAtts, 
                           uint32_t aAttsCount, 
                           int32_t aNameSpaceID, 
                           nsIAtom* aTagName,
                           uint32_t aLineNumber);

    bool NotifyForDocElement() { return false; }

    nsresult CreateElement(const PRUnichar** aAtts, uint32_t aAttsCount,
                           nsINodeInfo* aNodeInfo, uint32_t aLineNumber,
                           nsIContent** aResult, bool* aAppendContent,
                           mozilla::dom::FromParser aFromParser);
    
    nsresult AddAttributes(const PRUnichar** aAtts, 
                           nsIContent* aContent);

#ifdef MOZ_XUL    
    nsresult AddAttributesToXULPrototype(const PRUnichar **aAtts, 
                                         uint32_t aAttsCount, 
                                         nsXULPrototypeElement* aElement);
#endif

    // Our own helpers for constructing XBL prototype objects.
    nsresult ConstructBinding(uint32_t aLineNumber);
    void ConstructHandler(const PRUnichar **aAtts, uint32_t aLineNumber);
    void ConstructResource(const PRUnichar **aAtts, nsIAtom* aResourceType);
    void ConstructImplementation(const PRUnichar **aAtts);
    void ConstructProperty(const PRUnichar **aAtts, uint32_t aLineNumber);
    void ConstructMethod(const PRUnichar **aAtts);
    void ConstructParameter(const PRUnichar **aAtts);
    void ConstructField(const PRUnichar **aAtts, uint32_t aLineNumber);
  

  // nsXMLContentSink overrides
  nsresult FlushText(bool aReleaseTextNode = true);

  // nsIExpatSink overrides
  NS_IMETHOD ReportError(const PRUnichar* aErrorText,
                         const PRUnichar* aSourceText,
                         nsIScriptError *aError,
                         bool *_retval);

protected:
  nsresult ReportUnexpectedElement(nsIAtom* aElementName, uint32_t aLineNumber);

  void AddMember(nsXBLProtoImplMember* aMember);
  void AddField(nsXBLProtoImplField* aField);
  
  XBLPrimaryState mState;
  XBLSecondaryState mSecondaryState;
  nsXBLDocumentInfo* mDocInfo;
  bool mIsChromeOrResource; // For bug #45989
  bool mFoundFirstBinding;

  nsXBLPrototypeBinding* mBinding;
  nsXBLPrototypeHandler* mHandler; // current handler, owned by its PrototypeBinding
  nsXBLProtoImpl* mImplementation;
  nsXBLProtoImplMember* mImplMember;
  nsXBLProtoImplField* mImplField;
  nsXBLProtoImplProperty* mProperty;
  nsXBLProtoImplMethod* mMethod;
  nsXBLProtoImplField* mField;
};

nsresult
NS_NewXBLContentSink(nsIXMLContentSink** aResult,
                     nsIDocument* aDoc,
                     nsIURI* aURL,
                     nsISupports* aContainer);
#endif // nsXBLContentSink_h__
