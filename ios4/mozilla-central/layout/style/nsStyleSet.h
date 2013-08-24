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
 *   Daniel Glazman <glazman@netscape.com>
 *   Brian Ryner    <bryner@brianryner.com>
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation
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

/*
 * the container for the style sheets that apply to a presentation, and
 * the internal API that the style system exposes for creating (and
 * potentially re-creating) style contexts
 */

#ifndef nsStyleSet_h_
#define nsStyleSet_h_

#include "nsIStyleRuleProcessor.h"
#include "nsCSSStyleSheet.h"
#include "nsBindingManager.h"
#include "nsRuleNode.h"
#include "nsTArray.h"
#include "nsCOMArray.h"
#include "nsAutoPtr.h"
#include "nsIStyleRule.h"
#include "nsCSSPseudoElements.h"
#include "nsCSSAnonBoxes.h"

class nsIURI;
class nsCSSFontFaceRule;
class nsRuleWalker;
struct RuleProcessorData;

class nsEmptyStyleRule : public nsIStyleRule
{
  NS_DECL_ISUPPORTS
  virtual void MapRuleInfoInto(nsRuleData* aRuleData);
#ifdef DEBUG
  virtual void List(FILE* out = stdout, PRInt32 aIndent = 0) const;
#endif
};

// The style set object is created by the document viewer and ownership is
// then handed off to the PresShell.  Only the PresShell should delete a
// style set.

class nsStyleSet
{
 public:
  nsStyleSet();

  // Initialize the object.  You must check the return code and not use
  // the nsStyleSet if Init() fails.

  nsresult Init(nsPresContext *aPresContext);

  // For getting the cached default data in case we hit out-of-memory.
  // To be used only by nsRuleNode.
  nsCachedStyleData* DefaultStyleData() { return &mDefaultStyleData; }

  nsRuleNode* GetRuleTree() { return mRuleTree; }

  // enable / disable the Quirk style sheet
  void EnableQuirkStyleSheet(PRBool aEnable);

  // get a style context for a non-pseudo frame.
  already_AddRefed<nsStyleContext>
  ResolveStyleFor(mozilla::dom::Element* aElement,
                  nsStyleContext* aParentContext);

  // Get a style context (with the given parent) for the
  // sequence of style rules in the |aRules| array.
  already_AddRefed<nsStyleContext>
  ResolveStyleForRules(nsStyleContext* aParentContext,
                       const nsCOMArray<nsIStyleRule> &aRules);

  // Get a style context that represents aBaseContext, but as though
  // it additionally matched the rules in the aRules array (in that
  // order, as more specific than any other rules).
  already_AddRefed<nsStyleContext>
  ResolveStyleByAddingRules(nsStyleContext* aBaseContext,
                            const nsCOMArray<nsIStyleRule> &aRules);

  // Get a style context for a non-element (which no rules will match),
  // such as text nodes, placeholder frames, and the nsFirstLetterFrame
  // for everything after the first letter.
  //
  // Perhaps this should go away and we shouldn't even create style
  // contexts for such content nodes.  However, not doing any rule
  // matching for them is a first step.
  already_AddRefed<nsStyleContext>
  ResolveStyleForNonElement(nsStyleContext* aParentContext);

  // Get a style context for a pseudo-element.  aParentElement must be
  // non-null.  aPseudoID is the nsCSSPseudoElements::Type for the
  // pseudo-element.
  already_AddRefed<nsStyleContext>
  ResolvePseudoElementStyle(mozilla::dom::Element* aParentElement,
                            nsCSSPseudoElements::Type aType,
                            nsStyleContext* aParentContext);

  // This functions just like ResolvePseudoElementStyle except that it will
  // return nsnull if there are no explicit style rules for that
  // pseudo element.
  already_AddRefed<nsStyleContext>
  ProbePseudoElementStyle(mozilla::dom::Element* aParentElement,
                          nsCSSPseudoElements::Type aType,
                          nsStyleContext* aParentContext);
  
  // Get a style context for an anonymous box.  aPseudoTag is the
  // pseudo-tag to use and must be non-null.
  already_AddRefed<nsStyleContext>
  ResolveAnonymousBoxStyle(nsIAtom* aPseudoTag, nsStyleContext* aParentContext);

#ifdef MOZ_XUL
  // Get a style context for a XUL tree pseudo.  aPseudoTag is the
  // pseudo-tag to use and must be non-null.  aParentContent must be
  // non-null.  aComparator must be non-null.
  already_AddRefed<nsStyleContext>
  ResolveXULTreePseudoStyle(mozilla::dom::Element* aParentElement,
                            nsIAtom* aPseudoTag,
                            nsStyleContext* aParentContext,
                            nsICSSPseudoComparator* aComparator);
#endif

  // Append all the currently-active font face rules to aArray.  Return
  // true for success and false for failure.
  PRBool AppendFontFaceRules(nsPresContext* aPresContext,
                             nsTArray<nsFontFaceRuleContainer>& aArray);

  // Begin ignoring style context destruction, to avoid lots of unnecessary
  // work on document teardown.
  void BeginShutdown(nsPresContext* aPresContext);

