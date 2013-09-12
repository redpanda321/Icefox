/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "nsXULPrototypeDocument.h"
#include "nsXULDocument.h"

#include "nsAString.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIPrincipal.h"
#include "nsJSPrincipals.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIScriptSecurityManager.h"
#include "nsIScriptRuntime.h"
#include "nsIServiceManager.h"
#include "nsIArray.h"
#include "nsIURI.h"
#include "jsapi.h"
#include "nsString.h"
#include "nsIConsoleService.h"
#include "nsIScriptError.h"
#include "nsIDOMScriptObjectFactory.h"
#include "nsDOMCID.h"
#include "nsNodeInfoManager.h"
#include "nsContentUtils.h"
#include "nsCCUncollectableMarker.h"
#include "nsDOMJSUtils.h" // for GetScriptContextFromJSContext
#include "xpcpublic.h"
#include "mozilla/dom/BindingUtils.h"

using mozilla::dom::DestroyProtoAndIfaceCache;

static NS_DEFINE_CID(kDOMScriptObjectFactoryCID,
                     NS_DOM_SCRIPT_OBJECT_FACTORY_CID);


class nsXULPDGlobalObject : public nsIScriptGlobalObject,
                            public nsIScriptObjectPrincipal
{
public:
    nsXULPDGlobalObject(nsXULPrototypeDocument* owner);

    // nsISupports interface
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS

    // nsIScriptGlobalObject methods
    virtual void OnFinalize(JSObject* aObject);
    virtual void SetScriptsEnabled(bool aEnabled, bool aFireTimeouts);

    virtual JSObject* GetGlobalJSObject();
    virtual nsresult EnsureScriptEnvironment();

    virtual nsIScriptContext *GetScriptContext();

    // nsIScriptObjectPrincipal methods
    virtual nsIPrincipal* GetPrincipal();

    NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsXULPDGlobalObject,
                                             nsIScriptGlobalObject)

    void ClearGlobalObjectOwner();

protected:
    virtual ~nsXULPDGlobalObject();

    nsXULPrototypeDocument* mGlobalObjectOwner; // weak reference

    nsCOMPtr<nsIScriptContext> mContext;
    JSObject* mJSObject;

    nsCOMPtr<nsIPrincipal> mCachedPrincipal;

    static JSClass gSharedGlobalClass;
};

nsIPrincipal* nsXULPrototypeDocument::gSystemPrincipal;
nsXULPDGlobalObject* nsXULPrototypeDocument::gSystemGlobal;
uint32_t nsXULPrototypeDocument::gRefCnt;


void
nsXULPDGlobalObject_finalize(JSFreeOp *fop, JSObject *obj)
{
    nsISupports *nativeThis = (nsISupports*)JS_GetPrivate(obj);

    nsCOMPtr<nsIScriptGlobalObject> sgo(do_QueryInterface(nativeThis));

    if (sgo) {
        sgo->OnFinalize(obj);
    }

    // The addref was part of JSObject construction
    NS_RELEASE(nativeThis);
}


JSBool
nsXULPDGlobalObject_resolve(JSContext *cx, JSHandleObject obj, JSHandleId id)
{
    JSBool did_resolve = JS_FALSE;

    return JS_ResolveStandardClass(cx, obj, id, &did_resolve);
}


JSClass nsXULPDGlobalObject::gSharedGlobalClass = {
    "nsXULPrototypeScript compilation scope",
    JSCLASS_HAS_PRIVATE | JSCLASS_PRIVATE_IS_NSISUPPORTS |
    JSCLASS_IMPLEMENTS_BARRIERS | JSCLASS_GLOBAL_FLAGS_WITH_SLOTS(0),
    JS_PropertyStub,  JS_PropertyStub, JS_PropertyStub, JS_StrictPropertyStub,
    JS_EnumerateStub, nsXULPDGlobalObject_resolve,  JS_ConvertStub,
    nsXULPDGlobalObject_finalize, NULL, NULL, NULL, NULL,
    NULL
};



//----------------------------------------------------------------------
//
// ctors, dtors, n' stuff
//

nsXULPrototypeDocument::nsXULPrototypeDocument()
    : mRoot(nullptr),
      mLoaded(false),
      mCCGeneration(0)
{
    ++gRefCnt;
}


