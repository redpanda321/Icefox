/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * implementation of interface that allows layout-debug extension access
 * to some internals of layout
 */

#include "nsILayoutDebugger.h"
#include "nsFrame.h"
#include "nsDisplayList.h"
#include "FrameLayerBuilder.h"

#include <stdio.h>

using namespace mozilla::layers;

#ifdef DEBUG
class nsLayoutDebugger : public nsILayoutDebugger {
public:
  nsLayoutDebugger();
  virtual ~nsLayoutDebugger();

  NS_DECL_ISUPPORTS

  NS_IMETHOD SetShowFrameBorders(bool aEnable);

  NS_IMETHOD GetShowFrameBorders(bool* aResult);

  NS_IMETHOD SetShowEventTargetFrameBorder(bool aEnable);

  NS_IMETHOD GetShowEventTargetFrameBorder(bool* aResult);

  NS_IMETHOD GetContentSize(nsIDocument* aDocument,
                            PRInt32* aSizeInBytesResult);

  NS_IMETHOD GetFrameSize(nsIPresShell* aPresentation,
                          PRInt32* aSizeInBytesResult);

  NS_IMETHOD GetStyleSize(nsIPresShell* aPresentation,
                          PRInt32* aSizeInBytesResult);

};

nsresult
NS_NewLayoutDebugger(nsILayoutDebugger** aResult)
{
  NS_PRECONDITION(aResult, "null OUT ptr");
  if (!aResult) {
    return NS_ERROR_NULL_POINTER;
  }
  nsLayoutDebugger* it = new nsLayoutDebugger();
  return it->QueryInterface(NS_GET_IID(nsILayoutDebugger), (void**)aResult);
}

nsLayoutDebugger::nsLayoutDebugger()
{
}

nsLayoutDebugger::~nsLayoutDebugger()
{
}

NS_IMPL_ISUPPORTS1(nsLayoutDebugger, nsILayoutDebugger)

NS_IMETHODIMP
nsLayoutDebugger::SetShowFrameBorders(bool aEnable)
{
  nsFrame::ShowFrameBorders(aEnable);
  return NS_OK;
}

NS_IMETHODIMP
nsLayoutDebugger::GetShowFrameBorders(bool* aResult)
{
  *aResult = nsFrame::GetShowFrameBorders();
  return NS_OK;
}

NS_IMETHODIMP
nsLayoutDebugger::SetShowEventTargetFrameBorder(bool aEnable)
{
  nsFrame::ShowEventTargetFrameBorder(aEnable);
  return NS_OK;
}

NS_IMETHODIMP
nsLayoutDebugger::GetShowEventTargetFrameBorder(bool* aResult)
{
  *aResult = nsFrame::GetShowEventTargetFrameBorder();
  return NS_OK;
}

NS_IMETHODIMP
nsLayoutDebugger::GetContentSize(nsIDocument* aDocument,
                                 PRInt32* aSizeInBytesResult)
{
  *aSizeInBytesResult = 0;
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsLayoutDebugger::GetFrameSize(nsIPresShell* aPresentation,
                               PRInt32* aSizeInBytesResult)
{
  *aSizeInBytesResult = 0;
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsLayoutDebugger::GetStyleSize(nsIPresShell* aPresentation,
                               PRInt32* aSizeInBytesResult)
{
  *aSizeInBytesResult = 0;
  return NS_ERROR_FAILURE;
}
#endif

#ifdef MOZ_DUMP_PAINTING
static void
PrintDisplayListTo(nsDisplayListBuilder* aBuilder, const nsDisplayList& aList,
                   FILE* aOutput)
{
  fprintf(aOutput, "<ul>");

  for (nsDisplayItem* i = aList.GetBottom(); i != nsnull; i = i->GetAbove()) {
#ifdef DEBUG
    if (aList.DidComputeVisibility() && i->GetVisibleRect().IsEmpty())
      continue;
#endif
    fprintf(aOutput, "<li>");
    nsIFrame* f = i->GetUnderlyingFrame();
    nsAutoString fName;
#ifdef DEBUG
    if (f) {
      f->GetFrameName(fName);
    }
#endif
    bool snap;
    nsRect rect = i->GetBounds(aBuilder, &snap);
    switch (i->GetType()) {
      case nsDisplayItem::TYPE_CLIP:
      case nsDisplayItem::TYPE_CLIP_ROUNDED_RECT: {
        nsDisplayClip* c = static_cast<nsDisplayClip*>(i);
        rect = c->GetClipRect();
        break;
      }
      default:
        break;
    }
    nscolor color;
    nsRect vis = i->GetVisibleRect();
    nsDisplayList* list = i->GetList();
    nsRegion opaque;
    if (i->GetType() == nsDisplayItem::TYPE_TRANSFORM) {
        nsDisplayTransform* t = static_cast<nsDisplayTransform*>(i);
        list = t->GetStoredList()->GetList();
    }
#ifdef DEBUG
    if (!list || list->DidComputeVisibility()) {
      opaque = i->GetOpaqueRegion(aBuilder, &snap);
    }
#endif
    if (i->Painted()) {
      nsCString string(i->Name());
      string.Append("-");
      string.AppendInt((PRUint64)i);
      fprintf(aOutput, "<a href=\"javascript:ViewImage('%s')\">", string.BeginReading());
    }
    fprintf(aOutput, "%s %p(%s) (%d,%d,%d,%d)(%d,%d,%d,%d)%s%s",
            i->Name(), (void*)f, NS_ConvertUTF16toUTF8(fName).get(),
            rect.x, rect.y, rect.width, rect.height,
            vis.x, vis.y, vis.width, vis.height,
            opaque.IsEmpty() ? "" : " opaque",
            i->IsUniform(aBuilder, &color) ? " uniform" : "");
    if (i->Painted()) {
      fprintf(aOutput, "</a>");
    }
    if (f) {
      PRUint32 key = i->GetPerFrameKey();
      Layer* layer = aBuilder->LayerBuilder()->GetOldLayerFor(f, key);
      if (layer) {
        fprintf(aOutput, " <a href=\"#%p\">layer=%p</a>", layer, layer);
      }
    }
    if (i->GetType() == nsDisplayItem::TYPE_SVG_EFFECTS) {
      (static_cast<nsDisplaySVGEffects*>(i))->PrintEffects(aOutput);
    }
    fputc('\n', aOutput);
    if (list) {
      PrintDisplayListTo(aBuilder, *list, aOutput);
    }
    fprintf(aOutput, "</li>");
  }
  
  fprintf(aOutput, "</ul>");
}

void
nsFrame::PrintDisplayList(nsDisplayListBuilder* aBuilder,
                          const nsDisplayList& aList,
                          FILE* aFile)
{
  PrintDisplayListTo(aBuilder, aList, aFile);
}

#endif
