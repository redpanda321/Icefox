/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
 
#include "nsICharsetConverterManager.h"
#include "nsServiceManagerUtils.h"
#include "nsEncoderDecoderUtils.h"
#include "nsTraceRefcnt.h"

#include "mozilla/dom/EncodingUtils.h"

using mozilla::dom::EncodingUtils;

void
nsHtml5MetaScanner::sniff(nsHtml5ByteReadable* bytes, nsIUnicodeDecoder** decoder, nsACString& charset)
{
  readable = bytes;
  stateLoop(stateSave);
  readable = nullptr;
  if (mUnicodeDecoder) {
    mUnicodeDecoder.forget(decoder);
    charset.Assign(mCharset);
  }
}

bool
nsHtml5MetaScanner::tryCharset(nsString* charset)
{
  // This code needs to stay in sync with
  // nsHtml5StreamParser::internalEncodingDeclaration. Unfortunately, the
  // trickery with member fields here leads to some copy-paste reuse. :-(
  nsresult res = NS_OK;
  nsCOMPtr<nsICharsetConverterManager> convManager = do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &res);
  if (NS_FAILED(res)) {
    NS_ERROR("Could not get CharsetConverterManager service.");
    return false;
  }
  nsAutoCString encoding;
  CopyUTF16toUTF8(*charset, encoding);
  encoding.Trim(" \t\r\n\f");
  if (encoding.LowerCaseEqualsLiteral("utf-16") ||
      encoding.LowerCaseEqualsLiteral("utf-16be") ||
      encoding.LowerCaseEqualsLiteral("utf-16le")) {
    mCharset.Assign("UTF-8");
    res = convManager->GetUnicodeDecoderRaw(mCharset.get(), getter_AddRefs(mUnicodeDecoder));
    if (NS_FAILED(res)) {
      NS_ERROR("Could not get decoder for UTF-8.");
      return false;
    }
    return true;
  }
  nsAutoCString preferred;
  if (!EncodingUtils::FindEncodingForLabel(encoding, preferred)) {
    return false;
  }
  if (preferred.LowerCaseEqualsLiteral("utf-16") ||
      preferred.LowerCaseEqualsLiteral("utf-16be") ||
      preferred.LowerCaseEqualsLiteral("utf-16le") ||
      preferred.LowerCaseEqualsLiteral("utf-7") ||
      preferred.LowerCaseEqualsLiteral("x-imap4-modified-utf7")) {
    return false;
  }
  res = convManager->GetUnicodeDecoderRaw(preferred.get(), getter_AddRefs(mUnicodeDecoder));
  if (res == NS_ERROR_UCONV_NOCONV) {
    return false;
  } else if (NS_FAILED(res)) {
    NS_ERROR("Getting an encoding decoder failed in a bad way.");
    mUnicodeDecoder = nullptr;
    return false;
  } else {
    NS_ASSERTION(mUnicodeDecoder, "Getter nsresult and object don't match.");
    mCharset.Assign(preferred);
    return true;
  }
}
