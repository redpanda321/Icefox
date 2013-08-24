/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsNativeModuleLoader_h__
#define nsNativeModuleLoader_h__

#include "nsISupports.h"
#include "mozilla/ModuleLoader.h"
#include "nsDataHashtable.h"
#include "nsHashKeys.h"
#include "mozilla/Module.h"
#include "prlink.h"

class nsNativeModuleLoader : public mozilla::ModuleLoader
{
 public:
    NS_DECL_ISUPPORTS_INHERITED

    nsNativeModuleLoader() {}
    ~nsNativeModuleLoader() {}

    NS_OVERRIDE virtual const mozilla::Module* LoadModule(mozilla::FileLocation &aFile);

    nsresult Init();

    void UnloadLibraries();

 private:
    struct NativeLoadData
    {
        NativeLoadData()
            : module(NULL)
            , library(NULL)
        { }

        const mozilla::Module* module;
        PRLibrary* library;
    };

    static PLDHashOperator
    ReleaserFunc(nsIHashable* aHashedFile, NativeLoadData &aLoadData, void*);

    static PLDHashOperator
    UnloaderFunc(nsIHashable* aHashedFile, NativeLoadData &aLoadData, void*);

    nsDataHashtable<nsHashableHashKey, NativeLoadData> mLibraries;
};

#endif /* nsNativeModuleLoader_h__ */
