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
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Josh Aas <josh@mozilla.com>
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

#ifndef nsMenuBaseX_h_
#define nsMenuBaseX_h_

#import <Foundation/Foundation.h>

#include "nsCOMPtr.h"
#include "nsIContent.h"

enum nsMenuObjectTypeX {
  eMenuBarObjectType,
  eSubmenuObjectType,
  eMenuItemObjectType,
  eStandaloneNativeMenuObjectType,
};

// All menu objects subclass this.
// Menu bars are owned by their top-level nsIWidgets.
// All other objects are memory-managed based on the DOM.
// Content removal deletes them immediately and nothing else should.
// Do not attempt to hold strong references to them or delete them.
class nsMenuObjectX
{
public:
  virtual ~nsMenuObjectX() { }
  virtual nsMenuObjectTypeX MenuObjectType()=0;
  virtual void*             NativeData()=0;
  nsIContent*               Content() { return mContent; }

protected:
  nsCOMPtr<nsIContent> mContent;
};


//
// Object stored as "representedObject" for all menu items
//

class nsMenuGroupOwnerX;

@interface MenuItemInfo : NSObject
{
  nsMenuGroupOwnerX * mMenuGroupOwner;
}

- (id) initWithMenuGroupOwner:(nsMenuGroupOwnerX *)aMenuGroupOwner;
- (nsMenuGroupOwnerX *) menuGroupOwner;
- (void) setMenuGroupOwner:(nsMenuGroupOwnerX *)aMenuGroupOwner;

@end


// Special command IDs that we know Mac OS X does not use for anything else.
// We use these in place of carbon's IDs for these commands in order to stop
// Carbon from messing with our event handlers. See bug 346883.

enum {
  eCommand_ID_About      = 1,
  eCommand_ID_Prefs      = 2,
  eCommand_ID_Quit       = 3,
  eCommand_ID_HideApp    = 4,
  eCommand_ID_HideOthers = 5,
  eCommand_ID_ShowAll    = 6,
  eCommand_ID_Last       = 7
};

#endif // nsMenuBaseX_h_
