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
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *
 * Original nsWindow.h Contributor(s):
 *   Robert O'Callahan <roc+moz@cs.cmu.edu>
 *   Dean Tessman <dean_tessman@hotmail.com>
 *   Makoto Kato  <m_kato@ga2.so-net.ne.jp>
 *   Dainis Jonitis <Dainis_Jonitis@swh-t.lv>
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *   Ningjie Chen <chenn@email.uc.edu>
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

#ifndef nsIMM32Handler_h__
#define nsIMM32Handler_h__

#include "nscore.h"
#include <windows.h>
#include "nsString.h"
#include "nsGUIEvent.h"
#include "nsTArray.h"

class nsIWidget;
class nsWindow;
struct nsIntRect;

#ifndef WINCE
#define ENABLE_IME_MOUSE_HANDLING 1
#endif // WINCE

#define NS_WM_IMEFIRST WM_IME_SETCONTEXT
#define NS_WM_IMELAST  WM_IME_KEYUP

class nsIMEContext
{
public:
  nsIMEContext(HWND aWnd) : mWnd(aWnd)
  {
    mIMC = ::ImmGetContext(mWnd);
  }

  ~nsIMEContext()
  {
    if (mIMC) {
      ::ImmReleaseContext(mWnd, mIMC);
      mIMC = nsnull;
    }
  }

  HIMC get() const
  {
    return mIMC;
  }

  PRBool IsValid() const
  {
    return !!mIMC;
  }

protected:
  nsIMEContext()
  {
    NS_ERROR("Don't create nsIMEContext without window handle");
  }

  nsIMEContext(const nsIMEContext &aSrc) : mWnd(nsnull), mIMC(nsnull)
  {
    NS_ERROR("Don't copy nsIMEContext");
  }

  HWND mWnd;
  HIMC mIMC;
};

class nsIMM32Handler
{
public:
  static void Initialize();
  static void Terminate();
  // The result of Process* method mean "The message was processed, don't
  // process the message in the caller (nsWindow)" when it's TRUE.  At that
  // time, aEatMessage means that the message should be passed to next WndProc
  // when it's FALSE, otherwise, the message should be eaten by us.  When the
  // result is FALSE, aEatMessage doesn't have any meaning.  Then, the caller
  // should continue to process the message.
  static PRBool ProcessMessage(nsWindow* aWindow, UINT msg,
                               WPARAM &wParam, LPARAM &lParam,
                               LRESULT *aRetValue, PRBool &aEatMessage);
  static PRBool IsComposing()
  {
    return IsComposingOnOurEditor() || IsComposingOnPlugin();
  }
  static PRBool IsComposingOn(nsWindow* aWindow)
  {
    return IsComposing() && IsComposingWindow(aWindow);
  }
  static PRBool IsStatusChanged() { return sIsStatusChanged; }

  static PRBool IsDoingKakuteiUndo(HWND aWnd);

  static void NotifyEndStatusChange() { sIsStatusChanged = PR_FALSE; }

  static PRBool CanOptimizeKeyAndIMEMessages(MSG *aNextKeyOrIMEMessage);

  // If aForce is TRUE, these methods doesn't check whether we have composition
  // or not.  If you don't set it to TRUE, these method doesn't commit/cancel
  // the composition on uexpected window.
  static void CommitComposition(nsWindow* aWindow, PRBool aForce = PR_FALSE);
  static void CancelComposition(nsWindow* aWindow, PRBool aForce = PR_FALSE);

protected:
  static void EnsureHandlerInstance();

  static PRBool IsComposingOnOurEditor();
  static PRBool IsComposingOnPlugin();
  static PRBool IsComposingWindow(nsWindow* aWindow);

  static PRBool ShouldDrawCompositionStringOurselves();
  static void InitKeyboardLayout(HKL aKeyboardLayout);
  static UINT GetKeyboardCodePage();

  /**
   * Checks whether the window is top level window of the composing window.
   * In this method, the top level window means in all windows, not only in all
   * OUR windows.  I.e., if the aWindow is embedded, this always returns FALSE.
   */
  static PRBool IsTopLevelWindowOfComposition(nsWindow* aWindow);