  // Free all of the data associated with this style set.
  void Shutdown(nsPresContext* aPresContext);

  // Notification that a style context is being destroyed.
  void NotifyStyleContextDestroyed(nsPresContext* aPresContext,
                                   nsStyleContext* aStyleContext);

  // Get a new style context that lives in a different parent
  // The new context will be the same as the old if the new parent is the
  // same as the old parent.
  // aElement should be non-null if this is a style context for an
  // element or pseudo-element; in the latter case it should be the
  // real element the pseudo-element is for.
  already_AddRefed<nsStyleContext>
  ReparentStyleContext(nsStyleContext* aStyleContext,
                       nsStyleContext* aNewParentContext,
                       mozilla::dom::Element* aElement);

  // Test if style is dependent on a document state.
  PRBool HasDocumentStateDependentStyle(nsPresContext* aPresContext,
                                        nsIContent*    aContent,
                                        PRInt32        aStateMask);

  // Test if style is dependent on content state
  nsRestyleHint HasStateDependentStyle(nsPresContext* aPresContext,
                                       mozilla::dom::Element* aElement,
                                       PRInt32 aStateMask);

  // Test if style is dependent on the presence of an attribute.
  nsRestyleHint HasAttributeDependentStyle(nsPresContext* aPresContext,
                                           mozilla::dom::Element* aElement,
                                           nsIAtom*       aAttribute,
                                           PRInt32        aModType,
                                           PRBool         aAttrHasChanged);

  /*
   * Do any processing that needs to happen as a result of a change in
   * the characteristics of the medium, and return whether style rules
   * may have changed as a result.
   */
  PRBool MediumFeaturesChanged(nsPresContext* aPresContext);

  // APIs for registering objects that can supply additional
  // rules during processing.
  void SetBindingManager(nsBindingManager* aBindingManager)
  {
    mBindingManager = aBindingManager;
  }

  // The "origins" of the CSS cascade, from lowest precedence to
  // highest (for non-!important rules).
  enum sheetType {
    eAgentSheet, // CSS
    ePresHintSheet,
    eUserSheet, // CSS
    eHTMLPresHintSheet,
    eDocSheet, // CSS
    eStyleAttrSheet,
    eOverrideSheet, // CSS
    eTransitionSheet,
    eSheetTypeCount
    // be sure to keep the number of bits in |mDirty| below and in
    // NS_RULE_NODE_LEVEL_MASK updated when changing the number of sheet
    // types
  };

  // APIs to manipulate the style sheet lists.  The sheets in each
  // list are stored with the most significant sheet last.
  nsresult AppendStyleSheet(sheetType aType, nsIStyleSheet *aSheet);
  nsresult PrependStyleSheet(sheetType aType, nsIStyleSheet *aSheet);
  nsresult RemoveStyleSheet(sheetType aType, nsIStyleSheet *aSheet);
  nsresult ReplaceSheets(sheetType aType,
                         const nsCOMArray<nsIStyleSheet> &aNewSheets);

  //Enable/Disable entire author style level (Doc & PresHint levels)
  PRBool GetAuthorStyleDisabled();
  nsresult SetAuthorStyleDisabled(PRBool aStyleDisabled);

  PRInt32 SheetCount(sheetType aType) const {
    return mSheets[aType].Count();
  }

  nsIStyleSheet* StyleSheetAt(sheetType aType, PRInt32 aIndex) const {
    return mSheets[aType].ObjectAt(aIndex);
  }

  nsresult AddDocStyleSheet(nsIStyleSheet* aSheet, nsIDocument* aDocument);

  void     BeginUpdate();
  nsresult EndUpdate();

  // Methods for reconstructing the tree; BeginReconstruct basically moves the
  // old rule tree root and style context roots out of the way,
  // and EndReconstruct destroys the old rule tree when we're done
  nsresult BeginReconstruct();
  // Note: EndReconstruct should not be called if BeginReconstruct fails
  void EndReconstruct();

  // Let the style set know that a particular sheet is the quirks sheet.  This
  // sheet must already have been added to the UA sheets.  The pointer must not
  // be null.  This should only be called once for a given style set.
  void SetQuirkStyleSheet(nsIStyleSheet* aQuirkStyleSheet);

  // Return whether the rule tree has cached data such that we need to
  // do dynamic change handling for changes that change the results of
  // media queries or require rebuilding all style data.
  // We don't care whether we have cached rule processors or whether
  // they have cached rule cascades; getting the rule cascades again in
  // order to do rule matching will get the correct rule cascade.
  PRBool HasCachedStyleData() const {
    return (mRuleTree && mRuleTree->TreeHasCachedData()) || !mRoots.IsEmpty();
  }

  // Notify the style set that a rulenode is no longer in use, or was
  // just created and is not in use yet.
  void RuleNodeUnused() {
    ++mUnusedRuleNodeCount;
  }

  // Notify the style set that a rulenode that wasn't in use now is
  void RuleNodeInUse() {
    --mUnusedRuleNodeCount;
  }

