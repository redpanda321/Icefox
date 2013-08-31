/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_file_domarchivereader_h__
#define mozilla_dom_file_domarchivereader_h__

#include "nsIDOMArchiveReader.h"
#include "nsIJSNativeInitializer.h"

#include "FileCommon.h"

#include "nsCOMArray.h"
#include "nsIChannel.h"
#include "nsIDOMFile.h"
#include "mozilla/Attributes.h"
#include "DictionaryHelpers.h"

BEGIN_FILE_NAMESPACE

class ArchiveRequest;

/**
 * This is the ArchiveReader object
 */
class ArchiveReader MOZ_FINAL : public nsIDOMArchiveReader,
                                public nsIJSNativeInitializer
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  NS_DECL_NSIDOMARCHIVEREADER

  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(ArchiveReader,
                                           nsIDOMArchiveReader)

  ArchiveReader();

  // nsIJSNativeInitializer
  NS_IMETHOD Initialize(nsISupports* aOwner,
                        JSContext* aCx,
                        JSObject* aObj,
                        uint32_t aArgc,
                        JS::Value* aArgv);

  nsresult GetInputStream(nsIInputStream** aInputStream);
  nsresult GetSize(uint64_t* aSize);

  static bool PrefEnabled();

public: // for the ArchiveRequest:
  nsresult RegisterRequest(ArchiveRequest* aRequest);

public: // For events:
  void Ready(nsTArray<nsCOMPtr<nsIDOMFile> >& aFileList,
             nsresult aStatus);

private:
  ~ArchiveReader();

  already_AddRefed<ArchiveRequest> GenerateArchiveRequest();

  nsresult OpenArchive();

  void RequestReady(ArchiveRequest* aRequest);

protected:
  // The archive blob/file
  nsCOMPtr<nsIDOMBlob> mBlob;

  // The window is needed by the requests
  nsCOMPtr<nsIDOMWindow> mWindow;

  // Are we ready to return data?
  enum {
    NOT_STARTED = 0,
    WORKING,
    READY
  } mStatus;

  // State of the read:
  enum {
    Header, // We are collecting the header: 30bytes
    Name,   // The name length is contained in the header
    Data,   // The length of the data segment COULD be written in the header
    Search  // ... if the data length is unknown (== 0) we wait until we read a new header 
  } mReadStatus;

  // List of requests to be processed
  nsTArray<nsRefPtr<ArchiveRequest> > mRequests;

  // Everything related to the blobs and the status:
  struct {
    nsTArray<nsCOMPtr<nsIDOMFile> > fileList;
    nsresult status;
  } mData;

  ArchiveReaderOptions mOptions;
};

END_FILE_NAMESPACE

#endif // mozilla_dom_file_domarchivereader_h__
