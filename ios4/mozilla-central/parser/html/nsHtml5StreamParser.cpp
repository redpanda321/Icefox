/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=2 et tw=79: */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *   Henri Sivonen <hsivonen@iki.fi>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsHtml5StreamParser.h"
#include "nsICharsetConverterManager.h"
#include "nsICharsetAlias.h"
#include "nsServiceManagerUtils.h"
#include "nsEncoderDecoderUtils.h"
#include "nsContentUtils.h"
#include "nsHtml5Tokenizer.h"
#include "nsIHttpChannel.h"
#include "nsHtml5Parser.h"
#include "nsHtml5TreeBuilder.h"
#include "nsHtml5AtomTable.h"
#include "nsHtml5Module.h"
#include "nsHtml5RefPtr.h"
#include "nsIScriptError.h"

static NS_DEFINE_CID(kCharsetAliasCID, NS_CHARSETALIAS_CID);

PRInt32 nsHtml5StreamParser::sTimerInitialDelay = 120;
PRInt32 nsHtml5StreamParser::sTimerSubsequentDelay = 120;

// static
void
nsHtml5StreamParser::InitializeStatics()
{
  nsContentUtils::AddIntPrefVarCache("html5.flushtimer.initialdelay",
                                     &sTimerInitialDelay);
  nsContentUtils::AddIntPrefVarCache("html5.flushtimer.subsequentdelay",
                                     &sTimerSubsequentDelay);
}

/*
 * Note that nsHtml5StreamParser implements cycle collecting AddRef and
 * Release. Therefore, nsHtml5StreamParser must never be refcounted from
 * the parser thread!
 *
 * To work around this limitation, runnables posted by the main thread to the
 * parser thread hold their reference to the stream parser in an
 * nsHtml5RefPtr. Upon creation, nsHtml5RefPtr addrefs the object it holds
 * just like a regular nsRefPtr. This is OK, since the creation of the
 * runnable and the nsHtml5RefPtr happens on the main thread.
 *
 * When the runnable is done on the parser thread, the destructor of
 * nsHtml5RefPtr runs there. It doesn't call Release on the held object
 * directly. Instead, it posts another runnable back to the main thread where
 * that runnable calls Release on the wrapped object.
 *
 * When posting runnables in the other direction, the runnables have to be
 * created on the main thread when nsHtml5StreamParser is instantiated and
 * held for the lifetime of the nsHtml5StreamParser. This works, because the
 * same runnabled can be dispatched multiple times and currently runnables
 * posted from the parser thread to main thread don't need to wrap any
 * runnable-specific data. (In the other direction, the runnables most notably
 * wrap the byte data of the stream.)
 */
NS_IMPL_CYCLE_COLLECTING_ADDREF(nsHtml5StreamParser)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsHtml5StreamParser)

NS_INTERFACE_TABLE_HEAD(nsHtml5StreamParser)
  NS_INTERFACE_TABLE2(nsHtml5StreamParser, 
                      nsIStreamListener, 
                      nsICharsetDetectionObserver)
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(nsHtml5StreamParser)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_CLASS(nsHtml5StreamParser)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsHtml5StreamParser)
  tmp->DropTimer();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mObserver)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mRequest)
  tmp->mOwner = nsnull;
  tmp->mExecutorFlusher = nsnull;
  tmp->mLoadFlusher = nsnull;
  tmp->mExecutor = nsnull;
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mChardet)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsHtml5StreamParser)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mRequest)
  if (tmp->mOwner) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mOwner");
    cb.NoteXPCOMChild(static_cast<nsIParser*> (tmp->mOwner));
  }
  // hack: count the strongly owned edge wrapped in the runnable
  if (tmp->mExecutorFlusher) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mExecutorFlusher->mExecutor");
    cb.NoteXPCOMChild(static_cast<nsIContentSink*> (tmp->mExecutor));
  }
  // hack: count the strongly owned edge wrapped in the runnable
  if (tmp->mLoadFlusher) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mLoadFlusher->mExecutor");
    cb.NoteXPCOMChild(static_cast<nsIContentSink*> (tmp->mExecutor));
  }
  // hack: count self if held by mChardet
  if (tmp->mChardet) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, 
      "mChardet->mObserver");
    cb.NoteXPCOMChild(static_cast<nsIStreamListener*>(tmp));
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

class nsHtml5ExecutorFlusher : public nsRunnable
{
  private:
    nsRefPtr<nsHtml5TreeOpExecutor> mExecutor;
  public:
    nsHtml5ExecutorFlusher(nsHtml5TreeOpExecutor* aExecutor)
      : mExecutor(aExecutor)
    {}
    NS_IMETHODIMP Run()
    {
      mExecutor->RunFlushLoop();
      return NS_OK;
    }
};

class nsHtml5LoadFlusher : public nsRunnable
{
  private:
    nsRefPtr<nsHtml5TreeOpExecutor> mExecutor;
  public:
    nsHtml5LoadFlusher(nsHtml5TreeOpExecutor* aExecutor)
      : mExecutor(aExecutor)
    {}
    NS_IMETHODIMP Run()
    {
      mExecutor->FlushSpeculativeLoads();
      return NS_OK;
    }
};

