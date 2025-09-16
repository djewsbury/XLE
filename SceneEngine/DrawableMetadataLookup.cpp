// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DrawableMetadataLookup.h"
#include "../RenderCore/Techniques/DrawableConstructor.h"
#include "../RenderCore/Assets/ModelMachine.h"
#include "../RenderCore/Assets/ModelRendererConstruction.h"
#include "../RenderCore/Assets/CompiledMaterialSet.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Core/Exceptions.h"
#include <any>

using namespace Utility::Literals;

namespace SceneEngine
{
	void LightWeightMetadataLookup::SingleInstance(
		DrawableMetadataLookupContext& lookupContext,
		RenderCore::Techniques::DrawableConstructor& constructor,
		const std::shared_ptr<RenderCore::Assets::ModelRendererConstruction>& rendererConstruction)
	{
		using namespace RenderCore;
		assert(!constructor._cmdStreams.empty());
		auto& cmdStream = constructor._cmdStreams.front();		// first is always the default

		assert(lookupContext.PktIndex() < dimof(cmdStream._drawCallCounts));
		if (lookupContext.NextIndex() >= cmdStream._drawCallCounts[lookupContext.PktIndex()]) {
			lookupContext.AdvanceIndexOffset(cmdStream._drawCallCounts[lookupContext.PktIndex()]);
			return;
		}

		using DrawableConstructor = RenderCore::Techniques::DrawableConstructor;

		const Float4x4* geoSpaceToNodeSpace = nullptr;
		IteratorRange<const uint64_t*> materialGuids;
		unsigned materialGuidsIterator = 0;
		unsigned transformMarker = ~0u;
		unsigned elementIdx = ~0u;
		unsigned drawCallCounter = 0;
		for (auto cmd:cmdStream.GetCmdStream()) {
			switch (cmd.Cmd()) {
			case (uint32_t)RenderCore::Assets::ModelCommand::SetTransformMarker:
				transformMarker = cmd.As<unsigned>();
				break;
			case (uint32_t)RenderCore::Assets::ModelCommand::SetMaterialAssignments:
				materialGuids = cmd.RawData().Cast<const uint64_t*>();
				materialGuidsIterator = 0;
				break;
			case (uint32_t)DrawableConstructor::Command::BeginElement:
				elementIdx = cmd.As<unsigned>();
				break;
			case (uint32_t)DrawableConstructor::Command::SetGeoSpaceToNodeSpace:
				geoSpaceToNodeSpace = (!cmd.RawData().empty()) ? &cmd.As<Float4x4>() : nullptr;
				break;
			case (uint32_t)DrawableConstructor::Command::ExecuteDrawCalls:
				{
					struct DrawCallsRef { unsigned _start, _end; };
					auto& drawCallsRef = cmd.As<DrawCallsRef>();

					for (const auto& dc:MakeIteratorRange(cmdStream._drawCalls.begin()+drawCallsRef._start, cmdStream._drawCalls.begin()+drawCallsRef._end)) {
						if (dc._batchFilter != lookupContext.PktIndex()) continue;
						if (lookupContext.Finished()) break;
						if (drawCallCounter == lookupContext.NextIndex()) {
							lookupContext.SetProviderForNextIndex(
								[drawCallIndex=unsigned(&dc-AsPointer(cmdStream._drawCalls.begin()+drawCallsRef._start)), drawCallCount=unsigned(drawCallsRef._end-drawCallsRef._start), matGuid = materialGuids[materialGuidsIterator], dc, elementIdx, rendererConstruction=std::weak_ptr<RenderCore::Assets::ModelRendererConstruction>{rendererConstruction}]
								(uint64_t semantic) -> std::any
								{
									switch(semantic) {
									case "DrawCallIndex"_h: return drawCallIndex;	// within the geo
									case "DrawCallCount"_h: return drawCallCount;	// within the geo
									case "MaterialGuid"_h: return matGuid;
									case "IndexCount"_h: return dc._indexCount;
									case "ElementIndex"_h: return elementIdx;
									case "MaterialName"_h: 
										if (auto l = rendererConstruction.lock())
											return l->GetElement(elementIdx)->GetMaterials()->DehashMaterialName(matGuid).AsString();
										return {};
									case "ShortMaterialName"_h: 
										if (auto l = rendererConstruction.lock())
											return MakeFileNameSplitter(l->GetElement(elementIdx)->GetMaterials()->DehashMaterialName(matGuid)).Parameters().AsString();
										return {};
									case "MaterialSet"_h:
										if (auto l = rendererConstruction.lock())
											return l->GetElement(elementIdx)->GetMaterialSetName();
										return {};
									case "ModelScaffold"_h:
										if (auto l = rendererConstruction.lock())
											return l->GetElement(elementIdx)->GetModelScaffoldName();
										return {};
									default: return {};
									}
								});
						}
						materialGuidsIterator++;
						++drawCallCounter;
					}
				}
				break;
			}
		}

		if (!lookupContext.Finished())
			lookupContext.AdvanceIndexOffset(cmdStream._drawCallCounts[lookupContext.PktIndex()]);
	}
}