nsresult
nsXULPrototypeDocument::Init()
{
    mNodeInfoManager = new nsNodeInfoManager();
    NS_ENSURE_TRUE(mNodeInfoManager, NS_ERROR_OUT_OF_MEMORY);

    return mNodeInfoManager->Init(nullptr);
}

nsXULPrototypeDocument::~nsXULPrototypeDocument()
{
    if (mGlobalObject) {
        // cleaup cycles etc.
        mGlobalObject->ClearGlobalObjectOwner();
    }

    if (mRoot)
        mRoot->ReleaseSubtree();

    if (--gRefCnt == 0) {
        NS_IF_RELEASE(gSystemPrincipal);
        NS_IF_RELEASE(gSystemGlobal);
    }
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsXULPrototypeDocument)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsXULPrototypeDocument)
    tmp->mPrototypeWaiters.Clear();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsXULPrototypeDocument)
    if (nsCCUncollectableMarker::InGeneration(cb, tmp->mCCGeneration)) {
        return NS_SUCCESS_INTERRUPTED_TRAVERSE;
    }
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mRoot)
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mGlobalObject");
    cb.NoteXPCOMChild(static_cast<nsIScriptGlobalObject*>(tmp->mGlobalObject));
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mNodeInfoManager)
    for (uint32_t i = 0; i < tmp->mPrototypeWaiters.Length(); ++i) {
        NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mPrototypeWaiters[i]");
        cb.NoteXPCOMChild(static_cast<nsINode*>(tmp->mPrototypeWaiters[i].get()));
    }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsXULPrototypeDocument)
    NS_INTERFACE_MAP_ENTRY(nsIScriptGlobalObjectOwner)
    NS_INTERFACE_MAP_ENTRY(nsISerializable)
    NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIScriptGlobalObjectOwner)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsXULPrototypeDocument)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsXULPrototypeDocument)

NS_IMETHODIMP
NS_NewXULPrototypeDocument(nsXULPrototypeDocument** aResult)
{
    *aResult = new nsXULPrototypeDocument();
    if (! *aResult)
        return NS_ERROR_OUT_OF_MEMORY;

    nsresult rv;
    rv = (*aResult)->Init();
    if (NS_FAILED(rv)) {
        delete *aResult;
        *aResult = nullptr;
        return rv;
    }

    NS_ADDREF(*aResult);
    return rv;
}

// Helper method that shares a system global among all prototype documents
// that have the system principal as their security principal.   Called by
// nsXULPrototypeDocument::Read and
// nsXULPrototypeDocument::GetScriptGlobalObject.
// This method greatly reduces the number of nsXULPDGlobalObjects and their
// nsIScriptContexts in apps that load many XUL documents via chrome: URLs.

nsXULPDGlobalObject *
nsXULPrototypeDocument::NewXULPDGlobalObject()
{
    // Now compare DocumentPrincipal() to gSystemPrincipal, in order to create
    // gSystemGlobal if the two pointers are equal.  Thus, gSystemGlobal
    // implies gSystemPrincipal.
    nsXULPDGlobalObject *global;
    if (DocumentPrincipal() == gSystemPrincipal) {
        if (!gSystemGlobal) {
            gSystemGlobal = new nsXULPDGlobalObject(nullptr);
            if (! gSystemGlobal)
                return nullptr;
            NS_ADDREF(gSystemGlobal);
        }
        global = gSystemGlobal;
    } else {
        global = new nsXULPDGlobalObject(this); // does not refcount
        if (! global)
            return nullptr;
    }
    return global;
}

//----------------------------------------------------------------------
//
// nsISerializable methods
//

