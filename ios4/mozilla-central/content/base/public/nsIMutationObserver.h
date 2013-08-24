/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ms2ger <ms2ger@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef nsIMutationObserver_h
#define nsIMutationObserver_h

#include "nsISupports.h"

class nsIAtom;
class nsIContent;
class nsIDocument;
class nsINode;

namespace mozilla {
namespace dom {
class Element;
} // namespace dom
} // namespace mozilla

#define NS_IMUTATION_OBSERVER_IID \
{ 0x85eea794, 0xed8e, 0x4e1b, \
  { 0xa1, 0x28, 0xd0, 0x93, 0x00, 0xae, 0x51, 0xaa } }

/**
 * Information details about a characterdata change.  Basically, we
 * view all changes as replacements of a length of text at some offset
 * with some other text (of possibly some other length).
 */
struct CharacterDataChangeInfo
{
  /**
   * True if this character data change is just an append.
   */
  PRBool mAppend;

  /**
   * The offset in the text where the change occurred.
   */
  PRUint32 mChangeStart;

  /**
   * The offset such that mChangeEnd - mChangeStart is equal to the length of
   * the text we removed. If this was a pure insert or append, this is equal to
   * mChangeStart.
   */
  PRUint32 mChangeEnd;

  /**
   * The length of the text that was inserted in place of the removed text.  If
   * this was a pure text removal, this is 0.
   */
  PRUint32 mReplaceLength;

  /**
   * The net result is that mChangeStart characters at the beginning of the
   * text remained as they were.  The next mChangeEnd - mChangeStart characters
   * were removed, and mReplaceLength characters were inserted in their place.
   * The text that used to begin at mChangeEnd now begins at
   * mChangeStart + mReplaceLength.
   */
};

/**
 * Mutation observer interface
 *
 * See nsINode::AddMutationObserver, nsINode::RemoveMutationObserver for how to
 * attach or remove your observers.
 *
 * WARNING: During these notifications, you are not allowed to perform
 * any mutations to the current or any other document, or start a
 * network load.  If you need to perform such operations do that
 * during the _last_ nsIDocumentObserver::EndUpdate notification.  The
 * expection for this is ParentChainChanged, where mutations should be
 * done from an async event, as the notification might not be
 * surrounded by BeginUpdate/EndUpdate calls.
 */
