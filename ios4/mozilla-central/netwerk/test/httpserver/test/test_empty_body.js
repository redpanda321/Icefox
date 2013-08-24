/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
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
 * The Original Code is httpd.js code.
 *
 * The Initial Developer of the Original Code is
 * Jeff Walden <jwalden+code@mit.edu>.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

// in its original incarnation, the server didn't like empty response-bodies;
// see the comment in _end for details

var tests =
  [
   new Test("http://localhost:4444/empty-body-unwritten",
            null, ensureEmpty, null),
   new Test("http://localhost:4444/empty-body-written",
            null, ensureEmpty, null),
  ];

function run_test()
{
  var srv = createServer();

  // register a few test paths
  srv.registerPathHandler("/empty-body-unwritten", emptyBodyUnwritten);
  srv.registerPathHandler("/empty-body-written", emptyBodyWritten);

  srv.start(4444);

  runHttpTests(tests, testComplete(srv));
}

// TEST DATA

function ensureEmpty(ch, cx)
{
  do_check_true(ch.contentLength == 0);
}

// PATH HANDLERS

// /empty-body-unwritten
function emptyBodyUnwritten(metadata, response)
{
  response.setStatusLine("1.1", 200, "OK");
}

// /empty-body-written
function emptyBodyWritten(metadata, response)
{
  response.setStatusLine("1.1", 200, "OK");
  var body = "";
  response.bodyOutputStream.write(body, body.length);
}
