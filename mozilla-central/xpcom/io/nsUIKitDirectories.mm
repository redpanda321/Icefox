#include <Foundation/NSPathUtilities.h>

#include "nsString.h"
#include "nsILocalFile.h"

nsresult GetTemporaryFolder(nsILocalFile** aFile)
{
  return NS_NewNativeLocalFile(
          nsDependentCString([NSTemporaryDirectory() UTF8String]),
          PR_TRUE, aFile);
}
