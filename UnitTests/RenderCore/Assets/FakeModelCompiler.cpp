// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "FakeModelCompiler.h"
#include "../../../RenderCore/Assets/ModelScaffold.h"
#include "../../../RenderCore/Assets/RawMaterial.h"
#include "../../../RenderCore/GeoProc/NascentCommandStream.h"
#include "../../../RenderCore/GeoProc/NascentAnimController.h"
#include "../../../RenderCore/GeoProc/NascentObjectsSerialize.h"
#include "../../../RenderCore/GeoProc/NascentModel.h"
#include "../../../RenderCore/GeoProc/MeshDatabase.h"
#include "../../../RenderCore/StateDesc.h"
#include "../../../ConsoleRig/GlobalServices.h"			// for ConsoleRig::GetLibVersionDesc()
#include "../../../OSServices/AttachableLibrary.h"
#include "../../../Utility/Streams/StreamTypes.h"
#include "../../../Utility/Streams/StreamFormatter.h"
#include "../../../Utility/Streams/OutputStreamFormatter.h"

namespace UnitTests
{
	static const uint64 Type_Model = ConstHash64<'Mode', 'l'>::Value;
	static const uint64 Type_RawMat = ConstHash64<'RawM', 'at'>::Value;
	static const uint64 Type_AnimationSet = ConstHash64<'Anim', 'Set'>::Value;
	static const uint64 Type_Skeleton = ConstHash64<'Skel', 'eton'>::Value;

	class FakeModelCompileOperation : public ::Assets::ICompileOperation
	{
	public:
		std::vector<::Assets::DependentFileState> GetDependencies() const override { return _dependencies; }
		std::vector<TargetDesc> GetTargets() const override { return _targets; }
		std::vector<SerializedArtifact> SerializeTarget(unsigned idx) override
		{
			if (idx >= _targets.size())
				return {};

			if (_targets[idx]._targetCode == Type_Model)
				return SerializeModel();
			else if (_targets[idx]._targetCode == Type_RawMat)
				return SerializeRawMat();
			else if (_targets[idx]._targetCode == Type_Skeleton)
				return SerializeSkeleton();

			return {};
		}
		
		FakeModelCompileOperation()
		{
			// Normally we'd open the input file at this point and figure out
			// what it contains and what we can compile it into. There's no input
			// for this compile operation, though, so everything's just static

			// We are generally able to extract multiple different types of
			// asset information from a single compile of an input model file
			// That is we can get model data, as well as skeleton and material data
			// Some formats (eg, collada, fbx) will store different types of asset data
			// within the same container format. Ie, some files of this format might 
			// have model data, other might have animation.
			// This is why the ICompileOperation returns a list of the different 
			// "target" data types for this specific operation.
			_targets = {
				TargetDesc { Type_Model, "Model" },
				TargetDesc { Type_RawMat, "RawMat" },
				TargetDesc { Type_Skeleton, "Skeleton" }
			};

			_modelName = "fake-model";
		}
		~FakeModelCompileOperation()
		{}
	private:
		std::vector<::Assets::DependentFileState> _dependencies;
		std::vector<TargetDesc> _targets;
		std::string _modelName;

		std::vector<SerializedArtifact> SerializeModel();
		std::vector<SerializedArtifact> SerializeRawMat();
		std::vector<SerializedArtifact> SerializeSkeleton();
	};

	///////////////////////////////////////////////////////////////////////////////////////////////
		//   M A T E R I A L   T A B L E   //
	///////////////////////////////////////////////////////////////////////////////////////////////

	auto FakeModelCompileOperation::SerializeRawMat() -> std::vector<SerializedArtifact>
	{
		MemoryOutputStream<char> strm;

		{
			OutputStreamFormatter formatter(strm);

			RenderCore::Assets::RawMaterial material0;
			material0._uniforms = {
				std::make_pair("Emissive", "{.5, .5, .5}c"),
				std::make_pair("Brightness", "50")
			};
			RenderCore::Assets::RawMaterial material1;
			material1._uniforms = {
				std::make_pair("Emissive", "{2.5, .25, .15}c"),
				std::make_pair("Brightness", "33")
			};

			auto matContainer = formatter.BeginKeyedElement("Material0");
			formatter << material0;
			formatter.EndElement(matContainer);

			matContainer = formatter.BeginKeyedElement("Material1");
			formatter << material1;
			formatter.EndElement(matContainer);
		}

		return {
			::Assets::ICompileOperation::SerializedArtifact{
				Type_RawMat, 0, _modelName,
				::Assets::AsBlob(MakeIteratorRange(strm.GetBuffer().Begin(), strm.GetBuffer().End()))}
		};
	}

	///////////////////////////////////////////////////////////////////////////////////////////////
		//   S K E L E T O N   //
	///////////////////////////////////////////////////////////////////////////////////////////////

