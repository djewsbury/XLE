// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ShaderReflection.h"
#include "../../Types.h"

#define HAS_SPIRV_HEADERS
#if defined(HAS_SPIRV_HEADERS)

#include "../../../OSServices/Log.h"
#include "../../../Utility/MemoryUtils.h"
#include "../../../Utility/StringFormat.h"
#include "../../../Utility/FastParseValue.h"

// Vulkan SDK includes -- 
#pragma push_macro("new")
#undef new
#define ENABLE_OPT 1
#include "glslang/SPIRV/spirv.hpp"
#include "glslang/SPIRV/SpvTools.h"
#pragma pop_macro("new")

namespace RenderCore { namespace Metal_Vulkan
{

    struct ShaderSemantic { StringSection<> _name; unsigned _index = 0; };
    static ShaderSemantic MakeShaderSemantic(StringSection<> input)
    {
        auto name = input;

        // Our shader path prepends "in_" infront of the semantic name
        // when generating a variable name. Remove it before we make a hash.
        // Alternatively, the HLSL -> spirv compiler prepends "in.var.", and we should remove that
        if (XlBeginsWith(name, MakeStringSection("in_")))
            name._start += 3;
        else if (XlBeginsWith(name, MakeStringSection("in.var.")))
            name._start += 7;
        else if (XlBeginsWith(name, MakeStringSection("out.var.")))
            name._start += 8;
        while (!name.IsEmpty() && isdigit(*(name._end-1)))
            --name._end;

        unsigned index = 0;
        if (name.end() != input.end())
            FastParseValue(MakeStringSection(name.end(), input.end()), index);
        return {name, index};
    }

    std::vector<uint32_t> PatchUpStreamOutput(
        IteratorRange<const void*> byteCode,
        const StreamOutputInitializers& soInit)
    {
        using namespace spv;
        std::vector<uint32_t> byteCode2 {(const uint32_t*)byteCode.begin(), (const uint32_t*)byteCode.end()};

        auto elements = NormalizeInputAssembly(soInit._outputElements);
        struct VariableBinding
        {
            uint32_t _variable;
            InputElementDesc* _eleDesc;
            bool _hasBeenDecorated;
        };
        std::vector<VariableBinding> bindings;

        bool wroteExecuteMode = false;
        auto bci = byteCode2.begin() + 5;
        while (bci < byteCode2.end()) {
            unsigned int firstWord = *bci;
            unsigned wordCount = firstWord >> WordCountShift;
            Op opCode = (Op)(firstWord & OpCodeMask);
            auto paramStart = bci+1;
            bci += wordCount;

            if (opCode == OpName) {
                // Lookup the name in the list of vertex outputs we're expecting
                if (XlBeginsWith(MakeStringSection((const char*)&paramStart[1]), "out.var.")) {
                    auto sem = MakeShaderSemantic(MakeStringSection((const char*)&paramStart[1]));
                    for (auto e=elements.begin(); e!=elements.end(); ++e) {
                        if (XlEqString(sem._name, e->_semanticName) && sem._index == e->_semanticIndex) {
                            bindings.push_back({paramStart[0], AsPointer(e), false});
                        }
                    }
                }
            } /*else if (opCode == OpEmitVertex) {
                // To change OpEmitVertex to emit a vertex to a specific stream:
                assert(wordCount == 1);
                *(paramStart-1) = OpEmitStreamVertex | (2u<<WordCountShift);
                bci = 1+byteCode2.insert(bci, 5);       // 5 here must be a constant "int" type with the stream index
            } */else if (opCode == OpCapability) {
                assert(wordCount == 2);
                bci = 1+byteCode2.insert(bci, OpCapability | (2u<<WordCountShift));
                bci = 1+byteCode2.insert(bci, CapabilityTransformFeedback);
                // bci = 1+byteCode2.insert(bci, OpCapability | (2u<<WordCountShift));
                // bci = 1+byteCode2.insert(bci, CapabilityGeometryStreams);
            } else if (opCode == OpExecutionMode && !wroteExecuteMode) {
                bci -= wordCount;
                auto entry = bci[1];
                bci = 1+byteCode2.insert(bci, OpExecutionMode | (3u<<WordCountShift));
                bci = 1+byteCode2.insert(bci, entry);
                bci = 1+byteCode2.insert(bci, ExecutionModeXfb);
                wroteExecuteMode = true;
            } else if (opCode == OpDecorate) {
                for (auto& b:bindings) {
                    if (b._variable != paramStart[0] || b._hasBeenDecorated) continue;

                    auto varToDecorate = paramStart[0];
                    bci = 1+byteCode2.insert(bci, OpDecorate | (4u<<WordCountShift));
                    bci = 1+byteCode2.insert(bci, varToDecorate);
                    bci = 1+byteCode2.insert(bci, DecorationXfbBuffer);
                    bci = 1+byteCode2.insert(bci, b._eleDesc->_inputSlot);

                    bci = 1+byteCode2.insert(bci, OpDecorate | (4u<<WordCountShift));
                    bci = 1+byteCode2.insert(bci, varToDecorate);
                    bci = 1+byteCode2.insert(bci, DecorationXfbStride);
                    assert(b._eleDesc->_inputSlot < soInit._outputBufferStrides.size());
                    bci = 1+byteCode2.insert(bci, soInit._outputBufferStrides[b._eleDesc->_inputSlot]);

                    bci = 1+byteCode2.insert(bci, OpDecorate | (4u<<WordCountShift));
                    bci = 1+byteCode2.insert(bci, varToDecorate);
                    bci = 1+byteCode2.insert(bci, DecorationOffset);
                    assert(b._eleDesc->_alignedByteOffset != ~0u);
                    bci = 1+byteCode2.insert(bci, b._eleDesc->_alignedByteOffset);

                    /*  If there are multiple streams, you can configure them here:
                    bci = 1+byteCode2.insert(bci, OpDecorate | (4u<<WordCountShift));
                    bci = 1+byteCode2.insert(bci, varToDecorate);
                    bci = 1+byteCode2.insert(bci, DecorationStream);
                    bci = 1+byteCode2.insert(bci, 0);*/

                    b._hasBeenDecorated = true;
                }
            }
        }

        return byteCode2;
    }

