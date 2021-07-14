// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PipelineAccelerator.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"

namespace RenderCore { namespace Techniques
{
	class BoundUniformsPool
	{
	public:
		std::vector<std::pair<uint64_t, Metal::BoundUniforms>> _boundUniforms;

		Metal::BoundUniforms& Get(
			const Metal::GraphicsPipeline& pipeline,
			const UniformsStreamInterface& group0,
			const UniformsStreamInterface& group1);
		Metal::BoundUniforms& Get(
			const Metal::ComputePipeline& pipeline,
			const UniformsStreamInterface& group0,
			const UniformsStreamInterface& group1);
		Metal::BoundUniforms& Get(
			const Metal::GraphicsPipeline& pipeline,
			const UniformsStreamInterface& group0,
			const UniformsStreamInterface& group1,
			const UniformsStreamInterface& group2);
		Metal::BoundUniforms& Get(
			const Metal::ComputePipeline& pipeline,
			const UniformsStreamInterface& group0,
			const UniformsStreamInterface& group1,
			const UniformsStreamInterface& group2);
	};

	class IPipelineAcceleratorPool::Pipeline
	{
	public:
		std::shared_ptr<Metal::GraphicsPipeline> _metalPipeline;
		BoundUniformsPool _boundUniformsPool;
		
		::Assets::DependencyValidation _depVal;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		#if defined(_DEBUG)
			std::string _vsDescription, _psDescription, _gsDescription;
		#endif
	};

	inline Metal::BoundUniforms& BoundUniformsPool::Get(
		const Metal::GraphicsPipeline& pipeline,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1)
	{
		uint64_t hash = pipeline.GetInterfaceBindingGUID();
		hash = HashCombine(group0.GetHash(), hash);
		hash = HashCombine(group1.GetHash(), hash);

		auto i = LowerBound(_boundUniforms, hash);
		if (i == _boundUniforms.end() || i->first != hash) {
			Metal::BoundUniforms boundUniforms(pipeline, group0, group1);
			i = _boundUniforms.insert(i, std::make_pair(hash, std::move(boundUniforms)));
		}
			
		return i->second;
	}

	inline Metal::BoundUniforms& BoundUniformsPool::Get(
		const Metal::ComputePipeline& pipeline,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1)
	{
		uint64_t hash = pipeline.GetInterfaceBindingGUID();
		hash = HashCombine(group0.GetHash(), hash);
		hash = HashCombine(group1.GetHash(), hash);

		auto i = LowerBound(_boundUniforms, hash);
		if (i == _boundUniforms.end() || i->first != hash) {
			Metal::BoundUniforms boundUniforms(pipeline, group0, group1);
			i = _boundUniforms.insert(i, std::make_pair(hash, std::move(boundUniforms)));
		}
			
		return i->second;
	}

	inline Metal::BoundUniforms& BoundUniformsPool::Get(
		const Metal::GraphicsPipeline& pipeline,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1,
		const UniformsStreamInterface& group2)
	{
		uint64_t hash = pipeline.GetInterfaceBindingGUID();
		hash = HashCombine(group0.GetHash(), hash);
		hash = HashCombine(group1.GetHash(), hash);
		hash = HashCombine(group2.GetHash(), hash);

		auto i = LowerBound(_boundUniforms, hash);
		if (i == _boundUniforms.end() || i->first != hash) {
			Metal::BoundUniforms boundUniforms(pipeline, group0, group1, group2);
			i = _boundUniforms.insert(i, std::make_pair(hash, std::move(boundUniforms)));
		}
			
		return i->second;
	}

	inline Metal::BoundUniforms& BoundUniformsPool::Get(
		const Metal::ComputePipeline& pipeline,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1,
		const UniformsStreamInterface& group2)
	{
		uint64_t hash = pipeline.GetInterfaceBindingGUID();
		hash = HashCombine(group0.GetHash(), hash);
		hash = HashCombine(group1.GetHash(), hash);
		hash = HashCombine(group2.GetHash(), hash);

		auto i = LowerBound(_boundUniforms, hash);
		if (i == _boundUniforms.end() || i->first != hash) {
			Metal::BoundUniforms boundUniforms(pipeline, group0, group1, group2);
			i = _boundUniforms.insert(i, std::make_pair(hash, std::move(boundUniforms)));
		}
			
		return i->second;
	}
}}
