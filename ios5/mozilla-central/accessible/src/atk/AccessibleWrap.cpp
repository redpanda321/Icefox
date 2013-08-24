/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AccessibleWrap.h"

#include "Accessible-inl.h"
#include "ApplicationAccessibleWrap.h"
#include "InterfaceInitFuncs.h"
#include "nsAccUtils.h"
#include "nsIAccessibleRelation.h"
#include "RootAccessible.h"
#include "nsIAccessibleValue.h"
#include "nsMai.h"
#include "nsMaiHyperlink.h"
#include "nsString.h"
#include "nsAutoPtr.h"
#include "prprf.h"
#include "nsStateMap.h"
#include "Relation.h"
#include "RootAccessible.h"
#include "States.h"

#include "mozilla/Util.h"
#include "nsXPCOMStrings.h"
#include "nsComponentManagerUtils.h"

using namespace mozilla;
using namespace mozilla::a11y;

AccessibleWrap::EAvailableAtkSignals AccessibleWrap::gAvailableAtkSignals =
  eUnknown;

//defined in ApplicationAccessibleWrap.cpp
extern "C" GType g_atk_hyperlink_impl_type;

/* MaiAtkObject */

enum {
  ACTIVATE,
  CREATE,
  DEACTIVATE,
  DESTROY,
  MAXIMIZE,
  MINIMIZE,
  RESIZE,
  RESTORE,
  LAST_SIGNAL
};

enum MaiInterfaceType {
    MAI_INTERFACE_COMPONENT, /* 0 */
    MAI_INTERFACE_ACTION,
    MAI_INTERFACE_VALUE,
    MAI_INTERFACE_EDITABLE_TEXT,
    MAI_INTERFACE_HYPERTEXT,
    MAI_INTERFACE_HYPERLINK_IMPL,
    MAI_INTERFACE_SELECTION,
    MAI_INTERFACE_TABLE,
    MAI_INTERFACE_TEXT,
    MAI_INTERFACE_DOCUMENT, 
    MAI_INTERFACE_IMAGE /* 10 */
};

static GType GetAtkTypeForMai(MaiInterfaceType type)
{
  switch (type) {
    case MAI_INTERFACE_COMPONENT:
      return ATK_TYPE_COMPONENT;
    case MAI_INTERFACE_ACTION:
      return ATK_TYPE_ACTION;
    case MAI_INTERFACE_VALUE:
      return ATK_TYPE_VALUE;
    case MAI_INTERFACE_EDITABLE_TEXT:
      return ATK_TYPE_EDITABLE_TEXT;
    case MAI_INTERFACE_HYPERTEXT:
      return ATK_TYPE_HYPERTEXT;
    case MAI_INTERFACE_HYPERLINK_IMPL:
       return g_atk_hyperlink_impl_type;
    case MAI_INTERFACE_SELECTION:
      return ATK_TYPE_SELECTION;
    case MAI_INTERFACE_TABLE:
      return ATK_TYPE_TABLE;
    case MAI_INTERFACE_TEXT:
      return ATK_TYPE_TEXT;
    case MAI_INTERFACE_DOCUMENT:
      return ATK_TYPE_DOCUMENT;
    case MAI_INTERFACE_IMAGE:
      return ATK_TYPE_IMAGE;
  }
  return G_TYPE_INVALID;
}

static const char* kNonUserInputEvent = ":system";
    
static const GInterfaceInfo atk_if_infos[] = {
    {(GInterfaceInitFunc)componentInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL}, 
    {(GInterfaceInitFunc)actionInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL},
    {(GInterfaceInitFunc)valueInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL},
    {(GInterfaceInitFunc)editableTextInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL},
    {(GInterfaceInitFunc)hypertextInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL},
    {(GInterfaceInitFunc)hyperlinkImplInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL},
    {(GInterfaceInitFunc)selectionInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL},
    {(GInterfaceInitFunc)tableInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL},
    {(GInterfaceInitFunc)textInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL},
    {(GInterfaceInitFunc)documentInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL},
    {(GInterfaceInitFunc)imageInterfaceInitCB,
     (GInterfaceFinalizeFunc) NULL, NULL}
};

/**
 * This MaiAtkObject is a thin wrapper, in the MAI namespace, for AtkObject
 */
struct MaiAtkObject
{
  AtkObject parent;
  /*
   * The AccessibleWrap whose properties and features are exported
   * via this object instance.
   */
  AccessibleWrap* accWrap;
};

struct MaiAtkObjectClass
{
    AtkObjectClass parent_class;
};

static guint mai_atk_object_signals [LAST_SIGNAL] = { 0, };

#ifdef MAI_LOGGING
PRInt32 sMaiAtkObjCreated = 0;
PRInt32 sMaiAtkObjDeleted = 0;
#endif

G_BEGIN_DECLS
/* callbacks for MaiAtkObject */
static void classInitCB(AtkObjectClass *aClass);
static void initializeCB(AtkObject *aAtkObj, gpointer aData);
static void finalizeCB(GObject *aObj);

/* callbacks for AtkObject virtual functions */
static const gchar*        getNameCB (AtkObject *aAtkObj);
/* getDescriptionCB is also used by image interface */
       const gchar*        getDescriptionCB (AtkObject *aAtkObj);
static AtkRole             getRoleCB(AtkObject *aAtkObj);
static AtkAttributeSet*    getAttributesCB(AtkObject *aAtkObj);
static AtkObject*          getParentCB(AtkObject *aAtkObj);
static gint                getChildCountCB(AtkObject *aAtkObj);
static AtkObject*          refChildCB(AtkObject *aAtkObj, gint aChildIndex);
static gint                getIndexInParentCB(AtkObject *aAtkObj);
static AtkStateSet*        refStateSetCB(AtkObject *aAtkObj);
static AtkRelationSet*     refRelationSetCB(AtkObject *aAtkObj);

