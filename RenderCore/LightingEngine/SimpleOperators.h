// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Format.h"
#include <memory>

namespace RenderCore { class IResourceView; class FrameBufferProperties; }
namespace RenderCore { namespace Techniques { class FragmentStitchingContext; class IComputeShaderOperator; class IShaderOperator; class PipelineCollection; class ParsingContext; struct FrameBufferTarget; class IShaderResourceDelegate; }}
namespace Assets { class DependencyValidation; }
namespace std { template<typename T> class promise; }

namespace RenderCore { namespace LightingEngine 
{
	class RenderStepFragmentInterface;

	struct RefractionBufferOperatorDesc
	{
		Format _format = Format::R11G11B10_FLOAT;
		Format _depthFormat = Format::Unknown; // Leave unknown to disable depth buffer copy, otherwise set to something like Format::R16_UNORM
	};

	class RefractionBufferOperator : public std::enable_shared_from_this<RefractionBufferOperator>
	{
	public:
		void SecondStageConstruction(
			std::promise<std::shared_ptr<RefractionBufferOperator>>&& promise,
			const Techniques::FrameBufferTarget& fbTarget);
		void PreregisterAttachments(Techniques::FragmentStitchingContext& stitchingContext, const FrameBufferProperties& fbProps);
		RenderStepFragmentInterface CreateFragment(const FrameBufferProperties& fbProps);

		std::shared_ptr<Techniques::IShaderResourceDelegate> CreateShaderResourceDelegate();
		::Assets::DependencyValidation GetDependencyValidation() const;

		RefractionBufferOperator(
			std::shared_ptr<Techniques::PipelineCollection> pipelinePool,
			const RefractionBufferOperatorDesc& desc);
		~RefractionBufferOperator();
	private:
		std::shared_ptr<Techniques::IComputeShaderOperator> _shader;
		std::shared_ptr<Techniques::PipelineCollection> _pool;
		unsigned _secondStageConstructionState = 0;		// debug usage only
		RefractionBufferOperatorDesc _desc;
	};

}}

