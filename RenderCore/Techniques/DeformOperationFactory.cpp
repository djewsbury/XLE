// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "DeformOperationFactory.h"
#include "Services.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Techniques
{
#if 0
	auto DeformOperationFactorySet::CreateDeformOperators(
		StringSection<> initializer,
		const std::shared_ptr<RenderCore::Assets::ModelScaffold>& modelScaffold,
		const std::string& modelScaffoldName) -> std::vector<Deformer>
	{
		std::vector<Deformer> result;

		auto* i = initializer.begin();
		while (i != initializer.end()) {
			auto* start = i;
			
			while (i!=initializer.end() && *i != ';') ++i;
			auto* end = i;
			if (i!=initializer.end()) ++i;

			auto* colon = XlFindChar(MakeStringSection(start, end), ':');
			auto* afterColon = end;
			if (colon) {
				afterColon = colon+1;
			} else {
				colon = end;
			}

			auto hash = Hash64(MakeStringSection(start, colon));
			auto i = LowerBound(_instantiationFunctions, hash);
			if (i==_instantiationFunctions.end() || i->first != hash)
				continue;

			Deformer deformer;
			deformer._factory = i->second._factory;
			deformer._operator = i->second._factory->Configure(
				deformer._instantiations, 
				MakeStringSection(afterColon, end), modelScaffold, modelScaffoldName);
			if (deformer._operator)
				result.push_back(std::move(deformer));
		}
		return result;
	}

	std::shared_ptr<IGeoDeformerFactory> DeformOperationFactorySet::GetFactory(uint64_t name) const
	{
		auto i = LowerBound(_instantiationFunctions, name);
		if (i!=_instantiationFunctions.end() && i->first == name)
			return i->second._factory;
		return nullptr;
	}

	auto DeformOperationFactorySet::Register(StringSection<> name, std::shared_ptr<IGeoDeformerFactory> factory) -> RegisteredDeformId
	{
		RegisteredDeformId result = _nextDeformId++;
		auto hash = Hash64(name.begin(), name.end());
		auto i = LowerBound(_instantiationFunctions, hash);
		if (i!=_instantiationFunctions.end() && i->first == hash) {
			i->second = {std::move(factory), result};
		} else {
			_instantiationFunctions.insert(i, std::make_pair(hash, RegisteredDeformOp{std::move(factory), result}));
		}
		return result;
	}

	void DeformOperationFactorySet::Deregister(RegisteredDeformId deformId)
	{
		auto i = std::remove_if(
			_instantiationFunctions.begin(),
			_instantiationFunctions.end(),
			[deformId](const std::pair<uint64_t, RegisteredDeformOp>& i) {
				return i.second._deformId == deformId;
			});
		_instantiationFunctions.erase(i, _instantiationFunctions.end());
	}

	DeformOperationFactorySet::DeformOperationFactorySet()
	{
		_nextDeformId = 1;
	}

	DeformOperationFactorySet::~DeformOperationFactorySet()
	{
	}

	DeformOperationFactorySet& DeformOperationFactorySet::GetInstance()
	{
		return Services::GetDeformOperationFactorySet();
	}

	IGeoDeformerFactory::~IGeoDeformerFactory() {}
#endif
}}
