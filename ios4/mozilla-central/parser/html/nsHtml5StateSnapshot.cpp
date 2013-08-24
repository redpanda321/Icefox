/*
 * Copyright (c) 2009 Mozilla Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a 
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation 
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, 
 * and/or sell copies of the Software, and to permit persons to whom the 
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in 
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * THIS IS A GENERATED FILE. PLEASE DO NOT EDIT.
 * Please edit StateSnapshot.java instead and regenerate.
 */

#define nsHtml5StateSnapshot_cpp__

#include "prtypes.h"
#include "nsIAtom.h"
#include "nsHtml5AtomTable.h"
#include "nsString.h"
#include "nsINameSpaceManager.h"
#include "nsIContent.h"
#include "nsIDocument.h"
#include "nsTraceRefcnt.h"
#include "jArray.h"
#include "nsHtml5DocumentMode.h"
#include "nsHtml5ArrayCopy.h"
#include "nsHtml5NamedCharacters.h"
#include "nsHtml5NamedCharactersAccel.h"
#include "nsHtml5Atoms.h"
#include "nsHtml5ByteReadable.h"
#include "nsIUnicodeDecoder.h"
#include "nsAHtml5TreeBuilderState.h"
#include "nsHtml5Macros.h"

#include "nsHtml5Tokenizer.h"
#include "nsHtml5TreeBuilder.h"
#include "nsHtml5MetaScanner.h"
#include "nsHtml5AttributeName.h"
#include "nsHtml5ElementName.h"
#include "nsHtml5HtmlAttributes.h"
#include "nsHtml5StackNode.h"
#include "nsHtml5UTF16Buffer.h"
#include "nsHtml5Portability.h"

#include "nsHtml5StateSnapshot.h"


nsHtml5StateSnapshot::nsHtml5StateSnapshot(jArray<nsHtml5StackNode*,PRInt32> stack, jArray<nsHtml5StackNode*,PRInt32> listOfActiveFormattingElements, nsIContent** formPointer, nsIContent** headPointer, PRInt32 mode, PRInt32 originalMode, PRBool framesetOk, PRBool inForeign, PRBool needToDropLF, PRBool quirks)
  : stack(stack),
    listOfActiveFormattingElements(listOfActiveFormattingElements),
    formPointer(formPointer),
    headPointer(headPointer),
    mode(mode),
    originalMode(originalMode),
    framesetOk(framesetOk),
    inForeign(inForeign),
    needToDropLF(needToDropLF),
    quirks(quirks)
{
  MOZ_COUNT_CTOR(nsHtml5StateSnapshot);
}

jArray<nsHtml5StackNode*,PRInt32> 
nsHtml5StateSnapshot::getStack()
{
  return stack;
}

jArray<nsHtml5StackNode*,PRInt32> 
nsHtml5StateSnapshot::getListOfActiveFormattingElements()
{
  return listOfActiveFormattingElements;
}

nsIContent** 
nsHtml5StateSnapshot::getFormPointer()
{
  return formPointer;
}

nsIContent** 
nsHtml5StateSnapshot::getHeadPointer()
{
  return headPointer;
}

PRInt32 
nsHtml5StateSnapshot::getMode()
{
  return mode;
}

PRInt32 
nsHtml5StateSnapshot::getOriginalMode()
{
  return originalMode;
}

PRBool 
nsHtml5StateSnapshot::isFramesetOk()
{
  return framesetOk;
}

PRBool 
nsHtml5StateSnapshot::isInForeign()
{
  return inForeign;
}

PRBool 
nsHtml5StateSnapshot::isNeedToDropLF()
{
  return needToDropLF;
}

PRBool 
nsHtml5StateSnapshot::isQuirks()
{
  return quirks;
}

PRInt32 
nsHtml5StateSnapshot::getListOfActiveFormattingElementsLength()
{
  return listOfActiveFormattingElements.length;
}

PRInt32 
nsHtml5StateSnapshot::getStackLength()
{
  return stack.length;
}


nsHtml5StateSnapshot::~nsHtml5StateSnapshot()
{
  MOZ_COUNT_DTOR(nsHtml5StateSnapshot);
  for (PRInt32 i = 0; i < stack.length; i++) {
    stack[i]->release();
  }
  stack.release();
  for (PRInt32 i = 0; i < listOfActiveFormattingElements.length; i++) {
    if (listOfActiveFormattingElements[i]) {
      listOfActiveFormattingElements[i]->release();
    }
  }
  listOfActiveFormattingElements.release();
  ;
}

void
nsHtml5StateSnapshot::initializeStatics()
{
}

void
nsHtml5StateSnapshot::releaseStatics()
{
}


