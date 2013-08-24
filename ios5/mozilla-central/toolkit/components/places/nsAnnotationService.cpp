/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Util.h"

#include "nsAnnotationService.h"
#include "nsNavHistory.h"
#include "nsPlacesTables.h"
#include "nsPlacesIndexes.h"
#include "nsPlacesMacros.h"
#include "Helpers.h"

#include "nsNetUtil.h"
#include "nsIVariant.h"
#include "nsString.h"
#include "nsVariant.h"
#include "mozilla/storage.h"

#include "sampler.h"

using namespace mozilla;

#define ENSURE_ANNO_TYPE(_type, _statement)                                    \
  PR_BEGIN_MACRO                                                               \
  PRInt32 type = _statement->AsInt32(kAnnoIndex_Type);                         \
  NS_ENSURE_TRUE(type == nsIAnnotationService::_type, NS_ERROR_INVALID_ARG);   \
  PR_END_MACRO

#define NOTIFY_ANNOS_OBSERVERS(_notification)                                  \
  PR_BEGIN_MACRO                                                               \
  for (PRInt32 i = 0; i < mObservers.Count(); i++)                             \
    mObservers[i]->_notification;                                              \
  PR_END_MACRO

const PRInt32 nsAnnotationService::kAnnoIndex_ID = 0;
const PRInt32 nsAnnotationService::kAnnoIndex_PageOrItem = 1;
const PRInt32 nsAnnotationService::kAnnoIndex_NameID = 2;
const PRInt32 nsAnnotationService::kAnnoIndex_MimeType = 3;
const PRInt32 nsAnnotationService::kAnnoIndex_Content = 4;
const PRInt32 nsAnnotationService::kAnnoIndex_Flags = 5;
const PRInt32 nsAnnotationService::kAnnoIndex_Expiration = 6;
const PRInt32 nsAnnotationService::kAnnoIndex_Type = 7;
const PRInt32 nsAnnotationService::kAnnoIndex_DateAdded = 8;
const PRInt32 nsAnnotationService::kAnnoIndex_LastModified = 9;

using namespace mozilla::places;

PLACES_FACTORY_SINGLETON_IMPLEMENTATION(nsAnnotationService, gAnnotationService)

NS_IMPL_ISUPPORTS3(nsAnnotationService
, nsIAnnotationService
, nsIObserver
, nsISupportsWeakReference
)


nsAnnotationService::nsAnnotationService()
  : mHasSessionAnnotations(false)
{
  NS_ASSERTION(!gAnnotationService,
               "Attempting to create two instances of the service!");
  gAnnotationService = this;
}


nsAnnotationService::~nsAnnotationService()
{
  NS_ASSERTION(gAnnotationService == this,
               "Deleting a non-singleton instance of the service");
  if (gAnnotationService == this)
    gAnnotationService = nsnull;
}


nsresult
nsAnnotationService::Init()
{
  mDB = Database::GetDatabase();
  NS_ENSURE_STATE(mDB);

  nsCOMPtr<nsIObserverService> obsSvc = mozilla::services::GetObserverService();
  if (obsSvc) {
    (void)obsSvc->AddObserver(this, TOPIC_PLACES_SHUTDOWN, true);
  }

  return NS_OK;
}

