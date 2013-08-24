/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __editor_h__
#define __editor_h__

#include "mozilla/Assertions.h"         // for MOZ_ASSERT, etc
#include "nsAutoPtr.h"                  // for nsRefPtr
#include "nsCOMArray.h"                 // for nsCOMArray
#include "nsCOMPtr.h"                   // for already_AddRefed, nsCOMPtr
#include "nsCycleCollectionParticipant.h"
#include "nsEditProperty.h"             // for nsEditProperty, etc
#include "nsIEditor.h"                  // for nsIEditor::EDirection, etc
#include "nsIEditorIMESupport.h"        // for NS_DECL_NSIEDITORIMESUPPORT, etc
#include "nsIObserver.h"                // for NS_DECL_NSIOBSERVER, etc
#include "nsIPhonetic.h"                // for NS_DECL_NSIPHONETIC, etc
#include "nsIPlaintextEditor.h"         // for nsIPlaintextEditor, etc
#include "nsISupportsImpl.h"            // for nsEditor::Release, etc
#include "nsIWeakReferenceUtils.h"      // for nsWeakPtr
#include "nsLiteralString.h"            // for NS_LITERAL_STRING
#include "nsSelectionState.h"           // for nsRangeUpdater, etc
#include "nsString.h"                   // for nsCString
#include "nsWeakReference.h"            // for nsSupportsWeakReference
#include "nscore.h"                     // for nsresult, nsAString, etc
#include "prtypes.h"                    // for PRInt32, PRUint32, PRInt8, etc

class AddStyleSheetTxn;
class ChangeAttributeTxn;
class CreateElementTxn;
class DeleteNodeTxn;
class DeleteTextTxn;
class EditAggregateTxn;
class IMETextTxn;
class InsertElementTxn;
class InsertTextTxn;
class JoinElementTxn;
class RemoveStyleSheetTxn;
class SplitElementTxn;
class nsCSSStyleSheet;
class nsIAtom;
class nsIContent;
class nsIDOMCharacterData;
class nsIDOMDataTransfer;
class nsIDOMDocument;
class nsIDOMElement;
class nsIDOMEvent;
class nsIDOMEventListener;
class nsIDOMEventTarget;
class nsIDOMKeyEvent;
class nsIDOMNSEvent;
class nsIDOMNode;
class nsIDOMRange;
class nsIDocument;
class nsIDocumentStateListener;
class nsIEditActionListener;
class nsIEditorObserver;
class nsIInlineSpellChecker;
class nsINode;
class nsIPresShell;
class nsIPrivateTextRangeList;
class nsISelection;
class nsISupports;
class nsITransaction;
class nsIWidget;
class nsKeyEvent;
class nsRange;
class nsString;
class nsTransactionManager;

namespace mozilla {
class Selection;

namespace dom {
class Element;
}  // namespace dom
}  // namespace mozilla

namespace mozilla {
namespace widget {
struct IMEState;
} // namespace widget
} // namespace mozilla

#define kMOZEditorBogusNodeAttrAtom nsEditProperty::mozEditorBogusNode
#define kMOZEditorBogusNodeValue NS_LITERAL_STRING("TRUE")

/** implementation of an editor object.  it will be the controller/focal point 
 *  for the main editor services. i.e. the GUIManager, publishing, transaction 
 *  manager, event interfaces. the idea for the event interfaces is to have them 
 *  delegate the actual commands to the editor independent of the XPFE implementation.
 */
