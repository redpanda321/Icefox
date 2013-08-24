/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Places code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Shawn Wilsher <me@shawnwilsher.com> (Original Author)
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

#ifndef mozilla_places_History_h_
#define mozilla_places_History_h_

#include "mozilla/IHistory.h"
#include "mozilla/dom/Link.h"
#include "nsTHashtable.h"
#include "nsString.h"
#include "nsURIHashKey.h"
#include "nsTArray.h"
#include "nsDeque.h"
#include "nsIObserver.h"

namespace mozilla {
namespace places {

#define NS_HISTORYSERVICE_CID \
  {0x0937a705, 0x91a6, 0x417a, {0x82, 0x92, 0xb2, 0x2e, 0xb1, 0x0d, 0xa8, 0x6c}}

class History : public IHistory
              , public nsIObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_IHISTORY
  NS_DECL_NSIOBSERVER

  History();

  /**
   * Notifies about the visited status of a given URI.
   *
   * @param aURI
   *        The URI to notify about.
   */
  void NotifyVisited(nsIURI *aURI);

  /**
   * Append a task to the queue for SQL queries that need to happen
   * atomically.
   *
   * @pre aTask is not null
   *
   * @param aTask
   *        Task that needs to be completed atomically
   */
  void AppendTask(class Step* aTask);

  /**
   * Call when all steps of the current running task are finished.  Each task
   * should be responsible for calling this when it is finished (even if there
   * are errors).
   *
   * Do not call this twice for the same visit.
   */
  void CurrentTaskFinished();

  /**
   * Obtains a pointer to this service.
   */
  static History *GetService();

  /**
   * Obtains a pointer that has had AddRef called on it.  Used by the service
   * manager only.
   */
  static History *GetSingleton();

private:
  ~History();

  /**
   * Since visits rapidly fire at once, it's very likely to have race
   * conditions for SQL queries.  We often need to see if a row exists
   * or peek at values, and by the time we have retrieved them they could
   * be different.
   *
   * We guarantee an ordering of our SQL statements so that a set of
   * callbacks for one visit are guaranteed to be atomic.  Each visit consists
   * of a data structure that sits in this queue.
   *
   * The front of the queue always has the current visit we are processing.
   */
  nsDeque mPendingVisits;

  /**
   * Begins next task at the front of the queue.  The task remains in the queue
   * until it is done and calls CurrentTaskFinished.
   */
  void StartNextTask();

  /**
   * Remove any memory references to tasks and do not take on any more.
   */
  void Shutdown();

  static History *gService;

  // Ensures new tasks aren't started on destruction.
  bool mShuttingDown;

  typedef nsTArray<mozilla::dom::Link *> ObserverArray;

  class KeyClass : public nsURIHashKey
  {
  public:
    KeyClass(const nsIURI *aURI)
    : nsURIHashKey(aURI)
    {
    }
    KeyClass(const KeyClass &aOther)
    : nsURIHashKey(aOther)
    {
      NS_NOTREACHED("Do not call me!");
    }
    ObserverArray array;
  };

  nsTHashtable<KeyClass> mObservers;
};

} // namespace places
} // namespace mozilla

#endif // mozilla_places_History_h_
