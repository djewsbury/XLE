#include "SpritePipeline.h"
#include "ShaderInstantiation.h"
#include "NodeGraphSignature.h"
#include "Utility/MemoryUtils.h"
#include "Utility/FastParseValue.h"
#include "Utility/StringFormat.h"
#include <sstream>

#include "ShaderSignatureParser.h"
#include <iostream>

using namespace Utility::Literals;

namespace ShaderSourceParser
{
	namespace Internal
	{
		struct WorkingAttribute
		{
			std::string _semantic;
			unsigned _semanticIdx = 0;
			std::string _type;
		};

		static std::pair<StringSection<>, unsigned> SplitSemanticAndIdx(StringSection<> input)
		{
			auto i  = input.end();
			while (i != input.begin() && (*(i-1) >= '0' && *(i-1) <= '9')) --i;
			unsigned idx = 0;
			FastParseValue(MakeStringSection(i, input.end()), idx);
			return {{input.begin(), i}, idx};
		}

		static bool CompareSemantic(std::pair<StringSection<>, unsigned> lhs, std::pair<StringSection<>, unsigned> rhs)
		{
			return lhs.second == rhs.second && XlEqString(lhs.first, rhs.first);
		}

		static bool CompareSemantic(const WorkingAttribute& lhs, StringSection<> p)
		{
			auto s = SplitSemanticAndIdx(p);
			return s.second == lhs._semanticIdx && XlEqString(s.first, lhs._semantic);
		}
		
		static bool CompareSemantic(const WorkingAttribute& lhs, const GraphLanguage::NodeGraphSignature::Parameter& p)
		{
			return CompareSemantic(lhs, p._semantic);
		}

		static std::vector<WorkingAttribute>::const_iterator Find(const std::vector<WorkingAttribute>& v, std::pair<StringSection<>, unsigned> s)
		{
			return std::find_if(v.begin(), v.end(), [s](const auto& q) { return q._semanticIdx == s.second && XlEqString(s.first, q._semantic); });
		}

		static WorkingAttribute MakeWorkingAttribute(const GraphLanguage::NodeGraphSignature::Parameter& p)
		{
			auto s = SplitSemanticAndIdx(p._semantic);
			if (s.first.size() == p._semantic.size()) return { p._semantic, 0, p._type };
			return { s.first.AsString(), s.second, p._type };
		}

		#if 0
			static std::vector<WorkingAttribute> AttributesFromParameters(const ShaderEntryPoint& entryPoint, GraphLanguage::ParameterDirection direction)
			{
				std::vector<WorkingAttribute> result;
				for (const auto& p:entryPoint._signature.GetParameters())
					if (p._direction == direction)
						result.emplace_back(MakeWorkingAttribute(p));
				return result;
			}
		#endif

		static bool UpdateActiveAttributesBackwards(
			std::vector<WorkingAttribute>& result,
			const GraphLanguage::NodeGraphSignature& signature,
			IteratorRange<const WorkingAttribute*> postActiveAttributes)
		{
			// If the entry point writes to any of the active attributes, we must activate it
			// and propagate the new active attributes backwards
			using namespace GraphLanguage;
			bool active = false;
			result.clear();
			for (const auto& p:signature.GetParameters()) {
				if (p._direction != ParameterDirection::Out) continue;
				
				// always accept system values written out
				if (XlBeginsWith(MakeStringSection(p._semantic), "SV_")) {
					active = true;
					break;
				}

				auto i = std::find_if(postActiveAttributes.begin(), postActiveAttributes.end(),
					[&p](const auto& q) { return CompareSemantic(q, p); });
				if (i != postActiveAttributes.end()) {
					active = true;
					break;
				}
			}
			if (!active) {
				result.insert(result.end(), postActiveAttributes.begin(), postActiveAttributes.end());
				return false;
			}

			// All attributes in "postActiveAttributes" go into result,
			// except if they are written to. If they are both written to and read from,
			// we will add then back in the next step
			result.reserve(postActiveAttributes.size() + signature.GetParameters().size());
			for (const auto& a:postActiveAttributes) {
				auto i = std::find_if(signature.GetParameters().begin(), signature.GetParameters().end(),
					[&a](const auto& q) { return q._direction == ParameterDirection::Out && CompareSemantic(a, q); });
				if (i == signature.GetParameters().end())
					result.emplace_back(a);
			}
			
			for (const auto& p:signature.GetParameters()) {
				if  (p._direction != ParameterDirection::In) continue;
				auto i = std::find_if(result.begin(), result.end(),
					[&p](const auto& q) { return CompareSemantic(q, p); });
				if (i == result.end())
					result.emplace_back(MakeWorkingAttribute(p));
			}

			return true;
		}