class nsEditor : public nsIEditor,
                 public nsIEditorIMESupport,
                 public nsSupportsWeakReference,
                 public nsIObserver,
                 public nsIPhonetic
{
public:

  enum IterDirection
  {
    kIterForward,
    kIterBackward
  };

  enum OperationID
  {
    kOpIgnore = -1,
    kOpNone = 0,
    kOpUndo,
    kOpRedo,
    kOpInsertNode,
    kOpCreateNode,
    kOpDeleteNode,
    kOpSplitNode,
    kOpJoinNode,
    kOpDeleteText = 1003,

    // text commands
    kOpInsertText         = 2000,
    kOpInsertIMEText      = 2001,
    kOpDeleteSelection    = 2002,
    kOpSetTextProperty    = 2003,
    kOpRemoveTextProperty = 2004,
    kOpOutputText         = 2005,

    // html only action
    kOpInsertBreak         = 3000,
    kOpMakeList            = 3001,
    kOpIndent              = 3002,
    kOpOutdent             = 3003,
    kOpAlign               = 3004,
    kOpMakeBasicBlock      = 3005,
    kOpRemoveList          = 3006,
    kOpMakeDefListItem     = 3007,
    kOpInsertElement       = 3008,
    kOpInsertQuotation     = 3009,
    kOpHTMLPaste           = 3012,
    kOpLoadHTML            = 3013,
    kOpResetTextProperties = 3014,
    kOpSetAbsolutePosition = 3015,
    kOpRemoveAbsolutePosition = 3016,
    kOpDecreaseZIndex      = 3017,
    kOpIncreaseZIndex      = 3018
  };

  /** The default constructor. This should suffice. the setting of the interfaces is done
   *  after the construction of the editor class.
   */
  nsEditor();
  /** The default destructor. This should suffice. Should this be pure virtual 
   *  for someone to derive from the nsEditor later? I don't believe so.
   */
  virtual ~nsEditor();

//Interfaces for addref and release and queryinterface
//NOTE: Use   NS_DECL_ISUPPORTS_INHERITED in any class inherited from nsEditor
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsEditor,
                                           nsIEditor)

  /* ------------ utility methods   -------------- */
  already_AddRefed<nsIDOMDocument> GetDOMDocument();
  already_AddRefed<nsIDocument> GetDocument();
  already_AddRefed<nsIPresShell> GetPresShell();
  void NotifyEditorObservers();

  /* ------------ nsIEditor methods -------------- */
  NS_DECL_NSIEDITOR
  /* ------------ nsIEditorIMESupport methods -------------- */
  NS_DECL_NSIEDITORIMESUPPORT
  
  /* ------------ nsIObserver methods -------------- */
  NS_DECL_NSIOBSERVER

  // nsIPhonetic
  NS_DECL_NSIPHONETIC

public:

  virtual bool IsModifiableNode(nsINode *aNode);
  
  NS_IMETHOD InsertTextImpl(const nsAString& aStringToInsert, 
                               nsCOMPtr<nsIDOMNode> *aInOutNode, 
                               PRInt32 *aInOutOffset,
                               nsIDOMDocument *aDoc);
  nsresult InsertTextIntoTextNodeImpl(const nsAString& aStringToInsert, 
                                      nsIDOMCharacterData *aTextNode, 
                                      PRInt32 aOffset,
                                      bool aSuppressIME = false);
  NS_IMETHOD DeleteSelectionImpl(EDirection aAction,
                                 EStripWrappers aStripWrappers);
  NS_IMETHOD DeleteSelectionAndCreateNode(const nsAString& aTag,
                                           nsIDOMNode ** aNewNode);

  /* helper routines for node/parent manipulations */
  nsresult DeleteNode(nsINode* aNode);
  nsresult ReplaceContainer(nsINode* inNode,
                            mozilla::dom::Element** outNode,
                            const nsAString& aNodeType,
                            const nsAString* aAttribute = nsnull,
                            const nsAString* aValue = nsnull,
                            bool aCloneAttributes = false);
  nsresult ReplaceContainer(nsIDOMNode *inNode, 
                            nsCOMPtr<nsIDOMNode> *outNode, 
                            const nsAString &aNodeType,
                            const nsAString *aAttribute = nsnull,
                            const nsAString *aValue = nsnull,
                            bool aCloneAttributes = false);

  nsresult RemoveContainer(nsINode* aNode);
  nsresult RemoveContainer(nsIDOMNode *inNode);
  nsresult InsertContainerAbove(nsIContent* aNode,
                                mozilla::dom::Element** aOutNode,
                                const nsAString& aNodeType,
                                const nsAString* aAttribute = nsnull,
                                const nsAString* aValue = nsnull);
  nsresult InsertContainerAbove(nsIDOMNode *inNode, 
                                nsCOMPtr<nsIDOMNode> *outNode, 
                                const nsAString &aNodeType,
                                const nsAString *aAttribute = nsnull,
                                const nsAString *aValue = nsnull);
  nsresult JoinNodes(nsINode* aNodeToKeep, nsIContent* aNodeToMove);
  nsresult MoveNode(nsIContent* aNode, nsINode* aParent, PRInt32 aOffset);
  nsresult MoveNode(nsIDOMNode *aNode, nsIDOMNode *aParent, PRInt32 aOffset);

  /* Method to replace certain CreateElementNS() calls. 
     Arguments:
      nsString& aTag          - tag you want
      nsIContent** aContent   - returned Content that was created with above namespace.
  */
  nsresult CreateHTMLContent(const nsAString& aTag,
                             mozilla::dom::Element** aContent);

  // IME event handlers
  virtual nsresult BeginIMEComposition();
  virtual nsresult UpdateIMEComposition(const nsAString &aCompositionString,
                                        nsIPrivateTextRangeList *aTextRange)=0;
  void EndIMEComposition();

  void SwitchTextDirectionTo(PRUint32 aDirection);