    template<typename Id>
        void FillInBinding(
            std::vector<std::pair<Id, SPIRVReflection::Binding>>& bindings,
            Id id,
            spv::Decoration decorationType,
            const unsigned* params, unsigned paramCount)
        {
            if (    decorationType == spv::DecorationBinding
                ||  decorationType == spv::DecorationDescriptorSet
                ||  decorationType == spv::DecorationLocation
                ||  decorationType == spv::DecorationOffset
                ||  decorationType == spv::DecorationInputAttachmentIndex) {

                if (paramCount < 1) return;

                auto i = LowerBound(bindings, id);
                if (i == bindings.end() || i->first != id)
                    i = bindings.insert(i, std::make_pair(id, SPIRVReflection::Binding()));

                switch (decorationType) {
                case spv::DecorationBinding: i->second._bindingPoint = params[0]; break;
                case spv::DecorationDescriptorSet: i->second._descriptorSet = params[0]; break;
                case spv::DecorationLocation: i->second._location = params[0]; break;
                case spv::DecorationOffset: i->second._offset = params[0]; break;
                case spv::DecorationInputAttachmentIndex: i->second._inputAttachmentIndex = params[0]; break;
                default: break;
                }
            }
        }

    static SPIRVReflection::StorageType AsStorageType(unsigned type)
    {
        switch (type)
        {
        case 0:  return SPIRVReflection::StorageType::UniformConstant;
        case 1:  return SPIRVReflection::StorageType::Input;
        case 2:  return SPIRVReflection::StorageType::Uniform;
        case 3:  return SPIRVReflection::StorageType::Output;
        case 4:  return SPIRVReflection::StorageType::Workgroup;
        case 5:  return SPIRVReflection::StorageType::CrossWorkgroup;
        case 6:  return SPIRVReflection::StorageType::Private;
        case 7:  return SPIRVReflection::StorageType::Function;
        case 8:  return SPIRVReflection::StorageType::Generic;
        case 9:  return SPIRVReflection::StorageType::PushConstant;
        case 10: return SPIRVReflection::StorageType::AtomicCounter;
        case 11: return SPIRVReflection::StorageType::Image;
        default: return SPIRVReflection::StorageType::Unknown;
        }
    }