		static std::string SemanticAndIdx(std::string semantic, unsigned semanticIdx)
		{
			std::string semanticAndIdx = semantic;
			if (semanticIdx != 0) semanticAndIdx += std::to_string(semanticIdx);
			return semanticAndIdx;
		}
		
		static std::string SemanticAndIdx(const WorkingAttribute& a)
		{
			std::string semanticAndIdx = a._semantic;
			if (a._semanticIdx != 0) semanticAndIdx += std::to_string(a._semanticIdx);
			return semanticAndIdx;
		}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		class FragmentWriter
		{
		public:
			void WriteInputParameter(std::string semantic, unsigned semanticIdx, std::string type);
			void WriteOutputParameter(std::string semantic, unsigned semanticIdx, std::string type);

			void WriteCall(StringSection<> callName, const GraphLanguage::NodeGraphSignature& sig);

			GraphLanguage::NodeGraphSignature Complete(std::stringstream& str, StringSection<> name);

			bool HasAttributeFor(StringSection<> semantic, unsigned semanticIdx);

			std::stringstream _parameterList, _body;

			struct WorkingAttributeWithName : public WorkingAttribute { std::string _name; };
			std::vector<WorkingAttributeWithName> _workingAttributes;
			GraphLanguage::NodeGraphSignature _signature;
			bool _parameterListPendingComma = false;
			unsigned _nextWorkingAttributeIdx = 0;
		};

		void FragmentWriter::WriteInputParameter(std::string semantic, unsigned semanticIdx, std::string type)
		{
			assert(SplitSemanticAndIdx(semantic).first.size() == semantic.size());
			auto i = std::find_if(_workingAttributes.begin(), _workingAttributes.end(),
				[semantic, semanticIdx](const auto& q) { return q._semantic == semantic && q._semanticIdx == semanticIdx; });
			if (i != _workingAttributes.end())
				Throw(std::runtime_error("Input attribute " + semantic + " specified multiple times"));

			if (_parameterListPendingComma) _parameterList << ", ";
			auto semanticAndIdx = SemanticAndIdx(semantic, semanticIdx);
			auto newName = Concatenate(semantic, "_gen_", std::to_string(_nextWorkingAttributeIdx++));
			_parameterList << type << " " << newName << ":" << semanticAndIdx;
			_parameterListPendingComma = true;
			_signature.AddParameter({type, newName, GraphLanguage::ParameterDirection::In, semanticAndIdx});
			_workingAttributes.emplace_back(WorkingAttributeWithName{semantic, semanticIdx, type, newName});
		}

		void FragmentWriter::WriteOutputParameter(std::string semantic, unsigned semanticIdx, std::string type)
		{
			assert(SplitSemanticAndIdx(semantic).first.size() == semantic.size());

			if (_parameterListPendingComma) _parameterList << ", ";
			auto semanticAndIdx = SemanticAndIdx(semantic, semanticIdx);
			auto newName = Concatenate("out_", semantic, "_gen_", std::to_string(_nextWorkingAttributeIdx++));
			_parameterList << "out " << type << " " << newName << ":" << semanticAndIdx;
			_parameterListPendingComma = true;
			_signature.AddParameter({type, newName, GraphLanguage::ParameterDirection::Out, semanticAndIdx});
		}

