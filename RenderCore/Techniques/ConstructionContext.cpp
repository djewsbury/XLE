// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ConstructionContext.h"
#include "DeferredShaderResource.h"
#include "Drawables.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../BufferUploads/BatchedResources.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Techniques
{

	class ConstructionContext::Pimpl
	{
	public:
		Threading::Mutex _lock;
		std::vector<std::pair<uint64_t, std::shared_future<std::shared_ptr<DeferredShaderResource>>>> _shaderResources;
		std::vector<BufferUploads::TransactionID> _uploadMarkers;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;
		std::shared_ptr<RepositionableGeometryConduit> _repositionableGeometry;
		uint64_t _guid;
	};

	void ConstructionContext::Cancel()
	{
		ScopedLock(_pimpl->_lock);
		std::sort(_pimpl->_uploadMarkers.begin(), _pimpl->_uploadMarkers.end());
		_pimpl->_uploadMarkers.erase(
			std::unique(_pimpl->_uploadMarkers.begin(), _pimpl->_uploadMarkers.end()),
			_pimpl->_uploadMarkers.end());
		_pimpl->_bufferUploads->Transaction_Cancel(_pimpl->_uploadMarkers);
		_pimpl->_uploadMarkers.clear();
	}

	void ConstructionContext::ReleaseWithoutCancel()
	{
		ScopedLock(_pimpl->_lock);
		_pimpl->_uploadMarkers.clear();
	}

	std::shared_future<std::shared_ptr<DeferredShaderResource>> ConstructionContext::ConstructShaderResource(StringSection<> initializer)
	{
		ScopedLock(_pimpl->_lock);
		auto hash = Hash64(initializer);
		auto i = LowerBound(_pimpl->_shaderResources, hash);
		if (i==_pimpl->_shaderResources.end() || i->first!=hash)
			return i->second;

		std::promise<std::shared_ptr<DeferredShaderResource>> promise;
		i = _pimpl->_shaderResources.insert(i, std::make_pair(hash, promise.get_future()));
		auto uploadID = DeferredShaderResource::ConstructToTrackablePromise(std::move(promise), initializer);
		if (uploadID != BufferUploads::TransactionID_Invalid)
			_pimpl->_uploadMarkers.push_back(uploadID);
		
		return i->second;
	}

	std::shared_future<std::shared_ptr<DeferredShaderResource>> ConstructionContext::ConstructShaderResource(const Assets::TextureCompilationRequest& compileRequest)
	{
		assert(0);	// todo -- implement
		return {};
	}

	std::future<BufferUploads::ResourceLocator> ConstructionContext::ConstructStaticGeometry(
		std::shared_ptr<BufferUploads::IAsyncDataSource> dataSource,
		BindFlag::BitField bindFlags,
		StringSection<> resourceName)
	{
		std::shared_ptr<BufferUploads::IResourcePool> resourceSource;
		if (_pimpl->_repositionableGeometry) {
			if (bindFlags & BindFlag::VertexBuffer) {
				assert(!(bindFlags & BindFlag::IndexBuffer));
				resourceSource = _pimpl->_repositionableGeometry->GetVBResourcePool();
			} else if (bindFlags & BindFlag::IndexBuffer) {
				resourceSource = _pimpl->_repositionableGeometry->GetIBResourcePool();
			}
		}

		if (resourceSource) {
			auto res = _pimpl->_bufferUploads->Transaction_Begin(std::move(dataSource), std::move(resourceSource));
			{
				ScopedLock(_pimpl->_lock);
				_pimpl->_uploadMarkers.push_back(res._transactionID);
			}
			return std::move(res._future);
		} else {
			auto res = _pimpl->_bufferUploads->Transaction_Begin(std::move(dataSource), bindFlags);
			{
				ScopedLock(_pimpl->_lock);
				_pimpl->_uploadMarkers.push_back(res._transactionID);
			}
			return std::move(res._future);
		}
	}

	std::shared_ptr<RepositionableGeometryConduit> ConstructionContext::GetRepositionableGeometryConduit()
	{
		return _pimpl->_repositionableGeometry;
	}

	void ConstructionContext::AddUploads(IteratorRange<const BufferUploads::TransactionID*> transactions)
	{
		ScopedLock(_pimpl->_lock);
		_pimpl->_uploadMarkers.insert(_pimpl->_uploadMarkers.end(), transactions.begin(), transactions.end());
	}

	uint64_t ConstructionContext::GetGUID() const
	{
		return _pimpl->_guid;
	}

	static uint64_t s_nextConstructionContextGuid = 1;

	ConstructionContext::ConstructionContext(
		std::shared_ptr<BufferUploads::IManager> bufferUploads,
		std::shared_ptr<RepositionableGeometryConduit> repositionableGeo)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_bufferUploads = std::move(bufferUploads);
		_pimpl->_repositionableGeometry = std::move(repositionableGeo);
		_pimpl->_guid = s_nextConstructionContextGuid++;
	}

	ConstructionContext::~ConstructionContext()
	{
		Cancel();
	}

}}
