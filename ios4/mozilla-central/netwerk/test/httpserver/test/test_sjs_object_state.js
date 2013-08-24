/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http: *www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is httpd.js code.
 *
 * The Initial Developer of the Original Code is
 * the Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Jeff Walden <jwalden+code@mit.edu>
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

/*
 * Tests that the object-state-preservation mechanism works correctly.
 */

const PORT = 4444;

const PATH = "http://localhost:" + PORT + "/object-state.sjs";

var srv;

function run_test()
{
  srv = createServer();
  var sjsDir = do_get_file("data/sjs/");
  srv.registerDirectory("/", sjsDir);
  srv.registerContentType("sjs", "sjs");
  srv.start(PORT);

  do_test_pending();

  new HTTPTestLoader(PATH + "?state=initial", initialStart, initialStop);
}

/********************
 * OBSERVER METHODS *
 ********************/

/*
 * In practice the current implementation will guarantee an exact ordering of
 * all start and stop callbacks.  However, in the interests of robustness, this
 * test will pass given any valid ordering of callbacks.  Any ordering of calls
 * which obeys the partial ordering shown by this directed acyclic graph will be
 * handled correctly:
 *
 *    initialStart
 *         |
 *         V
 *  intermediateStart
 *         |
 *         V
 *   intermediateStop
 *         |        \
 *         |         V
 *         |      initialStop
 *         V
 *     triggerStart
 *         |
 *         V
 *     triggerStop
 *
 */

var initialStarted = false;
function initialStart(ch, cx)
{
  dumpn("*** initialStart");

  if (initialStarted)
    do_throw("initialStart: initialStarted is true?!?!");

  initialStarted = true;

  new HTTPTestLoader(PATH + "?state=intermediate",
                     intermediateStart, intermediateStop);
}

var initialStopped = false;
function initialStop(ch, cx, status, data)
{
  dumpn("*** initialStop");

  do_check_eq(data.map(function(v) { return String.fromCharCode(v); }).join(""),
              "done");

  do_check_eq(srv.getObjectState("object-state-test"), null);

  if (!initialStarted)
    do_throw("initialStop: initialStarted is false?!?!");
  if (initialStopped)
    do_throw("initialStop: initialStopped is true?!?!");
  if (!intermediateStarted)
    do_throw("initialStop: intermediateStarted is false?!?!");
  if (!intermediateStopped)
    do_throw("initialStop: intermediateStopped is false?!?!");

  initialStopped = true;

  checkForFinish();
}

var intermediateStarted = false;
function intermediateStart(ch, cx)
{
  dumpn("*** intermediateStart");

  do_check_neq(srv.getObjectState("object-state-test"), null);

  if (!initialStarted)
    do_throw("intermediateStart: initialStarted is false?!?!");
  if (intermediateStarted)
    do_throw("intermediateStart: intermediateStarted is true?!?!");

  intermediateStarted = true;
}

var intermediateStopped = false;
function intermediateStop(ch, cx, status, data)
{
  dumpn("*** intermediateStop");

  do_check_eq(data.map(function(v) { return String.fromCharCode(v); }).join(""),
              "intermediate");

  do_check_neq(srv.getObjectState("object-state-test"), null);

  if (!initialStarted)
    do_throw("intermediateStop: initialStarted is false?!?!");
  if (!intermediateStarted)
    do_throw("intermediateStop: intermediateStarted is false?!?!");
  if (intermediateStopped)
    do_throw("intermediateStop: intermediateStopped is true?!?!");

  intermediateStopped = true;

  new HTTPTestLoader(PATH + "?state=trigger", triggerStart,
                     triggerStop);
}

var triggerStarted = false;
function triggerStart(ch, cx)
{
  dumpn("*** triggerStart");

  if (!initialStarted)
    do_throw("triggerStart: initialStarted is false?!?!");
  if (!intermediateStarted)
    do_throw("triggerStart: intermediateStarted is false?!?!");
  if (!intermediateStopped)
    do_throw("triggerStart: intermediateStopped is false?!?!");
  if (triggerStarted)
    do_throw("triggerStart: triggerStarted is true?!?!");

  triggerStarted = true;
}