		void FragmentWriter::WriteCall(StringSection<> callName, const GraphLanguage::NodeGraphSignature& sig)
		{
			std::stringstream temp;
			temp << "\t" << callName << "(";

			bool pendingComma = false;
			for (const auto&p:sig.GetParameters()) {
				if (pendingComma) temp << ", ";

				auto s = SplitSemanticAndIdx(p._semantic);
				auto i = std::find_if(_workingAttributes.begin(), _workingAttributes.end(), [s](const auto& q) { return s.second == q._semanticIdx && XlEqString(s.first, q._semantic); });
				if (p._direction == GraphLanguage::ParameterDirection::In) {
					if (i != _workingAttributes.end()) {
						if (i->_type == p._type) {
							temp << i->_name;
						} else {
							temp << "Cast_" << i->_type << "_to_" << p._type << "(" << i->_name << ")";
						}
					} else {
						temp << "DefaultValue_" << p._type << "()";
					}
				} else {
					// we will attempt to reuse the existing working attribute if we can. Otherwise we just create a new one
					if (i == _workingAttributes.end()) {
						auto newName = Concatenate(s.first, "_gen_", std::to_string(_nextWorkingAttributeIdx++));
						_body << "\t" << p._type << " " << newName << ";" << std::endl;
						i = _workingAttributes.insert(_workingAttributes.end(), WorkingAttributeWithName{s.first.AsString(), s.second, p._type, newName});
					} else if (i->_type != p._type) {
						auto newName = Concatenate(s.first, "_gen_", std::to_string(_nextWorkingAttributeIdx++));
						_body << "\t" << p._type << " " << newName << ";" << std::endl;
						*i = WorkingAttributeWithName{s.first.AsString(), s.second, p._type, newName};
					}
					temp << i->_name;
				}

				pendingComma = true;
			}

			_body << temp.str() << ");" << std::endl;
		}

		GraphLanguage::NodeGraphSignature FragmentWriter::Complete(std::stringstream& str, StringSection<> name)
		{
			str << "void " << name << "(" << _parameterList.str() << ")" << std::endl;
			str << "{" << std::endl;
			str << _body.str() << std::endl;

			// Write to the output parameters as they were declared in the signature
			for (const auto& p:_signature.GetParameters()) {
				if (p._direction != GraphLanguage::ParameterDirection::Out) continue;
				str << "\t" << p._name << " = ";
				auto s = SplitSemanticAndIdx(p._semantic);
				auto i = std::find_if(_workingAttributes.begin(), _workingAttributes.end(), [s](const auto& q) { return q._semanticIdx == s.second && XlEqString(s.first, q._semantic); });
				if (i != _workingAttributes.end()) {
					str << i->_name;
				} else {
					str << "DefaultValue_" << p._type << "()";		// we never actually got anything to write to this semantic
				}
				str << ";" << std::endl;
			}

			str << "}" << std::endl;
			return _signature;
		}

		bool FragmentWriter::HasAttributeFor(StringSection<> semantic, unsigned semanticIdx)
		{
			auto i = std::find_if(_workingAttributes.begin(), _workingAttributes.end(),
				[semantic, semanticIdx](const auto& q) { return semanticIdx == q._semanticIdx && XlEqString(semantic, q._semantic); });
			return i != _workingAttributes.end();
		}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics
		static constexpr std::pair<const char*, const char*> s_validVSInputSystemValues[] {
			{"SV_InstanceID", "uint"},
			{"SV_VertexID", "uint"}
		};

		static constexpr std::pair<const char*, const char*> s_validGSInputSystemValues[] {
			{"SV_ClipDistance", "float"},		// multiple indices
			{"SV_CullDistance", "float"},		// multiple indices
			{"SV_InstanceID", "uint"},
			{"SV_PrimitiveID", "uint"}
		};

