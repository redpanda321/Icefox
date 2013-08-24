/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


/* tokenization of CSS style sheets */

#include <math.h> // must be first due to symbol conflicts

#include "nsCSSScanner.h"
#include "nsString.h"
#include "nsCRT.h"
#include "mozilla/Util.h"

// for #ifdef CSS_REPORT_PARSE_ERRORS
#include "nsCOMPtr.h"
#include "nsIServiceManager.h"
#include "nsIComponentManager.h"
#include "nsReadableUtils.h"
#include "nsIURI.h"
#include "nsIConsoleService.h"
#include "nsIScriptError.h"
#include "nsIStringBundle.h"
#include "nsIDocument.h"
#include "mozilla/Services.h"
#include "mozilla/css/Loader.h"
#include "nsCSSStyleSheet.h"
#include "mozilla/Preferences.h"

using namespace mozilla;

#ifdef CSS_REPORT_PARSE_ERRORS
static bool gReportErrors = true;
static nsIConsoleService *gConsoleService;
static nsIFactory *gScriptErrorFactory;
static nsIStringBundle *gStringBundle;
#endif

static const PRUint8 IS_HEX_DIGIT  = 0x01;
static const PRUint8 START_IDENT   = 0x02;
static const PRUint8 IS_IDENT      = 0x04;
static const PRUint8 IS_WHITESPACE = 0x08;
static const PRUint8 IS_URL_CHAR   = 0x10;

#define W    IS_WHITESPACE
#define I    IS_IDENT
#define U                                      IS_URL_CHAR
#define S             START_IDENT
#define UI   IS_IDENT                         |IS_URL_CHAR
#define USI  IS_IDENT|START_IDENT             |IS_URL_CHAR
#define UXI  IS_IDENT            |IS_HEX_DIGIT|IS_URL_CHAR
#define UXSI IS_IDENT|START_IDENT|IS_HEX_DIGIT|IS_URL_CHAR

static const PRUint8 gLexTable[] = {
//                                     TAB LF      FF  CR
   0,  0,  0,  0,  0,  0,  0,  0,  0,  W,  W,  0,  W,  W,  0,  0,
//
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
// SPC !   "   #   $   %   &   '   (   )   *   +   ,   -   .   /
   W,  U,  0,  U,  U,  U,  U,  0,  0,  0,  U,  U,  U,  UI, U,  U,
// 0   1   2   3   4   5   6   7   8   9   :   ;   <   =   >   ?
   UXI,UXI,UXI,UXI,UXI,UXI,UXI,UXI,UXI,UXI,U,  U,  U,  U,  U,  U,
// @   A   B   C    D    E    F    G   H   I   J   K   L   M   N   O
   U,UXSI,UXSI,UXSI,UXSI,UXSI,UXSI,USI,USI,USI,USI,USI,USI,USI,USI,USI,
// P   Q   R   S   T   U   V   W   X   Y   Z   [   \   ]   ^   _
   USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,U,  S,  U,  U,  USI,
// `   a   b   c    d    e    f    g   h   i   j   k   l   m   n   o
   U,UXSI,UXSI,UXSI,UXSI,UXSI,UXSI,USI,USI,USI,USI,USI,USI,USI,USI,USI,
// p   q   r   s   t   u   v   w   x   y   z   {   |   }   ~
   USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,U,  U,  U,  U,  0,
// U+008*
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
// U+009*
   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
// U+00A*
   USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,
// U+00B*
   USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,
// U+00C*
   USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,
// U+00D*
   USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,
// U+00E*
   USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,
// U+00F*
   USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI,USI
};

MOZ_STATIC_ASSERT(NS_ARRAY_LENGTH(gLexTable) == 256,
                  "gLexTable expected to cover all 2^8 possible PRUint8s");

#undef W
#undef S
#undef I
#undef U
#undef UI
#undef USI
#undef UXI
#undef UXSI

static inline bool
IsIdentStart(PRInt32 aChar)
{
  return aChar >= 0 &&
    (aChar >= 256 || (gLexTable[aChar] & START_IDENT) != 0);
}

static inline bool
StartsIdent(PRInt32 aFirstChar, PRInt32 aSecondChar)
{
  return IsIdentStart(aFirstChar) ||
    (aFirstChar == '-' && IsIdentStart(aSecondChar));
}

static inline bool
IsWhitespace(PRInt32 ch) {
  return PRUint32(ch) < 256 && (gLexTable[ch] & IS_WHITESPACE) != 0;
}

static inline bool
IsDigit(PRInt32 ch) {
  return (ch >= '0') && (ch <= '9');
}

static inline bool
IsHexDigit(PRInt32 ch) {
  return PRUint32(ch) < 256 && (gLexTable[ch] & IS_HEX_DIGIT) != 0;
}

static inline bool
IsIdent(PRInt32 ch) {
  return ch >= 0 && (ch >= 256 || (gLexTable[ch] & IS_IDENT) != 0);
}

static inline bool
IsURLChar(PRInt32 ch) {
  return ch >= 0 && (ch >= 256 || (gLexTable[ch] & IS_URL_CHAR) != 0);
}

static inline PRUint32
DecimalDigitValue(PRInt32 ch)
{
  return ch - '0';
}

static inline PRUint32
HexDigitValue(PRInt32 ch)
{
  if (IsDigit(ch)) {
    return DecimalDigitValue(ch);
  } else {
    // Note: c&7 just keeps the low three bits which causes
    // upper and lower case alphabetics to both yield their
    // "relative to 10" value for computing the hex value.
    return (ch & 0x7) + 9;
  }
}

