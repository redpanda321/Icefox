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
 * The Original Code is Storage Test Code.
 *
 * The Initial Developer of the Original Code is
 *   Stefan Sitter.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * This code is based off of like.test from the sqlite code
 *
 * Contributor(s):
 *   Stefan Sitter <ssitter@gmail.com> (Original Author)
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

function createStatementWrapper(aSQL) 
{
    var stmt = getOpenedDatabase().createStatement(aSQL);
    var wrapper = Components.Constructor("@mozilla.org/storage/statement-wrapper;1", Ci.mozIStorageStatementWrapper)();
    wrapper.initialize(stmt);
    return wrapper;
}

function setup() 
{
    getOpenedDatabase().createTable("t1", "x TEXT");

    var stmt = getOpenedDatabase().createStatement("INSERT INTO t1 (x) VALUES ('/mozilla.org/20070129_1/Europe/Berlin')");
    stmt.execute();
    stmt.finalize();
}

function test_bug429521() 
{
    var wrapper = createStatementWrapper(
        "SELECT DISTINCT(zone) FROM ("+
            "SELECT x AS zone FROM t1 WHERE x LIKE '/mozilla.org%'" +
        ");");

    print("*** test_bug429521: started");

    try {
        while (wrapper.step()) {
            print("*** test_bug429521: step() Read wrapper.row.zone");

            // BUG: the print commands after the following statement
            // are never executed. Script stops immediately.
            var tzId = wrapper.row.zone;

            print("*** test_bug429521: step() Read wrapper.row.zone finished");
        }
    } catch (e) {
        print("*** test_bug429521: " + e);
    }

    print("*** test_bug429521: finished");

    wrapper.statement.finalize();
}

function run_test()
{
  setup();

  test_bug429521();
    
  cleanup();
}