		static void AddPSInputSystemAttributes(std::vector<WorkingAttribute>& result)
		{
			constexpr const char* svPositionAttribute = "SV_Position";
			if (std::find_if(result.begin(), result.end(), [svPositionAttribute](const auto& q) { return q._semantic == svPositionAttribute && q._semanticIdx == 0; }) == result.end())
				result.emplace_back(Internal::WorkingAttribute{svPositionAttribute, 0, "float4"});
		}

		static bool TryWriteVSSystemInput(FragmentWriter& writer, StringSection<> semantic, unsigned semanticIdx)
		{
			if (!XlBeginsWith(semantic, "SV_")) return false;
			for (const auto& q:s_validVSInputSystemValues)
				if (XlEqString(semantic, q.first)) {
					writer.WriteInputParameter(semantic.AsString(), semanticIdx, q.second);
					return true;
				}
			return false;
		}

		static bool IsVSInputSystemAttributes(StringSection<> semantic, unsigned semanticIdx)
		{
			// SV_Position is always generated in the VS (and so can be removed from this point)
			if (!XlBeginsWith(semantic, "SV_")) return false;
			for (const auto& s:s_validVSInputSystemValues)
				if (XlEqString(semantic, s.first))
					return true;
			return false;
		}

		static bool TryWriteGSSystemInput(FragmentWriter& writer, StringSection<> semantic, unsigned semanticIdx)
		{
			if (!XlBeginsWith(semantic, "SV_")) return false;
			for (const auto& q:s_validGSInputSystemValues)
				if (XlEqString(semantic, q.first)) {
					writer.WriteInputParameter(semantic.AsString(), semanticIdx, q.second);
					return true;
				}
			return false;
		}

		static bool IsGSInputSystemAttributes(StringSection<> semantic, unsigned semanticIdx)
		{
			// SV_Position is always generated in the VS (and so can be removed from this point)
			if (!XlBeginsWith(semantic, "SV_")) return false;
			for (const auto& s:s_validGSInputSystemValues)
				if (XlEqString(semantic, s.first))
					return true;
			return false;
		}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		class FragmentArranger
		{
		public:
			void AddStep(const ShaderEntryPoint& entryPoint);		// add in reverse order
			void AddFragmentOutput(const WorkingAttribute& a);

			std::vector<WorkingAttribute> RebuildInputAttributes();
			std::vector<WorkingAttribute> CalculateAvailableInputsAtStep(unsigned stepIdx);
			unsigned CalculateInsertPosition(const GraphLanguage::NodeGraphSignature& signature);

			struct Step
			{
				std::string _name;
				const GraphLanguage::NodeGraphSignature* _signature;
				bool _enabled = false;
			};
			std::vector<Step> _steps;

			std::vector<WorkingAttribute> _fragmentOutput;
		};

		void FragmentArranger::AddStep(const ShaderEntryPoint& entryPoint)
		{
			_steps.emplace_back(Step{entryPoint._name, &entryPoint._signature});
		}

		void FragmentArranger::AddFragmentOutput(const WorkingAttribute& a)
		{
			for (const auto& q:_fragmentOutput)
				if (q._semantic == a._semantic && q._semanticIdx == a._semanticIdx)
					return;	// suppress dupes
			_fragmentOutput.emplace_back(a);
		}

		std::vector<WorkingAttribute> FragmentArranger::RebuildInputAttributes()
		{
			// Walk backwards through the patches, updating the list of active attributes as we go
			// this is only actually required for filtering out the steps that are not required by downstream steps
			std::vector<WorkingAttribute> activeAttributes = _fragmentOutput;
			for (auto r=_steps.rbegin(); r!=_steps.rend(); ++r) {
				auto postActiveAttributes = std::move(activeAttributes);
				r->_enabled = UpdateActiveAttributesBackwards(activeAttributes, *r->_signature, postActiveAttributes);
			}
			return activeAttributes;
		}

