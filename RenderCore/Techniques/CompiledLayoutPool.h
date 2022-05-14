// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "TechniqueUtils.h"		// (for GetDefaultShaderLanguage())
#include "../Types.h"
#include "../../Assets/Marker.h"
#include <memory>

namespace RenderCore { namespace Assets { class ShaderPatchCollection; class PredefinedPipelineLayout; class PredefinedDescriptorSetLayout; class PredefinedPipelineLayoutFile; }}
namespace RenderCore { class IDevice; class ICompiledPipelineLayout; }

namespace RenderCore { namespace Techniques
{
	class CompiledShaderPatchCollection;
	class CompiledPipelineLayoutAsset;

	class ICompiledLayoutPool
	{
	public:
		virtual ::Assets::PtrToMarkerPtr<CompiledShaderPatchCollection> GetPatchCollectionFuture(const Assets::ShaderPatchCollection&) = 0;
		virtual ::Assets::PtrToMarkerPtr<CompiledShaderPatchCollection> GetDefaultPatchCollectionFuture() = 0;
		virtual ::Assets::PtrToMarkerPtr<CompiledPipelineLayoutAsset> GetPatchedPipelineLayout(StringSection<> techniquePipelineLayoutSrc) = 0;
		virtual const RenderCore::Assets::PredefinedDescriptorSetLayout& GetBaseMaterialDescriptorSetLayout() const = 0;
		virtual ~ICompiledLayoutPool();
	};

	class DescriptorSetLayoutAndBinding;
	std::shared_ptr<ICompiledLayoutPool> CreateCompiledLayoutPool(
		const std::shared_ptr<IDevice>& device,
		const std::shared_ptr<DescriptorSetLayoutAndBinding>& matDescSetLayout);

	class DescriptorSetLayoutAndBinding;

	class CompiledPipelineLayoutAsset
	{
	public:
		const std::shared_ptr<ICompiledPipelineLayout>& GetPipelineLayout() const { return _pipelineLayout; }
		const std::shared_ptr<Assets::PredefinedPipelineLayout>& GetPredefinedPipelineLayout() const { return _predefinedLayout; }
		const ::Assets::DependencyValidation GetDependencyValidation() const;

		CompiledPipelineLayoutAsset(
			std::shared_ptr<IDevice> device,
			std::shared_ptr<Assets::PredefinedPipelineLayout> predefinedLayout,
			std::shared_ptr<DescriptorSetLayoutAndBinding> patchInDescSet = nullptr,
			ShaderLanguage shaderLanguage = Techniques::GetDefaultShaderLanguage());
		CompiledPipelineLayoutAsset() = default;

		static void ConstructToPromise(
			std::promise<std::shared_ptr<CompiledPipelineLayoutAsset>>&& promise,
			const std::shared_ptr<IDevice>& device,
			StringSection<> srcFile,
			const std::shared_ptr<DescriptorSetLayoutAndBinding>& patchInDescSet = nullptr,
			ShaderLanguage shaderLanguage = GetDefaultShaderLanguage());

	protected:
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		std::shared_ptr<Assets::PredefinedPipelineLayout> _predefinedLayout;
	};

	class DescriptorSetLayoutAndBinding
	{
	public:
		const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& GetLayout() const { return _layout; }
		unsigned GetSlotIndex() const { return _slotIdx; }
		PipelineType GetPipelineType() const { return _pipelineType; }
		const std::string& GetName() const { return _name; }

		uint64_t GetHash() const { return _hash; }
		::Assets::DependencyValidation GetDependencyValidation() const { return _depVal; }

		DescriptorSetLayoutAndBinding(
			const std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout>& layout,
			unsigned slotIdx,
			const std::string& name,
			PipelineType pipelineType,
			::Assets::DependencyValidation depVal);
		DescriptorSetLayoutAndBinding();
		~DescriptorSetLayoutAndBinding();

	private:
		std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _layout;
		unsigned _slotIdx;
		uint64_t _hash;
		std::string _name;
		PipelineType _pipelineType = PipelineType::Graphics;
		::Assets::DependencyValidation _depVal;
	};

	std::shared_ptr<DescriptorSetLayoutAndBinding> FindLayout(const RenderCore::Assets::PredefinedPipelineLayoutFile&, const std::string& pipelineLayoutName, const std::string& descSetName, PipelineType pipelineType);
	std::shared_ptr<DescriptorSetLayoutAndBinding> FindLayout(const RenderCore::Assets::PredefinedPipelineLayout& file, const std::string& descriptorSetName, PipelineType pipelineType);

}}