nsCSSToken::nsCSSToken()
{
  mType = eCSSToken_Symbol;
}

void
nsCSSToken::AppendToString(nsString& aBuffer)
{
  switch (mType) {
    case eCSSToken_AtKeyword:
      aBuffer.Append(PRUnichar('@')); // fall through intentional
    case eCSSToken_Ident:
    case eCSSToken_WhiteSpace:
    case eCSSToken_Function:
    case eCSSToken_HTMLComment:
    case eCSSToken_URange:
      aBuffer.Append(mIdent);
      if (mType == eCSSToken_Function)
        aBuffer.Append(PRUnichar('('));
      break;
    case eCSSToken_URL:
    case eCSSToken_Bad_URL:
      aBuffer.AppendLiteral("url(");
      if (mSymbol != PRUnichar(0)) {
        aBuffer.Append(mSymbol);
      }
      aBuffer.Append(mIdent);
      if (mSymbol != PRUnichar(0)) {
        aBuffer.Append(mSymbol);
      }
      if (mType == eCSSToken_URL) {
        aBuffer.Append(PRUnichar(')'));
      }
      break;
    case eCSSToken_Number:
      if (mIntegerValid) {
        aBuffer.AppendInt(mInteger, 10);
      }
      else {
        aBuffer.AppendFloat(mNumber);
      }
      break;
    case eCSSToken_Percentage:
      NS_ASSERTION(!mIntegerValid, "How did a percentage token get this set?");
      aBuffer.AppendFloat(mNumber * 100.0f);
      aBuffer.Append(PRUnichar('%'));
      break;
    case eCSSToken_Dimension:
      if (mIntegerValid) {
        aBuffer.AppendInt(mInteger, 10);
      }
      else {
        aBuffer.AppendFloat(mNumber);
      }
      aBuffer.Append(mIdent);
      break;
    case eCSSToken_String:
      aBuffer.Append(mSymbol);
      aBuffer.Append(mIdent); // fall through intentional
    case eCSSToken_Symbol:
      aBuffer.Append(mSymbol);
      break;
    case eCSSToken_ID:
    case eCSSToken_Ref:
      aBuffer.Append(PRUnichar('#'));
      aBuffer.Append(mIdent);
      break;
    case eCSSToken_Includes:
      aBuffer.AppendLiteral("~=");
      break;
    case eCSSToken_Dashmatch:
      aBuffer.AppendLiteral("|=");
      break;
    case eCSSToken_Beginsmatch:
      aBuffer.AppendLiteral("^=");
      break;
    case eCSSToken_Endsmatch:
      aBuffer.AppendLiteral("$=");
      break;
    case eCSSToken_Containsmatch:
      aBuffer.AppendLiteral("*=");
      break;
    case eCSSToken_Bad_String:
      aBuffer.Append(mSymbol);
      aBuffer.Append(mIdent);
      break;
    default:
      NS_ERROR("invalid token type");
      break;
  }
}

nsCSSScanner::nsCSSScanner()
  : mReadPointer(nsnull)
  , mSVGMode(false)
#ifdef CSS_REPORT_PARSE_ERRORS
  , mError(mErrorBuf, ArrayLength(mErrorBuf), 0)
  , mInnerWindowID(0)
  , mWindowIDCached(false)
  , mSheet(nsnull)
  , mLoader(nsnull)
#endif
{
  MOZ_COUNT_CTOR(nsCSSScanner);
  mPushback = mLocalPushback;
  mPushbackSize = ArrayLength(mLocalPushback);
  // No need to init the other members, since they represent state
  // which can get cleared.  We'll init them every time Init() is
  // called.
}

nsCSSScanner::~nsCSSScanner()
{
  MOZ_COUNT_DTOR(nsCSSScanner);
  Close();
  if (mLocalPushback != mPushback) {
    delete [] mPushback;
  }
}

#ifdef CSS_REPORT_PARSE_ERRORS
#define CSS_ERRORS_PREF "layout.css.report_errors"

static int
CSSErrorsPrefChanged(const char *aPref, void *aClosure)
{
  gReportErrors = Preferences::GetBool(CSS_ERRORS_PREF, true);
  return NS_OK;
}
#endif

/* static */ bool
nsCSSScanner::InitGlobals()
{
#ifdef CSS_REPORT_PARSE_ERRORS
  if (gConsoleService && gScriptErrorFactory)
    return true;
  
  nsresult rv = CallGetService(NS_CONSOLESERVICE_CONTRACTID, &gConsoleService);
  NS_ENSURE_SUCCESS(rv, false);

  rv = CallGetClassObject(NS_SCRIPTERROR_CONTRACTID, &gScriptErrorFactory);
  NS_ENSURE_SUCCESS(rv, false);
  NS_ASSERTION(gConsoleService && gScriptErrorFactory,
               "unexpected null pointer without failure");

  Preferences::RegisterCallback(CSSErrorsPrefChanged, CSS_ERRORS_PREF);
  CSSErrorsPrefChanged(CSS_ERRORS_PREF, nsnull);
#endif
  return true;
}

/* static */ void
nsCSSScanner::ReleaseGlobals()
{
#ifdef CSS_REPORT_PARSE_ERRORS
  Preferences::UnregisterCallback(CSSErrorsPrefChanged, CSS_ERRORS_PREF);
  NS_IF_RELEASE(gConsoleService);
  NS_IF_RELEASE(gScriptErrorFactory);
  NS_IF_RELEASE(gStringBundle);
#endif
}

