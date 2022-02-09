// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AnimationVisualization.h"
#include "OverlayContext.h"
#include "../RenderCore/Assets/SkeletonMachine.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/CommonBindings.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ImmediateDrawables.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/Types.h"
#include "../Assets/Assets.h"
#include "../ConsoleRig/ResourceBox.h"

namespace RenderOverlays
{
	using namespace RenderCore;

	class Vertex_PC
	{
	public:
		Float3      _position;
		unsigned    _color;
	};

	static MiniInputElementDesc s_vertexInputLayout[] = {
        MiniInputElementDesc{ Techniques::CommonSemantics::POSITION, Format::R32G32B32_FLOAT },
        MiniInputElementDesc{ Techniques::CommonSemantics::COLOR, Format::R8G8B8A8_UNORM }
    };

	class SkeletonPreviewResourceBox
	{
	public:
		std::shared_ptr<Font> _font;

		SkeletonPreviewResourceBox(
            std::shared_ptr<RenderOverlays::Font> font)
        : _font(std::move(font))
        {}

        static void ConstructToPromise(std::promise<std::shared_ptr<SkeletonPreviewResourceBox>>&& promise)
        {
            ::Assets::WhenAll(
                RenderOverlays::MakeFont("Vera", 12)).ThenConstructToPromise(std::move(promise));
        }
	};

