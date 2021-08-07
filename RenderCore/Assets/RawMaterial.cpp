// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RawMaterial.h"
#include "../StateDesc.h"
#include "../../Assets/Assets.h"
#include "../../Assets/IntermediatesStore.h"		// (for GetDependentFileState)
#include "../../Assets/DeferredConstruction.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/AssetFutureContinuation.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/OutputStreamFormatter.h"
#include "../../Utility/Streams/StreamDOM.h"
#include "../../Utility/Streams/PathUtils.h"
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
            result.push_back(RequireValue(formatter).AsString());
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
            } else if (XlEqString(eleName, "ShaderParams")) {
                RequireBeginElement(formatter);
                _matParamBox = ParameterBox(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "Constants")) {
                RequireBeginElement(formatter);
                _constants = ParameterBox(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "ResourceBindings")) {
                RequireBeginElement(formatter);
                _resourceBindings = ParameterBox(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "States")) {
                RequireBeginElement(formatter);
                _stateSet = DeserializeStateSet(formatter);
                RequireEndElement(formatter);
            } else if (XlEqString(eleName, "Patches")) {
                RequireBeginElement(formatter);
                _patchCollection = ShaderPatchCollection(formatter, searchRules, depVal);
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

        if (_matParamBox.GetCount() > 0) {
            auto ele = formatter.BeginKeyedElement("ShaderParams");
            _matParamBox.SerializeWithCharType<utf8>(formatter);
            formatter.EndElement(ele);
        }

        if (_constants.GetCount() > 0) {
            auto ele = formatter.BeginKeyedElement("Constants");
            _constants.SerializeWithCharType<utf8>(formatter);
            formatter.EndElement(ele);
        }

        if (_resourceBindings.GetCount() > 0) {
            auto ele = formatter.BeginKeyedElement("ResourceBindings");
            _resourceBindings.SerializeWithCharType<utf8>(formatter);
            formatter.EndElement(ele);
        }

        if (HasSomethingToSerialize(_stateSet)) {
            auto ele = formatter.BeginKeyedElement("States");
            SerializeStateSet(formatter, _stateSet);
            formatter.EndElement(ele);
        }
    }

	void RawMaterial::MergeInto(RawMaterial& dest) const
	{
		dest._matParamBox.MergeIn(_matParamBox);
        dest._stateSet = Merge(dest._stateSet, _stateSet);
        dest._constants.MergeIn(_constants);
        dest._resourceBindings.MergeIn(_resourceBindings);
		_patchCollection.MergeInto(dest._patchCollection);
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
        const ::Assets::DirectorySearchRules& searchRules) const -> std::vector<AssetName>
    {
        std::vector<AssetName> result;

        for (auto i=_inherit.cbegin(); i!=_inherit.cend(); ++i) {
            auto name = *i;

            auto* colon = XlFindCharReverse(name.c_str(), ':');
            if (colon) {
                ::Assets::ResChar resolvedFile[MaxPath];
                XlCopyNString(resolvedFile, name.c_str(), colon-name.c_str());
                ResolveMaterialFilename(resolvedFile, dimof(resolvedFile), searchRules, resolvedFile);
                
                StringMeld<MaxPath, ::Assets::ResChar> finalRawMatName;
                finalRawMatName << resolvedFile << colon;
                result.push_back((AssetName)finalRawMatName);
            } else {
                result.push_back(name);
            }
        }

        return result;
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

	static bool IsMaterialFile(StringSection<::Assets::ResChar> extension) { return XlEqStringI(extension, "material") || XlEqStringI(extension, "hlsl"); }

	void RawMaterial::ConstructToFuture(
		::Assets::FuturePtr<RawMaterial>& future,
		StringSection<::Assets::ResChar> initializer)
	{
		// If we're loading from a .material file, then just go head and use the
		// default asset construction
		// Otherwise, we need to invoke a compile and load of a ConfigFileContainer
		auto splitName = MakeFileNameSplitter(initializer);
		if (IsMaterialFile(splitName.Extension())) {
			AutoConstructToFutureDirect(future, initializer);
			return;
		}

		// 
        auto containerInitializer = splitName.AllExceptParameters();
		auto containerFuture = std::make_shared<::Assets::FuturePtr<::Assets::ConfigFileContainer<>>>(containerInitializer.AsString());
		::Assets::DefaultCompilerConstruction(
			*containerFuture, 
            s_MaterialCompileProcessType,
			containerInitializer);

		std::string containerInitializerString = containerInitializer.AsString();
		std::string section = splitName.Parameters().AsString();
        ::Assets::WhenAll(containerFuture).ThenConstructToFuture(
            future,
            [section, containerInitializerString](std::shared_ptr<::Assets::ConfigFileContainer<>> containerActual) {
                auto fmttr = containerActual->GetFormatter(MakeStringSection(section));
                return std::make_shared<RawMaterial>(
                    fmttr, 
                    ::Assets::DefaultDirectorySearchRules(containerInitializerString),
                    containerActual->GetDependencyValidation());
            });
	}

	void MergeInto(MaterialScaffold::Material& dest, ShaderPatchCollection& destPatchCollection, const RawMaterial& source)
    {
        dest._matParams.MergeIn(source._matParamBox);
        dest._stateSet = Merge(dest._stateSet, source._stateSet);
        dest._constants.MergeIn(source._constants);

		// Resolve all of the directory names here, as we write into the Techniques::Material
		for (const auto&b:source._resourceBindings) {
			auto unresolvedName = b.ValueAsString();
			if (!unresolvedName.empty()) {
				char resolvedName[MaxPath];
				source.GetDirectorySearchRules().ResolveFile(resolvedName, unresolvedName);
				dest._bindings.SetParameter(b.Name(), MakeStringSection(resolvedName));
			} else {
				dest._bindings.SetParameter(b.Name(), MakeStringSection(unresolvedName));
			}
		}

		source._patchCollection.MergeInto(destPatchCollection);
		dest._patchCollection = destPatchCollection.GetHash();
    }

	static void AddDep(
        std::vector<::Assets::DependentFileState>& deps,
        StringSection<::Assets::ResChar> newDep)
    {
            // we need to call "GetDependentFileState" first, because this can change the
            // format of the filename. String compares alone aren't working well for us here
        auto depState = ::Assets::IntermediatesStore::GetDependentFileState(newDep);
        auto existing = std::find_if(
            deps.cbegin(), deps.cend(),
            [&](const ::Assets::DependentFileState& test) { return test._filename == depState._filename; });
        if (existing == deps.cend())
            deps.push_back(depState);
    }

	void MergeIn_Stall(
		MaterialScaffold::Material& result,
		ShaderPatchCollection& patchCollectionResult,
		StringSection<> sourceMaterialName,
        const ::Assets::DirectorySearchRules& searchRules,
        std::vector<::Assets::DependentFileState>& deps)
    {

            // resolve all of the inheritance options and generate a final 
            // ResolvedMaterial object. We need to start at the bottom of the
            // inheritance tree, and merge in new parameters as we come across them.

			// we still need to add a dependency, even if it's a missing file
		AddDep(deps, MakeFileNameSplitter(sourceMaterialName).AllExceptParameters());

		auto dependencyMat = ::Assets::MakeAsset<RawMaterial>(sourceMaterialName);
		auto state = dependencyMat->StallWhilePending();
		if (state == ::Assets::AssetState::Ready) {
			auto source = dependencyMat->Actualize();

			auto childSearchRules = source->GetDirectorySearchRules();
			childSearchRules.Merge(searchRules);

			for (const auto& child: source->ResolveInherited(searchRules))
				MergeIn_Stall(result, patchCollectionResult, child, childSearchRules, deps);

			MergeInto(result, patchCollectionResult, *source);
		}
    }

	void MergeIn_Stall(
		MaterialScaffold::Material& result,
		ShaderPatchCollection& patchCollectionResult,
		const RenderCore::Assets::RawMaterial& src,
		const ::Assets::DirectorySearchRules& searchRules)
	{
		auto childSearchRules = searchRules;
		childSearchRules.Merge(src.GetDirectorySearchRules());

		for (const auto& child : src.ResolveInherited(childSearchRules)) {
			auto dependencyMat = ::Assets::MakeAsset<RawMaterial>(child);
			auto state = dependencyMat->StallWhilePending();
			if (state == ::Assets::AssetState::Ready) {
				MergeIn_Stall(result, patchCollectionResult, *dependencyMat->Actualize(), childSearchRules);
			}
		}

		MergeInto(result, patchCollectionResult, src);
	}

	MaterialGuid MakeMaterialGuid(StringSection<utf8> name)
	{
		//  If the material name is just a number, then we will use that
		//  as the guid. Otherwise we hash the name.
		const char* parseEnd = nullptr;
		uint64 hashId = XlAtoI64((const char*)name.begin(), &parseEnd, 16);
		if (!parseEnd || parseEnd != (const char*)name.end()) { hashId = Hash64(name.begin(), name.end()); }
		return hashId;
	}

}}