    SPIRVReflection::SPIRVReflection(IteratorRange<const void*> byteCode)
    {
        /*std::vector<unsigned int> spirv { (unsigned*)byteCode.begin(), (unsigned*)byteCode.end() };
        glslang::SpirvToolsDisassemble(Log(Warning), spirv);
        Log(Warning) << std::endl;*/

        _entryPoint._id = ~0x0u;

        using namespace spv;
        // spv::Parameterize();

        auto* bci = ((const uint32_t*)byteCode.begin()) + 5;
        while (bci < byteCode.end()) {
            // Instruction wordCount and opcode
            unsigned int firstWord = *bci;
            unsigned wordCount = firstWord >> WordCountShift;
            Op opCode = (Op)(firstWord & OpCodeMask);
            auto paramStart = bci+1;
            bci += wordCount;

            switch (opCode) {
            case OpMemberName:
                // InstructionDesc[OpMemberName].operands.push(OperandId, "'Type'");
                // InstructionDesc[OpMemberName].operands.push(OperandLiteralNumber, "'Member'");
                // InstructionDesc[OpMemberName].operands.push(OperandLiteralString, "'Name'");
                {
                    if (((const char*)&paramStart[2])[0]) {
                        MemberId id(paramStart[0], paramStart[1]);
                        auto i = LowerBound(_memberNames, id);
                        if (i == _memberNames.end() || i->first != id)
                            i = _memberNames.insert(i, std::make_pair(id, Name()));
                        i->second = MakeStringSection((const char*)&paramStart[2]);
                    }
                    break;
                }

            case OpName:
                // InstructionDesc[OpName].operands.push(OperandId, "'Target'");
                // InstructionDesc[OpName].operands.push(OperandLiteralString, "'Name'");
                {
                    auto type = paramStart[0];
                    auto i = LowerBound(_names, type);
                    if (i == _names.end() || i->first != type)
                        i = _names.insert(i, std::make_pair(type, Name()));
                    i->second = MakeStringSection((const char*)&paramStart[1]);
                    break;
                }

            case OpDecorate:
                // InstructionDesc[OpDecorate].operands.push(OperandId, "'Target'");
                // InstructionDesc[OpDecorate].operands.push(OperandDecoration, "");
                // InstructionDesc[OpDecorate].operands.push(OperandVariableLiterals, "See <<Decoration,'Decoration'>>.");
                {
                    auto targetId = (Id)paramStart[0];
                    auto decorationType = (spv::Decoration)paramStart[1];
                    FillInBinding(_bindings, targetId, decorationType, &paramStart[2], wordCount-3);
                    break;
                }

            case OpMemberDecorate:
                // InstructionDesc[OpMemberDecorate].operands.push(OperandId, "'Structure Type'");
                // InstructionDesc[OpMemberDecorate].operands.push(OperandLiteralNumber, "'Member'");
                // InstructionDesc[OpMemberDecorate].operands.push(OperandDecoration, "");
                // InstructionDesc[OpMemberDecorate].operands.push(OperandVariableLiterals, "See <<Decoration,'Decoration'>>.");
                {
                    MemberId id(paramStart[0], paramStart[1]);
                    auto decorationType = (spv::Decoration)paramStart[2];
                    FillInBinding(_memberBindings, id, decorationType, &paramStart[3], wordCount-4);
                    break;
                }

            case OpEntryPoint:
                // InstructionDesc[OpEntryPoint].operands.push(OperandExecutionModel, "");
                // InstructionDesc[OpEntryPoint].operands.push(OperandId, "'Entry Point'");
                // InstructionDesc[OpEntryPoint].operands.push(OperandLiteralString, "'Name'");
                // InstructionDesc[OpEntryPoint].operands.push(OperandVariableIds, "'Interface'");
                {
                    assert(_entryPoint._name.IsEmpty() && _entryPoint._interface.empty());
                    auto executionModel = paramStart[0]; (void)executionModel;
                    _entryPoint._id = paramStart[1];
                    _entryPoint._name = (const char*)&paramStart[2];

                    auto nameEnd = XlStringEnd((const char*)&paramStart[2]);
                    auto len = nameEnd+1-(const char*)&paramStart[2];
                    if (len%4 != 0) len += 4-(len%4);
                    auto* interfaceStart = PtrAdd(&paramStart[2], len);
                    auto interfaceEnd = &paramStart[wordCount-1];
                    _entryPoint._interface = std::vector<ObjectId>(interfaceStart, interfaceEnd);
                    break;
                }

            case OpConstant:
                {
                    auto i = std::find_if(_basicTypes.begin(), _basicTypes.end(), [q=paramStart[0]](auto c) { return c.first == q; });
                    if (i!=_basicTypes.end() && i->second == BasicType::Int)
                        _integerConstants.push_back(std::make_pair(paramStart[1], paramStart[2]));
                }
                break;

            case OpTypeBool:
                _basicTypes.push_back(std::make_pair(paramStart[0], BasicType::Bool));
                break;

            case OpTypeFloat:  
                _basicTypes.push_back(std::make_pair(paramStart[0], BasicType::Float));
                break;

            case OpTypeInt:  
                _basicTypes.push_back(std::make_pair(paramStart[0], BasicType::Int));
                break;

            case OpTypeVector:  
                _vectorTypes.push_back(std::make_pair(paramStart[0], VectorType{paramStart[1], paramStart[2]}));
                break;

            case OpTypeSampler:
                _basicTypes.push_back(std::make_pair(paramStart[0], BasicType::Sampler));
                break;

            case OpTypeSampledImage:
                _basicTypes.push_back(std::make_pair(paramStart[0], BasicType::SampledImage));
                break;

            case OpTypeImage:
                if (paramStart[6] == 2) {
                    if (paramStart[2] == 5) {
                        _basicTypes.push_back(std::make_pair(paramStart[0], BasicType::StorageTexelBuffer));
                    } else
                        _basicTypes.push_back(std::make_pair(paramStart[0], BasicType::StorageImage));
                } else {
                    if (paramStart[2] == 5) {
                        _basicTypes.push_back(std::make_pair(paramStart[0], BasicType::TexelBuffer));
                    } else
                        _basicTypes.push_back(std::make_pair(paramStart[0], BasicType::Image));
                }
                break;

            case OpTypeStruct:
                _structTypes.push_back(paramStart[0]);
                break;

            case OpTypePointer:  
                _pointerTypes.push_back(std::make_pair(paramStart[0], PointerType{paramStart[2], AsStorageType(paramStart[1])}));
                break;

            case OpTypeArray:
                {
                    unsigned elementCount = 1;
                    auto i = std::find_if(_integerConstants.begin(), _integerConstants.end(), [q=paramStart[2]](auto c) { return c.first == q; });
                    if (i!=_integerConstants.end())
                        elementCount = i->second;
                    _arrayTypes.push_back(std::make_pair(paramStart[0], ArrayType{paramStart[1], elementCount}));
                }
                break;

            case OpVariable:
                if (wordCount > 3)
                    _variables.push_back(std::make_pair(paramStart[1], Variable{paramStart[0], AsStorageType(paramStart[2])}));
                break;

            default:
                break;
            }
        }

        // Our tables should be in near-sorted order, but are not guaranteed to be sorted.
        // So we have to sort here. Since they are near-sorted, quick sort is not ideal, but
        // 
        std::sort(_basicTypes.begin(), _basicTypes.end(), CompareFirst<ObjectId, BasicType>());
        std::sort(_vectorTypes.begin(), _vectorTypes.end(), CompareFirst<ObjectId, VectorType>());
        std::sort(_pointerTypes.begin(), _pointerTypes.end(), CompareFirst<ObjectId, PointerType>());
        std::sort(_arrayTypes.begin(), _arrayTypes.end(), CompareFirst<ObjectId, ArrayType>());
        std::sort(_variables.begin(), _variables.end(), CompareFirst<ObjectId, Variable>());

        // build the quick lookup table, which matches hash names to binding values
        for (auto& b:_bindings) {
            const auto& binding = b.second;
            auto bindingName = b.first;
            if (binding._descriptorSet==~0x0u && binding._bindingPoint==~0x0u) continue;

            // We can bind to the name of the variable, or the name of the type. This is
            // important for our HLSL path for constant buffers.
            // In that case, we get a dummy name for the variable, and the important name
            // is actually the name of the type.
            // Constant buffers become a pointer to a struct (where the struct has the name we want),
            // and the actual variable just has an empty name.
            auto n = LowerBound(_names, bindingName);
            if (n != _names.end() && n->first == bindingName) {
                auto nameStart = n->second.begin();
                auto nameEnd = n->second.end();
                if (nameStart < nameEnd)
                    _uniformQuickLookup.push_back(std::make_pair(Hash64(nameStart, nameEnd), binding));
            }

            // now insert the type name into the quick lookup table ---
            auto v = LowerBound(_variables, bindingName);
            if (v != _variables.end() && v->first == bindingName) {
                auto type = DecayType(v->second._type);                
                n = LowerBound(_names, type);
                if (n != _names.end() && n->first == type) {
                    auto nameStart = n->second.begin();
                    auto nameEnd = n->second.end();
                    if (nameStart < nameEnd)
                        _uniformQuickLookup.push_back(std::make_pair(Hash64(nameStart, nameEnd), binding));
                } 
            }
        }

        std::sort(
            _uniformQuickLookup.begin(), _uniformQuickLookup.end(),
            CompareFirst<uint64_t, Binding>());

        // build the quick lookup table for the input interface
        for (auto i:_entryPoint._interface) {
            auto v = LowerBound(_variables, i);
            if (v == _variables.end() || v->first != i) continue;
            if (v->second._storage != StorageType::Input) continue;

            auto b = LowerBound(_bindings, v->first);
            if (b == _bindings.end() || b->first != v->first) continue;

            const char* nameStart = nullptr, *nameEnd = nullptr;

            auto n = LowerBound(_names, v->first);
            if (n != _names.end() && n->first == v->first) {
                nameStart = n->second.begin();
                nameEnd = n->second.end();
            } 
            
            if (nameStart == nameEnd) {
                // If we have no name attached, we can try to get the name from the type
                // This occurs in our HLSL path for constant buffers. Constant buffers
                // become a pointer to a struct (where the struct has the name we want),
                // and the actual variable just has an empty name.
                auto type = DecayType(v->second._type);
                n = LowerBound(_names, type);
                if (n != _names.end() && n->first == type) {
                    nameStart = n->second.begin();
                    nameEnd = n->second.end();
                } 
            }

            if (nameStart == nameEnd) continue;

            auto sem = MakeShaderSemantic({nameStart, nameEnd});

            _inputInterfaceQuickLookup.push_back(
                std::make_pair(
                    Hash64(sem._name) + sem._index,
                    InputInterfaceElement{v->second._type, b->second._location}));
        }

        std::sort(
            _inputInterfaceQuickLookup.begin(), _inputInterfaceQuickLookup.end(),
            CompareFirst<uint64_t, InputInterfaceElement>());

		// build the quick lookup table for push constants
		for (unsigned vi=0; vi<_variables.size(); ++vi) {
			if (_variables[vi].second._storage != StorageType::PushConstant)
				continue;

			// We don't have a way to get the offset from the top of push constant
			// memory yet. We'll have to assume that all constants sit at the top
			// of the push constant memory

			PushConstantsVariable var;
			var._variable = vi;
			var._type = _variables[vi].second._type;

			// Similar to above, we must be able to bind against either the type name or
			// the variable name.

			auto n = LowerBound(_names, _variables[vi].first);
			if (n != _names.end() && n->first == _variables[vi].first)
				if (!n->second.IsEmpty())
					_pushConstantsQuickLookup.push_back({Hash64(n->second), var});

            auto type = _variables[vi].second._type;
            auto ptr = LowerBound(_pointerTypes, type);
            if (ptr != _pointerTypes.end() && ptr->first == type)
                type = ptr->second._targetType;
                
            n = LowerBound(_names, type);
            if (n != _names.end() && n->first == type)
				if (!n->second.IsEmpty())
					_pushConstantsQuickLookup.push_back({Hash64(n->second), var});
		}

		std::sort(
			_pushConstantsQuickLookup.begin(), _pushConstantsQuickLookup.end(),
			CompareFirst<uint64_t, PushConstantsVariable>());
    }