/* the missing atkobject virtual functions */
/*
  static AtkLayer            getLayerCB(AtkObject *aAtkObj);
  static gint                getMdiZorderCB(AtkObject *aAtkObj);
  static void                SetNameCB(AtkObject *aAtkObj,
  const gchar *name);
  static void                SetDescriptionCB(AtkObject *aAtkObj,
  const gchar *description);
  static void                SetParentCB(AtkObject *aAtkObj,
  AtkObject *parent);
  static void                SetRoleCB(AtkObject *aAtkObj,
  AtkRole role);
  static guint               ConnectPropertyChangeHandlerCB(
  AtkObject  *aObj,
  AtkPropertyChangeHandler *handler);
  static void                RemovePropertyChangeHandlerCB(
  AtkObject *aAtkObj,
  guint handler_id);
  static void                InitializeCB(AtkObject *aAtkObj,
  gpointer data);
  static void                ChildrenChangedCB(AtkObject *aAtkObj,
  guint change_index,
  gpointer changed_child);
  static void                FocusEventCB(AtkObject *aAtkObj,
  gboolean focus_in);
  static void                PropertyChangeCB(AtkObject *aAtkObj,
  AtkPropertyValues *values);
  static void                StateChangeCB(AtkObject *aAtkObj,
  const gchar *name,
  gboolean state_set);
  static void                VisibleDataChangedCB(AtkObject *aAtkObj);
*/
G_END_DECLS

static GType GetMaiAtkType(PRUint16 interfacesBits);
static const char * GetUniqueMaiAtkTypeName(PRUint16 interfacesBits);

static gpointer parent_class = NULL;

static GQuark quark_mai_hyperlink = 0;

GType
mai_atk_object_get_type(void)
{
    static GType type = 0;

    if (!type) {
        static const GTypeInfo tinfo = {
            sizeof(MaiAtkObjectClass),
            (GBaseInitFunc)NULL,
            (GBaseFinalizeFunc)NULL,
            (GClassInitFunc)classInitCB,
            (GClassFinalizeFunc)NULL,
            NULL, /* class data */
            sizeof(MaiAtkObject), /* instance size */
            0, /* nb preallocs */
            (GInstanceInitFunc)NULL,
            NULL /* value table */
        };

        type = g_type_register_static(ATK_TYPE_OBJECT,
                                      "MaiAtkObject", &tinfo, GTypeFlags(0));
        quark_mai_hyperlink = g_quark_from_static_string("MaiHyperlink");
    }
    return type;
}

#ifdef MAI_LOGGING
PRInt32 AccessibleWrap::mAccWrapCreated = 0;
PRInt32 AccessibleWrap::mAccWrapDeleted = 0;
#endif

AccessibleWrap::
  AccessibleWrap(nsIContent* aContent, DocAccessible* aDoc) :
  Accessible(aContent, aDoc), mAtkObject(nsnull)
{
#ifdef MAI_LOGGING
  ++mAccWrapCreated;
#endif
  MAI_LOG_DEBUG(("==AccessibleWrap creating: this=%p,total=%d left=%d\n",
                 (void*)this, mAccWrapCreated,
                 (mAccWrapCreated-mAccWrapDeleted)));
}

AccessibleWrap::~AccessibleWrap()
{
    NS_ASSERTION(!mAtkObject, "ShutdownAtkObject() is not called");

#ifdef MAI_LOGGING
    ++mAccWrapDeleted;
#endif
    MAI_LOG_DEBUG(("==AccessibleWrap deleting: this=%p,total=%d left=%d\n",
                   (void*)this, mAccWrapDeleted,
                   (mAccWrapCreated-mAccWrapDeleted)));
}

void
AccessibleWrap::ShutdownAtkObject()
{
    if (mAtkObject) {
        if (IS_MAI_OBJECT(mAtkObject)) {
            MAI_ATK_OBJECT(mAtkObject)->accWrap = nsnull;
        }
        SetMaiHyperlink(nsnull);
        g_object_unref(mAtkObject);
        mAtkObject = nsnull;
    }
}

void
AccessibleWrap::Shutdown()
{
  ShutdownAtkObject();
  Accessible::Shutdown();
}

MaiHyperlink*
AccessibleWrap::GetMaiHyperlink(bool aCreate /* = true */)
{
    // make sure mAtkObject is created
    GetAtkObject();

    NS_ASSERTION(quark_mai_hyperlink, "quark_mai_hyperlink not initialized");
    NS_ASSERTION(IS_MAI_OBJECT(mAtkObject), "Invalid AtkObject");
    MaiHyperlink* maiHyperlink = nsnull;
    if (quark_mai_hyperlink && IS_MAI_OBJECT(mAtkObject)) {
        maiHyperlink = (MaiHyperlink*)g_object_get_qdata(G_OBJECT(mAtkObject),
                                                         quark_mai_hyperlink);
        if (!maiHyperlink && aCreate) {
            maiHyperlink = new MaiHyperlink(this);
            SetMaiHyperlink(maiHyperlink);
        }
    }
    return maiHyperlink;
}

void
AccessibleWrap::SetMaiHyperlink(MaiHyperlink* aMaiHyperlink)
{
    NS_ASSERTION(quark_mai_hyperlink, "quark_mai_hyperlink not initialized");
    NS_ASSERTION(IS_MAI_OBJECT(mAtkObject), "Invalid AtkObject");
    if (quark_mai_hyperlink && IS_MAI_OBJECT(mAtkObject)) {
        MaiHyperlink* maiHyperlink = GetMaiHyperlink(false);
        if (!maiHyperlink && !aMaiHyperlink) {
            return; // Never set and we're shutting down
        }
        delete maiHyperlink;
        g_object_set_qdata(G_OBJECT(mAtkObject), quark_mai_hyperlink,
                           aMaiHyperlink);
    }
}

NS_IMETHODIMP
AccessibleWrap::GetNativeInterface(void** aOutAccessible)
{
    *aOutAccessible = nsnull;

    if (!mAtkObject) {
        if (IsDefunct() || !nsAccUtils::IsEmbeddedObject(this)) {
            // We don't create ATK objects for node which has been shutdown, or
            // nsIAccessible plain text leaves
            return NS_ERROR_FAILURE;
        }

        GType type = GetMaiAtkType(CreateMaiInterfaces());
        NS_ENSURE_TRUE(type, NS_ERROR_FAILURE);
        mAtkObject =
            reinterpret_cast<AtkObject *>
                            (g_object_new(type, NULL));
        NS_ENSURE_TRUE(mAtkObject, NS_ERROR_OUT_OF_MEMORY);

        atk_object_initialize(mAtkObject, this);
        mAtkObject->role = ATK_ROLE_INVALID;
        mAtkObject->layer = ATK_LAYER_INVALID;
    }

    *aOutAccessible = mAtkObject;
    return NS_OK;
}

