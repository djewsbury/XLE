// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RawMaterial.h"
#include "ModelCompilationConfiguration.h"
#include "../StateDesc.h"
#include "../../Assets/Assets.h"
#include "../../Assets/AssetTraits.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Assets/IArtifact.h"
#include "../../Assets/AssetMixins.h"
#include "../../Formatters/TextFormatter.h"
#include "../../Formatters/TextOutputFormatter.h"
#include "../../Formatters/StreamDOM.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Formatters/FormatterUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Conversion.h"

namespace RenderCore { namespace Assets
{

	static const auto s_MaterialCompileProcessType = ConstHash64Legacy<'RawM', 'at'>::Value;

///////////////////////////////////////////////////////////////////////////////////////////////////

    static const std::pair<Blend, const utf8*> s_blendNames[] =
    {
        std::make_pair(Blend::Zero, "zero"),
        std::make_pair(Blend::One, "one"),
            
        std::make_pair(Blend::SrcColor, "srccolor"),
        std::make_pair(Blend::InvSrcColor, "invsrccolor"),
        std::make_pair(Blend::DestColor, "destcolor"),
        std::make_pair(Blend::InvDestColor, "invdestcolor"),

        std::make_pair(Blend::SrcAlpha, "srcalpha"),
        std::make_pair(Blend::InvSrcAlpha, "invsrcalpha"),
        std::make_pair(Blend::DestAlpha, "destalpha"),
        std::make_pair(Blend::InvDestAlpha, "invdestalpha"),
    };

    static const std::pair<BlendOp, const utf8*> s_blendOpNames[] =
    {
        std::make_pair(BlendOp::NoBlending, "noblending"),
        std::make_pair(BlendOp::NoBlending, "none"),
        std::make_pair(BlendOp::NoBlending, "false"),

        std::make_pair(BlendOp::Add, "add"),
        std::make_pair(BlendOp::Subtract, "subtract"),
        std::make_pair(BlendOp::RevSubtract, "revSubtract"),
        std::make_pair(BlendOp::Min, "min"),
        std::make_pair(BlendOp::Max, "max")
    };
    
    static Blend DeserializeBlend(
        const Formatters::StreamDOMElement<Formatters::TextInputFormatter<utf8>>& ele, const utf8 name[])
    {
        if (ele) {
            auto child = ele.Attribute(name);
            if (child) {
                auto value = child.Value();
                for (unsigned c=0; c<dimof(s_blendNames); ++c)
                    if (XlEqStringI(value, s_blendNames[c].second))
                        return s_blendNames[c].first;
                return (Blend)XlAtoI32((const char*)child.Value().AsString().c_str());
            }
        }

        return Blend::Zero;
    }

    static BlendOp DeserializeBlendOp(
        const Formatters::StreamDOMElement<Formatters::TextInputFormatter<utf8>>& ele, const utf8 name[])
    {
        if (ele) {
            auto child = ele.Attribute(name);
            if (child) {
                auto value = child.Value();
                for (unsigned c=0; c<dimof(s_blendOpNames); ++c)
                    if (XlEqStringI(value, s_blendOpNames[c].second))
                        return s_blendOpNames[c].first;
                return (BlendOp)XlAtoI32((const char*)child.Value().AsString().c_str());
            }
        }

        return BlendOp::NoBlending;
    }

