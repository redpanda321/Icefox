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
 * The Original Code is HTML Parser C++ Translator code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Henri Sivonen <hsivonen@iki.fi>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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
 
#ifndef nsHtml5TreeOperation_h__
#define nsHtml5TreeOperation_h__

#include "nsHtml5DocumentMode.h"
#include "nsHtml5HtmlAttributes.h"
#include "nsXPCOMStrings.h"

class nsIContent;
class nsHtml5TreeOpExecutor;
class nsHtml5StateSnapshot;

enum eHtml5TreeOperation {
#ifdef DEBUG
  eTreeOpUninitialized,
#endif
  // main HTML5 ops
  eTreeOpAppend,
  eTreeOpDetach,
  eTreeOpAppendChildrenToNewParent,
  eTreeOpFosterParent,
  eTreeOpAppendToDocument,
  eTreeOpAddAttributes,
  eTreeOpDocumentMode,
  eTreeOpCreateElementNetwork,
  eTreeOpCreateElementNotNetwork,
  eTreeOpSetFormElement,
  eTreeOpAppendText,
  eTreeOpAppendIsindexPrompt,
  eTreeOpFosterParentText,
  eTreeOpAppendComment,
  eTreeOpAppendCommentToDocument,
  eTreeOpAppendDoctypeToDocument,
  // Gecko-specific on-pop ops
  eTreeOpRunScript,
  eTreeOpRunScriptAsyncDefer,
  eTreeOpDoneAddingChildren,
  eTreeOpDoneCreatingElement,
  eTreeOpFlushPendingAppendNotifications,
  eTreeOpSetDocumentCharset,
  eTreeOpNeedsCharsetSwitchTo,
  eTreeOpUpdateStyleSheet,
  eTreeOpProcessMeta,
  eTreeOpProcessOfflineManifest,
  eTreeOpMarkMalformedIfScript,
  eTreeOpStreamEnded,
  eTreeOpSetStyleLineNumber,
  eTreeOpSetScriptLineNumberAndFreeze,
#ifdef MOZ_SVG
  eTreeOpSvgLoad,
#endif
  eTreeOpStartLayout
};

class nsHtml5TreeOperationStringPair {
  private:
    nsString mPublicId;
    nsString mSystemId;
  public:
    nsHtml5TreeOperationStringPair(const nsAString& aPublicId, 
                                   const nsAString& aSystemId)
      : mPublicId(aPublicId)
      , mSystemId(aSystemId) {
      MOZ_COUNT_CTOR(nsHtml5TreeOperationStringPair);
    }
    
    ~nsHtml5TreeOperationStringPair() {
      MOZ_COUNT_DTOR(nsHtml5TreeOperationStringPair);    
    }
    
    inline void Get(nsAString& aPublicId, nsAString& aSystemId) {
      aPublicId.Assign(mPublicId);
      aSystemId.Assign(mSystemId);
    }
};

class nsHtml5TreeOperation {

  public:
    nsHtml5TreeOperation();

    ~nsHtml5TreeOperation();