NS_IMETHODIMP
nsXULPrototypeDocument::Read(nsIObjectInputStream* aStream)
{
    nsresult rv;

    rv = aStream->ReadObject(true, getter_AddRefs(mURI));

    uint32_t count, i;
    nsCOMPtr<nsIURI> styleOverlayURI;

    nsresult tmp = aStream->Read32(&count);
    if (NS_FAILED(tmp)) {
      return tmp;
    }
    if (NS_FAILED(rv)) {
      return rv;
    }

    for (i = 0; i < count; ++i) {
        tmp = aStream->ReadObject(true, getter_AddRefs(styleOverlayURI));
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
        mStyleSheetReferences.AppendObject(styleOverlayURI);
    }


    // nsIPrincipal mNodeInfoManager->mPrincipal
    nsCOMPtr<nsIPrincipal> principal;
    tmp = aStream->ReadObject(true, getter_AddRefs(principal));
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }
    // Better safe than sorry....
    mNodeInfoManager->SetDocumentPrincipal(principal);


    // nsIScriptGlobalObject mGlobalObject
    mGlobalObject = NewXULPDGlobalObject();
    if (! mGlobalObject)
        return NS_ERROR_OUT_OF_MEMORY;

    mRoot = new nsXULPrototypeElement();
    if (! mRoot)
       return NS_ERROR_OUT_OF_MEMORY;

    // nsINodeInfo table
    nsCOMArray<nsINodeInfo> nodeInfos;

    tmp = aStream->Read32(&count);
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }
    nsAutoString namespaceURI, prefixStr, localName;
    bool prefixIsNull;
    nsCOMPtr<nsIAtom> prefix;
    for (i = 0; i < count; ++i) {
        tmp = aStream->ReadString(namespaceURI);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
        tmp = aStream->ReadBoolean(&prefixIsNull);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
        if (prefixIsNull) {
            prefix = nullptr;
        } else {
            tmp = aStream->ReadString(prefixStr);
            if (NS_FAILED(tmp)) {
              rv = tmp;
            }
            prefix = do_GetAtom(prefixStr);
        }
        tmp = aStream->ReadString(localName);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }

        nsCOMPtr<nsINodeInfo> nodeInfo;
        // Using UINT16_MAX here as we don't know which nodeinfos will be
        // used for attributes and which for elements. And that doesn't really
        // matter.
        tmp = mNodeInfoManager->GetNodeInfo(localName, prefix, namespaceURI,
                                            UINT16_MAX,
                                            getter_AddRefs(nodeInfo));
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
        if (!nodeInfos.AppendObject(nodeInfo))
          rv = NS_ERROR_OUT_OF_MEMORY;
    }

    // Document contents
    uint32_t type;
    while (NS_SUCCEEDED(rv)) {
        tmp = aStream->Read32(&type);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }

        if ((nsXULPrototypeNode::Type)type == nsXULPrototypeNode::eType_PI) {
            nsRefPtr<nsXULPrototypePI> pi = new nsXULPrototypePI();
            if (! pi) {
               rv = NS_ERROR_OUT_OF_MEMORY;
               break;
            }

            tmp = pi->Deserialize(aStream, mGlobalObject, mURI, &nodeInfos);
            if (NS_FAILED(tmp)) {
              rv = tmp;
            }
            tmp = AddProcessingInstruction(pi);
            if (NS_FAILED(tmp)) {
              rv = tmp;
            }
        } else if ((nsXULPrototypeNode::Type)type == nsXULPrototypeNode::eType_Element) {
            tmp = mRoot->Deserialize(aStream, mGlobalObject, mURI, &nodeInfos);
            if (NS_FAILED(tmp)) {
              rv = tmp;
            }
            break;
        } else {
            NS_NOTREACHED("Unexpected prototype node type");
            rv = NS_ERROR_FAILURE;
            break;
        }
    }
    tmp = NotifyLoadDone();
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }

    return rv;
}

static nsresult
GetNodeInfos(nsXULPrototypeElement* aPrototype,
             nsCOMArray<nsINodeInfo>& aArray)
{
    nsresult rv;
    if (aArray.IndexOf(aPrototype->mNodeInfo) < 0) {
        if (!aArray.AppendObject(aPrototype->mNodeInfo)) {
            return NS_ERROR_OUT_OF_MEMORY;
        }
    }

    // Search attributes
    uint32_t i;
    for (i = 0; i < aPrototype->mNumAttributes; ++i) {
        nsCOMPtr<nsINodeInfo> ni;
        nsAttrName* name = &aPrototype->mAttributes[i].mName;
        if (name->IsAtom()) {
            ni = aPrototype->mNodeInfo->NodeInfoManager()->
                GetNodeInfo(name->Atom(), nullptr, kNameSpaceID_None,
                            nsIDOMNode::ATTRIBUTE_NODE);
            NS_ENSURE_TRUE(ni, NS_ERROR_OUT_OF_MEMORY);
        }
        else {
            ni = name->NodeInfo();
        }

        if (aArray.IndexOf(ni) < 0) {
            if (!aArray.AppendObject(ni)) {
                return NS_ERROR_OUT_OF_MEMORY;
            }
        }
    }

    // Search children
    for (i = 0; i < aPrototype->mChildren.Length(); ++i) {
        nsXULPrototypeNode* child = aPrototype->mChildren[i];
        if (child->mType == nsXULPrototypeNode::eType_Element) {
            rv = GetNodeInfos(static_cast<nsXULPrototypeElement*>(child),
                              aArray);
            NS_ENSURE_SUCCESS(rv, rv);
        }
    }

    return NS_OK;
}

