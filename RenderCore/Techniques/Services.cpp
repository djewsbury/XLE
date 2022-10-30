// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Services.h"
#include "CommonResources.h"
#include "SubFrameEvents.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../../ConsoleRig/AttachablePtr.h"
#include "../../Utility/Threading/Mutex.h"
#include "wildcards.hpp"
#include <vector>

namespace RenderCore { namespace Techniques
{
	class Services::Pimpl
	{
	public:
		struct TexturePlugin
		{
			std::string _initializerMatcher;
			std::function<Assets::TextureLoaderSignature> _loader;
			unsigned _id;
		};
		std::vector<TexturePlugin> _texturePlugins;
		std::function<Assets::TextureLoaderSignature> _fallbackTextureLoader;
		unsigned _nextTexturePluginId = 1;

		struct DeformConfigure
		{
			std::string _name;
			unsigned _id;
			std::shared_ptr<IDeformConfigure> _interface;
		};
		std::vector<DeformConfigure> _deformConfigures;
		unsigned _nextDeformConfigureId = 1;

		Threading::Mutex _lock;
	};

	unsigned Services::RegisterTextureLoader(
		StringSection<> initializerMatcher, 
		std::function<Assets::TextureLoaderSignature>&& loader)
	{
		ScopedLock(_pimpl->_lock);
		auto res = _pimpl->_nextTexturePluginId++;

		Pimpl::TexturePlugin plugin;
		plugin._loader = std::move(loader);
		plugin._id = res;
		plugin._initializerMatcher = initializerMatcher.AsString();
		_pimpl->_texturePlugins.push_back(std::move(plugin));
		return res;
	}

	void Services::DeregisterTextureLoader(unsigned pluginId)
	{
		ScopedLock(_pimpl->_lock);
		auto i = std::find_if(_pimpl->_texturePlugins.begin(), _pimpl->_texturePlugins.end(), [pluginId](const auto& c) { return c._id == pluginId; });
		if (i != _pimpl->_texturePlugins.end())
			_pimpl->_texturePlugins.erase(i);
	}

	void Services::SetFallbackTextureLoader(std::function<Assets::TextureLoaderSignature>&& loader)
	{
		ScopedLock(_pimpl->_lock);
		_pimpl->_fallbackTextureLoader = std::move(loader);
	}

	std::shared_ptr<BufferUploads::IAsyncDataSource> Services::CreateTextureDataSource(StringSection<> identifier, Assets::TextureLoaderFlags::BitField flags)
	{
		ScopedLock(_pimpl->_lock);
		for (const auto& plugin:_pimpl->_texturePlugins) {
			auto asStringView = cx::make_string_view(plugin._initializerMatcher.data(), plugin._initializerMatcher.size());
			if (wildcards::match(identifier, asStringView))
				return plugin._loader(identifier, flags);
		}
		if (_pimpl->_fallbackTextureLoader)
			return _pimpl->_fallbackTextureLoader(identifier, flags);
		return nullptr;
	}

	void Services::SetBufferUploads(const std::shared_ptr<BufferUploads::IManager>& manager)
	{
		_bufferUploads = manager;
	}

	void Services::SetCommonResources(const std::shared_ptr<CommonResourceBox>& res)
	{
		_commonResources = res;
	}

	IDeformConfigure* Services::FindDeformConfigure(StringSection<> name)
	{
		ScopedLock(_pimpl->_lock);
		for (auto& r:_pimpl->_deformConfigures)
			if (XlEqString(name, r._name)) return r._interface.get();
		return nullptr;
	}
	unsigned Services::RegisterDeformConfigure(StringSection<> name, std::shared_ptr<IDeformConfigure> interface)
	{
		ScopedLock(_pimpl->_lock);
		for (auto& r:_pimpl->_deformConfigures) assert(!XlEqString(name, r._name));
		unsigned id = _pimpl->_nextDeformConfigureId++;
		_pimpl->_deformConfigures.push_back({name.AsString(), id, std::move(interface)});
		return id;
	}
	void Services::DeregisterDeformConfigure(unsigned id)
	{
		ScopedLock(_pimpl->_lock);
		for (auto r=_pimpl->_deformConfigures.begin(); r!=_pimpl->_deformConfigures.end(); ++r)
			if (r->_id == id) {
				_pimpl->_deformConfigures.erase(r);
				return;
			}
		assert(0);		// didn't find it
	}

	Services::Services(const std::shared_ptr<RenderCore::IDevice>& device)
	{
		_pimpl = std::make_unique<Pimpl>();
		_device = device;
		_subFrameEvents = std::make_shared<SubFrameEvents>();
	}

	Services::~Services()
	{
	}

	// Our "s_instance" pointer to services must act as a weak pointer (otherwise clients can't control
	// the lifetime). We can achieve that with this pattern (though there may be some complexity between
	// the different ways clang and msvc handle dynamic libraries)
	static ConsoleRig::WeakAttachablePtr<Services> s_servicesInstance;

	bool Services::HasInstance() { return !s_servicesInstance.expired(); }
	Services& Services::GetInstance() { return *s_servicesInstance.lock(); }
}}

