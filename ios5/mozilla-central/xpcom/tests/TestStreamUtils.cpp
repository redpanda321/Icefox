/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdlib.h>
#include <stdio.h>
#include "nsIPipe.h"
#include "nsStreamUtils.h"
#include "nsString.h"
#include "nsCOMPtr.h"

//----

static bool test_consume_stream() {
  const char kData[] =
      "Get your facts first, and then you can distort them as much as you "
      "please.";

  nsCOMPtr<nsIInputStream> input;
  nsCOMPtr<nsIOutputStream> output;
  NS_NewPipe(getter_AddRefs(input),
             getter_AddRefs(output),
             10, PR_UINT32_MAX);
  if (!input || !output)
    return false;

  PRUint32 n = 0;
  output->Write(kData, sizeof(kData) - 1, &n);
  if (n != (sizeof(kData) - 1))
    return false;
  output = nsnull;  // close output

  nsCString buf;
  if (NS_FAILED(NS_ConsumeStream(input, PR_UINT32_MAX, buf)))
    return false;

  if (!buf.Equals(kData))
    return false;

  return true; 
}

//----

typedef bool (*TestFunc)();
#define DECL_TEST(name) { #name, name }

static const struct Test {
  const char* name;
  TestFunc    func;
} tests[] = {
  DECL_TEST(test_consume_stream),
  { nsnull, nsnull }
};

int main(int argc, char **argv) {
  int count = 1;
  if (argc > 1)
    count = atoi(argv[1]);

  if (NS_FAILED(NS_InitXPCOM2(nsnull, nsnull, nsnull)))
    return -1;

  while (count--) {
    for (const Test* t = tests; t->name != nsnull; ++t) {
      printf("%25s : %s\n", t->name, t->func() ? "SUCCESS" : "FAILURE");
    }
  }
  
  NS_ShutdownXPCOM(nsnull);
  return 0;
}
