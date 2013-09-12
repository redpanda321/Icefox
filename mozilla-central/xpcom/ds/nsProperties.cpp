/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsProperties.h"
#include "nsString.h"
#include "nsCRT.h"

////////////////////////////////////////////////////////////////////////////////

NS_IMPL_AGGREGATED(nsProperties)
NS_INTERFACE_MAP_BEGIN_AGGREGATED(nsProperties)
    NS_INTERFACE_MAP_ENTRY(nsIProperties)
NS_INTERFACE_MAP_END

NS_IMETHODIMP
nsProperties::Get(const char* prop, const nsIID & uuid, void* *result)
{
    NS_ENSURE_ARG(prop);

    nsCOMPtr<nsISupports> value;
    if (!nsProperties_HashBase::Get(prop, getter_AddRefs(value))) {
        return NS_ERROR_FAILURE;
    }
    return (value) ? value->QueryInterface(uuid, result) : NS_ERROR_NO_INTERFACE;
}

NS_IMETHODIMP
nsProperties::Set(const char* prop, nsISupports* value)
{
    NS_ENSURE_ARG(prop);
    Put(prop, value);
    return NS_OK;
}

NS_IMETHODIMP
nsProperties::Undefine(const char* prop)
{
    NS_ENSURE_ARG(prop);

    nsCOMPtr<nsISupports> value;
    if (!nsProperties_HashBase::Get(prop, getter_AddRefs(value)))
        return NS_ERROR_FAILURE;

    Remove(prop);
    return NS_OK;
}

NS_IMETHODIMP
nsProperties::Has(const char* prop, bool *result)
{
    NS_ENSURE_ARG(prop);

    nsCOMPtr<nsISupports> value;
    *result = nsProperties_HashBase::Get(prop,
                                         getter_AddRefs(value));
    return NS_OK;
}

struct GetKeysEnumData
{
    char **keys;
    uint32_t next;
    nsresult res;
};

 PLDHashOperator
GetKeysEnumerate(const char *key, nsISupports* data,
                 void *arg)
{
    GetKeysEnumData *gkedp = (GetKeysEnumData *)arg;
    gkedp->keys[gkedp->next] = nsCRT::strdup(key);

    if (!gkedp->keys[gkedp->next]) {
        gkedp->res = NS_ERROR_OUT_OF_MEMORY;
        return PL_DHASH_STOP;
    }

    gkedp->next++;
    return PL_DHASH_NEXT;
}

NS_IMETHODIMP 
nsProperties::GetKeys(uint32_t *count, char ***keys)
{
    NS_ENSURE_ARG(count);
    NS_ENSURE_ARG(keys);

    uint32_t n = Count();
    char ** k = (char **) nsMemory::Alloc(n * sizeof(char *));
    NS_ENSURE_TRUE(k, NS_ERROR_OUT_OF_MEMORY);

    GetKeysEnumData gked;
    gked.keys = k;
    gked.next = 0;
    gked.res = NS_OK;

    EnumerateRead(GetKeysEnumerate, &gked);

    nsresult rv = gked.res;
    if (NS_FAILED(rv)) {
        // Free 'em all
        for (uint32_t i = 0; i < gked.next; i++)
            nsMemory::Free(k[i]);
        nsMemory::Free(k);
        return rv;
    }

    *count = n;
    *keys = k;
    return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
