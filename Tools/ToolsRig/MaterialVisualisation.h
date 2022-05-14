// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetsCore.h"
#include <memory>
#include <vector>
#include <string>

namespace std { template<typename R> class future; }

namespace SceneEngine { class IScene; }
namespace RenderCore { namespace Techniques { class ITechniqueDelegate; class IPipelineAcceleratorPool; class CompiledShaderPatchCollection; }}
namespace RenderCore { namespace Assets { class RawMaterial; }}
namespace GraphLanguage { class INodeGraphProvider; class NodeGraph; class NodeGraphSignature; }
namespace ShaderSourceParser { class PreviewOptions; }
namespace OSServices { class OnChangeCallback; }

namespace ToolsRig
{
    class MaterialVisSettings
    {
    public:
        enum class GeometryType { Sphere, Cube, Plane2D };
        GeometryType _geometryType = GeometryType::Sphere;
    };

	std::shared_ptr<SceneEngine::IScene> MakeScene(
		const std::shared_ptr<RenderCore::Techniques::IPipelineAcceleratorPool>& pipelineAcceleratorPool,
		const MaterialVisSettings& visObject, 
		const std::shared_ptr<RenderCore::Assets::RawMaterial>& material = nullptr);

	class OnChangeCallback
    {
    public:
        virtual void    OnChange() = 0;
        virtual ~OnChangeCallback() = default;
    };

	class MessageRelay
	{
	public:
		std::string GetMessages() const;

		unsigned AddCallback(const std::shared_ptr<OnChangeCallback>& callback);
		void RemoveCallback(unsigned);

		void AddMessage(const std::string& msg);

		MessageRelay();
		~MessageRelay();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

	using PatchCollectionFuture = ::Assets::PtrToMarkerPtr<RenderCore::Techniques::CompiledShaderPatchCollection>;

#if 0
	std::unique_ptr<RenderCore::Techniques::ITechniqueDelegate> MakeShaderPatchAnalysisDelegate(
		const ShaderSourceParser::PreviewOptions& previewOptions);

	std::unique_ptr<RenderCore::Techniques::CompiledShaderPatchCollection> MakeCompiledShaderPatchCollection(
		const std::shared_ptr<GraphLanguage::INodeGraphProvider>& provider,
		const std::shared_ptr<MessageRelay>& logMessages);

	std::unique_ptr<RenderCore::Techniques::CompiledShaderPatchCollection> MakeCompiledShaderPatchCollection(
		const GraphLanguage::NodeGraph& nodeGraph,
		const GraphLanguage::NodeGraphSignature& nodeGraphSignature,
		uint32_t previewNodeId,
		const std::shared_ptr<GraphLanguage::INodeGraphProvider>& subProvider);
#endif

	PatchCollectionFuture MakeCompiledShaderPatchCollectionAsync(
		GraphLanguage::NodeGraph&& nodeGraph,
		GraphLanguage::NodeGraphSignature&& nodeGraphSignature,
		uint32_t previewNodeId,
		const std::shared_ptr<GraphLanguage::INodeGraphProvider>& subProvider);

	class DeferredCompiledShaderPatchCollection
	{
	public:
		const PatchCollectionFuture& GetFuture();

		DeferredCompiledShaderPatchCollection(
			GraphLanguage::NodeGraph&& nodeGraph,
			GraphLanguage::NodeGraphSignature&& nodeGraphSignature,
			uint32_t previewNodeId,
			const std::shared_ptr<GraphLanguage::INodeGraphProvider>& subProvider);
		~DeferredCompiledShaderPatchCollection();
	private:
		std::unique_ptr<PatchCollectionFuture> _future;
	};

	class IPatchCollectionVisualizationScene
	{
	public:
		virtual void SetPatchCollection(const PatchCollectionFuture& patchCollection) = 0;
	};
}