    SPIRVReflection::SPIRVReflection() {}
    SPIRVReflection::~SPIRVReflection() {}

	std::ostream& operator<<(std::ostream& str, const SPIRVReflection::Binding& binding)
	{
		bool pendingComma = false;
		if (binding._location != ~0u) {
			str << "loc: " << binding._location;
			pendingComma = true;
		}

		if (binding._bindingPoint != ~0u) {
			if (pendingComma) str << ", ";
			str << "binding: " << binding._bindingPoint;
			pendingComma = true;
		}

		if (binding._offset != ~0u) {
			if (pendingComma) str << ", ";
			str << "offset: " << binding._offset;
		}
		return str;
	}

    auto SPIRVReflection::DecayType(ObjectId type) const -> ObjectId
    {
        for (;;) {
            auto ptr = LowerBound(_pointerTypes, type);
            if (ptr != _pointerTypes.end() && ptr->first == type) {
                type = ptr->second._targetType;
                continue;
            }

            auto array = LowerBound(_arrayTypes, type);
            if (array != _arrayTypes.end() && array->first == type) {
                type = array->second._elementType;
                continue;
            }

            break;
        }
        return type;
    }

	std::ostream& SPIRVReflection::DescribeVariable(std::ostream& str, ObjectId variable) const
	{
		auto n = LowerBound(_names, variable);
		if (n != _names.end() && n->first == variable) {
			str << n->second;
		} else {
			str << "<<unnamed>>";
		}

		auto v = LowerBound(_variables, variable);
		if (v != _variables.end() && v->first == variable) {
			StringSection<> variableType;
			auto type = DecayType(v->second._type);
            n = LowerBound(_names, type);
            if (n != _names.end() && n->first == type)
				variableType = n->second;

			str << " (";
			if (!variableType.IsEmpty()) str << "type: " << variableType << ", ";
			str << "storage: " << (unsigned)v->second._storage << ")";
		}

		return str;
	}