nsHtml5StreamParser::nsHtml5StreamParser(nsHtml5TreeOpExecutor* aExecutor,
                                         nsHtml5Parser* aOwner)
  : mFirstBuffer(new nsHtml5UTF16Buffer(NS_HTML5_STREAM_PARSER_READ_BUFFER_SIZE))
  , mLastBuffer(mFirstBuffer)
  , mExecutor(aExecutor)
  , mTreeBuilder(new nsHtml5TreeBuilder(mExecutor->GetStage(),
                                        mExecutor->GetStage()))
  , mTokenizer(new nsHtml5Tokenizer(mTreeBuilder))
  , mTokenizerMutex("nsHtml5StreamParser mTokenizerMutex")
  , mOwner(aOwner)
  , mSpeculationMutex("nsHtml5StreamParser mSpeculationMutex")
  , mTerminatedMutex("nsHtml5StreamParser mTerminatedMutex")
  , mThread(nsHtml5Module::GetStreamParserThread())
  , mExecutorFlusher(new nsHtml5ExecutorFlusher(aExecutor))
  , mLoadFlusher(new nsHtml5LoadFlusher(aExecutor))
  , mFlushTimer(do_CreateInstance("@mozilla.org/timer;1"))
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  mFlushTimer->SetTarget(mThread);
  mAtomTable.Init(); // we aren't checking for OOM anyway...
#ifdef DEBUG
  mAtomTable.SetPermittedLookupThread(mThread);
#endif
  mTokenizer->setInterner(&mAtomTable);
  mTokenizer->setEncodingDeclarationHandler(this);

  // Chardet instantiation adapted from nsDOMFile.
  // Chardet is initialized here even if it turns out to be useless
  // to make the chardet refcount its observer (nsHtml5StreamParser)
  // on the main thread.
  const nsAdoptingString& detectorName = 
    nsContentUtils::GetLocalizedStringPref("intl.charset.detector");
  if (!detectorName.IsEmpty()) {
    nsCAutoString detectorContractID;
    detectorContractID.AssignLiteral(NS_CHARSET_DETECTOR_CONTRACTID_BASE);
    AppendUTF16toUTF8(detectorName, detectorContractID);
    if (mChardet = do_CreateInstance(detectorContractID.get())) {
      (void) mChardet->Init(this);
    }
  }

  // There's a zeroing operator new for everything else
}

nsHtml5StreamParser::~nsHtml5StreamParser()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  mTokenizer->end();
  NS_ASSERTION(!mFlushTimer, "Flush timer was not dropped before dtor!");
#ifdef DEBUG
  mRequest = nsnull;
  mObserver = nsnull;
  mUnicodeDecoder = nsnull;
  mSniffingBuffer = nsnull;
  mMetaScanner = nsnull;
  mFirstBuffer = nsnull;
  mExecutor = nsnull;
  mTreeBuilder = nsnull;
  mTokenizer = nsnull;
  mOwner = nsnull;
#endif
}

nsresult
nsHtml5StreamParser::GetChannel(nsIChannel** aChannel)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  return mRequest ? CallQueryInterface(mRequest, aChannel) :
                    NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
nsHtml5StreamParser::Notify(const char* aCharset, nsDetectionConfident aConf)
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  if (aConf == eBestAnswer || aConf == eSureAnswer) {
    mCharset.Assign(aCharset);
    mCharsetSource = kCharsetFromAutoDetection;
    mTreeBuilder->SetDocumentCharset(mCharset, mCharsetSource);
  }
  return NS_OK;
}

nsresult
nsHtml5StreamParser::SetupDecodingAndWriteSniffingBufferAndCurrentSegment(const PRUint8* aFromSegment, // can be null
                                                                          PRUint32 aCount,
                                                                          PRUint32* aWriteCount)
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  nsresult rv = NS_OK;
  nsCOMPtr<nsICharsetConverterManager> convManager = do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = convManager->GetUnicodeDecoder(mCharset.get(), getter_AddRefs(mUnicodeDecoder));
  if (rv == NS_ERROR_UCONV_NOCONV) {
    mCharset.AssignLiteral("windows-1252"); // lower case is the raw form
    mCharsetSource = kCharsetFromWeakDocTypeDefault;
    rv = convManager->GetUnicodeDecoderRaw(mCharset.get(), getter_AddRefs(mUnicodeDecoder));
    mTreeBuilder->SetDocumentCharset(mCharset, mCharsetSource);
  }
  NS_ENSURE_SUCCESS(rv, rv);
  mUnicodeDecoder->SetInputErrorBehavior(nsIUnicodeDecoder::kOnError_Recover);
  return WriteSniffingBufferAndCurrentSegment(aFromSegment, aCount, aWriteCount);
}

nsresult
nsHtml5StreamParser::WriteSniffingBufferAndCurrentSegment(const PRUint8* aFromSegment, // can be null
                                                          PRUint32 aCount,
                                                          PRUint32* aWriteCount)
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  nsresult rv = NS_OK;
  if (mSniffingBuffer) {
    PRUint32 writeCount;
    rv = WriteStreamBytes(mSniffingBuffer, mSniffingLength, &writeCount);
    NS_ENSURE_SUCCESS(rv, rv);
    mSniffingBuffer = nsnull;
  }
  mMetaScanner = nsnull;
  if (aFromSegment) {
    rv = WriteStreamBytes(aFromSegment, aCount, aWriteCount);
  }
  return rv;
}

nsresult
nsHtml5StreamParser::SetupDecodingFromBom(const char* aCharsetName, const char* aDecoderCharsetName)
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  nsresult rv = NS_OK;
  nsCOMPtr<nsICharsetConverterManager> convManager = do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = convManager->GetUnicodeDecoderRaw(aDecoderCharsetName, getter_AddRefs(mUnicodeDecoder));
  NS_ENSURE_SUCCESS(rv, rv);
  mUnicodeDecoder->SetInputErrorBehavior(nsIUnicodeDecoder::kOnError_Recover);
  mCharset.Assign(aCharsetName);
  mCharsetSource = kCharsetFromByteOrderMark;
  mTreeBuilder->SetDocumentCharset(mCharset, mCharsetSource);
  mSniffingBuffer = nsnull;
  mMetaScanner = nsnull;
  mBomState = BOM_SNIFFING_OVER;
  return rv;
}

