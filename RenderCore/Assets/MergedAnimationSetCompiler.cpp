// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "MergedAnimationSetCompiler.h"
#include "ModelScaffold.h"
#include "RawAnimationCurve.h"
#include "../GeoProc/NascentCommandStream.h"
#include "../GeoProc/NascentObjectsSerialize.h"
#include "../../Assets/IntermediatesStore.h"
#include "../../Assets/Assets.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Assets/Assets.h"
#include "../../OSServices/AttachableLibrary.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Core/Prefix.h"
#include <regex>

namespace RenderCore { namespace Assets
{
///////////////////////////////////////////////////////////////////////////////////////////////////
	static const uint64_t Type_AnimationSet = ConstHash64<'Anim', 'Set'>::Value;
	static const auto ChunkType_Log = ConstHash64<'Log'>::Value;

	static void AddDep(
		std::vector<::Assets::DependentFileState>& deps,
		const ::Assets::DependentFileState& newDep)
	{
		auto existing = std::find_if(
			deps.cbegin(), deps.cend(),
			[&](const ::Assets::DependentFileState& test) { return test._filename == newDep._filename; });
		if (existing == deps.cend())
			deps.push_back(newDep);
	}
	
	static void AddDep(
		std::vector<::Assets::DependentFileState>& deps,
		StringSection<> newDep)
	{
			// we need to call "GetDependentFileState" first, because this can change the
			// format of the filename. String compares alone aren't working well for us here
		AddDep(deps, ::Assets::IntermediatesStore::GetDependentFileState(newDep));
	}

	static void MergeInAsManyAnimations(
		GeoProc::NascentAnimationSet& dst,
		const AnimationImmutableData& src,
		const std::string& namePrefix)
	{
		auto curveOffset = dst.GetCurves().size();
		for (auto& c:src._animationSet.GetCurves())
			dst.AddCurve(RawAnimationCurve{c});

		auto outputInterface = src._animationSet.GetOutputInterface();
		auto constantDriverOffset = dst.GetConstantDrivers().size();
		for (auto& constantDriver:src._animationSet.GetConstantDrivers()) {
			dst.AddConstantDriver(
				outputInterface[constantDriver._parameterIndex]._name,
				outputInterface[constantDriver._parameterIndex]._component,
				PtrAdd(src._animationSet.GetConstantData().begin(), constantDriver._dataOffset),
				BitsPerPixel(constantDriver._format) / 8,
				constantDriver._format, constantDriver._samplerType, constantDriver._samplerOffset);
		}

		auto animationDriverOffset = dst.GetAnimationDrivers().size();
		for (auto& animDriver:src._animationSet.GetAnimationDrivers()) {
			dst.AddAnimationDriver(
				outputInterface[animDriver._parameterIndex]._name,
				outputInterface[animDriver._parameterIndex]._component,
				animDriver._curveIndex + curveOffset,
				animDriver._samplerType, animDriver._samplerOffset);
		}

		for (auto& animation:src._animationSet.GetAnimations()) {
			auto newAnim = animation.second;
			if (newAnim._beginDriver != newAnim._endDriver) {
				newAnim._beginDriver += (unsigned)animationDriverOffset;
				newAnim._endDriver += (unsigned)animationDriverOffset;
			}
			if (newAnim._beginConstantDriver != newAnim._endConstantDriver) {
				newAnim._beginConstantDriver += (unsigned)constantDriverOffset;
				newAnim._endConstantDriver += (unsigned)constantDriverOffset;
			}
			auto name = src._animationSet.LookupStringName(animation.first).AsString();
			if (src._animationSet.GetAnimations().size() == 1) {
				name = namePrefix;
			} else
				name = namePrefix + name;
			dst.AddAnimation(
				name,
				newAnim._beginDriver, newAnim._endDriver,
				newAnim._beginConstantDriver, newAnim._endConstantDriver,
				newAnim._beginTime, newAnim._endTime);
		}
	}