protected:
  nsresult DetermineCurrentDirection();

  /** create a transaction for setting aAttribute to aValue on aElement
    */
  NS_IMETHOD CreateTxnForSetAttribute(nsIDOMElement *aElement, 
                                      const nsAString &  aAttribute, 
                                      const nsAString &  aValue,
                                      ChangeAttributeTxn ** aTxn);

  /** create a transaction for removing aAttribute on aElement
    */
  NS_IMETHOD CreateTxnForRemoveAttribute(nsIDOMElement *aElement, 
                                         const nsAString &  aAttribute,
                                         ChangeAttributeTxn ** aTxn);

  /** create a transaction for creating a new child node of aParent of type aTag.
    */
  NS_IMETHOD CreateTxnForCreateElement(const nsAString & aTag,
                                       nsIDOMNode     *aParent,
                                       PRInt32         aPosition,
                                       CreateElementTxn ** aTxn);

  /** create a transaction for inserting aNode as a child of aParent.
    */
  NS_IMETHOD CreateTxnForInsertElement(nsIDOMNode * aNode,
                                       nsIDOMNode * aParent,
                                       PRInt32      aOffset,
                                       InsertElementTxn ** aTxn);

  /** create a transaction for removing aNode from its parent.
    */
  nsresult CreateTxnForDeleteNode(nsINode* aNode, DeleteNodeTxn** aTxn);


  nsresult CreateTxnForDeleteSelection(EDirection aAction,
                                       EditAggregateTxn** aTxn,
                                       nsINode** aNode,
                                       PRInt32* aOffset,
                                       PRInt32* aLength);

  nsresult CreateTxnForDeleteInsertionPoint(nsRange* aRange, 
                                            EDirection aAction, 
                                            EditAggregateTxn* aTxn,
                                            nsINode** aNode,
                                            PRInt32* aOffset,
                                            PRInt32* aLength);


  /** create a transaction for inserting aStringToInsert into aTextNode
    * if aTextNode is null, the string is inserted at the current selection.
    */
  NS_IMETHOD CreateTxnForInsertText(const nsAString & aStringToInsert,
                                    nsIDOMCharacterData *aTextNode,
                                    PRInt32 aOffset,
                                    InsertTextTxn ** aTxn);

  NS_IMETHOD CreateTxnForIMEText(const nsAString & aStringToInsert,
                                 IMETextTxn ** aTxn);

  /** create a transaction for adding a style sheet
    */
  NS_IMETHOD CreateTxnForAddStyleSheet(nsCSSStyleSheet* aSheet, AddStyleSheetTxn* *aTxn);

  /** create a transaction for removing a style sheet
    */
  NS_IMETHOD CreateTxnForRemoveStyleSheet(nsCSSStyleSheet* aSheet, RemoveStyleSheetTxn* *aTxn);
  
  NS_IMETHOD DeleteText(nsIDOMCharacterData *aElement,
                        PRUint32             aOffset,
                        PRUint32             aLength);