class nsIMutationObserver : public nsISupports
{
public:
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_IMUTATION_OBSERVER_IID)

  /**
   * Notification that the node value of a data node (text, cdata, pi, comment)
   * will be changed.
   *
   * This notification is not sent when a piece of content is
   * added/removed from the document (the other notifications are used
   * for that).
   *
   * @param aDocument The owner-document of aContent. Can be null.
   * @param aContent  The piece of content that changed. Is never null.
   * @param aInfo     The structure with information details about the change.
   *
   * @note Callers of this method might not hold a strong reference to the
   *       observer.  The observer is responsible for making sure it stays
   *       alive for the duration of the call as needed.  The observer may
   *       assume that this call will happen when there are script blockers on
   *       the stack.
   */
  virtual void CharacterDataWillChange(nsIDocument *aDocument,
                                       nsIContent* aContent,
                                       CharacterDataChangeInfo* aInfo) = 0;

  /**
   * Notification that the node value of a data node (text, cdata, pi, comment)
   * has changed.
   *
   * This notification is not sent when a piece of content is
   * added/removed from the document (the other notifications are used
   * for that).
   *
   * @param aDocument The owner-document of aContent. Can be null.
   * @param aContent  The piece of content that changed. Is never null.
   * @param aInfo     The structure with information details about the change.
   *
   * @note Callers of this method might not hold a strong reference to the
   *       observer.  The observer is responsible for making sure it stays
   *       alive for the duration of the call as needed.  The observer may
   *       assume that this call will happen when there are script blockers on
   *       the stack.
   */
  virtual void CharacterDataChanged(nsIDocument *aDocument,
                                    nsIContent* aContent,
                                    CharacterDataChangeInfo* aInfo) = 0;

  /**
   * Notification that an attribute of an element will change.  This
   * can happen before the BeginUpdate for the change and may not
   * always be followed by an AttributeChanged (in particular, if the
   * attribute doesn't actually change there will be no corresponding
   * AttributeChanged).
   *
   * @param aDocument    The owner-document of aContent. Can be null.
   * @param aContent     The element whose attribute will change
   * @param aNameSpaceID The namespace id of the changing attribute
   * @param aAttribute   The name of the changing attribute
   * @param aModType     Whether or not the attribute will be added, changed, or
   *                     removed. The constants are defined in
   *                     nsIDOMMutationEvent.h.
   *
   * @note Callers of this method might not hold a strong reference to the
   *       observer.  The observer is responsible for making sure it stays
   *       alive for the duration of the call as needed.  The observer may
   *       assume that this call will happen when there are script blockers on
   *       the stack.
   */
  virtual void AttributeWillChange(nsIDocument* aDocument,
                                   mozilla::dom::Element* aElement,
                                   PRInt32      aNameSpaceID,
                                   nsIAtom*     aAttribute,
                                   PRInt32      aModType) = 0;

  /**
   * Notification that an attribute of an element has changed.
   *
   * @param aDocument    The owner-document of aContent. Can be null.
   * @param aElement     The element whose attribute changed
   * @param aNameSpaceID The namespace id of the changed attribute
   * @param aAttribute   The name of the changed attribute
   * @param aModType     Whether or not the attribute was added, changed, or
   *                     removed. The constants are defined in
   *                     nsIDOMMutationEvent.h.
   *
   * @note Callers of this method might not hold a strong reference to the
   *       observer.  The observer is responsible for making sure it stays
   *       alive for the duration of the call as needed.  The observer may
   *       assume that this call will happen when there are script blockers on
   *       the stack.
   */
  virtual void AttributeChanged(nsIDocument* aDocument,
                                mozilla::dom::Element* aElement,
                                PRInt32      aNameSpaceID,
                                nsIAtom*     aAttribute,
                                PRInt32      aModType) = 0;

  /**
   * Notification that one or more content nodes have been appended to the
   * child list of another node in the tree.
   *
   * @param aDocument  The owner-document of aContent. Can be null.
   * @param aContainer The container that had new children appended. Is never
   *                   null.
   * @param aFirstNewContent the node at aIndexInContainer in aContainer.
   * @param aNewIndexInContainer the index in the container of the first
   *                   new child
   *
   * @note Callers of this method might not hold a strong reference to the
   *       observer.  The observer is responsible for making sure it stays
   *       alive for the duration of the call as needed.  The observer may
   *       assume that this call will happen when there are script blockers on
   *       the stack.
   */
  virtual void ContentAppended(nsIDocument *aDocument,
                               nsIContent* aContainer,
                               nsIContent* aFirstNewContent,
                               PRInt32     aNewIndexInContainer) = 0;

  /**
   * Notification that a content node has been inserted as child to another
   * node in the tree.
   *
   * @param aDocument  The owner-document of aContent, or, when aContainer
   *                   is null, the container that had the child inserted.
   *                   Can be null.
   * @param aContainer The container that had new a child inserted. Can be
   *                   null to indicate that the child was inserted into
   *                   aDocument
   * @param aChild     The newly inserted child.
   * @param aIndexInContainer The index in the container of the new child.
   *
   * @note Callers of this method might not hold a strong reference to the
   *       observer.  The observer is responsible for making sure it stays
   *       alive for the duration of the call as needed.  The observer may
   *       assume that this call will happen when there are script blockers on
   *       the stack.
   */
  virtual void ContentInserted(nsIDocument *aDocument,
                               nsIContent* aContainer,
                               nsIContent* aChild,
                               PRInt32 aIndexInContainer) = 0;

  /**
   * Notification that a content node has been removed from the child list of
   * another node in the tree.
   *
   * @param aDocument  The owner-document of aContent, or, when aContainer
   *                   is null, the container that had the child removed.
   *                   Can be null.
   * @param aContainer The container that had new a child removed. Can be
   *                   null to indicate that the child was removed from
   *                   aDocument.
   * @param aChild     The child that was removed.
   * @param aIndexInContainer The index in the container which the child used
   *                          to have.
   * @param aPreviousSibling The previous sibling to the child that was removed.
   *                         Can be null if there was no previous sibling.
   *
   * @note Callers of this method might not hold a strong reference to the
   *       observer.  The observer is responsible for making sure it stays
   *       alive for the duration of the call as needed.  The observer may
   *       assume that this call will happen when there are script blockers on
   *       the stack.
   */
  virtual void ContentRemoved(nsIDocument *aDocument,
                              nsIContent* aContainer,
                              nsIContent* aChild,
                              PRInt32 aIndexInContainer,
                              nsIContent* aPreviousSibling) = 0;

 /**
   * The node is in the process of being destroyed. Calling QI on the node is
   * not supported, however it is possible to get children and flags through
   * nsINode as well as calling IsNodeOfType(eCONTENT) and casting to
   * nsIContent to get attributes.
   *
   * NOTE: This notification is only called on observers registered directly
   * on the node. This is because when the node is destroyed it can not have
   * any ancestors. If you want to know when a descendant node is being
   * removed from the observed node, use the ContentRemoved notification.
   * 
   * @param aNode The node being destroyed.
   *
   * @note Callers of this method might not hold a strong reference to
   *       the observer.  The observer is responsible for making sure it
   *       stays alive for the duration of the call as needed.
   */
  virtual void NodeWillBeDestroyed(const nsINode *aNode) = 0;

  /**
   * Notification that the node's parent chain has changed. This
   * happens when either the node or one of its ancestors is inserted
   * or removed as a child of another node.
   *
   * Note that when a node is inserted this notification is sent to
   * all descendants of that node, since all such nodes have their
   * parent chain changed.
   *
   * @param aContent  The piece of content that had its parent changed.
   *
   * @note Callers of this method might not hold a strong reference to
   *       the observer.  The observer is responsible for making sure it
   *       stays alive for the duration of the call as needed.
   */

  virtual void ParentChainChanged(nsIContent *aContent) = 0;
};

