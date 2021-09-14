// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "LooseFilesCache.h"
#include "IntermediatesStore.h"
#include "AssetUtils.h"
#include "IFileSystem.h"
#include "ChunkFile.h"
#include "ChunkFileContainer.h"
#include "IArtifact.h"
#include "DepVal.h"
#include "BlockSerializer.h"
#include "../OSServices/Log.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/StreamFormatter.h"
#include "../Utility/Streams/OutputStreamFormatter.h"
#include "../Utility/Streams/Stream.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/SerializationUtils.h"
#include "../Utility/Conversion.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/StringFormat.h"
#include <filesystem>

namespace Assets
{
	static const auto ChunkType_Metrics = ConstHash64<'Metr', 'ics'>::Value;
	static const auto ChunkType_Log = ConstHash64<'Log'>::Value;
	static const auto ChunkType_Multi = ConstHash64<'Mult', 'iChu', 'nk'>::Value;

	class CompileProductsFile
	{
	public:
		struct Product
		{
			uint64_t _type;
			std::string _intermediateArtifact;
		};
		std::vector<Product> _compileProducts;
		std::vector<DependentFileState> _dependencies;

		::Assets::AssetState _state = AssetState::Ready;
		std::string _basePath;

		const Product* FindProduct(uint64_t type) const
		{
			for (const auto&p:_compileProducts)
				if (p._type == type)
					return &p;
			return nullptr;
		}
	};

	static std::shared_ptr<IArtifactCollection> MakeArtifactCollection(
		const CompileProductsFile& productsFile, 
		std::shared_ptr<IFileSystem> fs,
		const ::Assets::DependencyValidation& depVal,
		const std::shared_ptr<StoreReferenceCounts>& refCounts,
		uint64_t refCountHashCode);

	static void SerializationOperator(OutputStreamFormatter& formatter, const CompileProductsFile& compileProducts)
	{
		formatter.WriteKeyedValue("BasePath", compileProducts._basePath);
		formatter.WriteKeyedValue("Invalid", compileProducts._state == AssetState::Ready ? "0" : "1");

		for (const auto&product:compileProducts._compileProducts) {
			auto ele = formatter.BeginKeyedElement(std::to_string(product._type));
			formatter.WriteKeyedValue("Artifact", product._intermediateArtifact.c_str());
			formatter.EndElement(ele);
		}

		{
			auto ele = formatter.BeginKeyedElement("Dependencies");
			for (const auto&product:compileProducts._dependencies) {
				if (product._status == DependentFileState::Status::DoesNotExist) {
					formatter.WriteKeyedValue(
						MakeStringSection(product._filename), 
						"doesnotexist");
				} else if (product._status == DependentFileState::Status::Shadowed) {
					formatter.WriteKeyedValue(
						MakeStringSection(product._filename), 
						"shadowed");
				} else {
					formatter.WriteKeyedValue(
						MakeStringSection(product._filename), 
						MakeStringSection(std::to_string(product._timeMarker)));
				}
			}
			formatter.EndElement(ele);
		}
	}

