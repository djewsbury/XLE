// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PipelineConfigurationUtils.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/StreamDOM.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/MemoryUtils.h"

using namespace Utility::Literals;
namespace RenderCore { namespace Assets
{
	static LegacyRegisterBindingDesc::RegisterQualifier AsQualifier(StringSection<char> str)
	{
		// look for "(image)" or "(buffer)" qualifiers
		if (str.IsEmpty() || str[0] != '(') return LegacyRegisterBindingDesc::RegisterQualifier::None;

		if (XlEqStringI(StringSection<char>(str.begin()+1, str.end()), "buffer)"))
			return LegacyRegisterBindingDesc::RegisterQualifier::Buffer;

		if (XlEqStringI(StringSection<char>(str.begin()+1, str.end()), "texture)"))
			return LegacyRegisterBindingDesc::RegisterQualifier::Texture;

		return LegacyRegisterBindingDesc::RegisterQualifier::None;
	}
	
	struct RegisterRange
	{
		unsigned long _begin = 0, _end = 0;
		LegacyRegisterBindingDesc::RegisterQualifier _qualifier;
	};

	static RegisterRange AsRegisterRange(StringSection<> input)
	{
		if (input.IsEmpty()) return {};

		char* endPt = nullptr;
		auto start = std::strtoul(input.begin(), &endPt, 10);
		auto end = start+1;
		if (endPt && endPt[0] == '.' && endPt[1] == '.')
			end = std::strtoul(endPt+2, &endPt, 10);

		auto qualifier = AsQualifier(StringSection<char>(endPt, input.end()));
		return {start, end, qualifier};
	}

	static LegacyRegisterBindingDesc::RegisterType AsLegacyRegisterType(char type)
	{
		// convert between HLSL style register binding indices to a type enum
		switch (type) {
		case 'b': return LegacyRegisterBindingDesc::RegisterType::ConstantBuffer;
		case 's': return LegacyRegisterBindingDesc::RegisterType::Sampler;
		case 't': return LegacyRegisterBindingDesc::RegisterType::ShaderResource;
		case 'u': return LegacyRegisterBindingDesc::RegisterType::UnorderedAccess;
		default:  return LegacyRegisterBindingDesc::RegisterType::Unknown;
		}
	}

	void DeserializationOperator(
		InputStreamFormatter<>& formatter,
		LegacyRegisterBindingDesc& result)
	{
		StreamDOM<InputStreamFormatter<>> dom(formatter);
		auto element = dom.RootElement();
		for (auto e:element.children()) {
			auto name = e.Name();
			if (name.IsEmpty())
				Throw(std::runtime_error("Legacy register binding with empty name"));

			auto regType = AsLegacyRegisterType(name[0]);
			if (regType == LegacyRegisterBindingDesc::RegisterType::Unknown)
				Throw(::Exceptions::BasicLabel("Could not parse legacy register binding (%s)", name.AsString().c_str()));

			auto legacyRegisters = AsRegisterRange({name.begin()+1, name.end()});
			if (legacyRegisters._end <= legacyRegisters._begin)
				Throw(::Exceptions::BasicLabel("Could not parse legacy register binding (%s)", name.AsString().c_str()));

			auto mappedRegisters = AsRegisterRange(e.Attribute("mapping").Value());
			if (mappedRegisters._begin == mappedRegisters._end)
				Throw(::Exceptions::BasicLabel("Could not parse target register mapping in ReadLegacyRegisterBinding (%s)", e.Attribute("mapping").Value().AsString().c_str()));
			
			if ((mappedRegisters._end - mappedRegisters._begin) != (legacyRegisters._end - legacyRegisters._begin))
				Throw(::Exceptions::BasicLabel("Number of legacy register and number of mapped registers don't match up in ReadLegacyRegisterBinding"));

			result.AppendEntry(
				regType, legacyRegisters._qualifier,
				LegacyRegisterBindingDesc::Entry {
					(unsigned)legacyRegisters._begin, (unsigned)legacyRegisters._end,
					Hash64(e.Attribute("set").Value()),
					e.Attribute("setIndex").As<unsigned>().value(),
					(unsigned)mappedRegisters._begin, (unsigned)mappedRegisters._end });
		}
	}

	RenderCore::LegacyRegisterBindingDesc CreateDefaultLegacyRegisterBindingDesc()
	{
		#if 0
			const char* defaultCfg = R"--(
				t0..3=~
					set = Numeric
					setIndex = 2
					mapping = 0..3
				b3..4=~
					set = Numeric
					setIndex = 2
					mapping = 3..4
			)--";

			LegacyRegisterBindingDesc result;
			InputStreamFormatter<> formatter(MakeStringSection(defaultCfg));
			DeserializationOperator(formatter, result);     // have to call it explicitly, because DeserializationOperator is not in the same namespace as LegacyRegisterBindingDesc
			return result;
		#else
			LegacyRegisterBindingDesc result;
			result.AppendPassThroughDescriptorSet("Numeric"_h);
			return result;
		#endif
	}

}}
