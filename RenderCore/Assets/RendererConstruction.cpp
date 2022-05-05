// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ScaffoldCmdStream.h"
#include "ModelMachine.h"

namespace RenderCore { namespace Assets
{
	
	class RendererConstruction : public IScaffoldNavigation
	{
	public:

		virtual IteratorRange<const void*> GetSubModel() override
		{
			return _firstSubModel;
		}

		virtual IteratorRange<const void*> GetGeoMachine(GeoId geoId) override
		{
			auto i = LowerBound(_geoMachines, geoId);
			if (i != _geoMachines.end() && i->first == geoId)
				return i->second;
			return {};
		}

		virtual IteratorRange<const void*> GetMaterialMachine(MaterialId) override
		{
			assert(0);
			return {};
		}

		virtual const IteratorRange<const void*> GetGeometryBufferData(GeoId, GeoBufferType) override
		{
			assert(0);
			return {};
		}

		RendererConstruction(std::shared_ptr<ScaffoldAsset> scaffoldAsset)
		: _scaffoldAsset(std::move(scaffoldAsset))
		{
			auto cmdStream = _scaffoldAsset->GetCmdStream();
			// pass through the cmd stream once and pull out important data
			for (auto cmd:cmdStream) {
				switch (cmd.Cmd()) {
				case (uint32_t)ModelCommand::Geo:
					{
						auto data = cmd.RawData();
						assert(data.size() > sizeof(uint32_t));
						_geoMachines.emplace_back(
							*(uint32_t*)data.begin(),
							VoidRange{PtrAdd(data.begin(), sizeof(uint32_t)), data.end()});
					}
					break;
				default:
					break;
				}
			}

			std::sort(_geoMachines.begin(), _geoMachines.end());
		}

	private:
		std::shared_ptr<ScaffoldAsset> _scaffoldAsset;

		using VoidRange = IteratorRange<const void*>;
		std::vector<std::pair<GeoId, VoidRange>> _geoMachines;
		VoidRange _firstSubModel;
	};

	std::shared_ptr<IScaffoldNavigation> CreateSimpleRendererConstruction(std::shared_ptr<ScaffoldAsset> scaffoldAsset)
	{
		return std::make_shared<RendererConstruction>(std::move(scaffoldAsset));
	}

}}