nsresult
nsAnnotationService::SetAnnotationStringInternal(nsIURI* aURI,
                                                 PRInt64 aItemId,
                                                 const nsACString& aName,
                                                 const nsAString& aValue,
                                                 PRInt32 aFlags,
                                                 PRUint16 aExpiration)
{
  mozStorageTransaction transaction(mDB->MainConn(), false);
  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartSetAnnotation(aURI, aItemId, aName, aFlags, aExpiration,
                                   nsIAnnotationService::TYPE_STRING,
                                   statement);
  NS_ENSURE_SUCCESS(rv, rv);
  mozStorageStatementScoper scoper(statement);

  rv = statement->BindStringByName(NS_LITERAL_CSTRING("content"), aValue);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = statement->BindNullByName(NS_LITERAL_CSTRING("mime_type"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetPageAnnotation(nsIURI* aURI,
                                       const nsACString& aName,
                                       nsIVariant* aValue,
                                       PRInt32 aFlags,
                                       PRUint16 aExpiration)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG(aValue);

  PRUint16 dataType;
  nsresult rv = aValue->GetDataType(&dataType);
  NS_ENSURE_SUCCESS(rv, rv);

  switch (dataType) {
    case nsIDataType::VTYPE_INT8:
    case nsIDataType::VTYPE_UINT8:
    case nsIDataType::VTYPE_INT16:
    case nsIDataType::VTYPE_UINT16:
    case nsIDataType::VTYPE_INT32:
    case nsIDataType::VTYPE_UINT32:
    case nsIDataType::VTYPE_BOOL: {
      PRInt32 valueInt;
      rv = aValue->GetAsInt32(&valueInt);
      if (NS_SUCCEEDED(rv)) {
        NS_ENSURE_SUCCESS(rv, rv);
        rv = SetPageAnnotationInt32(aURI, aName, valueInt, aFlags, aExpiration);
        NS_ENSURE_SUCCESS(rv, rv);
        return NS_OK;
      }
      // Fall through PRInt64 case otherwise.
    }
    case nsIDataType::VTYPE_INT64:
    case nsIDataType::VTYPE_UINT64: {
      PRInt64 valueLong;
      rv = aValue->GetAsInt64(&valueLong);
      if (NS_SUCCEEDED(rv)) {
        NS_ENSURE_SUCCESS(rv, rv);
        rv = SetPageAnnotationInt64(aURI, aName, valueLong, aFlags, aExpiration);
        NS_ENSURE_SUCCESS(rv, rv);
        return NS_OK;
      }
      // Fall through double case otherwise.
    }
    case nsIDataType::VTYPE_FLOAT:
    case nsIDataType::VTYPE_DOUBLE: {
      double valueDouble;
      rv = aValue->GetAsDouble(&valueDouble);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = SetPageAnnotationDouble(aURI, aName, valueDouble, aFlags, aExpiration);
      NS_ENSURE_SUCCESS(rv, rv);
      return NS_OK;
    }
    case nsIDataType::VTYPE_CHAR:
    case nsIDataType::VTYPE_WCHAR:
    case nsIDataType::VTYPE_DOMSTRING:
    case nsIDataType::VTYPE_CHAR_STR:
    case nsIDataType::VTYPE_WCHAR_STR:
    case nsIDataType::VTYPE_STRING_SIZE_IS:
    case nsIDataType::VTYPE_WSTRING_SIZE_IS:
    case nsIDataType::VTYPE_UTF8STRING:
    case nsIDataType::VTYPE_CSTRING:
    case nsIDataType::VTYPE_ASTRING: {
      nsAutoString stringValue;
      rv = aValue->GetAsAString(stringValue);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = SetPageAnnotationString(aURI, aName, stringValue, aFlags, aExpiration);
      NS_ENSURE_SUCCESS(rv, rv);
      return NS_OK;
    }
  }

  return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP
nsAnnotationService::SetItemAnnotation(PRInt64 aItemId,
                                       const nsACString& aName,
                                       nsIVariant* aValue,
                                       PRInt32 aFlags,
                                       PRUint16 aExpiration)
{
  SAMPLE_LABEL("AnnotationService", "SetItemAnnotation");
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG(aValue);

  if (aExpiration == EXPIRE_WITH_HISTORY)
    return NS_ERROR_INVALID_ARG;

  PRUint16 dataType;
  nsresult rv = aValue->GetDataType(&dataType);
  NS_ENSURE_SUCCESS(rv, rv);

  switch (dataType) {
    case nsIDataType::VTYPE_INT8:
    case nsIDataType::VTYPE_UINT8:
    case nsIDataType::VTYPE_INT16:
    case nsIDataType::VTYPE_UINT16:
    case nsIDataType::VTYPE_INT32:
    case nsIDataType::VTYPE_UINT32:
    case nsIDataType::VTYPE_BOOL: {
      PRInt32 valueInt;
      rv = aValue->GetAsInt32(&valueInt);
      if (NS_SUCCEEDED(rv)) {
        NS_ENSURE_SUCCESS(rv, rv);
        rv = SetItemAnnotationInt32(aItemId, aName, valueInt, aFlags, aExpiration);
        NS_ENSURE_SUCCESS(rv, rv);
        return NS_OK;
      }
      // Fall through PRInt64 case otherwise.
    }
    case nsIDataType::VTYPE_INT64:
    case nsIDataType::VTYPE_UINT64: {
      PRInt64 valueLong;
      rv = aValue->GetAsInt64(&valueLong);
      if (NS_SUCCEEDED(rv)) {
        NS_ENSURE_SUCCESS(rv, rv);
        rv = SetItemAnnotationInt64(aItemId, aName, valueLong, aFlags, aExpiration);
        NS_ENSURE_SUCCESS(rv, rv);
        return NS_OK;
      }
      // Fall through double case otherwise.
    }
    case nsIDataType::VTYPE_FLOAT:
    case nsIDataType::VTYPE_DOUBLE: {
      double valueDouble;
      rv = aValue->GetAsDouble(&valueDouble);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = SetItemAnnotationDouble(aItemId, aName, valueDouble, aFlags, aExpiration);
      NS_ENSURE_SUCCESS(rv, rv);
      return NS_OK;
    }
    case nsIDataType::VTYPE_CHAR:
    case nsIDataType::VTYPE_WCHAR:
    case nsIDataType::VTYPE_DOMSTRING:
    case nsIDataType::VTYPE_CHAR_STR:
    case nsIDataType::VTYPE_WCHAR_STR:
    case nsIDataType::VTYPE_STRING_SIZE_IS:
    case nsIDataType::VTYPE_WSTRING_SIZE_IS:
    case nsIDataType::VTYPE_UTF8STRING:
    case nsIDataType::VTYPE_CSTRING:
    case nsIDataType::VTYPE_ASTRING: {
      nsAutoString stringValue;
      rv = aValue->GetAsAString(stringValue);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = SetItemAnnotationString(aItemId, aName, stringValue, aFlags, aExpiration);
      NS_ENSURE_SUCCESS(rv, rv);
      return NS_OK;
    }
  }

  return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP
nsAnnotationService::SetPageAnnotationString(nsIURI* aURI,
                                             const nsACString& aName,
                                             const nsAString& aValue,
                                             PRInt32 aFlags,
                                             PRUint16 aExpiration)
{
  NS_ENSURE_ARG(aURI);

  if (InPrivateBrowsingMode())
    return NS_OK;

  nsresult rv = SetAnnotationStringInternal(aURI, 0, aName, aValue,
                                            aFlags, aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnPageAnnotationSet(aURI, aName));

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetItemAnnotationString(PRInt64 aItemId,
                                             const nsACString& aName,
                                             const nsAString& aValue,
                                             PRInt32 aFlags,
                                             PRUint16 aExpiration)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  if (aExpiration == EXPIRE_WITH_HISTORY)
    return NS_ERROR_INVALID_ARG;

  nsresult rv = SetAnnotationStringInternal(nsnull, aItemId, aName, aValue,
                                            aFlags, aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnItemAnnotationSet(aItemId, aName));

  return NS_OK;
}


nsresult
nsAnnotationService::SetAnnotationInt32Internal(nsIURI* aURI,
                                                PRInt64 aItemId,
                                                const nsACString& aName,
                                                PRInt32 aValue,
                                                PRInt32 aFlags,
                                                PRUint16 aExpiration)
{
  mozStorageTransaction transaction(mDB->MainConn(), false);
  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartSetAnnotation(aURI, aItemId, aName, aFlags, aExpiration,
                                   nsIAnnotationService::TYPE_INT32,
                                   statement);
  NS_ENSURE_SUCCESS(rv, rv);
  mozStorageStatementScoper scoper(statement);

  rv = statement->BindInt32ByName(NS_LITERAL_CSTRING("content"), aValue);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = statement->BindNullByName(NS_LITERAL_CSTRING("mime_type"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetPageAnnotationInt32(nsIURI* aURI,
                                            const nsACString& aName,
                                            PRInt32 aValue,
                                            PRInt32 aFlags,
                                            PRUint16 aExpiration)
{
  NS_ENSURE_ARG(aURI);

  if (InPrivateBrowsingMode())
    return NS_OK;

  nsresult rv = SetAnnotationInt32Internal(aURI, 0, aName, aValue,
                                           aFlags, aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnPageAnnotationSet(aURI, aName));

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetItemAnnotationInt32(PRInt64 aItemId,
                                            const nsACString& aName,
                                            PRInt32 aValue,
                                            PRInt32 aFlags,
                                            PRUint16 aExpiration)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  if (aExpiration == EXPIRE_WITH_HISTORY)
    return NS_ERROR_INVALID_ARG;

  nsresult rv = SetAnnotationInt32Internal(nsnull, aItemId, aName, aValue,
                                           aFlags, aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnItemAnnotationSet(aItemId, aName));

  return NS_OK;
}


nsresult
nsAnnotationService::SetAnnotationInt64Internal(nsIURI* aURI,
                                                PRInt64 aItemId,
                                                const nsACString& aName,
                                                PRInt64 aValue,
                                                PRInt32 aFlags,
                                                PRUint16 aExpiration)
{
  mozStorageTransaction transaction(mDB->MainConn(), false);
  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartSetAnnotation(aURI, aItemId, aName, aFlags, aExpiration,
                                   nsIAnnotationService::TYPE_INT64,
                                   statement);
  NS_ENSURE_SUCCESS(rv, rv);
  mozStorageStatementScoper scoper(statement);

  rv = statement->BindInt64ByName(NS_LITERAL_CSTRING("content"), aValue);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = statement->BindNullByName(NS_LITERAL_CSTRING("mime_type"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetPageAnnotationInt64(nsIURI* aURI,
                                            const nsACString& aName,
                                            PRInt64 aValue,
                                            PRInt32 aFlags,
                                            PRUint16 aExpiration)
{
  NS_ENSURE_ARG(aURI);

  if (InPrivateBrowsingMode())
    return NS_OK;

  nsresult rv = SetAnnotationInt64Internal(aURI, 0, aName, aValue,
                                           aFlags, aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnPageAnnotationSet(aURI, aName));

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetItemAnnotationInt64(PRInt64 aItemId,
                                            const nsACString& aName,
                                            PRInt64 aValue,
                                            PRInt32 aFlags,
                                            PRUint16 aExpiration)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  if (aExpiration == EXPIRE_WITH_HISTORY)
    return NS_ERROR_INVALID_ARG;

  nsresult rv = SetAnnotationInt64Internal(nsnull, aItemId, aName, aValue,
                                           aFlags, aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnItemAnnotationSet(aItemId, aName));

  return NS_OK;
}


nsresult
nsAnnotationService::SetAnnotationDoubleInternal(nsIURI* aURI,
                                                 PRInt64 aItemId,
                                                 const nsACString& aName,
                                                 double aValue,
                                                 PRInt32 aFlags,
                                                 PRUint16 aExpiration)
{
  mozStorageTransaction transaction(mDB->MainConn(), false);
  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartSetAnnotation(aURI, aItemId, aName, aFlags, aExpiration,
                                   nsIAnnotationService::TYPE_DOUBLE,
                                   statement);
  NS_ENSURE_SUCCESS(rv, rv);
  mozStorageStatementScoper scoper(statement);

  rv = statement->BindDoubleByName(NS_LITERAL_CSTRING("content"), aValue);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = statement->BindNullByName(NS_LITERAL_CSTRING("mime_type"));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetPageAnnotationDouble(nsIURI* aURI,
                                             const nsACString& aName,
                                             double aValue,
                                             PRInt32 aFlags,
                                             PRUint16 aExpiration)
{
  NS_ENSURE_ARG(aURI);

  if (InPrivateBrowsingMode())
    return NS_OK;

  nsresult rv = SetAnnotationDoubleInternal(aURI, 0, aName, aValue,
                                            aFlags, aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnPageAnnotationSet(aURI, aName));

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetItemAnnotationDouble(PRInt64 aItemId,
                                             const nsACString& aName,
                                             double aValue,
                                             PRInt32 aFlags,
                                             PRUint16 aExpiration)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  if (aExpiration == EXPIRE_WITH_HISTORY)
    return NS_ERROR_INVALID_ARG;

  nsresult rv = SetAnnotationDoubleInternal(nsnull, aItemId, aName, aValue,
                                            aFlags, aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnItemAnnotationSet(aItemId, aName));

  return NS_OK;
}


nsresult
nsAnnotationService::SetAnnotationBinaryInternal(nsIURI* aURI,
                                                 PRInt64 aItemId,
                                                 const nsACString& aName,
                                                 const PRUint8* aData,
                                                 PRUint32 aDataLen,
                                                 const nsACString& aMimeType,
                                                 PRInt32 aFlags,
                                                 PRUint16 aExpiration)
{
  if (aMimeType.Length() == 0)
    return NS_ERROR_INVALID_ARG;

  mozStorageTransaction transaction(mDB->MainConn(), false);
  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartSetAnnotation(aURI, aItemId, aName, aFlags, aExpiration,
                                   nsIAnnotationService::TYPE_BINARY,
                                   statement);
  NS_ENSURE_SUCCESS(rv, rv);
  mozStorageStatementScoper scoper(statement);

  rv = statement->BindBlobByName(NS_LITERAL_CSTRING("content"), aData, aDataLen);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = statement->BindUTF8StringByName(NS_LITERAL_CSTRING("mime_type"), aMimeType);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetPageAnnotationBinary(nsIURI* aURI,
                                             const nsACString& aName,
                                             const PRUint8* aData,
                                             PRUint32 aDataLen,
                                             const nsACString& aMimeType,
                                             PRInt32 aFlags,
                                             PRUint16 aExpiration)
{
  NS_ENSURE_ARG(aURI);

  if (InPrivateBrowsingMode())
    return NS_OK;

  nsresult rv = SetAnnotationBinaryInternal(aURI, 0, aName, aData, aDataLen,
                                            aMimeType, aFlags, aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnPageAnnotationSet(aURI, aName));

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::SetItemAnnotationBinary(PRInt64 aItemId,
                                             const nsACString& aName,
                                             const PRUint8* aData,
                                             PRUint32 aDataLen,
                                             const nsACString& aMimeType,
                                             PRInt32 aFlags,
                                             PRUint16 aExpiration)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  if (aExpiration == EXPIRE_WITH_HISTORY)
    return NS_ERROR_INVALID_ARG;

  nsresult rv = SetAnnotationBinaryInternal(nsnull, aItemId, aName, aData,
                                            aDataLen, aMimeType, aFlags,
                                            aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnItemAnnotationSet(aItemId, aName));

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetPageAnnotationString(nsIURI* aURI,
                                             const nsACString& aName,
                                             nsAString& _retval)
{
  NS_ENSURE_ARG(aURI);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(aURI, 0, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  ENSURE_ANNO_TYPE(TYPE_STRING, statement);
  rv = statement->GetString(kAnnoIndex_Content, _retval);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotationString(PRInt64 aItemId,
                                             const nsACString& aName,
                                             nsAString& _retval)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(nsnull, aItemId, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  ENSURE_ANNO_TYPE(TYPE_STRING, statement);
  rv = statement->GetString(kAnnoIndex_Content, _retval);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetPageAnnotation(nsIURI* aURI,
                                       const nsACString& aName,
                                       nsIVariant** _retval)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(aURI, 0, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);

  nsCOMPtr<nsIWritableVariant> value = new nsVariant();
  PRInt32 type = statement->AsInt32(kAnnoIndex_Type);
  switch (type) {
    case nsIAnnotationService::TYPE_INT32:
    case nsIAnnotationService::TYPE_INT64:
    case nsIAnnotationService::TYPE_DOUBLE: {
      rv = value->SetAsDouble(statement->AsDouble(kAnnoIndex_Content));
      break;
    }
    case nsIAnnotationService::TYPE_STRING: {
      nsAutoString valueString;
      rv = statement->GetString(kAnnoIndex_Content, valueString);
      if (NS_SUCCEEDED(rv))
        rv = value->SetAsAString(valueString);
      break;
    }
    case nsIAnnotationService::TYPE_BINARY: {
      rv = NS_ERROR_INVALID_ARG;
      break;
    }
    default: {
      rv = NS_ERROR_UNEXPECTED;
      break;
    }
  }

  if (NS_SUCCEEDED(rv))
    NS_ADDREF(*_retval = value);

  return rv;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotation(PRInt64 aItemId,
                                       const nsACString& aName,
                                       nsIVariant** _retval)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(nsnull, aItemId, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);

  nsCOMPtr<nsIWritableVariant> value = new nsVariant();
  PRInt32 type = statement->AsInt32(kAnnoIndex_Type);
  switch (type) {
    case nsIAnnotationService::TYPE_INT32:
    case nsIAnnotationService::TYPE_INT64:
    case nsIAnnotationService::TYPE_DOUBLE: {
      rv = value->SetAsDouble(statement->AsDouble(kAnnoIndex_Content));
      break;
    }
    case nsIAnnotationService::TYPE_STRING: {
      nsAutoString valueString;
      rv = statement->GetString(kAnnoIndex_Content, valueString);
      if (NS_SUCCEEDED(rv))
        rv = value->SetAsAString(valueString);
      break;
    }
    case nsIAnnotationService::TYPE_BINARY: {
      rv = NS_ERROR_INVALID_ARG;
      break;
    }
    default: {
      rv = NS_ERROR_UNEXPECTED;
      break;
    }
  }

  if (NS_SUCCEEDED(rv))
    NS_ADDREF(*_retval = value);

  return rv;
}


NS_IMETHODIMP
nsAnnotationService::GetPageAnnotationInt32(nsIURI* aURI,
                                        const nsACString& aName,
                                        PRInt32* _retval)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(aURI, 0, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  ENSURE_ANNO_TYPE(TYPE_INT32, statement);
  *_retval = statement->AsInt32(kAnnoIndex_Content);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotationInt32(PRInt64 aItemId,
                                            const nsACString& aName,
                                            PRInt32* _retval)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(nsnull, aItemId, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  ENSURE_ANNO_TYPE(TYPE_INT32, statement);
  *_retval = statement->AsInt32(kAnnoIndex_Content);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetPageAnnotationInt64(nsIURI* aURI,
                                            const nsACString& aName,
                                            PRInt64* _retval)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(aURI, 0, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  ENSURE_ANNO_TYPE(TYPE_INT64, statement);
  *_retval = statement->AsInt64(kAnnoIndex_Content);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotationInt64(PRInt64 aItemId,
                                            const nsACString& aName,
                                            PRInt64* _retval)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(nsnull, aItemId, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  ENSURE_ANNO_TYPE(TYPE_INT64, statement);
  *_retval = statement->AsInt64(kAnnoIndex_Content);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetPageAnnotationType(nsIURI* aURI,
                                           const nsACString& aName,
                                           PRUint16* _retval)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(aURI, 0, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  *_retval = statement->AsInt32(kAnnoIndex_Type);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotationType(PRInt64 aItemId,
                                           const nsACString& aName,
                                           PRUint16* _retval)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(nsnull, aItemId, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  *_retval = statement->AsInt32(kAnnoIndex_Type);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetPageAnnotationDouble(nsIURI* aURI,
                                             const nsACString& aName,
                                             double* _retval)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_retval);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(aURI, 0, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  ENSURE_ANNO_TYPE(TYPE_DOUBLE, statement);
  *_retval = statement->AsDouble(kAnnoIndex_Content);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotationDouble(PRInt64 aItemId,
                                             const nsACString& aName,
                                             double* _retval)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(nsnull, aItemId, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  ENSURE_ANNO_TYPE(TYPE_DOUBLE, statement);
  *_retval = statement->AsDouble(kAnnoIndex_Content);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetPageAnnotationBinary(nsIURI* aURI,
                                             const nsACString& aName,
                                             PRUint8** _data,
                                             PRUint32* _dataLen,
                                             nsACString& _mimeType)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_data);
  NS_ENSURE_ARG_POINTER(_dataLen);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(aURI, 0, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  ENSURE_ANNO_TYPE(TYPE_BINARY, statement);
  rv = statement->GetBlob(kAnnoIndex_Content, _dataLen, _data);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = statement->GetUTF8String(kAnnoIndex_MimeType, _mimeType);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotationBinary(PRInt64 aItemId,
                                             const nsACString& aName,
                                             PRUint8** _data,
                                             PRUint32* _dataLen,
                                             nsACString& _mimeType)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_data);
  NS_ENSURE_ARG_POINTER(_dataLen);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(nsnull, aItemId, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  ENSURE_ANNO_TYPE(TYPE_BINARY, statement);
  rv = statement->GetBlob(kAnnoIndex_Content, _dataLen, _data);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = statement->GetUTF8String(kAnnoIndex_MimeType, _mimeType);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetPageAnnotationInfo(nsIURI* aURI,
                                           const nsACString& aName,
                                           PRInt32* _flags,
                                           PRUint16* _expiration,
                                           nsACString& _mimeType,
                                           PRUint16* _storageType)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_flags);
  NS_ENSURE_ARG_POINTER(_expiration);
  NS_ENSURE_ARG_POINTER(_storageType);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(aURI, 0, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  *_flags = statement->AsInt32(kAnnoIndex_Flags);
  *_expiration = (PRUint16)statement->AsInt32(kAnnoIndex_Expiration);
  rv = statement->GetUTF8String(kAnnoIndex_MimeType, _mimeType);
  NS_ENSURE_SUCCESS(rv, rv);
  PRInt32 type = (PRUint16)statement->AsInt32(kAnnoIndex_Type);
  if (type == 0) {
    // For annotations created before explicit typing,
    // we can't determine type, just return as string type.
    *_storageType = nsIAnnotationService::TYPE_STRING;
  }
  else
    *_storageType = type;

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotationInfo(PRInt64 aItemId,
                                           const nsACString& aName,
                                           PRInt32* _flags,
                                           PRUint16* _expiration,
                                           nsACString& _mimeType,
                                           PRUint16* _storageType)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_flags);
  NS_ENSURE_ARG_POINTER(_expiration);
  NS_ENSURE_ARG_POINTER(_storageType);

  nsCOMPtr<mozIStorageStatement> statement;
  nsresult rv = StartGetAnnotation(nsnull, aItemId, aName, statement);
  if (NS_FAILED(rv))
    return rv;

  mozStorageStatementScoper scoper(statement);
  *_flags = statement->AsInt32(kAnnoIndex_Flags);
  *_expiration = (PRUint16)statement->AsInt32(kAnnoIndex_Expiration);
  rv = statement->GetUTF8String(kAnnoIndex_MimeType, _mimeType);
  NS_ENSURE_SUCCESS(rv, rv);
  PRInt32 type = (PRUint16)statement->AsInt32(kAnnoIndex_Type);
  if (type == 0) {
    // For annotations created before explicit typing,
    // we can't determine type, just return as string type.
    *_storageType = nsIAnnotationService::TYPE_STRING;
  }
  else {
    *_storageType = type;
  }

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetPagesWithAnnotation(const nsACString& aName,
                                            PRUint32* _resultCount,
                                            nsIURI*** _results)
{
  NS_ENSURE_TRUE(!aName.IsEmpty(), NS_ERROR_INVALID_ARG);
  NS_ENSURE_ARG_POINTER(_resultCount);
  NS_ENSURE_ARG_POINTER(_results);

  *_resultCount = 0;
  *_results = nsnull;
  nsCOMArray<nsIURI> results;

  nsresult rv = GetPagesWithAnnotationCOMArray(aName, &results);
  NS_ENSURE_SUCCESS(rv, rv);

  // Convert to raw array.
  if (results.Count() == 0)
    return NS_OK;

  *_results = static_cast<nsIURI**>
                         (nsMemory::Alloc(results.Count() * sizeof(nsIURI*)));
  NS_ENSURE_TRUE(*_results, NS_ERROR_OUT_OF_MEMORY);

  *_resultCount = results.Count();
  for (PRUint32 i = 0; i < *_resultCount; i ++) {
    (*_results)[i] = results[i];
    NS_ADDREF((*_results)[i]);
  }

  return NS_OK;
}


nsresult
nsAnnotationService::GetPagesWithAnnotationCOMArray(const nsACString& aName,
                                                    nsCOMArray<nsIURI>* _results)
{
  nsCOMPtr<mozIStorageStatement> stmt = mDB->GetStatement(
    "SELECT h.url "
    "FROM moz_anno_attributes n "
    "JOIN moz_annos a ON n.id = a.anno_attribute_id "
    "JOIN moz_places h ON h.id = a.place_id "
    "WHERE n.name = :anno_name"
  );
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasMore = false;
  while (NS_SUCCEEDED(rv = stmt->ExecuteStep(&hasMore)) &&
         hasMore) {
    nsCAutoString uristring;
    rv = stmt->GetUTF8String(0, uristring);
    NS_ENSURE_SUCCESS(rv, rv);

    // convert to a URI, in case of some invalid URI, just ignore this row
    // so we can mostly continue.
    nsCOMPtr<nsIURI> uri;
    rv = NS_NewURI(getter_AddRefs(uri), uristring);
    if (NS_FAILED(rv))
      continue;

    bool added = _results->AppendObject(uri);
    NS_ENSURE_TRUE(added, NS_ERROR_OUT_OF_MEMORY);
  }

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetItemsWithAnnotation(const nsACString& aName,
                                            PRUint32* _resultCount,
                                            PRInt64** _results)
{
  NS_ENSURE_TRUE(!aName.IsEmpty(), NS_ERROR_INVALID_ARG);
  NS_ENSURE_ARG_POINTER(_resultCount);
  NS_ENSURE_ARG_POINTER(_results);

  *_resultCount = 0;
  *_results = nsnull;
  nsTArray<PRInt64> results;

  nsresult rv = GetItemsWithAnnotationTArray(aName, &results);
  NS_ENSURE_SUCCESS(rv, rv);

  // Convert to raw array.
  if (results.Length() == 0)
    return NS_OK;

  *_results = static_cast<PRInt64*>
                         (nsMemory::Alloc(results.Length() * sizeof(PRInt64)));
  NS_ENSURE_TRUE(*_results, NS_ERROR_OUT_OF_MEMORY);

  *_resultCount = results.Length();
  for (PRUint32 i = 0; i < *_resultCount; i ++) {
    (*_results)[i] = results[i];
  }

  return NS_OK;
}


nsresult
nsAnnotationService::GetItemsWithAnnotationTArray(const nsACString& aName,
                                                  nsTArray<PRInt64>* _results)
{
  nsCOMPtr<mozIStorageStatement> stmt = mDB->GetStatement(
    "SELECT a.item_id "
    "FROM moz_anno_attributes n "
    "JOIN moz_items_annos a ON n.id = a.anno_attribute_id "
    "WHERE n.name = :anno_name"
  );
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper scoper(stmt);

  nsresult rv = stmt->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasMore = false;
  while (NS_SUCCEEDED(stmt->ExecuteStep(&hasMore)) &&
         hasMore) {
    if (!_results->AppendElement(stmt->AsInt64(0)))
      return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetPageAnnotationNames(nsIURI* aURI,
                                            PRUint32* _count,
                                            nsIVariant*** _result)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_count);
  NS_ENSURE_ARG_POINTER(_result);

  *_count = 0;
  *_result = nsnull;

  nsTArray<nsCString> names;
  nsresult rv = GetAnnotationNamesTArray(aURI, 0, &names);
  NS_ENSURE_SUCCESS(rv, rv);

  if (names.Length() == 0)
    return NS_OK;

  *_result = static_cast<nsIVariant**>
                        (nsMemory::Alloc(sizeof(nsIVariant*) * names.Length()));
  NS_ENSURE_TRUE(*_result, NS_ERROR_OUT_OF_MEMORY);

  for (PRUint32 i = 0; i < names.Length(); i ++) {
    nsCOMPtr<nsIWritableVariant> var = new nsVariant();
    if (!var) {
      // need to release all the variants we've already created
      for (PRUint32 j = 0; j < i; j ++)
        NS_RELEASE((*_result)[j]);
      nsMemory::Free(*_result);
      *_result = nsnull;
      return NS_ERROR_OUT_OF_MEMORY;
    }
    var->SetAsAUTF8String(names[i]);
    NS_ADDREF((*_result)[i] = var);
  }
  *_count = names.Length();

  return NS_OK;
}


nsresult
nsAnnotationService::GetAnnotationNamesTArray(nsIURI* aURI,
                                              PRInt64 aItemId,
                                              nsTArray<nsCString>* _result)
{
  _result->Clear();

  bool isItemAnnotation = (aItemId > 0);
  nsCOMPtr<mozIStorageStatement> statement;
  if (isItemAnnotation) {
    statement = mDB->GetStatement(
      "SELECT n.name "
      "FROM moz_anno_attributes n "
      "JOIN moz_items_annos a ON a.anno_attribute_id = n.id "
      "WHERE a.item_id = :item_id"
    );
  }
  else {
    statement = mDB->GetStatement(
      "SELECT n.name "
      "FROM moz_anno_attributes n "
      "JOIN moz_annos a ON a.anno_attribute_id = n.id "
      "JOIN moz_places h ON h.id = a.place_id "
      "WHERE h.url = :page_url"
    );
  }
  NS_ENSURE_STATE(statement);
  mozStorageStatementScoper scoper(statement);

  nsresult rv;
  if (isItemAnnotation)
    rv = statement->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  else
    rv = URIBinder::Bind(statement, NS_LITERAL_CSTRING("page_url"), aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult = false;
  while (NS_SUCCEEDED(statement->ExecuteStep(&hasResult)) &&
         hasResult) {
    nsCAutoString name;
    rv = statement->GetUTF8String(0, name);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!_result->AppendElement(name))
      return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::GetItemAnnotationNames(PRInt64 aItemId,
                                            PRUint32* _count,
                                            nsIVariant*** _result)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_count);
  NS_ENSURE_ARG_POINTER(_result);

  *_count = 0;
  *_result = nsnull;

  nsTArray<nsCString> names;
  nsresult rv = GetAnnotationNamesTArray(nsnull, aItemId, &names);
  NS_ENSURE_SUCCESS(rv, rv);

  if (names.Length() == 0)
    return NS_OK;

  *_result = static_cast<nsIVariant**>
                        (nsMemory::Alloc(sizeof(nsIVariant*) * names.Length()));
  NS_ENSURE_TRUE(*_result, NS_ERROR_OUT_OF_MEMORY);

  for (PRUint32 i = 0; i < names.Length(); i ++) {
    nsCOMPtr<nsIWritableVariant> var = new nsVariant();
    if (!var) {
      // need to release all the variants we've already created
      for (PRUint32 j = 0; j < i; j ++)
        NS_RELEASE((*_result)[j]);
      nsMemory::Free(*_result);
      *_result = nsnull;
      return NS_ERROR_OUT_OF_MEMORY;
    }
    var->SetAsAUTF8String(names[i]);
    NS_ADDREF((*_result)[i] = var);
  }
  *_count = names.Length();

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::PageHasAnnotation(nsIURI* aURI,
                                       const nsACString& aName,
                                       bool* _retval)
{
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv = HasAnnotationInternal(aURI, 0, aName, _retval);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::ItemHasAnnotation(PRInt64 aItemId,
                                       const nsACString& aName,
                                       bool* _retval)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);
  NS_ENSURE_ARG_POINTER(_retval);

  nsresult rv = HasAnnotationInternal(nsnull, aItemId, aName, _retval);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


/**
 * @note We don't remove anything from the moz_anno_attributes table. If we
 *       delete the last item of a given name, that item really should go away.
 *       It will be cleaned up by expiration.
 */
nsresult
nsAnnotationService::RemoveAnnotationInternal(nsIURI* aURI,
                                              PRInt64 aItemId,
                                              const nsACString& aName)
{
  bool isItemAnnotation = (aItemId > 0);
  nsCOMPtr<mozIStorageStatement> statement;
  if (isItemAnnotation) {
    statement = mDB->GetStatement(
      "DELETE FROM moz_items_annos "
      "WHERE item_id = :item_id "
        "AND anno_attribute_id = "
          "(SELECT id FROM moz_anno_attributes WHERE name = :anno_name)"
    );
  }
  else {
    statement = mDB->GetStatement(
      "DELETE FROM moz_annos "
      "WHERE place_id = (SELECT id FROM moz_places WHERE url = :page_url) "
        "AND anno_attribute_id = "
          "(SELECT id FROM moz_anno_attributes WHERE name = :anno_name)"
    );
  }
  NS_ENSURE_STATE(statement);
  mozStorageStatementScoper scoper(statement);

  nsresult rv;
  if (isItemAnnotation)
    rv = statement->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  else
    rv = URIBinder::Bind(statement, NS_LITERAL_CSTRING("page_url"), aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::RemovePageAnnotation(nsIURI* aURI,
                                          const nsACString& aName)
{
  NS_ENSURE_ARG(aURI);

  nsresult rv = RemoveAnnotationInternal(aURI, 0, aName);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnPageAnnotationRemoved(aURI, aName));

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::RemoveItemAnnotation(PRInt64 aItemId,
                                          const nsACString& aName)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  nsresult rv = RemoveAnnotationInternal(nsnull, aItemId, aName);
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnItemAnnotationRemoved(aItemId, aName));

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::RemovePageAnnotations(nsIURI* aURI)
{
  NS_ENSURE_ARG(aURI);

  // Should this be precompiled or a getter?
  nsCOMPtr<mozIStorageStatement> statement = mDB->GetStatement(
    "DELETE FROM moz_annos WHERE place_id = "
      "(SELECT id FROM moz_places WHERE url = :page_url)"
  );
  NS_ENSURE_STATE(statement);
  mozStorageStatementScoper scoper(statement);

  nsresult rv = URIBinder::Bind(statement, NS_LITERAL_CSTRING("page_url"), aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  // Update observers
  NOTIFY_ANNOS_OBSERVERS(OnPageAnnotationRemoved(aURI, EmptyCString()));

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::RemoveItemAnnotations(PRInt64 aItemId)
{
  NS_ENSURE_ARG_MIN(aItemId, 1);

  // Should this be precompiled or a getter?
  nsCOMPtr<mozIStorageStatement> statement = mDB->GetStatement(
    "DELETE FROM moz_items_annos WHERE item_id = :item_id"
  );
  NS_ENSURE_STATE(statement);
  mozStorageStatementScoper scoper(statement);

  nsresult rv = statement->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = statement->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  NOTIFY_ANNOS_OBSERVERS(OnItemAnnotationRemoved(aItemId, EmptyCString()));

  return NS_OK;
}


/**
 * @note If we use annotations for some standard items like GeckoFlags, it
 *       might be a good idea to blacklist these standard annotations from this
 *       copy function.
 */
NS_IMETHODIMP
nsAnnotationService::CopyPageAnnotations(nsIURI* aSourceURI,
                                         nsIURI* aDestURI,
                                         bool aOverwriteDest)
{
  NS_ENSURE_ARG(aSourceURI);
  NS_ENSURE_ARG(aDestURI);

  if (InPrivateBrowsingMode())
    return NS_OK;

  mozStorageTransaction transaction(mDB->MainConn(), false);

  nsCOMPtr<mozIStorageStatement> sourceStmt = mDB->GetStatement(
    "SELECT h.id, n.id, n.name, a2.id "
    "FROM moz_places h "
    "JOIN moz_annos a ON a.place_id = h.id "
    "JOIN moz_anno_attributes n ON n.id = a.anno_attribute_id "
    "LEFT JOIN moz_annos a2 ON a2.place_id = "
      "(SELECT id FROM moz_places WHERE url = :dest_url) "
                          "AND a2.anno_attribute_id = n.id "
    "WHERE url = :source_url"
  );
  NS_ENSURE_STATE(sourceStmt);
  mozStorageStatementScoper sourceScoper(sourceStmt);

  nsresult rv = URIBinder::Bind(sourceStmt, NS_LITERAL_CSTRING("source_url"), aSourceURI);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = URIBinder::Bind(sourceStmt, NS_LITERAL_CSTRING("dest_url"), aDestURI);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<mozIStorageStatement> copyStmt = mDB->GetStatement(
    "INSERT INTO moz_annos "
    "(place_id, anno_attribute_id, mime_type, content, flags, expiration, "
     "type, dateAdded, lastModified) "
    "SELECT (SELECT id FROM moz_places WHERE url = :page_url), "
           "anno_attribute_id, mime_type, content, flags, expiration, type, "
           ":date, :date "
    "FROM moz_annos "
    "WHERE place_id = :page_id "
    "AND anno_attribute_id = :name_id"
  );
  NS_ENSURE_STATE(copyStmt);
  mozStorageStatementScoper copyScoper(copyStmt);

  bool hasResult;
  while (NS_SUCCEEDED(sourceStmt->ExecuteStep(&hasResult)) && hasResult) {
    PRInt64 sourcePlaceId = sourceStmt->AsInt64(0);
    PRInt64 annoNameID = sourceStmt->AsInt64(1);
    nsCAutoString annoName;
    rv = sourceStmt->GetUTF8String(2, annoName);
    NS_ENSURE_SUCCESS(rv, rv);
    PRInt64 annoExistsOnDest = sourceStmt->AsInt64(3);

    if (annoExistsOnDest) {
      if (!aOverwriteDest)
        continue;
      rv = RemovePageAnnotation(aDestURI, annoName);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Copy the annotation.
    mozStorageStatementScoper scoper(copyStmt);
    rv = URIBinder::Bind(copyStmt, NS_LITERAL_CSTRING("page_url"), aDestURI);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = copyStmt->BindInt64ByName(NS_LITERAL_CSTRING("page_id"), sourcePlaceId);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = copyStmt->BindInt64ByName(NS_LITERAL_CSTRING("name_id"), annoNameID);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = copyStmt->BindInt64ByName(NS_LITERAL_CSTRING("date"), PR_Now());
    NS_ENSURE_SUCCESS(rv, rv);

    rv = copyStmt->Execute();
    NS_ENSURE_SUCCESS(rv, rv);

    NOTIFY_ANNOS_OBSERVERS(OnPageAnnotationSet(aDestURI, annoName));
  }

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::CopyItemAnnotations(PRInt64 aSourceItemId,
                                         PRInt64 aDestItemId,
                                         bool aOverwriteDest)
{
  NS_ENSURE_ARG_MIN(aSourceItemId, 1);
  NS_ENSURE_ARG_MIN(aDestItemId, 1);

  mozStorageTransaction transaction(mDB->MainConn(), false);

  nsCOMPtr<mozIStorageStatement> sourceStmt = mDB->GetStatement(
    "SELECT n.id, n.name, a2.id "
    "FROM moz_bookmarks b "
    "JOIN moz_items_annos a ON a.item_id = b.id "
    "JOIN moz_anno_attributes n ON n.id = a.anno_attribute_id "
    "LEFT JOIN moz_items_annos a2 ON a2.item_id = :dest_item_id "
                                "AND a2.anno_attribute_id = n.id "
    "WHERE b.id = :source_item_id"
  );
  NS_ENSURE_STATE(sourceStmt);
  mozStorageStatementScoper sourceScoper(sourceStmt);

  nsresult rv = sourceStmt->BindInt64ByName(NS_LITERAL_CSTRING("source_item_id"), aSourceItemId);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = sourceStmt->BindInt64ByName(NS_LITERAL_CSTRING("dest_item_id"), aDestItemId);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<mozIStorageStatement> copyStmt = mDB->GetStatement(
      "INSERT OR REPLACE INTO moz_items_annos "
      "(item_id, anno_attribute_id, mime_type, content, flags, expiration, "
       "type, dateAdded, lastModified) "
      "SELECT :dest_item_id, anno_attribute_id, mime_type, content, flags, expiration, "
             "type, :date, :date "
      "FROM moz_items_annos "
      "WHERE item_id = :source_item_id "
      "AND anno_attribute_id = :name_id"
  );
  NS_ENSURE_STATE(copyStmt);
  mozStorageStatementScoper copyScoper(copyStmt);

  bool hasResult;
  while (NS_SUCCEEDED(sourceStmt->ExecuteStep(&hasResult)) && hasResult) {
    PRInt64 annoNameID = sourceStmt->AsInt64(0);
    nsCAutoString annoName;
    rv = sourceStmt->GetUTF8String(1, annoName);
    NS_ENSURE_SUCCESS(rv, rv);
    PRInt64 annoExistsOnDest = sourceStmt->AsInt64(2);

    if (annoExistsOnDest) {
      if (!aOverwriteDest)
        continue;
      rv = RemoveItemAnnotation(aDestItemId, annoName);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Copy the annotation.
    mozStorageStatementScoper scoper(copyStmt);
    rv = copyStmt->BindInt64ByName(NS_LITERAL_CSTRING("dest_item_id"), aDestItemId);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = copyStmt->BindInt64ByName(NS_LITERAL_CSTRING("source_item_id"), aSourceItemId);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = copyStmt->BindInt64ByName(NS_LITERAL_CSTRING("name_id"), annoNameID);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = copyStmt->BindInt64ByName(NS_LITERAL_CSTRING("date"), PR_Now());
    NS_ENSURE_SUCCESS(rv, rv);

    rv = copyStmt->Execute();
    NS_ENSURE_SUCCESS(rv, rv);

    NOTIFY_ANNOS_OBSERVERS(OnItemAnnotationSet(aDestItemId, annoName));
  }

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::AddObserver(nsIAnnotationObserver* aObserver)
{
  NS_ENSURE_ARG(aObserver);

  if (mObservers.IndexOfObject(aObserver) >= 0)
    return NS_ERROR_INVALID_ARG; // Already registered.
  if (!mObservers.AppendObject(aObserver))
    return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}


NS_IMETHODIMP
nsAnnotationService::RemoveObserver(nsIAnnotationObserver* aObserver)
{
  NS_ENSURE_ARG(aObserver);

  if (!mObservers.RemoveObject(aObserver))
    return NS_ERROR_INVALID_ARG;
  return NS_OK;
}


// nsAnnotationService::GetAnnotationURI
//
// XXX: does not support item-annotations

NS_IMETHODIMP
nsAnnotationService::GetAnnotationURI(nsIURI* aURI,
                                      const nsACString& aName,
                                      nsIURI** _result)
{
  if (aName.IsEmpty())
    return NS_ERROR_INVALID_ARG;

  nsCAutoString annoSpec;
  nsresult rv = aURI->GetSpec(annoSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString spec;
  spec.AssignLiteral("moz-anno:");
  spec += aName;
  spec += NS_LITERAL_CSTRING(":");
  spec += annoSpec;

  return NS_NewURI(_result, spec);
}


nsresult
nsAnnotationService::HasAnnotationInternal(nsIURI* aURI,
                                           PRInt64 aItemId,
                                           const nsACString& aName,
                                           bool* _hasAnno)
{
  bool isItemAnnotation = (aItemId > 0);
  nsCOMPtr<mozIStorageStatement> stmt;
  if (isItemAnnotation) {
    stmt = mDB->GetStatement(
      "SELECT b.id, "
             "(SELECT id FROM moz_anno_attributes WHERE name = :anno_name) AS nameid, "
             "a.id, a.dateAdded "
      "FROM moz_bookmarks b "
      "LEFT JOIN moz_items_annos a ON a.item_id = b.id "
                                 "AND a.anno_attribute_id = nameid "
      "WHERE b.id = :item_id"
    );
  }
  else {
    stmt = mDB->GetStatement(
      "SELECT h.id, "
             "(SELECT id FROM moz_anno_attributes WHERE name = :anno_name) AS nameid, "
             "a.id, a.dateAdded "
      "FROM moz_places h "
      "LEFT JOIN moz_annos a ON a.place_id = h.id "
                           "AND a.anno_attribute_id = nameid "
      "WHERE h.url = :page_url"
    );
  }
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper checkAnnoScoper(stmt);

  nsresult rv = stmt->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);
  if (isItemAnnotation)
    rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  else
    rv = URIBinder::Bind(stmt, NS_LITERAL_CSTRING("page_url"), aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!hasResult) {
    // We are trying to get an annotation on an invalid bookmarks or
    // history entry.
    // Here we preserve the old behavior, returning that we don't have the
    // annotation, ignoring the fact itemId is invalid.
    // Otherwise we should return NS_ERROR_INVALID_ARG, but this will somehow
    // break the API.  In future we could want to be pickier.
    *_hasAnno = false;
  }
  else {
    PRInt64 annotationId = stmt->AsInt64(2);
    *_hasAnno = (annotationId > 0);
  }

  return NS_OK;
}


/**
 * This loads the statement and steps it once so you can get data out of it.
 *
 * @note You have to reset the statement when you're done if this succeeds.
 * @throws NS_ERROR_NOT_AVAILABLE if the annotation is not found.
 */

nsresult
nsAnnotationService::StartGetAnnotation(nsIURI* aURI,
                                        PRInt64 aItemId,
                                        const nsACString& aName,
                                        nsCOMPtr<mozIStorageStatement>& aStatement)
{
  bool isItemAnnotation = (aItemId > 0);

  if (isItemAnnotation) {
    aStatement = mDB->GetStatement(
      "SELECT a.id, a.item_id, :anno_name, a.mime_type, a.content, a.flags, "
             "a.expiration, a.type "
      "FROM moz_anno_attributes n "
      "JOIN moz_items_annos a ON a.anno_attribute_id = n.id "
      "WHERE a.item_id = :item_id "
      "AND n.name = :anno_name"
    );
  }
  else {
    aStatement = mDB->GetStatement(
      "SELECT a.id, a.place_id, :anno_name, a.mime_type, a.content, a.flags, "
             "a.expiration, a.type "
      "FROM moz_anno_attributes n "
      "JOIN moz_annos a ON n.id = a.anno_attribute_id "
      "JOIN moz_places h ON h.id = a.place_id "
      "WHERE h.url = :page_url "
        "AND n.name = :anno_name"
    );
  }
  NS_ENSURE_STATE(aStatement);
  mozStorageStatementScoper getAnnoScoper(aStatement);

  nsresult rv;
  if (isItemAnnotation)
    rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  else
    rv = URIBinder::Bind(aStatement, NS_LITERAL_CSTRING("page_url"), aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aStatement->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult = false;
  rv = aStatement->ExecuteStep(&hasResult);
  if (NS_FAILED(rv) || !hasResult)
    return NS_ERROR_NOT_AVAILABLE;

  // on success, DON'T reset the statement, the caller needs to read from it,
  // and it is the caller's job to reset it.
  getAnnoScoper.Abandon();

  return NS_OK;
}


bool
nsAnnotationService::InPrivateBrowsingMode() const
{
  nsNavHistory* history = nsNavHistory::GetHistoryService();
  return history && history->InPrivateBrowsingMode();
}


/**
 * This does most of the setup work needed to set an annotation, except for
 * binding the the actual value and MIME type and executing the statement.
 * It will either update an existing annotation or insert a new one.
 *
 * @note The aStatement RESULT IS NOT ADDREFED.  This is just one of the class
 *       vars, which control its scope.  DO NOT RELEASE.
 *       The caller must take care of resetting the statement if this succeeds.
 */
nsresult
nsAnnotationService::StartSetAnnotation(nsIURI* aURI,
                                        PRInt64 aItemId,
                                        const nsACString& aName,
                                        PRInt32 aFlags,
                                        PRUint16 aExpiration,
                                        PRUint16 aType,
                                        nsCOMPtr<mozIStorageStatement>& aStatement)
{
  bool isItemAnnotation = (aItemId > 0);

  if (aExpiration == EXPIRE_SESSION) {
    mHasSessionAnnotations = true;
  }

  // Ensure the annotation name exists.
  nsCOMPtr<mozIStorageStatement> addNameStmt = mDB->GetStatement(
    "INSERT OR IGNORE INTO moz_anno_attributes (name) VALUES (:anno_name)"
  );
  NS_ENSURE_STATE(addNameStmt);
  mozStorageStatementScoper scoper(addNameStmt);

  nsresult rv = addNameStmt->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = addNameStmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  // We have to check 2 things:
  // - if the annotation already exists we should update it.
  // - we should not allow setting annotations on invalid URIs or itemIds.
  // This query will tell us:
  // - whether the item or page exists.
  // - whether the annotation already exists.
  // - the nameID associated with the annotation name.
  // - the id and dateAdded of the old annotation, if it exists.
  nsCOMPtr<mozIStorageStatement> stmt;
  if (isItemAnnotation) {
    stmt = mDB->GetStatement(
      "SELECT b.id, "
             "(SELECT id FROM moz_anno_attributes WHERE name = :anno_name) AS nameid, "
             "a.id, a.dateAdded "
      "FROM moz_bookmarks b "
      "LEFT JOIN moz_items_annos a ON a.item_id = b.id "
                                 "AND a.anno_attribute_id = nameid "
      "WHERE b.id = :item_id"
    );
  }
  else {
    stmt = mDB->GetStatement(
      "SELECT h.id, "
             "(SELECT id FROM moz_anno_attributes WHERE name = :anno_name) AS nameid, "
             "a.id, a.dateAdded "
      "FROM moz_places h "
      "LEFT JOIN moz_annos a ON a.place_id = h.id "
                           "AND a.anno_attribute_id = nameid "
      "WHERE h.url = :page_url"
    );
  }
  NS_ENSURE_STATE(stmt);
  mozStorageStatementScoper checkAnnoScoper(stmt);

  rv = stmt->BindUTF8StringByName(NS_LITERAL_CSTRING("anno_name"), aName);
  NS_ENSURE_SUCCESS(rv, rv);
  if (isItemAnnotation)
    rv = stmt->BindInt64ByName(NS_LITERAL_CSTRING("item_id"), aItemId);
  else
    rv = URIBinder::Bind(stmt, NS_LITERAL_CSTRING("page_url"), aURI);
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!hasResult) {
    // We are trying to create an annotation on an invalid bookmark
    // or history entry.
    return NS_ERROR_INVALID_ARG;
  }

  PRInt64 fkId = stmt->AsInt64(0);
  PRInt64 nameID = stmt->AsInt64(1);
  PRInt64 oldAnnoId = stmt->AsInt64(2);
  PRInt64 oldAnnoDate = stmt->AsInt64(3);

  if (isItemAnnotation) {
    aStatement = mDB->GetStatement(
      "INSERT OR REPLACE INTO moz_items_annos "
        "(id, item_id, anno_attribute_id, mime_type, content, flags, "
         "expiration, type, dateAdded, lastModified) "
      "VALUES (:id, :fk, :name_id, :mime_type, :content, :flags, "
      ":expiration, :type, :date_added, :last_modified)"
    );
  }
  else {
    aStatement = mDB->GetStatement(
      "INSERT OR REPLACE INTO moz_annos "
        "(id, place_id, anno_attribute_id, mime_type, content, flags, "
         "expiration, type, dateAdded, lastModified) "
      "VALUES (:id, :fk, :name_id, :mime_type, :content, :flags, "
      ":expiration, :type, :date_added, :last_modified)"
    );
  }
  NS_ENSURE_STATE(aStatement);
  mozStorageStatementScoper setAnnoScoper(aStatement);

  // Don't replace existing annotations.
  if (oldAnnoId > 0) {
    rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("id"), oldAnnoId);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("date_added"), oldAnnoDate);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else {
    rv = aStatement->BindNullByName(NS_LITERAL_CSTRING("id"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("date_added"), PR_Now());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("fk"), fkId);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("name_id"), nameID);
  NS_ENSURE_SUCCESS(rv, rv);
  // MimeType and Content will be bound by the caller.
  rv = aStatement->BindInt32ByName(NS_LITERAL_CSTRING("flags"), aFlags);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aStatement->BindInt32ByName(NS_LITERAL_CSTRING("expiration"), aExpiration);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aStatement->BindInt32ByName(NS_LITERAL_CSTRING("type"), aType);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aStatement->BindInt64ByName(NS_LITERAL_CSTRING("last_modified"), PR_Now());
  NS_ENSURE_SUCCESS(rv, rv);

  // on success, leave the statement open, the caller will set the value
  // and MIME type and execute the statement
  setAnnoScoper.Abandon();

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// nsIObserver

NS_IMETHODIMP
nsAnnotationService::Observe(nsISupports *aSubject,
                             const char *aTopic,
                             const PRUnichar *aData)
{
  NS_ASSERTION(NS_IsMainThread(), "This can only be called on the main thread");

  if (strcmp(aTopic, TOPIC_PLACES_SHUTDOWN) == 0) {
    // Remove all session annotations, if any.
    if (mHasSessionAnnotations) {
      nsCOMPtr<mozIStorageAsyncStatement> pageAnnoStmt = mDB->GetAsyncStatement(
        "DELETE FROM moz_annos WHERE expiration = :expire_session"
      );
      NS_ENSURE_STATE(pageAnnoStmt);
      nsresult rv = pageAnnoStmt->BindInt32ByName(NS_LITERAL_CSTRING("expire_session"),
                                                  EXPIRE_SESSION);
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<mozIStorageAsyncStatement> itemAnnoStmt = mDB->GetAsyncStatement(
        "DELETE FROM moz_items_annos WHERE expiration = :expire_session"
      );
      NS_ENSURE_STATE(itemAnnoStmt);
      rv = itemAnnoStmt->BindInt32ByName(NS_LITERAL_CSTRING("expire_session"),
                                         EXPIRE_SESSION);
      NS_ENSURE_SUCCESS(rv, rv);

      mozIStorageBaseStatement *stmts[] = {
        pageAnnoStmt.get()
      , itemAnnoStmt.get()
      };

      nsCOMPtr<mozIStoragePendingStatement> ps;
      rv = mDB->MainConn()->ExecuteAsync(stmts, ArrayLength(stmts), nsnull,
                                         getter_AddRefs(ps));
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}