NS_DEFINE_STATIC_IID_ACCESSOR(nsIMutationObserver, NS_IMUTATION_OBSERVER_IID)

#define NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATAWILLCHANGE                  \
    virtual void CharacterDataWillChange(nsIDocument* aDocument,             \
                                         nsIContent* aContent,               \
                                         CharacterDataChangeInfo* aInfo);

#define NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATACHANGED                     \
    virtual void CharacterDataChanged(nsIDocument* aDocument,                \
                                      nsIContent* aContent,                  \
                                      CharacterDataChangeInfo* aInfo);

#define NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTEWILLCHANGE                      \
    virtual void AttributeWillChange(nsIDocument* aDocument,                 \
                                     mozilla::dom::Element* aElement,        \
                                     PRInt32 aNameSpaceID,                   \
                                     nsIAtom* aAttribute,                    \
                                     PRInt32 aModType);

#define NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED                         \
    virtual void AttributeChanged(nsIDocument* aDocument,                    \
                                  mozilla::dom::Element* aElement,           \
                                  PRInt32 aNameSpaceID,                      \
                                  nsIAtom* aAttribute,                       \
                                  PRInt32 aModType);

#define NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED                          \
    virtual void ContentAppended(nsIDocument* aDocument,                     \
                                 nsIContent* aContainer,                     \
                                 nsIContent* aFirstNewContent,               \
                                 PRInt32 aNewIndexInContainer);

#define NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED                          \
    virtual void ContentInserted(nsIDocument* aDocument,                     \
                                 nsIContent* aContainer,                     \
                                 nsIContent* aChild,                         \
                                 PRInt32 aIndexInContainer);

#define NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED                           \
    virtual void ContentRemoved(nsIDocument* aDocument,                      \
                                nsIContent* aContainer,                      \
                                nsIContent* aChild,                          \
                                PRInt32 aIndexInContainer,                   \
                                nsIContent* aPreviousSibling);

#define NS_DECL_NSIMUTATIONOBSERVER_NODEWILLBEDESTROYED                      \
    virtual void NodeWillBeDestroyed(const nsINode* aNode);

#define NS_DECL_NSIMUTATIONOBSERVER_PARENTCHAINCHANGED                       \
    virtual void ParentChainChanged(nsIContent *aContent);

#define NS_DECL_NSIMUTATIONOBSERVER                                          \
    NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATAWILLCHANGE                      \
    NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATACHANGED                         \
    NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTEWILLCHANGE                          \
    NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED                             \
    NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED                              \
    NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED                              \
    NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED                               \
    NS_DECL_NSIMUTATIONOBSERVER_NODEWILLBEDESTROYED                          \
    NS_DECL_NSIMUTATIONOBSERVER_PARENTCHAINCHANGED

#define NS_IMPL_NSIMUTATIONOBSERVER_CORE_STUB(_class)                     \
void                                                                      \
_class::NodeWillBeDestroyed(const nsINode* aNode)                               \
{                                                                         \
}

#define NS_IMPL_NSIMUTATIONOBSERVER_CONTENT(_class)                       \
void                                                                      \
_class::CharacterDataWillChange(nsIDocument* aDocument,                   \
                                nsIContent* aContent,                     \
                                CharacterDataChangeInfo* aInfo)           \
{                                                                         \
}                                                                         \
void                                                                      \
_class::CharacterDataChanged(nsIDocument* aDocument,                      \
                             nsIContent* aContent,                        \
                             CharacterDataChangeInfo* aInfo)              \
{                                                                         \
}                                                                         \
void                                                                      \
_class::AttributeWillChange(nsIDocument* aDocument,                       \
                            mozilla::dom::Element* aElement,              \
                            PRInt32 aNameSpaceID,                         \
                            nsIAtom* aAttribute,                          \
                            PRInt32 aModType)                             \
{                                                                         \
}                                                                         \
void                                                                      \
_class::AttributeChanged(nsIDocument* aDocument,                          \
                         mozilla::dom::Element* aElement,                 \
                         PRInt32 aNameSpaceID,                            \
                         nsIAtom* aAttribute,                             \
                         PRInt32 aModType)                                \
{                                                                         \
}                                                                         \
void                                                                      \
_class::ContentAppended(nsIDocument* aDocument,                           \
                        nsIContent* aContainer,                           \
                        nsIContent* aFirstNewContent,                     \
                        PRInt32 aNewIndexInContainer)                     \
{                                                                         \
}                                                                         \
void                                                                      \
_class::ContentInserted(nsIDocument* aDocument,                           \
                        nsIContent* aContainer,                           \
                        nsIContent* aChild,                               \
                        PRInt32 aIndexInContainer)                        \
{                                                                         \
}                                                                         \
void                                                                      \
_class::ContentRemoved(nsIDocument* aDocument,                            \
                       nsIContent* aContainer,                            \
                       nsIContent* aChild,                                \
                       PRInt32 aIndexInContainer,                         \
                       nsIContent* aPreviousSibling)                      \
{                                                                         \
}                                                                         \
void                                                                      \
_class::ParentChainChanged(nsIContent *aContent)                          \
{                                                                         \
}


#endif /* nsIMutationObserver_h */
