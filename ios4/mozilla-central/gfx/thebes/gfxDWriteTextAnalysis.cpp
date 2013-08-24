/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Foundation code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2010
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Bas Schouten <bschouten@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include "gfxDWriteTextAnalysis.h"

TextAnalysis::TextAnalysis(const wchar_t* text,
                           UINT32 textLength,
                           const wchar_t* localeName,
                           DWRITE_READING_DIRECTION readingDirection)
  : mText(text)
  , mTextLength(textLength)
  , mLocaleName(localeName)
  , mReadingDirection(readingDirection)
  , mBreakpoints(NULL)
  , mRunHead(NULL)
  , mCurrentRun(NULL)
{
}

TextAnalysis::~TextAnalysis()
{
    delete [] mBreakpoints;
    for (Run *run = mRunHead; run;) {
        Run *origRun = run;
        run = run->nextRun;
        delete origRun;
    }
}

STDMETHODIMP 
TextAnalysis::GenerateResults(IDWriteTextAnalyzer* textAnalyzer,
                              OUT Run **runHead,
                              OUT DWRITE_LINE_BREAKPOINT **breakpoints)
{
    // Analyzes the text using each of the analyzers and returns
    // their results as a series of runs.

    HRESULT hr = S_OK;

    // Initially start out with one result that covers the entire range.
    // This result will be subdivided by the analysis processes.
    mRunHead = new Run;
    
    mRunHead->mTextStart = 0;
    mRunHead->mTextLength = mTextLength;
    mRunHead->mBidiLevel = 
        (mReadingDirection == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
    mRunHead->nextRun = NULL;
    mCurrentRun = mRunHead;
#ifdef USE_DWRITE_BREAKPOINTS
    delete [] mBreakpoints;
    mBreakpoints = new DWRITE_LINE_BREAKPOINT[mTextLength];
#endif

    // Call each of the analyzers in sequence, recording their results.
    if (
#ifdef USE_DWRITE_BREAKPOINTS
        SUCCEEDED(hr = textAnalyzer->AnalyzeLineBreakpoints(this,
                                                            0,
                                                            mTextLength,
                                                            this)) && 
#endif
        SUCCEEDED(hr = textAnalyzer->AnalyzeBidi(this,
                                                 0,
                                                 mTextLength,
                                                 this)) &&
        SUCCEEDED(hr = textAnalyzer->AnalyzeScript(this,
                                                   0,
                                                   mTextLength,
                                                   this)) &&
        SUCCEEDED(hr = textAnalyzer->AnalyzeNumberSubstitution(this,
                                                               0,
                                                               mTextLength,
                                                               this))) {
        *breakpoints = mBreakpoints;

        *runHead = mRunHead;
    }

    return hr;
}


////////////////////////////////////////////////////////////////////////////////
// IDWriteTextAnalysisSource source implementation

IFACEMETHODIMP 
TextAnalysis::GetTextAtPosition(UINT32 textPosition,
                                OUT WCHAR const** textString,
                                OUT UINT32* textLength)
{
    if (textPosition >= mTextLength) {
        // No text at this position, valid query though.
        *textString = NULL;
        *textLength = 0;
    } else {
        *textString = mText + textPosition;
        *textLength = mTextLength - textPosition;
    }
    return S_OK;
}


IFACEMETHODIMP 
TextAnalysis::GetTextBeforePosition(UINT32 textPosition,
                                    OUT WCHAR const** textString,
                                    OUT UINT32* textLength)
{
    if (textPosition == 0 || textPosition > mTextLength) {
        // Either there is no text before here (== 0), or this
        // is an invalid position. The query is considered valid thouh.
        *textString = NULL;
        *textLength = 0;
    } else {
        *textString = mText;
        *textLength = textPosition;
    }
    return S_OK;
}


DWRITE_READING_DIRECTION STDMETHODCALLTYPE 
TextAnalysis::GetParagraphReadingDirection()
{
    // We support only a single reading direction.
    return mReadingDirection;
}


IFACEMETHODIMP 
TextAnalysis::GetLocaleName(UINT32 textPosition,
                            OUT UINT32* textLength,
                            OUT WCHAR const** localeName)
{
    // Single locale name is used, valid until the end of the string.
    *localeName = mLocaleName;
    *textLength = mTextLength - textPosition;

    return S_OK;
}


IFACEMETHODIMP 
TextAnalysis::GetNumberSubstitution(UINT32 textPosition,
                                    OUT UINT32* textLength,
                                    OUT IDWriteNumberSubstitution** numberSubstitution)
{
    // We do not support number substitution.
    *numberSubstitution = NULL;
    *textLength = mTextLength - textPosition;

    return S_OK;
}


////////////////////////////////////////////////////////////////////////////////
// IDWriteTextAnalysisSink implementation

IFACEMETHODIMP 
TextAnalysis::SetLineBreakpoints(UINT32 textPosition,
                                 UINT32 textLength,
                                 DWRITE_LINE_BREAKPOINT const* lineBreakpoints)
{
    if (textLength > 0) {
        memcpy(mBreakpoints + textPosition,
               lineBreakpoints,
               textLength * sizeof(DWRITE_LINE_BREAKPOINT));
    }
    return S_OK;
}


IFACEMETHODIMP 
TextAnalysis::SetScriptAnalysis(UINT32 textPosition,
                                UINT32 textLength,
                                DWRITE_SCRIPT_ANALYSIS const* scriptAnalysis)
{
    SetCurrentRun(textPosition);
    SplitCurrentRun(textPosition);
    while (textLength > 0) {
        Run *run = FetchNextRun(&textLength);
        run->mScript = *scriptAnalysis;
    }

    return S_OK;
}


IFACEMETHODIMP 
TextAnalysis::SetBidiLevel(UINT32 textPosition,
                           UINT32 textLength,
                           UINT8 explicitLevel,
                           UINT8 resolvedLevel)
{
    SetCurrentRun(textPosition);
    SplitCurrentRun(textPosition);
    while (textLength > 0) {
        Run *run = FetchNextRun(&textLength);
        run->mBidiLevel = resolvedLevel;
    }

    return S_OK;
}


IFACEMETHODIMP 
TextAnalysis::SetNumberSubstitution(UINT32 textPosition,
                                    UINT32 textLength,
                                    IDWriteNumberSubstitution* numberSubstitution)
{
    // We don't use this for now.
    return S_OK;
}


////////////////////////////////////////////////////////////////////////////////
// Run modification.

TextAnalysis::Run *
TextAnalysis::FetchNextRun(IN OUT UINT32* textLength)
{
    // Used by the sink setters, this returns a reference to the next run.
    // Position and length are adjusted to now point after the current run
    // being returned.

    Run *origRun = mCurrentRun;
    // Split the tail if needed (the length remaining is less than the
    // current run's size).
    if (*textLength < mCurrentRun->mTextLength) {
        SplitCurrentRun(mCurrentRun->mTextStart + *textLength);
    } else {
        // Just advance the current run.
        mCurrentRun = mCurrentRun->nextRun;
    }
    *textLength -= origRun->mTextLength;

    // Return a reference to the run that was just current.
    return origRun;
}


void TextAnalysis::SetCurrentRun(UINT32 textPosition)
{
    // Move the current run to the given position.
    // Since the analyzers generally return results in a forward manner,
    // this will usually just return early. If not, find the
    // corresponding run for the text position.

    if (mCurrentRun && mCurrentRun->ContainsTextPosition(textPosition)) {
        return;
    }

    for (Run *run = mRunHead; run; run = run->nextRun) {
        if (run->ContainsTextPosition(textPosition)) {
            mCurrentRun = run;
            return;
        }
    }
    NS_NOTREACHED("We should always be able to find the text position in one \
        of our runs");
}


void TextAnalysis::SplitCurrentRun(UINT32 splitPosition)
{
    if (!mCurrentRun) {
        NS_ASSERTION(false, "SplitCurrentRun called without current run.");
        // Shouldn't be calling this when no current run is set!
        return;
    }
    // Split the current run.
    if (splitPosition <= mCurrentRun->mTextStart) {
        // No need to split, already the start of a run
        // or before it. Usually the first.
        return;
    }
    Run *newRun = new Run;

    *newRun = *mCurrentRun;

    // Insert the new run in our linked list.
    newRun->nextRun = mCurrentRun->nextRun;
    mCurrentRun->nextRun = newRun;

    // Adjust runs' text positions and lengths.
    UINT32 splitPoint = splitPosition - mCurrentRun->mTextStart;
    newRun->mTextStart += splitPoint;
    newRun->mTextLength -= splitPoint;
    mCurrentRun->mTextLength = splitPoint;
    mCurrentRun = newRun;
}