AtkObject *
AccessibleWrap::GetAtkObject(void)
{
    void *atkObj = nsnull;
    GetNativeInterface(&atkObj);
    return static_cast<AtkObject *>(atkObj);
}

// Get AtkObject from nsIAccessible interface
/* static */
AtkObject *
AccessibleWrap::GetAtkObject(nsIAccessible* acc)
{
    void *atkObjPtr = nsnull;
    acc->GetNativeInterface(&atkObjPtr);
    return atkObjPtr ? ATK_OBJECT(atkObjPtr) : nsnull;    
}

/* private */
PRUint16
AccessibleWrap::CreateMaiInterfaces(void)
{
  PRUint16 interfacesBits = 0;
    
  // The Component interface is supported by all accessibles.
  interfacesBits |= 1 << MAI_INTERFACE_COMPONENT;

  // Add Action interface if the action count is more than zero.
  if (ActionCount() > 0)
    interfacesBits |= 1 << MAI_INTERFACE_ACTION;

  // Text, Editabletext, and Hypertext interface.
  HyperTextAccessible* hyperText = AsHyperText();
  if (hyperText && hyperText->IsTextRole()) {
    interfacesBits |= 1 << MAI_INTERFACE_TEXT;
    interfacesBits |= 1 << MAI_INTERFACE_EDITABLE_TEXT;
    if (!nsAccUtils::MustPrune(this))
      interfacesBits |= 1 << MAI_INTERFACE_HYPERTEXT;
  }

  // Value interface.
  nsCOMPtr<nsIAccessibleValue> accessInterfaceValue;
  QueryInterface(NS_GET_IID(nsIAccessibleValue),
                 getter_AddRefs(accessInterfaceValue));
  if (accessInterfaceValue) {
    interfacesBits |= 1 << MAI_INTERFACE_VALUE; 
  }

  // Document interface.
  if (IsDoc())
    interfacesBits |= 1 << MAI_INTERFACE_DOCUMENT;

  if (IsImage())
    interfacesBits |= 1 << MAI_INTERFACE_IMAGE;

  // HyperLink interface.
  if (IsLink())
    interfacesBits |= 1 << MAI_INTERFACE_HYPERLINK_IMPL;

  if (!nsAccUtils::MustPrune(this)) {  // These interfaces require children
    // Table interface.
    nsCOMPtr<nsIAccessibleTable> accessInterfaceTable;
    QueryInterface(NS_GET_IID(nsIAccessibleTable),
                   getter_AddRefs(accessInterfaceTable));
    if (accessInterfaceTable) {
      interfacesBits |= 1 << MAI_INTERFACE_TABLE;
    }
      
    // Selection interface.
    if (IsSelect()) {
      interfacesBits |= 1 << MAI_INTERFACE_SELECTION;
    }
  }

  return interfacesBits;
}

static GType
GetMaiAtkType(PRUint16 interfacesBits)
{
    GType type;
    static const GTypeInfo tinfo = {
        sizeof(MaiAtkObjectClass),
        (GBaseInitFunc) NULL,
        (GBaseFinalizeFunc) NULL,
        (GClassInitFunc) NULL,
        (GClassFinalizeFunc) NULL,
        NULL, /* class data */
        sizeof(MaiAtkObject), /* instance size */
        0, /* nb preallocs */
        (GInstanceInitFunc) NULL,
        NULL /* value table */
    };

    /*
     * The members we use to register GTypes are GetAtkTypeForMai
     * and atk_if_infos, which are constant values to each MaiInterface
     * So we can reuse the registered GType when having
     * the same MaiInterface types.
     */
    const char *atkTypeName = GetUniqueMaiAtkTypeName(interfacesBits);
    type = g_type_from_name(atkTypeName);
    if (type) {
        return type;
    }

    /*
     * gobject limits the number of types that can directly derive from any
     * given object type to 4095.
     */
    static PRUint16 typeRegCount = 0;
    if (typeRegCount++ >= 4095) {
        return G_TYPE_INVALID;
    }
    type = g_type_register_static(MAI_TYPE_ATK_OBJECT,
                                  atkTypeName,
                                  &tinfo, GTypeFlags(0));

    for (PRUint32 index = 0; index < ArrayLength(atk_if_infos); index++) {
      if (interfacesBits & (1 << index)) {
        g_type_add_interface_static(type,
                                    GetAtkTypeForMai((MaiInterfaceType)index),
                                    &atk_if_infos[index]);
      }
    }

    return type;
}

static const char*
GetUniqueMaiAtkTypeName(PRUint16 interfacesBits)
{
#define MAI_ATK_TYPE_NAME_LEN (30)     /* 10+sizeof(PRUint16)*8/4+1 < 30 */

    static gchar namePrefix[] = "MaiAtkType";   /* size = 10 */
    static gchar name[MAI_ATK_TYPE_NAME_LEN + 1];

    PR_snprintf(name, MAI_ATK_TYPE_NAME_LEN, "%s%x", namePrefix,
                interfacesBits);
    name[MAI_ATK_TYPE_NAME_LEN] = '\0';

    MAI_LOG_DEBUG(("MaiWidget::LastedTypeName=%s\n", name));

    return name;
}

bool
AccessibleWrap::IsValidObject()
{
    // to ensure we are not shut down
    return !IsDefunct();
}