	static void DeserializationOperator(InputStreamFormatter<utf8>& formatter, CompileProductsFile::Product& result)
	{
		while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
			StringSection<utf8> name, value;
			if (!formatter.TryKeyedItem(name) || !formatter.TryValue(value))
				Throw(Utility::FormatException("Poorly formed attribute in CompileProductsFile", formatter.GetLocation()));
			if (XlEqString(name, "Artifact")) {
				result._intermediateArtifact = value.AsString();
			} else
				Throw(Utility::FormatException("Unknown attribute in CompileProductsFile", formatter.GetLocation()));
		}
	}

	static void DerializeDependencies(InputStreamFormatter<utf8>& formatter, CompileProductsFile& result)
	{
		while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
			StringSection<utf8> name, value;
			if (!formatter.TryKeyedItem(name) || !formatter.TryValue(value))
				Throw(Utility::FormatException("Poorly formed attribute in CompileProductsFile", formatter.GetLocation()));
			if (XlEqString(value, "doesnotexist")) {
				result._dependencies.push_back(DependentFileState {
					name.AsString(),
					0, DependentFileState::Status::DoesNotExist
				});
			} else if (XlEqString(value, "shadowed")) {
			} else {
				result._dependencies.push_back(DependentFileState {
					name.AsString(),
					Conversion::Convert<uint64_t>(value)
				});
			}
		}
	}

	static StringSection<utf8> DeserializeValue(InputStreamFormatter<utf8>& formatter)
	{
		StringSection<utf8> value;
		if (!formatter.TryValue(value))
			Throw(Utility::FormatException("Expecting value", formatter.GetLocation()));
		return value;
	}

	static void DeserializationOperator(InputStreamFormatter<utf8>& formatter, CompileProductsFile& result)
	{
		while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
			InputStreamFormatter<utf8>::InteriorSection name;
			if (!formatter.TryKeyedItem(name))
				Throw(Utility::FormatException("Poorly formed item in CompileProductsFile", formatter.GetLocation()));

			if (XlEqString(name, "Dependencies")) {
				RequireBeginElement(formatter);
				DerializeDependencies(formatter, result);
				RequireEndElement(formatter);
			} else if (XlEqString(name, "BasePath")) {
				result._basePath = DeserializeValue(formatter).AsString();
			} else if (XlEqString(name, "Invalid")) {
				if (XlEqString(DeserializeValue(formatter), "1")) {
					result._state = AssetState::Invalid;
				} else
					result._state = AssetState::Ready;
			} else if (formatter.PeekNext() == FormatterBlob::BeginElement) {
				RequireBeginElement(formatter);
				CompileProductsFile::Product product;
				formatter >> product;
				product._type = Conversion::Convert<uint64_t>(name);
				result._compileProducts.push_back(product);
				RequireEndElement(formatter);
			} else
				Throw(Utility::FormatException("Unknown attribute in CompileProductsFile", formatter.GetLocation()));
		}
	}

	static std::pair<::Assets::DependencyValidation, bool> GetDepVal(const CompileProductsFile& finalProductsFile, StringSection<> archivableName)
	{
		bool stillValid = true;
		auto depVal = GetDepValSys().Make();
		for (const auto&dep:finalProductsFile._dependencies) {
			if (!finalProductsFile._basePath.empty()) {
				auto adjustedDep = dep;
				char buffer[MaxPath];
				Legacy::XlConcatPath(buffer, dimof(buffer), finalProductsFile._basePath.c_str(), AsPointer(dep._filename.begin()), AsPointer(dep._filename.end()));
				adjustedDep._filename = buffer;
				stillValid &= IntermediatesStore::TryRegisterDependency(depVal, adjustedDep, archivableName);
			} else {
				stillValid &= IntermediatesStore::TryRegisterDependency(depVal, dep, archivableName);
			}
		}
		return std::make_pair(std::move(depVal), stillValid);
	}

	std::shared_ptr<IArtifactCollection> LooseFilesStorage::RetrieveCompileProducts(
		StringSection<> archivableName,
		const std::shared_ptr<StoreReferenceCounts>& storeRefCounts,
		uint64_t hashCode)
	{
		auto intermediateName = MakeProductsFileName(archivableName);
		std::unique_ptr<IFileInterface> productsFile;
		auto ioResult = TryOpen(productsFile, *_filesystem, MakeStringSection(intermediateName), "rb", 0);
		if (ioResult != ::Assets::IFileSystem::IOReason::Success || !productsFile)
			return nullptr;
	
		size_t size = productsFile->GetSize();
		auto productsFileData = std::make_unique<char[]>(size);
		productsFile->Read(productsFileData.get(), 1, size);

		InputStreamFormatter<> formatter(
			MakeStringSection(productsFileData.get(), PtrAdd(productsFileData.get(), size)));

		CompileProductsFile finalProductsFile;
		formatter >> finalProductsFile;
		auto depVal = GetDepVal(finalProductsFile, archivableName);
		if (!depVal.second)
			return nullptr;
		return MakeArtifactCollection(finalProductsFile, _filesystem, depVal.first, storeRefCounts, hashCode);
	}

	static std::string MakeSafeName(StringSection<> input, size_t sizeLimit = std::numeric_limits<size_t>::max())
	{
		auto result = input.AsString();
		for (auto&b:result)
			if (b == ':' || b == '*' || b == '/' || b == '\\') b = '-';
		if (result.size() > sizeLimit) {
			// shorten, but try to keep extension
			auto splitter = MakeFileNameSplitter(result);
			assert(splitter.ParametersWithDivider().IsEmpty());
			if (!splitter.ExtensionWithPeriod().IsEmpty() && (splitter.ExtensionWithPeriod().size()+1) <= sizeLimit) {
				auto extSize = splitter.ExtensionWithPeriod().size();
				auto nonExtLength = sizeLimit - extSize;
				result.erase(result.begin()+nonExtLength, result.end()-extSize);
			} else
				result.erase(result.begin()+sizeLimit, result.end());
		}
		return result;
	}

	static std::unique_ptr<IFileInterface> OpenFileInterface(IFileSystem& filesystem, StringSection<> fn, const char openMode[], OSServices::FileShareMode::BitField shareMode)
	{
		std::unique_ptr<IFileInterface> result;
		auto ioResult = TryOpen(result, filesystem, fn, openMode, shareMode);
		if (ioResult != MainFileSystem::IOReason::Success || !result)
			Throw(std::runtime_error("Failed to open file in loose files cache: " + fn.AsString()));
		return result;
	}

	std::shared_ptr<IArtifactCollection> LooseFilesStorage::StoreCompileProducts(
		StringSection<> archivableName,
		IteratorRange<const ICompileOperation::SerializedArtifact*> artifacts,
		::Assets::AssetState state,
		IteratorRange<const DependentFileState*> dependencies,
		const std::shared_ptr<StoreReferenceCounts>& storeRefCounts,
		uint64_t hashCode)
	{
		CompileProductsFile compileProductsFile;
		compileProductsFile._state = state;

		compileProductsFile._dependencies.reserve(dependencies.size());
		for (const auto& s:dependencies) {
			auto adjustedDep = s;
			adjustedDep._filename = MakeSplitPath(s._filename).Simplify().Rebuild();
			assert(!adjustedDep._filename.empty());
			compileProductsFile._dependencies.push_back(adjustedDep);
		}

		// MakeProductsFileName limits the result to MaxPath-20
		//	Those extra 20 characters allow for: "-<blockname>.metrics.s" so long as <blockname> does not exceed 9 characters 
		auto productsName = MakeProductsFileName(archivableName);
		OSServices::CreateDirectoryRecursive(MakeFileNameSplitter(productsName).DriveAndPath());
		std::vector<std::pair<std::string, std::string>> renameOps;

		// Will we create one chunk file that will contain most of the artifacts
		// However, some special artifacts (eg, metric files), can become separate files
		std::vector<ICompileOperation::SerializedArtifact> chunksInMainFile;
		for (const auto&a:artifacts)
			if (a._chunkTypeCode == ChunkType_Metrics) {
				std::string metricsName;
				if (!a._name.empty()) {
					metricsName = productsName + "-" + MakeSafeName(a._name, 9) + ".metrics";
				} else 
					metricsName = productsName + ".metrics";
				auto outputFile = OpenFileInterface(*_filesystem, metricsName + ".s", "wb", 0);
				outputFile->Write((const void*)AsPointer(a._data->cbegin()), 1, a._data->size());
				compileProductsFile._compileProducts.push_back({a._chunkTypeCode, metricsName});
				renameOps.push_back({metricsName + ".s", metricsName});
			} else if (a._chunkTypeCode == ChunkType_Log) {
				std::string metricsName;
				if (!a._name.empty()) {
					metricsName = productsName + "-" + MakeSafeName(a._name, 9) + ".log";
				} else 
					metricsName = productsName + ".log";
				auto outputFile = OpenFileInterface(*_filesystem, metricsName + ".s", "wb", 0);
				outputFile->Write((const void*)AsPointer(a._data->cbegin()), 1, a._data->size());
				compileProductsFile._compileProducts.push_back({a._chunkTypeCode, metricsName});
				renameOps.push_back({metricsName + ".s", metricsName});
			} else {
				chunksInMainFile.push_back(a);
			}

		if (chunksInMainFile.size() == 1) {
			auto& a = chunksInMainFile[0];
			std::string mainArtifactName = productsName + "-" + MakeSafeName(a._name, 9);
			auto outputFile = OpenFileInterface(*_filesystem, mainArtifactName + ".s", "wb", 0);
			outputFile->Write((const void*)AsPointer(a._data->cbegin()), 1, a._data->size());
			compileProductsFile._compileProducts.push_back({a._chunkTypeCode, mainArtifactName});
			renameOps.push_back({mainArtifactName + ".s", mainArtifactName});
		} else if (!chunksInMainFile.empty()) {
			auto mainBlobName = productsName + ".chunk";
			auto outputFile = OpenFileInterface(*_filesystem, mainBlobName + ".s", "wb", 0);
			ChunkFile::BuildChunkFile(*outputFile, MakeIteratorRange(chunksInMainFile), _compilerVersionInfo);
			compileProductsFile._compileProducts.push_back({ChunkType_Multi, mainBlobName});
			renameOps.push_back({mainBlobName + ".s", mainBlobName});
		}

		// note -- we can set compileProductsFile._basePath here, and then make the dependencies
		// 			within the compiler products file into relative filenames
		/*
			auto basePathSplitPath = MakeSplitPath(compileProductsFile._basePath);
			if (!compileProductsFile._basePath.empty()) {
				filename = MakeRelativePath(basePathSplitPath, MakeSplitPath(filename));
			} else {
		*/

		{
			std::shared_ptr<IFileInterface> productsFile = OpenFileInterface(*_filesystem, productsName + ".s", "wb", 0); // note -- no sharing allowed on this file. We take an exclusive lock on it
			FileOutputStream stream(productsFile);
			OutputStreamFormatter fmtter(stream);
			fmtter << compileProductsFile;
			renameOps.push_back({productsName + ".s", productsName});
		}

#if defined(_DEBUG)
		// Check for duplicated names in renameOps. Any dupes will result in exceptions later
		for (auto i=renameOps.begin(); i!=renameOps.end(); ++i)
			for (auto i2=renameOps.begin(); i2!=i; ++i2) {
				if (i->first == i2->first)
					Throw(std::runtime_error("Duplicated rename op in LooseFilesStorage for intermediate: " + i->first));
				if (i->second == i2->second)
					Throw(std::runtime_error("Duplicated rename op in LooseFilesStorage for intermediate: " + i->second));
			}
#endif

		// If we get to here successfully, go ahead and rename all of the staging files to their final names 
		// This gives us a little bit of protection against exceptions while writing out the staging files
		for (const auto& renameOp:renameOps) {
			std::filesystem::remove(renameOp.second);
			std::filesystem::rename(renameOp.first, renameOp.second);
		}

		return MakeArtifactCollection(compileProductsFile, _filesystem, GetDepVal(compileProductsFile, archivableName).first, storeRefCounts, hashCode);
	}

	std::string LooseFilesStorage::MakeProductsFileName(StringSection<> archivableName)
	{
		std::string result = _baseDirectory;
		result.reserve(result.size() + archivableName.size());
		for (auto b:archivableName)
			result.push_back((b != ':' && b != '*')?b:'-');

		const auto graceChars = 20;		// allow some space for concatenations
		if (result.size() > (MaxPath-graceChars)) {
			// shorten by replacing part of the name with a hash
			auto breakPoint = result.begin()+(MaxPath-graceChars-16);
			if (std::find(breakPoint, result.end(), '/') != result.end() || std::find(breakPoint, result.end(), '\\') != result.end())
				Throw(std::runtime_error("Loose file cache directory name is too long to shorten: " + result));
			auto hash = Hash64(MakeStringSection(breakPoint, result.end()));
			result.erase(breakPoint, result.end());
			result += std::to_string(hash);
		}
		return result;
	}

	LooseFilesStorage::LooseFilesStorage(std::shared_ptr<IFileSystem> filesystem, StringSection<> baseDirectory, const ConsoleRig::LibVersionDesc& compilerVersionInfo)
	: _baseDirectory(baseDirectory.AsString())
	, _compilerVersionInfo(compilerVersionInfo)
	, _filesystem(std::move(filesystem))
	{}
	LooseFilesStorage::~LooseFilesStorage() {}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	static Blob TryLoadFileAsBlob(IFileSystem&fs, StringSection<char> sourceFileName)
	{
		std::unique_ptr<IFileInterface> file;
		if (TryOpen(file, fs, sourceFileName, "rb", OSServices::FileShareMode::Read) == IFileSystem::IOReason::Success) {
			size_t size = file->GetSize();
			if (size) {
				auto result = std::make_shared<std::vector<uint8_t>>(size);
				file->Read(result->data(), 1, size);
				return result;
			}
		}
		return nullptr;
	}

	static std::unique_ptr<uint8_t[], PODAlignedDeletor> TryLoadFileAsAlignedBuffer(IFileSystem&fs, StringSection<char> sourceFileName, size_t& res)
	{
		std::unique_ptr<IFileInterface> file;
		if (TryOpen(file, fs, sourceFileName, "rb", OSServices::FileShareMode::Read) == IFileSystem::IOReason::Success) {
			size_t size = file->GetSize();
			if (size) {
				uint8_t* mem = (uint8*)XlMemAlign(size, sizeof(uint64_t));
				auto result = std::unique_ptr<uint8_t[], PODAlignedDeletor>(mem);
				file->Read(result.get(), 1, size);
				return result;
			}
		}
		return nullptr;
	}

	class CompileProductsArtifactCollection : public IArtifactCollection
	{
	public:
		std::vector<ArtifactRequestResult> ResolveRequests(IteratorRange<const ArtifactRequest*> requests) const override
		{
			std::vector<ArtifactRequestResult> result;
			result.resize(requests.size());

			std::vector<ArtifactRequest> requestsForMulti;
			std::vector<unsigned> requestsForMultiMapping;
			requestsForMulti.reserve(requests.size());
			requestsForMultiMapping.reserve(requests.size());

			// Look for exact matches in the compile products list. This is required for retrieving "log"
			// files. Though we only do this with "sharedblob" types
			for (size_t r=0; r<requests.size(); ++r) {
				bool foundExactMatch = false;
				if (requests[r]._dataType == ArtifactRequest::DataType::SharedBlob) {
					for (const auto&prod:_productsFile._compileProducts)
						if (prod._type == requests[r]._chunkTypeCode) {
							auto fileData = TryLoadFileAsBlob(*_filesystem, prod._intermediateArtifact);
							result[r] = {nullptr, 0, fileData, nullptr};
							foundExactMatch = true;
							break;
						}
				} else if (requests[r]._dataType == ArtifactRequest::DataType::Filename) {
					for (const auto&prod:_productsFile._compileProducts)
						if (prod._type == requests[r]._chunkTypeCode) {
							result[r] = {nullptr, 0, {}, nullptr, prod._intermediateArtifact};
							foundExactMatch = true;
							break;
						}
				} else if (requests[r]._dataType == ArtifactRequest::DataType::BlockSerializer || requests[r]._dataType == ArtifactRequest::DataType::Raw) {
					for (const auto&prod:_productsFile._compileProducts)
						if (prod._type == requests[r]._chunkTypeCode) {
							size_t size = 0;
							auto fileData = TryLoadFileAsAlignedBuffer(*_filesystem, prod._intermediateArtifact, size);
							if (requests[r]._dataType == ArtifactRequest::DataType::BlockSerializer)
								Block_Initialize(fileData.get());
							result[r] = {std::move(fileData), size, nullptr, nullptr};
							foundExactMatch = true;
							break;
						}
				} else if (requests[r]._dataType == ArtifactRequest::DataType::ReopenFunction) {
					for (const auto&prod:_productsFile._compileProducts)
						if (prod._type == requests[r]._chunkTypeCode) {
							result[r]._reopenFunction = [fs=_filesystem, fn=prod._intermediateArtifact]() -> std::shared_ptr<IFileInterface> {
								return OpenFileInterface(*fs, fn, "rb", 0);
							};
							foundExactMatch = true;
							break;
						}
				} else {
					assert(0);
				}

				if (!foundExactMatch) {
					requestsForMultiMapping.push_back((unsigned)r);
					requestsForMulti.push_back(requests[r]);
				}
			}

			// look for the main chunk file in the compile products -- we'll use this for resolving the remaining requests
			if (!requestsForMulti.empty()) {
				for (const auto&prod:_productsFile._compileProducts)
					if (prod._type == ChunkType_Multi) {
						// open with no sharing
						auto mainChunkFile = OpenFileInterface(*_filesystem, prod._intermediateArtifact, "rb", 0);
						ChunkFileContainer temp(prod._intermediateArtifact, _depVal);
						auto fromMulti = temp.ResolveRequests(*mainChunkFile, MakeIteratorRange(requestsForMulti));
						for (size_t c=0; c<fromMulti.size(); ++c)
							result[requestsForMultiMapping[c]] = std::move(fromMulti[c]);
						break;
					}
			}
				
			return result;
		}

		DependencyValidation GetDependencyValidation() const override { return _depVal; }
		StringSection<ResChar> GetRequestParameters() const override { return {}; }
		AssetState GetAssetState() const override { return _productsFile._state; }
		CompileProductsArtifactCollection(
			const CompileProductsFile& productsFile,
			std::shared_ptr<IFileSystem> fs,
			const DependencyValidation& depVal,
			const std::shared_ptr<StoreReferenceCounts>& refCounts,
			uint64_t refCountHashCode)
		: _productsFile(productsFile), _depVal(depVal)
		, _refCounts(refCounts), _refCountHashCode(refCountHashCode)
		, _filesystem(std::move(fs))
		{
			ScopedLock(_refCounts->_lock);
			auto read = LowerBound(_refCounts->_readReferenceCount, _refCountHashCode);
			if (read != _refCounts->_readReferenceCount.end() && read->first == _refCountHashCode) {
				++read->second;
			} else
				_refCounts->_readReferenceCount.insert(read, std::make_pair(_refCountHashCode, 1));
		}

		~CompileProductsArtifactCollection() 
		{
			ScopedLock(_refCounts->_lock);
			auto read = LowerBound(_refCounts->_readReferenceCount, _refCountHashCode);
			if (read != _refCounts->_readReferenceCount.end() && read->first == _refCountHashCode) {
				assert(read->second > 0);
				--read->second;
			} else {
				Log(Error) << "Missing _readReferenceCount marker during cleanup op in RetrieveCompileProducts" << std::endl;
			}
		}

		CompileProductsArtifactCollection(const CompileProductsArtifactCollection&) = delete;
		CompileProductsArtifactCollection& operator=(const CompileProductsArtifactCollection&) = delete;
	private:
		CompileProductsFile _productsFile;
		DependencyValidation _depVal;
		std::shared_ptr<StoreReferenceCounts> _refCounts;
		uint64_t _refCountHashCode;
		std::shared_ptr<IFileSystem> _filesystem;
	};

	static std::shared_ptr<IArtifactCollection> MakeArtifactCollection(
		const CompileProductsFile& productsFile, 
		std::shared_ptr<IFileSystem> fs,
		const DependencyValidation& depVal,
		const std::shared_ptr<StoreReferenceCounts>& refCounts,
		uint64_t refCountHashCode)
	{
		return std::make_shared<CompileProductsArtifactCollection>(productsFile, std::move(fs), depVal, refCounts, refCountHashCode);
	}

}