		unsigned FragmentArranger::CalculateInsertPosition(const GraphLanguage::NodeGraphSignature& signature)
		{
			// calculate the correct place to insert this
			// We must return the location before any step that uses any of it's outputs
			std::vector<std::pair<StringSection<>, unsigned>> outputs;
			for (const auto& p:signature.GetParameters())
				if (p._direction == GraphLanguage::ParameterDirection::Out)
					outputs.emplace_back(SplitSemanticAndIdx(p._semantic));

			for (unsigned idx=0; idx<(unsigned)_steps.size(); ++idx) {
				bool overlap = false;
				for (const auto& p:_steps[idx]._signature->GetParameters()) {
					if (p._direction != GraphLanguage::ParameterDirection::In) continue;
					auto s = SplitSemanticAndIdx(p._semantic);
					auto i = std::find_if(
						outputs.begin(), outputs.end(),
						[s](const auto&q) { return q.second == s.second && XlEqString(q.first, s.first); });
					overlap |= i!=outputs.end();
				}
				if (overlap) return idx;		// insert before this step
			}
			return (unsigned)_steps.size();
		}

		std::vector<WorkingAttribute> FragmentArranger::CalculateAvailableInputsAtStep(unsigned stepIdx)
		{
			std::vector<WorkingAttribute> result;
			// Calculate the input attributes that are going to be available by the given step
			assert(stepIdx <= _steps.size());
			for (const auto& step:MakeIteratorRange(AsPointer(_steps.begin()), AsPointer(_steps.begin())+stepIdx)) {
				for (const auto& p:step._signature->GetParameters()) {
					if (p._direction != GraphLanguage::ParameterDirection::Out) continue;
					if (Find(result, SplitSemanticAndIdx(p._semantic))  == result.end())
						result.emplace_back(MakeWorkingAttribute(p));
				}
			}
			return result;
		}