var triggerStopped = false;
function triggerStop(ch, cx, status, data)
{
  dumpn("*** triggerStop");

  do_check_eq(data.map(function(v) { return String.fromCharCode(v); }).join(""),
              "trigger");

  if (!initialStarted)
    do_throw("triggerStop: initialStarted is false?!?!");
  if (!intermediateStarted)
    do_throw("triggerStop: intermediateStarted is false?!?!");
  if (!intermediateStopped)
    do_throw("triggerStop: intermediateStopped is false?!?!");
  if (!triggerStarted)
    do_throw("triggerStop: triggerStarted is false?!?!");
  if (triggerStopped)
    do_throw("triggerStop: triggerStopped is false?!?!");

  triggerStopped = true;

  checkForFinish();
}

var finished = false;
function checkForFinish()
{
  if (finished)
  {
    try
    {
      do_throw("uh-oh, how are we being finished twice?!?!");
    }
    finally
    {
      quit(1);
    }
  }

  if (triggerStopped && initialStopped)
  {
    finished = true;
    try
    {
      do_check_eq(srv.getObjectState("object-state-test"), null);

      if (!initialStarted)
        do_throw("checkForFinish: initialStarted is false?!?!");
      if (!intermediateStarted)
        do_throw("checkForFinish: intermediateStarted is false?!?!");
      if (!intermediateStopped)
        do_throw("checkForFinish: intermediateStopped is false?!?!");
      if (!triggerStarted)
        do_throw("checkForFinish: triggerStarted is false?!?!");
    }
    finally
    {
      srv.stop(do_test_finished);
    }
  }
}


/*********************************
 * UTILITY OBSERVABLE URL LOADER *
 *********************************/

/** Stream listener for the channels. */
function HTTPTestLoader(path, start, stop)
{
  /** Path to load. */
  this._path = path;

  /** Array of bytes of data in body of response. */
  this._data = [];

  /** onStartRequest callback. */
  this._start = start;

  /** onStopRequest callback. */
  this._stop = stop;

  var channel = makeChannel(path);
  channel.asyncOpen(this, null);
}
HTTPTestLoader.prototype =
  {
    onStartRequest: function(request, cx)
    {
      dumpn("*** HTTPTestLoader.onStartRequest for " + this._path);

      var ch = request.QueryInterface(Ci.nsIHttpChannel)
                      .QueryInterface(Ci.nsIHttpChannelInternal);

      try
      {
        try
        {
          this._start(ch, cx);
        }
        catch (e)
        {
          do_throw(this._path + ": error in onStartRequest: " + e);
        }
      }
      catch (e)
      {
        dumpn("!!! swallowing onStartRequest exception so onStopRequest is " +
              "called...");
      }
    },
    onDataAvailable: function(request, cx, inputStream, offset, count)
    {
      dumpn("*** HTTPTestLoader.onDataAvailable for " + this._path);

      Array.prototype.push.apply(this._data,
                                 makeBIS(inputStream).readByteArray(count));
    },
    onStopRequest: function(request, cx, status)
    {
      dumpn("*** HTTPTestLoader.onStopRequest for " + this._path);

      var ch = request.QueryInterface(Ci.nsIHttpChannel)
                      .QueryInterface(Ci.nsIHttpChannelInternal);

      this._stop(ch, cx, status, this._data);
    },
    QueryInterface: function(aIID)
    {
      dumpn("*** QueryInterface: " + aIID);

      if (aIID.equals(Ci.nsIStreamListener) ||
          aIID.equals(Ci.nsIRequestObserver) ||
          aIID.equals(Ci.nsISupports))
        return this;
      throw Cr.NS_ERROR_NO_INTERFACE;
    }
  };
