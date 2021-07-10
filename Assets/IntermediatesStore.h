// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "AssetsCore.h"
#include "ICompileOperation.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/StringUtils.h"
#include "../Utility/Threading/Mutex.h"
#include <set>
#include <vector>

namespace Utility { class OutputStream; }
namespace OSServices { class BasicFile; }
namespace ConsoleRig { class LibVersionDesc; }

namespace Assets
{
	class DependentFileState;
	class DependencyValidation;
	class IArtifactCollection;
	class IFileSystem;

	/// <summary>Archive of compiled intermediate assets</summary>
	/// When compile operations succeed, the resulting artifacts are cached in an IntermediatesStore,
	/// which is typically in permanent memory (ie, on disk).
	///
	/// When working with multiple different versions of the engine codebase, it's necessary to have separate
	/// copies of the intermediate store (ie, because of changes to the data format, etc). This object provides
	/// the logic to select the correct store for the current codebase.
	///
	/// This make it easier to rapidly switch between different versions of the codebase, which can allow (for
	/// example) performance comparisons between different versions. Or, consider the case where we have 2
	/// executables (eg, a game executable and a GUI tool executable) which we want to use with the same 
	/// source assets, but they may have been compiled with different version of the engine code. This system
	/// allows both executables to maintain separate copies of the intermediate store.
	class IntermediatesStore
	{
	public:
		using CompileProductsGroupId = uint64_t;
		using ArchiveEntryId = uint64_t;

		// --------- Store & retrieve loose files ---------
		std::shared_ptr<IArtifactCollection> StoreCompileProducts(
            StringSection<> archivableName,
			CompileProductsGroupId groupId,
			IteratorRange<const ICompileOperation::SerializedArtifact*> artifacts,
			::Assets::AssetState state,
			IteratorRange<const DependentFileState*> dependencies);

		std::shared_ptr<IArtifactCollection> RetrieveCompileProducts(
            StringSection<> archivableName,
			CompileProductsGroupId groupId);

		// --------- Store & retrieve from optimized archive caches ---------
		void StoreCompileProducts(
            StringSection<> archiveName,
			ArchiveEntryId entryId,
			StringSection<> entryDescriptiveName,
			CompileProductsGroupId groupId,
			IteratorRange<const ICompileOperation::SerializedArtifact*> artifacts,
			::Assets::AssetState state,
			IteratorRange<const DependentFileState*> dependencies);

		std::shared_ptr<IArtifactCollection> RetrieveCompileProducts(
            StringSection<> archiveName,
			ArchiveEntryId entryId,
			CompileProductsGroupId groupId);

		// --------- Registration & utilities ---------
		CompileProductsGroupId RegisterCompileProductsGroup(
			StringSection<> name,
			const ConsoleRig::LibVersionDesc& compilerVersionInfo,
			bool enableArchiveCacheSet = false);

		void FlushToDisk();

		static auto GetDependentFileState(StringSection<> filename) -> DependentFileState;
		static void ShadowFile(StringSection<> filename);
		static bool TryRegisterDependency(
			DependencyValidation& target,
			const DependentFileState& fileState,
			const StringSection<> assetName);

		IntermediatesStore(
			std::shared_ptr<IFileSystem> intermediatesFilesystem,
			StringSection<> baseDirectory,
			StringSection<> versionString,
			StringSection<> configString,
			bool universal = false);
		~IntermediatesStore();
		IntermediatesStore(const IntermediatesStore&) = delete;
		IntermediatesStore& operator=(const IntermediatesStore&) = delete;

	protected:
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

	class StoreReferenceCounts
	{
	public:
		Threading::Mutex _lock;
		std::set<uint64_t> _storeOperationsInFlight;
		std::vector<std::pair<uint64_t, unsigned>> _readReferenceCount;
	};
}
