#pragma once

#include "../../Assets/CompoundAsset.h"
#include "../../Assets/OperationContext.h"
#include "../../Utility/FunctionUtils.h"
#include <functional>

namespace RenderCore::BufferUploads { class IAsyncDataSource; }

namespace RenderCore { namespace Assets
{
	class TextureCompilerRegistrar
	{
	public:
		struct SubCompilerContext
		{
			::Assets::OperationContextHelper _opContext;
			const VariantFunctions _conduit;
			std::vector<::Assets::DependencyValidation> _dependencies;
		};
		using SubCompilerFunctionSig = std::shared_ptr<BufferUploads::IAsyncDataSource>(
			SubCompilerContext&,
			std::shared_ptr<::AssetsNew::CompoundAssetUtil>,
			const ::AssetsNew::ScaffoldIndexer&);

		using RegistrationId = unsigned;
		RegistrationId Register(std::function<SubCompilerFunctionSig>&&);
		void Deregister(RegistrationId);

	protected:
		Threading::Mutex _mutex;
		std::
	};
}}
