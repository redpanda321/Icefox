/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsXBLBinding_h_
#define nsXBLBinding_h_

#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsINodeList.h"
#include "nsIStyleRuleProcessor.h"
#include "nsClassHashtable.h"
#include "nsTArray.h"
#include "nsCycleCollectionParticipant.h"
#include "nsISupportsImpl.h"

class nsXBLPrototypeBinding;
class nsIContent;
class nsIAtom;
class nsIDocument;
class nsIScriptContext;
class nsObjectHashtable;
class nsXBLInsertionPoint;
typedef nsTArray<nsRefPtr<nsXBLInsertionPoint> > nsInsertionPointList;
struct JSContext;
struct JSObject;

// *********************************************************************/
// The XBLBinding class

class nsXBLBinding
{
public:
  nsXBLBinding(nsXBLPrototypeBinding* aProtoBinding);
  ~nsXBLBinding();

  /**
   * XBLBindings are refcounted.  They are held onto in 3 ways:
   * 1. The binding manager's binding table holds onto all bindings that are
   *    currently attached to a content node.
   * 2. Bindings hold onto their base binding.  This is important since
   *    the base binding itself may not be attached to anything.
   * 3. The binding manager holds an additional reference to bindings
   *    which are queued to fire their constructors.
   */

  NS_INLINE_DECL_REFCOUNTING(nsXBLBinding)

  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(nsXBLBinding)

  nsXBLPrototypeBinding* PrototypeBinding() { return mPrototypeBinding; }
  nsIContent* GetAnonymousContent() { return mContent.get(); }

  nsXBLBinding* GetBaseBinding() { return mNextBinding; }
  void SetBaseBinding(nsXBLBinding *aBinding);

  nsIContent* GetBoundElement() { return mBoundElement; }
  void SetBoundElement(nsIContent *aElement);

  bool IsStyleBinding() const { return mIsStyleBinding; }
  void SetIsStyleBinding(bool aIsStyle) { mIsStyleBinding = aIsStyle; }

  void MarkForDeath();
  bool MarkedForDeath() const { return mMarkedForDeath; }

  bool HasStyleSheets() const;
  bool InheritsStyle() const;
  bool ImplementsInterface(REFNSIID aIID) const;

  void GenerateAnonymousContent();
  void InstallAnonymousContent(nsIContent* aAnonParent, nsIContent* aElement);
  static void UninstallAnonymousContent(nsIDocument* aDocument,
                                        nsIContent* aAnonParent);
  void InstallEventHandlers();
  nsresult InstallImplementation();

  void ExecuteAttachedHandler();
  void ExecuteDetachedHandler();
  void UnhookEventHandlers();

  nsIAtom* GetBaseTag(PRInt32* aNameSpaceID);
  nsXBLBinding* RootBinding();
  nsXBLBinding* GetFirstStyleBinding();

  // Resolve all the fields for this binding and all ancestor bindings on the
  // object |obj|.  False return means a JS exception was set.
  bool ResolveAllFields(JSContext *cx, JSObject *obj) const;

  // Get the list of insertion points for aParent. The nsInsertionPointList
  // is owned by the binding, you should not delete it.
  void GetInsertionPointsFor(nsIContent* aParent,
                             nsInsertionPointList** aResult);

  nsInsertionPointList* GetExistingInsertionPointsFor(nsIContent* aParent);

  // XXXbz this aIndex has nothing to do with an index into the child
  // list of the insertion parent or anything.
  nsIContent* GetInsertionPoint(const nsIContent* aChild, PRUint32* aIndex);

  nsIContent* GetSingleInsertionPoint(PRUint32* aIndex,
                                      bool* aMultipleInsertionPoints);

  void AttributeChanged(nsIAtom* aAttribute, PRInt32 aNameSpaceID,
                        bool aRemoveFlag, bool aNotify);

  void ChangeDocument(nsIDocument* aOldDocument, nsIDocument* aNewDocument);

  void WalkRules(nsIStyleRuleProcessor::EnumFunc aFunc, void* aData);

  nsINodeList* GetAnonymousNodes();

  static nsresult DoInitJSClass(JSContext *cx, JSObject *global, JSObject *obj,
                                const nsAFlatCString& aClassName,
                                nsXBLPrototypeBinding* aProtoBinding,
                                JSObject** aClassObject);

  bool AllowScripts();  // XXX make const

  void RemoveInsertionParent(nsIContent* aParent);
  bool HasInsertionParent(nsIContent* aParent);

// MEMBER VARIABLES
protected:

  bool mIsStyleBinding;
  bool mMarkedForDeath;

  nsXBLPrototypeBinding* mPrototypeBinding; // Weak, but we're holding a ref to the docinfo
  nsCOMPtr<nsIContent> mContent; // Strong. Our anonymous content stays around with us.
  nsRefPtr<nsXBLBinding> mNextBinding; // Strong. The derived binding owns the base class bindings.
  
  nsIContent* mBoundElement; // [WEAK] We have a reference, but we don't own it.
  
  // A hash from nsIContent* -> (a sorted array of nsXBLInsertionPoint)
  nsClassHashtable<nsISupportsHashKey, nsInsertionPointList>* mInsertionPointTable;
};

#endif // nsXBLBinding_h_