nsresult
nsHtml5StreamParser::FinalizeSniffing(const PRUint8* aFromSegment, // can be null
                                      PRUint32 aCount,
                                      PRUint32* aWriteCount,
                                      PRUint32 aCountToSniffingLimit)
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  // meta scan failed.
  if (mCharsetSource >= kCharsetFromHintPrevDoc) {
    return SetupDecodingAndWriteSniffingBufferAndCurrentSegment(aFromSegment, aCount, aWriteCount);
  }
  // maybe try chardet now; 
  if (mChardet) {
    nsresult rv;
    PRBool dontFeed = PR_FALSE;
    if (mSniffingBuffer) {
      rv = mChardet->DoIt((const char*)mSniffingBuffer.get(), mSniffingLength, &dontFeed);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    if (!dontFeed && aFromSegment) {
      rv = mChardet->DoIt((const char*)aFromSegment, aCountToSniffingLimit, &dontFeed);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    rv = mChardet->Done();
    NS_ENSURE_SUCCESS(rv, rv);
    // fall thru; callback may have changed charset  
  }
  if (mCharsetSource == kCharsetUninitialized) {
    // Hopefully this case is never needed, but dealing with it anyway
    mCharset.AssignLiteral("windows-1252");
    mCharsetSource = kCharsetFromWeakDocTypeDefault;
    mTreeBuilder->SetDocumentCharset(mCharset, mCharsetSource);
  }
  return SetupDecodingAndWriteSniffingBufferAndCurrentSegment(aFromSegment, aCount, aWriteCount);
}

nsresult
nsHtml5StreamParser::SniffStreamBytes(const PRUint8* aFromSegment,
                                      PRUint32 aCount,
                                      PRUint32* aWriteCount)
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  nsresult rv = NS_OK;
  PRUint32 writeCount;
  for (PRUint32 i = 0; i < aCount && mBomState != BOM_SNIFFING_OVER; i++) {
    switch (mBomState) {
      case BOM_SNIFFING_NOT_STARTED:
        NS_ASSERTION(i == 0, "Bad BOM sniffing state.");
        switch (*aFromSegment) {
          case 0xEF:
            mBomState = SEEN_UTF_8_FIRST_BYTE;
            break;
          case 0xFF:
            mBomState = SEEN_UTF_16_LE_FIRST_BYTE;
            break;
          case 0xFE:
            mBomState = SEEN_UTF_16_BE_FIRST_BYTE;
            break;
          default:
            mBomState = BOM_SNIFFING_OVER;
            break;
        }
        break;
      case SEEN_UTF_16_LE_FIRST_BYTE:
        if (aFromSegment[i] == 0xFE) {
          rv = SetupDecodingFromBom("UTF-16", "UTF-16LE"); // upper case is the raw form
          NS_ENSURE_SUCCESS(rv, rv);
          PRUint32 count = aCount - (i + 1);
          rv = WriteStreamBytes(aFromSegment + (i + 1), count, &writeCount);
          NS_ENSURE_SUCCESS(rv, rv);
          *aWriteCount = writeCount + (i + 1);
          return rv;
        }
        mBomState = BOM_SNIFFING_OVER;
        break;
      case SEEN_UTF_16_BE_FIRST_BYTE:
        if (aFromSegment[i] == 0xFF) {
          rv = SetupDecodingFromBom("UTF-16", "UTF-16BE"); // upper case is the raw form
          NS_ENSURE_SUCCESS(rv, rv);
          PRUint32 count = aCount - (i + 1);
          rv = WriteStreamBytes(aFromSegment + (i + 1), count, &writeCount);
          NS_ENSURE_SUCCESS(rv, rv);
          *aWriteCount = writeCount + (i + 1);
          return rv;
        }
        mBomState = BOM_SNIFFING_OVER;
        break;
      case SEEN_UTF_8_FIRST_BYTE:
        if (aFromSegment[i] == 0xBB) {
          mBomState = SEEN_UTF_8_SECOND_BYTE;
        } else {
          mBomState = BOM_SNIFFING_OVER;
        }
        break;
      case SEEN_UTF_8_SECOND_BYTE:
        if (aFromSegment[i] == 0xBF) {
          rv = SetupDecodingFromBom("UTF-8", "UTF-8"); // upper case is the raw form
          NS_ENSURE_SUCCESS(rv, rv);
          PRUint32 count = aCount - (i + 1);
          rv = WriteStreamBytes(aFromSegment + (i + 1), count, &writeCount);
          NS_ENSURE_SUCCESS(rv, rv);
          *aWriteCount = writeCount + (i + 1);
          return rv;
        }
        mBomState = BOM_SNIFFING_OVER;
        break;
      default:
        mBomState = BOM_SNIFFING_OVER;
        break;
    }
  }
  // if we get here, there either was no BOM or the BOM sniffing isn't complete yet
  
  if (!mMetaScanner) {
    mMetaScanner = new nsHtml5MetaScanner();
  }
  
  if (mSniffingLength + aCount >= NS_HTML5_STREAM_PARSER_SNIFFING_BUFFER_SIZE) {
    // this is the last buffer
    PRUint32 countToSniffingLimit = NS_HTML5_STREAM_PARSER_SNIFFING_BUFFER_SIZE - mSniffingLength;
    nsHtml5ByteReadable readable(aFromSegment, aFromSegment + countToSniffingLimit);
    mMetaScanner->sniff(&readable, getter_AddRefs(mUnicodeDecoder), mCharset);
    if (mUnicodeDecoder) {
      mUnicodeDecoder->SetInputErrorBehavior(nsIUnicodeDecoder::kOnError_Recover);
      // meta scan successful
      mCharsetSource = kCharsetFromMetaPrescan;
      mTreeBuilder->SetDocumentCharset(mCharset, mCharsetSource);
      mMetaScanner = nsnull;
      return WriteSniffingBufferAndCurrentSegment(aFromSegment, aCount, aWriteCount);
    }
    return FinalizeSniffing(aFromSegment, aCount, aWriteCount, countToSniffingLimit);
  }

  // not the last buffer
  nsHtml5ByteReadable readable(aFromSegment, aFromSegment + aCount);
  mMetaScanner->sniff(&readable, getter_AddRefs(mUnicodeDecoder), mCharset);
  if (mUnicodeDecoder) {
    // meta scan successful
    mUnicodeDecoder->SetInputErrorBehavior(nsIUnicodeDecoder::kOnError_Recover);
    mCharsetSource = kCharsetFromMetaPrescan;
    mTreeBuilder->SetDocumentCharset(mCharset, mCharsetSource);
    mMetaScanner = nsnull;
    return WriteSniffingBufferAndCurrentSegment(aFromSegment, aCount, aWriteCount);
  }
  if (!mSniffingBuffer) {
    mSniffingBuffer = new PRUint8[NS_HTML5_STREAM_PARSER_SNIFFING_BUFFER_SIZE];
  }
  memcpy(mSniffingBuffer + mSniffingLength, aFromSegment, aCount);
  mSniffingLength += aCount;
  *aWriteCount = aCount;
  return NS_OK;
}

