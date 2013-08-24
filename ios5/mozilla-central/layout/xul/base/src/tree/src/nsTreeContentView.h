/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsTreeContentView_h__
#define nsTreeContentView_h__

#include "nsFixedSizeAllocator.h"
#include "nsTArray.h"
#include "nsIDocument.h"
#include "nsStubDocumentObserver.h"
#include "nsITreeBoxObject.h"
#include "nsITreeColumns.h"
#include "nsITreeView.h"
#include "nsITreeContentView.h"
#include "nsITreeSelection.h"
#include "mozilla/Attributes.h"

class Row;

nsresult NS_NewTreeContentView(nsITreeView** aResult);

class nsTreeContentView MOZ_FINAL : public nsINativeTreeView,
                                    public nsITreeContentView,
                                    public nsStubDocumentObserver
{
  public:
    nsTreeContentView(void);

    ~nsTreeContentView(void);

    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsTreeContentView,
                                             nsINativeTreeView)

    NS_DECL_NSITREEVIEW
    // nsINativeTreeView: Untrusted code can use us
    NS_IMETHOD EnsureNative() { return NS_OK; }

    NS_DECL_NSITREECONTENTVIEW

    // nsIDocumentObserver
    NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED
    NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
    NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
    NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED
    NS_DECL_NSIMUTATIONOBSERVER_NODEWILLBEDESTROYED

    static bool CanTrustTreeSelection(nsISupports* aValue);

  protected:
    // Recursive methods which deal with serializing of nested content.
    void Serialize(nsIContent* aContent, PRInt32 aParentIndex, PRInt32* aIndex,
                   nsTArray<Row*>& aRows);

    void SerializeItem(nsIContent* aContent, PRInt32 aParentIndex,
                       PRInt32* aIndex, nsTArray<Row*>& aRows);

    void SerializeSeparator(nsIContent* aContent, PRInt32 aParentIndex,
                            PRInt32* aIndex, nsTArray<Row*>& aRows);

    void GetIndexInSubtree(nsIContent* aContainer, nsIContent* aContent, PRInt32* aResult);
    
    // Helper methods which we use to manage our plain array of rows.
    PRInt32 EnsureSubtree(PRInt32 aIndex);

    PRInt32 RemoveSubtree(PRInt32 aIndex);

    PRInt32 InsertRow(PRInt32 aParentIndex, PRInt32 aIndex, nsIContent* aContent);

    void InsertRowFor(nsIContent* aParent, nsIContent* aChild);

    PRInt32 RemoveRow(PRInt32 aIndex);

    void ClearRows();
    
    void OpenContainer(PRInt32 aIndex);

    void CloseContainer(PRInt32 aIndex);

    PRInt32 FindContent(nsIContent* aContent);

    void UpdateSubtreeSizes(PRInt32 aIndex, PRInt32 aCount);

    void UpdateParentIndexes(PRInt32 aIndex, PRInt32 aSkip, PRInt32 aCount);

    // Content helpers.
    nsIContent* GetCell(nsIContent* aContainer, nsITreeColumn* aCol);

  private:
    nsCOMPtr<nsITreeBoxObject>          mBoxObject;
    nsCOMPtr<nsITreeSelection>          mSelection;
    nsCOMPtr<nsIContent>                mRoot;
    nsCOMPtr<nsIContent>                mBody;
    nsIDocument*                        mDocument;      // WEAK
    nsFixedSizeAllocator                mAllocator;
    nsTArray<Row*>                      mRows;
};

#endif // nsTreeContentView_h__