  static PRBool ProcessInputLangChangeMessage(nsWindow* aWindow,
                                              WPARAM wParam,
                                              LPARAM lParam,
                                              LRESULT *aRetValue,
                                              PRBool &aEatMessage);
  static PRBool ProcessMessageForPlugin(nsWindow* aWindow, UINT msg,
                                        WPARAM &wParam, LPARAM &lParam,
                                        LRESULT *aRetValue,
                                        PRBool &aEatMessage);

  nsIMM32Handler();
  ~nsIMM32Handler();

  // The result of following On*Event methods means "The message was processed,
  // don't process the message in the caller (nsWindow)".
#ifdef ENABLE_IME_MOUSE_HANDLING
  PRBool OnMouseEvent(nsWindow* aWindow, LPARAM lParam, int aAction);
#endif // ENABLE_IME_MOUSE_HANDLING
  static PRBool OnKeyDownEvent(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                               PRBool &aEatMessage);

  // The result of On* methods mean "eat this message" when it's TRUE.
  PRBool OnIMEStartComposition(nsWindow* aWindow);
  PRBool OnIMEStartCompositionOnPlugin(nsWindow* aWindow,
                                       WPARAM wParam, LPARAM lParam);
  PRBool OnIMEComposition(nsWindow* aWindow, WPARAM wParam, LPARAM lParam);
  PRBool OnIMECompositionOnPlugin(nsWindow* aWindow,
                                  WPARAM wParam, LPARAM lParam);
  PRBool OnIMEEndComposition(nsWindow* aWindow);
  PRBool OnIMEEndCompositionOnPlugin(nsWindow* aWindow,
                                     WPARAM wParam, LPARAM lParam);
  PRBool OnIMERequest(nsWindow* aWindow, WPARAM wParam, LPARAM lParam,
                      LRESULT *aResult);
  PRBool OnIMECharOnPlugin(nsWindow* aWindow, WPARAM wParam, LPARAM lParam);
  PRBool OnChar(nsWindow* aWindow, WPARAM wParam, LPARAM lParam);
  PRBool OnCharOnPlugin(nsWindow* aWindow, WPARAM wParam, LPARAM lParam);
  PRBool OnInputLangChange(nsWindow* aWindow, WPARAM wParam, LPARAM lParam);

  // These message handlers don't use instance members, we should not create
  // the instance by the messages.  So, they should be static.
  static PRBool OnIMEChar(nsWindow* aWindow, WPARAM wParam, LPARAM lParam);
  static PRBool OnIMESetContext(nsWindow* aWindow,
                                WPARAM wParam, LPARAM lParam,
                                LRESULT *aResult);
  static PRBool OnIMESetContextOnPlugin(nsWindow* aWindow,
                                        WPARAM wParam, LPARAM lParam,
                                        LRESULT *aResult);
  static PRBool OnIMECompositionFull(nsWindow* aWindow);
  static PRBool OnIMENotify(nsWindow* aWindow, WPARAM wParam, LPARAM lParam);
  static PRBool OnIMESelect(nsWindow* aWindow, WPARAM wParam, LPARAM lParam);

  // The result of Handle* method mean "Processed" when it's TRUE.
  void HandleStartComposition(nsWindow* aWindow,
                              const nsIMEContext &aIMEContext);
  PRBool HandleComposition(nsWindow* aWindow, const nsIMEContext &aIMEContext,
                           LPARAM lParam);
  void HandleEndComposition(nsWindow* aWindow);
  PRBool HandleReconvert(nsWindow* aWindow, LPARAM lParam, LRESULT *oResult);
  PRBool HandleQueryCharPosition(nsWindow* aWindow, LPARAM lParam,
                                 LRESULT *oResult);
  PRBool HandleDocumentFeed(nsWindow* aWindow, LPARAM lParam, LRESULT *oResult);

  /**
   *  When a window's IME context is activating but we have composition on
   *  another window, we should commit our composition because IME context is
   *  shared by all our windows (including plug-ins).
   *  @param aWindow is a new activated window.
   *  If aWindow is our composing window, this method does nothing.
   *  Otherwise, this commits the composition on the previous window.
   *  If this method did commit a composition, this returns TRUE.
   */
  PRBool CommitCompositionOnPreviousWindow(nsWindow* aWindow);