nsresult
nsHtml5StreamParser::WriteStreamBytes(const PRUint8* aFromSegment,
                                      PRUint32 aCount,
                                      PRUint32* aWriteCount)
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  // mLastBuffer always points to a buffer of the size NS_HTML5_STREAM_PARSER_READ_BUFFER_SIZE.
  if (mLastBuffer->getEnd() == NS_HTML5_STREAM_PARSER_READ_BUFFER_SIZE) {
    mLastBuffer = (mLastBuffer->next = new nsHtml5UTF16Buffer(NS_HTML5_STREAM_PARSER_READ_BUFFER_SIZE));
  }
  PRInt32 totalByteCount = 0;
  for (;;) {
    PRInt32 end = mLastBuffer->getEnd();
    PRInt32 byteCount = aCount - totalByteCount;
    PRInt32 utf16Count = NS_HTML5_STREAM_PARSER_READ_BUFFER_SIZE - end;

    NS_ASSERTION(utf16Count, "Trying to convert into a buffer with no free space!");

    nsresult convResult = mUnicodeDecoder->Convert((const char*)aFromSegment, &byteCount, mLastBuffer->getBuffer() + end, &utf16Count);

    end += utf16Count;
    mLastBuffer->setEnd(end);
    totalByteCount += byteCount;
    aFromSegment += byteCount;

    NS_ASSERTION(end <= NS_HTML5_STREAM_PARSER_READ_BUFFER_SIZE,
        "The Unicode decoder wrote too much data.");
    NS_ASSERTION(byteCount >= -1, "The decoder consumed fewer than -1 bytes.");
    NS_ASSERTION(byteCount > 0 || NS_FAILED(convResult),
        "The decoder consumed too few bytes but did not signal an error.");

    if (NS_FAILED(convResult)) {
      // Using the more generic NS_FAILED test above in case there are still
      // decoders around that don't use NS_ERROR_ILLEGAL_INPUT properly.
      NS_ASSERTION(convResult == NS_ERROR_ILLEGAL_INPUT,
          "The decoder signaled an error other than NS_ERROR_ILLEGAL_INPUT.");

      // There's an illegal byte in the input. It's now the responsibility
      // of this calling code to output a U+FFFD REPLACEMENT CHARACTER and
      // reset the decoder.

      if (totalByteCount < (PRInt32)aCount) {
        // advance over the bad byte
        ++totalByteCount;
        ++aFromSegment;
      } else {
        NS_NOTREACHED("The decoder signaled an error but consumed all input.");
        // Recovering from this situation in case there are still broken
        // decoders, since nsScanner had recovery code, too.
        totalByteCount = (PRInt32)aCount;
      }

      // Emit the REPLACEMENT CHARACTER
      mLastBuffer->getBuffer()[end] = 0xFFFD;
      ++end;
      mLastBuffer->setEnd(end);
      if (end == NS_HTML5_STREAM_PARSER_READ_BUFFER_SIZE) {
          mLastBuffer = mLastBuffer->next = new nsHtml5UTF16Buffer(NS_HTML5_STREAM_PARSER_READ_BUFFER_SIZE);
      }

      mUnicodeDecoder->Reset();
      if (totalByteCount == (PRInt32)aCount) {
        *aWriteCount = (PRUint32)totalByteCount;
        return NS_OK;
      }
    } else if (convResult == NS_PARTIAL_MORE_OUTPUT) {
      mLastBuffer = mLastBuffer->next = new nsHtml5UTF16Buffer(NS_HTML5_STREAM_PARSER_READ_BUFFER_SIZE);
      NS_ASSERTION(totalByteCount < (PRInt32)aCount,
          "The Unicode decoder consumed too many bytes.");
    } else {
      NS_ASSERTION(totalByteCount == (PRInt32)aCount,
          "The Unicode decoder consumed the wrong number of bytes.");
      *aWriteCount = (PRUint32)totalByteCount;
      return NS_OK;
    }
  }
}

