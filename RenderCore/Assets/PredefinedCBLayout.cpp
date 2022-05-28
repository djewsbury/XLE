// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PredefinedCBLayout.h"
#include "../RenderUtils.h"
#include "../ShaderLangUtil.h"
#include "../Format.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/DepVal.h"
#include "../../Assets/PreprocessorIncludeHandler.h"
#include "../../OSServices/Log.h"
#include "../../Utility/BitUtils.h"
#include "../../OSServices/RawFS.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/Streams/PreprocessorInterpreter.h"
#include "../../Utility/Streams/ConditionalPreprocessingTokenizer.h"

namespace RenderCore { namespace Assets
{

    PredefinedCBLayout::PredefinedCBLayout(StringSection<::Assets::ResChar> initializer)
    {
		for (unsigned c=0; c<dimof(_cbSizeByLanguage); ++c) {
            _cbSizeByLanguageNoPostfix[c] = 0;
            _cbSizeByLanguage[c] = 0;
        }
		_validationCallback = ::Assets::GetDepValSys().Make(initializer);

		TRY {
			// Here, we will read a simple configuration file that will define the layout
			// of a constant buffer. Sometimes we need to get the layout of a constant 
			// buffer without compiling any shader code, or really touching the HLSL at all.
			size_t size;
			auto file = ::Assets::MainFileSystem::TryLoadFileAsMemoryBlock(initializer, &size);
			StringSection<char> configSection((const char*)file.get(), (const char*)PtrAdd(file.get(), size));

			// if it's a compound document, we're only going to extra the cb layout part
			auto compoundDoc = ::Assets::ReadCompoundTextDocument(configSection);
			if (!compoundDoc.empty()) {
				auto i = std::find_if(
					compoundDoc.cbegin(), compoundDoc.cend(),
					[](const ::Assets::TextChunk<char>& chunk)
					{ return XlEqString(chunk._type, "CBLayout"); });
				if (i != compoundDoc.cend())
					configSection = i->_content;
			}

			ConditionalProcessingTokenizer tokenizer{configSection};
            Parse(tokenizer);
            if (!tokenizer.Remaining().IsEmpty())
    			Throw(FormatException("Additional tokens found, expecting end of file", tokenizer.GetLocation()));
		} CATCH(const ::Assets::Exceptions::ConstructionError& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, _validationCallback));
		} CATCH (const std::exception& e) {
			Throw(::Assets::Exceptions::ConstructionError(e, _validationCallback));
		} CATCH_END
	}

    PredefinedCBLayout::PredefinedCBLayout(
        StringSection<char> source, 
        const ::Assets::DirectorySearchRules& searchRules,
        const ::Assets::DependencyValidation& depVal)
    : _validationCallback(depVal)
    {
        for (unsigned c=0; c<dimof(_cbSizeByLanguage); ++c) {
            _cbSizeByLanguageNoPostfix[c] = 0;
            _cbSizeByLanguage[c] = 0;
        }
        ConditionalProcessingTokenizer tokenizer{source};
        Parse(tokenizer);
        if (!tokenizer.Remaining().IsEmpty())
			Throw(FormatException("Additional tokens found, expecting end of file", tokenizer.GetLocation()));
    }

    PredefinedCBLayout::PredefinedCBLayout(
        ConditionalProcessingTokenizer& tokenizer,
        const ::Assets::DependencyValidation& depVal)
    : _validationCallback(depVal)
    {
        for (unsigned c=0; c<dimof(_cbSizeByLanguage); ++c) {
            _cbSizeByLanguageNoPostfix[c] = 0;
            _cbSizeByLanguage[c] = 0;
        }
        Parse(tokenizer);
    }

    /**
        "iterator" is the current offset iterator, and "nextElement" is the next element we want to
        append to the CB.
        The return value will be the offset to this new element, and "iterator" will be updated so
        it can be used in a subsequent call to CalculateElementOffset.
        In this way, the function can apply alignment both before and after the element.

        arrayElementStride will also receive the stride of the array elements, if it is an array
    */
    static unsigned CalculateElementOffset(unsigned& iterator, unsigned& arrayElementStride, const ImpliedTyping::TypeDesc& nextElement, unsigned nextElementArrayCount, PredefinedCBLayout::AlignmentRules alignmentRules)
    {
        if (alignmentRules == PredefinedCBLayout::AlignmentRules_HLSL) {

            auto size = nextElement.GetSize();

            if (nextElementArrayCount==0) {
                // HLSL adds padding so that vectors don't straddle 16 byte boundaries!
                // let's detect that case, and add pre-alignment as necessary
                if (FloorToMultiplePow2(iterator, 16) != FloorToMultiplePow2(iterator + std::min(16u, size) - 1, 16)) {
                    iterator = CeilToMultiplePow2(iterator, 16);
                }

                unsigned result = iterator;     // this is the offset for the new element
                arrayElementStride = size;
                iterator += size;
                return result;

            } else {
                // We must always begin on 16 byte alignment for an array
                // note this even applies to array of a single element
                iterator = CeilToMultiplePow2(iterator, 16);
                unsigned result = iterator;
                arrayElementStride = CeilToMultiplePow2(size, 16);
                iterator += nextElementArrayCount * arrayElementStride;
                return result;
            }

        } else if (alignmentRules == PredefinedCBLayout::AlignmentRules_GLSL_std140) {

            // Alignment rules for std140 mode in GLSL
            // (ie, use "layout(std140) uniform ...").
            // Alignment rules are documented here:
            //  https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_uniform_buffer_object.txt
            // It's similar to both MSL and HLSL modes, but differs in very subtle cases

            auto adjustedType = nextElement;
            if (adjustedType._arrayCount == 3)      // alignment for a 3d vector behaves like a 4d vector
                adjustedType._arrayCount = 4;
            auto sizeForAlignment = adjustedType.GetSize();

            if (nextElementArrayCount >= 1) {
                // As per rule 4, round up the element size to the alignment for a vec4
                arrayElementStride = CeilToMultiple(sizeForAlignment, 16);
                iterator = CeilToMultiple(iterator, arrayElementStride);
                unsigned result = iterator;                                 // this is the offset for the new element

                iterator += (std::max(1u, nextElementArrayCount)-1) * arrayElementStride;     // simplified logic, given alignment is equal to size

                // For the last element in the array (or for the only element in a non-array case), we
                // move forward only by the size of the type. That is, we don't add padding to based
                // on the alignment size of the type. This is one of the only differences relative to
                // the Metal Shader Language alignment type
                iterator += nextElement.GetSize();

                return result;
            } else {
                iterator = CeilToMultiple(iterator, sizeForAlignment);
                unsigned result = iterator;                                 // this is the offset for the new element
                arrayElementStride = nextElement.GetSize();
                iterator += arrayElementStride;
                return result;
            }

        } else if (alignmentRules == PredefinedCBLayout::AlignmentRules_MSL) {

            // In metal shader language, the alignment is always equal to the size of the type,
            // however we regard 3-component types as having the same size as 4 component types.
            //
            // There are "packed" types, which have slightly different rules. These are the
            // rules for the non-packed types. We're avoiding the packed types on the assumption
            // that the non-packed types will have performance advantages.

            auto adjustedType = nextElement;
            if (adjustedType._arrayCount == 3)
                adjustedType._arrayCount = 4;
            auto size = adjustedType.GetSize();

            iterator = CeilToMultiple(iterator, size);
            unsigned result = iterator;                                 // this is the offset for the new element
            iterator += std::max(1u, nextElementArrayCount) * size;     // simplified logic, given alignment is equal to size

            arrayElementStride = size;

            return result;

        } else {
            return iterator;
        }
    }

    PredefinedCBLayout::NameAndType ParseStatement(ConditionalProcessingTokenizer& streamIterator, ParameterBox& defaults)
    {
        auto type = streamIterator.GetNextToken();
        auto cond = streamIterator._preprocessorContext.GetCurrentConditionString();

        auto name = streamIterator.GetNextToken();
        if (name._value.IsEmpty())
            Throw(FormatException("Expecting variable name", name._start));

        StringSection<> arrayCount;
        if (XlEqString(streamIterator.PeekNextToken()._value, "[")) {
            streamIterator.GetNextToken();      // skip the one we peeked

            auto count = streamIterator.GetNextToken();
            if (count._value.IsEmpty() || XlEqString(count._value, "]"))
                Throw(FormatException("Expecting array count", count._start));

            arrayCount = count._value;
            auto token = streamIterator.GetNextToken();
            if (!XlEqString(token._value, "]"))
                Throw(FormatException("Expecting closing array brace", token._start));
        }

        PredefinedCBLayout::NameAndType result;
        result._name = name._value.AsString();
        result._type = ShaderLangTypeNameAsTypeDesc(type._value);
        result._conditions = cond;

        auto size = result._type.GetSize();
        if (!size) {
            Throw(FormatException(
                StringMeld<256>() << "Problem parsing type (" << type._value << ") in PredefinedCBLayout. Type size is: " << size,
                type._start));
        }

        unsigned arrayElementCount = 0;
        if (!arrayCount.IsEmpty()) {
            arrayElementCount = Conversion::Convert<unsigned>(arrayCount);
        }
        result._arrayElementCount = arrayElementCount;

        if (XlEqString(streamIterator.PeekNextToken()._value, "=")) {
            auto equalsToken = streamIterator.GetNextToken();      // skip the one we peeked

            if (!arrayCount.IsEmpty())
                Throw(FormatException(
                    "Attempting to provide an default for an array type in PredefinedCBLayout (this isn't supported)",
                    equalsToken._start));

            // We should read until we hit a ';'
            // (note that the whitespace between tokens will be removed by this method)
            auto startLocation = streamIterator.GetLocation();
            std::string value;
            for (;;) {
                auto nextToken = streamIterator.GetNextToken();
                if (nextToken._value.IsEmpty() || XlEqString(nextToken._value, ";"))
                    break;
                value.insert(value.end(), nextToken._value.begin(), nextToken._value.end());
            }

            uint8 buffer0[256], buffer1[256];
            auto defaultType = ImpliedTyping::ParseFullMatch(
                MakeStringSection(value),
                MakeIteratorRange(buffer0));

            if (!(defaultType == result._type)) {
                    //  The initialiser isn't exactly the same type as the
                    //  defined variable. Let's try a casting operation.
                    //  Sometimes we can get int defaults for floats variables, etc.
                bool castSuccess = ImpliedTyping::Cast(
                    MakeIteratorRange(buffer1), result._type,
                    MakeIteratorRange(buffer0), defaultType);
                if (castSuccess) {
                    auto* end = &buffer1[std::min(dimof(buffer1), (size_t)defaultType.GetSize())];
                    defaults.SetParameter((const utf8*)result._name.c_str(), MakeIteratorRange(buffer1, end), result._type);
                } else {
                    Throw(FormatException(
                        "Default initialiser can't be cast to same type as variable in PredefinedCBLayout: ",
                        startLocation));
                }
            } else {
                auto* end = &buffer0[std::min(dimof(buffer0), (size_t)defaultType.GetSize())];
                defaults.SetParameter((const utf8*)result._name.c_str(), MakeIteratorRange(buffer0, end), defaultType);
            }
        } else {
            auto token = streamIterator.GetNextToken();
            if (!XlEqString(token._value, ";"))
                Throw(FormatException("Expecting ';' to finish statement", token._start));
        }

        return result;
    }

    void AppendElement(PredefinedCBLayout& cbLayout, const PredefinedCBLayout::NameAndType& input, unsigned cbIterator[PredefinedCBLayout::AlignmentRules_Max])
    {
        PredefinedCBLayout::Element e;
        e._name = input._name;
        e._hash = ParameterBox::MakeParameterNameHash(e._name);
        e._type = input._type;
        e._conditions = input._conditions;
        e._arrayElementCount = input._arrayElementCount;
        for (unsigned c=0; c<PredefinedCBLayout::AlignmentRules_Max; ++c)
            e._offsetsByLanguage[c] = CalculateElementOffset(cbIterator[c], e._arrayElementStride, e._type, e._arrayElementCount, (PredefinedCBLayout::AlignmentRules)c);
        cbLayout._elements.push_back(e);
    }

    void PredefinedCBLayout::Parse(ConditionalProcessingTokenizer& si)
    {
        unsigned cbIterator[AlignmentRules_Max] = { 0, 0, 0 };
        for (;;) {
            auto next = si.PeekNextToken();
            if (next._value.IsEmpty() || XlEqString(next._value, "}"))
                break;

            auto parsed = ParseStatement(si, _defaults);
            AppendElement(*this, parsed, cbIterator);
        }

        for (unsigned c=0; c<dimof(cbIterator); ++c) {
            _cbSizeByLanguageNoPostfix[c] = cbIterator[c];
            _cbSizeByLanguage[c] = CeilToMultiplePow2(cbIterator[c], 16);
        }
    }

    static PredefinedCBLayout::AlignmentRules AlignmentRulesForLanguage(ShaderLanguage lang)
    {
        switch (lang) {
        case ShaderLanguage::MetalShaderLanguage: return PredefinedCBLayout::AlignmentRules_MSL;
		default:
        case ShaderLanguage::HLSL: return PredefinedCBLayout::AlignmentRules_HLSL;
        case ShaderLanguage::GLSL: return PredefinedCBLayout::AlignmentRules_GLSL_std140;
        }
    }

    void PredefinedCBLayout::WriteBuffer(void* dst, const ParameterBox& parameters, ShaderLanguage lang) const
    {
        unsigned alignmentRules = AlignmentRulesForLanguage(lang);
        for (auto c=_elements.cbegin(); c!=_elements.cend(); ++c) {
            for (auto e=0u; e<std::max(1u, c->_arrayElementCount); e++) {
                bool gotValue = parameters.GetParameter(
                    c->_hash + e, PtrAdd(dst, c->_offsetsByLanguage[alignmentRules] + e * c->_arrayElementStride),
                    c->_type);

                if (!gotValue)
                    _defaults.GetParameter(c->_hash + e, PtrAdd(dst, c->_offsetsByLanguage[alignmentRules]), c->_type);
            }
        }
    }

    std::vector<uint8> PredefinedCBLayout::BuildCBDataAsVector(const ParameterBox& parameters, ShaderLanguage lang) const
    {
        unsigned alignmentRules = AlignmentRulesForLanguage(lang);
        std::vector<uint8> cbData(_cbSizeByLanguage[alignmentRules], uint8(0));
        WriteBuffer(AsPointer(cbData.begin()), parameters, lang);
        return cbData;
    }

    SharedPkt PredefinedCBLayout::BuildCBDataAsPkt(const ParameterBox& parameters, ShaderLanguage lang) const
    {
        unsigned alignmentRules = AlignmentRulesForLanguage(lang);
        SharedPkt result = MakeSharedPktSize(_cbSizeByLanguage[alignmentRules]);
        std::memset(result.begin(), 0, _cbSizeByLanguage[alignmentRules]);
        WriteBuffer(result.begin(), parameters, lang);
        return result;
    }
    
    uint64_t PredefinedCBLayout::CalculateHash(uint64_t seed) const
    {
        uint64_t result = seed;
        for (const auto&e:_elements) {
            // note -- we have to carefully hash only the elements that meaningfully effect the
            //
            result = HashCombine(e._hash, result);
            result = Hash64(&e._type, &e._arrayElementStride+1, result);
            result = HashCombine(Hash64(e._conditions), result);
        }
        return result;
    }
    
    auto PredefinedCBLayout::MakeConstantBufferElements(ShaderLanguage lang) const -> std::vector<ConstantBufferElementDesc>
    {
        unsigned alignmentRules = AlignmentRulesForLanguage(lang);
        std::vector<ConstantBufferElementDesc> result;
        result.reserve(_elements.size());
        for (auto i=_elements.begin(); i!=_elements.end(); ++i) {
            result.push_back(ConstantBufferElementDesc {
                i->_hash, AsFormat(i->_type),
                i->_offsetsByLanguage[alignmentRules], i->_arrayElementCount });
        }
        return result;
    }

    unsigned PredefinedCBLayout::GetSize(ShaderLanguage lang) const
    {
        unsigned alignmentRules = AlignmentRulesForLanguage(lang);
        return _cbSizeByLanguage[alignmentRules];
    }

    unsigned PredefinedCBLayout::GetSize_NoPostfix(ShaderLanguage lang) const
    {
        unsigned alignmentRules = AlignmentRulesForLanguage(lang);
        return _cbSizeByLanguageNoPostfix[alignmentRules];
    }

    PredefinedCBLayout PredefinedCBLayout::Filter(const std::unordered_map<std::string, int>& definedTokens)
    {
        PredefinedCBLayout result;
        result._validationCallback = _validationCallback;
        unsigned cbIterator[AlignmentRules_Max] = { 0, 0, 0 };

        result._elements.reserve(_elements.size());
        for (const auto& e:_elements) {
            if (    e._conditions.empty()
                ||  EvaluatePreprocessorExpression(e._conditions, definedTokens)) {

                result._elements.push_back(e);
                auto& newE = *(result._elements.end()-1);

                for (unsigned c=0; c<PredefinedCBLayout::AlignmentRules_Max; ++c)
                    newE._offsetsByLanguage[c] = CalculateElementOffset(cbIterator[c], newE._arrayElementStride, newE._type, newE._arrayElementCount, (PredefinedCBLayout::AlignmentRules)c);

                auto defaultType = _defaults.GetParameterType(newE._hash);
                if (defaultType._type != ImpliedTyping::TypeCat::Void) {
                    auto rawValue = _defaults.GetParameterRawValue(newE._hash);
                    result._defaults.SetParameter(e._hash, rawValue, defaultType);
                }
            }
        }

        for (unsigned c=0; c<AlignmentRules_Max; ++c) {
            result._cbSizeByLanguageNoPostfix[c] = cbIterator[c];
            result._cbSizeByLanguage[c] = CeilToMultiplePow2(cbIterator[c], 16);
        }
        return result;
    }

    std::ostream& PredefinedCBLayout::DescribeCB(std::ostream& str, IteratorRange<const void*> cbData, ShaderLanguage lang)
    {
        unsigned alignmentRules = AlignmentRulesForLanguage(lang);
        for (const auto&e:_elements) {
            if (!e._conditions.empty())
                str << "#if " << e._conditions << std::endl;
            str << "\t" << AsShaderLangTypeName(e._type, lang) << " " << e._name;
            if (e._arrayElementCount != 0) {
                str << "[" << e._arrayElementCount << "] = {" << std::endl;
                for (unsigned a=0; a<e._arrayElementCount; ++a) {
                    auto range = MakeIteratorRange(PtrAdd(cbData.begin(), e._arrayElementStride*a + e._offsetsByLanguage[alignmentRules]), cbData.end());
                    str << "\t\t" << ImpliedTyping::AsString(range, e._type);
                    if ((a+1) != e._arrayElementCount)
                        str << ",";
                    str << std::endl;
                }
                str << "};" << std::endl;
            } else {
                str << " = " << ImpliedTyping::AsString(MakeIteratorRange(PtrAdd(cbData.begin(), e._offsetsByLanguage[alignmentRules]), cbData.end()), e._type) 
                    << ";" << std::endl;
            }
            if (!e._conditions.empty())
                str << "#endif" << std::endl;
        }
        return str;
    }

    PredefinedCBLayout::PredefinedCBLayout(IteratorRange<const NameAndType*> elements, const ParameterBox& defaults)
	: _defaults(defaults)
    {
        unsigned cbIterator[AlignmentRules_Max] = { 0, 0, 0 };

        for (auto&e:elements)
            AppendElement(*this, e, cbIterator);

        for (unsigned c=0; c<dimof(cbIterator); ++c) {
            _cbSizeByLanguageNoPostfix[c] = cbIterator[c];
            _cbSizeByLanguage[c] = CeilToMultiplePow2(cbIterator[c], 16);
        }
    }

    PredefinedCBLayout::PredefinedCBLayout()
    {
        for (unsigned c=0; c<dimof(_cbSizeByLanguage); ++c) {
            _cbSizeByLanguageNoPostfix[c] = 0;
            _cbSizeByLanguage[c] = 0;
        }
    }

    PredefinedCBLayout::~PredefinedCBLayout() {}

