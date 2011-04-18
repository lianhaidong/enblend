/*
 * Copyright (C) 2011 Mikolaj Leszczynski
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
#ifndef __POSTOPTIMIZER_H__
#define __POSTOPTIMIZER_H__

#include <vector>

#include "anneal.h"
#include "masktypedefs.h"
#include "mask.h"

using vigra::LineIterator;
using vigra::Point2D;
using vigra::UInt8Image;
using vigra::Rect2D;
using vigra::Size2D;
using vigra::Diff2D;

using vigra::functor::Arg1;
using vigra::functor::Arg2;
using vigra::functor::Arg3;
using vigra::functor::ifThenElse;
using vigra::functor::Param;
using vigra::functor::UnaryFunctor;


namespace enblend
{

template <typename MismatchImageType, typename VisualizeImageType, typename AlphaType>
class PostOptimizer {
public:
    PostOptimizer(MismatchImageType* aMismatchImage, VisualizeImageType* aVisualizeImage,
                  vigra::Size2D* aMismatchImageSize, int* aMismatchImageStride,
                  Diff2D* aUVBBStrideOffset, ContourVector* someContours,
                  const Rect2D* aUBB, Rect2D* aVBB,
                  std::vector<double>* someParameters,
                  const AlphaType* aWhiteAlpha, const AlphaType* aBlackAlpha, const Rect2D* aUVBB) :
        mismatchImage(aMismatchImage), visualizeImage(aVisualizeImage),
        mismatchImageSize(aMismatchImageSize), mismatchImageStride(aMismatchImageStride),
        uvBBStrideOffset(aUVBBStrideOffset), contours(someContours),
        uBB(aUBB), vBB(aVBB),
        parameters(*someParameters),
        whiteAlpha(aWhiteAlpha), blackAlpha(aBlackAlpha), uvBB(aUVBB) {
        configureOptimizer();
    }

    virtual void runOptimizer() {}
    virtual ~PostOptimizer() {}

protected:
    virtual void configureOptimizer() {}

    MismatchImageType* mismatchImage;
    VisualizeImageType* visualizeImage;
    vigra::Size2D* mismatchImageSize;
    int* mismatchImageStride;
    Diff2D* uvBBStrideOffset;
    ContourVector* contours;
    const Rect2D* uBB;
    Rect2D* vBB;
    std::vector<double> parameters;
    const AlphaType* whiteAlpha;
    const AlphaType* blackAlpha;
    const Rect2D* uvBB;

private:
    PostOptimizer();            // NOT IMPLEMENTED
};


template <class MismatchImagePixelType, class MismatchImageType, class VisualizeImageType, class AlphaType>
class AnnealOptimizer : public PostOptimizer<MismatchImageType, VisualizeImageType, AlphaType> {
public:
    typedef PostOptimizer<MismatchImageType, VisualizeImageType, AlphaType> super;

    AnnealOptimizer(MismatchImageType* mismatchImage, VisualizeImageType* visualizeImage,
                    vigra::Size2D* mismatchImageSize, int* mismatchImageStride,
                    Diff2D* uvBBStrideOffset, ContourVector* contours,
                    const Rect2D* uBB, Rect2D* vBB,
                    std::vector<double>* parameters,
                    const AlphaType* whiteAlpha, const AlphaType* blackAlpha, const Rect2D* uvBB) :
        super(mismatchImage, visualizeImage,
              mismatchImageSize, mismatchImageStride,
              uvBBStrideOffset, contours,
              uBB, vBB, parameters, whiteAlpha, blackAlpha, uvBB) {
        configureOptimizer();
    }

    virtual void runOptimizer() {
        int segmentNumber;

        for (ContourVector::iterator currentContour = (*this->contours).begin();
             currentContour != (*this->contours).end();
             ++currentContour) {
            segmentNumber = 0;
            for (Contour::iterator currentSegment = (*currentContour)->begin();
                 currentSegment != (*currentContour)->end();
                 ++currentSegment, ++segmentNumber) {
                Segment* snake = *currentSegment;

                if (Verbose >= VERBOSE_MASK_MESSAGES) {
                    cerr << command
                         << ": info: Annealing Optimizer, s"
                         << segmentNumber << ":";
                    cerr.flush();
                }

                if (snake->empty()) {
                    cerr << endl
                         << command
                         << ": warning: seam s"
                         << segmentNumber - 1
                         << " is a tiny closed contour and was removed before optimization"
                         << endl;
                    continue;
                }

                // Move snake points to mismatchImage-relative coordinates
                for (Segment::iterator vertexIterator = snake->begin();
                     vertexIterator != snake->end();
                     ++vertexIterator) {
                    vertexIterator->second =
                        (vertexIterator->second + this->uBB->upperLeft() - this->vBB->upperLeft()) /
                        *this->mismatchImageStride;
                }

                annealSnake(this->mismatchImage, OptimizerWeights,
                            snake, this->visualizeImage);

                // Post-process annealed vertices
                Segment::iterator lastVertex = snake->previous(snake->end());
                for (Segment::iterator vertexIterator = snake->begin();
                     vertexIterator != snake->end();) {
                    if (vertexIterator->first &&
                        (*this->mismatchImage)[vertexIterator->second] == NumericTraits<MismatchImagePixelType>::max()) {
                        // Vertex is still in max-cost region. Delete it.
                        if (vertexIterator == snake->begin()) {
                            snake->pop_front();
                            vertexIterator = snake->begin();
                        } else {
                            vertexIterator = snake->erase_after(lastVertex);
                        }

                        bool needsBreak = false;
                        if (vertexIterator == snake->end()) {
                            vertexIterator = snake->begin();
                            needsBreak = true;
                        }

                        // vertexIterator now points to next entry.

                        // It is conceivable but very unlikely that every vertex in a closed contour
                        // ended up in the max-cost region after annealing.
                        if (snake->empty()) {
                            break;
                        }

                        if (!(lastVertex->first || vertexIterator->first)) {
                            // We deleted an entire range of moveable points between two nonmoveable points.
                            // insert dummy point after lastVertex so dijkstra can work over this range.
                            if (vertexIterator == snake->begin()) {
                                snake->push_front(std::make_pair(true, vertexIterator->second));
                                lastVertex = snake->begin();
                            } else {
                                lastVertex = snake->insert_after(lastVertex,
                                                                 std::make_pair(true, vertexIterator->second));
                            }
                        }

                        if (needsBreak) {
                            break;
                        }
                    }
                    else {
                        lastVertex = vertexIterator;
                        ++vertexIterator;
                    }
                }

                if (Verbose >= VERBOSE_MASK_MESSAGES) {
                    cerr << endl;
                }

                // Print an explanation if every vertex in a closed contour ended up in the
                // max-cost region after annealing.
                // FIXME: explain how to fix this problem in the error message!
                if (snake->empty()) {
                    cerr << endl
                         << command
                         << ": seam s"
                         << segmentNumber - 1
                         << " is a tiny closed contour and was removed after optimization"
                         << endl;
                }
            }
        }
    }

    virtual ~AnnealOptimizer() {}

private:
    void configureOptimizer() {
#if 0
        // Areas other than intersection region have maximum cost.
        combineThreeImagesMP(stride(this->mismatchImageStride, this->mismatchImageStride, this->uvBB->apply(srcImageRange(*this->whiteAlpha))),
          stride(this->mismatchImageStride, this->mismatchImageStride, this->uvBB->apply(srcImage(*this->blackAlpha))),
          srcIter((this->mismatchImage)->upperLeft() + *this->uvBBStrideOffset),
          destIter((this->mismatchImage)->upperLeft() + *this->uvBBStrideOffset),
          ifThenElse(Arg1() & Arg2(), Arg3(), Param(NumericTraits<MismatchImagePixelType>::max())));
#endif
    }

    AnnealOptimizer();          // NOT IMPLEMENTED
};


template <class MismatchImagePixelType, class MismatchImageType, class VisualizeImageType, class AlphaType>
class DijkstraOptimizer : public PostOptimizer<MismatchImageType, VisualizeImageType, AlphaType> {
public:
    typedef PostOptimizer<MismatchImageType, VisualizeImageType, AlphaType> super;

    DijkstraOptimizer(MismatchImageType* mismatchImage, VisualizeImageType* visualizeImage,
                      vigra::Size2D* mismatchImageSize, int* mismatchImageStride,
                      Diff2D* uvBBStrideOffset, ContourVector* contours,
                      const Rect2D* uBB, Rect2D* vBB,
                      std::vector<double>* parameters,
                      const AlphaType* whiteAlpha, const AlphaType* blackAlpha, const Rect2D* uvBB) :
        super(mismatchImage, visualizeImage,
              mismatchImageSize, mismatchImageStride,
              uvBBStrideOffset, contours,
              uBB, vBB, parameters, whiteAlpha, blackAlpha, uvBB) {}

    virtual void runOptimizer() {
        Rect2D withinMismatchImage(*this->mismatchImageSize);
        int segmentNumber;

        if (Verbose >= VERBOSE_MASK_MESSAGES) {
            cerr << command
                 << ": info: Dijkstra Optimizer:";
            cerr.flush();
        }

        // Use Dijkstra to route between moveable snake vertices over mismatchImage.
        for (ContourVector::iterator currentContour = (*this->contours).begin();
             currentContour != (*this->contours).end();
             ++currentContour) {
            segmentNumber = 0;
            for (Contour::iterator currentSegment = (*currentContour)->begin();
                 currentSegment != (*currentContour)->end();
                 ++currentSegment, ++segmentNumber) {
                Segment* snake = *currentSegment;

                if (snake->empty()) {
                    continue;
                }

                if (Verbose >= VERBOSE_MASK_MESSAGES) {
                    cerr << " s" << segmentNumber;
                    cerr.flush();
                }

                for (Segment::iterator currentVertex = snake->begin(); ; ) {
                    Segment::iterator nextVertex = currentVertex;
                    ++nextVertex;
                    if (nextVertex == snake->end()) {
                        nextVertex = snake->begin();
                    }

                    if (currentVertex->first || nextVertex->first) {
                        // Find shortest path between these points
                        Point2D currentPoint = currentVertex->second;
                        Point2D nextPoint = nextVertex->second;

                        Rect2D pointSurround(currentPoint, Size2D(1, 1));
                        pointSurround |= Rect2D(nextPoint, Size2D(1, 1));
                        pointSurround.addBorder(DijkstraRadius);
                        pointSurround &= withinMismatchImage;

                        // Make BasicImage to hold pointSurround portion of mismatchImage.
                        // min cost path needs inexpensive random access to cost image.
                        BasicImage<MismatchImagePixelType> mismatchROIImage(pointSurround.size());
                        copyImage(pointSurround.apply(srcImageRange(*this->mismatchImage)),
                                  destImage(mismatchROIImage));

                        std::vector<Point2D>* shortPath =
                            minCostPath(srcImageRange(mismatchROIImage),
                                        Point2D(nextPoint - pointSurround.upperLeft()),
                                        Point2D(currentPoint - pointSurround.upperLeft()));

                        for (std::vector<Point2D>::iterator shortPathPoint = shortPath->begin();
                             shortPathPoint != shortPath->end();
                             ++shortPathPoint) {
                            snake->insert_after(currentVertex,
                                                std::make_pair(false,
                                                               *shortPathPoint + pointSurround.upperLeft()));

                            if (this->visualizeImage) {
                                (*this->visualizeImage)[*shortPathPoint + pointSurround.upperLeft()] =
                                    VISUALIZE_SHORT_PATH_VALUE;
                            }
                        }

                        delete shortPath;

                        if (this->visualizeImage) {
                            (*this->visualizeImage)[currentPoint] =
                                currentVertex->first ?
                                VISUALIZE_FIRST_VERTEX_VALUE :
                                VISUALIZE_NEXT_VERTEX_VALUE;
                            (*this->visualizeImage)[nextPoint] =
                                nextVertex->first ?
                                VISUALIZE_FIRST_VERTEX_VALUE :
                                VISUALIZE_NEXT_VERTEX_VALUE;
                        }
                    }

                    currentVertex = nextVertex;
                    if (nextVertex == snake->begin()) {
                        break;
                    }
                }

                // Move snake vertices from mismatchImage-relative
                // coordinates to uBB-relative coordinates.
                for (Segment::iterator currentVertex = snake->begin();
                     currentVertex != snake->end();
                     ++currentVertex) {
                    currentVertex->second =
                        currentVertex->second * (*this->mismatchImageStride) +
                        this->vBB->upperLeft() - this->uBB->upperLeft();
                }
            }
        }

        if (Verbose >= VERBOSE_MASK_MESSAGES) {
            cerr << endl;
        }
    }

    virtual ~DijkstraOptimizer() {}

private:
    void configureOptimizer() {
#if 0
        combineThreeImagesMP(stride(this->mismatchImageStride, this->mismatchImageStride, this->uvBB->apply(srcImageRange(*this->whiteAlpha))),
                             stride(this->mismatchImageStride, this->mismatchImageStride, this->uvBB->apply(srcImage(*this->blackAlpha))),
                             srcIter((this->mismatchImage)->upperLeft() + *this->uvBBStrideOffset),
                             destIter((this->mismatchImage)->upperLeft() + *this->uvBBStrideOffset),
                             ifThenElse(!(Arg1() || Arg2()), Param(NumericTraits<MismatchImagePixelType>::one()), Arg3()));
#endif
    }

    DijkstraOptimizer();        // NOT IMPLEMENTED
};


template <class MismatchImagePixelType, class MismatchImageType, class VisualizeImageType, class AlphaType>
class OptimizerChain : public PostOptimizer<MismatchImageType, VisualizeImageType, AlphaType> {
public:
    typedef PostOptimizer<MismatchImageType, VisualizeImageType, AlphaType> super;
    typedef std::vector<super*> optimizer_list_t;

    OptimizerChain(MismatchImageType* mismatchImage, VisualizeImageType* visualizeImage,
                   vigra::Size2D* mismatchImageSize, int* mismatchImageStride,
                   Diff2D* uvBBStrideOffset, ContourVector* contours,
                   const Rect2D* uBB, Rect2D* vBB,
                   std::vector<double>* parameters,
                   const AlphaType* whiteAlpha, const AlphaType* blackAlpha, const Rect2D* uvBB) :
        super(mismatchImage, visualizeImage,
              mismatchImageSize, mismatchImageStride,
              uvBBStrideOffset, contours,
              uBB, vBB, parameters, whiteAlpha, blackAlpha, uvBB),
        currentOptimizer(0) {}

    void addOptimizer(const std::string& anOptimizerName) {
        typedef enum {NO_OPTIMIZER, ANNEAL_OPTIMIZER, DIJKSTRA_OPTIMIZER} optimizer_id_t;

        optimizer_id_t id = NO_OPTIMIZER;

        if (anOptimizerName == "anneal") {
            id = ANNEAL_OPTIMIZER;
        } else if (anOptimizerName == "dijkstra") {
            id = DIJKSTRA_OPTIMIZER;
        }

        switch (id) {
        case ANNEAL_OPTIMIZER:
            optimizerList.push_back(new AnnealOptimizer<MismatchImagePixelType, MismatchImageType, VisualizeImageType, AlphaType>
                                    (this->mismatchImage, this->visualizeImage,
                                     this->mismatchImageSize, this->mismatchImageStride, this->uvBBStrideOffset, this->contours,
                                     this->uBB, this->vBB,
                                     &this->parameters,
                                     this->whiteAlpha, this->blackAlpha, this->uvBB));
            break;

        case DIJKSTRA_OPTIMIZER:
            optimizerList.push_back(new DijkstraOptimizer<MismatchImagePixelType, MismatchImageType, VisualizeImageType, AlphaType>
                                    (this->mismatchImage, this->visualizeImage,
                                     this->mismatchImageSize, this->mismatchImageStride, this->uvBBStrideOffset, this->contours,
                                     this->uBB, this->vBB,
                                     &this->parameters,
                                     this->whiteAlpha, this->blackAlpha, this->uvBB));
            break;

        default:
            assert(false);
        }
    }

    void runOptimizer() {
        for (typename optimizer_list_t::iterator i = optimizerList.begin(); i != optimizerList.end(); ++i) {
            (*i)->runOptimizer();
        }
    }

    void runNextOptimizer() {
        if (currentOptimizer < optimizerList.size()) {
            optimizerList[currentOptimizer]->runOptimizer();
            ++currentOptimizer;
        }
    }

    virtual ~OptimizerChain() {
        std::for_each(optimizerList.begin(), optimizerList.end(), bind(delete_ptr(), boost::lambda::_1));
    }

private:
    OptimizerChain();           // NOT IMPLEMENTED

    optimizer_list_t optimizerList;
    size_t currentOptimizer;
};

} // namespace enblend

#endif /* __POSTOPTIMIZER_H__ */


// Local Variables:
// mode: c++
// End: