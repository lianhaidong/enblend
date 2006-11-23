/*
 * Copyright (C) 2004-2005 Andrew Mihal
 *
 * This file is part of Enblend.
 *
 * Enblend is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Enblend is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Enblend; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __MASK_H__
#define __MASK_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <iostream>
#ifdef _WIN32
#include <slist>
#else
#include <ext/slist>
#endif

#include "common.h"
#include "anneal.h"
#include "nearest.h"
#include "path.h"

#include "vigra/contourcirculator.hxx"
#include "vigra/error.hxx"
#include "vigra/functorexpression.hxx"
#include "vigra/impex.hxx"
#include "vigra/initimage.hxx"
#include "vigra/numerictraits.hxx"
#include "vigra/transformimage.hxx"
#include "vigra/stdcachedfileimage.hxx"
#include "vigra_ext/impexalpha.hxx"
#include "vigra_ext/XMIWrapper.h"

#include <boost/lambda/lambda.hpp>
#include <boost/lambda/construct.hpp>
#include <boost/lambda/if.hpp>

using std::make_pair;
using std::vector;
#ifdef _WIN32
using std::slist;
#else
using __gnu_cxx::slist;
#endif

using vigra::combineThreeImages;
using vigra::combineTwoImages;
using vigra::CrackContourCirculator;
using vigra::exportImage;
using vigra::ImageExportInfo;
using vigra::ImageImportInfo;
using vigra::importImageAlpha;
using vigra::initImageIf;
using vigra::NumericTraits;
using vigra::Point2D;
using vigra::RGBToGrayAccessor;
using vigra::transformImage;
using vigra::transformImageIf;

using vigra::functor::Arg1;
using vigra::functor::Arg2;
using vigra::functor::Arg3;
using vigra::functor::ifThenElse;
using vigra::functor::Param;
using vigra::functor::UnaryFunctor;

using vigra_ext::copyPaintedSetToImage;

using boost::lambda::_1;
using boost::lambda::_2;
using boost::lambda::if_then_else_return;
using boost::lambda::constant;

namespace enblend {

template <typename PixelType, typename ResultType>
class PixelDifferenceFunctor
{
public:
    ResultType operator()(const PixelType & a, const PixelType & b) const {
        ResultType aLum = a.luminance();
        ResultType bLum = b.luminance();
        ResultType aHue = a.hue();
        ResultType bHue = b.hue();
        ResultType lumDiff = std::abs(aLum - bLum);
        ResultType hueDiff = std::abs(aHue - bHue);
        if (hueDiff > (NumericTraits<ResultType>::max() / 2)) hueDiff = NumericTraits<ResultType>::max() - hueDiff;
        return std::max(hueDiff, lumDiff);
    }
};

/** Calculate a blending mask between whiteImage and blackImage.
 */
