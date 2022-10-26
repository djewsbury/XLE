// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../EntityInterface/EntityInterface.h"
#include <memory>
#include <string>

namespace RenderCore { namespace Techniques { class ParsingContext; class DrawablesPacket; class IDrawablesPool; class IPipelineAcceleratorPool; } }
namespace RenderCore { namespace BufferUploads { class IManager; }}
namespace SceneEngine { class IIntersectionScene; class ExecuteSceneContext; }

namespace EntityInterface { class RetainedEntities; }

namespace ToolsRig
{
    class ObjectPlaceholders : public std::enable_shared_from_this<ObjectPlaceholders>
    {
    public:
		void BuildDrawables(SceneEngine::ExecuteSceneContext& executeContext);

        void AddAnnotation(uint64_t typeNameHash, const std::string& geoType);

        std::shared_ptr<SceneEngine::IIntersectionScene> CreateIntersectionTester();

        ObjectPlaceholders(
			std::shared_ptr<RenderCore::Techniques::IDrawablesPool> drawablesPool,
            std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> pipelineAcceleratorPool,
            std::shared_ptr<RenderCore::BufferUploads::IManager> bufferUploads,
			std::shared_ptr<EntityInterface::RetainedEntities> objects);
        ~ObjectPlaceholders();
    protected:
        std::shared_ptr<EntityInterface::RetainedEntities> _objects;

        struct Annotation { uint64_t _typeNameHash; };
        std::vector<Annotation> _cubeAnnotations;
		std::vector<Annotation> _directionalAnnotations;
        std::vector<Annotation> _triMeshAnnotations;
		std::vector<Annotation> _areaLightAnnotation;

		std::shared_ptr<RenderCore::Techniques::IDrawablesPool> _drawablesPool;
        std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool> _pipelineAcceleratorPool;
        std::shared_ptr<RenderCore::BufferUploads::IManager> _bufferUploads;

        class IntersectionTester;
    };
}