/* static functions for ATK callbacks */
void
classInitCB(AtkObjectClass *aClass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(aClass);

    parent_class = g_type_class_peek_parent(aClass);

    aClass->get_name = getNameCB;
    aClass->get_description = getDescriptionCB;
    aClass->get_parent = getParentCB;
    aClass->get_n_children = getChildCountCB;
    aClass->ref_child = refChildCB;
    aClass->get_index_in_parent = getIndexInParentCB;
    aClass->get_role = getRoleCB;
    aClass->get_attributes = getAttributesCB;
    aClass->ref_state_set = refStateSetCB;
    aClass->ref_relation_set = refRelationSetCB;

    aClass->initialize = initializeCB;

    gobject_class->finalize = finalizeCB;

    mai_atk_object_signals [ACTIVATE] =
    g_signal_new ("activate",
                  MAI_TYPE_ATK_OBJECT,
                  G_SIGNAL_RUN_LAST,
                  0, /* default signal handler */
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
    mai_atk_object_signals [CREATE] =
    g_signal_new ("create",
                  MAI_TYPE_ATK_OBJECT,
                  G_SIGNAL_RUN_LAST,
                  0, /* default signal handler */
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
    mai_atk_object_signals [DEACTIVATE] =
    g_signal_new ("deactivate",
                  MAI_TYPE_ATK_OBJECT,
                  G_SIGNAL_RUN_LAST,
                  0, /* default signal handler */
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
    mai_atk_object_signals [DESTROY] =
    g_signal_new ("destroy",
                  MAI_TYPE_ATK_OBJECT,
                  G_SIGNAL_RUN_LAST,
                  0, /* default signal handler */
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
    mai_atk_object_signals [MAXIMIZE] =
    g_signal_new ("maximize",
                  MAI_TYPE_ATK_OBJECT,
                  G_SIGNAL_RUN_LAST,
                  0, /* default signal handler */
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
    mai_atk_object_signals [MINIMIZE] =
    g_signal_new ("minimize",
                  MAI_TYPE_ATK_OBJECT,
                  G_SIGNAL_RUN_LAST,
                  0, /* default signal handler */
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
    mai_atk_object_signals [RESIZE] =
    g_signal_new ("resize",
                  MAI_TYPE_ATK_OBJECT,
                  G_SIGNAL_RUN_LAST,
                  0, /* default signal handler */
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
    mai_atk_object_signals [RESTORE] =
    g_signal_new ("restore",
                  MAI_TYPE_ATK_OBJECT,
                  G_SIGNAL_RUN_LAST,
                  0, /* default signal handler */
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

}

void
initializeCB(AtkObject *aAtkObj, gpointer aData)
{
    NS_ASSERTION((IS_MAI_OBJECT(aAtkObj)), "Invalid AtkObject");
    NS_ASSERTION(aData, "Invalid Data to init AtkObject");
    if (!aAtkObj || !aData)
        return;

    /* call parent init function */
    /* AtkObjectClass has not a "initialize" function now,
     * maybe it has later
     */

    if (ATK_OBJECT_CLASS(parent_class)->initialize)
        ATK_OBJECT_CLASS(parent_class)->initialize(aAtkObj, aData);

  /* initialize object */
  MAI_ATK_OBJECT(aAtkObj)->accWrap =
    static_cast<AccessibleWrap*>(aData);

#ifdef MAI_LOGGING
    ++sMaiAtkObjCreated;
#endif
    MAI_LOG_DEBUG(("MaiAtkObj Create obj=%p for AccWrap=%p, all=%d, left=%d\n",
                   (void*)aAtkObj, (void*)aData, sMaiAtkObjCreated,
                   (sMaiAtkObjCreated-sMaiAtkObjDeleted)));
}

void
finalizeCB(GObject *aObj)
{
    if (!IS_MAI_OBJECT(aObj))
        return;
    NS_ASSERTION(MAI_ATK_OBJECT(aObj)->accWrap == nsnull, "AccWrap NOT null");

#ifdef MAI_LOGGING
    ++sMaiAtkObjDeleted;
#endif
    MAI_LOG_DEBUG(("MaiAtkObj Delete obj=%p, all=%d, left=%d\n",
                   (void*)aObj, sMaiAtkObjCreated,
                   (sMaiAtkObjCreated-sMaiAtkObjDeleted)));

    // call parent finalize function
    // finalize of GObjectClass will unref the accessible parent if has
    if (G_OBJECT_CLASS (parent_class)->finalize)
        G_OBJECT_CLASS (parent_class)->finalize(aObj);
}

const gchar*
getNameCB(AtkObject* aAtkObj)
{
  AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj);
  if (!accWrap)
    return nsnull;

  nsAutoString uniName;
  accWrap->Name(uniName);

  NS_ConvertUTF8toUTF16 objName(aAtkObj->name);
  if (!uniName.Equals(objName))
    atk_object_set_name(aAtkObj, NS_ConvertUTF16toUTF8(uniName).get());

  return aAtkObj->name;
}

const gchar *
getDescriptionCB(AtkObject *aAtkObj)
{
    AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj);
    if (!accWrap || accWrap->IsDefunct())
        return nsnull;

    /* nsIAccessible is responsible for the non-NULL description */
    nsAutoString uniDesc;
    accWrap->Description(uniDesc);

    NS_ConvertUTF8toUTF16 objDesc(aAtkObj->description);
    if (!uniDesc.Equals(objDesc))
        atk_object_set_description(aAtkObj,
                                   NS_ConvertUTF16toUTF8(uniDesc).get());

    return aAtkObj->description;
}

AtkRole
getRoleCB(AtkObject *aAtkObj)
{
  AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj);
  if (!accWrap)
    return ATK_ROLE_INVALID;

#ifdef DEBUG
  NS_ASSERTION(nsAccUtils::IsTextInterfaceSupportCorrect(accWrap),
      "Does not support nsIAccessibleText when it should");
#endif

  if (aAtkObj->role != ATK_ROLE_INVALID)
    return aAtkObj->role;

#define ROLE(geckoRole, stringRole, atkRole, macRole, msaaRole, ia2Role) \
  case roles::geckoRole: \
    aAtkObj->role = atkRole; \
    break;

  switch (accWrap->Role()) {
#include "RoleMap.h"
    default:
      MOZ_NOT_REACHED("Unknown role.");
      aAtkObj->role = ATK_ROLE_UNKNOWN;
  };

#undef ROLE

  return aAtkObj->role;
}

AtkAttributeSet*
ConvertToAtkAttributeSet(nsIPersistentProperties* aAttributes)
{
    if (!aAttributes)
        return nsnull;

    AtkAttributeSet *objAttributeSet = nsnull;
    nsCOMPtr<nsISimpleEnumerator> propEnum;
    nsresult rv = aAttributes->Enumerate(getter_AddRefs(propEnum));
    NS_ENSURE_SUCCESS(rv, nsnull);

    bool hasMore;
    while (NS_SUCCEEDED(propEnum->HasMoreElements(&hasMore)) && hasMore) {
        nsCOMPtr<nsISupports> sup;
        rv = propEnum->GetNext(getter_AddRefs(sup));
        NS_ENSURE_SUCCESS(rv, objAttributeSet);

        nsCOMPtr<nsIPropertyElement> propElem(do_QueryInterface(sup));
        NS_ENSURE_TRUE(propElem, objAttributeSet);

        nsCAutoString name;
        rv = propElem->GetKey(name);
        NS_ENSURE_SUCCESS(rv, objAttributeSet);

        nsAutoString value;
        rv = propElem->GetValue(value);
        NS_ENSURE_SUCCESS(rv, objAttributeSet);

        AtkAttribute *objAttr = (AtkAttribute *)g_malloc(sizeof(AtkAttribute));
        objAttr->name = g_strdup(name.get());
        objAttr->value = g_strdup(NS_ConvertUTF16toUTF8(value).get());
        objAttributeSet = g_slist_prepend(objAttributeSet, objAttr);
    }

    //libspi will free it
    return objAttributeSet;
}

AtkAttributeSet*
GetAttributeSet(Accessible* aAccessible)
{
    nsCOMPtr<nsIPersistentProperties> attributes;
    aAccessible->GetAttributes(getter_AddRefs(attributes));

    if (attributes) {
        // Deal with attributes that we only need to expose in ATK
        if (aAccessible->State() & states::HASPOPUP) {
          // There is no ATK state for haspopup, must use object attribute to expose the same info
          nsAutoString oldValueUnused;
          attributes->SetStringProperty(NS_LITERAL_CSTRING("haspopup"), NS_LITERAL_STRING("true"),
                                        oldValueUnused);
        }

        return ConvertToAtkAttributeSet(attributes);
    }

    return nsnull;
}

AtkAttributeSet *
getAttributesCB(AtkObject *aAtkObj)
{
  AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj);
  return accWrap ? GetAttributeSet(accWrap) : nsnull;
}

AtkObject *
getParentCB(AtkObject *aAtkObj)
{
  if (!aAtkObj->accessible_parent) {
    AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj);
    if (!accWrap)
      return nsnull;

    Accessible* accParent = accWrap->Parent();
    if (!accParent)
      return nsnull;

    AtkObject* parent = AccessibleWrap::GetAtkObject(accParent);
    if (parent)
      atk_object_set_parent(aAtkObj, parent);
  }
  return aAtkObj->accessible_parent;
}

gint
getChildCountCB(AtkObject *aAtkObj)
{
    AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj);
    if (!accWrap || nsAccUtils::MustPrune(accWrap)) {
        return 0;
    }

    return static_cast<gint>(accWrap->EmbeddedChildCount());
}

AtkObject *
refChildCB(AtkObject *aAtkObj, gint aChildIndex)
{
    // aChildIndex should not be less than zero
    if (aChildIndex < 0) {
      return nsnull;
    }

    AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj);
    if (!accWrap || nsAccUtils::MustPrune(accWrap)) {
        return nsnull;
    }

    Accessible* accChild = accWrap->GetEmbeddedChildAt(aChildIndex);
    if (!accChild)
        return nsnull;

    AtkObject* childAtkObj = AccessibleWrap::GetAtkObject(accChild);

    NS_ASSERTION(childAtkObj, "Fail to get AtkObj");
    if (!childAtkObj)
        return nsnull;
    g_object_ref(childAtkObj);

  if (aAtkObj != childAtkObj->accessible_parent)
    atk_object_set_parent(childAtkObj, aAtkObj);

  return childAtkObj;
}

gint
getIndexInParentCB(AtkObject *aAtkObj)
{
    // We don't use nsIAccessible::GetIndexInParent() because
    // for ATK we don't want to include text leaf nodes as children
    AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj);
    if (!accWrap) {
        return -1;
    }

    Accessible* parent = accWrap->Parent();
    if (!parent)
        return -1; // No parent

    return parent->GetIndexOfEmbeddedChild(accWrap);
}