void
nsCSSScanner::Init(const nsAString& aBuffer,
                   nsIURI* aURI, PRUint32 aLineNumber,
                   nsCSSStyleSheet* aSheet, mozilla::css::Loader* aLoader)
{
  NS_PRECONDITION(!mReadPointer, "Should not have an existing input buffer!");

  mReadPointer = aBuffer.BeginReading();
  mCount = aBuffer.Length();

#ifdef CSS_REPORT_PARSE_ERRORS
  // If aURI is the same as mURI, no need to reget mFileName -- it
  // shouldn't have changed.
  if (aURI != mURI) {
    mURI = aURI;
    if (aURI) {
      aURI->GetSpec(mFileName);
    } else {
      mFileName.Adopt(NS_strdup("from DOM"));
    }
  }
#endif // CSS_REPORT_PARSE_ERRORS
  mLineNumber = aLineNumber;

  // Reset variables that we use to keep track of our progress through the input
  mOffset = 0;
  mPushbackCount = 0;

#ifdef CSS_REPORT_PARSE_ERRORS
  mColNumber = 0;
  mSheet = aSheet;
  mLoader = aLoader;
#endif
}

#ifdef CSS_REPORT_PARSE_ERRORS

// @see REPORT_UNEXPECTED_EOF in nsCSSParser.cpp
#define REPORT_UNEXPECTED_EOF(lf_) \
  ReportUnexpectedEOF(#lf_)

void
nsCSSScanner::AddToError(const nsSubstring& aErrorText)
{
  if (mError.IsEmpty()) {
    mErrorLineNumber = mLineNumber;
    mErrorColNumber = mColNumber;
    mError = aErrorText;
  } else {
    mError.Append(NS_LITERAL_STRING("  ") + aErrorText);
  }
}

void
nsCSSScanner::ClearError()
{
  mError.Truncate();
}

void
nsCSSScanner::OutputError()
{
  if (mError.IsEmpty()) return;
 
  // Log it to the Error console

  if (InitGlobals() && gReportErrors) {
    if (!mWindowIDCached) {
      if (mSheet) {
        mInnerWindowID = mSheet->FindOwningWindowInnerID();
      }
      if (mInnerWindowID == 0 && mLoader) {
        nsIDocument* doc = mLoader->GetDocument();
        if (doc) {
          mInnerWindowID = doc->InnerWindowID();
        }
      }
      mWindowIDCached = true;
    }

    nsresult rv;
    nsCOMPtr<nsIScriptError> errorObject =
      do_CreateInstance(gScriptErrorFactory, &rv);

    if (NS_SUCCEEDED(rv)) {
      rv = errorObject->InitWithWindowID(mError.get(),
                                         NS_ConvertUTF8toUTF16(mFileName).get(),
                                         EmptyString().get(),
                                         mErrorLineNumber,
                                         mErrorColNumber,
                                         nsIScriptError::warningFlag,
                                         "CSS Parser",
                                         mInnerWindowID);
      if (NS_SUCCEEDED(rv)) {
        gConsoleService->LogMessage(errorObject);
      }
    }
  }
  ClearError();
}

static bool
InitStringBundle()
{
  if (gStringBundle)
    return true;

  nsCOMPtr<nsIStringBundleService> sbs =
    mozilla::services::GetStringBundleService();
  if (!sbs)
    return false;

  nsresult rv = 
    sbs->CreateBundle("chrome://global/locale/css.properties", &gStringBundle);
  if (NS_FAILED(rv)) {
    gStringBundle = nsnull;
    return false;
  }

  return true;
}

#define ENSURE_STRINGBUNDLE \
  PR_BEGIN_MACRO if (!InitStringBundle()) return; PR_END_MACRO

// aMessage must take no parameters
void nsCSSScanner::ReportUnexpected(const char* aMessage)
{
  ENSURE_STRINGBUNDLE;

  nsXPIDLString str;
  gStringBundle->GetStringFromName(NS_ConvertASCIItoUTF16(aMessage).get(),
                                   getter_Copies(str));
  AddToError(str);
}
  
void
nsCSSScanner::ReportUnexpectedParams(const char* aMessage,
                                     const PRUnichar **aParams,
                                     PRUint32 aParamsLength)
{
  NS_PRECONDITION(aParamsLength > 0, "use the non-params version");
  ENSURE_STRINGBUNDLE;

  nsXPIDLString str;
  gStringBundle->FormatStringFromName(NS_ConvertASCIItoUTF16(aMessage).get(),
                                      aParams, aParamsLength,
                                      getter_Copies(str));
  AddToError(str);
}

// aLookingFor is a plain string, not a format string
void
nsCSSScanner::ReportUnexpectedEOF(const char* aLookingFor)
{
  ENSURE_STRINGBUNDLE;

  nsXPIDLString innerStr;
  gStringBundle->GetStringFromName(NS_ConvertASCIItoUTF16(aLookingFor).get(),
                                   getter_Copies(innerStr));

  const PRUnichar *params[] = {
    innerStr.get()
  };
  nsXPIDLString str;
  gStringBundle->FormatStringFromName(NS_LITERAL_STRING("PEUnexpEOF2").get(),
                                      params, ArrayLength(params),
                                      getter_Copies(str));
  AddToError(str);
}

// aLookingFor is a single character
void
nsCSSScanner::ReportUnexpectedEOF(PRUnichar aLookingFor)
{
  ENSURE_STRINGBUNDLE;

  const PRUnichar lookingForStr[] = {
    PRUnichar('\''), aLookingFor, PRUnichar('\''), PRUnichar(0)
  };
  const PRUnichar *params[] = { lookingForStr };
  nsXPIDLString str;
  gStringBundle->FormatStringFromName(NS_LITERAL_STRING("PEUnexpEOF2").get(),
                                      params, ArrayLength(params),
                                      getter_Copies(str));
  AddToError(str);
}

// aMessage must take 1 parameter (for the string representation of the
// unexpected token)
void
nsCSSScanner::ReportUnexpectedToken(nsCSSToken& tok,
                                    const char *aMessage)
{
  ENSURE_STRINGBUNDLE;
  
  nsAutoString tokenString;
  tok.AppendToString(tokenString);

  const PRUnichar *params[] = {
    tokenString.get()
  };

  ReportUnexpectedParams(aMessage, params);
}

// aParams's first entry must be null, and we'll fill in the token
void
nsCSSScanner::ReportUnexpectedTokenParams(nsCSSToken& tok,
                                          const char* aMessage,
                                          const PRUnichar **aParams,
                                          PRUint32 aParamsLength)
{
  NS_PRECONDITION(aParamsLength > 1, "use the non-params version");
  NS_PRECONDITION(aParams[0] == nsnull, "first param should be empty");

  ENSURE_STRINGBUNDLE;
  
  nsAutoString tokenString;
  tok.AppendToString(tokenString);
  aParams[0] = tokenString.get();

  ReportUnexpectedParams(aMessage, aParams, aParamsLength);
}

#else

#define REPORT_UNEXPECTED_EOF(lf_)

#endif // CSS_REPORT_PARSE_ERRORS

void
nsCSSScanner::Close()
{
  mReadPointer = nsnull;

  // Clean things up so we don't hold on to memory if our parser gets recycled.
#ifdef CSS_REPORT_PARSE_ERRORS
  mFileName.Truncate();
  mURI = nsnull;
  mError.Truncate();
  mInnerWindowID = 0;
  mWindowIDCached = false;
  mSheet = nsnull;
  mLoader = nsnull;
#endif
  if (mPushback != mLocalPushback) {
    delete [] mPushback;
    mPushback = mLocalPushback;
    mPushbackSize = ArrayLength(mLocalPushback);
  }
}

// Returns -1 on error or eof
PRInt32
nsCSSScanner::Read()
{
  PRInt32 rv;
  if (0 < mPushbackCount) {
    rv = PRInt32(mPushback[--mPushbackCount]);
  } else {
    if (mOffset == mCount) {
      return -1;
    }
    rv = PRInt32(mReadPointer[mOffset++]);
    // There are four types of newlines in CSS: "\r", "\n", "\r\n", and "\f".
    // To simplify dealing with newlines, they are all normalized to "\n" here
    if (rv == '\r') {
      if (mOffset < mCount && mReadPointer[mOffset] == '\n') {
        mOffset++;
      }
      rv = '\n';
    } else if (rv == '\f') {
      rv = '\n';
    }
    if (rv == '\n') {
      // 0 is a magical line number meaning that we don't know (i.e., script)
      if (mLineNumber != 0)
        ++mLineNumber;
#ifdef CSS_REPORT_PARSE_ERRORS
      mColNumber = 0;
    } else {
      mColNumber++;
#endif
    }
  }
  return rv;
}

PRInt32
nsCSSScanner::Peek()
{
  if (0 == mPushbackCount) {
    PRInt32 ch = Read();
    if (ch < 0) {
      return -1;
    }
    mPushback[0] = PRUnichar(ch);
    mPushbackCount++;
  }
  return PRInt32(mPushback[mPushbackCount - 1]);
}

void
nsCSSScanner::Pushback(PRUnichar aChar)
{
  if (mPushbackCount == mPushbackSize) { // grow buffer
    PRUnichar*  newPushback = new PRUnichar[mPushbackSize + 4];
    if (nsnull == newPushback) {
      return;
    }
    mPushbackSize += 4;
    memcpy(newPushback, mPushback, sizeof(PRUnichar) * mPushbackCount);
    if (mPushback != mLocalPushback) {
      delete [] mPushback;
    }
    mPushback = newPushback;
  }
  mPushback[mPushbackCount++] = aChar;
}

bool
nsCSSScanner::LookAhead(PRUnichar aChar)
{
  PRInt32 ch = Read();
  if (ch < 0) {
    return false;
  }
  if (ch == aChar) {
    return true;
  }
  Pushback(ch);
  return false;
}

bool
nsCSSScanner::LookAheadOrEOF(PRUnichar aChar)
{
  PRInt32 ch = Read();
  if (ch < 0) {
    return true;
  }
  if (ch == aChar) {
    return true;
  }
  Pushback(ch);
  return false;
}

void
nsCSSScanner::EatWhiteSpace()
{
  for (;;) {
    PRInt32 ch = Read();
    if (ch < 0) {
      break;
    }
    if ((ch != ' ') && (ch != '\n') && (ch != '\t')) {
      Pushback(ch);
      break;
    }
  }
}

bool
nsCSSScanner::Next(nsCSSToken& aToken)
{
  for (;;) { // Infinite loop so we can restart after comments.
    PRInt32 ch = Read();
    if (ch < 0) {
      return false;
    }

    // UNICODE-RANGE
    if ((ch == 'u' || ch == 'U') && Peek() == '+')
      return ParseURange(ch, aToken);

    // IDENT
    if (StartsIdent(ch, Peek()))
      return ParseIdent(ch, aToken);

    // AT_KEYWORD
    if (ch == '@') {
      PRInt32 nextChar = Read();
      if (nextChar >= 0) {
        PRInt32 followingChar = Peek();
        Pushback(nextChar);
        if (StartsIdent(nextChar, followingChar))
          return ParseAtKeyword(ch, aToken);
      }
    }

    // NUMBER or DIM
    if ((ch == '.') || (ch == '+') || (ch == '-')) {
      PRInt32 nextChar = Peek();
      if (IsDigit(nextChar)) {
        return ParseNumber(ch, aToken);
      }
      else if (('.' == nextChar) && ('.' != ch)) {
        nextChar = Read();
        PRInt32 followingChar = Peek();
        Pushback(nextChar);
        if (IsDigit(followingChar))
          return ParseNumber(ch, aToken);
      }
    }
    if (IsDigit(ch)) {
      return ParseNumber(ch, aToken);
    }

    // ID
    if (ch == '#') {
      return ParseRef(ch, aToken);
    }

    // STRING
    if ((ch == '"') || (ch == '\'')) {
      return ParseString(ch, aToken);
    }

    // WS
    if (IsWhitespace(ch)) {
      aToken.mType = eCSSToken_WhiteSpace;
      aToken.mIdent.Assign(PRUnichar(ch));
      EatWhiteSpace();
      return true;
    }
    if (ch == '/' && !IsSVGMode()) {
      PRInt32 nextChar = Peek();
      if (nextChar == '*') {
        Read();
        // FIXME: Editor wants comments to be preserved (bug 60290).
        if (!SkipCComment()) {
          return false;
        }
        continue; // start again at the beginning
      }
    }
    if (ch == '<') {  // consume HTML comment tags
      if (LookAhead('!')) {
        if (LookAhead('-')) {
          if (LookAhead('-')) {
            aToken.mType = eCSSToken_HTMLComment;
            aToken.mIdent.AssignLiteral("<!--");
            return true;
          }
          Pushback('-');
        }
        Pushback('!');
      }
    }
    if (ch == '-') {  // check for HTML comment end
      if (LookAhead('-')) {
        if (LookAhead('>')) {
          aToken.mType = eCSSToken_HTMLComment;
          aToken.mIdent.AssignLiteral("-->");
          return true;
        }
        Pushback('-');
      }
    }

    // INCLUDES ("~=") and DASHMATCH ("|=")
    if (( ch == '|' ) || ( ch == '~' ) || ( ch == '^' ) ||
        ( ch == '$' ) || ( ch == '*' )) {
      PRInt32 nextChar = Read();
      if ( nextChar == '=' ) {
        if (ch == '~') {
          aToken.mType = eCSSToken_Includes;
        }
        else if (ch == '|') {
          aToken.mType = eCSSToken_Dashmatch;
        }
        else if (ch == '^') {
          aToken.mType = eCSSToken_Beginsmatch;
        }
        else if (ch == '$') {
          aToken.mType = eCSSToken_Endsmatch;
        }
        else if (ch == '*') {
          aToken.mType = eCSSToken_Containsmatch;
        }
        return true;
      } else if (nextChar >= 0) {
        Pushback(nextChar);
      }
    }
    aToken.mType = eCSSToken_Symbol;
    aToken.mSymbol = ch;
    return true;
  }
}

bool
nsCSSScanner::NextURL(nsCSSToken& aToken)
{
  EatWhiteSpace();

  PRInt32 ch = Read();
  if (ch < 0) {
    return false;
  }

  // STRING
  if ((ch == '"') || (ch == '\'')) {
#ifdef DEBUG
    bool ok =
#endif
      ParseString(ch, aToken);
    NS_ABORT_IF_FALSE(ok, "ParseString should never fail, "
                          "since there's always something read");

    NS_ABORT_IF_FALSE(aToken.mType == eCSSToken_String ||
                      aToken.mType == eCSSToken_Bad_String,
                      "unexpected token type");
    if (NS_LIKELY(aToken.mType == eCSSToken_String)) {
      EatWhiteSpace();
      if (LookAheadOrEOF(')')) {
        aToken.mType = eCSSToken_URL;
      } else {
        aToken.mType = eCSSToken_Bad_URL;
      }
    } else {
      aToken.mType = eCSSToken_Bad_URL;
    }
    return true;
  }

  // Process a url lexical token. A CSS1 url token can contain
  // characters beyond identifier characters (e.g. '/', ':', etc.)
  // Because of this the normal rules for tokenizing the input don't
  // apply very well. To simplify the parser and relax some of the
  // requirements on the scanner we parse url's here. If we find a
  // malformed URL then we emit a token of type "Bad_URL" so that
  // the CSS1 parser can ignore the invalid input.  The parser must
  // treat a Bad_URL token like a Function token, and process
  // tokens until a matching parenthesis.

  aToken.mType = eCSSToken_Bad_URL;
  aToken.mSymbol = PRUnichar(0);
  nsString& ident = aToken.mIdent;
  ident.SetLength(0);

  // start of a non-quoted url (which may be empty)
  bool ok = true;
  for (;;) {
    if (IsURLChar(ch)) {
      // A regular url character.
      ident.Append(PRUnichar(ch));
    } else if (ch == ')') {
      // All done
      break;
    } else if (IsWhitespace(ch)) {
      // Whitespace is allowed at the end of the URL
      EatWhiteSpace();
      // Consume the close paren if we have it; if not we're an invalid URL.
      ok = LookAheadOrEOF(')');
      break;
    } else if (ch == '\\') {
      if (!ParseAndAppendEscape(ident, false)) {
        ok = false;
        Pushback(ch);
        break;
      }
    } else {
      // This is an invalid URL spec
      ok = false;
      Pushback(ch); // push it back so the parser can match tokens and
                    // then closing parenthesis
      break;
    }

    ch = Read();
    if (ch < 0) {
      break;
    }
  }

  // If the result of the above scanning is ok then change the token
  // type to a useful one.
  if (ok) {
    aToken.mType = eCSSToken_URL;
  }
  return true;
}


/**
 * Returns whether an escape was succesfully parsed; if it was not,
 * the backslash needs to be its own symbol token.
 */
bool
nsCSSScanner::ParseAndAppendEscape(nsString& aOutput, bool aInString)
{
  PRInt32 ch = Read();
  if (ch < 0) {
    return false;
  }
  if (IsHexDigit(ch)) {
    PRInt32 rv = 0;
    int i;
    Pushback(ch);
    for (i = 0; i < 6; i++) { // up to six digits
      ch = Read();
      if (ch < 0) {
        // Whoops: error or premature eof
        break;
      }
      if (!IsHexDigit(ch) && !IsWhitespace(ch)) {
        Pushback(ch);
        break;
      } else if (IsHexDigit(ch)) {
        rv = rv * 16 + HexDigitValue(ch);
      } else {
        NS_ASSERTION(IsWhitespace(ch), "bad control flow");
        // single space ends escape
        break;
      }
    }
    if (6 == i) { // look for trailing whitespace and eat it
      ch = Peek();
      if (IsWhitespace(ch)) {
        (void) Read();
      }
    }
    NS_ASSERTION(rv >= 0, "How did rv become negative?");
    // "[at most six hexadecimal digits following a backslash] stand
    // for the ISO 10646 character with that number, which must not be
    // zero. (It is undefined in CSS 2.1 what happens if a style sheet
    // does contain a character with Unicode codepoint zero.)"
    //   -- CSS2.1 section 4.1.3
    //
    // Silently deleting \0 opens a content-filtration loophole (see
    // bug 228856), so what we do instead is pretend the "cancels the
    // meaning of special characters" rule applied.
    if (rv > 0) {
      AppendUCS4ToUTF16(ENSURE_VALID_CHAR(rv), aOutput);
    } else {
      while (i--)
        aOutput.Append('0');
      if (IsWhitespace(ch))
        Pushback(ch);
    }
    return true;
  } 
  // "Any character except a hexidecimal digit can be escaped to
  // remove its special meaning by putting a backslash in front"
  // -- CSS1 spec section 7.1
  if (ch == '\n') {
    if (!aInString) {
      // Outside of strings (which includes url() that contains a
      // string), escaped newlines aren't special, and just tokenize as
      // eCSSToken_Symbol (DELIM).
      Pushback(ch);
      return false;
    }
    // In strings (and in url() containing a string), escaped newlines
    // are just dropped to allow splitting over multiple lines.
  } else {
    aOutput.Append(ch);
  }

  return true;
}

/**
 * Gather up the characters in an identifier. The identfier was
 * started by "aChar" which will be appended to aIdent. The result
 * will be aIdent with all of the identifier characters appended
 * until the first non-identifier character is seen. The termination
 * character is unread for the future re-reading.
 *
 * Returns failure when the character sequence does not form an ident at
 * all, in which case the caller is responsible for pushing back or
 * otherwise handling aChar.  (This occurs only when aChar is '\'.)
 */
bool
nsCSSScanner::GatherIdent(PRInt32 aChar, nsString& aIdent)
{
  if (aChar == '\\') {
    if (!ParseAndAppendEscape(aIdent, false)) {
      return false;
    }
  }
  else if (0 < aChar) {
    aIdent.Append(aChar);
  }
  for (;;) {
    // If nothing in pushback, first try to get as much as possible in one go
    if (!mPushbackCount && mOffset < mCount) {
      // See how much we can consume and append in one go
      PRUint32 n = mOffset;
      // Count number of Ident characters that can be processed
      while (n < mCount && IsIdent(mReadPointer[n])) {
        ++n;
      }
      // Add to the token what we have so far
      if (n > mOffset) {
#ifdef CSS_REPORT_PARSE_ERRORS
        mColNumber += n - mOffset;
#endif
        aIdent.Append(&mReadPointer[mOffset], n - mOffset);
        mOffset = n;
      }
    }

    aChar = Read();
    if (aChar < 0) break;
    if (aChar == '\\') {
      if (!ParseAndAppendEscape(aIdent, false)) {
        Pushback(aChar);
        break;
      }
    } else if (IsIdent(aChar)) {
      aIdent.Append(PRUnichar(aChar));
    } else {
      Pushback(aChar);
      break;
    }
  }
  return true;
}

bool
nsCSSScanner::ParseRef(PRInt32 aChar, nsCSSToken& aToken)
{
  // Fall back for when we don't have name characters following:
  aToken.mType = eCSSToken_Symbol;
  aToken.mSymbol = aChar;

  PRInt32 ch = Read();
  if (ch < 0) {
    return true;
  }
  if (IsIdent(ch) || ch == '\\') {
    // First char after the '#' is a valid ident char (or an escape),
    // so it makes sense to keep going
    nsCSSTokenType type =
      StartsIdent(ch, Peek()) ? eCSSToken_ID : eCSSToken_Ref;
    aToken.mIdent.SetLength(0);
    if (GatherIdent(ch, aToken.mIdent)) {
      aToken.mType = type;
      return true;
    }
  }

  // No ident chars after the '#'.  Just unread |ch| and get out of here.
  Pushback(ch);
  return true;
}

bool
nsCSSScanner::ParseIdent(PRInt32 aChar, nsCSSToken& aToken)
{
  nsString& ident = aToken.mIdent;
  ident.SetLength(0);
  if (!GatherIdent(aChar, ident)) {
    aToken.mType = eCSSToken_Symbol;
    aToken.mSymbol = aChar;
    return true;
  }

  nsCSSTokenType tokenType = eCSSToken_Ident;
  // look for functions (ie: "ident(")
  if (Peek() == PRUnichar('(')) {
    Read();
    tokenType = eCSSToken_Function;

    if (ident.LowerCaseEqualsLiteral("url")) {
      NextURL(aToken); // ignore return value, since *we* read something
      return true;
    }
  }

  aToken.mType = tokenType;
  return true;
}

bool
nsCSSScanner::ParseAtKeyword(PRInt32 aChar, nsCSSToken& aToken)
{
  aToken.mIdent.SetLength(0);
  aToken.mType = eCSSToken_AtKeyword;
  if (!GatherIdent(0, aToken.mIdent)) {
    aToken.mType = eCSSToken_Symbol;
    aToken.mSymbol = PRUnichar('@');
  }
  return true;
}

bool
nsCSSScanner::ParseNumber(PRInt32 c, nsCSSToken& aToken)
{
  NS_PRECONDITION(c == '.' || c == '+' || c == '-' || IsDigit(c),
                  "Why did we get called?");
  aToken.mHasSign = (c == '+' || c == '-');

  // Our sign.
  PRInt32 sign = c == '-' ? -1 : 1;
  // Absolute value of the integer part of the mantissa.  This is a double so
  // we don't run into overflow issues for consumers that only care about our
  // floating-point value while still being able to express the full PRInt32
  // range for consumers who want integers.
  double intPart = 0;
  // Fractional part of the mantissa.  This is a double so that when we convert
  // to float at the end we'll end up rounding to nearest float instead of
  // truncating down (as we would if fracPart were a float and we just
  // effectively lost the last several digits).
  double fracPart = 0;
  // Absolute value of the power of 10 that we should multiply by (only
  // relevant for numbers in scientific notation).  Has to be a signed integer,
  // because multiplication of signed by unsigned converts the unsigned to
  // signed, so if we plan to actually multiply by expSign...
  PRInt32 exponent = 0;
  // Sign of the exponent.
  PRInt32 expSign = 1;

  if (aToken.mHasSign) {
    NS_ASSERTION(c != '.', "How did that happen?");
    c = Read();
  }

  bool gotDot = (c == '.');

  if (!gotDot) {
    // Parse the integer part of the mantisssa
    NS_ASSERTION(IsDigit(c), "Why did we get called?");
    do {
      intPart = 10*intPart + DecimalDigitValue(c);
      c = Read();
      // The IsDigit check will do the right thing even if Read() returns < 0
    } while (IsDigit(c));

    gotDot = (c == '.') && IsDigit(Peek());
  }

  if (gotDot) {
    // Parse the fractional part of the mantissa.
    c = Read();
    NS_ASSERTION(IsDigit(c), "How did we get here?");
    // Power of ten by which we need to divide our next digit
    float divisor = 10;
    do {
      fracPart += DecimalDigitValue(c) / divisor;
      divisor *= 10;
      c = Read();
      // The IsDigit check will do the right thing even if Read() returns < 0
    } while (IsDigit(c));
  }

  bool gotE = false;
  if (IsSVGMode() && (c == 'e' || c == 'E')) {
    PRInt32 nextChar = Peek();
    PRInt32 expSignChar = 0;
    if (nextChar == '-' || nextChar == '+') {
      expSignChar = Read();
      nextChar = Peek();
    }
    if (IsDigit(nextChar)) {
      gotE = true;
      if (expSignChar == '-') {
        expSign = -1;
      }

      c = Read();
      NS_ASSERTION(IsDigit(c), "Peek() must have lied");
      do {
        exponent = 10*exponent + DecimalDigitValue(c);
        c = Read();
        // The IsDigit check will do the right thing even if Read() returns < 0
      } while (IsDigit(c));
    } else {
      if (expSignChar) {
        Pushback(expSignChar);
      }
    }
  }

  nsCSSTokenType type = eCSSToken_Number;

  // Set mIntegerValid for all cases (except %, below) because we need
  // it for the "2n" in :nth-child(2n).
  aToken.mIntegerValid = false;

  // Time to reassemble our number.
  float value = float(sign * (intPart + fracPart));
  if (gotE) {
    // pow(), not powf(), because at least wince doesn't have the latter.
    // And explicitly cast everything to doubles to avoid issues with
    // overloaded pow() on Windows.
    value *= pow(10.0, double(expSign * exponent));
  } else if (!gotDot) {
    // Clamp values outside of integer range.
    if (sign > 0) {
      aToken.mInteger = PRInt32(NS_MIN(intPart, double(PR_INT32_MAX)));
    } else {
      aToken.mInteger = PRInt32(NS_MAX(-intPart, double(PR_INT32_MIN)));
    }
    aToken.mIntegerValid = true;
  }

  nsString& ident = aToken.mIdent;
  ident.Truncate();

  // Look at character that terminated the number
  if (c >= 0) {
    if (StartsIdent(c, Peek())) {
      if (GatherIdent(c, ident)) {
        type = eCSSToken_Dimension;
      }
    } else if ('%' == c) {
      type = eCSSToken_Percentage;
      value = value / 100.0f;
      aToken.mIntegerValid = false;
    } else {
      // Put back character that stopped numeric scan
      Pushback(c);
    }
  }
  aToken.mNumber = value;
  aToken.mType = type;
  return true;
}

bool
nsCSSScanner::SkipCComment()
{
  for (;;) {
    PRInt32 ch = Read();
    if (ch < 0) break;
    if (ch == '*') {
      if (LookAhead('/')) {
        return true;
      }
    }
  }

  REPORT_UNEXPECTED_EOF(PECommentEOF);
  return false;
}

bool
nsCSSScanner::ParseString(PRInt32 aStop, nsCSSToken& aToken)
{
  aToken.mIdent.SetLength(0);
  aToken.mType = eCSSToken_String;
  aToken.mSymbol = PRUnichar(aStop); // remember how it's quoted
  for (;;) {
    // If nothing in pushback, first try to get as much as possible in one go
    if (!mPushbackCount && mOffset < mCount) {
      // See how much we can consume and append in one go
      PRUint32 n = mOffset;
      // Count number of characters that can be processed
      for (;n < mCount; ++n) {
        PRUnichar nextChar = mReadPointer[n];
        if ((nextChar == aStop) || (nextChar == '\\') ||
            (nextChar == '\n') || (nextChar == '\r') || (nextChar == '\f')) {
          break;
        }
#ifdef CSS_REPORT_PARSE_ERRORS
        ++mColNumber;
#endif
      }
      // Add to the token what we have so far
      if (n > mOffset) {
        aToken.mIdent.Append(&mReadPointer[mOffset], n - mOffset);
        mOffset = n;
      }
    }
    PRInt32 ch = Read();
    if (ch < 0 || ch == aStop) {
      break;
    }
    if (ch == '\n') {
      aToken.mType = eCSSToken_Bad_String;
#ifdef CSS_REPORT_PARSE_ERRORS
      ReportUnexpectedToken(aToken, "SEUnterminatedString");
#endif
      break;
    }
    if (ch == '\\') {
      if (!ParseAndAppendEscape(aToken.mIdent, true)) {
        aToken.mType = eCSSToken_Bad_String;
        Pushback(ch);
#ifdef CSS_REPORT_PARSE_ERRORS
        // For strings, the only case where ParseAndAppendEscape will
        // return false is when there's a backslash to start an escape
        // immediately followed by end-of-stream.  In that case, the
        // correct tokenization is badstring *followed* by a DELIM for
        // the backslash, but as far as the author is concerned, it
        // works pretty much the same as an unterminated string, so we
        // use the same error message.
        ReportUnexpectedToken(aToken, "SEUnterminatedString");
#endif
        break;
      }
    } else {
      aToken.mIdent.Append(ch);
    }
  }
  return true;
}

// UNICODE-RANGE tokens match the regular expression
//
//     u\+[0-9a-f?]{1,6}(-[0-9a-f]{1,6})?
//
// However, some such tokens are "invalid".  There are three valid forms:
//
//     u+[0-9a-f]{x}              1 <= x <= 6
//     u+[0-9a-f]{x}\?{y}         1 <= x+y <= 6
//     u+[0-9a-f]{x}-[0-9a-f]{y}  1 <= x <= 6, 1 <= y <= 6
//
// All unicode-range tokens have their text recorded in mIdent; valid ones
// are also decoded into mInteger and mInteger2, and mIntegerValid is set.

bool
nsCSSScanner::ParseURange(PRInt32 aChar, nsCSSToken& aResult)
{
  PRInt32 intro2 = Read();
  PRInt32 ch = Peek();

  // We should only ever be called if these things are true.
  NS_ASSERTION(aChar == 'u' || aChar == 'U',
               "unicode-range called with improper introducer (U)");
  NS_ASSERTION(intro2 == '+',
               "unicode-range called with improper introducer (+)");

  // If the character immediately after the '+' is not a hex digit or
  // '?', this is not really a unicode-range token; push everything
  // back and scan the U as an ident.
  if (!IsHexDigit(ch) && ch != '?') {
    Pushback(intro2);
    Pushback(aChar);
    return ParseIdent(aChar, aResult);
  }

  aResult.mIdent.Truncate();
  aResult.mIdent.Append(aChar);
  aResult.mIdent.Append(intro2);

  bool valid = true;
  bool haveQues = false;
  PRUint32 low = 0;
  PRUint32 high = 0;
  int i = 0;

  for (;;) {
    ch = Read();
    i++;
    if (i == 7 || !(IsHexDigit(ch) || ch == '?')) {
      break;
    }

    aResult.mIdent.Append(ch);
    if (IsHexDigit(ch)) {
      if (haveQues) {
        valid = false; // all question marks should be at the end
      }
      low = low*16 + HexDigitValue(ch);
      high = high*16 + HexDigitValue(ch);
    } else {
      haveQues = true;
      low = low*16 + 0x0;
      high = high*16 + 0xF;
    }
  }

  if (ch == '-' && IsHexDigit(Peek())) {
    if (haveQues) {
      valid = false;
    }

    aResult.mIdent.Append(ch);
    high = 0;
    i = 0;
    for (;;) {
      ch = Read();
      i++;
      if (i == 7 || !IsHexDigit(ch)) {
        break;
      }
      aResult.mIdent.Append(ch);
      high = high*16 + HexDigitValue(ch);
    }
  }
  Pushback(ch);

  aResult.mInteger = low;
  aResult.mInteger2 = high;
  aResult.mIntegerValid = valid;
  aResult.mType = eCSSToken_URange;
  return true;
}