// nsIRequestObserver methods:
nsresult
nsHtml5StreamParser::OnStartRequest(nsIRequest* aRequest, nsISupports* aContext)
{
  NS_PRECONDITION(STREAM_NOT_STARTED == mStreamState,
                  "Got OnStartRequest when the stream had already started.");
  NS_PRECONDITION(!mExecutor->HasStarted(), 
                  "Got OnStartRequest at the wrong stage in the executor life cycle.");
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  if (mObserver) {
    mObserver->OnStartRequest(aRequest, aContext);
  }
  mRequest = aRequest;

  mStreamState = STREAM_BEING_READ;

  PRBool scriptingEnabled = mExecutor->IsScriptEnabled();
  mOwner->StartTokenizer(scriptingEnabled);
  mTreeBuilder->setScriptingEnabled(scriptingEnabled);
  mTokenizer->start();
  mExecutor->Start();
  mExecutor->StartReadingFromStage();
  /*
   * If you move the following line, be very careful not to cause 
   * WillBuildModel to be called before the document has had its 
   * script global object set.
   */
  mExecutor->WillBuildModel(eDTDMode_unknown);
  
  nsresult rv = NS_OK;

  mReparseForbidden = PR_FALSE;
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(mRequest, &rv));
  if (NS_SUCCEEDED(rv)) {
    nsCAutoString method;
    httpChannel->GetRequestMethod(method);
    // XXX does Necko have a way to renavigate POST, etc. without hitting
    // the network?
    if (!method.EqualsLiteral("GET")) {
      // This is the old Gecko behavior but the HTML5 spec disagrees.
      // Don't reparse on POST.
      mReparseForbidden = PR_TRUE;
    }
  }
  
  if (mCharsetSource < kCharsetFromChannel) {
    // we aren't ready to commit to an encoding yet
    // leave converter uninstantiated for now
    return NS_OK;
  }
  
  nsCOMPtr<nsICharsetConverterManager> convManager = do_GetService(NS_CHARSETCONVERTERMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = convManager->GetUnicodeDecoder(mCharset.get(), getter_AddRefs(mUnicodeDecoder));
  NS_ENSURE_SUCCESS(rv, rv);
  mUnicodeDecoder->SetInputErrorBehavior(nsIUnicodeDecoder::kOnError_Recover);
  return NS_OK;
}

void
nsHtml5StreamParser::DoStopRequest()
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  NS_PRECONDITION(STREAM_BEING_READ == mStreamState,
                  "Stream ended without being open.");
  mTokenizerMutex.AssertCurrentThreadOwns();

  if (IsTerminated()) {
    return;
  }

  if (!mUnicodeDecoder) {
    PRUint32 writeCount;
    FinalizeSniffing(nsnull, 0, &writeCount, 0);
    // dropped nsresult here
  }

  mStreamState = STREAM_ENDED;

  if (IsTerminatedOrInterrupted()) {
    return;
  }

  ParseAvailableData(); 
}

class nsHtml5RequestStopper : public nsRunnable
{
  private:
    nsHtml5RefPtr<nsHtml5StreamParser> mStreamParser;
  public:
    nsHtml5RequestStopper(nsHtml5StreamParser* aStreamParser)
      : mStreamParser(aStreamParser)
    {}
    NS_IMETHODIMP Run()
    {
      mozilla::MutexAutoLock autoLock(mStreamParser->mTokenizerMutex);
      mStreamParser->DoStopRequest();
      return NS_OK;
    }
};

nsresult
nsHtml5StreamParser::OnStopRequest(nsIRequest* aRequest,
                             nsISupports* aContext,
                             nsresult status)
{
  NS_ASSERTION(mRequest == aRequest, "Got Stop on wrong stream.");
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  if (mObserver) {
    mObserver->OnStopRequest(aRequest, aContext, status);
  }
  nsCOMPtr<nsIRunnable> stopper = new nsHtml5RequestStopper(this);
  if (NS_FAILED(mThread->Dispatch(stopper, nsIThread::DISPATCH_NORMAL))) {
    NS_WARNING("Dispatching StopRequest event failed.");
  }
  return NS_OK;
}

void
nsHtml5StreamParser::DoDataAvailable(PRUint8* aBuffer, PRUint32 aLength)
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  NS_PRECONDITION(STREAM_BEING_READ == mStreamState,
                  "DoDataAvailable called when stream not open.");
  mTokenizerMutex.AssertCurrentThreadOwns();

  if (IsTerminated()) {
    return;
  }

  PRUint32 writeCount;
  HasDecoder() ? WriteStreamBytes(aBuffer, aLength, &writeCount) :
                 SniffStreamBytes(aBuffer, aLength, &writeCount);
  // dropping nsresult here
  NS_ASSERTION(writeCount == aLength, "Wrong number of stream bytes written/sniffed.");

  if (IsTerminatedOrInterrupted()) {
    return;
  }

  ParseAvailableData();

  if (mFlushTimerArmed || mSpeculating) {
    return;
  }

  mFlushTimer->InitWithFuncCallback(nsHtml5StreamParser::TimerCallback,
                                    static_cast<void*> (this),
                                    mFlushTimerEverFired ?
                                        sTimerInitialDelay :
                                        sTimerSubsequentDelay,
                                    nsITimer::TYPE_ONE_SHOT);
  mFlushTimerArmed = PR_TRUE;
}

class nsHtml5DataAvailable : public nsRunnable
{
  private:
    nsHtml5RefPtr<nsHtml5StreamParser> mStreamParser;
    nsAutoArrayPtr<PRUint8>            mData;
    PRUint32                           mLength;
  public:
    nsHtml5DataAvailable(nsHtml5StreamParser* aStreamParser,
                         PRUint8*             aData,
                         PRUint32             aLength)
      : mStreamParser(aStreamParser)
      , mData(aData)
      , mLength(aLength)
    {}
    NS_IMETHODIMP Run()
    {
      mozilla::MutexAutoLock autoLock(mStreamParser->mTokenizerMutex);
      mStreamParser->DoDataAvailable(mData, mLength);
      return NS_OK;
    }
};

