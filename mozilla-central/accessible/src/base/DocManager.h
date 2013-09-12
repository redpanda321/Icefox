/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_a11_DocManager_h_
#define mozilla_a11_DocManager_h_

#include "nsIDocument.h"
#include "nsIDOMEventListener.h"
#include "nsRefPtrHashtable.h"
#include "nsIWebProgress.h"
#include "nsIWebProgressListener.h"
#include "nsWeakReference.h"
#include "nsIPresShell.h"

namespace mozilla {
namespace a11y {

class Accessible;
class DocAccessible;

/**
 * Manage the document accessible life cycle.
 */
class DocManager : public nsIWebProgressListener,
                   public nsIDOMEventListener,
                   public nsSupportsWeakReference
{
public:
  virtual ~DocManager() { }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIWEBPROGRESSLISTENER
  NS_DECL_NSIDOMEVENTLISTENER

  /**
   * Return document accessible for the given DOM node.
   */
  DocAccessible* GetDocAccessible(nsIDocument* aDocument);

  /**
   * Return document accessible for the given presshell.
   */
  DocAccessible* GetDocAccessible(const nsIPresShell* aPresShell)
  {
    if (!aPresShell)
      return nullptr;

    DocAccessible* doc = aPresShell->GetDocAccessible();
    if (doc)
      return doc;

    return GetDocAccessible(aPresShell->GetDocument());
  }

  /**
   * Search through all document accessibles for an accessible with the given
   * unique id.
   */
  Accessible* FindAccessibleInCache(nsINode* aNode) const;

  /**
   * Return document accessible from the cache. Convenient method for testing.
   */
  inline DocAccessible* GetDocAccessibleFromCache(nsIDocument* aDocument) const
  {
    return mDocAccessibleCache.GetWeak(aDocument);
  }

  /**
   * Called by document accessible when it gets shutdown.
   */
  inline void NotifyOfDocumentShutdown(nsIDocument* aDocument)
  {
    mDocAccessibleCache.Remove(aDocument);
  }

#ifdef DEBUG
  bool IsProcessingRefreshDriverNotification() const;
#endif

protected:
  DocManager() { }

  /**
   * Initialize the manager.
   */
  bool Init();

  /**
   * Shutdown the manager.
   */
  void Shutdown();

private:
  DocManager(const DocManager&);
  DocManager& operator =(const DocManager&);

private:
  /**
   * Create an accessible document if it was't created and fire accessibility
   * events if needed.
   *
   * @param  aDocument       [in] loaded DOM document
   * @param  aLoadEventType  [in] specifies the event type to fire load event,
   *                           if 0 then no event is fired
   */
  void HandleDOMDocumentLoad(nsIDocument* aDocument,
                             uint32_t aLoadEventType);

  /**
   * Add 'pagehide' and 'DOMContentLoaded' event listeners.
   */
  void AddListeners(nsIDocument *aDocument, bool aAddPageShowListener);

  /**
   * Create document or root accessible.
   */
  DocAccessible* CreateDocOrRootAccessible(nsIDocument* aDocument);

  typedef nsRefPtrHashtable<nsPtrHashKey<const nsIDocument>, DocAccessible>
    DocAccessibleHashtable;

  /**
   * Get first entry of the document accessible from cache.
   */
  static PLDHashOperator
    GetFirstEntryInDocCache(const nsIDocument* aKey,
                            DocAccessible* aDocAccessible,
                            void* aUserArg);

  /**
   * Clear the cache and shutdown the document accessibles.
   */
  void ClearDocCache();

  struct nsSearchAccessibleInCacheArg
  {
    Accessible* mAccessible;
    nsINode* mNode;
  };

  static PLDHashOperator
    SearchAccessibleInDocCache(const nsIDocument* aKey,
                               DocAccessible* aDocAccessible,
                               void* aUserArg);

#ifdef DEBUG
  static PLDHashOperator
    SearchIfDocIsRefreshing(const nsIDocument* aKey,
                            DocAccessible* aDocAccessible, void* aUserArg);
#endif

  DocAccessibleHashtable mDocAccessibleCache;
};

} // namespace a11y
} // namespace mozilla

#endif // mozilla_a11_DocManager_h_