    static RenderStateSet DeserializeStateSet(Formatters::TextInputFormatter<utf8>& formatter)
    {
        RenderStateSet result;

        Formatters::StreamDOM<Formatters::TextInputFormatter<utf8>> doc(formatter);
        auto rootElement = doc.RootElement();

        {
            auto child = rootElement.Attribute("DoubleSided").As<bool>();
            if (child.has_value()) {
                result._doubleSided = child.value();
                result._flag |= RenderStateSet::Flag::DoubleSided;
            }
        }
        {
            auto child = rootElement.Attribute("SmoothLines").As<bool>();
            if (child.has_value()) {
                result._smoothLines = child.value();
                result._flag |= RenderStateSet::Flag::SmoothLines;
            }
        }
        {
            auto child = rootElement.Attribute("LineWeight").As<float>();
            if (child.has_value()) {
                result._lineWeight = child.value();
                result._flag |= RenderStateSet::Flag::LineWeight;
            }
        }
        {
            auto child = rootElement.Attribute("WriteMask").As<unsigned>();
            if (child.has_value()) {
                result._writeMask = child.value();
                result._flag |= RenderStateSet::Flag::WriteMask;
            }
        }
        {
            auto child = rootElement.Attribute("BlendType");
            if (child) {
                if (XlEqStringI(child.Value(), "decal")) {
                    result._blendType = RenderStateSet::BlendType::DeferredDecal;
                } else if (XlEqStringI(child.Value(), "ordered")) {
                    result._blendType = RenderStateSet::BlendType::Ordered;
                } else {
                    result._blendType = RenderStateSet::BlendType::Basic;
                }
                result._flag |= RenderStateSet::Flag::BlendType;
            }
        }
        {
            auto child = rootElement.Attribute("DepthBias").As<int>();
            if (child.has_value()) {
                result._depthBias = child.value();
                result._flag |= RenderStateSet::Flag::DepthBias;
            }
        }
        {
            auto child = rootElement.Element("ForwardBlend");
            if (child) {
                result._forwardBlendSrc = DeserializeBlend(child, "Src");
                result._forwardBlendDst = DeserializeBlend(child, "Dst");
                result._forwardBlendOp = DeserializeBlendOp(child, "Op");
                result._flag |= RenderStateSet::Flag::ForwardBlend;
            }
        }
        return result;
    }

    static const utf8* AsString(RenderStateSet::BlendType blend)
    {
        switch (blend) {
        case RenderStateSet::BlendType::DeferredDecal: return "decal";
        case RenderStateSet::BlendType::Ordered: return "ordered";
        default:
        case RenderStateSet::BlendType::Basic: return "basic";
        }
    }

    static const utf8* AsString(Blend input)
    {
        for (unsigned c=0; c<dimof(s_blendNames); ++c) {
            if (s_blendNames[c].first == input) {
                return s_blendNames[c].second;
            }
        }
        return "one";
    }

    static const utf8* AsString(BlendOp input)
    {
        for (unsigned c=0; c<dimof(s_blendOpNames); ++c) {
            if (s_blendOpNames[c].first == input) {
                return s_blendOpNames[c].second;
            }
        }
        return "noblending";
    }

    template<typename Type>
        std::basic_string<utf8> AutoAsString(const Type& type)
        {
            return Conversion::Convert<std::basic_string<utf8>>(
                ImpliedTyping::AsString(type, true));
        }

    static bool HasSomethingToSerialize(const RenderStateSet& stateSet)
    {
        return stateSet._flag != 0;
    }

    static void SerializeStateSet(Formatters::TextOutputFormatter& formatter, const RenderStateSet& stateSet)
    {
        if (stateSet._flag & RenderStateSet::Flag::DoubleSided)
            formatter.WriteKeyedValue("DoubleSided", AutoAsString(stateSet._doubleSided));

        if (stateSet._flag & RenderStateSet::Flag::SmoothLines)
            formatter.WriteKeyedValue("SmoothLines", AutoAsString(stateSet._smoothLines));

        if (stateSet._flag & RenderStateSet::Flag::LineWeight)
            formatter.WriteKeyedValue("LineWeigth", AutoAsString(stateSet._lineWeight));

        if (stateSet._flag & RenderStateSet::Flag::WriteMask)
            formatter.WriteKeyedValue("WriteMask", AutoAsString(stateSet._writeMask));

        if (stateSet._flag & RenderStateSet::Flag::BlendType)
            formatter.WriteKeyedValue("BlendType", AsString(stateSet._blendType));

        if (stateSet._flag & RenderStateSet::Flag::DepthBias)
            formatter.WriteKeyedValue("DepthBias", AutoAsString(stateSet._depthBias));

        if (stateSet._flag & RenderStateSet::Flag::ForwardBlend) {
            auto ele = formatter.BeginKeyedElement("ForwardBlend");
            formatter.WriteKeyedValue("Src", AsString(stateSet._forwardBlendSrc));
            formatter.WriteKeyedValue("Dst", AsString(stateSet._forwardBlendDst));
            formatter.WriteKeyedValue("Op", AsString(stateSet._forwardBlendOp));
            formatter.EndElement(ele);
        }
    }

