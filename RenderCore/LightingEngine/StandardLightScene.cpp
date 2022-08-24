// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "StandardLightScene.h"
#include "ILightScene.h"
#include "../../Core/Exceptions.h"

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	void* StandardPositionalLight::QueryInterface(uint64_t interfaceTypeCode)
	{
		if (interfaceTypeCode == typeid(IPositionalLightSource).hash_code()) {
			return (IPositionalLightSource*)this;
		} else if (interfaceTypeCode == typeid(IUniformEmittance).hash_code()) {
			return (IUniformEmittance*)this;
		} else if (interfaceTypeCode == typeid(IFiniteLightSource).hash_code()) {
			if (_flags & Flags::SupportFiniteRange)
				return (IFiniteLightSource*)this;
		} else if (interfaceTypeCode == typeid(StandardPositionalLight).hash_code()) {
			return this;
		} else if (interfaceTypeCode == typeid(IAttachedShadowProbe).hash_code()) {
			return (IAttachedShadowProbe*)this;
		}
		return nullptr;
	}

	void* StandardLightScene::TryGetLightSourceInterface(LightSourceId sourceId, uint64_t interfaceTypeCode)
	{
		for (auto&set:_tileableLightSets) {
			auto i = std::find_if(
				set._lights.begin(), set._lights.end(),
				[sourceId](const auto& c) { return c._id == sourceId; });
			if (i != set._lights.end())
				return i->_desc->QueryInterface(interfaceTypeCode);
		}

		assert(_dominantLightSet._lights.size() <= 1);
		if (!_dominantLightSet._lights.empty() && _dominantLightSet._lights[0]._id == sourceId)
			return _dominantLightSet._lights[0]._desc->QueryInterface(interfaceTypeCode);
		return nullptr;
	}

	auto StandardLightScene::AddLightSource(LightOperatorId operatorId, std::unique_ptr<ILightBase> desc) -> LightSourceId
	{
		auto result = _nextLightSource++;
		if (operatorId == _dominantLightSet._operatorId) {
			_dominantLightSet._lights.push_back({result, std::move(desc)});
		} else
			GetLightSet(operatorId, ~0u)._lights.push_back({result, std::move(desc)});
		return result;
	}

	void StandardLightScene::DestroyLightSource(LightSourceId sourceId)
	{
		for (auto&set:_tileableLightSets) {
			auto i = std::find_if(
				set._lights.begin(), set._lights.end(),
				[sourceId](const auto& c) { return c._id == sourceId; });
			if (i != set._lights.end()) {
				set._lights.erase(i);

				// Also destroy a shadow projection associated with this light, if it exists
				if (set._shadowOperatorId != ~0u) {
					auto i = std::find_if(
						_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
						[sourceId](const auto& c) { return c._lightId == sourceId; });
					if (i != _dynamicShadowProjections.end())
						_dynamicShadowProjections.erase(i);
				}
				return;
			}				
		}

		{
			auto i = std::find_if(
				_dominantLightSet._lights.begin(), _dominantLightSet._lights.end(),
				[sourceId](const auto& c) { return c._id == sourceId; });
			if (i != _dominantLightSet._lights.end()) {
				_dominantLightSet._lights.erase(i);
				// as with above, we have to destroy the attached shadow projection
				_dominantShadowProjection = {};
				return;
			}
		}

		Throw(std::runtime_error("Invalid light source id: " + std::to_string(sourceId)));
	}

	void* StandardLightScene::TryGetShadowProjectionInterface(ShadowProjectionId preparerId, uint64_t interfaceTypeCode)
	{
		auto i = std::find_if(
			_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
			[preparerId](const auto& c) { return c._id == preparerId; });
		if (i != _dynamicShadowProjections.end())
			return i->_desc->QueryInterface(interfaceTypeCode);

		if (_dominantShadowProjection._id == preparerId)
			return _dominantShadowProjection._desc->QueryInterface(interfaceTypeCode);

		return nullptr;
	}
	
	auto StandardLightScene::AddShadowProjection(
		ShadowOperatorId shadowOperatorId,
		LightSourceId associatedLight,
		std::unique_ptr<ILightBase> desc) -> ShadowProjectionId
	{
		auto result = _nextShadow++;

		// assuming just the one dominant light here
		assert(_dominantLightSet._lights.size() <= 1);
		if (!_dominantLightSet._lights.empty() && associatedLight == _dominantLightSet._lights[0]._id) {
			assert(_dominantShadowProjection._id == ~0u);
			assert(_dominantLightSet._shadowOperatorId == shadowOperatorId);		// the shadow operator is preset, and must agree with the shadow projection begin applied
			_dominantShadowProjection = {result, shadowOperatorId, associatedLight, std::move(desc)};
			return result;
		}

		_dynamicShadowProjections.push_back({result, shadowOperatorId, associatedLight, std::move(desc)});
		for (auto&set:_tileableLightSets) {
			if (set._shadowOperatorId != ~0u) {
				#if defined(_DEBUG)
					auto i = std::find_if(
						set._lights.begin(), set._lights.end(),
						[associatedLight](const auto& c) { return c._id == associatedLight; });
					assert(i == set._lights.end());		// if you hit this it means we're associating a shadow projection with a light that already has a shadow projection
				#endif
				continue;
			}
			auto i = std::find_if(
				set._lights.begin(), set._lights.end(),
				[associatedLight](const auto& c) { return c._id == associatedLight; });
			if (i != set._lights.end()) {
				auto l = std::move(*i);
				set._lights.erase(i);
				GetLightSet(set._operatorId, shadowOperatorId)._lights.push_back(std::move(l));
				return result;
			}
		}

		assert(0);		// could not find a light with the id "associatedLight"
		return result;
	}

	void StandardLightScene::ChangeLightsShadowOperator(IteratorRange<const LightSourceId*> lights, ShadowOperatorId shadowOperatorId)
	{
		std::vector<size_t> intersection;
		intersection.reserve(lights.size());
		for (unsigned seti=0; seti<_tileableLightSets.size(); ++seti) {
			{
				auto& set = _tileableLightSets[seti];
				if (set._shadowOperatorId == shadowOperatorId) continue;

				intersection.clear();
				for (size_t c=0; c<set._lights.size(); ++c)
					if (std::find(lights.begin(), lights.end(), set._lights[c]._id) != lights.end())
						intersection.push_back(c);
			}

			if (!intersection.empty()) {
				auto& dstSet = GetLightSet(_tileableLightSets[seti]._operatorId, shadowOperatorId);	// modifies _tileableLightSets
				auto& srcSet = _tileableLightSets[seti];
				for (auto i=intersection.rbegin(); i!=intersection.rend(); ++i) {
					auto l = std::move(srcSet._lights[*i]);
					srcSet._lights.erase(srcSet._lights.begin()+*i);
					dstSet._lights.push_back(std::move(l));
				}
			}
		}
	}

	void StandardLightScene::DestroyShadowProjection(ShadowProjectionId preparerId)
	{
		auto i = std::find_if(
			_dynamicShadowProjections.begin(), _dynamicShadowProjections.end(),
			[preparerId](const auto& c) { return c._id == preparerId; });
		if (i != _dynamicShadowProjections.end()) {
			auto associatedLight = i->_lightId;

			for (auto&set:_tileableLightSets) {
				if (set._shadowOperatorId != i->_operatorId) continue;
				auto i = std::find_if(
					set._lights.begin(), set._lights.end(),
					[associatedLight](const auto& c) { return c._id == associatedLight; });
				if (i != set._lights.end()) {
					auto l = std::move(*i);
					set._lights.erase(i);
					GetLightSet(set._operatorId, ~0u)._lights.push_back(std::move(l));
				}
			}

			_dynamicShadowProjections.erase(i);
			return;
		}

		if (_dominantShadowProjection._id == preparerId) {
			_dominantShadowProjection = {};
			return;
		}
			
		Throw(std::runtime_error("Invalid shadow preparer id: " + std::to_string(preparerId)));
	}

	auto StandardLightScene::GetLightSet(LightOperatorId lightOperator, ShadowOperatorId shadowOperator) -> LightSet&
	{
		for (auto&s:_tileableLightSets)
			if (s._operatorId == lightOperator && s._shadowOperatorId == shadowOperator)
				return s;
		_tileableLightSets.push_back(LightSet{lightOperator, shadowOperator});
		return *(_tileableLightSets.end()-1);
	}

	void StandardLightScene::Clear()
	{
		for (auto& set:_tileableLightSets)
			set._lights.clear();
		_dynamicShadowProjections.clear();
		_dominantShadowProjection = {};
		_dominantLightSet._lights.clear();
	}

	void StandardLightScene::ReserveLightSourceIds(unsigned idCount)
	{
		_nextLightSource += idCount;
	}

	void* StandardLightScene::QueryInterface(uint64_t typeCode)
	{
		if (typeCode == typeid(StandardLightScene).hash_code())
			return this;
		return nullptr;
	}

	StandardLightScene::StandardLightScene()
	{}
	StandardLightScene::~StandardLightScene()
	{}

	ILightBase::~ILightBase() {}
}}}

