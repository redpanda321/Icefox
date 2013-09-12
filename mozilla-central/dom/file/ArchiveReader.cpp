/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ArchiveReader.h"
#include "ArchiveRequest.h"
#include "ArchiveEvent.h"
#include "ArchiveZipEvent.h"

#include "nsContentUtils.h"
#include "nsLayoutStatics.h"
#include "nsDOMClassInfoID.h"

#include "nsIURI.h"
#include "nsNetUtil.h"

#include "mozilla/Preferences.h"

USING_FILE_NAMESPACE

ArchiveReader::ArchiveReader()
: mBlob(nullptr),
  mWindow(nullptr),
  mStatus(NOT_STARTED)
{
  MOZ_COUNT_CTOR(ArchiveReader);
  nsLayoutStatics::AddRef();
}

ArchiveReader::~ArchiveReader()
{
  MOZ_COUNT_DTOR(ArchiveReader);
  nsLayoutStatics::Release();
}

bool
ArchiveReader::PrefEnabled()
{
  return Preferences::GetBool("dom.archivereader.enabled", true);
}

NS_IMETHODIMP
ArchiveReader::Initialize(nsISupports* aOwner,
                          JSContext* aCx,
                          JSObject* aObj,
                          uint32_t aArgc,
                          JS::Value* aArgv)
{
  NS_ENSURE_TRUE(aArgc == 1 || aArgc == 2, NS_ERROR_INVALID_ARG);

  if (!PrefEnabled()) {
    return NS_ERROR_UNEXPECTED;
  }

  // We expect to get a Blob object
  if (!aArgv[0].isObject()) {
    return NS_ERROR_INVALID_ARG; // We're not interested
  }

  JSObject* obj = &aArgv[0].toObject();

  nsCOMPtr<nsIDOMBlob> blob;
  blob = do_QueryInterface(nsContentUtils::XPConnect()->GetNativeOfWrapper(aCx, obj));
  if (!blob) {
    return NS_ERROR_INVALID_ARG;
  }

  // Extra param is an object
  if (aArgc > 1) {
    nsresult rv = mOptions.Init(aCx, &aArgv[1]);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mWindow = do_QueryInterface(aOwner);
  if (!mWindow) {
    return NS_ERROR_UNEXPECTED;
  }

  mBlob = blob;

  return NS_OK;
}

nsresult
ArchiveReader::RegisterRequest(ArchiveRequest* aRequest)
{
  switch (mStatus) {
    // Append to the list and let's start to work:
    case NOT_STARTED:
      mRequests.AppendElement(aRequest);
      return OpenArchive();

    // Just append to the list:
    case WORKING:
      mRequests.AppendElement(aRequest);
      return NS_OK;

    // Return data!
    case READY:
      RequestReady(aRequest);
      return NS_OK;
  }

  NS_ASSERTION(false, "unexpected mStatus value");
  return NS_OK;
}

// This returns the input stream
nsresult
ArchiveReader::GetInputStream(nsIInputStream** aInputStream)
{
  // Getting the input stream
  mBlob->GetInternalStream(aInputStream);
  NS_ENSURE_TRUE(*aInputStream, NS_ERROR_UNEXPECTED);
  return NS_OK;
}

nsresult
ArchiveReader::GetSize(uint64_t* aSize)
{
  nsresult rv = mBlob->GetSize(aSize);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

// Here we open the archive:
nsresult
ArchiveReader::OpenArchive()
{
  mStatus = WORKING;
  nsresult rv;

  // Target:
  nsCOMPtr<nsIEventTarget> target = do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
  NS_ASSERTION(target, "Must have stream transport service");

  // Here a Event to make everything async:
  nsRefPtr<ArchiveReaderEvent> event;

  /* FIXME: If we want to support more than 1 format we should check the content type here: */
  event = new ArchiveReaderZipEvent(this, mOptions);
  rv = target->Dispatch(event, NS_DISPATCH_NORMAL);
  NS_ENSURE_SUCCESS(rv, rv);

  // In order to be sure that this object exists when the event finishes its task,
  // we increase the refcount here:
  AddRef();

  return NS_OK;
}

// Data received from the dispatched event:
void
ArchiveReader::Ready(nsTArray<nsCOMPtr<nsIDOMFile> >& aFileList,
                     nsresult aStatus)
{
  mStatus = READY;
 
  // Let's store the values:
  mData.fileList = aFileList;
  mData.status = aStatus;

  // Propagate the results:
  for (uint32_t index = 0; index < mRequests.Length(); ++index) {
    nsRefPtr<ArchiveRequest> request = mRequests[index];
    RequestReady(request);
  }

  mRequests.Clear();

  // The async operation is concluded, we can decrease the reference:
  Release();
}

void
ArchiveReader::RequestReady(ArchiveRequest* aRequest)
{
  // The request will do the rest:
  aRequest->ReaderReady(mData.fileList, mData.status);
}

/* nsIDOMArchiveRequest getFilenames (); */
NS_IMETHODIMP
ArchiveReader::GetFilenames(nsIDOMArchiveRequest** _retval)
{
  nsRefPtr<ArchiveRequest> request = GenerateArchiveRequest();
  request->OpGetFilenames();

  request.forget(_retval);
  return NS_OK;
}

/* nsIDOMArchiveRequest getFile (in DOMString filename); */
NS_IMETHODIMP
ArchiveReader::GetFile(const nsAString& filename,
                       nsIDOMArchiveRequest** _retval)
{
  nsRefPtr<ArchiveRequest> request = GenerateArchiveRequest();
  request->OpGetFile(filename);

  request.forget(_retval);
  return NS_OK;
}

/* nsIDOMArchiveRequest getFiles (); */
NS_IMETHODIMP
ArchiveReader::GetFiles(nsIDOMArchiveRequest** _retval)
{
  nsRefPtr<ArchiveRequest> request = GenerateArchiveRequest();
  request->OpGetFiles();

  request.forget(_retval);
  return NS_OK;
}

already_AddRefed<ArchiveRequest>
ArchiveReader::GenerateArchiveRequest()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  return ArchiveRequest::Create(mWindow, this);
}

NS_IMPL_CYCLE_COLLECTION_4(ArchiveReader,
                           mBlob,
                           mWindow,
                           mData.fileList,
                           mRequests)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ArchiveReader)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDOMArchiveReader)
  NS_INTERFACE_MAP_ENTRY(nsIJSNativeInitializer)
  NS_INTERFACE_MAP_ENTRY(nsIDOMArchiveReader)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(ArchiveReader)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ArchiveReader)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ArchiveReader)

DOMCI_DATA(ArchiveReader, ArchiveReader)