    static SamplerDesc DeserializeSamplerState(Formatters::TextInputFormatter<>& formatter)
    {
        // See also SamplerDesc ParseFixedSampler(ConditionalProcessingTokenizer& iterator) in PredefinedDescriptorSetLayout
        // Possibly we could create a IDynamicInputFormatter<> wrapper for ConditionalProcessingTokenizer and use that to make a single
        // deserialization method?
        char exceptionBuffer[256];
        SamplerDesc result{};
        StringSection<> keyname;
		while (formatter.TryKeyedItem(keyname)) {
			if (XlEqString(keyname, "Filter")) {
				auto value = RequireStringValue(formatter);
				auto filterMode = AsFilterMode(value);
				if (!filterMode)
					Throw(Formatters::FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown filter mode (" << value << ")", formatter.GetLocation()));
				result._filter = filterMode.value();
			} else if (XlEqString(keyname, "AddressU") || XlEqString(keyname, "AddressV") || XlEqString(keyname, "AddressW") ) {
				auto value = RequireStringValue(formatter);
                auto addressMode = AsAddressMode(value);
				if (!addressMode)
					Throw(Formatters::FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown address mode (" << value << ")", formatter.GetLocation()));
				if (XlEqString(keyname, "AddressU")) result._addressU = addressMode.value();
				if (XlEqString(keyname, "AddressV")) result._addressV = addressMode.value();
				else result._addressW = addressMode.value();
			} else if (XlEqString(keyname, "Comparison")) {
				auto value = RequireStringValue(formatter);
				auto compareMode = AsCompareOp(value);
				if (!compareMode)
					Throw(Formatters::FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown comparison mode (" << value << ")", formatter.GetLocation()));
				result._comparison = compareMode.value();
			} else {
				auto flag = AsSamplerDescFlag(keyname);
				if (!flag)
					Throw(Formatters::FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown sampler field (" << keyname << ")", formatter.GetLocation()));
				result._flags |= flag.value();
			}
		}

		return result;
    }

    static Formatters::TextOutputFormatter& SerializeSamplerDesc(Formatters::TextOutputFormatter& str, const SamplerDesc& sampler)
    {
        str.WriteKeyedValue("Filter", AsString(sampler._filter));
        str.WriteKeyedValue("AddressU", AsString(sampler._addressU));
        str.WriteKeyedValue("AddressV", AsString(sampler._addressV));
        str.WriteKeyedValue("AddressW", AsString(sampler._addressW));
        str.WriteKeyedValue("Comparison", AsString(sampler._comparison));
        for (auto f:{SamplerDescFlags::DisableMipmaps, SamplerDescFlags::UnnormalizedCoordinates})
            if (sampler._flags & f)
                str.WriteSequencedValue(SamplerDescFlagAsString(f));
        return str;
    }

    std::vector<std::pair<std::string, SamplerDesc>> DeserializeSamplerStates(Formatters::TextInputFormatter<>& formatter)
    {
        std::vector<std::pair<std::string, SamplerDesc>> result;
        StringSection<> keyName;
        while (formatter.TryKeyedItem(keyName)) {
            auto str = keyName.AsString();
            auto i = std::find_if(result.begin(), result.end(), [str](const auto& q) { return q.first==str; });
            if (i != result.end())
                Throw(Formatters::FormatException(StringMeld<256>() << "Multiple samplers with the same name (" << str << ")", formatter.GetLocation()));
            RequireBeginElement(formatter);
            result.emplace_back(str, DeserializeSamplerState(formatter));
            RequireEndElement(formatter);
        }
        return result;
    }

    static Formatters::TextOutputFormatter& SerializeSamplerStates(Formatters::TextOutputFormatter& str, const std::vector<std::pair<std::string, SamplerDesc>>& samplers)
    {
        for (const auto& s:samplers) {
            auto ele = str.BeginKeyedElement(s.first);
            SerializeSamplerDesc(str, s.second);
            str.EndElement(ele);
        }
        return str;
    }

    RenderStateSet Merge(RenderStateSet underride, RenderStateSet override)
    {
        RenderStateSet result = underride;
        if (override._flag & RenderStateSet::Flag::DoubleSided) {
            result._doubleSided = override._doubleSided;
            result._flag |= RenderStateSet::Flag::DoubleSided;
        }
        if (override._flag & RenderStateSet::Flag::SmoothLines) {
            result._smoothLines = override._smoothLines;
            result._flag |= RenderStateSet::Flag::SmoothLines;
        }
        if (override._flag & RenderStateSet::Flag::LineWeight) {
            result._lineWeight = override._lineWeight;
            result._flag |= RenderStateSet::Flag::LineWeight;
        }
        if (override._flag & RenderStateSet::Flag::WriteMask) {
            result._writeMask = override._writeMask;
            result._flag |= RenderStateSet::Flag::WriteMask;
        }
        if (override._flag & RenderStateSet::Flag::BlendType) {
            result._blendType = override._blendType;
            result._flag |= RenderStateSet::Flag::BlendType;
        }
        if (override._flag & RenderStateSet::Flag::ForwardBlend) {
            result._forwardBlendSrc = override._forwardBlendSrc;
            result._forwardBlendDst = override._forwardBlendDst;
            result._forwardBlendOp = override._forwardBlendOp;
            result._flag |= RenderStateSet::Flag::ForwardBlend;
        }
        if (override._flag & RenderStateSet::Flag::DepthBias) {
            result._depthBias = override._depthBias;
            result._flag |= RenderStateSet::Flag::DepthBias;
        }
        return result;
    }

    RawMaterial::RawMaterial() {}

    std::vector<::Assets::rstring> 
        DeserializeInheritList(Formatters::TextInputFormatter<utf8>& formatter)
    {
        std::vector<::Assets::rstring> result;
        while (formatter.PeekNext() == Formatters::FormatterBlob::Value)
            result.push_back(RequireStringValue(formatter).AsString());
        return result;
    }

    RawMaterial::RawMaterial(Formatters::TextInputFormatter<utf8>& formatter)
    {
        while (formatter.PeekNext() == Formatters::FormatterBlob::KeyedItem) {
            auto eleName = RequireKeyedItem(formatter);

                // first, load inherited settings.
            if (XlEqString(eleName, "Inherit")) {
                RequireBeginElement(formatter);
                _inherit = DeserializeInheritList(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "Selectors")) {
                RequireBeginElement(formatter);
                _selectors = ParameterBox(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "Uniforms")) {
                RequireBeginElement(formatter);
                _uniforms = ParameterBox(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "Resources")) {
                RequireBeginElement(formatter);
                _resources = ParameterBox(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "States")) {
                RequireBeginElement(formatter);
                _stateSet = DeserializeStateSet(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "Patches")) {
                RequireBeginElement(formatter);
                _patchCollection = ShaderPatchCollection(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "Samplers")) {
                RequireBeginElement(formatter);
                _samplers = DeserializeSamplerStates(formatter);
                RequireEndElement(formatter);
            } else {
                SkipValueOrElement(formatter);
            }
        }

        if (formatter.PeekNext() != Formatters::FormatterBlob::EndElement && formatter.PeekNext() != Formatters::FormatterBlob::None)
			Throw(Formatters::FormatException("Unexpected data while deserializating RawMaterial", formatter.GetLocation()));
    }

    RawMaterial::~RawMaterial() {}

    void RawMaterial::SerializeMethod(Formatters::TextOutputFormatter& formatter) const
    {
		if (!_patchCollection.GetPatches().empty()) {
			auto ele = formatter.BeginKeyedElement("Patches");
			SerializationOperator(formatter, _patchCollection);
			formatter.EndElement(ele);
		}

        if (!_inherit.empty()) {
            auto ele = formatter.BeginKeyedElement("Inherit");
            for (const auto& i:_inherit)
                formatter.WriteSequencedValue(i);
            formatter.EndElement(ele);
        }

        if (_selectors.GetCount() > 0) {
            auto ele = formatter.BeginKeyedElement("Selectors");
            _selectors.SerializeWithCharType<utf8>(formatter);
            formatter.EndElement(ele);
        }

        if (_uniforms.GetCount() > 0) {
            auto ele = formatter.BeginKeyedElement("Uniforms");
            _uniforms.SerializeWithCharType<utf8>(formatter);
            formatter.EndElement(ele);
        }

        if (_resources.GetCount() > 0) {
            auto ele = formatter.BeginKeyedElement("Resources");
            _resources.SerializeWithCharType<utf8>(formatter);
            formatter.EndElement(ele);
        }

        if (HasSomethingToSerialize(_stateSet)) {
            auto ele = formatter.BeginKeyedElement("States");
            SerializeStateSet(formatter, _stateSet);
            formatter.EndElement(ele);
        }

        if (!_samplers.empty()) {
            auto ele = formatter.BeginKeyedElement("Samplers");
            SerializeSamplerStates(formatter, _samplers);
            formatter.EndElement(ele);
        }
    }

    void RawMaterial::MergeInWithFilenameResolve(const RawMaterial& src, const ::Assets::DirectorySearchRules& searchRules)
	{
		_selectors.MergeIn(src._selectors);
        _stateSet = Merge(_stateSet, src._stateSet);
        _uniforms.MergeIn(src._uniforms);

        // Resolve all of the directory names here, as we write into the Techniques::Material
		for (const auto&b:src._resources) {
			auto unresolvedName = b.ValueAsString();
			if (!unresolvedName.empty()) {
				char resolvedName[MaxPath];
				searchRules.ResolveFile(resolvedName, unresolvedName);
				_resources.SetParameter(b.Name(), MakeStringSection(resolvedName));
			} else {
				_resources.SetParameter(b.Name(), MakeStringSection(unresolvedName));
			}
		}

        for (const auto& s:src._samplers) {
            auto i = std::find_if(_samplers.begin(), _samplers.end(), [n=s.first](const auto& q) { return q.first == n; });
            if (i != _samplers.end()) {
                i->second = s.second;
            } else
                _samplers.emplace_back(s);
        }
		_patchCollection.MergeInWithFilenameResolve(src._patchCollection, searchRules);
	}

#if 0
	void ResolveMaterialFilename(
        ::Assets::ResChar resolvedFile[], unsigned resolvedFileCount,
        const ::Assets::DirectorySearchRules& searchRules, StringSection<char> baseMatName)
    {
		auto splitName = MakeFileNameSplitter(baseMatName);
        searchRules.ResolveFile(resolvedFile, resolvedFileCount, splitName.AllExceptParameters());
		XlCatString(resolvedFile, resolvedFileCount, splitName.ParametersWithDivider());
    }

    auto RawMaterial::ResolveInherited(
        const ::Assets::DirectorySearchRules& searchRules) const -> std::vector<std::string>
    {
        std::vector<std::string> result;

        for (auto i=_inherit.cbegin(); i!=_inherit.cend(); ++i) {
            auto name = *i;

            auto* colon = XlFindCharReverse(name.c_str(), ':');
            if (colon) {
                ::Assets::ResChar resolvedFile[MaxPath];
                XlCopyNString(resolvedFile, name.c_str(), colon-name.c_str());
                ResolveMaterialFilename(resolvedFile, dimof(resolvedFile), searchRules, resolvedFile);
                
                StringMeld<MaxPath, ::Assets::ResChar> finalRawMatName;
                finalRawMatName << resolvedFile << colon;
                result.push_back(finalRawMatName.AsString());
            } else {
                result.push_back(name);
            }
        }

        return result;
    }
#endif

    void RawMaterial::BindSampler(const std::string& name, const SamplerDesc& sampler)
    {
        auto i = std::find_if(_samplers.begin(), _samplers.end(), [name](const auto& q) { return q.first == name; });
        if (i != _samplers.end()) {
            i->second = sampler;
        } else
            _samplers.emplace_back(name, sampler);
    }

    void RawMaterial::AddInherited(const std::string& value)
    {
        if (std::find(_inherit.begin(), _inherit.end(), value) == _inherit.end())
            _inherit.emplace_back(value);
    }

    uint64_t RawMaterial::CalculateHash(uint64_t seed) const
    {
        uint64_t hashes[] {
            _resources.GetHash(), _resources.GetParameterNamesHash(),
            _selectors.GetHash(), _selectors.GetParameterNamesHash(),
            _uniforms.GetHash(), _uniforms.GetParameterNamesHash(),
            _stateSet.GetHash(),
            _patchCollection.GetHash()
        };
        auto result = Hash64(MakeIteratorRange(hashes), seed);
        for (auto& s:_samplers)
            result = Hash64(s.first, s.second.Hash() + result);
        for (auto& i:_inherit)
            result = Hash64(i, result);
        return result;
    }

    RenderStateSet& RenderStateSet::SetDoubleSided(bool newValue)
    {
        _doubleSided = newValue;
        _flag |= Flag::DoubleSided;
        return *this;
    }

    RenderStateSet& RenderStateSet::SetSmoothLines(bool newValue)
    {
        _smoothLines = newValue;
        _flag |= Flag::SmoothLines;
        return *this;
    }

    RenderStateSet& RenderStateSet::SetLineWeight(float newValue)
    {
        assert(!(_flag & Flag::DepthBias));
        _lineWeight = newValue;
        _flag |= Flag::LineWeight;
        return *this;
    }

    RenderStateSet& RenderStateSet::SetWriteMask(unsigned newValue)
    {
        assert((newValue & ((1u<<4u)-1u)) == newValue);     // only lower 4 bits are used
        _writeMask = newValue;
        _flag |= Flag::WriteMask;
        return *this;
    }

    RenderStateSet& RenderStateSet::SetBlendType(BlendType newValue)
    {
        assert((unsigned(newValue) & ((1u<<4u)-1u)) == unsigned(newValue));
        _blendType = newValue;
        _flag |= Flag::BlendType;
        return *this;
    }

    RenderStateSet& RenderStateSet::SetForwardBlend(Blend src, Blend dst, BlendOp op)
    {
        assert((unsigned(src) & ((1u<<5u)-1u)) == unsigned(src));
        assert((unsigned(dst) & ((1u<<5u)-1u)) == unsigned(dst));
        assert((unsigned(op) & ((1u<<5u)-1u)) == unsigned(op));
        _forwardBlendSrc = src;
        _forwardBlendDst = dst;
        _forwardBlendOp = op;
        _flag |= Flag::ForwardBlend;
        return *this;
    }

    RenderStateSet& RenderStateSet::SetDepthBias(int newValue)
    {
        assert(!(_flag & Flag::LineWeight));
        _depthBias = newValue;
        _flag |= Flag::DepthBias;
        return *this;
    }

	RawMatConfigurations::RawMatConfigurations(
		const ::Assets::Blob& blob,
		const ::Assets::DependencyValidation& depVal,
		StringSection<::Assets::ResChar>)
    {
            //  Get associated "raw" material information. This is should contain the material information attached
            //  to the geometry export (eg, .dae file).

        if (blob && !blob->empty()) {
            Formatters::TextInputFormatter<utf8> formatter(MakeIteratorRange(*blob).template Cast<const void*>());

            StringSection<> keyName;
            while (formatter.TryKeyedItem(keyName)) {
                _configurations.push_back(keyName.AsString());
                SkipValueOrElement(formatter);
            }
        }

        _validationCallback = depVal;
    }

    static bool IsMaterialFile(StringSection<> extension) { return XlEqStringI(extension, "material"); }

    template<typename ObjectType>
        void CompilableMaterialAssetMixin<ObjectType>::ConstructToPromise(
            std::promise<std::shared_ptr<CompilableMaterialAssetMixin<ObjectType>>>&& promise,
            StringSection<::Assets::ResChar> initializer)
    {
        // If we're loading from a .material file, then just go head and use the
		// default asset construction
		// Otherwise, we need to invoke a compile and load of a ConfigFileContainer
		if (IsMaterialFile(MakeFileNameSplitter(initializer).Extension())) {
            ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
			    [init=initializer.AsString(), promise=std::move(promise)]() mutable {
                    TRY {
                        promise.set_value(::Assets::AutoConstructAsset<std::shared_ptr<CompilableMaterialAssetMixin<ObjectType>>>(init));
                    } CATCH (...) {
                        promise.set_exception(std::current_exception());
                    } CATCH_END
                });
			return;
		}

        ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[promise=std::move(promise), init=initializer.AsString()]() mutable {
                TRY {
                    auto splitName = MakeFileNameSplitter(init);
                    auto containerInitializer = splitName.AllExceptParameters();
                    std::string containerInitializerString = containerInitializer.AsString();
                    auto containerFuture = std::make_shared<::Assets::MarkerPtr<::Assets::ConfigFileContainer<>>>(containerInitializerString);
                    ::Assets::DefaultCompilerConstructionSynchronously(
                        containerFuture->AdoptPromise(),
                        s_MaterialCompileProcessType,
                        containerInitializer);

                    std::string section = splitName.Parameters().AsString();
                    ::Assets::WhenAll(containerFuture).ThenConstructToPromise(
                        std::move(promise),
                        [section, containerInitializerString](std::shared_ptr<::Assets::ConfigFileContainer<>> containerActual) {
                            auto fmttr = containerActual->GetFormatter(MakeStringSection(section));
                            return std::make_shared<CompilableMaterialAssetMixin<RawMaterial>>(
                                fmttr, 
                                ::Assets::DefaultDirectorySearchRules(containerInitializerString),
                                containerActual->GetDependencyValidation());
                        });
                } CATCH (...) {
                    promise.set_exception(std::current_exception());
                } CATCH_END
            });
	}

    template<typename ObjectType>
        void CompilableMaterialAssetMixin<ObjectType>::ConstructToPromise(
            std::promise<CompilableMaterialAssetMixin<ObjectType>>&& promise,
            StringSection<::Assets::ResChar> initializer)
    {
        // If we're loading from a .material file, then just go head and use the
		// default asset construction
		// Otherwise, we need to invoke a compile and load of a ConfigFileContainer
		if (IsMaterialFile(MakeFileNameSplitter(initializer).Extension())) {
            ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
			    [init=initializer.AsString(), promise=std::move(promise)]() mutable {
                    TRY {
                        promise.set_value(::Assets::AutoConstructAsset<CompilableMaterialAssetMixin<ObjectType>>(init));
                    } CATCH (...) {
                        promise.set_exception(std::current_exception());
                    } CATCH_END
                });
			return;
		}

        ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[promise=std::move(promise), init=initializer.AsString()]() mutable {
                TRY {
                    auto splitName = MakeFileNameSplitter(init);
                    auto containerInitializer = splitName.AllExceptParameters();
                    std::string containerInitializerString = containerInitializer.AsString();
                    auto containerFuture = std::make_shared<::Assets::MarkerPtr<::Assets::ConfigFileContainer<>>>(containerInitializerString);
                    ::Assets::DefaultCompilerConstructionSynchronously(
                        containerFuture->AdoptPromise(),
                        s_MaterialCompileProcessType,
                        containerInitializer);

                    std::string section = splitName.Parameters().AsString();
                    ::Assets::WhenAll(containerFuture).ThenConstructToPromise(
                        std::move(promise),
                        [section, containerInitializerString](std::shared_ptr<::Assets::ConfigFileContainer<>> containerActual) {
                            auto fmttr = containerActual->GetFormatter(MakeStringSection(section));
                            return CompilableMaterialAssetMixin<RawMaterial>(
                                fmttr, 
                                ::Assets::DefaultDirectorySearchRules(containerInitializerString),
                                containerActual->GetDependencyValidation());
                        });
                } CATCH (...) {
                    promise.set_exception(std::current_exception());
                } CATCH_END
            });
	}

