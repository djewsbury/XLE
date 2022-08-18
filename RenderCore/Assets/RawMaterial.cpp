// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RawMaterial.h"
#include "../StateDesc.h"
#include "../../Assets/Assets.h"
#include "../../Assets/DeferredConstruction.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/Continuation.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/OutputStreamFormatter.h"
#include "../../Utility/Streams/StreamDOM.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Streams/FormatterUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"
#include "../../Utility/StringFormat.h"

namespace RenderCore { namespace Assets
{

	static const auto s_MaterialCompileProcessType = ConstHash64<'RawM', 'at'>::Value;

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
        const StreamDOMElement<InputStreamFormatter<utf8>>& ele, const utf8 name[])
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
        const StreamDOMElement<InputStreamFormatter<utf8>>& ele, const utf8 name[])
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

    static RenderStateSet DeserializeStateSet(InputStreamFormatter<utf8>& formatter)
    {
        RenderStateSet result;

        StreamDOM<InputStreamFormatter<utf8>> doc(formatter);
        auto rootElement = doc.RootElement();

        {
            auto child = rootElement.Attribute("DoubleSided").As<bool>();
            if (child.has_value()) {
                result._doubleSided = child.value();
                result._flag |= RenderStateSet::Flag::DoubleSided;
            }
        }
        {
            auto child = rootElement.Attribute("Wireframe").As<bool>();
            if (child.has_value()) {
                result._wireframe = child.value();
                result._flag |= RenderStateSet::Flag::Wireframe;
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

    static void SerializeStateSet(OutputStreamFormatter& formatter, const RenderStateSet& stateSet)
    {
        if (stateSet._flag & RenderStateSet::Flag::DoubleSided)
            formatter.WriteKeyedValue("DoubleSided", AutoAsString(stateSet._doubleSided));

        if (stateSet._flag & RenderStateSet::Flag::Wireframe)
            formatter.WriteKeyedValue("Wireframe", AutoAsString(stateSet._wireframe));

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

    static SamplerDesc DeserializeSamplerState(InputStreamFormatter<>& formatter)
    {
        // See also SamplerDesc ParseFixedSampler(ConditionalProcessingTokenizer& iterator) in PredefinedDescriptorSetLayout
        // Possibly we could create a IDynamicFormatter<> wrapper for ConditionalProcessingTokenizer and use that to make a single
        // deserialization method?
        char exceptionBuffer[256];
        SamplerDesc result{};
        StringSection<> keyname;
		while (formatter.TryKeyedItem(keyname)) {
			if (XlEqString(keyname, "Filter")) {
				auto value = RequireStringValue(formatter);
				auto filterMode = AsFilterMode(value);
				if (!filterMode)
					Throw(FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown filter mode (" << value << ")", formatter.GetLocation()));
				result._filter = filterMode.value();
			} else if (XlEqString(keyname, "AddressU") || XlEqString(keyname, "AddressV") || XlEqString(keyname, "AddressW") ) {
				auto value = RequireStringValue(formatter);
                auto addressMode = AsAddressMode(value);
				if (!addressMode)
					Throw(FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown address mode (" << value << ")", formatter.GetLocation()));
				if (XlEqString(keyname, "AddressU")) result._addressU = addressMode.value();
				if (XlEqString(keyname, "AddressV")) result._addressV = addressMode.value();
				else result._addressW = addressMode.value();
			} else if (XlEqString(keyname, "Comparison")) {
				auto value = RequireStringValue(formatter);
				auto compareMode = AsCompareOp(value);
				if (!compareMode)
					Throw(FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown comparison mode (" << value << ")", formatter.GetLocation()));
				result._comparison = compareMode.value();
			} else {
				auto flag = AsSamplerDescFlag(keyname);
				if (!flag)
					Throw(FormatException(StringMeldInPlace(exceptionBuffer) << "Unknown sampler field (" << keyname << ")", formatter.GetLocation()));
				result._flags |= flag.value();
			}
		}

		return result;
    }

    static OutputStreamFormatter& SerializeSamplerDesc(OutputStreamFormatter& str, const SamplerDesc& sampler)
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

    std::vector<std::pair<std::string, SamplerDesc>> DeserializeSamplerStates(InputStreamFormatter<>& formatter)
    {
        std::vector<std::pair<std::string, SamplerDesc>> result;
        StringSection<> keyName;
        while (formatter.TryKeyedItem(keyName)) {
            auto str = keyName.AsString();
            auto i = std::find_if(result.begin(), result.end(), [str](const auto& q) { return q.first==str; });
            if (i != result.end())
                Throw(FormatException(StringMeld<256>() << "Multiple samplers with the same name (" << str << ")", formatter.GetLocation()));
            RequireBeginElement(formatter);
            result.emplace_back(str, DeserializeSamplerState(formatter));
            RequireEndElement(formatter);
        }
        return result;
    }

    static OutputStreamFormatter& SerializeSamplerStates(OutputStreamFormatter& str, const std::vector<std::pair<std::string, SamplerDesc>>& samplers)
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
        if (override._flag & RenderStateSet::Flag::Wireframe) {
            result._wireframe = override._wireframe;
            result._flag |= RenderStateSet::Flag::Wireframe;
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
        DeserializeInheritList(InputStreamFormatter<utf8>& formatter)
    {
        std::vector<::Assets::rstring> result;
        while (formatter.PeekNext() == FormatterBlob::Value)
            result.push_back(RequireStringValue(formatter).AsString());
        return result;
    }

    RawMaterial::RawMaterial(
		InputStreamFormatter<utf8>& formatter, 
		const ::Assets::DirectorySearchRules& searchRules, 
		const ::Assets::DependencyValidation& depVal)
	: _depVal(depVal), _searchRules(searchRules)
    {
        while (formatter.PeekNext() == FormatterBlob::KeyedItem) {
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
                _patchCollection = ShaderPatchCollection(formatter, searchRules, depVal);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "Samplers")) {
                RequireBeginElement(formatter);
                _samplers = DeserializeSamplerStates(formatter);
                RequireEndElement(formatter);
            } else {
                SkipValueOrElement(formatter);
            }
        }

        if (formatter.PeekNext() != FormatterBlob::EndElement && formatter.PeekNext() != FormatterBlob::None)
			Throw(FormatException("Unexpected data while deserializating RawMaterial", formatter.GetLocation()));
    }

    RawMaterial::~RawMaterial() {}

    void RawMaterial::SerializeMethod(OutputStreamFormatter& formatter) const
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

	void RawMaterial::MergeIn(const RawMaterial& src)
	{
		_selectors.MergeIn(src._selectors);
        _stateSet = Merge(_stateSet, src._stateSet);
        _uniforms.MergeIn(src._uniforms);
        _resources.MergeIn(src._resources);
        for (const auto& s:src._samplers) {
            auto i = std::find_if(_samplers.begin(), _samplers.end(), [n=s.first](const auto& q) { return q.first == n; });
            if (i != _samplers.end()) {
                i->second = s.second;
            } else
                _samplers.emplace_back(s);
        }
		_patchCollection.MergeIn(src._patchCollection);
	}

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

    void RawMaterial::BindSampler(const std::string& name, const SamplerDesc& sampler)
    {
        auto i = std::find_if(_samplers.begin(), _samplers.end(), [name](const auto& q) { return q.first == name; });
        if (i != _samplers.end()) {
            i->second = sampler;
        } else
            _samplers.emplace_back(name, sampler);
    }

    void RawMaterial::AddInheritted(const std::string& value)
    {
        if (std::find(_inherit.begin(), _inherit.end(), value) == _inherit.end())
            _inherit.emplace_back(value);
    }

    RenderStateSet& RenderStateSet::SetDoubleSided(bool newValue)
    {
        _doubleSided = newValue;
        _flag |= Flag::DoubleSided;
        return *this;
    }

    RenderStateSet& RenderStateSet::SetWireframe(bool newValue)
    {
        _wireframe = newValue;
        _flag |= Flag::Wireframe;
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
        _depthBias = newValue;
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
            InputStreamFormatter<utf8> formatter(MakeIteratorRange(*blob).template Cast<const void*>());

            StringSection<> keyName;
            while (formatter.TryKeyedItem(keyName)) {
                _configurations.push_back(keyName.AsString());
                SkipValueOrElement(formatter);
            }
        }

        _validationCallback = depVal;
    }

	static bool IsMaterialFile(StringSection<> extension) { return XlEqStringI(extension, "material") || XlEqStringI(extension, "hlsl"); }

	void RawMaterial::ConstructToPromise(
		std::promise<std::shared_ptr<RawMaterial>>&& promise,
		StringSection<> initializer)
	{
		// If we're loading from a .material file, then just go head and use the
		// default asset construction
		// Otherwise, we need to invoke a compile and load of a ConfigFileContainer
		if (IsMaterialFile(MakeFileNameSplitter(initializer).Extension())) {
            ConsoleRig::GlobalServices::GetInstance().GetShortTaskThreadPool().Enqueue(
			    [init=initializer.AsString(), promise=std::move(promise)]() mutable {
                    TRY {
                        promise.set_value(::Assets::AutoConstructAsset<std::shared_ptr<RawMaterial>>(init));
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
                            return std::make_shared<RawMaterial>(
                                fmttr, 
                                ::Assets::DefaultDirectorySearchRules(containerInitializerString),
                                containerActual->GetDependencyValidation());
                        });
                } CATCH (...) {
                    promise.set_exception(std::current_exception());
                } CATCH_END
            });
	}

    ResolvedMaterial::ResolvedMaterial() {}
    ResolvedMaterial::~ResolvedMaterial() {}
    
    static void MergeIn(ResolvedMaterial& dest, const RawMaterial& source);

    void ResolvedMaterial::ConstructToPromise(
        std::promise<ResolvedMaterial>&& promisedMaterial,
        StringSection<> initializer)
    {
        // We have to load an entire tree of RawMaterials and their inherited items.
        // We'll do this all with one future in such a way that we create a linear
        // list of all of the RawMaterials in the order that they need to be merged in
        // We do this in a kind of breadth first way, were we queue up all of the futures
        // for a given level together
        class PendingRawMaterialTree
        {
        public:
            unsigned _nextId = 1;
            std::vector<std::pair<unsigned, ::Assets::PtrToMarkerPtr<RawMaterial>>> _subFutures;
            std::vector<std::pair<unsigned, std::shared_ptr<RawMaterial>>> _loadedSubMaterials;
            std::vector<::Assets::DependencyValidation> _depVals;
        };
        auto pendingTree = std::make_shared<PendingRawMaterialTree>();

        auto i = initializer.begin();
        while (i != initializer.end()) {
            while (i != initializer.end() && *i == ';') ++i;
            auto i2 = i;
            while (i2 != initializer.end() && *i2 != ';') ++i2;
            if (i2==i) break;

            pendingTree->_subFutures.push_back(std::make_pair(0, ::Assets::MakeAssetMarker<std::shared_ptr<RawMaterial>>(MakeStringSection(i, i2))));
            i = i2;
        }
        assert(!pendingTree->_subFutures.empty());

        ::Assets::PollToPromise(
            std::move(promisedMaterial),
            [pendingTree]() {
                for (;;) {
                    ::Assets::AssetState currentState = ::Assets::AssetState::Ready;
                    std::vector<std::pair<unsigned, std::shared_ptr<RawMaterial>>> subMaterials;
                    std::vector<::Assets::DependencyValidation> subDepVals;
                    for (const auto& f:pendingTree->_subFutures) {
                        ::Assets::Blob queriedLog;
                        ::Assets::DependencyValidation queriedDepVal;
                        std::shared_ptr<RawMaterial> subMat;
                        auto state = f.second->CheckStatusBkgrnd(subMat, queriedDepVal, queriedLog);
                        if (state == ::Assets::AssetState::Pending)
                            return ::Assets::PollStatus::Continue;

                        // "invalid" is actually ok here. we include the dep val as normal, but ignore
                        // the RawMaterial

                        subDepVals.push_back(queriedDepVal);
                        if (state == ::Assets::AssetState::Ready)
                            subMaterials.push_back(std::make_pair(f.first, std::move(subMat)));
                    }
                    pendingTree->_subFutures.clear();
                    pendingTree->_depVals.insert(pendingTree->_depVals.end(), subDepVals.begin(), subDepVals.end());

                    // merge these RawMats into _loadedSubMaterials in the right places
                    // also queue the next level of loads as we go
                    // We want each subMaterial to go into _loadedSubMaterials in the same order as 
                    // in subMaterials, but immediately before their parent
                    for (const auto&m:subMaterials) {
                        unsigned newParentId = pendingTree->_nextId++;
                        if (m.first == 0) {
                            pendingTree->_loadedSubMaterials.push_back({newParentId, m.second});
                        } else {
                            auto i = std::find_if(pendingTree->_loadedSubMaterials.begin(), pendingTree->_loadedSubMaterials.end(),
                                [s=m.first](const auto& c) { return c.first == s;});
                            assert(i!=pendingTree->_loadedSubMaterials.end());
                            // insert just before the parent, after any siblings added this turn 
                            pendingTree->_loadedSubMaterials.insert(i, {newParentId, m.second});
                        }

                        auto inheritted = m.second->ResolveInherited(m.second->GetDirectorySearchRules());
                        for (const auto&i:inheritted) {
                            pendingTree->_subFutures.push_back(std::make_pair(newParentId, ::Assets::MakeAssetMarker<std::shared_ptr<RawMaterial>>(i)));
                        }
                    }

                    // if we still have subfutures, need to roll around again
                    // we'll do this immediately, just incase everything is already loaded
                    if (pendingTree->_subFutures.empty()) break;
                }
                // survived the gauntlet -- everything is ready to dispatch now
                return ::Assets::PollStatus::Finish;
            },
            [pendingTree]() {
                // All of the RawMaterials in the tree are loaded; and we can just merge them together
                // into a final resolved material
                ResolvedMaterial finalMaterial;
                for (const auto& m:pendingTree->_loadedSubMaterials)
                    MergeIn(finalMaterial, *m.second);

                ::Assets::DependencyValidationMarker depVals[pendingTree->_depVals.size()];
                for (unsigned c=0; c<pendingTree->_depVals.size(); c++) depVals[c] = pendingTree->_depVals[c];
                finalMaterial._depVal = ::Assets::GetDepValSys().MakeOrReuse(MakeIteratorRange(depVals, &depVals[pendingTree->_depVals.size()]));
                return finalMaterial;
            });
    }

    void MergeIn(ResolvedMaterial& dest, const RawMaterial& source)
    {
        dest._selectors.MergeIn(source._selectors);
        dest._stateSet = Merge(dest._stateSet, source._stateSet);
        dest._uniforms.MergeIn(source._uniforms);

		// Resolve all of the directory names here, as we write into the Techniques::Material
		for (const auto&b:source._resources) {
			auto unresolvedName = b.ValueAsString();
			if (!unresolvedName.empty()) {
				char resolvedName[MaxPath];
				source.GetDirectorySearchRules().ResolveFile(resolvedName, unresolvedName);
				dest._resources.SetParameter(b.Name(), MakeStringSection(resolvedName));
			} else {
				dest._resources.SetParameter(b.Name(), MakeStringSection(unresolvedName));
			}
		}

        for (const auto& s:source._samplers) {
            auto i = std::find_if(dest._samplers.begin(), dest._samplers.end(), [n=s.first](const auto& q) { return q.first == n; });
            if (i != dest._samplers.end()) {
                i->second = s.second;
            } else
                dest._samplers.emplace_back(s);
        }

		dest._patchCollection.MergeIn(source._patchCollection);
    }

}}