//  NS_IMETHOD DeleteRange(nsIDOMRange *aRange);

  nsresult CreateTxnForDeleteText(nsIDOMCharacterData* aElement,
                                  PRUint32             aOffset,
                                  PRUint32             aLength,
                                  DeleteTextTxn**      aTxn);

  nsresult CreateTxnForDeleteCharacter(nsIDOMCharacterData* aData,
                                       PRUint32             aOffset,
                                       EDirection           aDirection,
                                       DeleteTextTxn**      aTxn);
	
  NS_IMETHOD CreateTxnForSplitNode(nsIDOMNode *aNode,
                                   PRUint32    aOffset,
                                   SplitElementTxn **aTxn);

  NS_IMETHOD CreateTxnForJoinNode(nsIDOMNode  *aLeftNode,
                                  nsIDOMNode  *aRightNode,
                                  JoinElementTxn **aTxn);

  /**
   * This method first deletes the selection, if it's not collapsed.  Then if
   * the selection lies in a CharacterData node, it splits it.  If the
   * selection is at this point collapsed in a CharacterData node, it's
   * adjusted to be collapsed right before or after the node instead (which is
   * always possible, since the node was split).
   */
  nsresult DeleteSelectionAndPrepareToCreateNode();


  // called after a transaction is done successfully
  void DoAfterDoTransaction(nsITransaction *aTxn);
  // called after a transaction is undone successfully
  void DoAfterUndoTransaction();
  // called after a transaction is redone successfully
  void DoAfterRedoTransaction();

  typedef enum {
    eDocumentCreated,
    eDocumentToBeDestroyed,
    eDocumentStateChanged
  } TDocumentListenerNotification;
  
  // tell the doc state listeners that the doc state has changed
  NS_IMETHOD NotifyDocumentListeners(TDocumentListenerNotification aNotificationType);
  
  /** make the given selection span the entire document */
  NS_IMETHOD SelectEntireDocument(nsISelection *aSelection);

  /** helper method for scrolling the selection into view after
   *  an edit operation. aScrollToAnchor should be true if you
   *  want to scroll to the point where the selection was started.
   *  If false, it attempts to scroll the end of the selection into view.
   *
   *  Editor methods *should* call this method instead of the versions
   *  in the various selection interfaces, since this version makes sure
   *  that the editor's sync/async settings for reflowing, painting, and
   *  scrolling match.
   */
  NS_IMETHOD ScrollSelectionIntoView(bool aScrollToAnchor);

  // Convenience method; forwards to IsBlockNode(nsINode*).
  bool IsBlockNode(nsIDOMNode* aNode);
  // stub.  see comment in source.                     
  virtual bool IsBlockNode(nsINode* aNode);
  
  // helper for GetPriorNode and GetNextNode
  nsIContent* FindNextLeafNode(nsINode  *aCurrentNode,
                               bool      aGoForward,
                               bool      bNoBlockCrossing);

  // Get nsIWidget interface
  nsresult GetWidget(nsIWidget **aWidget);


  // install the event listeners for the editor 
  virtual nsresult InstallEventListeners();

  virtual void CreateEventListeners();

  // unregister and release our event listeners
  virtual void RemoveEventListeners();

  /**
   * Return true if spellchecking should be enabled for this editor.
   */
  bool GetDesiredSpellCheckState();

  nsKeyEvent* GetNativeKeyEvent(nsIDOMKeyEvent* aDOMKeyEvent);

  bool CanEnableSpellCheck()
  {
    // Check for password/readonly/disabled, which are not spellchecked
    // regardless of DOM. Also, check to see if spell check should be skipped or not.
    return !IsPasswordEditor() && !IsReadonly() && !IsDisabled() && !ShouldSkipSpellCheck();
  }

