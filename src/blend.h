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
#ifndef __BLEND_H__
#define __BLEND_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fixmath.h"

#include "vigra/combineimages.hxx"
#include "vigra/numerictraits.hxx"

using std::cout;
using std::vector;

using vigra::combineThreeImages;
using vigra::NumericTraits;

namespace enblend {

/** Functor for blending a black and white pyramid level using a mask
 *  pyramid level.
 */
template <typename MaskPixelType>
class CartesianBlendFunctor {
public:
    CartesianBlendFunctor(MaskPixelType w) : white(NumericTraits<MaskPixelType>::toRealPromote(w)) {}

    template <typename ImagePixelType>
    ImagePixelType operator()(const MaskPixelType &maskP, const ImagePixelType &wP, const ImagePixelType &bP) const {

        typedef typename NumericTraits<ImagePixelType>::RealPromote RealImagePixelType;

        // Convert mask pixel to blend coefficient in range [0.0, 1.0].
        double whiteCoeff =
                NumericTraits<MaskPixelType>::toRealPromote(maskP) / white;
        double blackCoeff = 1.0 - whiteCoeff;

        RealImagePixelType rwP = NumericTraits<ImagePixelType>::toRealPromote(wP);
        RealImagePixelType rbP = NumericTraits<ImagePixelType>::toRealPromote(bP);

        RealImagePixelType blendP = (whiteCoeff * rwP) + (blackCoeff * rbP);

        return NumericTraits<ImagePixelType>::fromRealPromote(blendP);
    }

protected:
    double white;
};

/** Functor for blending a black and white pyramid level using a mask
 *  pyramid level.
 */
template <typename MaskPixelType>
class CylindricalBlendFunctor {
public:
    CylindricalBlendFunctor(MaskPixelType w) : white(NumericTraits<MaskPixelType>::toRealPromote(w)) {}

    template <typename ImagePixelType>
    ImagePixelType blend(const MaskPixelType &maskP, const ImagePixelType &wP, const ImagePixelType &bP, VigraTrueType) const {

        typedef typename NumericTraits<ImagePixelType>::RealPromote RealImagePixelType;

        // Convert mask pixel to blend coefficient in range [0.0, 1.0].
        double whiteCoeff =
                NumericTraits<MaskPixelType>::toRealPromote(maskP) / white;
        double blackCoeff = 1.0 - whiteCoeff;

        RealImagePixelType rwP = NumericTraits<ImagePixelType>::toRealPromote(wP);
        RealImagePixelType rbP = NumericTraits<ImagePixelType>::toRealPromote(bP);

        RealImagePixelType blendP = (whiteCoeff * rwP) + (blackCoeff * rbP);

        return NumericTraits<ImagePixelType>::fromRealPromote(blendP);
    }

    template <typename ImagePixelType>
    ImagePixelType blend(const MaskPixelType &maskP, const ImagePixelType &wP, const ImagePixelType &bP, VigraFalseType) const {
        typedef typename ImagePixelType::value_type ImagePixelComponentType;
        typedef typename NumericTraits<ImagePixelComponentType>::RealPromote RealImagePixelComponentType;

        double whiteCoeff =
                NumericTraits<MaskPixelType>::toRealPromote(maskP) / white;
        double blackCoeff = 1.0 - whiteCoeff;

        RealImagePixelComponentType rwPj = NumericTraits<ImagePixelComponentType>::toRealPromote(wP.red());
        RealImagePixelComponentType rwPc = NumericTraits<ImagePixelComponentType>::toRealPromote(wP.green());
        RealImagePixelComponentType rwPh = NumericTraits<ImagePixelComponentType>::toRealPromote(wP.blue());
        RealImagePixelComponentType rbPj = NumericTraits<ImagePixelComponentType>::toRealPromote(bP.red());
        RealImagePixelComponentType rbPc = NumericTraits<ImagePixelComponentType>::toRealPromote(bP.green());
        RealImagePixelComponentType rbPh = NumericTraits<ImagePixelComponentType>::toRealPromote(bP.blue());

        RealImagePixelComponentType blendPj = (rwPj * whiteCoeff) + (rbPj * blackCoeff);
        RealImagePixelComponentType blendPc = (rwPc * whiteCoeff) + (rbPc * blackCoeff);
        RealImagePixelComponentType blendPh = ((rwPh * whiteCoeff) + (rbPh * blackCoeff)) / 2.0;
        if (abs((rwPh * whiteCoeff) - (rbPh * blackCoeff)) > 180.0) blendPh = (blendPh + 180.0) % 360.0;

        return NumericTraits<ImagePixelType>(NumericTraits<ImagePixelComponentType>::fromRealPromote(blendPj),
                                             NumericTraits<ImagePixelComponentType>::fromRealPromote(blendPc),
                                             NumericTraits<ImagePixelComponentType>::fromRealPromote(blendPh));
    }

    template <typename ImagePixelType>
    ImagePixelType operator()(const MaskPixelType &maskP, const ImagePixelType &wP, const ImagePixelType &bP) const {
        typedef typename NumericTraits<typename ImagePixelType::value_type>::isScalar imageIsScalar;
        return blend(maskP, wP, bP, imageIsScalar());
    }

protected:
    double white;
};

/** Blend black and white pyramids using mask pyramid.
 */
template <typename MaskPyramidType, typename ImagePyramidType>
void blend(vector<MaskPyramidType*> *maskGP,
        vector<ImagePyramidType*> *whiteLP,
        vector<ImagePyramidType*> *blackLP,
        typename MaskPyramidType::value_type maskPyramidWhiteValue) {

    if (Verbose > VERBOSE_BLEND_MESSAGES) {
        cout << "Blending layers:";
        cout.flush();
    }

    for (unsigned int layer = 0; layer < maskGP->size(); layer++) {

        if (Verbose > VERBOSE_BLEND_MESSAGES) {
            cout << " l" << layer;
            cout.flush();
        }

        // Blend the pixels in this layer using the blend functor.
        if (UseCIECAM) {
            combineThreeImages(srcImageRange(*((*maskGP)[layer])),
                    srcImage(*((*whiteLP)[layer])),
                    srcImage(*((*blackLP)[layer])),
                    destImage(*((*blackLP)[layer])),
                    CartesianBlendFunctor<typename MaskPyramidType::value_type>(maskPyramidWhiteValue));
                    //CylindricalBlendFunctor<typename MaskPyramidType::value_type>(maskPyramidWhiteValue));
        } else {
            combineThreeImages(srcImageRange(*((*maskGP)[layer])),
                    srcImage(*((*whiteLP)[layer])),
                    srcImage(*((*blackLP)[layer])),
                    destImage(*((*blackLP)[layer])),
                    CartesianBlendFunctor<typename MaskPyramidType::value_type>(maskPyramidWhiteValue));
        }
    }

    if (Verbose > VERBOSE_BLEND_MESSAGES) {
        cout << endl;
    }

};

} // namespace enblend

#endif /* __BLEND_H__ */