// nsIStreamListener method:
nsresult
nsHtml5StreamParser::OnDataAvailable(nsIRequest* aRequest,
                               nsISupports* aContext,
                               nsIInputStream* aInStream,
                               PRUint32 aSourceOffset,
                               PRUint32 aLength)
{
  NS_ASSERTION(mRequest == aRequest, "Got data on wrong stream.");
  PRUint32 totalRead;
  nsAutoArrayPtr<PRUint8> data(new PRUint8[aLength]);
  nsresult rv = aInStream->Read(reinterpret_cast<char*>(data.get()),
  aLength, &totalRead);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ASSERTION(totalRead <= aLength, "Read more bytes than were available?");
  nsCOMPtr<nsIRunnable> dataAvailable = new nsHtml5DataAvailable(this,
                                                                data.forget(),
                                                                totalRead);
  if (NS_FAILED(mThread->Dispatch(dataAvailable, nsIThread::DISPATCH_NORMAL))) {
    NS_WARNING("Dispatching DataAvailable event failed.");
  }
  return rv;
}

void
nsHtml5StreamParser::internalEncodingDeclaration(nsString* aEncoding)
{
  // This code needs to stay in sync with
  // nsHtml5MetaScanner::tryCharset. Unfortunately, the
  // trickery with member fields there leads to some copy-paste reuse. :-(
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  if (mCharsetSource >= kCharsetFromMetaTag) { // this threshold corresponds to "confident" in the HTML5 spec
    return;
  }

  if (mReparseForbidden) {
    return; // not reparsing even if we wanted to
  }

  nsCAutoString newEncoding;
  CopyUTF16toUTF8(*aEncoding, newEncoding);
  // XXX spec says only UTF-16
  if (newEncoding.LowerCaseEqualsLiteral("utf-16") ||
      newEncoding.LowerCaseEqualsLiteral("utf-16be") ||
      newEncoding.LowerCaseEqualsLiteral("utf-16le")) {
    newEncoding.Assign("UTF-8");
  }

  nsresult rv = NS_OK;
  nsCOMPtr<nsICharsetAlias> calias(do_GetService(kCharsetAliasCID, &rv));
  if (NS_FAILED(rv)) {
    NS_NOTREACHED("Charset alias service not available.");
    return;
  }
  PRBool eq;
  rv = calias->Equals(newEncoding, mCharset, &eq);
  if (NS_FAILED(rv)) {
    NS_NOTREACHED("Charset name equality check failed.");
    return;
  }
  if (eq) {
    mCharsetSource = kCharsetFromMetaTag; // become confident
    return;
  }
  
  // XXX check HTML5 non-IANA aliases here
  
  nsCAutoString preferred;
  
  rv = calias->GetPreferred(newEncoding, preferred);
  if (NS_FAILED(rv)) {
    // the encoding name is bogus
    return;
  }
  
  if (preferred.LowerCaseEqualsLiteral("utf-16") ||
      preferred.LowerCaseEqualsLiteral("utf-16be") ||
      preferred.LowerCaseEqualsLiteral("utf-16le") ||
      preferred.LowerCaseEqualsLiteral("utf-32") ||
      preferred.LowerCaseEqualsLiteral("utf-32be") ||
      preferred.LowerCaseEqualsLiteral("utf-32le") ||
      preferred.LowerCaseEqualsLiteral("utf-7") ||
      preferred.LowerCaseEqualsLiteral("jis_x0212-1990") ||
      preferred.LowerCaseEqualsLiteral("x-jis0208") ||
      preferred.LowerCaseEqualsLiteral("x-imap4-modified-utf7") ||
      preferred.LowerCaseEqualsLiteral("x-user-defined")) {
    // Not a rough ASCII superset
    return;
  }

  mTreeBuilder->NeedsCharsetSwitchTo(preferred);
  FlushTreeOpsAndDisarmTimer();
  Interrupt();
  // the tree op executor will cause the stream parser to terminate
  // if the charset switch request is accepted or it'll uninterrupt 
  // if the request failed.
}

void
nsHtml5StreamParser::FlushTreeOpsAndDisarmTimer()
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  if (mFlushTimerArmed) {
    // avoid calling Cancel if the flush timer isn't armed to avoid acquiring
    // a mutex
    mFlushTimer->Cancel();
    mFlushTimerArmed = PR_FALSE;
  }
  mTreeBuilder->Flush();
  if (NS_FAILED(NS_DispatchToMainThread(mExecutorFlusher))) {
    NS_WARNING("failed to dispatch executor flush event");
  }
}

