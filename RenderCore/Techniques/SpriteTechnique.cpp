#include "SpriteTechnique.h"
#include "CompiledShaderPatchCollection.h"
#include "../../ShaderParser/ShaderInstantiation.h"
#include "../../ShaderParser/NodeGraphSignature.h"
#include "../../ShaderParser/ShaderSignatureParser.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/FastParseValue.h"
#include "../../Utility/StringFormat.h"
#include <sstream>


using namespace Utility::Literals;

namespace RenderCore { namespace Techniques
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

		static std::vector<WorkingAttribute>::iterator Find(std::vector<WorkingAttribute>& v, std::pair<StringSection<>, unsigned> s)
		{
			return std::find_if(v.begin(), v.end(), [s](const auto& q) { return q._semanticIdx == s.second && XlEqString(s.first, q._semantic); });
		}

		static WorkingAttribute MakeWorkingAttribute(const GraphLanguage::NodeGraphSignature::Parameter& p)
		{
			auto s = SplitSemanticAndIdx(p._semantic);
			if (s.first.size() == p._semantic.size()) return { p._semantic, 0, p._type };
			return { s.first.AsString(), s.second, p._type };
		}

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
			void WriteInputParameter(std::string semantic, unsigned semanticIdx, std::string type, bool gsInputParameter = false);
			void WriteOutputParameter(std::string semantic, unsigned semanticIdx, std::string type);

			void WriteCall(StringSection<> callName, const GraphLanguage::NodeGraphSignature& sig);
			void WriteGSPredicateCall(StringSection<> callName, const GraphLanguage::NodeGraphSignature& sig);

			GraphLanguage::NodeGraphSignature WriteFragment(std::stringstream& str, StringSection<> name);
			GraphLanguage::NodeGraphSignature WriteGSFragment(std::stringstream& str, StringSection<> name);

			bool HasAttributeFor(StringSection<> semantic, unsigned semanticIdx);

			std::stringstream  _body;

			struct WorkingAttributeWithName : public WorkingAttribute { std::string _name; bool _gsInputParameter; };
			std::vector<WorkingAttributeWithName> _workingAttributes;
			GraphLanguage::NodeGraphSignature _signature;
			unsigned _nextWorkingAttributeIdx = 0;

