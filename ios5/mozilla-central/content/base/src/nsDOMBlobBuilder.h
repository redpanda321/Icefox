/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDOMBlobBuilder_h
#define nsDOMBlobBuilder_h

#include "nsDOMFile.h"

#include "mozilla/CheckedInt.h"
#include "mozilla/Attributes.h"

using namespace mozilla;

class nsDOMMultipartFile : public nsDOMFile,
                           public nsIJSNativeInitializer
{
public:
  // Create as a file
  nsDOMMultipartFile(nsTArray<nsCOMPtr<nsIDOMBlob> > aBlobs,
                     const nsAString& aName,
                     const nsAString& aContentType)
    : nsDOMFile(aName, aContentType, UINT64_MAX),
      mBlobs(aBlobs)
  {
  }

  // Create as a blob
  nsDOMMultipartFile(nsTArray<nsCOMPtr<nsIDOMBlob> > aBlobs,
                     const nsAString& aContentType)
    : nsDOMFile(aContentType, UINT64_MAX),
      mBlobs(aBlobs)
  {
  }

  // Create as a file to be later initialized
  nsDOMMultipartFile(const nsAString& aName)
    : nsDOMFile(aName, EmptyString(), UINT64_MAX)
  {
  }

  // Create as a blob to be later initialized
  nsDOMMultipartFile()
    : nsDOMFile(EmptyString(), UINT64_MAX)
  {
  }

  NS_DECL_ISUPPORTS_INHERITED

  // nsIJSNativeInitializer
  NS_IMETHOD Initialize(nsISupports* aOwner,
                        JSContext* aCx,
                        JSObject* aObj,
                        PRUint32 aArgc,
                        jsval* aArgv);

  typedef nsIDOMBlob* (*UnwrapFuncPtr)(JSContext*, JSObject*);
  nsresult InitInternal(JSContext* aCx,
                        PRUint32 aArgc,
                        jsval* aArgv,
                        UnwrapFuncPtr aUnwrapFunc);

  already_AddRefed<nsIDOMBlob>
  CreateSlice(PRUint64 aStart, PRUint64 aLength, const nsAString& aContentType);

  NS_IMETHOD GetSize(PRUint64*);
  NS_IMETHOD GetInternalStream(nsIInputStream**);

  static nsresult
  NewFile(const nsAString& aName, nsISupports* *aNewObject);

  // DOMClassInfo constructor (for Blob([b1, "foo"], { type: "image/png" }))
  static nsresult
  NewBlob(nsISupports* *aNewObject);

  virtual const nsTArray<nsCOMPtr<nsIDOMBlob> >*
  GetSubBlobs() const { return &mBlobs; }

protected:
  nsTArray<nsCOMPtr<nsIDOMBlob> > mBlobs;
};

class BlobSet {
public:
  BlobSet()
    : mData(nsnull), mDataLen(0), mDataBufferLen(0)
  {}

  nsresult AppendVoidPtr(const void* aData, PRUint32 aLength);
  nsresult AppendString(JSString* aString, bool nativeEOL, JSContext* aCx);
  nsresult AppendBlob(nsIDOMBlob* aBlob);
  nsresult AppendArrayBuffer(JSObject* aBuffer, JSContext *aCx);
  nsresult AppendBlobs(const nsTArray<nsCOMPtr<nsIDOMBlob> >& aBlob);

  nsTArray<nsCOMPtr<nsIDOMBlob> >& GetBlobs() { Flush(); return mBlobs; }

protected:
  bool ExpandBufferSize(PRUint64 aSize)
  {
    if (mDataBufferLen >= mDataLen + aSize) {
      mDataLen += aSize;
      return true;
    }

    // Start at 1 or we'll loop forever.
    CheckedUint32 bufferLen = NS_MAX<PRUint32>(mDataBufferLen, 1);
    while (bufferLen.isValid() && bufferLen.value() < mDataLen + aSize)
      bufferLen *= 2;

    if (!bufferLen.isValid())
      return false;

    // PR_ memory functions are still fallible
    void* data = PR_Realloc(mData, bufferLen.value());
    if (!data)
      return false;

    mData = data;
    mDataBufferLen = bufferLen.value();
    mDataLen += aSize;
    return true;
  }

  void Flush() {
    if (mData) {
      // If we have some data, create a blob for it
      // and put it on the stack

      nsCOMPtr<nsIDOMBlob> blob =
        new nsDOMMemoryFile(mData, mDataLen, EmptyString(), EmptyString());
      mBlobs.AppendElement(blob);
      mData = nsnull; // The nsDOMMemoryFile takes ownership of the buffer
      mDataLen = 0;
      mDataBufferLen = 0;
    }
  }

  nsTArray<nsCOMPtr<nsIDOMBlob> > mBlobs;
  void* mData;
  PRUint64 mDataLen;
  PRUint64 mDataBufferLen;
};

class nsDOMBlobBuilder MOZ_FINAL : public nsIDOMMozBlobBuilder,
                                   public nsIJSNativeInitializer
{
public:
  nsDOMBlobBuilder()
    : mBlobSet()
  {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSIDOMMOZBLOBBUILDER

  nsresult AppendVoidPtr(const void* aData, PRUint32 aLength)
  { return mBlobSet.AppendVoidPtr(aData, aLength); }

  nsresult GetBlobInternal(const nsAString& aContentType,
                           bool aClearBuffer, nsIDOMBlob** aBlob);

  // nsIJSNativeInitializer
  NS_IMETHOD Initialize(nsISupports* aOwner,
                        JSContext* aCx,
                        JSObject* aObj,
                        PRUint32 aArgc,
                        jsval* aArgv);
protected:
  BlobSet mBlobSet;
};

#endif