	static RenderCore::Assets::GeoProc::NascentSkeleton GenerateNascentSkeleton()
	{
		using NascentSkeleton = RenderCore::Assets::GeoProc::NascentSkeleton;
		NascentSkeleton result;
		result.WriteOutputMarker("", "identity");

		// Using Push & Pop, we can generate a hierarchy of nodes
		// transformations can either be static, or be animatable
		// For animatable transformations, we must give the animatable parameters names
		//		those parameters can either be scale, translation, rotation parts (in various forms)
		//		or even a full matrix

		result.WritePushLocalToWorld();
			NascentSkeleton::Transform transform;
			transform._rotationAsQuaternion = MakeRotationQuaternion({0.f, 1.0f, 0.f}, 45.f * gPI / 180.f);
			transform._uniformScale = 1.f;
			transform._translation = {5.0f, 0.f, 2.0f};
			result.WriteParameterizedTransform("AnimatableTransform", transform);
			result.WriteOutputMarker("", "RootNode");

			result.WritePushLocalToWorld();
				ScaleRotationTranslationQ staticTransform{ Float3{1,1,1}, MakeRotationQuaternion({0.f, 0.0f, 1.f}, -33.f * gPI / 180.f), Float3{-3.f, -2.f, 1.f} };
				result.WriteStaticTransform(AsFloat4x4(staticTransform));
				result.WriteOutputMarker("", "InternalNode");
			result.WritePopLocalToWorld();
		result.WritePopLocalToWorld();

		return result;
	}

	auto FakeModelCompileOperation::SerializeSkeleton() -> std::vector<SerializedArtifact>
	{
		auto nascentSkeleton = GenerateNascentSkeleton();

		RenderCore::Assets::TransformationMachineOptimizer_Null optimizer;
		nascentSkeleton.GetSkeletonMachine().Optimize(optimizer);

		return RenderCore::Assets::GeoProc::SerializeSkeletonToChunks("skeleton", nascentSkeleton);
	}

	///////////////////////////////////////////////////////////////////////////////////////////////
		//   M O D E L   D A T A   //
	///////////////////////////////////////////////////////////////////////////////////////////////

	static RenderCore::Assets::GeoProc::NascentModel GenerateModel()
	{
		namespace GeoProc = RenderCore::Assets::GeoProc;
		GeoProc::NascentModel result;

		Float3 vertexPositions[] {
			Float3 { -1.f, -1.f, -1.f },
			Float3 {  1.f, -1.f, -1.f },
			Float3 { -1.f,  1.f, -1.f },
			Float3 {  1.f,  1.f, -1.f },
			Float3 { -1.f, -1.f,  1.f },
			Float3 {  1.f, -1.f,  1.f },
			Float3 { -1.f,  1.f,  1.f },
			Float3 {  1.f,  1.f,  1.f }
		};
		unsigned indices[] {
			2, 0, 3,
			3, 0, 1,

			1, 5, 3,
			3, 5, 7,

			6, 4, 2,
			2, 4, 0,

			0, 4, 1,
			1, 4, 5,

			2, 3, 6,
			6, 3, 7,

			7, 5, 6,
			5, 6, 4
		};

		GeoProc::NascentModel::GeometryBlock geoBlock;
		geoBlock._mesh = std::make_shared<GeoProc::MeshDatabase>();
		geoBlock._mesh->AddStream(
			GeoProc::CreateRawDataSource(
				vertexPositions, PtrAdd(vertexPositions, sizeof(vertexPositions)), 
				RenderCore::Format::R32G32B32_FLOAT),
			{},
			"POSITION", 0);
		geoBlock._drawCalls.push_back( GeoProc::NascentModel::DrawCallDesc { 0, dimof(indices), RenderCore::Topology::TriangleList } );
		geoBlock._indices = std::vector<uint8_t>{ (uint8_t*)indices, (uint8_t*)PtrAdd(indices, sizeof(indices)) };
		geoBlock._indexFormat = RenderCore::Format::R32_UINT;
		geoBlock._geoSpaceToNodeSpace = Identity<Float4x4>();

		auto geoBlockGuid = result.Add(std::move(geoBlock));

		GeoProc::NascentModel::Command cmd;
		cmd._geometryBlock = geoBlockGuid;
		cmd._localToModel = "InternalNode";
		cmd._materialBindingSymbols = { "Material0", "Material1" };
		result.Add(std::move(cmd));

		return result;
	}
	
	auto FakeModelCompileOperation::SerializeModel() -> std::vector<SerializedArtifact>
	{
		auto model = GenerateModel();
		auto embeddedSkeleton = GenerateNascentSkeleton();
		RenderCore::Assets::GeoProc::OptimizeSkeleton(embeddedSkeleton, model);
		RenderCore::Assets::GeoProc::NativeVBSettings nativeVBSettings { true };
		return model.SerializeToChunks("skin", embeddedSkeleton, nativeVBSettings);
	}

	///////////////////////////////////////////////////////////////////////////////////////////////
		//   E T C   //
	///////////////////////////////////////////////////////////////////////////////////////////////

	static std::shared_ptr<::Assets::ICompileOperation> BeginFakeModelCompilation(
		const ::Assets::InitializerPack&)
	{
		return std::make_shared<FakeModelCompileOperation>();
	}

	::Assets::CompilerRegistration RegisterFakeModelCompiler(
		::Assets::IIntermediateCompilers& intermediateCompilers)
	{
		::Assets::CompilerRegistration result{
			intermediateCompilers,
			"fake-model-scaffold-compiler",
			"fake-model-scaffold-compiler",
			ConsoleRig::GetLibVersionDesc(),
			{},
			BeginFakeModelCompilation};

		uint64_t outputAssetTypes[] = { 
			Type_Model,
			Type_RawMat,
			Type_AnimationSet,
			Type_Skeleton,
		};
		intermediateCompilers.AssociateRequest(
			result.RegistrationId(),
			MakeIteratorRange(outputAssetTypes),
			"fake-model");
		return result;
	}   
}