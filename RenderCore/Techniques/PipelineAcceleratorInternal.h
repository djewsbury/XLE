// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "PipelineAccelerator.h"
#include "../Metal/DeviceContext.h"
#include "../Metal/InputLayout.h"
#include "../../Utility/Threading/Mutex.h"

namespace RenderCore { class ICompiledPipelineLayout; }
namespace RenderCore { namespace Techniques
{
	class BoundUniformsPool
	{
	public:
		std::vector<std::pair<uint64_t, std::unique_ptr<Metal::BoundUniforms>>> _boundUniforms;
		mutable Threading::Mutex _lock;

		Metal::BoundUniforms& Get(
			const Metal::GraphicsPipeline& pipeline,
			const UniformsStreamInterface& group0);
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

		BoundUniformsPool() = default;
		BoundUniformsPool(BoundUniformsPool&&);
		BoundUniformsPool& operator=(BoundUniformsPool&&);
		BoundUniformsPool(const BoundUniformsPool&);
		BoundUniformsPool& operator=(const BoundUniformsPool&);
	};

	class IPipelineAcceleratorPool::Pipeline
	{
	public:
		std::shared_ptr<Metal::GraphicsPipeline> _metalPipeline;
		std::shared_ptr<ICompiledPipelineLayout> _pipelineLayout;
		mutable BoundUniformsPool _boundUniformsPool;
		
		::Assets::DependencyValidation _depVal;
		const ::Assets::DependencyValidation& GetDependencyValidation() const { return _depVal; }

		#if defined(_DEBUG)
			std::string _vsDescription, _psDescription, _gsDescription;
		#endif
	};

	inline Metal::BoundUniforms& BoundUniformsPool::Get(
		const Metal::GraphicsPipeline& pipeline,
		const UniformsStreamInterface& group0)
	{
		ScopedLock(_lock);
		uint64_t hash = HashCombine(group0.GetHash(), pipeline.GetInterfaceBindingGUID());
		auto i = LowerBound(_boundUniforms, hash);
		if (i == _boundUniforms.end() || i->first != hash) {
			auto boundUniforms = std::make_unique<Metal::BoundUniforms>(pipeline, group0);
			i = _boundUniforms.insert(i, std::make_pair(hash, std::move(boundUniforms)));
		}
		return *i->second;
	}
	
	inline Metal::BoundUniforms& BoundUniformsPool::Get(
		const Metal::GraphicsPipeline& pipeline,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1)
	{
		ScopedLock(_lock);
		uint64_t hash = pipeline.GetInterfaceBindingGUID();
		hash = HashCombine(group0.GetHash(), hash);
		hash = HashCombine(group1.GetHash(), hash);

		auto i = LowerBound(_boundUniforms, hash);
		if (i == _boundUniforms.end() || i->first != hash) {
			auto boundUniforms = std::make_unique<Metal::BoundUniforms>(pipeline, group0, group1);
			i = _boundUniforms.insert(i, std::make_pair(hash, std::move(boundUniforms)));
		}
			
		return *i->second;
	}

	inline Metal::BoundUniforms& BoundUniformsPool::Get(
		const Metal::ComputePipeline& pipeline,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1)
	{
		ScopedLock(_lock);
		uint64_t hash = pipeline.GetInterfaceBindingGUID();
		hash = HashCombine(group0.GetHash(), hash);
		hash = HashCombine(group1.GetHash(), hash);

		auto i = LowerBound(_boundUniforms, hash);
		if (i == _boundUniforms.end() || i->first != hash) {
			auto boundUniforms = std::make_unique<Metal::BoundUniforms>(pipeline, group0, group1);
			i = _boundUniforms.insert(i, std::make_pair(hash, std::move(boundUniforms)));
		}
			
		return *i->second;
	}

	inline Metal::BoundUniforms& BoundUniformsPool::Get(
		const Metal::GraphicsPipeline& pipeline,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1,
		const UniformsStreamInterface& group2)
	{
		ScopedLock(_lock);
		uint64_t hash = pipeline.GetInterfaceBindingGUID();
		hash = HashCombine(group0.GetHash(), hash);
		hash = HashCombine(group1.GetHash(), hash);
		hash = HashCombine(group2.GetHash(), hash);

		auto i = LowerBound(_boundUniforms, hash);
		if (i == _boundUniforms.end() || i->first != hash) {
			auto boundUniforms = std::make_unique<Metal::BoundUniforms>(pipeline, group0, group1, group2);
			i = _boundUniforms.insert(i, std::make_pair(hash, std::move(boundUniforms)));
		}
			
		return *i->second;
	}

	inline Metal::BoundUniforms& BoundUniformsPool::Get(
		const Metal::ComputePipeline& pipeline,
		const UniformsStreamInterface& group0,
		const UniformsStreamInterface& group1,
		const UniformsStreamInterface& group2)
	{
		ScopedLock(_lock);
		uint64_t hash = pipeline.GetInterfaceBindingGUID();
		hash = HashCombine(group0.GetHash(), hash);
		hash = HashCombine(group1.GetHash(), hash);
		hash = HashCombine(group2.GetHash(), hash);

		auto i = LowerBound(_boundUniforms, hash);
		if (i == _boundUniforms.end() || i->first != hash) {
			auto boundUniforms = std::make_unique<Metal::BoundUniforms>(pipeline, group0, group1, group2);
			i = _boundUniforms.insert(i, std::make_pair(hash, std::move(boundUniforms)));
		}
			
		return *i->second;
	}

	inline BoundUniformsPool::BoundUniformsPool(BoundUniformsPool&& moveFrom)
	{
		ScopedLock(moveFrom._lock);
		_boundUniforms = std::move(moveFrom._boundUniforms);
	}
	inline BoundUniformsPool& BoundUniformsPool::operator=(BoundUniformsPool&& moveFrom)
	{
		std::unique_lock<Threading::Mutex> l0(_lock, std::defer_lock);
		std::unique_lock<Threading::Mutex> l1(moveFrom._lock, std::defer_lock);
		std::lock(l0, l1);
		_boundUniforms = std::move(moveFrom._boundUniforms);
		return *this;
	}
	inline BoundUniformsPool::BoundUniformsPool(const BoundUniformsPool& copyFrom)
	{
		// in normal usage we should only attempt to copy empty pools
		ScopedLock(copyFrom._lock);
		if (!copyFrom._boundUniforms.empty())
			Throw(std::runtime_error("Attempting to copy non-empty bound uniforms pool"));
	}
	inline BoundUniformsPool& BoundUniformsPool::operator=(const BoundUniformsPool& copyFrom)
	{
		// in normal usage we should only attempt to copy empty pools
		std::unique_lock<Threading::Mutex> l0(_lock, std::defer_lock);
		std::unique_lock<Threading::Mutex> l1(copyFrom._lock, std::defer_lock);
		std::lock(l0, l1);
		if (!copyFrom._boundUniforms.empty() || !_boundUniforms.empty())
			Throw(std::runtime_error("Attempting to copy non-empty bound uniforms pool"));
		return *this;
	}
}}