template <typename ImageType, typename AlphaType, typename MaskType>
MaskType *createMask(ImageType *white,
        ImageType *black,
        AlphaType *whiteAlpha,
        AlphaType *blackAlpha,
        EnblendROI &uBB,
        EnblendROI &iBB,
        bool wraparound,
        EnblendROI &mBB) {

    typedef typename ImageType::PixelType ImagePixelType;
    typedef typename MaskType::PixelType MaskPixelType;
    typedef typename MaskType::traverser MaskIteratorType;
    typedef typename MaskType::Accessor MaskAccessor;

    //// Read mask from a file instead of calculating it.
    //// Be sure to still calculate mBB below.
    //MaskType *fileMask = new MaskType(uBB.size());
    //ImageImportInfo fileMaskInfo("enblend_mask.tif");
    //importImage(fileMaskInfo, destImage(*fileMask));
    //MaskType *mask = fileMask;

    // uBB rounded up to multiple of 8 pixels in each direction
    Diff2D stride8_size(((uBB.size().x + 7) >> 3), ((uBB.size().y + 7) >> 3));

    // range of stride8 pixels that intersect uBB
    EnblendROI stride8_initBB;
    stride8_initBB.setCorners(Diff2D(0, 0), Diff2D((uBB.size().x >> 3), (uBB.size().y >> 3)));

    cout << "uBB=" << uBB << endl;
    cout << "stride8_size=" << stride8_size << endl;
    cout << "stride8_initBB=" << stride8_initBB << endl;

    // Stride 8 mask
    MaskType *maskInit = new MaskType(stride8_size);
    // Mask initializer pixel values:
    // 0 = outside both black and white image, or inside both images.
    // 1 = inside white image only.
    // 255 = inside black image only.
    //MaskType *maskInit = new MaskType(uBB.size());
    // mem xsection = BImage*ubb

    combineTwoImages(stride(8, 8, uBB.apply(srcImageRange(*whiteAlpha))),
                     stride(8, 8, uBB.apply(srcImage(*blackAlpha))),
                     stride8_initBB.apply(destImage(*maskInit)),
                     ifThenElse(Arg1() ^ Arg2(),
                                ifThenElse(Arg1(),
                                           Param(NumericTraits<MaskPixelType>::one()),
                                           Param(NumericTraits<MaskPixelType>::max())),
                                Param(NumericTraits<MaskPixelType>::zero())));

    //ImageExportInfo maskInitInfo("enblend_mask_init.tif");
    //exportImage(srcImageRange(*maskInit), maskInitInfo);

    // Mask transform replaces 0 areas with either 1 or 255.
    //MaskType *maskTransform = new MaskType(uBB.size());
    //MaskType *maskTransform = new MaskType(stride8_size);
    MaskType *mask = new MaskType(stride8_size + Diff2D(2,2));
    // mem xsection = 2*BImage*ubb
    // ignore 1-pixel border around maskInit and maskTransform
    nearestFeatureTransform(wraparound,
            srcImageRange(*maskInit),
            destIter(mask->upperLeft() + Diff2D(1,1)));
    // mem xsection = 2*BImage*ubb + 2*UIImage*ubb

    delete maskInit;
    // mem xsection = BImage*ubb

    //MaskType *mask = new MaskType(uBB.size());
    // Add 1-pixel border all around so we can use the crackcontourcirculator
    //MaskType *mask = new MaskType(stride8_size + Diff2D(2,2));
    // mem xsection = BImage*ubb + MaskType*ubb

    // Dump maskTransform into mask
    // maskTransform = 1, then mask = max value (white image)
    // maskTransform != 1, then mask = zero - (black image)
    //transformImage(srcImageRange(*maskTransform),
    //        destIter(mask->upperLeft() + Diff2D(1,1)),
    //        ifThenElse(Arg1() == Param(NumericTraits<MaskPixelType>::one()),
    //                Param(NumericTraits<MaskPixelType>::max()),
    //                Param(NumericTraits<MaskPixelType>::zero())));

    //delete maskTransform;
    // mem xsection = MaskType*ubb

    //ImageExportInfo nearestMaskInfo("enblend_nearest_mask.tif");
    //exportImage(srcImageRange(*mask), nearestMaskInfo);

    typedef slist<pair<bool, Point2D> > Segment;
    typedef vector<Segment*> Contour;
    typedef vector<Contour*> ContourVector;

    Contour rawSegments;

    // 0 = uninitialized border region
    // 1 = white image
    // 255 = black image
    // Vectorize white regions in mask
    int distance = 4;
    Point2D borderUL(1,1);
    Point2D borderLR(mask->width()-1, mask->height()-1);
    MaskIteratorType my = mask->upperLeft() + Diff2D(1,1);
    MaskIteratorType mend = mask->lowerRight() + Diff2D(-1, -1);
    for (int y = 1; my.y < mend.y; ++y, ++my.y) {
        MaskIteratorType mx = my;
        MaskPixelType lastColor = NumericTraits<MaskPixelType>::max();

        for (int x = 1; mx.x < mend.x; ++x, ++mx.x) {
            if ((*mx == NumericTraits<MaskPixelType>::one()) && (lastColor == NumericTraits<MaskPixelType>::max())) {

                // Found the corner of a previously unvisited white region.
                // Create a snake to hold the border of this region.
                vector<Point2D> excessPoints;
                Segment *snake = new Segment();
                rawSegments.push_back(snake);

                // Walk around border of white region.
                CrackContourCirculator<MaskIteratorType> crack(mx);
                CrackContourCirculator<MaskIteratorType> crackEnd(crack);
                bool lastPointFrozen = false;
                int distanceLastPoint = 0;
                do {
                    Point2D currentPoint = *crack + Diff2D(x,y);
                    crack++;
                    Point2D nextPoint = *crack + Diff2D(x,y);

                    // See if currentPoint lies on border.
                    if ((currentPoint.x == borderUL.x) || (currentPoint.x == borderLR.x)
                            || (currentPoint.y == borderUL.y) || (currentPoint.y == borderLR.y)) {

                        // See if currentPoint is in a corner.
                        if ((currentPoint.x == borderUL.x && currentPoint.y == borderUL.y)
                                || (currentPoint.x == borderUL.x && currentPoint.y == borderLR.y)
                                || (currentPoint.x == borderLR.x && currentPoint.y == borderUL.y)
                                || (currentPoint.x == borderLR.x && currentPoint.y == borderLR.y)) {
                            snake->push_front(make_pair(false, currentPoint));
                            distanceLastPoint = 0;
                        }
                        else if (!lastPointFrozen
                                || ((nextPoint.x != borderUL.x) && (nextPoint.x != borderLR.x)
                                        && (nextPoint.y != borderUL.y) && (nextPoint.y != borderLR.y))) {
                            snake->push_front(make_pair(false, currentPoint));
                            distanceLastPoint = 0;
                        }
                        else {
                            excessPoints.push_back(currentPoint);
                        }
                        lastPointFrozen = true;
                    }
                    else {
                        // Current point is not frozen.
                        if ((distanceLastPoint % distance) == 0) {
                            snake->push_front(make_pair(true, currentPoint));
                            distanceLastPoint = 0;
                        } else {
                            excessPoints.push_back(currentPoint);
                        }
                        lastPointFrozen = false;
                    }
                    distanceLastPoint++;
                } while (crack != crackEnd);

                // Paint the border so this region will not be found again
                for (Segment::iterator vertexIterator = snake->begin();
                         vertexIterator != snake->end(); ++vertexIterator) {
                    (*mask)[vertexIterator->second] = NumericTraits<MaskPixelType>::zero();
                }
                for (vector<Point2D>::iterator vertexIterator = excessPoints.begin();
                        vertexIterator != excessPoints.end(); ++vertexIterator) {
                    (*mask)[*vertexIterator] = NumericTraits<MaskPixelType>::zero();
                }

            }

            lastColor = *mx;
        }
    }
    
    // Fill mask with union region
    mask->init(NumericTraits<MaskPixelType>::zero());
    combineTwoImages(stride(8, 8, srcIterRange(whiteAlpha->upperLeft() + uBB.getUL(), whiteAlpha->upperLeft() + uBB.getLR())),
            stride(8, 8, maskIter(blackAlpha->upperLeft() + uBB.getUL())),
            destIter(mask->upperLeft() + Diff2D(1,1)),
            ifThenElse(Arg1() || Arg2(), Param(NumericTraits<MaskPixelType>::max()), Param(NumericTraits<MaskPixelType>::zero())));

    // Mark movable snake vertices (vertices inside union region)
    for (Contour::iterator segments = rawSegments.begin();
            segments != rawSegments.end(); ++segments) {
        Segment *snake = *segments;
        for (Segment::iterator vertexIterator = snake->begin();
                vertexIterator != snake->end(); ++vertexIterator) {

            // Vertices outside union region are not moveable.
            if (vertexIterator->first &&
                    ((*mask)[vertexIterator->second] == NumericTraits<MaskPixelType>::zero())) {
                vertexIterator->first = false;
            }

            // Convert snake vertices to root-relative vertices
            vertexIterator->second = uBB.getUL() + (8 * (vertexIterator->second + Diff2D(-1,-1)));
        }
    }

    //ImageExportInfo smallMaskInfo("enblend_small_mask.tif");
    //exportImage(srcImageRange(*mask), smallMaskInfo);
    delete mask;

    // Convert snakes into segments with unbroken runs of moveable vertices
    ContourVector contours;
    for (Contour::iterator segments = rawSegments.begin();
            segments != rawSegments.end(); ++segments) {
        Segment *snake = *segments;

        // Snake becomes multiple separate segments in one contour
        Contour *currentContour = new Contour();
        contours.push_back(currentContour);

        // Find first nonmoveable vertex
        Segment::iterator vertexIterator = snake->begin();
        bool foundNonmoveableVertex = false;
        for (Segment::iterator vertexIterator = snake->begin();
                vertexIterator != snake->end(); ++vertexIterator) {
            if (!vertexIterator->first) {
                foundNonmoveableVertex = true;
                break;
            }
        }

        if (!foundNonmoveableVertex) {
            // snake consists of only moveable vertices.
            currentContour->push_back(snake);
        }
        else {
            // Move initial run of moveable vertices plus first nonmoveable vertex to end of snake
            Segment::iterator vertexIteratorPlusOne = vertexIterator; ++vertexIteratorPlusOne;
            snake->insert(snake->end(), snake->begin(), vertexIteratorPlusOne);
            snake->erase(snake->begin(), vertexIterator);

            Segment *currentSegment = NULL;
            bool startedMoveableSegment = false;
            for (Segment::iterator vertexIterator = snake->begin();
                    vertexIterator != snake->end(); ++vertexIterator) {
                if (currentSegment == NULL) {
                    currentSegment = new Segment();
                    currentContour->push_back(currentSegment);
                }
                currentSegment->push_front(*vertexIterator);
                cout << vertexIterator->first << " " << vertexIterator->second << endl;
                if (!startedMoveableSegment && vertexIterator->first) startedMoveableSegment = true;
                else if (startedMoveableSegment && !vertexIterator->first) {
                    // End of currentSegment.
                    // Correct for the push_fronts we've been doing
                    currentSegment->reverse();
                    // Cause a new CurrentSegment to be generated on next vertex.
                    startedMoveableSegment = false;
                    currentSegment = NULL;
                    cout << "starting new segment" << endl;
                }
            }
                    
            delete snake;
        }
    }
    rawSegments.clear();

    int totalSegments = 0;
    for (ContourVector::iterator currentContour = contours.begin(); currentContour != contours.end(); ++currentContour) {
        totalSegments += (*currentContour)->size();
    }
    if (totalSegments == 1) {
        cout << "There is 1 distinct seam." << endl;
    } else {
        cout << "There are " << totalSegments << " distinct seams." << endl;
    }
/*
    // Find extent of moveable snake vertices, and vertices bordering moveable vertices
    int leftExtent = NumericTraits<int>::max();
    int rightExtent = NumericTraits<int>::min();
    int topExtent = NumericTraits<int>::max();
    int bottomExtent = NumericTraits<int>::min();
    for (vector<slist<pair<bool, Point2D> > *>::iterator snakeIterator = snakes.begin();
            snakeIterator != snakes.end(); ++snakeIterator) {
        slist<pair<bool, Point2D> > *snake = *snakeIterator;
        slist<pair<bool, Point2D> >::iterator lastVertex = snake->previous(snake->end());
        for (slist<pair<bool, Point2D> >::iterator vertexIterator = snake->begin();
                vertexIterator != snake->end(); ++vertexIterator) {
            if (lastVertex->first || vertexIterator->first) {      
                leftExtent = std::min(leftExtent, lastVertex->second.x);
                rightExtent = std::max(rightExtent, lastVertex->second.x);
                topExtent = std::min(topExtent, lastVertex->second.y);
                bottomExtent = std::max(bottomExtent, lastVertex->second.y);
                leftExtent = std::min(leftExtent, vertexIterator->second.x);
                rightExtent = std::max(rightExtent, vertexIterator->second.x);
                topExtent = std::min(topExtent, vertexIterator->second.y);
                bottomExtent = std::max(bottomExtent, vertexIterator->second.y);
            }
            lastVertex = vertexIterator;
        }
    }

    // Vertex bounding box
    EnblendROI vBB;
    vBB.setCorners(Point2D(leftExtent, topExtent), Point2D(rightExtent+1, bottomExtent+1));

    // Make sure that vertex bounding box is bigger than iBB by one pixel in each direction
    EnblendROI iBBPlus;
    iBBPlus.setCorners(iBB.getUL() + Diff2D(-1,-1), iBB.getLR() + Diff2D(1,1));
    vBB.unite(iBBPlus, vBB);

    // Vertex-Union bounding box: portion of uBB inside vBB.
    EnblendROI uvBB;
    vBB.intersect(uBB, uvBB);

    // Offset between vBB and uvBB
    Diff2D uvBBOffset = uvBB.getUL() - vBB.getUL();

    // Push ul corner of vBB so that there is an even number of pixels between vBB and uvBB.
    // This is necessary for striding by two over vBB.
    if (uvBBOffset.x % 2) vBB.setUpperLeft(vBB.getUL() + Diff2D(-1,0));
    if (uvBBOffset.y % 2) vBB.setUpperLeft(vBB.getUL() + Diff2D(0,-1));
    uvBBOffset = uvBB.getUL() - vBB.getUL();
    Diff2D uvBBOffsetHalf = uvBBOffset / 2;

    // Create stitch mismatch image
    typedef UInt8 MismatchImagePixelType;
    //EnblendNumericTraits<MismatchImagePixelType>::ImageType mismatchImage((vBB.size() + Diff2D(1,1)) / 2,
    //        NumericTraits<MismatchImagePixelType>::max());
    BasicImage<MismatchImagePixelType> mismatchImage((vBB.size() + Diff2D(1,1)) / 2,
            NumericTraits<MismatchImagePixelType>::max());

    // Areas other than intersection region have maximum cost
    combineTwoImages(stride(2, 2, uvBB.apply(srcImageRange(*white))),
                     stride(2, 2, uvBB.apply(srcImage(*black))),
                     destIter(mismatchImage.upperLeft() + uvBBOffsetHalf),
                     PixelDifferenceFunctor<ImagePixelType, MismatchImagePixelType>());
    combineThreeImages(stride(2, 2, uvBB.apply(srcImageRange(*whiteAlpha))),
                     stride(2, 2, uvBB.apply(srcImage(*blackAlpha))),
                     srcIter(mismatchImage.upperLeft() + uvBBOffsetHalf),
                     destIter(mismatchImage.upperLeft() + uvBBOffsetHalf),
                     ifThenElse(Arg1() & Arg2(), Arg3(), Param(NumericTraits<MismatchImagePixelType>::max())));

    // Anneal snakes over mismatch image
    for (vector<slist<pair<bool, Point2D> > *>::iterator snakeIterator = snakes.begin();
            snakeIterator != snakes.end(); ++snakeIterator) {
        slist<pair<bool, Point2D> > *snake = *snakeIterator;

        // Move snake points to mismatchImage-relative coordinates
        for (slist<pair<bool, Point2D> >::iterator vertexIterator = snake->begin();
                vertexIterator != snake->end(); ++vertexIterator) {
            vertexIterator->second = (vertexIterator->second - vBB.getUL()) / 2;
        }

        annealSnake(&mismatchImage, snake);

        // Postprocess annealed vertices
        slist<pair<bool, Point2D> >::iterator lastVertex = snake->previous(snake->end());
        for (slist<pair<bool, Point2D> >::iterator vertexIterator = snake->begin();
                vertexIterator != snake->end(); ) {
            if (vertexIterator->first &&
                    (mismatchImage[vertexIterator->second] == NumericTraits<MismatchImagePixelType>::max())) {
                // Vertex is still in max-cost region. Delete it.
                if (vertexIterator == snake->begin()) {
                    snake->pop_front();
                    vertexIterator = snake->begin();
                }
                else {
                    vertexIterator = snake->erase_after(lastVertex);
                }
                bool needsBreak = false;
                if (vertexIterator == snake->end()) {
                    vertexIterator = snake->begin();
                    needsBreak = true;
                }
                // vertexIterator now points to next entry.

                if (!(lastVertex->first || vertexIterator->first)) {
                    // We deleted an entire range of moveable points between two nonmoveable points.
                    // insert dummy point after lastVertex so dijkstra can work over this range.
                    if (vertexIterator == snake->begin()) {
                        snake->push_front(make_pair(true, vertexIterator->second));
                        lastVertex = snake->begin();
                    } else {
                        lastVertex = snake->insert_after(lastVertex, make_pair(true, vertexIterator->second));
                    }
                }

                if (needsBreak) break;
            }
            else {
                lastVertex = vertexIterator;
                ++vertexIterator;
            }
        }
    }

    // Adjust cost image for shortest path algorithm.
    // Areas outside union region have epsilon cost
    combineThreeImages(stride(2, 2, uvBB.apply(srcImageRange(*whiteAlpha))),
                     stride(2, 2, uvBB.apply(srcImage(*blackAlpha))),
                     srcIter(mismatchImage.upperLeft() + uvBBOffsetHalf),
                     destIter(mismatchImage.upperLeft() + uvBBOffsetHalf),
                     ifThenElse(!(Arg1() || Arg2()), Param(NumericTraits<MismatchImagePixelType>::one()), Arg3()));

    EnblendROI withinVBB;
    withinVBB.setCorners(Diff2D(0,0), (vBB.size() + Diff2D(1,1)) / 2);

    // Use Dijkstra to route between moveable snake vertices over mismatchImage.
    for (vector<slist<pair<bool, Point2D> > *>::iterator snakeIterator = snakes.begin();
            snakeIterator != snakes.end(); ++snakeIterator) {
        slist<pair<bool, Point2D> > *snake = *snakeIterator;

        for (slist<pair<bool, Point2D> >::iterator currentVertex = snake->begin(); ; ) {
            slist<pair<bool, Point2D> >::iterator nextVertex = currentVertex;
            ++nextVertex;
            if (nextVertex == snake->end()) nextVertex = snake->begin();

            if (currentVertex->first || nextVertex->first) {
                // Find shortest path between these points
                Point2D currentPoint = currentVertex->second;
                Point2D nextPoint = nextVertex->second;

                int radius = 25;
                EnblendROI pointSurround;
                pointSurround.setCorners(currentPoint - Diff2D(radius,radius), currentPoint + Diff2D(radius,radius));
                EnblendROI nextPointSurround;
                nextPointSurround.setCorners(nextPoint - Diff2D(radius,radius), nextPoint + Diff2D(radius,radius));
                pointSurround.unite(nextPointSurround, pointSurround);
                pointSurround.intersect(withinVBB, pointSurround);

                vector<Point2D> *shortPath = minCostPath(pointSurround.apply(srcImageRange(mismatchImage)),
                        nextPoint - pointSurround.getUL(),
                        currentPoint - pointSurround.getUL());

                for (vector<Point2D>::iterator shortPathPoint = shortPath->begin();
                        shortPathPoint != shortPath->end();
                        ++shortPathPoint) {
                    mismatchImage[*shortPathPoint + pointSurround.getUL()] = 130;
                    snake->insert_after(currentVertex, make_pair(false, (*shortPathPoint + pointSurround.getUL())));
                }

                delete shortPath;

                mismatchImage[currentPoint] = currentVertex->first ? 200 : 230;
                mismatchImage[nextPoint] = nextVertex->first ? 200 : 230;
            }

            currentVertex = nextVertex;
            if (nextVertex == snake->begin()) break;
        }

        // Move vertices relative to root
        for (slist<pair<bool, Point2D> >::iterator vertexIterator = snake->begin();
            vertexIterator != snake->end(); ++vertexIterator) {
            vertexIterator->second = (vertexIterator->second * 2) + vBB.getUL();
        }
    }

    ImageExportInfo mismatchInfo("enblend_dijkstra.tif");
    exportImage(srcImageRange(mismatchImage), mismatchInfo);
*/

    // Fill snakes on uBB-sized mask
    miPixel pixels[2];
    pixels[0] = NumericTraits<MaskPixelType>::max();
    pixels[1] = NumericTraits<MaskPixelType>::max();
    miGC *pGC = miNewGC(2, pixels);
    miPaintedSet *paintedSet = miNewPaintedSet();

    mask = new MaskType(uBB.size());

    for (ContourVector::iterator currentContour = contours.begin();
            currentContour != contours.end();
            ++currentContour) {
        int totalPoints = 0;
        for (Contour::iterator currentSegment = (*currentContour)->begin();
                currentSegment != (*currentContour)->end();
                ++currentSegment) {
            totalPoints += (*currentSegment)->size();
        }

        miPoint *points = new miPoint[totalPoints];

        int i = 0;
        for (Contour::iterator currentSegment = (*currentContour)->begin();
                currentSegment != (*currentContour)->end();
                ++currentSegment) {
            for (Segment::iterator vertexIterator = (*currentSegment)->begin();
                    vertexIterator != (*currentSegment)->end();
                    ++vertexIterator) {
            
                Point2D vertex = vertexIterator->second - uBB.getUL();
                points[i].x = vertex.x;
                points[i].y = vertex.y;
                ++i;
            }
        }

        miFillPolygon(paintedSet, pGC, MI_SHAPE_GENERAL, MI_COORD_MODE_ORIGIN, totalPoints, points);

        delete[] points;
    }

    copyPaintedSetToImage(destImageRange(*mask), paintedSet, Diff2D(0,0));

    miDeleteGC(pGC);
    miDeletePaintedSet(paintedSet);

    // Done with snakes
    for (ContourVector::iterator currentContour = contours.begin();
            currentContour != contours.end();
            ++currentContour) {
        for (Contour::iterator currentSegment = (*currentContour)->begin();
                currentSegment != (*currentContour)->end();
                ++currentSegment) {
            delete *currentSegment;
        }
        delete *currentContour;
    }

    // Find the bounding box of the mask transition line and put it in mBB.
    MaskIteratorType firstMulticolorColumn = mask->lowerRight();
    MaskIteratorType lastMulticolorColumn = mask->upperLeft();
    MaskIteratorType firstMulticolorRow = mask->lowerRight();
    MaskIteratorType lastMulticolorRow = mask->upperLeft();

    MaskIteratorType myPrev = mask->upperLeft();
    my = mask->upperLeft() + Diff2D(0,1);
    mend = mask->lowerRight();
    for (; my.y < mend.y; ++my.y, ++myPrev.y) {
        MaskIteratorType mxLeft = my;
        MaskIteratorType mx = my + Diff2D(1,0);
        MaskIteratorType mxUpLeft = myPrev;
        MaskIteratorType mxUp = myPrev + Diff2D(1,0);

        if (*mxUpLeft != *mxLeft) {
            // Transition line is between mxUpLeft and mxLeft.
            if (firstMulticolorRow.y > mxUpLeft.y) firstMulticolorRow = mxUpLeft;
            if (lastMulticolorRow.y < mxLeft.y) lastMulticolorRow = mxLeft;
        }

        for (; mx.x < mend.x; ++mx.x, ++mxLeft.x, ++mxUp.x) {
            if (*mxLeft != *mx || *mxUp != *mx) {
                // Transition line is between mxLeft and mx and between mx and mxUp
                if (firstMulticolorColumn.x > mxLeft.x) firstMulticolorColumn = mxLeft;
                if (lastMulticolorColumn.x < mx.x) lastMulticolorColumn = mx;
                if (firstMulticolorRow.y > mxUp.y) firstMulticolorRow = mxUp;
                if (lastMulticolorRow.y < mx.y) lastMulticolorRow = mx;
            }
        }
    }

    // Check that mBB is well-defined.
    if ((firstMulticolorColumn.x >= lastMulticolorColumn.x)
            || (firstMulticolorRow.y >= lastMulticolorRow.y)) {
        // No transition pixels were found in the mask at all.
        // This means that one image has no contribution.
        if (*(mask->upperLeft()) == NumericTraits<MaskPixelType>::zero()) {
            // If the mask is entirely black, then inspectOverlap should have caught this.
            // It should have said that the white image is redundant.
            vigra_fail("Mask is entirely black, but white image was not identified as redundant.");
        }
        else {
            // If the mask is entirely white, then the black image would have been identified
            // as redundant if black and white were swapped.
            // Set mBB to the full size of the mask.
            mBB.setCorners(uBB.getUL(), uBB.getLR());
            // Explain why the black image disappears completely.
            cerr << "enblend: the previous images are completely overlapped by the current images"
                 << endl;
        }
    }
    else {
        // Move mBB lower right corner out one pixel, per VIGRA convention.
        ++lastMulticolorColumn.x;
        ++lastMulticolorRow.y;

        // mBB is defined relative to the inputUnion origin.
        mBB.setCorners(
                uBB.getUL() + Diff2D(firstMulticolorColumn.x - mask->upperLeft().x,
                                     firstMulticolorRow.y - mask->upperLeft().y),
                uBB.getUL() + Diff2D(lastMulticolorColumn.x - mask->upperLeft().x,
                                     lastMulticolorRow.y - mask->upperLeft().y));
    }

    if (Verbose > VERBOSE_ROIBB_SIZE_MESSAGES) {
        cout << "Mask transition line bounding box: ("
             << mBB.getUL().x
             << ", "
             << mBB.getUL().y
             << ") -> ("
             << mBB.getLR().x
             << ", "
             << mBB.getLR().y
             << ")" << endl;
    }

    return mask;

}

} // namespace enblend

#endif /* __MASK_H__ */
