// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SingleWindowAppRig.h"
#include "PlatformApparatuses.h"
#include "DebugScreenRegistry.h"
#include "DebuggingDisplays/PipelineAcceleratorDisplay.h"
#include "DebuggingDisplays/DeformAcceleratorDisplay.h"
#include "DebuggingDisplays/VulkanMemoryDisplay.h"
#include "DebuggingDisplays/VulkanInternalPoolsDisplay.h"
#include "DebuggingDisplays/BufferUploadDisplay.h"
#include "DebuggingDisplays/InvalidAssetDisplay.h"
#include "DebuggingDisplays/GPUProfileDisplay.h"
#include "DebuggingDisplays/CPUProfileDisplay.h"
#include "DebuggingDisplays/InvalidAssetDisplay.h"
#include "DebuggingDisplays/DisplaySettingsDisplay.h"
#include "DebuggingDisplays/HelpDisplay.h"
#include "../RenderCore/Techniques/Apparatuses.h"
#include "../RenderCore/IDevice.h"
#include "../RenderCore/IAnnotator.h"
#include "../Assets/AssetServices.h"
#include "../Assets/AssetSetManager.h"
#include "../Utility/Profiling/CPUProfiler.h"

namespace PlatformRig
{
	void InstallDefaultDebuggingDisplays(AppRigGlobals& globals)
	{
		globals._displayRegistrations.emplace_back(
			"PipelineAccelerators", 
			Overlays::CreatePipelineAcceleratorPoolDisplay(globals._drawingApparatus->_pipelineAccelerators));

		globals._displayRegistrations.emplace_back(
			"DeformAccelerators",
			Overlays::CreateDeformAcceleratorPoolDisplay(globals._drawingApparatus->_deformAccelerators));

		globals._displayRegistrations.emplace_back(
			"Vulkan Memory Allocator",
			Overlays::CreateVulkanMemoryAllocatorDisplay(globals._drawingApparatus->_device));

		globals._displayRegistrations.emplace_back(
			"[Profiler] Buffer uploads",
			std::make_shared<Overlays::BufferUploadDisplay>(globals._primaryResourcesApparatus->_bufferUploads.get()));

		if (auto vulkanMemoryAllocatorDisplay = Overlays::CreateVulkanMemoryAllocatorDisplay(globals._drawingApparatus->_device))
			globals._displayRegistrations.emplace_back("Vulkan Memory Allocator", vulkanMemoryAllocatorDisplay);

		if (auto vulkanInternalPoolsDisplay = Overlays::CreateVulkanInternalPoolsDisplay(globals._renderDevice))
			globals._displayRegistrations.emplace_back("Vulkan Internal Pools", vulkanInternalPoolsDisplay);

		if (auto* annotator = &globals._windowApparatus->_immediateContext->GetAnnotator()) {
			globals._displayRegistrations.emplace_back(
				"[Profiler] GPU Profiler",
				Overlays::CreateGPUProfilerDisplay(*annotator));
		}

		globals._displayRegistrations.emplace_back(
			"[Profiler] CPU Profiler",
			Overlays::CreateHierarchicalProfilerDisplay(*globals._frameRenderingApparatus->_frameCPUProfiler));

		if (auto assetSets = ::Assets::Services::GetAssetSetsPtr())
			globals._displayRegistrations.emplace_back(
				"[Console] Invalid asset display",
				Overlays::CreateInvalidAssetDisplay(assetSets));

		globals._displayRegistrations.emplace_back(
			"Display Settings",
			Overlays::CreateDisplaySettingsDisplay(globals._windowApparatus->_displaySettings, globals._windowApparatus->_osWindow));

		globals._displayRegistrations.emplace_back(
			"Loading Context",
			std::make_shared<Overlays::OperationContextDisplay>(globals._windowApparatus->_mainLoadingContext));

	}

}