		static void RemoveActiveAttribute(std::vector<WorkingAttribute>& activeAttributes, StringSection<> semantic, unsigned semanticIdx)
		{
			auto i = std::remove_if(activeAttributes.begin(), activeAttributes.end(),
				[semantic, semanticIdx](const auto& q) { return q._semanticIdx == semanticIdx && XlEqString(semantic, q._semantic); });
			activeAttributes.erase(i, activeAttributes.end());
		}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		static void ConnectSystemPatches(
			FragmentArranger& arranger,
			const GraphLanguage::ShaderFragmentSignature& systemPatches,
			std::function<bool(StringSection<>, unsigned)>&& isProvidedFn)
		{
			unsigned attemptCount = 0;
			for (;;) {
				// protect against infinite loops
				if (attemptCount++ > 32u) Throw(std::runtime_error("Suspected infinite loop awhile attempting to construct sprite pipeline"));

				auto unprovidedAttributes = arranger.RebuildInputAttributes();
				{
					auto i = std::remove_if(unprovidedAttributes.begin(), unprovidedAttributes.end(), [&isProvidedFn](const auto& q) { return isProvidedFn(q._semantic, q._semanticIdx); });
					unprovidedAttributes.erase(i, unprovidedAttributes.end());
				}

				// We must attempt to get the attributes in unprovidedAttributes from system patches
				// we should place the new step as late in the order as possible, just before the point
				// it is required.
				//
				// However, the step we add might have new inputs it requires, as well -- and so we need to 
				// be prepared to satisfy those as well
				//
				// We'll prioritize the list of system patches by the order they appear in the file
				// We also need to prioritize based on the number of matched and unmatched inputs
				struct ProspectivePatch
				{
					unsigned _matchedInputs = 0, _unmatchedInputs = 0;
					unsigned _insertionPt = ~0u;
					std::string _name;
					const GraphLanguage::NodeGraphSignature* _signature = nullptr;
				};
				std::vector<ProspectivePatch> prospectivePatches;
				for (const auto& e:systemPatches._functions) {
					bool isUseful = false;
					for (const auto&p:e.second.GetParameters()) {
						if (p._direction != GraphLanguage::ParameterDirection::Out) continue;
						auto s = Internal::SplitSemanticAndIdx(p._semantic);
						if (Internal::Find(unprovidedAttributes, s) != unprovidedAttributes.end()) {
							// if the function both outputs and inputs the parameter, it's not considered a generator, and so is not useful
							// this is particularly input for some gs system patches which expand an attribute into four
							// without this check we can get infinite loops
							auto i = std::find_if(e.second.GetParameters().begin(), e.second.GetParameters().end(),
								[s](const auto& q) { return q._direction == GraphLanguage::ParameterDirection::In && CompareSemantic(Internal::SplitSemanticAndIdx(q._semantic), s); });
							isUseful = i == e.second.GetParameters().end();
						}
					}
					if (!isUseful) continue;

					// we have to figure out where this step would be added in the order, and find the input attributes available there
					// unfortunately, it's a lot of extra work to make these calculations
					auto insertPt = arranger.CalculateInsertPosition(e.second);
					auto availableInputs = arranger.CalculateAvailableInputsAtStep(insertPt);
					unsigned matchedInputs = 0, unmatchedInputs = 0;
					for (const auto& p:e.second.GetParameters()) {
						if (p._direction != GraphLanguage::ParameterDirection::In) continue;
						auto s = Internal::SplitSemanticAndIdx(p._semantic);
						auto matched = (Internal::Find(availableInputs, s) != availableInputs.end()) || isProvidedFn(s.first, s.second);
						matchedInputs += matched;
						unmatchedInputs += !matched;
					}

					prospectivePatches.emplace_back(ProspectivePatch{matchedInputs, unmatchedInputs, insertPt, e.first, &e.second});
				}

				if (prospectivePatches.empty()) {
					// finished -- system patches cannot improve things further
					break;
				}

				std::stable_sort(
					prospectivePatches.begin(), prospectivePatches.end(),
					[](const auto& lhs, const auto& rhs) { 
						if (lhs._matchedInputs > rhs._matchedInputs) return true;
						if (lhs._matchedInputs < rhs._matchedInputs) return false;
						return lhs._unmatchedInputs < rhs._unmatchedInputs;
					});

				// add the best patch into the list of steps
				auto& winner = *prospectivePatches.begin();
				arranger._steps.insert(arranger._steps.begin()+winner._insertionPt, Internal::FragmentArranger::Step{winner._name, winner._signature});
			}
		}

	}

	constexpr const char* s_vsSystemPatches = R"--(

#include "xleres/TechniqueLibrary/Framework/SystemUniforms.hlsl"
#include "xleres/TechniqueLibrary/Utility/Colour.hlsl"

void LocalToWorld3D(
	out float3 worldPosition : WORLDPOSITION,
	float3 position : POSITION)
{
	worldPosition = position;
}

void WorldToClip3D(
	out float4 clipPosition : SV_Position,
	float3 worldPosition : WORLDPOSITION)
{
	clipPosition = mul(SysUniform_GetWorldToClip(), float4(worldPosition,1));
}

void ColorSRGBToColorLinear(out float4 colorLinear : COLOR, float4 colorSRGB : COLOR_SRGB)
{
	colorLinear.rgb = SRGBToLinear_Formal(colorSRGB.rgb);
	colorLinear.a = colorSRGB.a;
}

)--";

	constexpr const char* s_gsSystemPatches = R"--(

