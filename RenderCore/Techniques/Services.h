// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Assets/TextureLoaders.h"
#include "../../Utility/IteratorUtils.h"
#include <memory>
#include <cassert>

namespace RenderCore { class IDevice; }
namespace std { 
	template<typename CharT> class regex_traits;
	template<typename CharT, typename Traits> class basic_regex; 
}
namespace BufferUploads { class IManager; }

namespace RenderCore { namespace Techniques
{
	class DeformOperationFactory;
	class CommonResourceBox;

	class Services
	{
	public:
		static BufferUploads::IManager& GetBufferUploads() { return *GetInstance()._bufferUploads; }
		static DeformOperationFactory& GetDeformOperationFactory() { assert(GetInstance()._deformOpsFactory); return *GetInstance()._deformOpsFactory; }
		static RenderCore::IDevice& GetDevice() { return *GetInstance()._device; }
		static std::shared_ptr<RenderCore::IDevice> GetDevicePtr() { return GetInstance()._device; }
		static std::shared_ptr<CommonResourceBox> GetCommonResources() { return GetInstance()._commonResources; }

		/////////////////////////////
		//   T E X T U R E   L O A D E R S
		////////////////////////////////////////////
		unsigned 	RegisterTextureLoader(const std::basic_regex<char, std::regex_traits<char>>& initializerMatcher, std::function<Assets::TextureLoaderSignature>&& loader);
		void 		DeregisterTextureLoader(unsigned pluginId);
		std::shared_ptr<BufferUploads::IAsyncDataSource> CreateTextureDataSource(StringSection<> identifier, Assets::TextureLoaderFlags::BitField flags);

		void 		SetBufferUploads(const std::shared_ptr<BufferUploads::IManager>&);
		void 		SetCommonResources(const std::shared_ptr<CommonResourceBox>&);
		
		Services(const std::shared_ptr<RenderCore::IDevice>& device);
		~Services();

		static bool HasInstance();
		static Services& GetInstance();

	protected:
		std::shared_ptr<RenderCore::IDevice> _device;
		std::shared_ptr<DeformOperationFactory> _deformOpsFactory;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;
		std::shared_ptr<CommonResourceBox> _commonResources;

		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

}}