void
nsHtml5StreamParser::ParseAvailableData()
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  mTokenizerMutex.AssertCurrentThreadOwns();

  if (IsTerminatedOrInterrupted()) {
    return;
  }
  
  for (;;) {
    if (!mFirstBuffer->hasMore()) {
      if (mFirstBuffer == mLastBuffer) {
        switch (mStreamState) {
          case STREAM_BEING_READ:
            // never release the last buffer.
            if (!mSpeculating) {
              // reuse buffer space if not speculating
              mFirstBuffer->setStart(0);
              mFirstBuffer->setEnd(0);
            }
            mTreeBuilder->FlushLoads();
            // Dispatch this runnable unconditionally, because the loads
            // that need flushing may have been flushed earlier even if the
            // flush right above here did nothing.
            if (NS_FAILED(NS_DispatchToMainThread(mLoadFlusher))) {
              NS_WARNING("failed to dispatch load flush event");
            }
            return; // no more data for now but expecting more
          case STREAM_ENDED:
            if (mAtEOF) {
              return;
            }
            mAtEOF = PR_TRUE;
            mTokenizer->eof();
            mTreeBuilder->StreamEnded();
            FlushTreeOpsAndDisarmTimer();
            return; // no more data and not expecting more
          default:
            NS_NOTREACHED("It should be impossible to reach this.");
            return;
        }
      }
      mFirstBuffer = mFirstBuffer->next;
      continue;
    }

    // now we have a non-empty buffer
    mFirstBuffer->adjust(mLastWasCR);
    mLastWasCR = PR_FALSE;
    if (mFirstBuffer->hasMore()) {
      mLastWasCR = mTokenizer->tokenizeBuffer(mFirstBuffer);
      // At this point, internalEncodingDeclaration() may have called 
      // Terminate, but that never happens together with script.
      // Can't assert that here, though, because it's possible that the main
      // thread has called Terminate() while this thread was parsing.
      if (mTreeBuilder->HasScript()) {
        mozilla::MutexAutoLock speculationAutoLock(mSpeculationMutex);
        nsHtml5Speculation* speculation = 
          new nsHtml5Speculation(mFirstBuffer,
                                 mFirstBuffer->getStart(),
                                 mTokenizer->getLineNumber(),
                                 mTreeBuilder->newSnapshot());
        mTreeBuilder->AddSnapshotToScript(speculation->GetSnapshot(), 
                                          speculation->GetStartLineNumber());
        FlushTreeOpsAndDisarmTimer();
        mTreeBuilder->SetOpSink(speculation);
        mSpeculations.AppendElement(speculation); // adopts the pointer
        mSpeculating = PR_TRUE;
      }
      if (IsTerminatedOrInterrupted()) {
        return;
      }
    }
    continue;
  }
}

class nsHtml5StreamParserContinuation : public nsRunnable
{
private:
  nsHtml5RefPtr<nsHtml5StreamParser> mStreamParser;
public:
  nsHtml5StreamParserContinuation(nsHtml5StreamParser* aStreamParser)
    : mStreamParser(aStreamParser)
  {}
  NS_IMETHODIMP Run()
  {
    mozilla::MutexAutoLock autoLock(mStreamParser->mTokenizerMutex);
    mStreamParser->Uninterrupt();
    mStreamParser->ParseAvailableData();
    return NS_OK;
  }
};

void
nsHtml5StreamParser::ContinueAfterScripts(nsHtml5Tokenizer* aTokenizer, 
                                          nsHtml5TreeBuilder* aTreeBuilder,
                                          PRBool aLastWasCR)
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  #ifdef DEBUG
    mExecutor->AssertStageEmpty();
  #endif
  PRBool speculationFailed = PR_FALSE;
  {
    mozilla::MutexAutoLock speculationAutoLock(mSpeculationMutex);
    if (mSpeculations.IsEmpty()) {
      NS_NOTREACHED("ContinueAfterScripts called without speculations.");
      return;
    }
    nsHtml5Speculation* speculation = mSpeculations.ElementAt(0);
    if (aLastWasCR || 
        !aTokenizer->isInDataState() || 
        !aTreeBuilder->snapshotMatches(speculation->GetSnapshot())) {
      speculationFailed = PR_TRUE;
      // We've got a failed speculation :-(
      Interrupt(); // Make the parser thread release the tokenizer mutex sooner
      // now fall out of the speculationAutoLock into the tokenizerAutoLock block
    } else {
      // We've got a successful speculation!
      if (mSpeculations.Length() > 1) {
        // the first speculation isn't the current speculation, so there's 
        // no need to bother the parser thread.
        speculation->FlushToSink(mExecutor);
        NS_ASSERTION(!mExecutor->IsScriptExecuting(),
          "ParseUntilBlocked() was supposed to ensure we don't come "
          "here when scripts are executing.");
        NS_ASSERTION(mExecutor->IsInFlushLoop(), "How are we here if "
          "RunFlushLoop() didn't call ParseUntilBlocked() which is the "
          "only caller of this method?");
        mSpeculations.RemoveElementAt(0);
        return;
      }
      // else
      Interrupt(); // Make the parser thread release the tokenizer mutex sooner
      
      // now fall through
      // the first speculation is the current speculation. Need to 
      // release the the speculation mutex and acquire the tokenizer 
      // mutex. (Just acquiring the other mutex here would deadlock)
    }
  }
  {
    mozilla::MutexAutoLock tokenizerAutoLock(mTokenizerMutex);
    #ifdef DEBUG
    {
      nsCOMPtr<nsIThread> mainThread;
      NS_GetMainThread(getter_AddRefs(mainThread));
      mAtomTable.SetPermittedLookupThread(mainThread);
    }
    #endif
    // In principle, the speculation mutex should be acquired here,
    // but there's no point, because the parser thread only acquires it
    // when it has also acquired the tokenizer mutex and we are already
    // holding the tokenizer mutex.
    if (speculationFailed) {
      // Rewind the stream
      mAtEOF = PR_FALSE;
      nsHtml5Speculation* speculation = mSpeculations.ElementAt(0);
      mFirstBuffer = speculation->GetBuffer();
      mFirstBuffer->setStart(speculation->GetStart());
      mTokenizer->setLineNumber(speculation->GetStartLineNumber());

      nsContentUtils::ReportToConsole(nsContentUtils::eDOM_PROPERTIES,
                                      "SpeculationFailed",
                                      nsnull, 0,
                                      mExecutor->GetDocument()->GetDocumentURI(),
                                      EmptyString(),
                                      speculation->GetStartLineNumber(),
                                      0,
                                      nsIScriptError::warningFlag,
                                      "DOM Events");

      nsHtml5UTF16Buffer* buffer = mFirstBuffer->next;
      while (buffer) {
        buffer->setStart(0);
        buffer = buffer->next;
      }
      
      mSpeculations.Clear(); // potentially a huge number of destructors 
                             // run here synchronously on the main thread...

      mTreeBuilder->flushCharacters(); // empty the pending buffer
      mTreeBuilder->ClearOps(); // now get rid of the failed ops

      mTreeBuilder->SetOpSink(mExecutor->GetStage());
      mExecutor->StartReadingFromStage();
      mSpeculating = PR_FALSE;

      // Copy state over
      mLastWasCR = aLastWasCR;
      mTokenizer->loadState(aTokenizer);
      mTreeBuilder->loadState(aTreeBuilder, &mAtomTable);
    } else {    
      // We've got a successful speculation and at least a moment ago it was
      // the current speculation
      mSpeculations.ElementAt(0)->FlushToSink(mExecutor);
      NS_ASSERTION(!mExecutor->IsScriptExecuting(),
        "ParseUntilBlocked() was supposed to ensure we don't come "
        "here when scripts are executing.");
      NS_ASSERTION(mExecutor->IsInFlushLoop(), "How are we here if "
        "RunFlushLoop() didn't call ParseUntilBlocked() which is the "
        "only caller of this method?");
      mSpeculations.RemoveElementAt(0);
      if (mSpeculations.IsEmpty()) {
        // yes, it was still the only speculation. Now stop speculating
        if (mTreeBuilder->IsDiscretionaryFlushSafe()) {
          // However, before telling the executor to read from stage, flush
          // any pending ops straight to the executor, because otherwise
          // they remain unflushed until we get more data from the network.
          mTreeBuilder->SetOpSink(mExecutor);
          mTreeBuilder->Flush();
        }
        mTreeBuilder->SetOpSink(mExecutor->GetStage());
        mExecutor->StartReadingFromStage();
        mSpeculating = PR_FALSE;
      }
    }
    nsCOMPtr<nsIRunnable> event = new nsHtml5StreamParserContinuation(this);
    if (NS_FAILED(mThread->Dispatch(event, nsIThread::DISPATCH_NORMAL))) {
      NS_WARNING("Failed to dispatch nsHtml5StreamParserContinuation");
    }
    // A stream event might run before this event runs, but that's harmless.
    #ifdef DEBUG
      mAtomTable.SetPermittedLookupThread(mThread);
    #endif
  }
}