static void
TranslateStates(PRUint64 aState, AtkStateSet* aStateSet)
{

  // Convert every state to an entry in AtkStateMap
  PRUint32 stateIndex = 0;
  PRUint64 bitMask = 1;
  while (gAtkStateMap[stateIndex].stateMapEntryType != kNoSuchState) {
    if (gAtkStateMap[stateIndex].atkState) { // There's potentially an ATK state for this
      bool isStateOn = (aState & bitMask) != 0;
      if (gAtkStateMap[stateIndex].stateMapEntryType == kMapOpposite) {
        isStateOn = !isStateOn;
      }
      if (isStateOn) {
        atk_state_set_add_state(aStateSet, gAtkStateMap[stateIndex].atkState);
      }
    }
    bitMask <<= 1;
    ++ stateIndex;
  }
}

AtkStateSet *
refStateSetCB(AtkObject *aAtkObj)
{
    AtkStateSet *state_set = nsnull;
    state_set = ATK_OBJECT_CLASS(parent_class)->ref_state_set(aAtkObj);

    AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj);
    if (!accWrap) {
        TranslateStates(states::DEFUNCT, state_set);
        return state_set;
    }

    // Map states
    TranslateStates(accWrap->State(), state_set);

    return state_set;
}

AtkRelationSet *
refRelationSetCB(AtkObject *aAtkObj)
{
  AtkRelationSet* relation_set =
    ATK_OBJECT_CLASS(parent_class)->ref_relation_set(aAtkObj);

  AccessibleWrap* accWrap = GetAccessibleWrap(aAtkObj);
  if (!accWrap)
    return relation_set;

  PRUint32 relationTypes[] = {
    nsIAccessibleRelation::RELATION_LABELLED_BY,
    nsIAccessibleRelation::RELATION_LABEL_FOR,
    nsIAccessibleRelation::RELATION_NODE_CHILD_OF,
    nsIAccessibleRelation::RELATION_CONTROLLED_BY,
    nsIAccessibleRelation::RELATION_CONTROLLER_FOR,
    nsIAccessibleRelation::RELATION_EMBEDS,
    nsIAccessibleRelation::RELATION_FLOWS_TO,
    nsIAccessibleRelation::RELATION_FLOWS_FROM,
    nsIAccessibleRelation::RELATION_DESCRIBED_BY,
    nsIAccessibleRelation::RELATION_DESCRIPTION_FOR,
  };

  for (PRUint32 i = 0; i < ArrayLength(relationTypes); i++) {
    AtkRelationType atkType = static_cast<AtkRelationType>(relationTypes[i]);
    AtkRelation* atkRelation =
      atk_relation_set_get_relation_by_type(relation_set, atkType);
    if (atkRelation)
      atk_relation_set_remove(relation_set, atkRelation);

    Relation rel(accWrap->RelationByType(relationTypes[i]));
    nsTArray<AtkObject*> targets;
    Accessible* tempAcc = nsnull;
    while ((tempAcc = rel.Next()))
      targets.AppendElement(AccessibleWrap::GetAtkObject(tempAcc));

    if (targets.Length()) {
      atkRelation = atk_relation_new(targets.Elements(), targets.Length(), atkType);
      atk_relation_set_add(relation_set, atkRelation);
      g_object_unref(atkRelation);
    }
  }

  return relation_set;
}