void ExpandClipSpacePosition(
	out float4 pos0 : SV_Position0,
	out float4 pos1 : SV_Position1,
	out float4 pos2 : SV_Position2,
	out float4 pos3 : SV_Position3,
	float4 inputPos : SV_Position,
	float radius : RADIUS,
	float rotation : ROTATION)
{
	const float hradius = radius;
	const float vradius = hradius * (16.f/9.f);		// todo -- proper aspect & radius scaling
	float2 sc; sincos(rotation, sc.x, sc.y);
	float2 h = float2(sc.y, -sc.x);
	float2 v = float2(sc.x, sc.y);
	h.x *= hradius; h.y *= vradius;
	v.x *= hradius; v.y *= vradius;

	pos0 = float4(inputPos.xy + -h-v, inputPos.zw);
	pos1 = float4(inputPos.xy + -h+v, inputPos.zw);
	pos2 = float4(inputPos.xy +  h-v, inputPos.zw);
	pos3 = float4(inputPos.xy +  h+v, inputPos.zw);
}

void ExpandClipSpacePosition(
	out float4 pos0 : SV_Position0,
	out float4 pos1 : SV_Position1,
	out float4 pos2 : SV_Position2,
	out float4 pos3 : SV_Position3,
	float4 inputPos : SV_Position,
	float radius : RADIUS)
{
	const float h = radius;
	const float v = h * (16.f/9.f);		// todo -- proper radius values
	pos0 = float4(inputPos.xy + float2(-h, -v), inputPos.zw);
	pos1 = float4(inputPos.xy + float2(-h, +v), inputPos.zw);
	pos2 = float4(inputPos.xy + float2( h, -v), inputPos.zw);
	pos3 = float4(inputPos.xy + float2( h, +v), inputPos.zw);
}

	)--";


	InstantiatedShader BuildSpritePipeline(const InstantiatedShader& patches, IteratorRange<const std::string*> iaAttributes)
	{
		std::vector<Internal::WorkingAttribute> psEntryAttributes, gsEntryAttributes, vsEntryAttributes;
		auto vsSystemPatches = ShaderSourceParser::ParseHLSL(s_vsSystemPatches);
		auto gsSystemPatches = ShaderSourceParser::ParseHLSL(s_gsSystemPatches);

		std::vector<Internal::FragmentArranger::Step> psSteps, gsSteps, vsSteps;
		{
			{
				Internal::FragmentArranger arranger;
				arranger.AddFragmentOutput(Internal::WorkingAttribute{"SV_Target", 0, "float4"});		// todo -- typing on this
				bool atLeastOnePSStep = false;
				for (unsigned ep=0; ep<patches._entryPoints.size(); ++ep) {
					if (patches._entryPoints[ep]._implementsName == "SV_SpritePS") {
						arranger.AddStep(patches._entryPoints[ep]);
						atLeastOnePSStep = true;
					}
				}
				if (!atLeastOnePSStep)
					Throw(std::runtime_error("Cannot generate sprite pipeline because we must have at least one SV_SpritePS entrypoint"));

				psEntryAttributes = arranger.RebuildInputAttributes();
				Internal::AddPSInputSystemAttributes(psEntryAttributes);
				psSteps = std::move(arranger._steps);
			}

			{
				Internal::FragmentArranger arranger;
				arranger.AddFragmentOutput(Internal::WorkingAttribute{"SV_Position", 0, "float4"});
				arranger.AddFragmentOutput(Internal::WorkingAttribute{"SV_Position", 1, "float4"});
				arranger.AddFragmentOutput(Internal::WorkingAttribute{"SV_Position", 2, "float4"});
				arranger.AddFragmentOutput(Internal::WorkingAttribute{"SV_Position", 3, "float4"});
				for (auto& a:psEntryAttributes) arranger.AddFragmentOutput(a);

				for (unsigned ep=0; ep<patches._entryPoints.size(); ++ep)
					if (patches._entryPoints[ep]._implementsName == "SV_SpriteGS")
						arranger.AddStep(patches._entryPoints[ep]);

				Internal::ConnectSystemPatches(
					arranger, gsSystemPatches,
					[&iaAttributes](StringSection<> semantic, unsigned semanticIdx) {
						return Internal::IsGSInputSystemAttributes(semantic, semanticIdx);
					});

				gsEntryAttributes = arranger.RebuildInputAttributes();
				// Internal::AddGSInputSystemAttributes(gsEntryAttributes);
				gsSteps = std::move(arranger._steps);
			}

			{
				Internal::FragmentArranger arranger;
				for (auto& a:gsEntryAttributes) arranger.AddFragmentOutput(a);

				for (unsigned ep=0; ep<patches._entryPoints.size(); ++ep)
					if (patches._entryPoints[ep]._implementsName == "SV_SpriteVS")
						arranger.AddStep(patches._entryPoints[ep]);

				Internal::ConnectSystemPatches(
					arranger, vsSystemPatches,
					[&iaAttributes](StringSection<> semantic, unsigned semanticIdx) {
						for (auto a:iaAttributes) {
							auto s = Internal::SplitSemanticAndIdx(a);
							if (s.second == semanticIdx && XlEqString(s.first, semantic)) return true;
						}
						return Internal::IsVSInputSystemAttributes(semantic, semanticIdx);
					});
				
				vsEntryAttributes = arranger.RebuildInputAttributes();
				vsSteps = std::move(arranger._steps);
			}
		}

		// Now we have the work through in the opposite direction. We will build the actual fragment function that
		// should perform all of the steps
		//
		// During this phase, we may also need to generate some custom patches for system values and required transformations
		std::stringstream vs, gs;
		GraphLanguage::NodeGraphSignature vsSignature;

		{
			Internal::FragmentWriter writerHelper;
			for (const auto& a:vsEntryAttributes) {
				auto ia = std::find_if(iaAttributes.begin(), iaAttributes.end(),
					[&a](const auto&q) { return Internal::CompareSemantic(a, q); });
				if (ia != iaAttributes.end()) {
					writerHelper.WriteInputParameter(a._semantic, a._semanticIdx, a._type);
				} else {
					TryWriteVSSystemInput(writerHelper, a._semantic, a._semanticIdx);
				}
			}

			for (const auto& step:vsSteps)
				if (step._enabled)
					writerHelper.WriteCall(step._name, *step._signature);

			for (const auto& a:gsEntryAttributes) {
				if (XlBeginsWith(MakeStringSection(a._semantic), "SV_") && !XlEqString(a._semantic, "SV_Position")) continue;

				// If the writer helper never actually got anything for this semantic, it will not become an output
				if (writerHelper.HasAttributeFor(a._semantic, a._semanticIdx))
					writerHelper.WriteOutputParameter(a._semantic, a._semanticIdx, a._type);	// early cast to type expected by gs
			}

			vsSignature = writerHelper.Complete(vs, "VSEntry");
		}

		{
			Internal::FragmentWriter writerHelper;
			for (const auto& a:gsEntryAttributes) {
				auto gsin = std::find_if(vsSignature.GetParameters().begin(), vsSignature.GetParameters().end(),
					[&a](const auto&q) { return Internal::CompareSemantic(a, q); });
				if (gsin != vsSignature.GetParameters().end()) {
					writerHelper.WriteInputParameter(a._semantic, a._semanticIdx, a._type);
				} else {
					TryWriteGSSystemInput(writerHelper, a._semantic, a._semanticIdx);
				}
			}

			for (const auto& step:gsSteps)
				if (step._enabled)
					writerHelper.WriteCall(step._name, *step._signature);

			for (const auto& a:psEntryAttributes) {
				if (XlBeginsWith(MakeStringSection(a._semantic), "SV_") && !XlEqString(a._semantic, "SV_Position")) continue;

				// If the writer helper never actually got anything for this semantic, it will not become an output
				if (writerHelper.HasAttributeFor(a._semantic, a._semanticIdx))
					writerHelper.WriteOutputParameter(a._semantic, a._semanticIdx, a._type);	// early cast to type expected by gs
			}

			writerHelper.Complete(gs, "GSEntry");
		}

		std::cout << vs.str() << std::endl;
		std::cout << gs.str() << std::endl;

		return {};
	}
}