namespace RenderCore { namespace LightingEngine
{
	uint64_t LightSourceOperatorDesc::GetHash(uint64_t seed) const
	{
		uint64_t h = 
			(uint64_t(_shape) & 0xff) << 8ull | (uint64_t(_diffuseModel) & 0xff) | (uint64_t(_flags) << 16ull);
		return HashCombine(h, seed);
	}

	std::optional<LightSourceShape> AsLightSourceShape(StringSection<> input)
	{
		if (XlEqString(input, "Directional")) return LightSourceShape::Directional;
		if (XlEqString(input, "Sphere")) return LightSourceShape::Sphere;
		if (XlEqString(input, "Tube")) return LightSourceShape::Tube;
		if (XlEqString(input, "Rectangle")) return LightSourceShape::Rectangle;
		if (XlEqString(input, "Disc")) return LightSourceShape::Disc;
		return {};
	}
	const char* AsString(LightSourceShape shape)
	{
		switch (shape) {
		case LightSourceShape::Directional: return "Directional";
		case LightSourceShape::Sphere: return "Sphere";
		case LightSourceShape::Tube: return "Tube";
		case LightSourceShape::Rectangle: return "Rectangle";
		case LightSourceShape::Disc: return "Disc";
		default:
			return nullptr;
		}
	}
	std::optional<DiffuseModel> AsDiffuseModel(StringSection<> input)
	{
		if (XlEqString(input, "Lambert")) return DiffuseModel::Lambert;
		if (XlEqString(input, "Disney")) return DiffuseModel::Disney;
		return {};
	}
	const char* AsString(DiffuseModel diffuseModel)
	{
		switch (diffuseModel) {
		case DiffuseModel::Lambert: return "Lambert";
		case DiffuseModel::Disney: return "Disney";
		default:
			return nullptr;
		}
	}

	ILightScene::~ILightScene() {}
	IPositionalLightSource::~IPositionalLightSource() {}
	IUniformEmittance::~IUniformEmittance() {}
	IFiniteLightSource::~IFiniteLightSource() {}
	IDistantIBLSource::~IDistantIBLSource() {}
	ISSAmbientOcclusion::~ISSAmbientOcclusion() {}
	IDepthTextureResolve::~IDepthTextureResolve() {}
	IArbitraryShadowProjections::~IArbitraryShadowProjections() {}
	IOrthoShadowProjections::~IOrthoShadowProjections() {}
	INearShadowProjection::~INearShadowProjection() {}
	IAttachedShadowProbe::~IAttachedShadowProbe() {}
}}