    template<typename ObjectType>
        void CompilableMaterialAssetMixin<ObjectType>::ConstructToPromise(
            std::promise<std::shared_ptr<CompilableMaterialAssetMixin<ObjectType>>>&& promise,
            StringSection<> initializer, std::shared_ptr<ModelCompilationConfiguration> cfg)
    {
        ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[promise=std::move(promise), init=initializer.AsString(), cfg=std::move(cfg)]() mutable {
                TRY {
                    auto splitName = MakeFileNameSplitter(init);
                    auto containerInitializer = splitName.AllExceptParameters();
                    std::string containerInitializerString = containerInitializer.AsString();
                    auto containerFuture = std::make_shared<::Assets::MarkerPtr<::Assets::ConfigFileContainer<>>>(containerInitializerString);
                    ::Assets::DefaultCompilerConstructionSynchronously(
                        containerFuture->AdoptPromise(),
                        s_MaterialCompileProcessType,
                        ::Assets::InitializerPack{containerInitializer, std::move(cfg)});

                    std::string section = splitName.Parameters().AsString();
                    ::Assets::WhenAll(containerFuture).ThenConstructToPromise(
                        std::move(promise),
                        [section, containerInitializerString](std::shared_ptr<::Assets::ConfigFileContainer<>> containerActual) {
                            auto fmttr = containerActual->GetFormatter(MakeStringSection(section));
                            return std::make_shared<CompilableMaterialAssetMixin<RawMaterial>>(
                                fmttr, 
                                ::Assets::DefaultDirectorySearchRules(containerInitializerString),
                                containerActual->GetDependencyValidation());
                        });
                } CATCH (...) {
                    promise.set_exception(std::current_exception());
                } CATCH_END
            });
    }

