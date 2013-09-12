/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __NS_SVGPATHDATAPARSER_H__
#define __NS_SVGPATHDATAPARSER_H__

#include "gfxPoint.h"
#include "nsSVGDataParser.h"

namespace mozilla {
class SVGPathData;
}

////////////////////////////////////////////////////////////////////////
// nsSVGPathDataParser: a simple recursive descent parser that builds
// DOMSVGPathSegs from path data strings. The grammar for path data
// can be found in SVG CR 20001102, chapter 8.

class nsSVGPathDataParser : public nsSVGDataParser
{
protected:
  // Path data storage
  virtual nsresult StoreMoveTo(bool absCoords, float x, float y) = 0;
  virtual nsresult StoreClosePath() = 0;
  virtual nsresult StoreLineTo(bool absCoords, float x, float y) = 0;
  virtual nsresult StoreHLineTo(bool absCoords, float x) = 0;
  virtual nsresult StoreVLineTo(bool absCoords, float y) = 0;
  virtual nsresult StoreCurveTo(bool absCoords, float x, float y,
                                float x1, float y1, float x2, float y2) = 0;
  virtual nsresult StoreSmoothCurveTo(bool absCoords, float x, float y,
                                      float x2, float y2) = 0;
  virtual nsresult StoreQuadCurveTo(bool absCoords, float x, float y,
                                    float x1, float y1) = 0;
  virtual nsresult StoreSmoothQuadCurveTo(bool absCoords,
                                          float x, float y) = 0;
  virtual nsresult StoreEllipticalArc(bool absCoords, float x, float y,
                                      float r1, float r2, float angle,
                                      bool largeArcFlag, bool sweepFlag) = 0;
  virtual nsresult Match();
 
  nsresult MatchCoordPair(float* aX, float* aY);
  bool IsTokenCoordPairStarter();

  nsresult MatchCoord(float* aX);
  bool IsTokenCoordStarter();

  nsresult MatchFlag(bool* f);

  nsresult MatchSvgPath();
  
  nsresult MatchSubPaths();
  bool IsTokenSubPathsStarter();
  
  nsresult MatchSubPath();
  bool IsTokenSubPathStarter();
  
  nsresult MatchSubPathElements();
  bool IsTokenSubPathElementsStarter();

  nsresult MatchSubPathElement();
  bool IsTokenSubPathElementStarter();

  nsresult MatchMoveto();
  nsresult MatchMovetoArgSeq(bool absCoords);
  
  nsresult MatchClosePath();
  
  nsresult MatchLineto();
  
  nsresult MatchLinetoArgSeq(bool absCoords);
  bool IsTokenLinetoArgSeqStarter();
  
  nsresult MatchHorizontalLineto();
  nsresult MatchHorizontalLinetoArgSeq(bool absCoords);
  
  nsresult MatchVerticalLineto();
  nsresult MatchVerticalLinetoArgSeq(bool absCoords);
  
  nsresult MatchCurveto();
  nsresult MatchCurvetoArgSeq(bool absCoords);
  nsresult MatchCurvetoArg(float* x, float* y, float* x1,
                           float* y1, float* x2, float* y2);
  bool IsTokenCurvetoArgStarter();
  
  nsresult MatchSmoothCurveto();
  nsresult MatchSmoothCurvetoArgSeq(bool absCoords);
  nsresult MatchSmoothCurvetoArg(float* x, float* y, float* x2, float* y2);
  bool IsTokenSmoothCurvetoArgStarter();
  
  nsresult MatchQuadBezierCurveto();
  nsresult MatchQuadBezierCurvetoArgSeq(bool absCoords);  
  nsresult MatchQuadBezierCurvetoArg(float* x, float* y, float* x1, float* y1);
  bool IsTokenQuadBezierCurvetoArgStarter();
  
  nsresult MatchSmoothQuadBezierCurveto();  
  nsresult MatchSmoothQuadBezierCurvetoArgSeq(bool absCoords);
  
  nsresult MatchEllipticalArc();  
  nsresult MatchEllipticalArcArgSeq(bool absCoords);
  nsresult MatchEllipticalArcArg(float* x, float* y,
                                 float* r1, float* r2, float* angle,
                                 bool* largeArcFlag, bool* sweepFlag);
  bool IsTokenEllipticalArcArgStarter();
  
 };

class nsSVGArcConverter
{
public:
  nsSVGArcConverter(const gfxPoint &from,
                    const gfxPoint &to,
                    const gfxPoint &radii,
                    double angle,
                    bool largeArcFlag,
                    bool sweepFlag);
  bool GetNextSegment(gfxPoint *cp1, gfxPoint *cp2, gfxPoint *to);
protected:
  int32_t mNumSegs, mSegIndex;
  double mTheta, mDelta, mT;
  double mSinPhi, mCosPhi;
  double mRx, mRy;
  gfxPoint mFrom, mC;
};

class nsSVGPathDataParserToInternal : public nsSVGPathDataParser
{
public:
  nsSVGPathDataParserToInternal(mozilla::SVGPathData *aList)
    : mPathSegList(aList)
  {}
  nsresult Parse(const nsAString &aValue);

protected:
  virtual nsresult StoreMoveTo(bool absCoords, float x, float y);
  virtual nsresult StoreClosePath();
  virtual nsresult StoreLineTo(bool absCoords, float x, float y);
  virtual nsresult StoreHLineTo(bool absCoords, float x);
  virtual nsresult StoreVLineTo(bool absCoords, float y);
  virtual nsresult StoreCurveTo(bool absCoords, float x, float y,
                                float x1, float y1, float x2, float y2);
  virtual nsresult StoreSmoothCurveTo(bool absCoords, float x, float y,
                                      float x2, float y2);
  virtual nsresult StoreQuadCurveTo(bool absCoords, float x, float y,
                                    float x1, float y1);
  virtual nsresult StoreSmoothQuadCurveTo(bool absCoords,
                                          float x, float y);
  virtual nsresult StoreEllipticalArc(bool absCoords, float x, float y,
                                      float r1, float r2, float angle,
                                      bool largeArcFlag, bool sweepFlag);

private:
  mozilla::SVGPathData *mPathSegList;
};

#endif // __NS_SVGPATHDATAPARSER_H__
