// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SampleRig.h"
#include "../../RenderCore/Techniques/Apparatuses.h"
#include "../../RenderCore/IDevice.h"
#include "../../RenderCore/IAnnotator.h"
#include "../../PlatformRig/PlatformApparatuses.h"
#include "../../PlatformRig/DebugScreenRegistry.h"
#include "../../PlatformRig/DebuggingDisplays/PipelineAcceleratorDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/DeformAcceleratorDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/VulkanMemoryDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/VulkanInternalPoolsDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/BufferUploadDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/InvalidAssetDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/GPUProfileDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/CPUProfileDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/InvalidAssetDisplay.h"
#include "../../PlatformRig/DebuggingDisplays/DisplaySettingsDisplay.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Utility/Profiling/CPUProfiler.h"

namespace Sample
{
	void InstallDefaultDebuggingDisplays(SampleGlobals& globals)
	{
		globals._displayRegistrations.emplace_back(
			"PipelineAccelerators", 
			PlatformRig::Overlays::CreatePipelineAcceleratorPoolDisplay(globals._drawingApparatus->_pipelineAccelerators));

		globals._displayRegistrations.emplace_back(
			"DeformAccelerators",
			PlatformRig::Overlays::CreateDeformAcceleratorPoolDisplay(globals._drawingApparatus->_deformAccelerators));

		globals._displayRegistrations.emplace_back(
			"Vulkan Memory Allocator",
			PlatformRig::Overlays::CreateVulkanMemoryAllocatorDisplay(globals._drawingApparatus->_device));

		globals._displayRegistrations.emplace_back(
			"[Profiler] Buffer uploads",
			std::make_shared<PlatformRig::Overlays::BufferUploadDisplay>(globals._primaryResourcesApparatus->_bufferUploads.get()));

		if (auto vulkanMemoryAllocatorDisplay = PlatformRig::Overlays::CreateVulkanMemoryAllocatorDisplay(globals._drawingApparatus->_device))
			globals._displayRegistrations.emplace_back("Vulkan Memory Allocator", vulkanMemoryAllocatorDisplay);

		if (auto vulkanInternalPoolsDisplay = PlatformRig::Overlays::CreateVulkanInternalPoolsDisplay(globals._renderDevice))
			globals._displayRegistrations.emplace_back("Vulkan Internal Pools", vulkanInternalPoolsDisplay);

		if (auto* annotator = &globals._windowApparatus->_immediateContext->GetAnnotator()) {
			globals._displayRegistrations.emplace_back(
				"[Profiler] GPU Profiler",
				std::make_shared<PlatformRig::Overlays::GPUProfileDisplay>(*annotator));
		}

		globals._displayRegistrations.emplace_back(
			"[Profiler] CPU Profiler",
			std::make_shared<PlatformRig::Overlays::HierarchicalProfilerDisplay>(globals._frameRenderingApparatus->_frameCPUProfiler.get()));

		if (auto assetSets = ::Assets::Services::GetAssetSetsPtr())
			globals._displayRegistrations.emplace_back(
				"[Console] Invalid asset display",
				PlatformRig::Overlays::CreateInvalidAssetDisplay(assetSets));

		globals._displayRegistrations.emplace_back(
			"Display Settings",
			PlatformRig::Overlays::CreateDisplaySettingsDisplay(globals._windowApparatus->_displaySettings, globals._windowApparatus->_osWindow));
	}

}

