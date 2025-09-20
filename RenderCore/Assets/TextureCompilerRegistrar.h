#pragma once

#include "../../Assets/CompoundAsset.h"
#include "../../Assets/OperationContext.h"
#include "../../Utility/FunctionUtils.h"
#include <functional>

namespace RenderCore::BufferUploads { class IAsyncDataSource; }

namespace RenderCore { namespace Assets
{
	class ITextureCompiler
	{
	public:
		struct Context
		{
			::Assets::OperationContextHelper* _opContext = nullptr;
			const VariantFunctions* _conduit = nullptr;
			std::vector<::Assets::DependencyValidation> _dependencies;
		};

		virtual std::string GetIntermediateName() const = 0;
		virtual std::shared_ptr<BufferUploads::IAsyncDataSource> ExecuteCompile(Context& ctx) = 0;
		virtual ~ITextureCompiler();
	};

	class TextureCompilerRegistrar
	{
	public:
		using SubCompilerFunctionSig = std::shared_ptr<ITextureCompiler>(
			std::shared_ptr<::AssetsNew::CompoundAssetUtil>,
			const ::AssetsNew::ScaffoldAndEntityName&);

		using RegistrationId = unsigned;
		RegistrationId Register(std::function<SubCompilerFunctionSig>&&);
		void Deregister(RegistrationId);

		std::shared_ptr<ITextureCompiler> TryBeginCompile(
			std::shared_ptr<::AssetsNew::CompoundAssetUtil>,
			const ::AssetsNew::ScaffoldAndEntityName&);

		TextureCompilerRegistrar();
		~TextureCompilerRegistrar();

	protected:
		Threading::Mutex _mutex;
		std::vector<std::pair<RegistrationId, std::function<SubCompilerFunctionSig>>> _fns;
		RegistrationId _nextRegistrationId;
	};
}}
