/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * A service that provides methods for synchronously loading a DOM in various ways.
 */

#ifndef nsSyncLoadService_h__
#define nsSyncLoadService_h__

#include "nscore.h"

class nsIInputStream;
class nsILoadGroup;
class nsIStreamListener;
class nsIURI;
class nsIPrincipal;
class nsIDOMDocument;
class nsIChannel;

class nsSyncLoadService
{
public:
    /**
     * Synchronously load the document from the specified URI.
     *
     * @param aURI URI to load the document from.
     * @param aLoaderPrincipal Principal of loading document. For security
     *                         checks and referrer header. May be null if no
     *                         security checks should be done.
     * @param aLoadGroup The loadgroup to use for loading the document.
     * @param aForceToXML Whether to parse the document as XML, regardless of
     *                    content type.
     * @param aResult [out] The document loaded from the URI.
     */
    static nsresult LoadDocument(nsIURI *aURI, nsIPrincipal *aLoaderPrincipal,
                                 nsILoadGroup *aLoadGroup, bool aForceToXML,
                                 nsIDOMDocument** aResult);

    /**
     * Read input stream aIn in chunks and deliver synchronously to aListener.
     *
     * @param aIn The stream to be read.
     * @param aListener The listener that will receive
     *                  OnStartRequest/OnDataAvailable/OnStopRequest
     *                  notifications.
     * @param aChannel The channel that aIn was opened from.
     */
    static nsresult PushSyncStreamToListener(nsIInputStream* aIn,
                                             nsIStreamListener* aListener,
                                             nsIChannel* aChannel);
};

#endif // nsSyncLoadService_h__