public:

  /** All editor operations which alter the doc should be prefaced
   *  with a call to StartOperation, naming the action and direction */
  NS_IMETHOD StartOperation(OperationID opID,
                            nsIEditor::EDirection aDirection);

  /** All editor operations which alter the doc should be followed
   *  with a call to EndOperation */
  NS_IMETHOD EndOperation();

  /** routines for managing the preservation of selection across 
   *  various editor actions */
  bool     ArePreservingSelection();
  void     PreserveSelectionAcrossActions(nsISelection *aSel);
  nsresult RestorePreservedSelection(nsISelection *aSel);
  void     StopPreservingSelection();

  /** 
   * SplitNode() creates a new node identical to an existing node, and split the contents between the two nodes
   * @param aExistingRightNode   the node to split.  It will become the new node's next sibling.
   * @param aOffset              the offset of aExistingRightNode's content|children to do the split at
   * @param aNewLeftNode         [OUT] the new node resulting from the split, becomes aExistingRightNode's previous sibling.
   * @param aParent              the parent of aExistingRightNode
   */
  nsresult SplitNodeImpl(nsIDOMNode *aExistingRightNode,
                         PRInt32     aOffset,
                         nsIDOMNode *aNewLeftNode,
                         nsIDOMNode *aParent);

  /** 
   * JoinNodes() takes 2 nodes and merge their content|children.
   * @param aNodeToKeep   The node that will remain after the join.
   * @param aNodeToJoin   The node that will be joined with aNodeToKeep.
   *                      There is no requirement that the two nodes be of the same type.
   * @param aParent       The parent of aNodeToKeep
   * @param aNodeToKeepIsFirst  if true, the contents|children of aNodeToKeep come before the
   *                            contents|children of aNodeToJoin, otherwise their positions are switched.
   */
  nsresult JoinNodesImpl(nsIDOMNode *aNodeToKeep,
                         nsIDOMNode *aNodeToJoin,
                         nsIDOMNode *aParent,
                         bool        aNodeToKeepIsFirst);

  /**
   * Return the offset of aChild in aParent.  Asserts fatally if parent or
   * child is null, or parent is not child's parent.
   */
  static PRInt32 GetChildOffset(nsIDOMNode *aChild,
                                nsIDOMNode *aParent);

  /**
   *  Set outOffset to the offset of aChild in the parent.
   *  Returns the parent of aChild.
   */
  static already_AddRefed<nsIDOMNode> GetNodeLocation(nsIDOMNode* aChild,
                                                      PRInt32* outOffset);

  /** returns the number of things inside aNode in the out-param aCount.  
    * @param  aNode is the node to get the length of.  
    *         If aNode is text, returns number of characters. 
    *         If not, returns number of children nodes.
    * @param  aCount [OUT] the result of the above calculation.
    */
  static nsresult GetLengthOfDOMNode(nsIDOMNode *aNode, PRUint32 &aCount);

  /** get the node immediately prior to aCurrentNode
    * @param aCurrentNode   the node from which we start the search
    * @param aEditableNode  if true, only return an editable node
    * @param aResultNode    [OUT] the node that occurs before aCurrentNode in the tree,
    *                       skipping non-editable nodes if aEditableNode is true.
    *                       If there is no prior node, aResultNode will be nsnull.
    * @param bNoBlockCrossing If true, don't move across "block" nodes, whatever that means.
    */
  nsresult GetPriorNode(nsIDOMNode  *aCurrentNode, 
                        bool         aEditableNode,
                        nsCOMPtr<nsIDOMNode> *aResultNode,
                        bool         bNoBlockCrossing = false);
  nsIContent* GetPriorNode(nsINode* aCurrentNode, bool aEditableNode,
                           bool aNoBlockCrossing = false);

  // and another version that takes a {parent,offset} pair rather than a node
  nsresult GetPriorNode(nsIDOMNode  *aParentNode, 
                        PRInt32      aOffset, 
                        bool         aEditableNode, 
                        nsCOMPtr<nsIDOMNode> *aResultNode,
                        bool         bNoBlockCrossing = false);
  nsIContent* GetPriorNode(nsINode* aParentNode,
                           PRInt32 aOffset,
                           bool aEditableNode,
                           bool aNoBlockCrossing = false);


  /** get the node immediately after to aCurrentNode
    * @param aCurrentNode   the node from which we start the search
    * @param aEditableNode  if true, only return an editable node
    * @param aResultNode    [OUT] the node that occurs after aCurrentNode in the tree,
    *                       skipping non-editable nodes if aEditableNode is true.
    *                       If there is no prior node, aResultNode will be nsnull.
    */
  nsresult GetNextNode(nsIDOMNode  *aCurrentNode, 
                       bool         aEditableNode,
                       nsCOMPtr<nsIDOMNode> *aResultNode,
                       bool         bNoBlockCrossing = false);
  nsIContent* GetNextNode(nsINode* aCurrentNode,
                          bool aEditableNode,
                          bool bNoBlockCrossing = false);

  // and another version that takes a {parent,offset} pair rather than a node
  nsresult GetNextNode(nsIDOMNode  *aParentNode, 
                       PRInt32      aOffset, 
                       bool         aEditableNode, 
                       nsCOMPtr<nsIDOMNode> *aResultNode,
                       bool         bNoBlockCrossing = false);
  nsIContent* GetNextNode(nsINode* aParentNode,
                          PRInt32 aOffset,
                          bool aEditableNode,
                          bool aNoBlockCrossing = false);

  // Helper for GetNextNode and GetPriorNode
  nsIContent* FindNode(nsINode *aCurrentNode,
                       bool     aGoForward,
                       bool     aEditableNode,
                       bool     bNoBlockCrossing);
  /**
   * Get the rightmost child of aCurrentNode;
   * return nsnull if aCurrentNode has no children.
   */
  already_AddRefed<nsIDOMNode> GetRightmostChild(nsIDOMNode *aCurrentNode, 
                                                 bool        bNoBlockCrossing = false);
  nsIContent* GetRightmostChild(nsINode *aCurrentNode,
                                bool     bNoBlockCrossing = false);

  /**
   * Get the leftmost child of aCurrentNode;
   * return nsnull if aCurrentNode has no children.
   */
  already_AddRefed<nsIDOMNode> GetLeftmostChild(nsIDOMNode  *aCurrentNode, 
                                                bool        bNoBlockCrossing = false);
  nsIContent* GetLeftmostChild(nsINode *aCurrentNode,
                               bool     bNoBlockCrossing = false);

  /** returns true if aNode is of the type implied by aTag */
  static inline bool NodeIsType(nsIDOMNode *aNode, nsIAtom *aTag)
  {
    return GetTag(aNode) == aTag;
  }

  /** returns true if aParent can contain a child of type aTag */
  bool CanContain(nsIDOMNode* aParent, nsIDOMNode* aChild);
  bool CanContainTag(nsIDOMNode* aParent, nsIAtom* aTag);
  bool TagCanContain(nsIAtom* aParentTag, nsIDOMNode* aChild);
  virtual bool TagCanContainTag(nsIAtom* aParentTag, nsIAtom* aChildTag);

  /** returns true if aNode is our root node */
  bool IsRoot(nsIDOMNode* inNode);
  bool IsRoot(nsINode* inNode);
  bool IsEditorRoot(nsINode* aNode);

  /** returns true if aNode is a descendant of our root node */
  bool IsDescendantOfRoot(nsIDOMNode* inNode);
  bool IsDescendantOfRoot(nsINode* inNode);
  bool IsDescendantOfEditorRoot(nsIDOMNode* aNode);
  bool IsDescendantOfEditorRoot(nsINode* aNode);

  /** returns true if aNode is a container */
  virtual bool IsContainer(nsIDOMNode *aNode);

  /** returns true if aNode is an editable node */
  bool IsEditable(nsIDOMNode *aNode);
  virtual bool IsEditable(nsIContent *aNode);

  /**
   * aNode must be a non-null text node.
   */
  virtual bool IsTextInDirtyFrameVisible(nsIContent *aNode);

  /** returns true if aNode is a MozEditorBogus node */
  bool IsMozEditorBogusNode(nsIContent *aNode);

  /** counts number of editable child nodes */
  PRUint32 CountEditableChildren(nsINode* aNode);
  
  /** Find the deep first and last children. */
  nsINode* GetFirstEditableNode(nsINode* aRoot);

  PRInt32 GetIMEBufferLength();
  bool IsIMEComposing();    /* test if IME is in composition state */
  void SetIsIMEComposing(); /* call this before |IsIMEComposing()| */

  /** from html rules code - migration in progress */
  static nsresult GetTagString(nsIDOMNode *aNode, nsAString& outString);
  static nsIAtom *GetTag(nsIDOMNode *aNode);

  bool NodesSameType(nsIDOMNode *aNode1, nsIDOMNode *aNode2);
  virtual bool AreNodesSameType(nsIContent* aNode1, nsIContent* aNode2);

  static bool IsTextNode(nsIDOMNode *aNode);
  static bool IsTextNode(nsINode *aNode);
  
  static nsCOMPtr<nsIDOMNode> GetChildAt(nsIDOMNode *aParent, PRInt32 aOffset);
  static nsCOMPtr<nsIDOMNode> GetNodeAtRangeOffsetPoint(nsIDOMNode* aParentOrNode, PRInt32 aOffset);

  static nsresult GetStartNodeAndOffset(nsISelection *aSelection, nsIDOMNode **outStartNode, PRInt32 *outStartOffset);
  static nsresult GetEndNodeAndOffset(nsISelection *aSelection, nsIDOMNode **outEndNode, PRInt32 *outEndOffset);
