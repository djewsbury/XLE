// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "StandardLightScene.h"
#include "../../Assets/AssetsCore.h"
#include <memory>

namespace RenderCore
{
	class IDevice;
	class IResource;
	class IResourceView;
	class IDescriptorSet;
}

namespace RenderCore { namespace Techniques
{
	class IShaderOperator;
	class PipelinePool;
	class FrameBufferTarget;
}}

namespace RenderCore { namespace LightingEngine
{
	class LightingTechniqueIterator;

	struct SkyOperatorDesc
	{
		SkyTextureType _textureType = SkyTextureType::Equirectangular;
		uint64_t GetHash() const;
	};

	class SkyOperator
	{
	public:
		void Execute(LightingEngine::LightingTechniqueIterator& iterator);
		void SetResource(std::shared_ptr<IResourceView>);

		::Assets::DependencyValidation GetDependencyValidation() const;

		SkyOperator(
			const SkyOperatorDesc& desc,
			std::shared_ptr<Techniques::IShaderOperator> shader,
			std::shared_ptr<IDevice> device);
		~SkyOperator();

		static void ConstructToFuture(
			::Assets::FuturePtr<SkyOperator>& future,
			const SkyOperatorDesc& desc,
			std::shared_ptr<Techniques::PipelinePool> pipelinePool,
			const Techniques::FrameBufferTarget& fbTarget);
	private:
		std::shared_ptr<Techniques::IShaderOperator> _shader;
		std::shared_ptr<IDescriptorSet> _descSet;
		std::shared_ptr<IDevice> _device;
	};

}}

