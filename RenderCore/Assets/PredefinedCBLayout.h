// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../UniformsStream.h"
#include "../ShaderLangUtil.h"
#include "../../Assets/DepVal.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/StringUtils.h"
#include <unordered_map>
#include <string>
#include <iosfwd>

namespace Utility { class ConditionalProcessingTokenizer; }
namespace RenderCore { class SharedPkt; }
namespace RenderCore { namespace Assets
{
    class PredefinedCBLayout
    {
    public:
        enum AlignmentRules {
            AlignmentRules_HLSL,            // Basic HLSL alignment; often compatible with GLSL
            AlignmentRules_GLSL_std140,     // GLSL "std140" layout style
            AlignmentRules_MSL,             // Apple Metal Shader Language
            AlignmentRules_Max
        };

        class Element
        {
        public:
            ParameterBox::ParameterNameHash _hash = ~0ull;
            ImpliedTyping::TypeDesc _type;
            unsigned _arrayElementCount = 0;            // set to zero if this parameter is not actually an array
            unsigned _arrayElementStride = 0;
            std::string _name;
            std::string _conditions;

            // Offsets according to the alignment rules for different shader languages
            unsigned _offsetsByLanguage[AlignmentRules_Max];

			Element()
			{
				for (unsigned c=0; c<AlignmentRules_Max; ++c)
					_offsetsByLanguage[c] = 0;
			}
        };
        std::vector<Element> _elements;
        ParameterBox _defaults;

        std::vector<uint8> BuildCBDataAsVector(const ParameterBox& parameters, ShaderLanguage lang) const;
        SharedPkt BuildCBDataAsPkt(const ParameterBox& parameters, ShaderLanguage lang) const;
        unsigned GetSize(ShaderLanguage lang) const;
        std::vector<ConstantBufferElementDesc> MakeConstantBufferElements(ShaderLanguage lang) const;

		// Reorder the given elements to try to find an ordering that will minimize the
		// size of the final constant buffer. This accounts for ordering rules such as
		// preventing vectors from crossing 16 byte boundaries.
		struct NameAndType { std::string _name; ImpliedTyping::TypeDesc _type; unsigned _arrayElementCount = 0u; std::string _conditions = {}; };
		static void OptimizeElementOrder(IteratorRange<NameAndType*> elements, ShaderLanguage lang);
        std::vector<NameAndType> GetNamesAndTypes();

        uint64_t CalculateHash() const;

        PredefinedCBLayout Filter(const std::unordered_map<std::string, int>& definedTokens);

        std::ostream& DescribeCB(std::ostream& str, IteratorRange<const void*> cbData, ShaderLanguage lang);

        PredefinedCBLayout();
        PredefinedCBLayout(StringSection<::Assets::ResChar> initializer);
        PredefinedCBLayout(
            StringSection<char> source, 
            const ::Assets::DirectorySearchRules& searchRules,
			const ::Assets::DependencyValidation& depVal);
        PredefinedCBLayout(
			ConditionalProcessingTokenizer&,
			const ::Assets::DependencyValidation&);
		PredefinedCBLayout(IteratorRange<const NameAndType*> elements, const ParameterBox& defaults = {});
        ~PredefinedCBLayout();
        
        PredefinedCBLayout(const PredefinedCBLayout&) = default;
        PredefinedCBLayout& operator=(const PredefinedCBLayout&) = default;
        PredefinedCBLayout(PredefinedCBLayout&&) never_throws = default;
        PredefinedCBLayout& operator=(PredefinedCBLayout&&) never_throws = default;

        const ::Assets::DependencyValidation& GetDependencyValidation() const     
            { return _validationCallback; }

    private:
        ::Assets::DependencyValidation   _validationCallback;

        void Parse(ConditionalProcessingTokenizer&);
        void WriteBuffer(void* dst, const ParameterBox& parameters, ShaderLanguage lang) const;

        // Similar to the offset values, the size of the CB depends on what shader language rules are used
        unsigned _cbSizeByLanguage[AlignmentRules_Max];

        friend class PredefinedCBLayoutFile;
		friend class PredefinedDescriptorSetLayout;
    };

	/// <summary>A file that can contain multiple PredefinedCBLayout</summary>
	/// Deprecated interface. Prefer PredefinedDescriptorSetLayout instead.
    XLE_DEPRECATED_ATTRIBUTE class PredefinedCBLayoutFile
    {
    public:
        std::unordered_map<std::string, std::shared_ptr<PredefinedCBLayout>> _layouts;

        PredefinedCBLayoutFile(
            StringSection<> inputData,
            const ::Assets::DirectorySearchRules& searchRules,
            const ::Assets::DependencyValidation& depVal);
        ~PredefinedCBLayoutFile();

        const ::Assets::DependencyValidation& GetDependencyValidation() const
            { return _validationCallback; }
    private:
        ::Assets::DependencyValidation   _validationCallback;
    };
}}
