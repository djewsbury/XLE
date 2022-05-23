// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "NascentRawGeometry.h"
#include "NascentObjectGuid.h"
#include "../Assets/ModelMachine.h"
#include "../Types.h"
#include "../../Math/Matrix.h"
#include "../../Math/Vector.h"
#include "../../Utility/UTFUtils.h"
#include "../../Utility/PtrUtils.h"
#include <vector>

namespace RenderCore { namespace Assets { namespace GeoProc
{
	std::pair<Float3, Float3>   InvalidBoundingBox();

        ////////////////////////////////////////////////////////

    class NascentBoundSkinnedGeometry
    {
    public:
        NascentRawGeometry          _unanimatedBase;

        GeoInputAssembly            _mainDrawAnimatedIA;

        std::vector<uint8_t>		_animatedVertexElements;
        std::vector<uint8_t>		_skeletonBinding;
        unsigned                    _skeletonBindingVertexStride = 0;
        unsigned                    _animatedVertexBufferSize = 0;

		struct Section
		{
			std::vector<Float4x4>		_bindShapeByInverseBindMatrices;
			std::vector<DrawCallDesc>   _preskinningDrawCalls;
            std::vector<unsigned>       _drawCallWeightsPerVertex;
			std::vector<uint16_t>		_jointMatrices;
			Float4x4					_bindShapeMatrix;
            Float4x4					_postSkinningBindMatrix;
		};
		std::vector<Section>		_preskinningSections;
        GeoInputAssembly            _preskinningIA;

        std::pair<Float3, Float3>	_localBoundingBox = InvalidBoundingBox();

        void    SerializeWithResourceBlock(
            ::Assets::BlockSerializer& outputSerializer,
            LargeResourceBlockConstructor& largeResourcesBlock) const;

        void    SerializeTopologicalWithResourceBlock(
            ::Assets::BlockSerializer& outputSerializer,
            LargeResourceBlockConstructor& largeResourcesBlock) const;
        friend std::ostream& SerializationOperator(std::ostream&, const NascentBoundSkinnedGeometry&);
    };

        ////////////////////////////////////////////////////////

    class UnboundSkinController 
    {
    public:
		void AddInfluences(
			unsigned targetVertex,
			IteratorRange<const float*> weights,
			IteratorRange<const unsigned*> jointIndices);
		void ReserveInfluences(unsigned vertexCount, unsigned influencesPerVertex);

		IteratorRange<const std::string*>	GetJointNames() const { return MakeIteratorRange(_jointNames); }
		IteratorRange<const Float4x4*>		GetInverseBindMatrices() const { return MakeIteratorRange(_inverseBindMatrices); }
		const Float4x4&						GetBindShapeMatrix() const { return _bindShapeMatrix; }
        const Float4x4&						GetPostSkinningBindMatrix() const { return _postSkinningBindMatrix; }

        UnboundSkinController(
            std::vector<Float4x4>&& inverseBindMatrices, const Float4x4& bindShapeMatrix, const Float4x4& postSkinningBindMatrix,
            std::vector<std::string>&& jointNames);

	private:
		Float4x4					_bindShapeMatrix;
        Float4x4                    _postSkinningBindMatrix;

        std::vector<std::string>	_jointNames;
        std::vector<Float4x4>		_inverseBindMatrices;

		struct AttachmentGroup
		{
			std::vector<float>			_weights;
			std::vector<unsigned>		_jointIndices;
		};
		std::vector<AttachmentGroup>	_attachmentGroups;
		std::vector<unsigned>			_influenceCount;

		friend class BuckettedSkinController;
    };

        ////////////////////////////////////////////////////////

    class UnboundMorphController
    {
    public:
        NascentObjectGuid   _source;

        UnboundMorphController();
        UnboundMorphController(UnboundMorphController&& moveFrom) never_throws;
        UnboundMorphController& operator=(UnboundMorphController&& moveFrom) never_throws;
    private:
        UnboundMorphController& operator=(const UnboundMorphController& copyFrom);
    };

        ////////////////////////////////////////////////////////

	struct UnboundSkinControllerAndJointMatrices
	{
		const UnboundSkinController* _controller;
		std::vector<uint16_t> _jointMatrices;
	};

    NascentBoundSkinnedGeometry BindController(
        NascentRawGeometry&& sourceGeo,
        IteratorRange<const UnboundSkinControllerAndJointMatrices*> controllers,
        const char nodeName[]);
}}}