  /**
   *  ResolveIMECaretPos
   *  Convert the caret rect of a composition event to another widget's
   *  coordinate system.
   *
   *  @param aReferenceWidget The origin widget of aCursorRect.
   *                          Typically, this is mReferenceWidget of the
   *                          composing events. If the aCursorRect is in screen
   *                          coordinates, set nsnull.
   *  @param aCursorRect      The cursor rect.
   *  @param aNewOriginWidget aOutRect will be in this widget's coordinates. If
   *                          this is nsnull, aOutRect will be in screen
   *                          coordinates.
   *  @param aOutRect         The converted cursor rect.
   */
  void ResolveIMECaretPos(nsIWidget* aReferenceWidget,
                          nsIntRect& aCursorRect,
                          nsIWidget* aNewOriginWidget,
                          nsIntRect& aOutRect);

  PRBool ConvertToANSIString(const nsAFlatString& aStr,
                             UINT aCodePage,
                             nsACString& aANSIStr);

  PRBool SetIMERelatedWindowsPos(nsWindow* aWindow,
                                 const nsIMEContext &aIMEContext);
  PRBool GetCharacterRectOfSelectedTextAt(nsWindow* aWindow,
                                          PRUint32 aOffset,
                                          nsIntRect &aCharRect);
  PRBool GetCaretRect(nsWindow* aWindow, nsIntRect &aCaretRect);
  void GetCompositionString(const nsIMEContext &aIMEContext, DWORD aIndex);
  /**
   *  Get the current target clause of composition string.
   *  If there are one or more characters whose attribute is ATTR_TARGET_*,
   *  this returns the first character's offset and its length.
   *  Otherwise, e.g., the all characters are ATTR_INPUT, this returns
   *  the composition string range because the all is the current target.
   *
   *  aLength can be null (default), but aOffset must not be null.
   *
   *  The aOffset value is offset in the contents.  So, when you need offset
   *  in the composition string, you need to subtract mCompositionStart from it.
   */
  PRBool GetTargetClauseRange(PRUint32 *aOffset, PRUint32 *aLength = nsnull);
  void DispatchTextEvent(nsWindow* aWindow, const nsIMEContext &aIMEContext,
                         PRBool aCheckAttr = PR_TRUE);
  void SetTextRangeList(nsTArray<nsTextRange> &aTextRangeList);

  nsresult EnsureClauseArray(PRInt32 aCount);
  nsresult EnsureAttributeArray(PRInt32 aCount);

  /**
   * When WM_IME_CHAR is received and passed to DefWindowProc, we need to
   * record the messages.  In other words, we should record the messages
   * when we receive WM_IME_CHAR on windowless plug-in (if we have focus,
   * we always eat them).  When focus is moved from a windowless plug-in to
   * our window during composition, WM_IME_CHAR messages were received when
   * the plug-in has focus.  However, WM_CHAR messages are received after the
   * plug-in lost focus.  So, we need to ignore the WM_CHAR messages because
   * they make unexpected text input events on us.
   */
  nsTArray<MSG> mPassedIMEChar;

  PRBool IsIMECharRecordsEmpty()
  {
    return mPassedIMEChar.IsEmpty();
  }
  void ResetIMECharRecords()
  {
    mPassedIMEChar.Clear();
  }
  void DequeueIMECharRecords(WPARAM &wParam, LPARAM &lParam)
  {
    MSG msg = mPassedIMEChar.ElementAt(0);
    wParam = msg.wParam;
    lParam = msg.lParam;
    mPassedIMEChar.RemoveElementAt(0);
  }
  void EnqueueIMECharRecords(WPARAM wParam, LPARAM lParam)
  {
    MSG msg;
    msg.wParam = wParam;
    msg.lParam = lParam;
    mPassedIMEChar.AppendElement(msg);
  }

  nsWindow* mComposingWindow;
  nsString  mCompositionString;
  nsTArray<PRUint32> mClauseArray;
  nsTArray<PRUint8> mAttributeArray;

  PRInt32 mCursorPosition;
  PRUint32 mCompositionStart;

  PRPackedBool mIsComposing;
  PRPackedBool mIsComposingOnPlugin;
  PRPackedBool mNativeCaretIsCreated;

  static PRPackedBool sIsStatusChanged;
  static PRPackedBool sIsIME;
  static PRPackedBool sIsIMEOpening;

#ifndef WINCE
  static UINT sCodePage;
  static DWORD sIMEProperty;
#endif // #ifndef WINCE
};

#endif // nsIMM32Handler_h__