    template<typename ObjectType>
        void CompilableMaterialAssetMixin<ObjectType>::ConstructToPromise(
            std::promise<CompilableMaterialAssetMixin<ObjectType>>&& promise,
            StringSection<> initializer, std::shared_ptr<ModelCompilationConfiguration> cfg)
    {
        ConsoleRig::GlobalServices::GetInstance().GetLongTaskThreadPool().Enqueue(
			[promise=std::move(promise), init=initializer.AsString(), cfg=std::move(cfg)]() mutable {
                TRY {
                    auto splitName = MakeFileNameSplitter(init);
                    auto containerInitializer = splitName.AllExceptParameters();
                    std::string containerInitializerString = containerInitializer.AsString();
                    auto containerFuture = std::make_shared<::Assets::MarkerPtr<::Assets::ConfigFileContainer<>>>(containerInitializerString);
                    ::Assets::DefaultCompilerConstructionSynchronously(
                        containerFuture->AdoptPromise(),
                        s_MaterialCompileProcessType,
                        ::Assets::InitializerPack{containerInitializer, std::move(cfg)});

                    std::string section = splitName.Parameters().AsString();
                    ::Assets::WhenAll(containerFuture).ThenConstructToPromise(
                        std::move(promise),
                        [section, containerInitializerString](std::shared_ptr<::Assets::ConfigFileContainer<>> containerActual) {
                            auto fmttr = containerActual->GetFormatter(MakeStringSection(section));
                            return CompilableMaterialAssetMixin<RawMaterial>(
                                fmttr, 
                                ::Assets::DefaultDirectorySearchRules(containerInitializerString),
                                containerActual->GetDependencyValidation());
                        });
                } CATCH (...) {
                    promise.set_exception(std::current_exception());
                } CATCH_END
            });
    }

    template class CompilableMaterialAssetMixin<RawMaterial>;
}}