    inline void Init(eHtml5TreeOperation aOpCode) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      mOpCode = aOpCode;
    }

    inline void Init(eHtml5TreeOperation aOpCode, nsIContent** aNode) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      NS_PRECONDITION(aNode, "Initialized tree op with null node.");
      mOpCode = aOpCode;
      mOne.node = aNode;
    }

    inline void Init(eHtml5TreeOperation aOpCode, 
                     nsIContent** aNode,
                     nsIContent** aParent) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      NS_PRECONDITION(aNode, "Initialized tree op with null node.");
      NS_PRECONDITION(aParent, "Initialized tree op with null parent.");
      mOpCode = aOpCode;
      mOne.node = aNode;
      mTwo.node = aParent;
    }
    
    inline void Init(eHtml5TreeOperation aOpCode, 
                     const nsACString& aString,
                     PRInt32 aInt32) {
      Init(aOpCode, aString);
      mInt = aInt32;
    }

    inline void Init(eHtml5TreeOperation aOpCode,
                     nsIContent** aNode,
                     nsIContent** aParent, 
                     nsIContent** aTable) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      NS_PRECONDITION(aNode, "Initialized tree op with null node.");
      NS_PRECONDITION(aParent, "Initialized tree op with null parent.");
      NS_PRECONDITION(aTable, "Initialized tree op with null table.");
      mOpCode = aOpCode;
      mOne.node = aNode;
      mTwo.node = aParent;
      mThree.node = aTable;
    }

    inline void Init(nsHtml5DocumentMode aMode) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      mOpCode = eTreeOpDocumentMode;
      mOne.mode = aMode;
    }
    
    inline void InitScript(nsIContent** aNode) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      NS_PRECONDITION(aNode, "Initialized tree op with null node.");
      mOpCode = eTreeOpRunScript;
      mOne.node = aNode;
      mTwo.state = nsnull;
    }
    
    inline void Init(PRInt32 aNamespace, 
                     nsIAtom* aName, 
                     nsHtml5HtmlAttributes* aAttributes,
                     nsIContent** aTarget,
                     PRBool aFromNetwork) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      NS_PRECONDITION(aName, "Initialized tree op with null name.");
      NS_PRECONDITION(aTarget, "Initialized tree op with null target node.");
      mOpCode = aFromNetwork ?
                eTreeOpCreateElementNetwork :
                eTreeOpCreateElementNotNetwork;
      mInt = aNamespace;
      mOne.node = aTarget;
      mTwo.atom = aName;
      if (aAttributes == nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES) {
        mThree.attributes = nsnull;
      } else {
        mThree.attributes = aAttributes;
      }
    }

    inline void Init(eHtml5TreeOperation aOpCode, 
                     PRUnichar* aBuffer, 
                     PRInt32 aLength, 
                     nsIContent** aStackParent,
                     nsIContent** aTable) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      NS_PRECONDITION(aBuffer, "Initialized tree op with null buffer.");
      mOpCode = aOpCode;
      mOne.node = aStackParent;
      mTwo.unicharPtr = aBuffer;
      mThree.node = aTable;
      mInt = aLength;
    }

    inline void Init(eHtml5TreeOperation aOpCode, 
                     PRUnichar* aBuffer, 
                     PRInt32 aLength, 
                     nsIContent** aParent) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      NS_PRECONDITION(aBuffer, "Initialized tree op with null buffer.");
      mOpCode = aOpCode;
      mOne.node = aParent;
      mTwo.unicharPtr = aBuffer;
      mInt = aLength;
    }

    inline void Init(eHtml5TreeOperation aOpCode, 
                     PRUnichar* aBuffer, 
                     PRInt32 aLength) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      NS_PRECONDITION(aBuffer, "Initialized tree op with null buffer.");
      mOpCode = aOpCode;
      mTwo.unicharPtr = aBuffer;
      mInt = aLength;
    }
    
    inline void Init(nsIContent** aElement,
                     nsHtml5HtmlAttributes* aAttributes) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      NS_PRECONDITION(aElement, "Initialized tree op with null element.");
      mOpCode = eTreeOpAddAttributes;
      mOne.node = aElement;
      mTwo.attributes = aAttributes;
    }
    
    inline void Init(nsIAtom* aName, 
                     const nsAString& aPublicId, 
                     const nsAString& aSystemId) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      mOpCode = eTreeOpAppendDoctypeToDocument;
      mOne.atom = aName;
      mTwo.stringPair = new nsHtml5TreeOperationStringPair(aPublicId, aSystemId);
    }
    
    inline void Init(eHtml5TreeOperation aOpCode, const nsACString& aString) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");

      PRInt32 len = aString.Length();
      char* str = new char[len + 1];
      const char* start = aString.BeginReading();
      for (PRInt32 i = 0; i < len; ++i) {
        str[i] = start[i];
      }
      str[len] = '\0';

      mOpCode = aOpCode;
      mOne.charPtr = str;
    }

    inline void Init(eHtml5TreeOperation aOpCode, const nsAString& aString) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");

      PRUnichar* str = NS_StringCloneData(aString);
      mOpCode = aOpCode;
      mOne.unicharPtr = str;
    }
    
    inline void Init(eHtml5TreeOperation aOpCode,
                     nsIContent** aNode,
                     PRInt32 aInt) {
      NS_PRECONDITION(mOpCode == eTreeOpUninitialized,
        "Op code must be uninitialized when initializing.");
      NS_PRECONDITION(aNode, "Initialized tree op with null node.");
      mOpCode = aOpCode;
      mOne.node = aNode;
      mInt = aInt;
    }

    inline PRBool IsRunScript() {
      return mOpCode == eTreeOpRunScript;
    }
    
    inline void SetSnapshot(nsAHtml5TreeBuilderState* aSnapshot, PRInt32 aLine) {
      NS_ASSERTION(IsRunScript(), 
        "Setting a snapshot for a tree operation other than eTreeOpRunScript!");
      NS_PRECONDITION(aSnapshot, "Initialized tree op with null snapshot.");
      mTwo.state = aSnapshot;
      mInt = aLine;
    }

    nsresult Perform(nsHtml5TreeOpExecutor* aBuilder, nsIContent** aScriptElement);

    inline already_AddRefed<nsIAtom> Reget(nsIAtom* aAtom) {
      if (!aAtom || aAtom->IsStaticAtom()) {
        return aAtom;
      }
      nsAutoString str;
      aAtom->ToString(str);
      return do_GetAtom(str);
    }

  private:

    nsresult AppendTextToTextNode(const PRUnichar* aBuffer,
                                  PRInt32 aLength,
                                  nsIContent* aTextNode,
                                  nsHtml5TreeOpExecutor* aBuilder);

    nsresult AppendText(const PRUnichar* aBuffer,
                        PRInt32 aLength,
                        nsIContent* aParent,
                        nsHtml5TreeOpExecutor* aBuilder);

    nsresult Append(nsIContent* aNode,
                    nsIContent* aParent,
                    nsHtml5TreeOpExecutor* aBuilder);

    nsresult AppendToDocument(nsIContent* aNode,
                              nsHtml5TreeOpExecutor* aBuilder);
  
    // possible optimization:
    // Make the queue take items the size of pointer and make the op code
    // decide how many operands it dequeues after it.
    eHtml5TreeOperation mOpCode;
    union {
      nsIContent**                    node;
      nsIAtom*                        atom;
      nsHtml5HtmlAttributes*          attributes;
      nsHtml5DocumentMode             mode;
      PRUnichar*                      unicharPtr;
      char*                           charPtr;
      nsHtml5TreeOperationStringPair* stringPair;
      nsAHtml5TreeBuilderState*       state;
    }                   mOne, mTwo, mThree;
    PRInt32             mInt; // optimize this away later by using an end 
                              // pointer instead of string length and distinct
                              // element creation opcodes for HTML, MathML and
                              // SVG.
};

#endif // nsHtml5TreeOperation_h__
