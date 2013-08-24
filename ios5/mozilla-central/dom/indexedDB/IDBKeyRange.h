/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_indexeddb_idbkeyrange_h__
#define mozilla_dom_indexeddb_idbkeyrange_h__

#include "mozilla/dom/indexedDB/IndexedDatabase.h"
#include "mozilla/dom/indexedDB/Key.h"

#include "nsIIDBKeyRange.h"

#include "nsCycleCollectionParticipant.h"

class mozIStorageStatement;

BEGIN_INDEXEDDB_NAMESPACE

namespace ipc {
namespace FIXME_Bug_521898_objectstore {
class KeyRange;
} // namespace FIXME_Bug_521898_objectstore
} // namespace ipc

class IDBKeyRange MOZ_FINAL : public nsIIDBKeyRange
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIIDBKEYRANGE
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(IDBKeyRange)

  static JSBool DefineConstructors(JSContext* aCx,
                                   JSObject* aObject);

  static nsresult FromJSVal(JSContext* aCx,
                            const jsval& aVal,
                            IDBKeyRange** aKeyRange);

  template <class T>
  static already_AddRefed<IDBKeyRange>
  FromSerializedKeyRange(const T& aKeyRange);

  IDBKeyRange(bool aLowerOpen,
              bool aUpperOpen,
              bool aIsOnly)
  : mCachedLowerVal(JSVAL_VOID), mCachedUpperVal(JSVAL_VOID),
    mLowerOpen(aLowerOpen), mUpperOpen(aUpperOpen), mIsOnly(aIsOnly),
    mHaveCachedLowerVal(false), mHaveCachedUpperVal(false), mRooted(false)
  { }

  const Key& Lower() const
  {
    return mLower;
  }

  Key& Lower()
  {
    return mLower;
  }

  const Key& Upper() const
  {
    return mIsOnly ? mLower : mUpper;
  }

  Key& Upper()
  {
    return mIsOnly ? mLower : mUpper;
  }

  bool IsLowerOpen() const
  {
    return mLowerOpen;
  }

  bool IsUpperOpen() const
  {
    return mUpperOpen;
  }

  bool IsOnly() const
  {
    return mIsOnly;
  }

  void GetBindingClause(const nsACString& aKeyColumnName,
                        nsACString& _retval) const
  {
    NS_NAMED_LITERAL_CSTRING(andStr, " AND ");
    NS_NAMED_LITERAL_CSTRING(spacecolon, " :");
    NS_NAMED_LITERAL_CSTRING(lowerKey, "lower_key");

    if (IsOnly()) {
      // Both keys are set and they're equal.
      _retval = andStr + aKeyColumnName + NS_LITERAL_CSTRING(" =") +
                spacecolon + lowerKey;
    }
    else {
      nsCAutoString clause;

      if (!Lower().IsUnset()) {
        // Lower key is set.
        clause.Append(andStr + aKeyColumnName);
        clause.AppendLiteral(" >");
        if (!IsLowerOpen()) {
          clause.AppendLiteral("=");
        }
        clause.Append(spacecolon + lowerKey);
      }

      if (!Upper().IsUnset()) {
        // Upper key is set.
        clause.Append(andStr + aKeyColumnName);
        clause.AppendLiteral(" <");
        if (!IsUpperOpen()) {
          clause.AppendLiteral("=");
        }
        clause.Append(spacecolon + NS_LITERAL_CSTRING("upper_key"));
      }

      _retval = clause;
    }
  }

  nsresult BindToStatement(mozIStorageStatement* aStatement) const
  {
    NS_NAMED_LITERAL_CSTRING(lowerKey, "lower_key");

    if (IsOnly()) {
      return Lower().BindToStatement(aStatement, lowerKey);
    }

    nsresult rv;

    if (!Lower().IsUnset()) {
      rv = Lower().BindToStatement(aStatement, lowerKey);
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
    }

    if (!Upper().IsUnset()) {
      rv = Upper().BindToStatement(aStatement, NS_LITERAL_CSTRING("upper_key"));
      NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
    }

    return NS_OK;
  }

  template <class T>
  void ToSerializedKeyRange(T& aKeyRange);

private:
  ~IDBKeyRange();

  Key mLower;
  Key mUpper;
  jsval mCachedLowerVal;
  jsval mCachedUpperVal;
  bool mLowerOpen;
  bool mUpperOpen;
  bool mIsOnly;
  bool mHaveCachedLowerVal;
  bool mHaveCachedUpperVal;
  bool mRooted;
};

END_INDEXEDDB_NAMESPACE

#endif // mozilla_dom_indexeddb_idbkeyrange_h__