NS_IMETHODIMP
nsXULPrototypeDocument::Write(nsIObjectOutputStream* aStream)
{
    nsresult rv;

    rv = aStream->WriteCompoundObject(mURI, NS_GET_IID(nsIURI), true);
    
    uint32_t count;

    count = mStyleSheetReferences.Count();
    nsresult tmp = aStream->Write32(count);
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }

    uint32_t i;
    for (i = 0; i < count; ++i) {
        tmp = aStream->WriteCompoundObject(mStyleSheetReferences[i],
                                           NS_GET_IID(nsIURI), true);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
    }

    // nsIPrincipal mNodeInfoManager->mPrincipal
    tmp = aStream->WriteObject(mNodeInfoManager->DocumentPrincipal(),
                               true);
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }
    
#ifdef DEBUG
    // XXX Worrisome if we're caching things without system principal.
    if (!nsContentUtils::IsSystemPrincipal(mNodeInfoManager->DocumentPrincipal())) {
        NS_WARNING("Serializing document without system principal");
    }
#endif

    // nsINodeInfo table
    nsCOMArray<nsINodeInfo> nodeInfos;
    if (mRoot) {
      tmp = GetNodeInfos(mRoot, nodeInfos);
      if (NS_FAILED(tmp)) {
        rv = tmp;
      }
    }

    uint32_t nodeInfoCount = nodeInfos.Count();
    tmp = aStream->Write32(nodeInfoCount);
    if (NS_FAILED(tmp)) {
      rv = tmp;
    }
    for (i = 0; i < nodeInfoCount; ++i) {
        nsINodeInfo *nodeInfo = nodeInfos[i];
        NS_ENSURE_TRUE(nodeInfo, NS_ERROR_FAILURE);

        nsAutoString namespaceURI;
        tmp = nodeInfo->GetNamespaceURI(namespaceURI);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
        tmp = aStream->WriteWStringZ(namespaceURI.get());
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }

        nsAutoString prefix;
        nodeInfo->GetPrefix(prefix);
        bool nullPrefix = DOMStringIsNull(prefix);
        tmp = aStream->WriteBoolean(nullPrefix);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
        if (!nullPrefix) {
            tmp = aStream->WriteWStringZ(prefix.get());
            if (NS_FAILED(tmp)) {
              rv = tmp;
            }
        }

        nsAutoString localName;
        nodeInfo->GetName(localName);
        tmp = aStream->WriteWStringZ(localName.get());
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
    }

    // Now serialize the document contents
    nsIScriptGlobalObject* globalObject = GetScriptGlobalObject();
    NS_ENSURE_TRUE(globalObject, NS_ERROR_UNEXPECTED);

    count = mProcessingInstructions.Length();
    for (i = 0; i < count; ++i) {
        nsXULPrototypePI* pi = mProcessingInstructions[i];
        tmp = pi->Serialize(aStream, globalObject, &nodeInfos);
        if (NS_FAILED(tmp)) {
          rv = tmp;
        }
    }

    if (mRoot) {
      tmp = mRoot->Serialize(aStream, globalObject, &nodeInfos);
      if (NS_FAILED(tmp)) {
        rv = tmp;
      }
    }
 
    return rv;
}


//----------------------------------------------------------------------
//

nsresult
nsXULPrototypeDocument::InitPrincipal(nsIURI* aURI, nsIPrincipal* aPrincipal)
{
    NS_ENSURE_ARG_POINTER(aURI);

    mURI = aURI;
    mNodeInfoManager->SetDocumentPrincipal(aPrincipal);
    return NS_OK;
}
    

nsIURI*
nsXULPrototypeDocument::GetURI()
{
    NS_ASSERTION(mURI, "null URI");
    return mURI;
}


