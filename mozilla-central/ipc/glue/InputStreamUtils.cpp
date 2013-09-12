/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InputStreamUtils.h"

#include "nsIIPCSerializableInputStream.h"

#include "mozilla/Assertions.h"
#include "mozilla/dom/ipc/Blob.h"
#include "nsComponentManagerUtils.h"
#include "nsDebug.h"
#include "nsID.h"
#include "nsIDOMFile.h"
#include "nsIXULRuntime.h"
#include "nsMIMEInputStream.h"
#include "nsMultiplexInputStream.h"
#include "nsNetCID.h"
#include "nsStringStream.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

using namespace mozilla::ipc;
using mozilla::dom::BlobChild;
using mozilla::dom::BlobParent;

namespace {

NS_DEFINE_CID(kStringInputStreamCID, NS_STRINGINPUTSTREAM_CID);
NS_DEFINE_CID(kFileInputStreamCID, NS_LOCALFILEINPUTSTREAM_CID);
NS_DEFINE_CID(kPartialFileInputStreamCID, NS_PARTIALLOCALFILEINPUTSTREAM_CID);
NS_DEFINE_CID(kBufferedInputStreamCID, NS_BUFFEREDINPUTSTREAM_CID);
NS_DEFINE_CID(kMIMEInputStreamCID, NS_MIMEINPUTSTREAM_CID);
NS_DEFINE_CID(kMultiplexInputStreamCID, NS_MULTIPLEXINPUTSTREAM_CID);

} // anonymous namespace

namespace mozilla {
namespace ipc {

void
SerializeInputStream(nsIInputStream* aInputStream,
                     InputStreamParams& aParams)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aInputStream);

  nsCOMPtr<nsIIPCSerializableInputStream> serializable =
    do_QueryInterface(aInputStream);
  if (!serializable) {
    MOZ_NOT_REACHED("Input stream is not serializable!");
  }

  serializable->Serialize(aParams);

  if (aParams.type() == InputStreamParams::T__None) {
    MOZ_NOT_REACHED("Serialize failed!");
  }
}

void
SerializeInputStream(nsIInputStream* aInputStream,
                     OptionalInputStreamParams& aParams)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (aInputStream) {
    InputStreamParams params;
    SerializeInputStream(aInputStream, params);
    aParams = params;
  }
  else {
    aParams = mozilla::void_t();
  }
}

already_AddRefed<nsIInputStream>
DeserializeInputStream(const InputStreamParams& aParams)
{
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIIPCSerializableInputStream> serializable;

  switch (aParams.type()) {
    case InputStreamParams::TStringInputStreamParams:
      serializable = do_CreateInstance(kStringInputStreamCID);
      break;

    case InputStreamParams::TFileInputStreamParams:
      serializable = do_CreateInstance(kFileInputStreamCID);
      break;

    case InputStreamParams::TPartialFileInputStreamParams:
      serializable = do_CreateInstance(kPartialFileInputStreamCID);
      break;

    case InputStreamParams::TBufferedInputStreamParams:
      serializable = do_CreateInstance(kBufferedInputStreamCID);
      break;

    case InputStreamParams::TMIMEInputStreamParams:
      serializable = do_CreateInstance(kMIMEInputStreamCID);
      break;

    case InputStreamParams::TMultiplexInputStreamParams:
      serializable = do_CreateInstance(kMultiplexInputStreamCID);
      break;

    // When the input stream already exists in this process, all we need to do
    // is retrieve the original instead of sending any data over the wire.
    case InputStreamParams::TRemoteInputStreamParams: {
      nsCOMPtr<nsIDOMBlob> domBlob;
      const RemoteInputStreamParams& params =
          aParams.get_RemoteInputStreamParams();

      domBlob = params.remoteBlobParent() ?
          static_cast<BlobParent*>(params.remoteBlobParent())->GetBlob() :
          static_cast<BlobChild*>(params.remoteBlobChild())->GetBlob();

      MOZ_ASSERT(domBlob, "Invalid blob contents");

      // If fetching the internal stream fails, we ignore it and return a
      // null stream.
      nsCOMPtr<nsIInputStream> stream;
      nsresult rv = domBlob->GetInternalStream(getter_AddRefs(stream));
      if (NS_FAILED(rv) || !stream) {
        NS_WARNING("Couldn't obtain a valid stream from the blob");
      }
      return stream.forget();
    }

    default:
      MOZ_ASSERT(false, "Unknown params!");
      return nullptr;
  }

  MOZ_ASSERT(serializable);

  if (!serializable->Deserialize(aParams)) {
    MOZ_ASSERT(false, "Deserialize failed!");
    return nullptr;
  }

  nsCOMPtr<nsIInputStream> stream = do_QueryInterface(serializable);
  MOZ_ASSERT(stream);

  return stream.forget();
}

already_AddRefed<nsIInputStream>
DeserializeInputStream(const OptionalInputStreamParams& aParams)
{
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIInputStream> stream;

  switch (aParams.type()) {
    case OptionalInputStreamParams::Tvoid_t:
      // Leave stream null.
      break;

    case OptionalInputStreamParams::TInputStreamParams:
      stream = DeserializeInputStream(aParams.get_InputStreamParams());
      break;

    default:
      MOZ_ASSERT(false, "Unknown params!");
  }

  return stream.forget();
}

} // namespace ipc
} // namespace mozilla