// Check if aAtkObj is a valid MaiAtkObject, and return the AccessibleWrap
// for it.
AccessibleWrap*
GetAccessibleWrap(AtkObject* aAtkObj)
{
  NS_ENSURE_TRUE(IS_MAI_OBJECT(aAtkObj), nsnull);
  AccessibleWrap* accWrap = MAI_ATK_OBJECT(aAtkObj)->accWrap;

  // Check if the accessible was deconstructed.
  if (!accWrap)
    return nsnull;

  NS_ENSURE_TRUE(accWrap->GetAtkObject() == aAtkObj, nsnull);

  AccessibleWrap* appAccWrap = nsAccessNode::GetApplicationAccessible();
  if (appAccWrap != accWrap && !accWrap->IsValidObject())
    return nsnull;

  return accWrap;
}

nsresult
AccessibleWrap::HandleAccEvent(AccEvent* aEvent)
{
  nsresult rv = Accessible::HandleAccEvent(aEvent);
  NS_ENSURE_SUCCESS(rv, rv);

  return FirePlatformEvent(aEvent);
}

nsresult
AccessibleWrap::FirePlatformEvent(AccEvent* aEvent)
{
    Accessible* accessible = aEvent->GetAccessible();
    NS_ENSURE_TRUE(accessible, NS_ERROR_FAILURE);

    PRUint32 type = aEvent->GetEventType();

    AtkObject* atkObj = AccessibleWrap::GetAtkObject(accessible);

    // We don't create ATK objects for nsIAccessible plain text leaves,
    // just return NS_OK in such case
    if (!atkObj) {
        NS_ASSERTION(type == nsIAccessibleEvent::EVENT_SHOW ||
                     type == nsIAccessibleEvent::EVENT_HIDE,
                     "Event other than SHOW and HIDE fired for plain text leaves");
        return NS_OK;
    }

    AccessibleWrap* accWrap = GetAccessibleWrap(atkObj);
    if (!accWrap) {
        return NS_OK; // Node is shut down
    }

    switch (type) {
    case nsIAccessibleEvent::EVENT_STATE_CHANGE:
        return FireAtkStateChangeEvent(aEvent, atkObj);

    case nsIAccessibleEvent::EVENT_TEXT_REMOVED:
    case nsIAccessibleEvent::EVENT_TEXT_INSERTED:
        return FireAtkTextChangedEvent(aEvent, atkObj);

    case nsIAccessibleEvent::EVENT_FOCUS:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_FOCUS\n"));
        a11y::RootAccessible* rootAccWrap = accWrap->RootAccessible();
        if (rootAccWrap && rootAccWrap->mActivated) {
            atk_focus_tracker_notify(atkObj);
            // Fire state change event for focus
            nsRefPtr<AccEvent> stateChangeEvent =
              new AccStateChangeEvent(accessible, states::FOCUSED, true);
            return FireAtkStateChangeEvent(stateChangeEvent, atkObj);
        }
      } break;

    case nsIAccessibleEvent::EVENT_NAME_CHANGE:
      {
        nsAutoString newName;
        accessible->Name(newName);
        NS_ConvertUTF16toUTF8 utf8Name(newName);
        if (!atkObj->name || !utf8Name.Equals(atkObj->name))
          atk_object_set_name(atkObj, utf8Name.get());

        break;
      }
    case nsIAccessibleEvent::EVENT_VALUE_CHANGE:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_VALUE_CHANGE\n"));
        nsCOMPtr<nsIAccessibleValue> value(do_QueryObject(accessible));
        if (value) {    // Make sure this is a numeric value
            // Don't fire for MSAA string value changes (e.g. text editing)
            // ATK values are always numeric
            g_object_notify( (GObject*)atkObj, "accessible-value" );
        }
      } break;

    case nsIAccessibleEvent::EVENT_SELECTION:
    case nsIAccessibleEvent::EVENT_SELECTION_ADD:
    case nsIAccessibleEvent::EVENT_SELECTION_REMOVE:
    {
      // XXX: dupe events may be fired
      MAI_LOG_DEBUG(("\n\nReceived: EVENT_SELECTION_CHANGED\n"));
      AccSelChangeEvent* selChangeEvent = downcast_accEvent(aEvent);
      g_signal_emit_by_name(AccessibleWrap::GetAtkObject(selChangeEvent->Widget()),
                            "selection_changed");
      break;
    }

    case nsIAccessibleEvent::EVENT_SELECTION_WITHIN:
    {
      MAI_LOG_DEBUG(("\n\nReceived: EVENT_SELECTION_CHANGED\n"));
      g_signal_emit_by_name(atkObj, "selection_changed");
      break;
    }

    case nsIAccessibleEvent::EVENT_TEXT_SELECTION_CHANGED:
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_TEXT_SELECTION_CHANGED\n"));
        g_signal_emit_by_name(atkObj, "text_selection_changed");
        break;

    case nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_TEXT_CARET_MOVED\n"));

        AccCaretMoveEvent* caretMoveEvent = downcast_accEvent(aEvent);
        NS_ASSERTION(caretMoveEvent, "Event needs event data");
        if (!caretMoveEvent)
            break;

        PRInt32 caretOffset = caretMoveEvent->GetCaretOffset();

        MAI_LOG_DEBUG(("\n\nCaret postion: %d", caretOffset));
        g_signal_emit_by_name(atkObj,
                              "text_caret_moved",
                              // Curent caret position
                              caretOffset);
      } break;

    case nsIAccessibleEvent::EVENT_TEXT_ATTRIBUTE_CHANGED:
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_TEXT_ATTRIBUTE_CHANGED\n"));

        g_signal_emit_by_name(atkObj,
                              "text-attributes-changed");
        break;

    case nsIAccessibleEvent::EVENT_TABLE_MODEL_CHANGED:
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_TABLE_MODEL_CHANGED\n"));
        g_signal_emit_by_name(atkObj, "model_changed");
        break;

    case nsIAccessibleEvent::EVENT_TABLE_ROW_INSERT:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_TABLE_ROW_INSERT\n"));
        AccTableChangeEvent* tableEvent = downcast_accEvent(aEvent);
        NS_ENSURE_TRUE(tableEvent, NS_ERROR_FAILURE);

        PRInt32 rowIndex = tableEvent->GetIndex();
        PRInt32 numRows = tableEvent->GetCount();

        g_signal_emit_by_name(atkObj,
                              "row_inserted",
                              // After which the rows are inserted
                              rowIndex,
                              // The number of the inserted
                              numRows);
     } break;

   case nsIAccessibleEvent::EVENT_TABLE_ROW_DELETE:
     {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_TABLE_ROW_DELETE\n"));
        AccTableChangeEvent* tableEvent = downcast_accEvent(aEvent);
        NS_ENSURE_TRUE(tableEvent, NS_ERROR_FAILURE);

        PRInt32 rowIndex = tableEvent->GetIndex();
        PRInt32 numRows = tableEvent->GetCount();

        g_signal_emit_by_name(atkObj,
                              "row_deleted",
                              // After which the rows are deleted
                              rowIndex,
                              // The number of the deleted
                              numRows);
      } break;

    case nsIAccessibleEvent::EVENT_TABLE_ROW_REORDER:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_TABLE_ROW_REORDER\n"));
        g_signal_emit_by_name(atkObj, "row_reordered");
        break;
      }

    case nsIAccessibleEvent::EVENT_TABLE_COLUMN_INSERT:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_TABLE_COLUMN_INSERT\n"));
        AccTableChangeEvent* tableEvent = downcast_accEvent(aEvent);
        NS_ENSURE_TRUE(tableEvent, NS_ERROR_FAILURE);

        PRInt32 colIndex = tableEvent->GetIndex();
        PRInt32 numCols = tableEvent->GetCount();

        g_signal_emit_by_name(atkObj,
                              "column_inserted",
                              // After which the columns are inserted
                              colIndex,
                              // The number of the inserted
                              numCols);
      } break;

    case nsIAccessibleEvent::EVENT_TABLE_COLUMN_DELETE:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_TABLE_COLUMN_DELETE\n"));
        AccTableChangeEvent* tableEvent = downcast_accEvent(aEvent);
        NS_ENSURE_TRUE(tableEvent, NS_ERROR_FAILURE);

        PRInt32 colIndex = tableEvent->GetIndex();
        PRInt32 numCols = tableEvent->GetCount();

        g_signal_emit_by_name(atkObj,
                              "column_deleted",
                              // After which the columns are deleted
                              colIndex,
                              // The number of the deleted
                              numCols);
      } break;

    case nsIAccessibleEvent::EVENT_TABLE_COLUMN_REORDER:
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_TABLE_COLUMN_REORDER\n"));
        g_signal_emit_by_name(atkObj, "column_reordered");
        break;

    case nsIAccessibleEvent::EVENT_SECTION_CHANGED:
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_SECTION_CHANGED\n"));
        g_signal_emit_by_name(atkObj, "visible_data_changed");
        break;

    case nsIAccessibleEvent::EVENT_SHOW:
        return FireAtkShowHideEvent(aEvent, atkObj, true);

    case nsIAccessibleEvent::EVENT_HIDE:
        return FireAtkShowHideEvent(aEvent, atkObj, false);

        /*
         * Because dealing with menu is very different between nsIAccessible
         * and ATK, and the menu activity is important, specially transfer the
         * following two event.
         * Need more verification by AT test.
         */
    case nsIAccessibleEvent::EVENT_MENU_START:
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_MENU_START\n"));
        break;

    case nsIAccessibleEvent::EVENT_MENU_END:
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_MENU_END\n"));
        break;

    case nsIAccessibleEvent::EVENT_WINDOW_ACTIVATE:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_WINDOW_ACTIVATED\n"));
        accessible->AsRoot()->mActivated = true;
        guint id = g_signal_lookup ("activate", MAI_TYPE_ATK_OBJECT);
        g_signal_emit(atkObj, id, 0);

        // Always fire a current focus event after activation.
        FocusMgr()->ForceFocusEvent();
      } break;

    case nsIAccessibleEvent::EVENT_WINDOW_DEACTIVATE:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_WINDOW_DEACTIVATED\n"));
        accessible->AsRoot()->mActivated = false;
        guint id = g_signal_lookup ("deactivate", MAI_TYPE_ATK_OBJECT);
        g_signal_emit(atkObj, id, 0);
      } break;

    case nsIAccessibleEvent::EVENT_WINDOW_MAXIMIZE:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_WINDOW_MAXIMIZE\n"));
        guint id = g_signal_lookup ("maximize", MAI_TYPE_ATK_OBJECT);
        g_signal_emit(atkObj, id, 0);
      } break;

    case nsIAccessibleEvent::EVENT_WINDOW_MINIMIZE:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_WINDOW_MINIMIZE\n"));
        guint id = g_signal_lookup ("minimize", MAI_TYPE_ATK_OBJECT);
        g_signal_emit(atkObj, id, 0);
      } break;

    case nsIAccessibleEvent::EVENT_WINDOW_RESTORE:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_WINDOW_RESTORE\n"));
        guint id = g_signal_lookup ("restore", MAI_TYPE_ATK_OBJECT);
        g_signal_emit(atkObj, id, 0);
      } break;

    case nsIAccessibleEvent::EVENT_DOCUMENT_LOAD_COMPLETE:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_DOCUMENT_LOAD_COMPLETE\n"));
        g_signal_emit_by_name (atkObj, "load_complete");
      } break;

    case nsIAccessibleEvent::EVENT_DOCUMENT_RELOAD:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_DOCUMENT_RELOAD\n"));
        g_signal_emit_by_name (atkObj, "reload");
      } break;

    case nsIAccessibleEvent::EVENT_DOCUMENT_LOAD_STOPPED:
      {
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_DOCUMENT_LOAD_STOPPED\n"));
        g_signal_emit_by_name (atkObj, "load_stopped");
      } break;

    case nsIAccessibleEvent::EVENT_MENUPOPUP_START:
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_MENUPOPUP_START\n"));
        atk_focus_tracker_notify(atkObj); // fire extra focus event
        atk_object_notify_state_change(atkObj, ATK_STATE_VISIBLE, true);
        atk_object_notify_state_change(atkObj, ATK_STATE_SHOWING, true);
        break;

    case nsIAccessibleEvent::EVENT_MENUPOPUP_END:
        MAI_LOG_DEBUG(("\n\nReceived: EVENT_MENUPOPUP_END\n"));
        atk_object_notify_state_change(atkObj, ATK_STATE_VISIBLE, false);
        atk_object_notify_state_change(atkObj, ATK_STATE_SHOWING, false);
        break;
    }

    return NS_OK;
}

