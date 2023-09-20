// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StandardLightScene.h"
#include "../Format.h"
#include "../../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore
{
	class IDevice;
	class IResource;
	class IResourceView;
	class IDescriptorSet;
	class IThreadContext;
}

namespace RenderCore { namespace Techniques
{
	class IShaderOperator;
	class PipelineCollection;
	struct FrameBufferTarget;
	class ParsingContext;
	class DeferredShaderResource;
}}

namespace RenderCore { namespace BufferUploads { using CommandListID = uint32_t; class IManager; }}
namespace std { template<typename T> class promise; }

namespace RenderCore { namespace LightingEngine
{
	class SequenceIterator;
	class SHCoefficients;

	enum class SkyTextureType { HemiCube, Cube, Equirectangular, HemiEquirectangular };
	struct SkyOperatorDesc
	{
		SkyTextureType _textureType = SkyTextureType::Equirectangular;

		uint64_t GetHash(uint64_t seed=DefaultSeed64) const;
	};

	struct SkyTextureProcessorDesc
	{
		unsigned _cubemapFaceDimension = 1024;
		Format _cubemapFormat = Format::BC6H_UF16;

		unsigned _specularCubemapFaceDimension = 512;
		Format _specularCubemapFormat = Format::BC6H_UF16;

		bool _progressiveCompilation = false;
		bool _useProgressiveSpecularAsBackground = false;
		bool _blurBackground = false;
	};

	/// <summary>Utility for transforming from asset name to sky texture resources, and assign to necessary operators</summary>
	class ISkyTextureProcessor
	{
	public:
		virtual void SetEquirectangularSource(std::shared_ptr<::Assets::OperationContext> loadingContext, StringSection<> src) = 0;

		virtual void SetSkyResource(std::shared_ptr<IResourceView>, BufferUploads::CommandListID) = 0;
		virtual void SetIBL(std::shared_ptr<IResourceView> specular, BufferUploads::CommandListID specularCompletion, SHCoefficients& diffuse) = 0;

		virtual ~ISkyTextureProcessor();
	};

	class SkyOperator : public std::enable_shared_from_this<SkyOperator>
	{
	public:
		void Execute(Techniques::ParsingContext& parsingContext);
		void Execute(SequenceIterator&);

		void SecondStageConstruction(
			std::promise<std::shared_ptr<SkyOperator>>&& promise,
			const Techniques::FrameBufferTarget& fbTarget);

		::Assets::DependencyValidation GetDependencyValidation() const;
		BufferUploads::CommandListID GetCompletionCommandList() const { return _completionCommandList; }

		void SetResource(std::shared_ptr<IResourceView>, BufferUploads::CommandListID);

		SkyOperator(
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			const SkyOperatorDesc& desc);
		~SkyOperator();
	private:
		std::shared_ptr<Techniques::IShaderOperator> _shader;
		std::shared_ptr<IDescriptorSet> _descSet;
		std::shared_ptr<Techniques::PipelineCollection> _pool;
		std::shared_ptr<IDevice> _device;
		unsigned _secondStageConstructionState = 0;		// debug usage only
		SkyOperatorDesc _desc;
		BufferUploads::CommandListID _completionCommandList = 0;
	};

	using OnSkyTextureUpdateFn = std::function<void(std::shared_ptr<IResourceView>, BufferUploads::CommandListID)>;
	using OnIBLUpdateFn = std::function<void(std::shared_ptr<IResourceView>, BufferUploads::CommandListID, SHCoefficients&)>;

	std::shared_ptr<ISkyTextureProcessor> CreateSkyTextureProcessor(
		const SkyTextureProcessorDesc& desc,
		std::shared_ptr<SkyOperator> skyOperator,
		OnSkyTextureUpdateFn&& onSkyTextureUpdate,
		OnIBLUpdateFn&& onIBLUpdate);

	void SkyTextureProcessorPrerender(ISkyTextureProcessor&);

	class FillBackgroundOperator : public std::enable_shared_from_this<FillBackgroundOperator>
	{
	public:
		void Execute(Techniques::ParsingContext& parsingContext);
		::Assets::DependencyValidation GetDependencyValidation() const;

		void SecondStageConstruction(
			std::promise<std::shared_ptr<FillBackgroundOperator>>&& promise,
			const Techniques::FrameBufferTarget& fbTarget);

		FillBackgroundOperator(std::shared_ptr<Techniques::PipelineCollection> pipelinePool);
		~FillBackgroundOperator();
	private:
		std::shared_ptr<Techniques::IShaderOperator> _shader;
		std::shared_ptr<Techniques::PipelineCollection> _pool;
		unsigned _secondStageConstructionState = 0;		// debug usage only
	};

}}

