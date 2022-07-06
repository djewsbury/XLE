// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../ResourceDesc.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"
#include <memory>
#include <future>

namespace RenderCore { namespace BufferUploads { using TransactionID = uint64_t; class ResourceLocator; class IAsyncDataSource; class IManager; }}
namespace RenderCore { namespace Assets { class TextureCompilationRequest; class ModelScaffold; }}

namespace RenderCore { namespace Techniques
{
	class RepositionableGeometryConduit;
	class DeferredShaderResource;
	
	class ConstructionContext
	{
	public:
		// Cancel any operations were previously queued via this context, but haven't completed yet
		void Cancel();

		// Allows any construction operations queued previous to complete, even if this ConstructionContext is destroyed
		// Doesn't effect operations queued in the future, however
		void ReleaseWithoutCancel();

		std::shared_future<std::shared_ptr<DeferredShaderResource>> ConstructShaderResource(StringSection<>);
		std::shared_future<std::shared_ptr<DeferredShaderResource>> ConstructShaderResource(const Assets::TextureCompilationRequest& compileRequest);

		std::future<BufferUploads::ResourceLocator> ConstructStaticGeometry(
			std::shared_ptr<BufferUploads::IAsyncDataSource> dataSource,
			BindFlag::BitField bindFlags,
			StringSection<> resourceName);

		std::shared_ptr<RepositionableGeometryConduit> GetRepositionableGeometryConduit();

		void AddUploads(IteratorRange<const BufferUploads::TransactionID*> transactions);

		uint64_t GetGUID() const;

		ConstructionContext(
			std::shared_ptr<BufferUploads::IManager>,
			std::shared_ptr<RepositionableGeometryConduit>);
		~ConstructionContext();
	private:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};
}}