		private:
			void WriteCallParametersInternal(std::ostream& temp, const GraphLanguage::NodeGraphSignature& sig);
			void WriteCallParameterInternal(std::ostream& temp, const GraphLanguage::NodeGraphSignature::Parameter& p);
		};

		void FragmentWriter::WriteInputParameter(std::string semantic, unsigned semanticIdx, std::string type, bool gsInputParameter)
		{
			assert(SplitSemanticAndIdx(semantic).first.size() == semantic.size());
			auto i = std::find_if(_workingAttributes.begin(), _workingAttributes.end(),
				[semantic, semanticIdx](const auto& q) { return q._semantic == semantic && q._semanticIdx == semanticIdx; });
			if (i != _workingAttributes.end())
				Throw(std::runtime_error("Input attribute " + semantic + " specified multiple times"));

			auto semanticAndIdx = SemanticAndIdx(semantic, semanticIdx);
			auto newName = Concatenate(semantic, "_gen_", std::to_string(_nextWorkingAttributeIdx++));
			_signature.AddParameter({type, newName, GraphLanguage::ParameterDirection::In, semanticAndIdx});
			_workingAttributes.emplace_back(WorkingAttributeWithName{semantic, semanticIdx, type, newName, gsInputParameter});
		}

		void FragmentWriter::WriteOutputParameter(std::string semantic, unsigned semanticIdx, std::string type)
		{
			assert(SplitSemanticAndIdx(semantic).first.size() == semantic.size());

			auto semanticAndIdx = SemanticAndIdx(semantic, semanticIdx);
			auto newName = Concatenate("out_", semantic, "_gen_", std::to_string(_nextWorkingAttributeIdx++));
			_signature.AddParameter({type, newName, GraphLanguage::ParameterDirection::Out, semanticAndIdx});
		}

		static void WriteCastOrAssignExpression(
			std::ostream& str,
			FragmentWriter::WorkingAttributeWithName& attribute,
			const std::string& requiredType)
		{
			if (attribute._type == requiredType) {
				if (attribute._gsInputParameter) str << "input[0].";
				str << attribute._name;
			} else {
				str << "Cast_" << attribute._type << "_to_" << requiredType << "(";
				if (attribute._gsInputParameter) str << "input[0].";
				str << attribute._name;
				str << ")";
			}
		}

		static void WriteDefaultValueExpression(
			std::ostream& str,
			const std::string& requiredType)
		{
			str << "DefaultValue_" << requiredType << "()";
		}

		void FragmentWriter::WriteCallParameterInternal(std::ostream& temp, const GraphLanguage::NodeGraphSignature::Parameter& p)
		{
			auto s = SplitSemanticAndIdx(p._semantic);
			auto i = std::find_if(_workingAttributes.begin(), _workingAttributes.end(), [s](const auto& q) { return s.second == q._semanticIdx && XlEqString(s.first, q._semantic); });
			if (p._direction == GraphLanguage::ParameterDirection::In) {
				if (i != _workingAttributes.end()) {
					WriteCastOrAssignExpression(temp, *i, p._type);
				} else {
					WriteDefaultValueExpression(temp, p._type);
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
				if (i->_gsInputParameter) temp << "input[0].";
				temp << i->_name;
			}
		}

		void FragmentWriter::WriteCallParametersInternal(std::ostream& temp, const GraphLanguage::NodeGraphSignature& sig)
		{
			bool pendingComma = false;
			for (const auto&p:sig.GetParameters()) {
				if (pendingComma) temp << ", ";
				if (p._direction == GraphLanguage::ParameterDirection::Out && XlEqString(p._name, "result")) continue;
				WriteCallParameterInternal(temp, p);
				pendingComma = true;
			}
		}

		void FragmentWriter::WriteCall(StringSection<> callName, const GraphLanguage::NodeGraphSignature& sig)
		{
			std::stringstream temp;
			temp << "\t";

			auto i = std::find_if(sig.GetParameters().begin(), sig.GetParameters().end(), [](const auto& p) { return p._direction == GraphLanguage::ParameterDirection::Out && XlEqString(p._name, "result"); });
			if (i != sig.GetParameters().end()) {
				WriteCallParameterInternal(temp, *i);
				temp << " = ";
			}

			temp << callName << "(";
			WriteCallParametersInternal(temp, sig);
			_body << temp.str() << ");" << std::endl;
		}

		void FragmentWriter::WriteGSPredicateCall(StringSection<> callName, const GraphLanguage::NodeGraphSignature& sig)
		{
			std::stringstream temp;
			temp << "\tif (!" << callName << "(";
			WriteCallParametersInternal(temp, sig);
			_body << temp.str() << ")) return;" << std::endl;
		}

		GraphLanguage::NodeGraphSignature FragmentWriter::WriteFragment(std::stringstream& str, StringSection<> name)
		{
			str << "void " << name << "(";

			bool pendingComma = false;
			for (const auto& p:_signature.GetParameters()) {
				if (pendingComma) str << ", ";
				if (p._direction == GraphLanguage::ParameterDirection::Out) str << "out ";
				str << p._type << " " << p._name << ":" << p._semantic;
				pendingComma = true;
			}
			str << ")" << std::endl << "{" << std::endl;
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

		static const char* s_VSToGS = "VS_TO_GS";
		static const char* s_GSToPS = "GS_TO_PS";

		GraphLanguage::NodeGraphSignature FragmentWriter::WriteGSFragment(std::stringstream& str, StringSection<> name)
		{
			// (note -- some SV_ values might still need to be direct function parameters?)
			str << "struct " << name << "_" << s_VSToGS << std::endl << "{" << std::endl;
			for (const auto& p:_signature.GetParameters()) {
				if (p._direction != GraphLanguage::ParameterDirection::In) continue;
				str << "\t" << p._type << " " << p._name << ":" << p._semantic << ";" << std::endl;
			}
			str << "};" << std::endl << std::endl;

			str << "struct " << name << "_" << s_GSToPS << std::endl << "{" << std::endl;
			for (const auto& p:_signature.GetParameters()) {
				if (p._direction != GraphLanguage::ParameterDirection::Out) continue;
				str << "\t" << p._type << " " << p._name << ":" << p._semantic << ";" << std::endl;
			}
			str << "};" << std::endl << std::endl;

			str << "[maxvertexcount(4)]" << std::endl;
			str << "\tvoid " << name << "(point " << name << "_" << s_VSToGS << " input[1], inout TriangleStream<" << name << "_" << s_GSToPS << "> outputStream)" << std::endl;
			str << "{" << std::endl;
			str << _body.str();

			// write the code that should move values from the working attributes into the output vertices
			for (unsigned vIdx=0; vIdx<4; ++vIdx) {
				str << "\t" << name << "_" << s_GSToPS << " output" << vIdx << ";" << std::endl;
				for (const auto& p:_signature.GetParameters()) {
					if (p._direction != GraphLanguage::ParameterDirection::Out) continue;
					str << "\t" << "output" << vIdx << "." << p._name << " = ";
					// look for the working parameter that matches the semantic (consider cases where we have separate values for each vertex
					auto s = SplitSemanticAndIdx(p._semantic);
					assert(s.second == 0);		// funny things happen if this is not zero
					auto i = std::find_if(_workingAttributes.begin(), _workingAttributes.end(),
						[s, vIdx](const auto& q) { return q._semanticIdx == vIdx && XlEqString(s.first, q._semantic); });
					if (i == _workingAttributes.end() && vIdx != 0)
						i = std::find_if(_workingAttributes.begin(), _workingAttributes.end(),
							[s](const auto& q) { return q._semanticIdx == 0 && XlEqString(s.first, q._semantic); });
					if (i != _workingAttributes.end()) {
						WriteCastOrAssignExpression(str, *i, p._type);
					} else {
						WriteDefaultValueExpression(str, p._type);
					}
					str << ";" << std::endl;
				}
				str << "\toutputStream.Append(output" << vIdx << ");" << std::endl;
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

		static constexpr std::pair<const char*, const char*> s_validPSInputSystemValues[] {
			{"SV_ClipDistance", "float"},		// multiple indices
			{"SV_CullDistance", "float"},		// multiple indices
			{"SV_InstanceID", "uint"},
			{"SV_PrimitiveID", "uint"},
			{"SV_Coverage", "uint"},
			{"SV_InnerCoverage", "uint"},
			{"SV_IsFrontFace", "bool"},
			{"SV_Position", "float4"},
			{"SV_PrimitiveID", "uint"},
			{"SV_RenderTargetArrayIndex", "uint"},
			{"SV_SampleIndex", "uint"},
			{"SV_ViewportArrayIndex", "uint"},
			{"SV_ShadingRate", "uint"}
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

		static bool VSCanProvideAttribute(IteratorRange<const PatchDelegateInput*> patches, StringSection<> semantic, unsigned semanticIdx)
		{
			for (unsigned ep=0; ep<patches.size(); ++ep)
				if (patches[ep]._implementsHash == "SV_SpriteVS"_h)
					for (const auto&p:patches[ep]._signature->GetParameters()) {
						if (p._direction != GraphLanguage::ParameterDirection::Out) continue;
						if (CompareSemantic({semantic, semanticIdx}, SplitSemanticAndIdx(p._semantic)))
							return true;
					}
			return false;
		}

		static bool TryWritePSSystemInput(FragmentWriter& writer, StringSection<> semantic, unsigned semanticIdx)
		{
			if (!XlBeginsWith(semantic, "SV_")) return false;
			for (const auto& q:s_validPSInputSystemValues)
				if (XlEqString(semantic, q.first)) {
					writer.WriteInputParameter(semantic.AsString(), semanticIdx, q.second);
					return true;
				}
			return false;
		}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		class FragmentArranger
		{
		public:
			void AddStep(std::string name, const GraphLanguage::NodeGraphSignature& signature, uint64_t originalPatchCode=0);		// add in reverse order
			void AddFragmentOutput(const WorkingAttribute& a);

			std::vector<WorkingAttribute> RebuildInputAttributes();
			std::vector<WorkingAttribute> CalculateAvailableInputsAtStep(unsigned stepIdx);
			unsigned CalculateInsertPosition(const GraphLanguage::NodeGraphSignature& signature);

			struct Step
			{
				std::string _name;
				const GraphLanguage::NodeGraphSignature* _signature;
				bool _enabled = false;
				uint64_t _originalPatchCode = 0;
			};
			std::vector<Step> _steps;

			std::vector<WorkingAttribute> _fragmentOutput;
		};

		void FragmentArranger::AddStep(std::string name, const GraphLanguage::NodeGraphSignature& signature, uint64_t originalPatchCode)
		{
			_steps.emplace_back(Step{std::move(name), &signature, false, originalPatchCode});
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

float4 PixelCoordToSVPosition(float2 pixelCoord)
{
	// This is a kind of viewport transform -- unfortunately it needs to
	// be customized for vulkan because of the different NDC space
#if (NDC == NDC_POSITIVE_RIGHT_HANDED)
	return float4(	pixelCoord.x * SysUniform_ReciprocalViewportDimensions().x *  2.f - 1.f,
					pixelCoord.y * SysUniform_ReciprocalViewportDimensions().y *  2.f - 1.f,
					0.f, 1.f);
#elif (NDC == NDC_POSITIVE_RIGHT_HANDED_REVERSEZ)
	return float4(	pixelCoord.x * SysUniform_ReciprocalViewportDimensions().x *  2.f - 1.f,
					pixelCoord.y * SysUniform_ReciprocalViewportDimensions().y *  2.f - 1.f,
					1.f, 1.f);
#elif (NDC == NDC_POSITIVE_REVERSEZ)
	return float4(	pixelCoord.x * SysUniform_ReciprocalViewportDimensions().x *  2.f - 1.f,
					pixelCoord.y * SysUniform_ReciprocalViewportDimensions().y * -2.f + 1.f,
					1.f, 1.f);
#else
	return float4(	pixelCoord.x * SysUniform_ReciprocalViewportDimensions().x *  2.f - 1.f,
					pixelCoord.y * SysUniform_ReciprocalViewportDimensions().y * -2.f + 1.f,
					0.f, 1.f);
#endif
}

void PixelPositionOutput(
	out float4 clipPosition : SV_Position,
	float2 pixelPosition : PIXELPOSITION)
{
	clipPosition = PixelCoordToSVPosition(pixelPosition);
}

void ColorSRGBToColorLinear(out float4 colorLinear : COLOR, float4 colorSRGB : COLOR_SRGB)
{
	colorLinear.rgb = SRGBToLinear_Formal(colorSRGB.rgb);
	colorLinear.a = colorSRGB.a;
}

)--";

	constexpr const char* s_gsSpriteSystemPatches = R"--(

#include "xleres/TechniqueLibrary/Framework/SystemUniforms.hlsl"

void ExpandClipSpacePosition(
	out float4 pos0 : SV_Position0,
	out float4 pos1 : SV_Position1,
	out float4 pos2 : SV_Position2,
	out float4 pos3 : SV_Position3,
	float4 inputPos : SV_Position,
	float radius : RADIUS,
	float rotation : ROTATION)
{
	const float hradius = radius * SysUniform_GetMinimalProjection()[0];
	const float vradius = radius * -SysUniform_GetMinimalProjection()[1];
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
	const float h = radius * SysUniform_GetMinimalProjection()[0];
	const float v = radius * -SysUniform_GetMinimalProjection()[1];
	pos0 = float4(inputPos.xy + float2(-h, -v), inputPos.zw);
	pos1 = float4(inputPos.xy + float2(-h, +v), inputPos.zw);
	pos2 = float4(inputPos.xy + float2( h, -v), inputPos.zw);
	pos3 = float4(inputPos.xy + float2( h, +v), inputPos.zw);
}

	)--";


	std::vector<PatchDelegateOutput> BuildSpritePipeline(IteratorRange<const PatchDelegateInput*> patches, IteratorRange<const uint64_t*> iaAttributes)
	{
		std::vector<Internal::WorkingAttribute> psEntryAttributes, gsEntryAttributes, vsEntryAttributes;
		auto vsSystemPatches = ShaderSourceParser::ParseHLSL(s_vsSystemPatches);
		auto gsSystemPatches = ShaderSourceParser::ParseHLSL(s_gsSpriteSystemPatches);

		std::vector<Internal::FragmentArranger::Step> psSteps, gsSteps, vsSteps;
		{
			{
				Internal::FragmentArranger arranger;
				arranger.AddFragmentOutput(Internal::WorkingAttribute{"SV_Target", 0, "float4"});		// todo -- typing on this
				bool atLeastOnePSStep = false;
				for (unsigned ep=0; ep<patches.size(); ++ep) {
					if (patches[ep]._implementsHash == "SV_SpritePS"_h) {
						arranger.AddStep(patches[ep]._name, *patches[ep]._signature, patches[ep]._implementsHash);
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

				for (unsigned ep=0; ep<patches.size(); ++ep)
					if (patches[ep]._implementsHash == "SV_SpriteGSPredicate"_h)
						arranger.AddStep(patches[ep]._name, *patches[ep]._signature, patches[ep]._implementsHash);

				for (unsigned ep=0; ep<patches.size(); ++ep)
					if (patches[ep]._implementsHash == "SV_SpriteGS"_h)
						arranger.AddStep(patches[ep]._name, *patches[ep]._signature, patches[ep]._implementsHash);

				Internal::ConnectSystemPatches(
					arranger, gsSystemPatches,
					[patches](StringSection<> semantic, unsigned semanticIdx) {
						return Internal::IsGSInputSystemAttributes(semantic, semanticIdx)
							|| Internal::VSCanProvideAttribute(patches, semantic, semanticIdx);
					});

				gsEntryAttributes = arranger.RebuildInputAttributes();
				gsSteps = std::move(arranger._steps);
			}

			{
				Internal::FragmentArranger arranger;
				for (auto& a:gsEntryAttributes) arranger.AddFragmentOutput(a);

				for (unsigned ep=0; ep<patches.size(); ++ep)
					if (patches[ep]._implementsHash == "SV_SpriteVS"_h)
						arranger.AddStep(patches[ep]._name, *patches[ep]._signature, patches[ep]._implementsHash);

				Internal::ConnectSystemPatches(
					arranger, vsSystemPatches,
					[&iaAttributes](StringSection<> semantic, unsigned semanticIdx) {
						auto ia = std::find(iaAttributes.begin(), iaAttributes.end(), Hash64(semantic)+semanticIdx);
						if (ia != iaAttributes.end()) return true;
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
		std::stringstream vs, gs, ps;
		GraphLanguage::NodeGraphSignature vsSignature, gsSignature, psSignature;

		{
			Internal::FragmentWriter writerHelper;
			for (const auto& a:vsEntryAttributes) {
				auto ia = std::find(iaAttributes.begin(), iaAttributes.end(), Hash64(a._semantic)+a._semanticIdx);
				if (ia != iaAttributes.end()) {
					writerHelper.WriteInputParameter(a._semantic, a._semanticIdx, a._type);
				} else {
					Internal::TryWriteVSSystemInput(writerHelper, a._semantic, a._semanticIdx);
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

			vsSignature = writerHelper.WriteFragment(vs, "VSEntry");
		}

		{
			Internal::FragmentWriter writerHelper;
			for (const auto& a:gsEntryAttributes) {
				auto gsin = std::find_if(vsSignature.GetParameters().begin(), vsSignature.GetParameters().end(),
					[&a](const auto&q) { return Internal::CompareSemantic(a, q); });
				if (gsin != vsSignature.GetParameters().end()) {
					writerHelper.WriteInputParameter(a._semantic, a._semanticIdx, a._type, true);
				} else {
					Internal::TryWriteGSSystemInput(writerHelper, a._semantic, a._semanticIdx);
				}
			}

			for (auto& step:gsSteps)
				if (step._originalPatchCode == "SV_SpriteGSPredicate"_h) {
					step._enabled = true;		// force it on
					writerHelper.WriteGSPredicateCall(step._name, *step._signature);
				} else if (step._enabled)
					writerHelper.WriteCall(step._name, *step._signature);

			for (const auto& a:psEntryAttributes) {
				if (XlBeginsWith(MakeStringSection(a._semantic), "SV_") && !XlEqString(a._semantic, "SV_Position")) continue;

				// If the writer helper never actually got anything for this semantic, it will not become an output
				if (writerHelper.HasAttributeFor(a._semantic, a._semanticIdx))
					writerHelper.WriteOutputParameter(a._semantic, a._semanticIdx, a._type);	// early cast to type expected by gs
			}

			// GS signature isn't strictly the signature of a particular function, but contains the members of the
			// vertex input and output structures
			gsSignature = writerHelper.WriteGSFragment(gs, "GSEntry");
		}

		{
			Internal::FragmentWriter writerHelper;
			for (const auto& a:psEntryAttributes) {
				auto gsin = std::find_if(gsSignature.GetParameters().begin(), gsSignature.GetParameters().end(),
					[&a](const auto&q) { return Internal::CompareSemantic(a, q); });
				if (gsin != gsSignature.GetParameters().end()) {
					writerHelper.WriteInputParameter(a._semantic, a._semanticIdx, a._type);
				} else {
					Internal::TryWritePSSystemInput(writerHelper, a._semantic, a._semanticIdx);
				}
			}

			std::vector<Internal::WorkingAttribute> psOutputAttributes;
			for (const auto& step:psSteps)
				if (step._enabled) {
					writerHelper.WriteCall(step._name, *step._signature);

					// any SV_ values that are actually written one of the patches are considered
					// outputs of the final fragment shader
					for (const auto& p:step._signature->GetParameters()) {
						if (p._direction != GraphLanguage::ParameterDirection::Out) continue;
						if (!XlBeginsWith(MakeStringSection(p._semantic), "SV_")) continue;
						auto existing = Internal::Find(psOutputAttributes, Internal::SplitSemanticAndIdx(p._semantic));
						if (existing != psOutputAttributes.end()) {
							existing->_type = p._type;
						} else {
							psOutputAttributes.emplace_back(Internal::MakeWorkingAttribute(p));
						}
					}
				}

			for (const auto& a:psOutputAttributes)
				if (writerHelper.HasAttributeFor(a._semantic, a._semanticIdx))
					writerHelper.WriteOutputParameter(a._semantic, a._semanticIdx, a._type);

			psSignature = writerHelper.WriteFragment(ps, "PSEntry");
		}

		PatchDelegateOutput vsOutput, gsOutput, psOutput;
		vsOutput._stage = ShaderStage::Vertex;
		vsOutput._resource._postPatchesFragments.emplace_back(s_vsSystemPatches);
		vsOutput._resource._postPatchesFragments.emplace_back(vs.str());
		vsOutput._resource._entrypoint._entryPoint = "VSEntry";
		vsOutput._resource._entrypoint._shaderModel = s_SMVS;
		vsOutput._entryPointSignature = std::make_unique<GraphLanguage::NodeGraphSignature>(vsSignature);
		for (auto& s:vsSteps) if (s._enabled && s._originalPatchCode) vsOutput._resource._patchCollectionExpansions.emplace_back(s._originalPatchCode);

		gsOutput._stage = ShaderStage::Geometry;
		gsOutput._resource._postPatchesFragments.emplace_back(s_gsSpriteSystemPatches);
		gsOutput._resource._postPatchesFragments.emplace_back(gs.str());
		gsOutput._resource._entrypoint._entryPoint = "GSEntry";
		gsOutput._resource._entrypoint._shaderModel = s_SMGS;
		for (auto& s:gsSteps) if (s._enabled && s._originalPatchCode) gsOutput._resource._patchCollectionExpansions.emplace_back(s._originalPatchCode);

		psOutput._stage = ShaderStage::Pixel;
		psOutput._resource._postPatchesFragments.emplace_back(ps.str());
		psOutput._resource._entrypoint._entryPoint = "PSEntry";
		psOutput._resource._entrypoint._shaderModel = s_SMPS;
		psOutput._entryPointSignature = std::make_unique<GraphLanguage::NodeGraphSignature>(psSignature);
		for (auto& s:psSteps) if (s._enabled && s._originalPatchCode) psOutput._resource._patchCollectionExpansions.emplace_back(s._originalPatchCode);

		std::vector<PatchDelegateOutput> result;
		result.emplace_back(std::move(vsOutput));
		result.emplace_back(std::move(psOutput));
		result.emplace_back(std::move(gsOutput));
		return result;
	}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	std::vector<PatchDelegateOutput> BuildAutoPipeline(IteratorRange<const PatchDelegateInput*> patches, IteratorRange<const uint64_t*> iaAttributes)
	{
		std::vector<Internal::WorkingAttribute> psEntryAttributes, vsEntryAttributes;
		auto vsSystemPatches = ShaderSourceParser::ParseHLSL(s_vsSystemPatches);

		std::vector<Internal::FragmentArranger::Step> psSteps, gsSteps, vsSteps;
		{
			{
				Internal::FragmentArranger arranger;
				arranger.AddFragmentOutput(Internal::WorkingAttribute{"SV_Target", 0, "float4"});		// todo -- typing on this
				bool atLeastOnePSStep = false;
				for (unsigned ep=0; ep<patches.size(); ++ep) {
					if (patches[ep]._implementsHash == "SV_AutoPS"_h) {
						arranger.AddStep(patches[ep]._name, *patches[ep]._signature, patches[ep]._implementsHash);
						atLeastOnePSStep = true;
					}
				}
				if (!atLeastOnePSStep)
					Throw(std::runtime_error("Cannot generate auto pipeline because we must have at least one SV_AutoPS entrypoint"));

				psEntryAttributes = arranger.RebuildInputAttributes();
				Internal::AddPSInputSystemAttributes(psEntryAttributes);
				psSteps = std::move(arranger._steps);
			}

			{
				Internal::FragmentArranger arranger;
				for (auto& a:psEntryAttributes) arranger.AddFragmentOutput(a);

				for (unsigned ep=0; ep<patches.size(); ++ep)
					if (patches[ep]._implementsHash == "SV_AutoVS"_h)
						arranger.AddStep(patches[ep]._name, *patches[ep]._signature, patches[ep]._implementsHash);

				Internal::ConnectSystemPatches(
					arranger, vsSystemPatches,
					[&iaAttributes](StringSection<> semantic, unsigned semanticIdx) {
						auto ia = std::find(iaAttributes.begin(), iaAttributes.end(), Hash64(semantic)+semanticIdx);
						if (ia != iaAttributes.end()) return true;
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
		std::stringstream vs, ps;
		GraphLanguage::NodeGraphSignature vsSignature, psSignature;

		{
			Internal::FragmentWriter writerHelper;
			for (const auto& a:vsEntryAttributes) {
				auto ia = std::find(iaAttributes.begin(), iaAttributes.end(), Hash64(a._semantic)+a._semanticIdx);
				if (ia != iaAttributes.end()) {
					writerHelper.WriteInputParameter(a._semantic, a._semanticIdx, a._type);
				} else {
					Internal::TryWriteVSSystemInput(writerHelper, a._semantic, a._semanticIdx);
				}
			}

			for (const auto& step:vsSteps)
				if (step._enabled)
					writerHelper.WriteCall(step._name, *step._signature);

			for (const auto& a:psEntryAttributes) {
				if (XlBeginsWith(MakeStringSection(a._semantic), "SV_") && !XlEqString(a._semantic, "SV_Position")) continue;

				// If the writer helper never actually got anything for this semantic, it will not become an output
				if (writerHelper.HasAttributeFor(a._semantic, a._semanticIdx))
					writerHelper.WriteOutputParameter(a._semantic, a._semanticIdx, a._type);	// early cast to type expected by gs
			}

			vsSignature = writerHelper.WriteFragment(vs, "VSEntry");
		}

		{
			Internal::FragmentWriter writerHelper;
			for (const auto& a:psEntryAttributes) {
				auto gsin = std::find_if(vsSignature.GetParameters().begin(), vsSignature.GetParameters().end(),
					[&a](const auto&q) { return Internal::CompareSemantic(a, q); });
				if (gsin != vsSignature.GetParameters().end()) {
					writerHelper.WriteInputParameter(a._semantic, a._semanticIdx, a._type);
				} else {
					Internal::TryWritePSSystemInput(writerHelper, a._semantic, a._semanticIdx);
				}
			}

			std::vector<Internal::WorkingAttribute> psOutputAttributes;
			for (const auto& step:psSteps)
				if (step._enabled) {
					writerHelper.WriteCall(step._name, *step._signature);

					// any SV_ values that are actually written one of the patches are considered
					// outputs of the final fragment shader
					for (const auto& p:step._signature->GetParameters()) {
						if (p._direction != GraphLanguage::ParameterDirection::Out) continue;
						if (!XlBeginsWith(MakeStringSection(p._semantic), "SV_")) continue;
						auto existing = Internal::Find(psOutputAttributes, Internal::SplitSemanticAndIdx(p._semantic));
						if (existing != psOutputAttributes.end()) {
							existing->_type = p._type;
						} else {
							psOutputAttributes.emplace_back(Internal::MakeWorkingAttribute(p));
						}
					}
				}

			for (const auto& a:psOutputAttributes)
				if (writerHelper.HasAttributeFor(a._semantic, a._semanticIdx))
					writerHelper.WriteOutputParameter(a._semantic, a._semanticIdx, a._type);

			psSignature = writerHelper.WriteFragment(ps, "PSEntry");
		}

		PatchDelegateOutput vsOutput, psOutput;
		vsOutput._stage = ShaderStage::Vertex;
		vsOutput._resource._postPatchesFragments.emplace_back(s_vsSystemPatches);
		vsOutput._resource._postPatchesFragments.emplace_back(vs.str());
		vsOutput._resource._entrypoint._entryPoint = "VSEntry";
		vsOutput._resource._entrypoint._shaderModel = s_SMVS;
		vsOutput._entryPointSignature = std::make_unique<GraphLanguage::NodeGraphSignature>(vsSignature);
		for (auto& s:vsSteps) if (s._enabled && s._originalPatchCode) vsOutput._resource._patchCollectionExpansions.emplace_back(s._originalPatchCode);

		psOutput._stage = ShaderStage::Pixel;
		psOutput._resource._postPatchesFragments.emplace_back(ps.str());
		psOutput._resource._entrypoint._entryPoint = "PSEntry";
		psOutput._resource._entrypoint._shaderModel = s_SMPS;
		psOutput._entryPointSignature = std::make_unique<GraphLanguage::NodeGraphSignature>(psSignature);
		for (auto& s:psSteps) if (s._enabled && s._originalPatchCode) psOutput._resource._patchCollectionExpansions.emplace_back(s._originalPatchCode);

		std::vector<PatchDelegateOutput> result;
		result.emplace_back(std::move(vsOutput));
		result.emplace_back(std::move(psOutput));
		return result;
	}

}}
