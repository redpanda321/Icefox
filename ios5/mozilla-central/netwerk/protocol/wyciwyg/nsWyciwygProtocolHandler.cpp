/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWyciwyg.h"
#include "nsWyciwygChannel.h"
#include "nsWyciwygProtocolHandler.h"
#include "nsIURL.h"
#include "nsIComponentManager.h"
#include "nsNetCID.h"
#include "nsServiceManagerUtils.h"
#include "plstr.h"
#include "nsNetUtil.h"

#include "mozilla/net/NeckoChild.h"

using namespace mozilla::net;
#include "mozilla/net/WyciwygChannelChild.h"

////////////////////////////////////////////////////////////////////////////////

nsWyciwygProtocolHandler::nsWyciwygProtocolHandler() 
{
#if defined(PR_LOGGING)
  if (!gWyciwygLog)
    gWyciwygLog = PR_NewLogModule("nsWyciwygChannel");
#endif

  LOG(("Creating nsWyciwygProtocolHandler [this=%x].\n", this));
}

nsWyciwygProtocolHandler::~nsWyciwygProtocolHandler() 
{
  LOG(("Deleting nsWyciwygProtocolHandler [this=%x]\n", this));
}

NS_IMPL_ISUPPORTS1(nsWyciwygProtocolHandler, nsIProtocolHandler)

////////////////////////////////////////////////////////////////////////////////
// nsIProtocolHandler methods:
////////////////////////////////////////////////////////////////////////////////

NS_IMETHODIMP
nsWyciwygProtocolHandler::GetScheme(nsACString &result)
{
  result = "wyciwyg";
  return NS_OK;
}

NS_IMETHODIMP
nsWyciwygProtocolHandler::GetDefaultPort(PRInt32 *result) 
{
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP 
nsWyciwygProtocolHandler::AllowPort(PRInt32 port, const char *scheme, bool *_retval)
{
  // don't override anything.  
  *_retval = false;
  return NS_OK;
}

NS_IMETHODIMP
nsWyciwygProtocolHandler::NewURI(const nsACString &aSpec,
                                 const char *aCharset, // ignored
                                 nsIURI *aBaseURI,
                                 nsIURI **result) 
{
  nsresult rv;

  nsCOMPtr<nsIURI> url = do_CreateInstance(NS_SIMPLEURI_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = url->SetSpec(aSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  *result = url;
  NS_ADDREF(*result);

  return rv;
}

NS_IMETHODIMP
nsWyciwygProtocolHandler::NewChannel(nsIURI* url, nsIChannel* *result)
{
  if (mozilla::net::IsNeckoChild())
    mozilla::net::NeckoChild::InitNeckoChild();

  NS_ENSURE_ARG_POINTER(url);
  nsresult rv;

  nsCOMPtr<nsIWyciwygChannel> channel;
  if (IsNeckoChild()) {
    NS_ENSURE_TRUE(gNeckoChild != nsnull, NS_ERROR_FAILURE);

    WyciwygChannelChild *wcc = static_cast<WyciwygChannelChild *>(
                                 gNeckoChild->SendPWyciwygChannelConstructor());
    if (!wcc)
      return NS_ERROR_OUT_OF_MEMORY;

    channel = wcc;
    rv = wcc->Init(url);
    if (NS_FAILED(rv))
      PWyciwygChannelChild::Send__delete__(wcc);
  } else
  {
    // If original channel used https, make sure PSM is initialized
    // (this may be first channel to load during a session restore)
    nsCAutoString path;
    rv = url->GetPath(path);
    NS_ENSURE_SUCCESS(rv, rv);
    PRInt32 slashIndex = path.FindChar('/', 2);
    if (slashIndex == kNotFound)
      return NS_ERROR_FAILURE;
    if (path.Length() < (PRUint32)slashIndex + 1 + 5)
      return NS_ERROR_FAILURE;
    if (!PL_strncasecmp(path.get() + slashIndex + 1, "https", 5))
      net_EnsurePSMInit();

    nsWyciwygChannel *wc = new nsWyciwygChannel();
    channel = wc;
    rv = wc->Init(url);
  }

  if (NS_FAILED(rv))
    return rv;

  *result = channel.forget().get();
  return NS_OK;
}

NS_IMETHODIMP
nsWyciwygProtocolHandler::GetProtocolFlags(PRUint32 *result) 
{
  // Should this be an an nsINestedURI?  We don't really want random webpages
  // loading these URIs...

  // Note that using URI_INHERITS_SECURITY_CONTEXT here is OK -- untrusted code
  // is not allowed to link to wyciwyg URIs and users shouldn't be able to get
  // at them, and nsDocShell::InternalLoad forbids non-history loads of these
  // URIs.  And when loading from history we end up using the principal from
  // the history entry, which we put there ourselves, so all is ok.
  *result = URI_NORELATIVE | URI_NOAUTH | URI_DANGEROUS_TO_LOAD |
    URI_INHERITS_SECURITY_CONTEXT;
  return NS_OK;
}