  nsCSSStyleSheet::EnsureUniqueInnerResult EnsureUniqueInnerOnCSSSheets();

 private:
  // Not to be implemented
  nsStyleSet(const nsStyleSet& aCopy);
  nsStyleSet& operator=(const nsStyleSet& aCopy);

  // Returns false on out-of-memory.
  PRBool BuildDefaultStyleData(nsPresContext* aPresContext);

  // Run mark-and-sweep GC on mRuleTree and mOldRuleTrees, based on mRoots.
  void GCRuleTrees();

  // Update the rule processor list after a change to the style sheet list.
  nsresult GatherRuleProcessors(sheetType aType);

  void AddImportantRules(nsRuleNode* aCurrLevelNode,
                         nsRuleNode* aLastPrevLevelNode,
                         nsRuleWalker* aRuleWalker);

  // Move aRuleWalker forward by the appropriate rule if we need to add
  // a rule due to property restrictions on pseudo-elements.
  void WalkRestrictionRule(nsCSSPseudoElements::Type aPseudoType,
                           nsRuleWalker* aRuleWalker);

#ifdef DEBUG
  // Just like AddImportantRules except it doesn't actually add anything; it
  // just asserts that there are no important rules between aCurrLevelNode and
  // aLastPrevLevelNode.
  void AssertNoImportantRules(nsRuleNode* aCurrLevelNode,
                              nsRuleNode* aLastPrevLevelNode);
  
  // Just like AddImportantRules except it doesn't actually add anything; it
  // just asserts that there are no CSS rules between aCurrLevelNode and
  // aLastPrevLevelNode.  Mostly useful for the preshint levels.
  void AssertNoCSSRules(nsRuleNode* aCurrLevelNode,
                        nsRuleNode* aLastPrevLevelNode);
#endif
  
  // Enumerate the rules in a way that cares about the order of the
  // rules.
  // aContent is the node the rules are for.  It might be null.  aData
  // is the closure to pass to aCollectorFunc.  If aContent is not null,
  // aData must be a RuleProcessorData*
  void FileRules(nsIStyleRuleProcessor::EnumFunc aCollectorFunc,
                 void* aData, nsIContent* aContent, nsRuleWalker* aRuleWalker);

  // Enumerate all the rules in a way that doesn't care about the order
  // of the rules and break out if the enumeration is halted.
  void WalkRuleProcessors(nsIStyleRuleProcessor::EnumFunc aFunc,
                          RuleProcessorData* aData,
                          PRBool aWalkAllXBLStylesheets);

  already_AddRefed<nsStyleContext>
  GetContext(nsStyleContext* aParentContext,
             nsRuleNode* aRuleNode,
             nsRuleNode* aVisitedRuleNode,
             PRBool aIsLink,
             PRBool aIsVisitedLink,
             nsIAtom* aPseudoTag,
             nsCSSPseudoElements::Type aPseudoType);

  nsPresContext* PresContext() { return mRuleTree->GetPresContext(); }

  // The sheets in each array in mSheets are stored with the most significant
  // sheet last.
  nsCOMArray<nsIStyleSheet> mSheets[eSheetTypeCount];

  nsCOMPtr<nsIStyleRuleProcessor> mRuleProcessors[eSheetTypeCount];

  // cached instance for enabling/disabling
  nsCOMPtr<nsIStyleSheet> mQuirkStyleSheet;

  nsRefPtr<nsBindingManager> mBindingManager;

  // To be used only in case of emergency, such as being out of memory
  // or operating on a deleted rule node.  The latter should never
  // happen, of course.
  nsCachedStyleData mDefaultStyleData;

  nsRuleNode* mRuleTree; // This is the root of our rule tree.  It is a
                         // lexicographic tree of matched rules that style
                         // contexts use to look up properties.

  PRUint32 mUnusedRuleNodeCount; // used to batch rule node GC
  nsTArray<nsStyleContext*> mRoots; // style contexts with no parent

  // Empty style rules to force things that restrict which properties
  // apply into different branches of the rule tree.
  nsRefPtr<nsEmptyStyleRule> mFirstLineRule, mFirstLetterRule;

  PRUint16 mBatching;

  // Old rule trees, which should only be non-empty between
  // BeginReconstruct and EndReconstruct, but in case of bugs that cause
  // style contexts to exist too long, may last longer.
  nsTArray<nsRuleNode*> mOldRuleTrees;

  unsigned mInShutdown : 1;
  unsigned mAuthorStyleDisabled: 1;
  unsigned mInReconstruct : 1;
  unsigned mDirty : 8;  // one dirty bit is used per sheet type

};

#ifdef _IMPL_NS_LAYOUT
inline
void nsRuleNode::AddRef()
{
  if (mRefCnt++ == 0 && !IsRoot()) {
    mPresContext->StyleSet()->RuleNodeInUse();
  }
}

inline
void nsRuleNode::Release()
{
  if (--mRefCnt == 0 && !IsRoot()) {
    mPresContext->StyleSet()->RuleNodeUnused();
  }
}
#endif

#endif