////////////////////////////////////////////////////////////////////////////////////////////////////

    static PredefinedCBLayout::NameAndType* FindAlignmentGap(IteratorRange<PredefinedCBLayout::NameAndType*> elements, size_t requestSize)
    {
        // HLSL / GLSL implementation --

        unsigned cbIterator = 0;

        auto i = elements.begin();
        for(;i!=elements.end(); ++i) {
            auto newCBIterator = cbIterator;
            if (FloorToMultiplePow2(newCBIterator, 16) != FloorToMultiplePow2(newCBIterator + std::min(16u, i->_type.GetSize()) - 1, 16)) {
                newCBIterator = CeilToMultiplePow2(newCBIterator, 16);

                auto paddingSpace = newCBIterator - cbIterator;
                // If the paddingSpace equals or exceeds the space we're looking for, then let's use this space
                // We return the current iterator, which means the space can be found immediately before this element
                if (paddingSpace >= requestSize)
                    return i;
            }

            auto eleSize = i->_type.GetSize();
            auto arrayElementStride = (i->_arrayElementCount > 1) ? CeilToMultiplePow2(eleSize, 16) : eleSize;
            if (i->_arrayElementCount != 0)
                cbIterator += (i->_arrayElementCount - 1) * arrayElementStride + eleSize;
        }

        return i;
    }

    void PredefinedCBLayout::OptimizeElementOrder(IteratorRange<NameAndType*> elements, ShaderLanguage lang)
    {
        // Order optimization not implemented for MSL yet (only GLSL/HLSL, which use the same rules)
        assert(lang != ShaderLanguage::MetalShaderLanguage);

        // Optimize ordering in 2 steps:
        //  1) order by type size (largest first) -- using a stable sort to maintain the original ordering as much as possible
        //    2) move any elements that can be squeezed into gaps in earlier parts of the ordering
        std::stable_sort(
            elements.begin(), elements.end(),
            [](const NameAndType& lhs, const NameAndType& rhs) {
                if (lhs._arrayElementCount > rhs._arrayElementCount) return true;
                if (lhs._arrayElementCount < rhs._arrayElementCount) return false;
                return lhs._type.GetSize() > rhs._type.GetSize();
            });

        for (auto i=elements.begin(); i!=elements.end(); ++i) {
            if (i->_arrayElementCount != 1 || i->_type.GetSize() >= 16) continue;

            // Find the best gap to squeeze this into
            // (note that since "elements" is ordered from largest to smallest, we will always
            // find and occupy the large alignment gaps first)
            auto insertionPoint = FindAlignmentGap(MakeIteratorRange(elements.begin(), i), i->_type.GetSize());
            if (insertionPoint == i) continue;    // no better location found

            // we can insert this object immediate before 'insertionPoint'. To do that, we should
            // move all elements from i-1 up to (and including) insertionPoint forward on element
            auto elementToInsert = *i;
            for (auto i2=i; (i2-1)>=insertionPoint; i2--)
                *i2 = *(i2-1);
            *insertionPoint = elementToInsert;
        }
    }

    auto PredefinedCBLayout::GetNamesAndTypes() -> std::vector<NameAndType>
    {
        std::vector<NameAndType> result;
        result.reserve(_elements.size());
        for (const auto&e:_elements) {
            result.push_back(NameAndType{e._name, e._type, e._arrayElementCount, e._conditions});
        }
        return result;
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

    PredefinedCBLayoutFile::PredefinedCBLayoutFile(
        StringSection<> inputData,
        const ::Assets::DirectorySearchRules& searchRules,
        const ::Assets::DependencyValidation& depVal)
    {
        //
        //  Parse through thes input data line by line.
        //  If we find lines beginning with preprocessor command, we should pass them through
        //  the PreprocessorParseContext
        //
        //  Note that we don't support line appending syntax (eg, back-slash and then a newline)
        //      -- that just requires a bunch of special cases, and doesn't seem like it's
        //      worth the hassle.
        //  Also preprocessor symbols must be at the start of the line, or at least preceeded only
        //  by whitespace (same as C/CPP)
        //

        ::Assets::PreprocessorIncludeHandler includeHandler;
        ConditionalProcessingTokenizer iterator(inputData, searchRules.GetBaseFile(), &includeHandler);

        std::string currentLayoutName;
        std::shared_ptr<PredefinedCBLayout> currentLayout;
        unsigned currentLayoutCBIterator[PredefinedCBLayout::AlignmentRules_Max] = { 0, 0, 0 };

        for (;;) {
            auto next = iterator.PeekNextToken();
            if (next._value.IsEmpty())
                break;

            if (currentLayoutName.empty()) {
                auto token = iterator.GetNextToken();
                if (!XlEqString(token._value, "struct"))
                    Throw(FormatException(StringMeld<256>() << "Expecting 'struct' keyword, but got " << token._value, token._start));

                auto layoutName = iterator.GetNextToken();
                if (layoutName._value.IsEmpty())
                    Throw(FormatException("Expecting identifier after struct keyword", token._start));

                currentLayoutName = layoutName._value.AsString();

                token = iterator.GetNextToken();
                if (!XlEqString(token._value, "{"))
                    Throw(FormatException(StringMeld<256>() << "Expecting '{', but got " << token._value, token._start));
            }

            if (XlEqString(next._value, "}")) {
                iterator.GetNextToken();
                auto token = iterator.GetNextToken();
                if (!XlEqString(token._value, ";"))
                    Throw(FormatException(StringMeld<256>() << "Expecting ; after }, but got " << token._value, token._start));

                for (unsigned c=0; c<dimof(currentLayout->_cbSizeByLanguage); ++c) {
                    currentLayout->_cbSizeByLanguageNoPostfix[c] = currentLayoutCBIterator[c];
                    currentLayout->_cbSizeByLanguage[c] = CeilToMultiplePow2(currentLayoutCBIterator[c], 16);
                }
                _layouts.insert(std::make_pair(currentLayoutName, std::move(currentLayout)));
                currentLayoutName = {};
                currentLayout = nullptr;
                for (unsigned c=0; c<dimof(currentLayout->_cbSizeByLanguage); ++c)
                    currentLayoutCBIterator[c] = 0;
                continue;
            }

            if (!currentLayout)
                currentLayout = std::make_shared<PredefinedCBLayout>();

            auto parsed = ParseStatement(iterator, currentLayout->_defaults);
            AppendElement(*currentLayout, parsed, currentLayoutCBIterator);
        }

        if (currentLayout) {
            for (unsigned c=0; c<dimof(currentLayout->_cbSizeByLanguage); ++c) {
                currentLayout->_cbSizeByLanguageNoPostfix[c] = currentLayoutCBIterator[c];
                currentLayout->_cbSizeByLanguage[c] = CeilToMultiplePow2(currentLayoutCBIterator[c], 16);
            }
            _layouts.insert(std::make_pair(currentLayoutName, std::move(currentLayout)));
        }
    }

    PredefinedCBLayoutFile::~PredefinedCBLayoutFile()
    {
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

}}
