/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHttpConnectionInfo.h"

void
nsHttpConnectionInfo::SetOriginServer(const nsACString &host, int32_t port)
{
    mHost = host;
    mPort = port == -1 ? DefaultPort() : port;

    //
    // build hash key:
    //
    // the hash key uniquely identifies the connection type.  two connections
    // are "equal" if they end up talking the same protocol to the same server
    // and are both used for anonymous or non-anonymous connection only;
    // anonymity of the connection is setup later from nsHttpChannel::AsyncOpen
    // where we know we use anonymous connection (LOAD_ANONYMOUS load flag)
    //

    const char *keyHost;
    int32_t keyPort;

    if (mUsingHttpProxy && !mUsingConnect) {
        keyHost = ProxyHost();
        keyPort = ProxyPort();
    }
    else {
        keyHost = Host();
        keyPort = Port();
    }

    mHashKey.AssignLiteral("....");
    mHashKey.Append(keyHost);
    mHashKey.Append(':');
    mHashKey.AppendInt(keyPort);

    if (mUsingHttpProxy)
        mHashKey.SetCharAt('P', 0);
    if (mUsingSSL)
        mHashKey.SetCharAt('S', 1);

    // NOTE: for transparent proxies (e.g., SOCKS) we need to encode the proxy
    // info in the hash key (this ensures that we will continue to speak the
    // right protocol even if our proxy preferences change).
    //
    // NOTE: for SSL tunnels add the proxy information to the cache key.
    // We cannot use the proxy as the host parameter (as we do for non SSL)
    // because this is a single host tunnel, but we need to include the proxy
    // information so that a change in proxy config will mean this connection
    // is not reused

    if ((!mUsingHttpProxy && ProxyHost()) ||
        (mUsingHttpProxy && mUsingConnect)) {
        mHashKey.AppendLiteral(" (");
        mHashKey.Append(ProxyType());
        mHashKey.Append(':');
        mHashKey.Append(ProxyHost());
        mHashKey.Append(':');
        mHashKey.AppendInt(ProxyPort());
        mHashKey.Append(')');
    }
}

nsHttpConnectionInfo*
nsHttpConnectionInfo::Clone() const
{
    nsHttpConnectionInfo* clone = new nsHttpConnectionInfo(mHost, mPort, mProxyInfo, mUsingSSL);

    // Make sure the anonymous and private flags are transferred!
    clone->SetAnonymous(GetAnonymous());
    clone->SetPrivate(GetPrivate());

    return clone;
}

bool
nsHttpConnectionInfo::UsingProxy()
{
    if (!mProxyInfo)
        return false;
    return !mProxyInfo->IsDirect();
}