#if DEBUG_JOE
  static void DumpNode(nsIDOMNode *aNode, PRInt32 indent=0);
#endif
  mozilla::Selection* GetSelection();

  // Helpers to add a node to the selection. 
  // Used by table cell selection methods
  nsresult CreateRange(nsIDOMNode *aStartParent, PRInt32 aStartOffset,
                       nsIDOMNode *aEndParent, PRInt32 aEndOffset,
                       nsIDOMRange **aRange);

  // Creates a range with just the supplied node and appends that to the selection
  nsresult AppendNodeToSelectionAsRange(nsIDOMNode *aNode);
  // When you are using AppendNodeToSelectionAsRange, call this first to start a new selection
  nsresult ClearSelection();

  nsresult IsPreformatted(nsIDOMNode *aNode, bool *aResult);

  nsresult SplitNodeDeep(nsIDOMNode *aNode, 
                         nsIDOMNode *aSplitPointParent, 
                         PRInt32 aSplitPointOffset,
                         PRInt32 *outOffset,
                         bool    aNoEmptyContainers = false,
                         nsCOMPtr<nsIDOMNode> *outLeftNode = 0,
                         nsCOMPtr<nsIDOMNode> *outRightNode = 0);
  nsresult JoinNodeDeep(nsIDOMNode *aLeftNode, nsIDOMNode *aRightNode, nsCOMPtr<nsIDOMNode> *aOutJoinNode, PRInt32 *outOffset); 

  nsresult GetString(const nsAString& name, nsAString& value);

  void BeginUpdateViewBatch(void);
  virtual nsresult EndUpdateViewBatch(void);

  bool GetShouldTxnSetSelection();

  virtual nsresult HandleKeyPressEvent(nsIDOMKeyEvent* aKeyEvent);

  nsresult HandleInlineSpellCheck(OperationID action,
                                    nsISelection *aSelection,
                                    nsIDOMNode *previousSelectedNode,
                                    PRInt32 previousSelectedOffset,
                                    nsIDOMNode *aStartNode,
                                    PRInt32 aStartOffset,
                                    nsIDOMNode *aEndNode,
                                    PRInt32 aEndOffset);

  virtual already_AddRefed<nsIDOMEventTarget> GetDOMEventTarget() = 0;

  // Fast non-refcounting editor root element accessor
  mozilla::dom::Element *GetRoot();

  // Likewise, but gets the editor's root instead, which is different for HTML
  // editors
  virtual mozilla::dom::Element* GetEditorRoot();

  // Accessor methods to flags
  bool IsPlaintextEditor() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorPlaintextMask) != 0;
  }

  bool IsSingleLineEditor() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorSingleLineMask) != 0;
  }

  bool IsPasswordEditor() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorPasswordMask) != 0;
  }

  bool IsReadonly() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorReadonlyMask) != 0;
  }

  bool IsDisabled() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorDisabledMask) != 0;
  }

  bool IsInputFiltered() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorFilterInputMask) != 0;
  }

  bool IsMailEditor() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorMailMask) != 0;
  }

  bool IsWrapHackEnabled() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorEnableWrapHackMask) != 0;
  }

  bool IsFormWidget() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorWidgetMask) != 0;
  }

  bool NoCSS() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorNoCSSMask) != 0;
  }

  bool IsInteractionAllowed() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorAllowInteraction) != 0;
  }

  bool DontEchoPassword() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorDontEchoPassword) != 0;
  }
  
  bool ShouldSkipSpellCheck() const
  {
    return (mFlags & nsIPlaintextEditor::eEditorSkipSpellCheck) != 0;
  }

  bool IsTabbable() const
  {
    return IsSingleLineEditor() || IsPasswordEditor() || IsFormWidget() ||
           IsInteractionAllowed();
  }

  // Get the input event target. This might return null.
  virtual already_AddRefed<nsIContent> GetInputEventTargetContent() = 0;

  // Get the focused content, if we're focused.  Returns null otherwise.
  virtual already_AddRefed<nsIContent> GetFocusedContent();

  // Whether the editor is active on the DOM window.  Note that when this
  // returns true but GetFocusedContent() returns null, it means that this editor was
  // focused when the DOM window was active.
  virtual bool IsActiveInDOMWindow();

  // Whether the aEvent should be handled by this editor or not.  When this
  // returns FALSE, The aEvent shouldn't be handled on this editor,
  // i.e., The aEvent should be handled by another inner editor or ancestor
  // elements.
  virtual bool IsAcceptableInputEvent(nsIDOMEvent* aEvent);

  // FindSelectionRoot() returns a selection root of this editor when aNode
  // gets focus.  aNode must be a content node or a document node.  When the
  // target isn't a part of this editor, returns NULL.  If this is for
  // designMode, you should set the document node to aNode except that an
  // element in the document has focus.
  virtual already_AddRefed<nsIContent> FindSelectionRoot(nsINode* aNode);

  // Initializes selection and caret for the editor.  If aEventTarget isn't
  // a host of the editor, i.e., the editor doesn't get focus, this does
  // nothing.
  nsresult InitializeSelection(nsIDOMEventTarget* aFocusEventTarget);

  // This method has to be called by nsEditorEventListener::Focus.
  // All actions that have to be done when the editor is focused needs to be
  // added here.
  void OnFocus(nsIDOMEventTarget* aFocusEventTarget);

  // Used to insert content from a data transfer into the editable area.
  // This is called for each item in the data transfer, with the index of
  // each item passed as aIndex.
  virtual nsresult InsertFromDataTransfer(nsIDOMDataTransfer *aDataTransfer,
                                          PRInt32 aIndex,
                                          nsIDOMDocument *aSourceDoc,
                                          nsIDOMNode *aDestinationNode,
                                          PRInt32 aDestOffset,
                                          bool aDoDeleteSelection) = 0;

  virtual nsresult InsertFromDrop(nsIDOMEvent* aDropEvent) = 0;

  virtual already_AddRefed<nsIDOMNode> FindUserSelectAllNode(nsIDOMNode* aNode) { return nsnull; }

  NS_STACK_CLASS class HandlingTrustedAction
  {
  public:
    explicit HandlingTrustedAction(nsEditor* aSelf, bool aIsTrusted = true)
    {
      Init(aSelf, aIsTrusted);
    }

    HandlingTrustedAction(nsEditor* aSelf, nsIDOMNSEvent* aEvent);

    ~HandlingTrustedAction()
    {
      mEditor->mHandlingTrustedAction = mWasHandlingTrustedAction;
      mEditor->mHandlingActionCount--;
    }

  private:
    nsRefPtr<nsEditor> mEditor;
    bool mWasHandlingTrustedAction;

    void Init(nsEditor* aSelf, bool aIsTrusted)
    {
      MOZ_ASSERT(aSelf);

      mEditor = aSelf;
      mWasHandlingTrustedAction = aSelf->mHandlingTrustedAction;
      if (aIsTrusted) {
        // If action is nested and the outer event is not trusted,
        // we shouldn't override it.
        if (aSelf->mHandlingActionCount == 0) {
          aSelf->mHandlingTrustedAction = true;
        }
      } else {
        aSelf->mHandlingTrustedAction = false;
      }
      aSelf->mHandlingActionCount++;
    }
  };

