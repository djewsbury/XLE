// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Techniques/TechniqueDelegates.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/InitializerPack.h"
#include "../../Assets/Marker.h"
#include "../../Assets/AssetHeap.h"		// (for ::Assets::IsInvalidated)
#include <memory>
#include <map>
#if !defined(__CLR_VER)
	#include <future>
#endif

namespace RenderCore { namespace Techniques 
{
	class TechniqueSetFile;
	class ITechniqueDelegate;
	class DrawingApparatus;
	class IPipelineAcceleratorPool;
	class PipelineCollection;
	class SystemUniformsDelegate;
}}
namespace RenderCore { class IDevice; }
namespace Assets { class InitializerPack; }

namespace RenderCore { namespace LightingEngine
{
	class SharedTechniqueDelegateBox;

	class LightingEngineApparatus
	{
	public:
		std::shared_ptr<SharedTechniqueDelegateBox> _sharedDelegates;
		std::shared_ptr<IDevice> _device;
		std::shared_ptr<Techniques::IPipelineAcceleratorPool> _pipelineAccelerators;
		std::shared_ptr<Techniques::PipelineCollection> _lightingOperatorCollection;
		std::shared_ptr<Techniques::SystemUniformsDelegate> _systemUniformsDelegate;

		LightingEngineApparatus(std::shared_ptr<Techniques::DrawingApparatus>);
		~LightingEngineApparatus();
		LightingEngineApparatus(LightingEngineApparatus&) = delete;
		LightingEngineApparatus& operator=(LightingEngineApparatus&) = delete;
	};

#if !defined(__CLR_VER)
	class SharedTechniqueDelegateBox
	{
	public:
		std::shared_ptr<RenderCore::Assets::PredefinedPipelineLayoutFile> _lightingOperatorsPipelineLayoutFile;
		std::shared_ptr<RenderCore::Assets::PredefinedDescriptorSetLayout> _dmShadowDescSetTemplate;
		std::shared_ptr<ICompiledPipelineLayout> _lightingOperatorLayout;

		using TechniqueDelegateFuture = std::shared_future<std::shared_ptr<Techniques::ITechniqueDelegate>>;
		TechniqueDelegateFuture GetForwardIllumDelegate_DisableDepthWrite();
		TechniqueDelegateFuture GetDepthOnlyDelegate();
		TechniqueDelegateFuture GetDepthMotionDelegate();
		TechniqueDelegateFuture GetDepthMotionNormalDelegate();
		TechniqueDelegateFuture GetDepthMotionNormalRoughnessDelegate();
		TechniqueDelegateFuture GetDeferredIllumDelegate();

		std::shared_future<std::shared_ptr<Techniques::TechniqueSetFile>> GetTechniqueSetFile();

		template<typename... Args>
			TechniqueDelegateFuture GetShadowGenTechniqueDelegate(Args&&... args);

		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		SharedTechniqueDelegateBox(
			IDevice& device, ShaderLanguage shaderLanguage, SamplerPool* samplerPool);
	private:
		std::map<uint64_t, ::Assets::PtrToMarkerPtr<Techniques::ITechniqueDelegate>> _shadowGenTechniqueDelegates;
		::Assets::DependencyValidation _depVal;

		::Assets::MarkerPtr<Techniques::TechniqueSetFile> _techniqueSetFile;
		::Assets::MarkerPtr<Techniques::ITechniqueDelegate> _forwardIllumDelegate_DisableDepthWrite;
		::Assets::MarkerPtr<Techniques::ITechniqueDelegate> _depthOnlyDelegate;
		::Assets::MarkerPtr<Techniques::ITechniqueDelegate> _depthMotionDelegate;
		::Assets::MarkerPtr<Techniques::ITechniqueDelegate> _depthMotionNormalDelegate;
		::Assets::MarkerPtr<Techniques::ITechniqueDelegate> _depthMotionNormalRoughnessDelegate;
		::Assets::MarkerPtr<Techniques::ITechniqueDelegate> _deferredIllumDelegate;
	};

	template<typename... Args>
		auto SharedTechniqueDelegateBox::GetShadowGenTechniqueDelegate(Args&&... args) -> TechniqueDelegateFuture
	{
		::Assets::InitializerPack pack{args...};
		auto hash = pack.ArchivableHash();
		auto i = _shadowGenTechniqueDelegates.find(hash);
		if (i != _shadowGenTechniqueDelegates.end()) {
			if (::Assets::IsInvalidated(*i->second)) {
				i->second = std::make_shared<::Assets::MarkerPtr<Techniques::ITechniqueDelegate>>();
				Techniques::CreateTechniqueDelegate_ShadowGen(i->second->AdoptPromise(), GetTechniqueSetFile(), std::forward<Args>(args)...);
			}
			return i->second->ShareFuture();
		}

		auto delegate = std::make_shared<::Assets::MarkerPtr<Techniques::ITechniqueDelegate>>();
		Techniques::CreateTechniqueDelegate_ShadowGen(delegate->AdoptPromise(), GetTechniqueSetFile(), std::forward<Args>(args)...);
		_shadowGenTechniqueDelegates.insert(std::make_pair(hash, delegate));
		return delegate->ShareFuture();
	}
#endif

}}

namespace RenderCore
{
	namespace Techniques 
	{ 
		uint64_t Hash64(RSDepthBias, uint64_t=DefaultSeed64); 
	}
}