	void    RenderSkeleton(
        IOverlayContext& overlayContext, 
		RenderCore::Techniques::ParsingContext& parserContext,
		const RenderCore::Assets::SkeletonMachine& skeleton,
		IteratorRange<const Float4x4*> outputMatrices,
		const Float4x4& localToWorld, 
		bool drawBoneNames)
    {
		auto outputMatrixCount = skeleton.GetOutputMatrixCount();
        auto vertexCount = outputMatrixCount * 2 * 2 * 3;
		Techniques::ImmediateDrawableMaterial material;
		material._stateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::WriteMask;
		material._stateSet._writeMask = 0;		// disable depth read & write
		auto workingVertices = overlayContext.GetImmediateDrawables().QueueDraw(
			vertexCount, MakeIteratorRange(s_vertexInputLayout), 
			material, Topology::TriangleList).Cast<Vertex_PC*>();
		size_t workingVertexIterator = 0;

		// Draw a sprite for each output matrix location
		auto cameraRight = Normalize(ExtractRight_Cam(parserContext.GetProjectionDesc()._cameraToWorld));
		auto cameraUp = Normalize(ExtractUp_Cam(parserContext.GetProjectionDesc()._cameraToWorld));
		auto cameraForward = Normalize(ExtractForward_Cam(parserContext.GetProjectionDesc()._cameraToWorld));
		auto worldToProjection = parserContext.GetProjectionDesc()._worldToProjection;
		auto s = XlTan(.5f * parserContext.GetProjectionDesc()._verticalFov);
		for (const auto&o:outputMatrices) {
			auto position = TransformPoint(localToWorld, ExtractTranslation(o));
			float scale = 0.01f * (worldToProjection * Expand(position, 1.0f))[3] * s;	// normalize scale for screen space

			Float3 corners[] = {
				position - scale * cameraUp - scale * cameraRight,
				position - scale * cameraUp + scale * cameraRight,
				position + scale * cameraUp - scale * cameraRight,
				position + scale * cameraUp + scale * cameraRight
			};

			workingVertices[workingVertexIterator++] = {corners[0], 0xff00af00};
			workingVertices[workingVertexIterator++] = {corners[1], 0xff00af00};
			workingVertices[workingVertexIterator++] = {corners[2], 0xff00af00};
			workingVertices[workingVertexIterator++] = {corners[2], 0xff00af00};
			workingVertices[workingVertexIterator++] = {corners[1], 0xff00af00};
			workingVertices[workingVertexIterator++] = {corners[3], 0xff00af00};
		}

		std::vector<uint32_t> parents(outputMatrixCount);
		skeleton.CalculateParentPointers(MakeIteratorRange(parents));
		for (unsigned c=0; c<parents.size(); ++c) {
			if (parents[c] >= outputMatrices.size()) continue;

			auto childPosition = TransformPoint(localToWorld, ExtractTranslation(outputMatrices[c]));
			auto parentPosition = TransformPoint(localToWorld, ExtractTranslation(outputMatrices[parents[c]]));

			auto axis = parentPosition - childPosition;
			Float3 tangent;
			if (!Normalize_Checked<Float3>(&tangent, Cross(cameraForward, axis)))
				continue;

			float scaleC = 0.33f * 0.01f * (worldToProjection * Expand(childPosition, 1.0f))[3] * s;	// normalize scale for screen space
			float scaleP = 0.33f * 0.01f * (worldToProjection * Expand(parentPosition, 1.0f))[3] * s;	// normalize scale for screen space

			Float3 corners[] = {
				childPosition - scaleC * tangent,
				childPosition + scaleC * tangent,
				parentPosition - scaleP * tangent,
				parentPosition + scaleP * tangent
			};

			workingVertices[workingVertexIterator++] = {corners[0], 0xff00af00};
			workingVertices[workingVertexIterator++] = {corners[1], 0xff00af00};
			workingVertices[workingVertexIterator++] = {corners[2], 0xff00af00};
			workingVertices[workingVertexIterator++] = {corners[2], 0xff00af00};
			workingVertices[workingVertexIterator++] = {corners[1], 0xff00af00};
			workingVertices[workingVertexIterator++] = {corners[3], 0xff00af00};
		}

		assert(workingVertexIterator <= vertexCount); 

		auto* box = ConsoleRig::TryActualizeCachedBox<SkeletonPreviewResourceBox>();
		if (drawBoneNames && box) {
			auto viewport = parserContext.GetViewport();
			auto outputMatrixNames = skeleton.GetOutputMatrixNames();

			std::vector<Float2> screenspacePositions;

			for (unsigned idx=0; idx<outputMatrices.size(); ++idx) {
				auto o = outputMatrices[idx];
				auto hposition = worldToProjection * Expand(TransformPoint(localToWorld, ExtractTranslation(o)), 1.0f);
				if (	hposition[0] < -hposition[3] || hposition[0] > hposition[3]
					||	hposition[1] < -hposition[3] || hposition[1] > hposition[3]
					||	hposition[2] < -hposition[3] || hposition[2] > hposition[3])
					continue;

				Float2 viewportSpace { hposition[0] / hposition[3], hposition[1] / hposition[3] };
				Float2 screenSpace {
					(viewportSpace[0] *  0.5f + 0.5f) * viewport._width + viewport._x,
					(viewportSpace[1] *  0.5f + 0.5f) * viewport._height + viewport._y };

				for (;;) {
					bool foundOverlap = false;
					for (auto f:screenspacePositions)
						foundOverlap |= MagnitudeSquared(f - screenSpace) < 8.f * 8.f;
					if (!foundOverlap) break;
					screenSpace[1] += 10.0f;
				}
				screenspacePositions.push_back(screenSpace);

				std::string name;
				if (idx < outputMatrixNames.size()) {
					name = outputMatrixNames[idx].AsString();
				} else {
					name = "Unnamed: " + std::to_string(idx);
				}
				overlayContext.DrawText(
					std::make_tuple(
						Float3(screenSpace[0] - 100.f, screenSpace[1] - 20.f, 0.f), 
						Float3(screenSpace[0] + 100.f, screenSpace[1] + 20.f, 0.f)),
					*box->_font, RenderOverlays::DrawTextFlags::Shadow, RenderOverlays::ColorB{0xffffffff}, RenderOverlays::TextAlignment::Center, 
					MakeStringSection(name));
			}
		}
    }

	void    RenderSkeleton(
        IOverlayContext& context, 
        Techniques::ParsingContext& parserContext, 
		const RenderCore::Assets::SkeletonMachine& skeleton,
		const Float4x4& localToWorld,
		bool drawBoneNames)
	{
		auto outputMatrixCount = skeleton.GetOutputMatrixCount();
		std::vector<Float4x4> outputMatrices(outputMatrixCount);
		skeleton.GenerateOutputTransforms(MakeIteratorRange(outputMatrices));
		RenderSkeleton(context, parserContext, skeleton, MakeIteratorRange(outputMatrices), localToWorld, drawBoneNames);
	}
}
