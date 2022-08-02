// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetUtils.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/StringUtils.h"
#include <memory>
#include <functional>
#include <vector>
#include <string>

namespace OSServices { class LibVersionDesc; }

namespace Assets
{
	class ICompileOperation;
	class IIntermediateCompileMarker;
	class InitializerPack;
	class IntermediatesStore;
	class DependencyValidation;

    class IIntermediateCompilers
    {
    public:
        virtual std::shared_ptr<IIntermediateCompileMarker> Prepare(CompileRequestCode, InitializerPack&&) = 0;
        virtual void StallOnPendingOperations(bool cancelAll) = 0;
		
		struct SplitArchiveName { std::string _archive; uint64_t _entryId = 0ull; std::string _descriptiveName; };
		using CompileOperationDelegate = std::function<std::shared_ptr<ICompileOperation>(const InitializerPack&)>;
		using ArchiveNameDelegate = std::function<SplitArchiveName(ArtifactTargetCode, const InitializerPack&)>;

		using RegisteredCompilerId = uint64_t;
		virtual RegisteredCompilerId RegisterCompiler(
			const std::string& name,										///< string name for the compiler, usually something user-presentable
			const std::string& shortName,									///< shortened name, for the intermediate assets store
			OSServices::LibVersionDesc srcVersion,							///< version information for the module (propagated onto any assets written to disk)
			const DependencyValidation& compilerDepVal,						///< dependency validation for the compiler shared library itself. Can trigger recompiles if the compiler changes
			CompileOperationDelegate&& delegate,							///< delegate that can create the ICompileOperation for a given asset
			ArchiveNameDelegate&& archiveNameDelegate = {}					///< delegate used to store the artifacts with in ArchiveCache, rather than individual files (optional)
			) = 0;

		virtual void DeregisterCompiler(RegisteredCompilerId id) = 0;

		// AssociateRequest associates a pattern with a compiler (previously registered with RegisterCompiler)
		// When requests are made (via Prepare) that match the pattern, that compiler can be selected to
		// handle the request
		virtual void AssociateRequest(
			RegisteredCompilerId compiler,
			IteratorRange<const CompileRequestCode*> compileRequestCode,	///< id used to request this compilation operation. Matches "CompileProcessType" in compilable asset types
			const std::string& initializerRegexFilter = {}					///< compiler will be invoked for assets that match this regex filter
			) = 0;

		//

		// AssociateExtensions & GetExtensionsForTargetCodes are both used for FileOpen dialogs in tools
		// It's so the tool knows what model formats are available to load (for example)
		virtual void AssociateExtensions(RegisteredCompilerId associatedCompiler, const std::string& commaSeparatedExtensions) = 0;
		virtual std::vector<std::pair<std::string, std::string>> GetExtensionsForTargetCode(CompileRequestCode typeCode) = 0;
		virtual std::vector<CompileRequestCode> GetTargetCodesForExtension(StringSection<>) = 0;

		//

		virtual void FlushCachedMarkers() = 0;
		virtual ~IIntermediateCompilers() = default;
	};

	std::shared_ptr<IIntermediateCompilers> CreateIntermediateCompilers(const std::shared_ptr<IntermediatesStore>& store);

	class CompilerRegistration
	{
	public:
		IIntermediateCompilers::RegisteredCompilerId RegistrationId() const { return _registration; }

		CompilerRegistration(
			IIntermediateCompilers& compilers,
			const std::string& name,
			const std::string& shortName,
			OSServices::LibVersionDesc srcVersion,
			const DependencyValidation& compilerDepVal,
			IIntermediateCompilers::CompileOperationDelegate&& delegate,
			IIntermediateCompilers::ArchiveNameDelegate&& archiveNameDelegate = {});
		CompilerRegistration();
		~CompilerRegistration();
		CompilerRegistration(CompilerRegistration&&);
		CompilerRegistration& operator=(CompilerRegistration&&);
	private:
		IIntermediateCompilers* _compilers = nullptr;
		IIntermediateCompilers::RegisteredCompilerId _registration = ~0u;
	};

	class DirectorySearchRules;
	DirectorySearchRules DefaultLibrarySearchDirectories();

	std::vector<CompilerRegistration> DiscoverCompileOperations(
		IIntermediateCompilers& compilerManager,
		StringSection<> librarySearch,
		const DirectorySearchRules& searchRules = DefaultLibrarySearchDirectories());

	class IArtifactCollection;
	class ArtifactCollectionFuture;

	/// <summary>Returned from a IAssetCompiler on response to a compile request</summary>
	/// After receiving a compile marker, the caller can choose to either retrieve an existing
	/// artifact from a previous compile, or begin a new asynchronous compile operation.
	class IIntermediateCompileMarker
	{
	public:
		virtual std::shared_ptr<IArtifactCollection> GetExistingAsset(ArtifactTargetCode) const = 0;
		virtual std::shared_ptr<ArtifactCollectionFuture> InvokeCompile() = 0;
		virtual std::string GetCompilerDescription() const = 0;
		virtual ~IIntermediateCompileMarker();
	};
}