nsresult
AccessibleWrap::FireAtkStateChangeEvent(AccEvent* aEvent,
                                        AtkObject* aObject)
{
    MAI_LOG_DEBUG(("\n\nReceived: EVENT_STATE_CHANGE\n"));

    AccStateChangeEvent* event = downcast_accEvent(aEvent);
    NS_ENSURE_TRUE(event, NS_ERROR_FAILURE);

    bool isEnabled = event->IsStateEnabled();
    PRInt32 stateIndex = AtkStateMap::GetStateIndexFor(event->GetState());
    if (stateIndex >= 0) {
        NS_ASSERTION(gAtkStateMap[stateIndex].stateMapEntryType != kNoSuchState,
                     "No such state");

        if (gAtkStateMap[stateIndex].atkState != kNone) {
            NS_ASSERTION(gAtkStateMap[stateIndex].stateMapEntryType != kNoStateChange,
                         "State changes should not fired for this state");

            if (gAtkStateMap[stateIndex].stateMapEntryType == kMapOpposite)
                isEnabled = !isEnabled;

            // Fire state change for first state if there is one to map
            atk_object_notify_state_change(aObject,
                                           gAtkStateMap[stateIndex].atkState,
                                           isEnabled);
        }
    }

    return NS_OK;
}

nsresult
AccessibleWrap::FireAtkTextChangedEvent(AccEvent* aEvent,
                                        AtkObject* aObject)
{
    MAI_LOG_DEBUG(("\n\nReceived: EVENT_TEXT_REMOVED/INSERTED\n"));

    AccTextChangeEvent* event = downcast_accEvent(aEvent);
    NS_ENSURE_TRUE(event, NS_ERROR_FAILURE);

    PRInt32 start = event->GetStartOffset();
    PRUint32 length = event->GetLength();
    bool isInserted = event->IsTextInserted();
    bool isFromUserInput = aEvent->IsFromUserInput();
    char* signal_name = nsnull;

  if (gAvailableAtkSignals == eUnknown)
    gAvailableAtkSignals =
      g_signal_lookup("text-insert", G_OBJECT_TYPE(aObject)) ?
        eHaveNewAtkTextSignals : eNoNewAtkSignals;

  if (gAvailableAtkSignals == eNoNewAtkSignals) {
    // XXX remove this code and the gHaveNewTextSignals check when we can
    // stop supporting old atk since it doesn't really work anyway
    // see bug 619002
    signal_name = g_strconcat(isInserted ? "text_changed::insert" :
                              "text_changed::delete",
                              isFromUserInput ? "" : kNonUserInputEvent, NULL);
    g_signal_emit_by_name(aObject, signal_name, start, length);
  } else {
    nsAutoString text;
    event->GetModifiedText(text);
    signal_name = g_strconcat(isInserted ? "text-insert" : "text-remove",
                              isFromUserInput ? "" : "::system", NULL);
    g_signal_emit_by_name(aObject, signal_name, start, length,
                          NS_ConvertUTF16toUTF8(text).get());
  }

  g_free(signal_name);
  return NS_OK;
}

nsresult
AccessibleWrap::FireAtkShowHideEvent(AccEvent* aEvent,
                                     AtkObject* aObject, bool aIsAdded)
{
    if (aIsAdded) {
        MAI_LOG_DEBUG(("\n\nReceived: Show event\n"));
    } else {
        MAI_LOG_DEBUG(("\n\nReceived: Hide event\n"));
    }

    PRInt32 indexInParent = getIndexInParentCB(aObject);
    AtkObject *parentObject = getParentCB(aObject);
    NS_ENSURE_STATE(parentObject);

    bool isFromUserInput = aEvent->IsFromUserInput();
    char *signal_name = g_strconcat(aIsAdded ? "children_changed::add" :  "children_changed::remove",
                                    isFromUserInput ? "" : kNonUserInputEvent, NULL);
    g_signal_emit_by_name(parentObject, signal_name, indexInParent, aObject, NULL);
    g_free(signal_name);

    return NS_OK;
}
