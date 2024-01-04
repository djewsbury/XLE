// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ResourceConstructionContext.h"
#include "DeferredShaderResource.h"
#include "Drawables.h"
#include "Services.h"
#include "SubFrameEvents.h"
#include "../BufferUploads/IBufferUploads.h"
#include "../BufferUploads/BatchedResources.h"
#include "../../Assets/Continuation.h"
#include "../../Utility/Threading/Mutex.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Techniques
{

	class ResourceConstructionContext::Pimpl
	{
	public:
		Threading::Mutex _lock;
		struct ShaderResource
		{
			std::shared_future<std::shared_ptr<DeferredShaderResource>> _future;		// pending or invalid state
			std::weak_ptr<DeferredShaderResource> _completed;
		};
		std::vector<std::pair<uint64_t, ShaderResource>> _shaderResources;
		std::vector<BufferUploads::TransactionID> _uploadMarkers;
		std::shared_ptr<BufferUploads::IManager> _bufferUploads;
		std::shared_ptr<RepositionableGeometryConduit> _repositionableGeometry;
		SignalDelegateId _onFrameBarrierBind = ~0u;
		uint64_t _guid;

		struct RecentCompletions
		{
			Threading::Mutex _lock;
			std::vector<uint64_t> _shaderResourceCompletions;
		};
		std::shared_ptr<RecentCompletions> _recentCompletions;

		void OnFrameBarrier();
	};

	void ResourceConstructionContext::Cancel()
	{
		ScopedLock(_pimpl->_lock);
		std::sort(_pimpl->_uploadMarkers.begin(), _pimpl->_uploadMarkers.end());
		_pimpl->_uploadMarkers.erase(
			std::unique(_pimpl->_uploadMarkers.begin(), _pimpl->_uploadMarkers.end()),
			_pimpl->_uploadMarkers.end());
		_pimpl->_bufferUploads->Cancel(_pimpl->_uploadMarkers);
		_pimpl->_uploadMarkers.clear();
	}

	void ResourceConstructionContext::ReleaseWithoutCancel()
	{
		ScopedLock(_pimpl->_lock);
		_pimpl->_uploadMarkers.clear();
	}

	std::shared_future<std::shared_ptr<DeferredShaderResource>> ResourceConstructionContext::ConstructShaderResource(StringSection<> initializer)
	{
		std::promise<std::shared_ptr<DeferredShaderResource>> promise;

		ScopedLock(_pimpl->_lock);
		auto hash = Hash64(initializer);
		auto i = LowerBound(_pimpl->_shaderResources, hash);
		if (i!=_pimpl->_shaderResources.end() && i->first==hash) {
			if (i->second._future.valid())
				return i->second._future;

			if (auto l = i->second._completed.lock()) {
				promise.set_value(std::move(l));
				return promise.get_future();
			}

			// else fall through and re-construct
			i->second._future = promise.get_future();
		} else {
			i = _pimpl->_shaderResources.emplace(i, std::make_pair(hash, Pimpl::ShaderResource{promise.get_future()}));
		}

		auto uploadID = DeferredShaderResource::ConstructToTrackablePromise(std::move(promise), initializer);
		if (uploadID != BufferUploads::TransactionID_Invalid)
			_pimpl->_uploadMarkers.push_back(uploadID);

		::Assets::WhenAll(i->second._future).Then(
			[rc=_pimpl->_recentCompletions, hash](const auto&) {
				ScopedLock(rc->_lock);
				rc->_shaderResourceCompletions.push_back(hash);
			});
		
		return i->second._future;
	}

	std::shared_future<std::shared_ptr<DeferredShaderResource>> ResourceConstructionContext::ConstructShaderResource(const Assets::TextureCompilationRequest& compileRequest)
	{
		assert(0);	// todo -- implement
		return {};
	}

	std::future<BufferUploads::ResourceLocator> ResourceConstructionContext::ConstructStaticGeometry(
		std::shared_ptr<BufferUploads::IAsyncDataSource> dataSource,
		BindFlag::BitField bindFlags)
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
			auto res = _pimpl->_bufferUploads->Begin(std::move(dataSource), std::move(resourceSource));
			{
				ScopedLock(_pimpl->_lock);
				_pimpl->_uploadMarkers.push_back(res._transactionID);
			}
			return std::move(res._future);
		} else {
			auto res = _pimpl->_bufferUploads->Begin(std::move(dataSource), bindFlags);
			{
				ScopedLock(_pimpl->_lock);
				_pimpl->_uploadMarkers.push_back(res._transactionID);
			}
			return std::move(res._future);
		}
	}

	std::future<BufferUploads::ResourceLocator> ResourceConstructionContext::ConstructStaticGeometry(
		std::shared_ptr<BufferUploads::IDataPacket> dataSource,
		BindFlag::BitField bindFlags)
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

		auto desc = CreateDesc(bindFlags, LinearBufferDesc::Create(dataSource->GetData().size()));

		if (resourceSource) {
			auto res = _pimpl->_bufferUploads->Begin(desc, std::move(dataSource), std::move(resourceSource));
			{
				ScopedLock(_pimpl->_lock);
				_pimpl->_uploadMarkers.push_back(res._transactionID);
			}
			return std::move(res._future);
		} else {
			auto res = _pimpl->_bufferUploads->Begin(desc, std::move(dataSource), bindFlags);
			{
				ScopedLock(_pimpl->_lock);
				_pimpl->_uploadMarkers.push_back(res._transactionID);
			}
			return std::move(res._future);
		}
	}

	std::shared_ptr<RepositionableGeometryConduit> ResourceConstructionContext::GetRepositionableGeometryConduit()
	{
		return _pimpl->_repositionableGeometry;
	}

	void ResourceConstructionContext::AddUploads(IteratorRange<const BufferUploads::TransactionID*> transactions)
	{
		ScopedLock(_pimpl->_lock);
		_pimpl->_uploadMarkers.insert(_pimpl->_uploadMarkers.end(), transactions.begin(), transactions.end());
	}

	uint64_t ResourceConstructionContext::GetGUID() const
	{
		return _pimpl->_guid;
	}

	void ResourceConstructionContext::Pimpl::OnFrameBarrier()
	{
		std::vector<uint64_t> srCompletions;
		{
			ScopedLock(_recentCompletions->_lock);
			std::swap(_recentCompletions->_shaderResourceCompletions, srCompletions);
		}

		if (srCompletions.empty()) return;

		std::sort(srCompletions.begin(), srCompletions.end());
		srCompletions.erase(std::unique(srCompletions.begin(), srCompletions.end()), srCompletions.end());

		ScopedLock(_lock);
		auto i = _shaderResources.begin();
		for (auto c:srCompletions) {
			i = LowerBound2(MakeIteratorRange(i, _shaderResources.end()), c);
			if (i == _shaderResources.end() || i->first != c) continue;

			i->second._completed.reset();
			TRY {
				assert(i->second._future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
				i->second._completed = i->second._future.get();
				// Clearing the future will destroy the shader resource, unless some else is holding either the future or a strong pointer directly to the shader resource
				i->second._future = {};
			} CATCH(...) {
				// don't clear the future on exception -- we just leave the future in it's invalid state
			} CATCH_END
		}
	}

	static uint64_t s_nextConstructionContextGuid = 1;

	ResourceConstructionContext::ResourceConstructionContext(
		std::shared_ptr<BufferUploads::IManager> bufferUploads,
		std::shared_ptr<RepositionableGeometryConduit> repositionableGeo)
	{
		_pimpl = std::make_unique<Pimpl>();
		_pimpl->_bufferUploads = std::move(bufferUploads);
		_pimpl->_repositionableGeometry = std::move(repositionableGeo);
		_pimpl->_guid = s_nextConstructionContextGuid++;

		_pimpl->_recentCompletions = std::make_shared<Pimpl::RecentCompletions>();
		_pimpl->_onFrameBarrierBind = Services::GetSubFrameEvents()._onFrameBarrier.Bind([p=_pimpl.get()]() { p->OnFrameBarrier(); });
	}

	ResourceConstructionContext::~ResourceConstructionContext()
	{
		Cancel();
		if (_pimpl->_onFrameBarrierBind != ~0u)
			Services::GetSubFrameEvents()._onFrameBarrier.Unbind(_pimpl->_onFrameBarrierBind);
	}

}}