nsXULPrototypeElement*
nsXULPrototypeDocument::GetRootElement()
{
    return mRoot;
}


void
nsXULPrototypeDocument::SetRootElement(nsXULPrototypeElement* aElement)
{
    mRoot = aElement;
}

nsresult
nsXULPrototypeDocument::AddProcessingInstruction(nsXULPrototypePI* aPI)
{
    NS_PRECONDITION(aPI, "null ptr");
    if (!mProcessingInstructions.AppendElement(aPI)) {
        return NS_ERROR_OUT_OF_MEMORY;
    }
    return NS_OK;
}

const nsTArray<nsRefPtr<nsXULPrototypePI> >&
nsXULPrototypeDocument::GetProcessingInstructions() const
{
    return mProcessingInstructions;
}

void
nsXULPrototypeDocument::AddStyleSheetReference(nsIURI* aURI)
{
    NS_PRECONDITION(aURI, "null ptr");
    if (!mStyleSheetReferences.AppendObject(aURI)) {
        NS_WARNING("mStyleSheetReferences->AppendElement() failed."
                   "Stylesheet overlay dropped.");
    }
}

const nsCOMArray<nsIURI>&
nsXULPrototypeDocument::GetStyleSheetReferences() const
{
    return mStyleSheetReferences;
}

NS_IMETHODIMP
nsXULPrototypeDocument::GetHeaderData(nsIAtom* aField, nsAString& aData) const
{
    // XXX Not implemented
    aData.Truncate();
    return NS_OK;
}


NS_IMETHODIMP
nsXULPrototypeDocument::SetHeaderData(nsIAtom* aField, const nsAString& aData)
{
    // XXX Not implemented
    return NS_OK;
}



nsIPrincipal*
nsXULPrototypeDocument::DocumentPrincipal()
{
    NS_PRECONDITION(mNodeInfoManager, "missing nodeInfoManager");
    return mNodeInfoManager->DocumentPrincipal();
}

void
nsXULPrototypeDocument::SetDocumentPrincipal(nsIPrincipal* aPrincipal)
{
    mNodeInfoManager->SetDocumentPrincipal(aPrincipal);
}

nsNodeInfoManager*
nsXULPrototypeDocument::GetNodeInfoManager()
{
    return mNodeInfoManager;
}


nsresult
nsXULPrototypeDocument::AwaitLoadDone(nsXULDocument* aDocument, bool* aResult)
{
    nsresult rv = NS_OK;

    *aResult = mLoaded;

    if (!mLoaded) {
        rv = mPrototypeWaiters.AppendElement(aDocument)
              ? NS_OK : NS_ERROR_OUT_OF_MEMORY; // addrefs
    }

    return rv;
}


nsresult
nsXULPrototypeDocument::NotifyLoadDone()
{
    // Call back to each XUL document that raced to start the same
    // prototype document load, lost the race, but hit the XUL
    // prototype cache because the winner filled the cache with
    // the not-yet-loaded prototype object.

    nsresult rv = NS_OK;

    mLoaded = true;

    for (uint32_t i = mPrototypeWaiters.Length(); i > 0; ) {
        --i;
        // true means that OnPrototypeLoadDone will also
        // call ResumeWalk().
        rv = mPrototypeWaiters[i]->OnPrototypeLoadDone(true);
        if (NS_FAILED(rv)) break;
    }
    mPrototypeWaiters.Clear();

    return rv;
}

//----------------------------------------------------------------------
//
// nsIScriptGlobalObjectOwner methods
//

nsIScriptGlobalObject*
nsXULPrototypeDocument::GetScriptGlobalObject()
{
    if (!mGlobalObject)
        mGlobalObject = NewXULPDGlobalObject();

    return mGlobalObject;
}

//----------------------------------------------------------------------
//
// nsXULPDGlobalObject
//

nsXULPDGlobalObject::nsXULPDGlobalObject(nsXULPrototypeDocument* owner)
  : mGlobalObjectOwner(owner)
  , mJSObject(NULL)
{
}


