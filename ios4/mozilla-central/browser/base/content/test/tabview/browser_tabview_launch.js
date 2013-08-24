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
 * The Original Code is tabview launch test.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * Raymond Lee <raymond@appcoast.com>
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

function test() {
  waitForExplicitFinish();
  
  let tabViewShownCount = 0;
  let onTabViewHidden = function() {
    ok(!TabView.isVisible(), "Tab View is hidden");

    if (tabViewShownCount == 1) {
      document.getElementById("menu_tabview").doCommand();
    } else if (tabViewShownCount == 2) {
       var utils = window.QueryInterface(Components.interfaces.nsIInterfaceRequestor).
                        getInterface(Components.interfaces.nsIDOMWindowUtils);
      if (utils) {
        var keyCode = 0;
        var charCode;
        var eventObject;
        if (navigator.platform.indexOf("Mac") != -1) {
          charCode = 160;
          eventObject = { altKey: true };
        } else {
          charCode = 32;
          eventObject = { ctrlKey: true };
        }
        var modifiers = EventUtils._parseModifiers(eventObject);
        var keyDownDefaultHappened =
            utils.sendKeyEvent("keydown", keyCode, charCode, modifiers);
        utils.sendKeyEvent("keypress", keyCode, charCode, modifiers,
                             !keyDownDefaultHappened);
        utils.sendKeyEvent("keyup", keyCode, charCode, modifiers);
      }
    } else if (tabViewShownCount == 3) {
      window.removeEventListener("tabviewshown", onTabViewShown, false);
      window.removeEventListener("tabviewhidden", onTabViewHidden, false);
      finish();
    }
  }
  let onTabViewShown = function() {
    ok(TabView.isVisible(), "Tab View is visible");
    tabViewShownCount++
    TabView.toggle();
  }
  window.addEventListener("tabviewshown", onTabViewShown, false);
  window.addEventListener("tabviewhidden", onTabViewHidden, false);
  
  ok(!TabView.isVisible(), "Tab View is hidden");

  let button = document.getElementById("tabview-button");
  ok(button, "Tab View button exists");
  EventUtils.synthesizeMouse(button, 1, 1, {});
}