protected:
  enum Tristate {
    eTriUnset,
    eTriFalse,
    eTriTrue
  };
  // Spellchecking
  nsCString mContentMIMEType;       // MIME type of the doc we are editing.

  nsCOMPtr<nsIInlineSpellChecker> mInlineSpellChecker;

  nsRefPtr<nsTransactionManager> mTxnMgr;
  nsCOMPtr<mozilla::dom::Element> mRootElement; // cached root node
  nsCOMPtr<nsIPrivateTextRangeList> mIMETextRangeList; // IME special selection ranges
  nsCOMPtr<nsIDOMCharacterData>     mIMETextNode;      // current IME text node
  nsCOMPtr<nsIDOMEventTarget> mEventTarget; // The form field as an event receiver
  nsCOMPtr<nsIDOMEventListener> mEventListener;
  nsWeakPtr        mSelConWeak;          // weak reference to the nsISelectionController
  nsWeakPtr        mPlaceHolderTxn;      // weak reference to placeholder for begin/end batch purposes
  nsWeakPtr        mDocWeak;             // weak reference to the nsIDOMDocument
  nsIAtom          *mPlaceHolderName;    // name of placeholder transaction
  nsSelectionState *mSelState;           // saved selection state for placeholder txn batching
  nsString         *mPhonetic;

  // various listeners
  nsCOMArray<nsIEditActionListener> mActionListeners;  // listens to all low level actions on the doc
  nsCOMArray<nsIEditorObserver> mEditorObservers;  // just notify once per high level change
  nsCOMArray<nsIDocumentStateListener> mDocStateListeners;// listen to overall doc state (dirty or not, just created, etc)

  nsSelectionState  mSavedSel;           // cached selection for nsAutoSelectionReset
  nsRangeUpdater    mRangeUpdater;       // utility class object for maintaining preserved ranges

  PRUint32          mModCount;     // number of modifications (for undo/redo stack)
  PRUint32          mFlags;        // behavior flags. See nsIPlaintextEditor.idl for the flags we use.

  PRInt32           mUpdateCount;

  PRInt32           mPlaceHolderBatch;   // nesting count for batching
  OperationID       mAction;             // the current editor action
  PRUint32          mHandlingActionCount;

  PRUint32          mIMETextOffset;    // offset in text node where IME comp string begins
  PRUint32          mIMEBufferLength;  // current length of IME comp string

  EDirection        mDirection;          // the current direction of editor action
  PRInt8            mDocDirtyState;      // -1 = not initialized
  PRUint8           mSpellcheckCheckboxState; // a Tristate value

  bool mInIMEMode;        // are we inside an IME composition?
  bool mIsIMEComposing;   // is IME in composition state?
                                                       // This is different from mInIMEMode. see Bug 98434.

  bool mShouldTxnSetSelection;  // turn off for conservative selection adjustment by txns
  bool mDidPreDestroy;    // whether PreDestroy has been called
  bool mDidPostCreate;    // whether PostCreate has been called
  bool mHandlingTrustedAction;
  bool mDispatchInputEvent;

  friend bool NSCanUnload(nsISupports* serviceMgr);
  friend class nsAutoTxnsConserveSelection;
  friend class nsAutoSelectionReset;
  friend class nsAutoRules;
  friend class nsRangeUpdater;
};


#endif