nsXULPDGlobalObject::~nsXULPDGlobalObject()
{
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsXULPDGlobalObject)
NS_IMPL_CYCLE_COLLECTION_UNLINK_0(nsXULPDGlobalObject)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsXULPDGlobalObject)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mContext)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsXULPDGlobalObject)
  NS_INTERFACE_MAP_ENTRY(nsIScriptGlobalObject)
  NS_INTERFACE_MAP_ENTRY(nsIScriptObjectPrincipal)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIScriptGlobalObject)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsXULPDGlobalObject)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsXULPDGlobalObject)

//----------------------------------------------------------------------
//
// nsIScriptGlobalObject methods
//

nsresult
nsXULPDGlobalObject::EnsureScriptEnvironment()
{
  if (mContext) {
    return NS_OK;
  }
  NS_ASSERTION(!mJSObject, "Have global without context?");

  nsCOMPtr<nsIScriptRuntime> languageRuntime;
  nsresult rv = NS_GetJSRuntime(getter_AddRefs(languageRuntime));
  NS_ENSURE_SUCCESS(rv, NS_OK);

  nsCOMPtr<nsIScriptContext> ctxNew = languageRuntime->CreateContext(false, nullptr);
  MOZ_ASSERT(ctxNew);

  // We have to setup a special global object.  We do this then
  // attach it as the global for this context.  Then, we
  // will re-fetch the global and set it up in our language globals array.
  {
    JSContext *cx = ctxNew->GetNativeContext();
    JSAutoRequest ar(cx);

    JSObject *newGlob = JS_NewGlobalObject(cx, &gSharedGlobalClass,
                                           nsJSPrincipals::get(GetPrincipal()));
    if (!newGlob)
        return NS_OK;

    ::JS_SetGlobalObject(cx, newGlob);

    // Add an owning reference from JS back to us. This'll be
    // released when the JSObject is finalized.
    ::JS_SetPrivate(newGlob, this);
    NS_ADDREF(this);
  }

  // should probably assert the context is clean???
  ctxNew->WillInitializeContext();
  rv = ctxNew->InitContext();
  NS_ENSURE_SUCCESS(rv, NS_OK);

  ctxNew->DidInitializeContext();

  JSObject* global = ctxNew->GetNativeGlobal();
  NS_ASSERTION(global, "GetNativeGlobal returned NULL!");

  mContext = ctxNew;
  mJSObject = global;

  // Set the location information for the new global, so that tools like
  // about:memory may use that information
  nsIURI *ownerURI = mGlobalObjectOwner->GetURI();
  xpc::SetLocationForGlobal(mJSObject, ownerURI);

  return NS_OK;
}

nsIScriptContext*
nsXULPDGlobalObject::GetScriptContext()
{
  // This global object creates a context on demand - do that now.
  nsresult rv = EnsureScriptEnvironment();
  if (NS_FAILED(rv)) {
    NS_ERROR("Failed to setup script language");
    return NULL;
  }

  return mContext;
}

JSObject*
nsXULPDGlobalObject::GetGlobalJSObject()
{
  return xpc_UnmarkGrayObject(mJSObject);
}


void
nsXULPDGlobalObject::ClearGlobalObjectOwner()
{
  NS_ASSERTION(!mCachedPrincipal, "This shouldn't ever be set until now!");

  // Cache mGlobalObjectOwner's principal if possible.
  if (this != nsXULPrototypeDocument::gSystemGlobal)
    mCachedPrincipal = mGlobalObjectOwner->DocumentPrincipal();

  mContext = NULL;
  mGlobalObjectOwner = NULL;
}


void
nsXULPDGlobalObject::OnFinalize(JSObject* aObject)
{
  mJSObject = NULL;
}

void
nsXULPDGlobalObject::SetScriptsEnabled(bool aEnabled, bool aFireTimeouts)
{
  // We don't care...
}

//----------------------------------------------------------------------
//
// nsIScriptObjectPrincipal methods
//

nsIPrincipal*
nsXULPDGlobalObject::GetPrincipal()
{
    if (!mGlobalObjectOwner) {
        // See nsXULPrototypeDocument::NewXULPDGlobalObject, the comment
        // about gSystemGlobal implying gSystemPrincipal.
        if (this == nsXULPrototypeDocument::gSystemGlobal) {
            return nsXULPrototypeDocument::gSystemPrincipal;
        }
        // Return the cached principal if it exists.
        return mCachedPrincipal;
    }

    return mGlobalObjectOwner->DocumentPrincipal();
}
