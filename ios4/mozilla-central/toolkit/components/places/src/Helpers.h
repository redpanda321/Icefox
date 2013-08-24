/* vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
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

#ifndef mozilla_places_Helpers_h_
#define mozilla_places_Helpers_h_

/**
 * This file contains helper classes used by various bits of Places code.
 */

#include "mozilla/storage.h"
#include "nsIURI.h"

namespace mozilla {
namespace places {

////////////////////////////////////////////////////////////////////////////////
//// Asynchronous Statement Callback Helper

class AsyncStatementCallback : public mozIStorageStatementCallback
{
public:
  // Implement the error handler for asynchronous statements.
  NS_IMETHOD HandleError(mozIStorageError *aError);
};

/**
 * Macro to use in place of NS_DECL_MOZISTORAGESTATEMENTCALLBACK to declare the
 * methods this class does not implement.
 */
#define NS_DECL_ASYNCSTATEMENTCALLBACK \
  NS_IMETHOD HandleResult(mozIStorageResultSet *); \
  NS_IMETHOD HandleCompletion(PRUint16);

/**
 * Macros to use for lazy statements initialization in Places services that use
 * GetStatement() method.
 */
#define RETURN_IF_STMT(_stmt, _sql)                                            \
  PR_BEGIN_MACRO                                                               \
  if (address_of(_stmt) == address_of(aStmt)) {                                \
    if (!_stmt) {                                                              \
      nsresult rv = mDBConn->CreateStatement(_sql, getter_AddRefs(_stmt));     \
      NS_ENSURE_TRUE(NS_SUCCEEDED(rv) && _stmt, nsnull);                       \
    }                                                                          \
    return _stmt.get();                                                        \
  }                                                                            \
  PR_END_MACRO

// Async statements don't need to be scoped, they are reset when done.
// So use this version for statements used async, scoped version for statements
// used sync.
#define DECLARE_AND_ASSIGN_LAZY_STMT(_localStmt, _globalStmt)                  \
  mozIStorageStatement* _localStmt = GetStatement(_globalStmt);                \
  NS_ENSURE_STATE(_localStmt)

#define DECLARE_AND_ASSIGN_SCOPED_LAZY_STMT(_localStmt, _globalStmt)           \
  DECLARE_AND_ASSIGN_LAZY_STMT(_localStmt, _globalStmt);                       \
  mozStorageStatementScoper scoper(_localStmt)


/**
 * Utils to bind a specified URI (or URL) to a statement or binding params, at
 * the specified index or name.
 * @note URIs are always bound as UTF8.
 */
class URIBinder // static
{
public:
  // Bind URI to statement by index.
  static nsresult Bind(mozIStorageStatement* statement,
                       PRInt32 index,
                       nsIURI* aURI);
  // Statement URLCString to statement by index.
  static nsresult Bind(mozIStorageStatement* statement,
                       PRInt32 index,
                       const nsACString& aURLString);
  // Bind URI to statement by name.
  static nsresult Bind(mozIStorageStatement* statement,
                       const nsACString& aName,
                       nsIURI* aURI);
  // Bind URLCString to statement by name.
  static nsresult Bind(mozIStorageStatement* statement,
                       const nsACString& aName,
                       const nsACString& aURLString);
  // Bind URI to params by index.
  static nsresult Bind(mozIStorageBindingParams* aParams,
                       PRInt32 index,
                       nsIURI* aURI);
  // Bind URLCString to params by index.
  static nsresult Bind(mozIStorageBindingParams* aParams,
                       PRInt32 index,
                       const nsACString& aURLString);
  // Bind URI to params by name.
  static nsresult Bind(mozIStorageBindingParams* aParams,
                       const nsACString& aName,
                       nsIURI* aURI);
  // Bind URLCString to params by name.
  static nsresult Bind(mozIStorageBindingParams* aParams,
                       const nsACString& aName,
                       const nsACString& aURLString);
};

/**
 * This extracts the hostname from the URI and reverses it in the
 * form that we use (always ending with a "."). So
 * "http://microsoft.com/" becomes "moc.tfosorcim."
 * 
 * The idea behind this is that we can create an index over the items in
 * the reversed host name column, and then query for as much or as little
 * of the host name as we feel like.
 * 
 * For example, the query "host >= 'gro.allizom.' AND host < 'gro.allizom/'
 * Matches all host names ending in '.mozilla.org', including
 * 'developer.mozilla.org' and just 'mozilla.org' (since we define all
 * reversed host names to end in a period, even 'mozilla.org' matches).
 * The important thing is that this operation uses the index. Any substring
 * calls in a select statement (even if it's for the beginning of a string)
 * will bypass any indices and will be slow).
 *
 * @param aURI
 *        URI that contains spec to reverse
 * @param aRevHost
 *        Out parameter
 */
nsresult GetReversedHostname(nsIURI* aURI, nsString& aRevHost);

/**
 * Similar method to GetReversedHostName but for strings
 */
void GetReversedHostname(const nsString& aForward, nsString& aRevHost);

/**
 * Reverses a string.
 *
 * @param aInput
 *        The string to be reversed
 * @param aReversed
 *        Ouput parameter will contain the reversed string
 */
void ReverseString(const nsString& aInput, nsString& aReversed);

} // namespace places
} // namespace mozilla

#endif // mozilla_places_Helpers_h_