	static const std::string s_unnamed = "<<unnamed>>";

	StringSection<> SPIRVReflection::GetName(ObjectId objectId) const
	{
		auto n = LowerBound(_names, objectId);
		if (n != _names.end() && n->first == objectId)
			return n->second;
		return s_unnamed;
	}

	std::ostream& operator<<(std::ostream& str, const SPIRVReflection& refl)
	{
		str << "SPIR Reflection entry point [" << refl._entryPoint._name << "]" << std::endl;

		const unsigned maxDescriptorSet = 16;
		for (unsigned descriptorSet=0; descriptorSet<maxDescriptorSet; ++descriptorSet) {
			bool wroteHeader = false;
			for (auto&i:refl._bindings) {
				if (i.second._descriptorSet != descriptorSet)
					continue;
				
				if (!wroteHeader)
					str << "Descriptor set [" << descriptorSet << "]" << std::endl;
				wroteHeader = true;

				str << "\t[" << i.second << "]: ";
				refl.DescribeVariable(str, i.first);

				str << std::endl;
			}

			for (auto&i:refl._memberBindings) {
				if (i.second._descriptorSet != descriptorSet)
					continue;
				
				if (!wroteHeader)
					str << "Descriptor set [" << descriptorSet << "]" << std::endl;
				wroteHeader = true;

				auto n = LowerBound(refl._memberNames, i.first);
				if (n != refl._memberNames.end() && n->first == i.first) {
					str << "\t[" << n->second << "](member " << i.first.second << ") ";
				} else {
					str << "\t[Unnamed](member " << i.first.second << ") ";
				}

				str << i.second;

				auto v = LowerBound(refl._variables, i.first.first);
				if (v != refl._variables.end() && v->first == i.first.first) {
					str << " (type: " << v->second._type << ", storage: " << (unsigned)v->second._storage << ")";
				}

				str << std::endl;
			}
		}

		for (auto v:refl._variables) {
			if (v.second._storage != SPIRVReflection::StorageType::PushConstant)
				continue;
			
			str << "\tPush Constants: ";
			refl.DescribeVariable(str, v.first);
			str << std::endl;
		}

		// std::stringstream disassem;
        // std::vector<unsigned> spirv(byteCode.begin(), byteCode.end());
        // spv::Disassemble(disassem, spirv);
        // auto d = disassem.str();
        // (void)d;
		
		return str;
	}

}}

#else

namespace RenderCore { namespace Metal_Vulkan
{

    SPIRVReflection::SPIRVReflection(IteratorRange<const void*> byteCode) {}
    SPIRVReflection::SPIRVReflection() {}
    SPIRVReflection::~SPIRVReflection() {}
}}

#endif