	class MergedAnimSetCompileOperation : public ::Assets::ICompileOperation
	{
	public:
		std::vector<TargetDesc> GetTargets() const
		{
			if (_compilationException)
				return { 
					TargetDesc { Type_AnimationSet, "compilation-exception" }
				};
			if (_serializedArtifacts.empty()) return {};
			return {
				TargetDesc { Type_AnimationSet, _serializedArtifacts[0]._name.c_str() }
			};
		}
		std::vector<SerializedArtifact>	SerializeTarget(unsigned idx)
		{
			assert(idx == 0);
			if (_compilationException)
				std::rethrow_exception(_compilationException);
			return _serializedArtifacts;
		}
		std::vector<::Assets::DependentFileState> GetDependencies() const { return _dependencies; }

		MergedAnimSetCompileOperation(const ::Assets::InitializerPack& initializers)
		{
			TRY
			{
				auto baseFolderSrc = initializers.GetInitializer<std::string>(0);
				auto splitPath = MakeSplitPath(baseFolderSrc);
				if (splitPath.GetSectionCount() < 2 || !XlEqString(splitPath.GetSection(splitPath.GetSectionCount()-1), "*"))
					Throw(std::runtime_error("Expecting merged anim set request to end with '/*'"));
				auto baseFolder = MakeStringSection((const char*)AsPointer(baseFolderSrc.begin()), splitPath.GetSection(splitPath.GetSectionCount()-2).end()).AsString();

				auto walk = ::Assets::MainFileSystem::BeginWalk(baseFolder);
				std::vector<std::string> files;
				std::regex fileMatcher{R"(.*\.(([hH][kK][xX])|([dD][aA][eE])))"};
				for (auto w=walk.begin_files(); w!=walk.end_files(); ++w) {
					auto f = w.Desc()._mountedName;
					if (std::regex_match(f.begin(), f.end(), fileMatcher))
						files.push_back(f);
				}

				std::stringstream log;

				// merge all of the source files into a single output animation set
				RenderCore::Assets::GeoProc::NascentAnimationSet animSet;
				for (const auto& f:files) {
					TRY {
						auto part = ::Assets::ActualizeAssetPtr<AnimationSetScaffold>(f);
						MergeInAsManyAnimations(animSet, part->ImmutableData(), MakeFileNameSplitter(f).File().AsString());
					} CATCH(const ::Assets::Exceptions::ConstructionError& e) {
						// todo -- grab dep val
						log << "Failed to include animation (" << MakeFileNameSplitter(f).File() << ") in animation set because of exception: (" << e.what() << ")" << std::endl;
					} CATCH(const ::Assets::Exceptions::InvalidAsset& e) {
						// todo -- grab dep val
						log << "Failed to include animation (" << MakeFileNameSplitter(f).File() << ") in animation set because of exception: (" << e.what() << ")" << std::endl;
					} CATCH(const std::exception& e) {
						log << "Failed to include animation (" << MakeFileNameSplitter(f).File() << ") in animation set because of exception: (" << e.what() << ")" << std::endl;
					} CATCH_END
				}

				std::string finalName = splitPath.GetSection(splitPath.GetSectionCount()-2).AsString();
				_serializedArtifacts = GeoProc::SerializeAnimationsToChunks(finalName, animSet);

				auto logStr = log.str();
				if (!logStr.empty())
					_serializedArtifacts.push_back({ ChunkType_Log, 0, "log", ::Assets::AsBlob(std::move(logStr)) });

			} CATCH(...) {
				_compilationException = std::current_exception();
			} CATCH_END
		}

	private:
		std::vector<::Assets::DependentFileState> _dependencies;
		std::vector<SerializedArtifact> _serializedArtifacts;
		std::exception_ptr _compilationException;
	};

	::Assets::CompilerRegistration RegisterMergedAnimationSetCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers)
	{
		::Assets::CompilerRegistration result{
			intermediateCompilers,
			"merged-animset-compiler",
			"merged-animset-compiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			[](auto initializers) {
				return std::make_shared<MergedAnimSetCompileOperation>(initializers);
			}};

		uint64_t outputAssetTypes[] = { Type_AnimationSet };
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes),
			R"(.*[\\/]\*)");
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////
	
}}
