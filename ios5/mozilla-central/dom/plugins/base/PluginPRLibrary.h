/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PluginPRLibrary_h
#define PluginPRLibrary_h 1

#include "mozilla/PluginLibrary.h"
#include "nsNPAPIPlugin.h"
#include "npfunctions.h"

namespace mozilla {

class PluginPRLibrary : public PluginLibrary
{
public:
    PluginPRLibrary(const char* aFilePath, PRLibrary* aLibrary) :
#if defined(XP_UNIX) && !defined(XP_MACOSX)
        mNP_Initialize(nsnull),
#else
        mNP_Initialize(nsnull),
#endif
        mNP_Shutdown(nsnull),
        mNP_GetMIMEDescription(nsnull),
#if defined(XP_UNIX) && !defined(XP_MACOSX)
        mNP_GetValue(nsnull),
#endif
#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
        mNP_GetEntryPoints(nsnull),
#endif
        mNPP_New(nsnull),
        mNPP_GetValue(nsnull),
        mNPP_ClearSiteData(nsnull),
        mNPP_GetSitesWithData(nsnull),
        mLibrary(aLibrary),
        mFilePath(aFilePath)
    {
        NS_ASSERTION(mLibrary, "need non-null lib");
        // addref here??
    }

    virtual ~PluginPRLibrary()
    {
        // unref here??
    }

    virtual void SetPlugin(nsNPAPIPlugin*) { }

    virtual bool HasRequiredFunctions() {
        mNP_Initialize = (NP_InitializeFunc)
            PR_FindFunctionSymbol(mLibrary, "NP_Initialize");
        if (!mNP_Initialize)
            return false;

        mNP_Shutdown = (NP_ShutdownFunc)
            PR_FindFunctionSymbol(mLibrary, "NP_Shutdown");
        if (!mNP_Shutdown)
            return false;

        mNP_GetMIMEDescription = (NP_GetMIMEDescriptionFunc)
            PR_FindFunctionSymbol(mLibrary, "NP_GetMIMEDescription");
#ifndef XP_MACOSX
        if (!mNP_GetMIMEDescription)
            return false;
#endif

#if defined(XP_UNIX) && !defined(XP_MACOSX)
        mNP_GetValue = (NP_GetValueFunc)
            PR_FindFunctionSymbol(mLibrary, "NP_GetValue");
        if (!mNP_GetValue)
            return false;
#endif

#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
        mNP_GetEntryPoints = (NP_GetEntryPointsFunc)
            PR_FindFunctionSymbol(mLibrary, "NP_GetEntryPoints");
        if (!mNP_GetEntryPoints)
            return false;
#endif
        return true;
    }

#if defined(XP_UNIX) && !defined(XP_MACOSX) && !defined(MOZ_WIDGET_GONK)
    virtual nsresult NP_Initialize(NPNetscapeFuncs* bFuncs,
                                   NPPluginFuncs* pFuncs, NPError* error);
#else
    virtual nsresult NP_Initialize(NPNetscapeFuncs* bFuncs,
                                   NPError* error);
#endif

    virtual nsresult NP_Shutdown(NPError* error);
    virtual nsresult NP_GetMIMEDescription(const char** mimeDesc);

    virtual nsresult NP_GetValue(void *future, NPPVariable aVariable,
                                 void *aValue, NPError* error);

#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
    virtual nsresult NP_GetEntryPoints(NPPluginFuncs* pFuncs, NPError* error);
#endif

    virtual nsresult NPP_New(NPMIMEType pluginType, NPP instance,
                             uint16_t mode, int16_t argc, char* argn[],
                             char* argv[], NPSavedData* saved,
                             NPError* error);

    virtual nsresult NPP_ClearSiteData(const char* site, uint64_t flags,
                                       uint64_t maxAge);
    virtual nsresult NPP_GetSitesWithData(InfallibleTArray<nsCString>& result);

    virtual nsresult AsyncSetWindow(NPP instance, NPWindow* window);
    virtual nsresult GetImageContainer(NPP instance, ImageContainer** aContainer);
    virtual nsresult GetImageSize(NPP instance, nsIntSize* aSize);
    NS_OVERRIDE virtual bool UseAsyncPainting() { return false; }
#if defined(XP_MACOSX)
    virtual nsresult IsRemoteDrawingCoreAnimation(NPP instance, bool *aDrawing);
#endif
    NS_OVERRIDE
    virtual nsresult SetBackgroundUnknown(NPP instance);
    NS_OVERRIDE
    virtual nsresult BeginUpdateBackground(NPP instance,
                                           const nsIntRect&, gfxContext** aCtx);
    NS_OVERRIDE
    virtual nsresult EndUpdateBackground(NPP instance,
                                         gfxContext* aCtx, const nsIntRect&);
#if defined(MOZ_WIDGET_QT) && (MOZ_PLATFORM_MAEMO == 6)
    virtual nsresult HandleGUIEvent(NPP instance,
                                    const nsGUIEvent& anEvent, bool* handled);
#endif

    virtual void GetLibraryPath(nsACString& aPath) { aPath.Assign(mFilePath); }

private:
    NP_InitializeFunc mNP_Initialize;
    NP_ShutdownFunc mNP_Shutdown;
    NP_GetMIMEDescriptionFunc mNP_GetMIMEDescription;
#if defined(XP_UNIX) && !defined(XP_MACOSX)
    NP_GetValueFunc mNP_GetValue;
#endif
#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
    NP_GetEntryPointsFunc mNP_GetEntryPoints;
#endif
    NPP_NewProcPtr mNPP_New;
    NPP_GetValueProcPtr mNPP_GetValue;
    NPP_ClearSiteDataPtr mNPP_ClearSiteData;
    NPP_GetSitesWithDataPtr mNPP_GetSitesWithData;
    PRLibrary* mLibrary;
    nsCString mFilePath;
};


} // namespace mozilla

#endif  // ifndef PluginPRLibrary_h
