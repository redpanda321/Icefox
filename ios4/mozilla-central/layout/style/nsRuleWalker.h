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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Original Author: David W. Hyatt (hyatt@netscape.com)
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
 * a class that walks the lexicographic tree of rule nodes as style
 * rules are matched
 */

#ifndef nsRuleWalker_h_
#define nsRuleWalker_h_

#include "nsRuleNode.h"
#include "nsIStyleRule.h"

class nsRuleWalker {
public:
  nsRuleNode* CurrentNode() { return mCurrent; }
  void SetCurrentNode(nsRuleNode* aNode) {
    NS_ASSERTION(aNode, "Must have node here!");
    mCurrent = aNode;
  }

  void Forward(nsIStyleRule* aRule) { 
    mCurrent = mCurrent->Transition(aRule, mLevel, mImportance);
    mCheckForImportantRules =
      mCheckForImportantRules && !aRule->GetImportantRule();
    NS_POSTCONDITION(mCurrent, "Transition messed up");
  }

  void Reset() { mCurrent = mRoot; }

  PRBool AtRoot() { return mCurrent == mRoot; }

  void SetLevel(PRUint8 aLevel, PRBool aImportance,
                PRBool aCheckForImportantRules) {
    NS_ASSERTION(!aCheckForImportantRules || !aImportance,
                 "Shouldn't be checking for important rules while walking "
                 "important rules");
    mLevel = aLevel;
    mImportance = aImportance;
    mCheckForImportantRules = aCheckForImportantRules;
  }
  PRUint8 GetLevel() const { return mLevel; }
  PRBool GetImportance() const { return mImportance; }
  PRBool GetCheckForImportantRules() const { return mCheckForImportantRules; }

  // We define the visited-relevant link to be the link that is the
  // nearest self-or-ancestor to the node being matched.
  enum VisitedHandlingType {
    // Do rule matching as though all links are unvisited.
    eRelevantLinkUnvisited,
    // Do rule matching as though the relevant link is visited and all
    // other links are unvisited.
    eRelevantLinkVisited,
    // Do rule matching as though a rule should match if it would match
    // given any set of visitedness states.  (used by users other than
    // nsRuleWalker)
    eLinksVisitedOrUnvisited
  };

  void ResetForVisitedMatching() {
    Reset();
    mVisitedHandling = eRelevantLinkVisited;
  }
  VisitedHandlingType VisitedHandling() const { return mVisitedHandling; }
  void SetHaveRelevantLink() { mHaveRelevantLink = PR_TRUE; }
  PRBool HaveRelevantLink() const { return mHaveRelevantLink; }

private:
  nsRuleNode* mCurrent; // Our current position.  Never null.
  nsRuleNode* mRoot; // The root of the tree we're walking.
  PRUint8 mLevel; // an nsStyleSet::sheetType
  PRPackedBool mImportance;
  PRPackedBool mCheckForImportantRules; // If true, check for important rules as
                                        // we walk and set to false if we find
                                        // one.

  // When mVisitedHandling is eRelevantLinkUnvisited, this is set to
  // true on the RuleProcessorData *for the node being matched* if a
  // relevant link (see explanation in definition of VisitedHandling
  // enum) was encountered during the matching process, which means that
  // matching needs to be rerun with eRelevantLinkVisited.  Otherwise,
  // its behavior is undefined (it might get set appropriately, or might
  // not).
  PRBool mHaveRelevantLink;

  VisitedHandlingType mVisitedHandling;

public:
  nsRuleWalker(nsRuleNode* aRoot)
    : mCurrent(aRoot)
    , mRoot(aRoot)
    , mHaveRelevantLink(PR_FALSE)
    , mVisitedHandling(eRelevantLinkUnvisited)
  {
    NS_ASSERTION(mCurrent, "Caller screwed up and gave us null node");
    MOZ_COUNT_CTOR(nsRuleWalker);
  }
  ~nsRuleWalker() { MOZ_COUNT_DTOR(nsRuleWalker); }
};

#endif /* !defined(nsRuleWalker_h_) */
