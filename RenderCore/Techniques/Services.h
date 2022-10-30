// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/TextureLoaders.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <cassert>

namespace RenderCore { class IDevice; }
namespace RenderCore { namespace BufferUploads { class IManager; }}

namespace RenderCore { namespace Techniques
{
	class CommonResourceBox;
	class SubFrameEvents;
	class IDeformConfigure;

	class Services
	{
	public:
		static BufferUploads::IManager& GetBufferUploads() { return *GetInstance()._bufferUploads; }
		static std::shared_ptr<BufferUploads::IManager> GetBufferUploadsPtr() { return GetInstance()._bufferUploads; }
		static RenderCore::IDevice& GetDevice() { return *GetInstance()._device; }
		static std::shared_ptr<RenderCore::IDevice> GetDevicePtr() { return GetInstance()._device; }
		static std::shared_ptr<CommonResourceBox> GetCommonResources() { return GetInstance()._commonResources; }
		static SubFrameEvents& GetSubFrameEvents() { return *GetInstance()._subFrameEvents; }
		static std::shared_ptr<SubFrameEvents> GetSubFrameEventsPtr() { return GetInstance()._subFrameEvents; }

		/////////////////////////////
		//   T E X T U R E   L O A D E R S
		////////////////////////////////////////////
		unsigned 	RegisterTextureLoader(StringSection<> wildcardPattern, std::function<Assets::TextureLoaderSignature>&& loader);
		void 		DeregisterTextureLoader(unsigned pluginId);
		void 		SetFallbackTextureLoader(std::function<Assets::TextureLoaderSignature>&& loader);
		std::shared_ptr<BufferUploads::IAsyncDataSource> CreateTextureDataSource(StringSection<> identifier, Assets::TextureLoaderFlags::BitField flags);

		/////////////////////////////
		//   D E F O R M   C O N F I G U R E
		////////////////////////////////////////////
		IDeformConfigure* 	FindDeformConfigure(StringSection<>);
		unsigned 			RegisterDeformConfigure(StringSection<>, std::shared_ptr<IDeformConfigure>);
		void 				DeregisterDeformConfigure(unsigned);

		void 		SetBufferUploads(const std::shared_ptr<BufferUploads::IManager>&);
		void 		SetCommonResources(const std::shared_ptr<CommonResourceBox>&);
		
		Services(const std::shared_ptr<RenderCore::IDevice>& device);
		~Services();

		static bool HasInstance();
		static Services& GetInstance();

	protected:
		std::shared_ptr<RenderCore::IDevice> _device;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;
		std::shared_ptr<CommonResourceBox> _commonResources;
		std::shared_ptr<SubFrameEvents> _subFrameEvents;

		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

}}