void
nsHtml5StreamParser::ContinueAfterFailedCharsetSwitch()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  nsCOMPtr<nsIRunnable> event = new nsHtml5StreamParserContinuation(this);
  if (NS_FAILED(mThread->Dispatch(event, nsIThread::DISPATCH_NORMAL))) {
    NS_WARNING("Failed to dispatch nsHtml5StreamParserContinuation");
  }
}

class nsHtml5TimerKungFu : public nsRunnable
{
private:
  nsHtml5RefPtr<nsHtml5StreamParser> mStreamParser;
public:
  nsHtml5TimerKungFu(nsHtml5StreamParser* aStreamParser)
    : mStreamParser(aStreamParser)
  {}
  NS_IMETHODIMP Run()
  {
    if (mStreamParser->mFlushTimer) {
      mStreamParser->mFlushTimer->Cancel();
      mStreamParser->mFlushTimer = nsnull;
    }
    return NS_OK;
  }
};

void
nsHtml5StreamParser::DropTimer()
{
  NS_ASSERTION(NS_IsMainThread(), "Wrong thread!");
  /*
   * Simply nulling out the timer wouldn't work, because if the timer is
   * armed, it needs to be canceled first. Simply canceling it first wouldn't
   * work, because nsTimerImpl::Cancel is not safe for calling from outside
   * the thread where nsTimerImpl::Fire would run. It's not safe to
   * dispatch a runnable to cancel the timer from the destructor of this
   * class, because the timer has a weak (void*) pointer back to this instance
   * of the stream parser and having the timer fire before the runnable
   * cancels it would make the timer access a deleted object.
   *
   * This DropTimer method addresses these issues. This method must be called
   * on the main thread before the destructor of this class is reached.
   * The nsHtml5TimerKungFu object has an nsHtml5RefPtr that addrefs this
   * stream parser object to keep it alive until the runnable is done.
   * The runnable cancels the timer on the parser thread, drops the timer
   * and lets nsHtml5RefPtr send a runnable back to the main thread to
   * release the stream parser.
   */
  if (mFlushTimer) {
    nsCOMPtr<nsIRunnable> event = new nsHtml5TimerKungFu(this);
    if (NS_FAILED(mThread->Dispatch(event, nsIThread::DISPATCH_NORMAL))) {
      NS_WARNING("Failed to dispatch TimerKungFu event");
    }
  }
}

// Using a static, because the method name Notify is taken by the chardet 
// callback.
void
nsHtml5StreamParser::TimerCallback(nsITimer* aTimer, void* aClosure)
{
  (static_cast<nsHtml5StreamParser*> (aClosure))->TimerFlush();
}

void
nsHtml5StreamParser::TimerFlush()
{
  NS_ASSERTION(IsParserThread(), "Wrong thread!");
  mozilla::MutexAutoLock autoLock(mTokenizerMutex);

  NS_ASSERTION(!mSpeculating, "Flush timer fired while speculating.");

  // The timer fired if we got here. No need to cancel it. Mark it as
  // not armed, though.
  mFlushTimerArmed = PR_FALSE;

  mFlushTimerEverFired = PR_TRUE;

  if (IsTerminatedOrInterrupted()) {
    return;
  }

  // we aren't speculating and we don't know when new data is
  // going to arrive. Send data to the main thread.
  // However, don't do if the current element on the stack is a 
  // foster-parenting element and there's pending text, because flushing in 
  // that case would make the tree shape dependent on where the flush points 
  // fall.
  if (mTreeBuilder->IsDiscretionaryFlushSafe()) {
    if (mTreeBuilder->Flush()) {
      if (NS_FAILED(NS_DispatchToMainThread(mExecutorFlusher))) {
        NS_WARNING("failed to dispatch executor flush event");
      }
    }
  }
}
