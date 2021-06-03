// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "ModelRunTime.h"
#include "ModelRendererInternal.h"
#include "DelayedDrawCall.h"
#include "SharedStateSet.h"

#include "../RenderCore/Assets/ModelImmutableData.h"
#include "../RenderCore/Assets/MaterialScaffold.h"
#include "../RenderCore/Assets/RawAnimationCurve.h"
#include "../RenderCore/Techniques/DeferredShaderResource.h"
#include "../RenderCore/Assets/AssetUtils.h" // for IsDXTNormalMap
#include "../RenderCore/Techniques/Techniques.h"
#include "../RenderCore/Techniques/ParsingContext.h"
#include "../RenderCore/Techniques/CommonResources.h"
#include "../RenderCore/Assets/PredefinedCBLayout.h"

#include "../RenderCore/Metal/Buffer.h"
#include "../RenderCore/Metal/State.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/ObjectFactory.h"
#include "../RenderCore/RenderUtils.h"
#include "../RenderCore/Types.h"
#include "../RenderCore/Format.h"
#include "../RenderCore/BufferView.h"

#include "../Assets/IFileSystem.h"
#include "../Assets/Assets.h"
#include "../Utility/PtrUtils.h"
#include "../OSServices/RawFS.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/StringFormat.h"
#include "../Math/Transformations.h"
#include "../ConsoleRig/Console.h"
#include "../OSServices/Log.h"
#include "../ConsoleRig/ResourceBox.h"
#include "../Core/Exceptions.h"
#include "../xleres/FileList.h"

#include <string>
#include <set>

#include "../RenderCore/Metal/DeviceContextImpl.h" // pulls in DX/Windows indirectly

#pragma warning(disable:4189)

namespace FixedFunctionModel
{
    using ::Assets::ResChar;
	using namespace RenderCore;

    /// <summary>Internal namespace with utilities for constructing models</summary>
    /// These functions are normally used within the constructor of ModelRenderer
    namespace ModelConstruction
    {
        static size_t InsertOrCombine(std::vector<std::vector<uint8>>& dest, std::vector<uint8>&& compare)
        {
            assert(compare.size());
            for (auto i = dest.cbegin(); i!=dest.cend(); ++i) {
                if (i->size() == compare.size() && (XlCompareMemory(AsPointer(i->begin()), AsPointer(compare.begin()), i->size()) == 0)) {
                    return std::distance(dest.cbegin(), i);
                }
            }

            dest.push_back(std::forward<std::vector<uint8>>(compare));
            return dest.size()-1;
        }

        struct SubMatResources
        { 
            SharedTechniqueConfig _shaderName; 
            SharedParameterBox _matParams; 
            unsigned _constantBuffer; 
            unsigned _texturesIndex; 
            SharedRenderStateSet _renderStateSet;
            DelayStep _delayStep;
        };

        static const RenderCore::Assets::ModelCommandStream::GeoCall& GetGeoCall(const RenderCore::Assets::ModelScaffold& scaffold, unsigned geoCallIndex)
        {
                //  get the "RawGeometry" object in the given scaffold for the give
                //  geocall index. This will query both unskinned and skinned raw calls
            auto& cmdStream = scaffold.CommandStream();
            auto geoCallCount = cmdStream.GetGeoCallCount();

            return (geoCallIndex < geoCallCount) ? cmdStream.GetGeoCall(geoCallIndex) : cmdStream.GetSkinCall(geoCallIndex - geoCallCount);
        }
        
        static const RenderCore::Assets::RawGeometry& GetGeo(const RenderCore::Assets::ModelScaffold& scaffold, unsigned geoCallIndex)
        {
                //  get the "RawGeometry" object in the given scaffold for the give
                //  geocall index. This will query both unskinned and skinned raw calls
            auto& meshData = scaffold.ImmutableData();
            auto geoCallCount = scaffold.CommandStream().GetGeoCallCount();

            auto& geoCall = GetGeoCall(scaffold, geoCallIndex);
            return (geoCallIndex < geoCallCount) ? meshData._geos[geoCall._geoId] : (RenderCore::Assets::RawGeometry&)meshData._boundSkinnedControllers[geoCall._geoId];
        }

        static unsigned GetDrawCallCount(const RenderCore::Assets::ModelScaffold& scaffold, unsigned geoCallIndex)
        {
            return (unsigned)GetGeo(scaffold, geoCallIndex)._drawCalls.size();
        }

        static MaterialGuid ScaffoldMaterialIndex(const RenderCore::Assets::ModelScaffold& scaffold, unsigned geoCallIndex, unsigned drawCallIndex)
        {
            auto& meshData = scaffold.ImmutableData();
            auto geoCallCount = scaffold.CommandStream().GetGeoCallCount();

            auto& geoCall = GetGeoCall(scaffold, geoCallIndex);
            auto& geo = (geoCallIndex < geoCallCount) ? meshData._geos[geoCall._geoId] : (RenderCore::Assets::RawGeometry&)meshData._boundSkinnedControllers[geoCall._geoId];
            unsigned subMatI = geo._drawCalls[drawCallIndex]._subMaterialIndex;

                //  the "sub material index" in the draw call is an index
                //  into the array in the geo call
            if (subMatI < geoCall._materialCount) {
                return geoCall._materialGuids[subMatI];
            }

            return ~unsigned(0x0);
        }

        template <typename T> static bool AtLeastOneValidDrawCall(const RenderCore::Assets::RawGeometry& geo, const RenderCore::Assets::ModelScaffold& scaffold, unsigned geoCallIndex, std::vector<std::pair<MaterialGuid, T>>& subMatResources)
        {
                //  look for at least one valid draw call in this geo instance
                //  a valid draw call should have got shader and material information bound
            bool atLeastOneValidDrawCall = false;
            for (unsigned di=0; di<unsigned(geo._drawCalls.size()); ++di) {
                auto& d = geo._drawCalls[di];
                if (d._indexCount) {
                    auto subMatIndex = ScaffoldMaterialIndex(scaffold, geoCallIndex, di);
                    auto snm = LowerBound(subMatResources, subMatIndex);
                    if (snm != subMatResources.cend() && snm->first == subMatIndex) {
                        return true;
                    }
                }
            }

            return false;       // didn't find any draw calls with good material information. This whole geo object can be ignored.
        }

        static void LoadBlock(::Assets::IFileInterface& file, uint8 destination[], size_t fileOffset, size_t readSize)
        {
            file.Seek(fileOffset);
            file.Read(destination, 1, readSize);
        }

        static bool HasElement(const RenderCore::Assets::GeoInputAssembly& ia, const char name[])
        {
            return std::find_if(
                ia._elements.cbegin(), ia._elements.cend(), 
                [=](const RenderCore::Assets::VertexElement& ele) { return !XlCompareStringI(ele._semanticName, name); }) != ia._elements.cend();
        }

        #if defined(_DEBUG)
            static std::string MakeDescription(const ParameterBox& paramBox)
            {
                std::vector<std::pair<const utf8*, std::string>> defines;
                BuildStringTable(defines, paramBox);
                std::stringstream dst;
                for (auto i=defines.cbegin(); i!=defines.cend(); ++i) {
                    if (i != defines.cbegin()) { dst << "; "; }
                    dst << i->first << " = " << i->second;
                }
                return dst.str();
            }
            
            class ParamBoxDescriptions
            {
            public:
                void Add(SharedParameterBox index, const ParameterBox& box)
                {
                    auto existing = LowerBound(_descriptions, index);
                    if (existing == _descriptions.end() || existing->first != index) {
                        _descriptions.insert(existing, std::make_pair(index, MakeDescription(box)));
                    }
                }
                std::vector<std::pair<SharedParameterBox,std::string>> _descriptions;
            };
        #else
            class ParamBoxDescriptions
            {
            public:
                void Add(SharedParameterBox index, const ParameterBox& box) {}
            };
        #endif

        static SharedParameterBox BuildGeoParamBox(
            const InputLayout& ia, 
            SharedStateSet& sharedStateSet, 
            ModelConstruction::ParamBoxDescriptions& paramBoxDesc, bool normalFromSkinning)
        {
                //  Build a parameter box for this geometry configuration. The input assembly
            ParameterBox geoSelectors;
			Techniques::SetGeoSelectors(geoSelectors, ia);
			if (normalFromSkinning)
				geoSelectors.SetParameter((const utf8*)"GEO_HAS_NORMAL", 1);            
            auto result = sharedStateSet.InsertParameterBox(geoSelectors);
            paramBoxDesc.Add(result, geoSelectors);
            return result;
        }

        static const auto DefaultNormalsTextureBindingHash = ParameterBox::MakeParameterNameHash("NormalsTexture");

        static std::vector<std::pair<MaterialGuid, SubMatResources>> BuildMaterialResources(
            const RenderCore::Assets::ModelScaffold& scaffold, const RenderCore::Assets::MaterialScaffold& matScaffold,
            SharedStateSet& sharedStateSet, unsigned levelOfDetail,
            std::vector<uint64>& textureBindPoints,
            std::vector<std::vector<uint8>>& prescientMaterialConstantBuffers,
            ParamBoxDescriptions& paramBoxDesc, std::set<const RenderCore::Assets::PredefinedCBLayout*>& cbLayouts,
            const ::Assets::DirectorySearchRules* searchRules)
        {
            std::vector<std::pair<MaterialGuid, SubMatResources>> materialResources;

            auto& cmdStream = scaffold.CommandStream();
            auto& meshData = scaffold.ImmutableData();

            auto geoCallCount = cmdStream.GetGeoCallCount();
            auto skinCallCount = cmdStream.GetSkinCallCount();
            for (unsigned gi=0; gi<geoCallCount + skinCallCount; ++gi) {
                auto& geoInst = (gi < geoCallCount) ? cmdStream.GetGeoCall(gi) : cmdStream.GetSkinCall(gi - geoCallCount);
                if (geoInst._levelOfDetail != levelOfDetail) { continue; }

                    //  Lookup the mesh geometry and material information from their respective inputs.
                auto& geo = (gi < geoCallCount) ? meshData._geos[geoInst._geoId] : (RenderCore::Assets::RawGeometry&)meshData._boundSkinnedControllers[geoInst._geoId];
                for (unsigned di=0; di<unsigned(geo._drawCalls.size()); ++di) {
                    auto scaffoldMatIndex = ScaffoldMaterialIndex(scaffold, gi, di);
                    auto existing = LowerBound(materialResources, scaffoldMatIndex);
                    if (existing == materialResources.cend() || existing->first != scaffoldMatIndex) {
                        materialResources.insert(existing, std::make_pair(scaffoldMatIndex, SubMatResources()));
                    }
                }
            }

                // fill in the details for all of the material references we found
				// Note that we no longer store a shader name in the material itself,
				// it's usually provided by some external source
            const ::Assets::ResChar* shaderName = ILLUM_LEGACY_TECH;
			for (auto i=materialResources.begin(); i!=materialResources.end(); ++i) {
                i->second._shaderName = sharedStateSet.InsertTechniqueConfig(shaderName);
                i->second._texturesIndex = (unsigned)std::distance(materialResources.begin(), i);
            }

                // build material constants
            for (auto i=materialResources.begin(); i!=materialResources.end(); ++i) {
                auto* matData = matScaffold.GetMaterial(i->first);
                auto* cbLayout = sharedStateSet.GetCBLayout(i->second._shaderName);
				auto lang = RenderCore::Techniques::GetDefaultShaderLanguage();
				if (cbLayout && cbLayout->GetSize(lang)) {
					auto cbData = matData ? cbLayout->BuildCBDataAsVector(matData->_constants, lang) : std::vector<uint8>(cbLayout->GetSize(lang), uint8(0));
					cbLayouts.insert(cbLayout);

					i->second._constantBuffer = 
						(unsigned)InsertOrCombine(
							prescientMaterialConstantBuffers, 
							std::move(cbData));
				} else {
					i->second._constantBuffer = ~0u;
				}
            }

                // configure the texture bind points array & material parameters box
            for (auto i=materialResources.begin(); i!=materialResources.end(); ++i) {
                ParameterBox materialParamBox;
                RenderCore::Assets::RenderStateSet stateSet;

                    //  we need to create a list of all of the texture bind points that are referenced
                    //  by all of the materials used here. They will end up in sorted order
                auto* matData = matScaffold.GetMaterial(i->first);
                if (matData) {
                    const auto& materialScaffoldData = i->first;
                
                    materialParamBox = matData->_matParams;
                    stateSet = matData->_stateSet;
                
                    for (const auto& param:matData->_bindings) {
                        if (param.Type().GetSize() == 0) continue;

                        materialParamBox.SetParameter(
                            (const utf8*)(StringMeld<64, utf8>() << "RES_HAS_" << param.Name()), 1);
                
                        auto bindNameHash = Hash64(param.Name().begin(), param.Name().end());
                        auto q = std::lower_bound(textureBindPoints.begin(), textureBindPoints.end(), bindNameHash);
                        if (q != textureBindPoints.end() && *q == bindNameHash) { continue; }
                        textureBindPoints.insert(q, bindNameHash);
                    }

                    auto boundNormalMapName = matData->_bindings.GetString<::Assets::ResChar>(DefaultNormalsTextureBindingHash);
                    if (!boundNormalMapName.empty()) {
                            //  We need to decide whether the normal map is "DXT" 
                            //  format or not. This information isn't in the material
                            //  itself; we actually need to look at the texture file
                            //  to see what format it is. Unfortunately that means
                            //  opening the texture file to read it's header. However
                            //  we can accelerate it a bit by caching the result
                        bool isDxtNormalMap = false;
                        if (searchRules) {
                            ::Assets::ResChar resolvedPath[MaxPath];
                            searchRules->ResolveFile(resolvedPath, dimof(resolvedPath), boundNormalMapName.c_str());
                            isDxtNormalMap = RenderCore::Techniques::DeferredShaderResource::IsDXTNormalMap(resolvedPath);
                        } else 
                            isDxtNormalMap = RenderCore::Techniques::DeferredShaderResource::IsDXTNormalMap(boundNormalMapName.c_str());
                        materialParamBox.SetParameter((const utf8*)"RES_HAS_NormalsTexture_DXT", isDxtNormalMap);
                    }
                }

                i->second._matParams = sharedStateSet.InsertParameterBox(materialParamBox);
                i->second._renderStateSet = sharedStateSet.InsertRenderStateSet(stateSet);

                if (stateSet._forwardBlendOp == BlendOp::NoBlending) {
                    i->second._delayStep = DelayStep::OpaqueRender;
                } else {
                    if (stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::BlendType) {
                        switch (RenderCore::Assets::RenderStateSet::BlendType(stateSet._blendType)) {
                        case RenderCore::Assets::RenderStateSet::BlendType::DeferredDecal: i->second._delayStep = DelayStep::OpaqueRender; break;
                        case RenderCore::Assets::RenderStateSet::BlendType::Ordered: i->second._delayStep = DelayStep::SortedBlending; break;
                        default: i->second._delayStep = DelayStep::PostDeferred; break;
                        }
                    } else {
                        i->second._delayStep = DelayStep::PostDeferred;
                    }
                }

                paramBoxDesc.Add(i->second._matParams, materialParamBox);
            }

            return materialResources;
        }

        std::vector<::Assets::PtrToFuturePtr<RenderCore::Techniques::DeferredShaderResource>> BuildBoundTextures(
            const RenderCore::Assets::ModelScaffold& scaffold, const RenderCore::Assets::MaterialScaffold& matScaffold,
            const ::Assets::DirectorySearchRules* searchRules,
            const std::vector<std::pair<MaterialGuid, SubMatResources>>& materialResources,
            const std::vector<uint64>& textureBindPoints, unsigned textureSetCount,
            std::vector<::Assets::rstring>& boundTextureNames)
        {
            auto texturesPerMaterial = textureBindPoints.size();

            std::vector<::Assets::PtrToFuturePtr<RenderCore::Techniques::DeferredShaderResource>> boundTextures;
            boundTextures.resize(textureSetCount * texturesPerMaterial);
            DEBUG_ONLY(boundTextureNames.resize(textureSetCount * texturesPerMaterial));

            for (auto mi=materialResources.begin(); mi!=materialResources.end(); ++mi) {
                unsigned textureSetIndex = mi->second._texturesIndex;
        
                auto* matData = matScaffold.GetMaterial(mi->first);
                if (!matData) { continue; }

                for (const auto& param:matData->_bindings) {
                    if (param.Type().GetSize() == 0) continue;

                    auto bindNameHash = Hash64(param.Name().begin(), param.Name().end());

                    auto i = std::find(textureBindPoints.cbegin(), textureBindPoints.cend(), bindNameHash);
                    assert(i!=textureBindPoints.cend() && *i == bindNameHash);
                    auto index = std::distance(textureBindPoints.cbegin(), i);

                    auto resourceName = matData->_bindings.GetString<::Assets::ResChar>(param.HashName());
                    if (resourceName.empty()) continue;
                
                    TRY {
                            // note --  Ideally we want to do this filename resolve in a background thread
                            //          however, it doesn't work well with our resources system. Because we're
                            //          expecting to create the DeferredShaderResource from a definitive file
                            //          name, something that can be matched against other (already loaded) resources.
                            //          So we need something different here... Something that can resolve a filename
                            //          in the background, and then return a shareable resource afterwards
                        auto dsti = textureSetIndex*texturesPerMaterial + index;
                        if (searchRules) {
                            ResChar resolvedPath[MaxPath];
                            searchRules->ResolveFile(resolvedPath, dimof(resolvedPath), resourceName.c_str());
                            boundTextures[dsti] = ::Assets::MakeAsset<RenderCore::Techniques::DeferredShaderResource>(resolvedPath);
                            DEBUG_ONLY(boundTextureNames[dsti] = resolvedPath);
                        } else {
                            boundTextures[dsti] = ::Assets::MakeAsset<RenderCore::Techniques::DeferredShaderResource>(resourceName.c_str());
                            DEBUG_ONLY(boundTextureNames[dsti] = resourceName);
                        }
                    } CATCH (const ::Assets::Exceptions::InvalidAsset&) {
                        Log(Warning) << "Warning -- shader resource (" << resourceName << ") couldn't be found" << std::endl;
                    } CATCH_END
                }
            }

            return std::move(boundTextures);
        }

        class BuffersUnderConstruction
        {
        public:
            unsigned _vbSize;
            unsigned _ibSize;

            std::vector<PendingGeoUpload>  _vbUploads;
            std::vector<PendingGeoUpload>  _ibUploads;

            unsigned AllocateIB(unsigned size, Format format)
            {
                unsigned allocation = _ibSize;

                    // we have to align the index buffer offset correctly
                unsigned indexStride = (format == Format::R32_UINT)?4:2;
                unsigned rem = _ibSize % indexStride;
                if (rem != 0) {
                    allocation += indexStride - rem;
                }

                _ibSize = allocation + size;
                return allocation;
            }

            unsigned AllocateVB(unsigned size)
            {
                unsigned result = _vbSize;
                _vbSize += size;
                return result;
            }

            BuffersUnderConstruction() : _vbSize(0), _ibSize(0) {}
        };

        static void FindSupplementGeo(
            std::vector<RenderCore::Assets::VertexData*>& result,
            IteratorRange<const RenderCore::Assets::ModelSupplementScaffold**> supplements,
            unsigned geoId)
        {
            result.clear();
            for (auto c=supplements.cbegin(); c!=supplements.cend(); ++c) {
                auto& immData = (*c)->ImmutableData();
                for (size_t g=0; g<immData._geoCount; ++g)
                    if (immData._geos[g]._geoId == geoId)
                        result.push_back(&immData._geos[g]._vb);
            }
        }

        static void ReadImmediately(
            std::vector<uint8>& nascentBuffer,
            ::Assets::IFileInterface& file, size_t largeBlocksOffset,
            IteratorRange<PendingGeoUpload*> uploads, unsigned supplementIndex = ~0u)
        {
            for (auto u=uploads.cbegin(); u!=uploads.cend(); ++u)
                if (u->_supplementIndex==supplementIndex)
                    LoadBlock(
                        file, &nascentBuffer[u->_bufferDestination], 
                        largeBlocksOffset + u->_sourceFileOffset, u->_size);
        }
    }

    ModelRenderer::ModelRenderer(
        const RenderCore::Assets::ModelScaffold& scaffold, const RenderCore::Assets::MaterialScaffold& matScaffold,
        Supplements supplements,
        SharedStateSet& sharedStateSet, 
        const ::Assets::DirectorySearchRules* searchRules, unsigned levelOfDetail)
    {
        using namespace ModelConstruction;

            // build the underlying objects required to render the given scaffold 
            //  (at the given level of detail)
        std::vector<uint64> textureBindPoints;
        std::vector<std::vector<uint8>> prescientMaterialConstantBuffers;
        std::set<const RenderCore::Assets::PredefinedCBLayout*> cbLayouts;
        ModelConstruction::ParamBoxDescriptions paramBoxDesc;
        auto materialResources = BuildMaterialResources(
            scaffold, matScaffold, sharedStateSet, levelOfDetail,
            textureBindPoints, prescientMaterialConstantBuffers,
            paramBoxDesc, cbLayouts, searchRules);

            // one "textureset" for each sub material (though, in theory, we could 
            // combine texture sets for materials that share the same textures
        unsigned textureSetCount = unsigned(materialResources.size());

        auto& cmdStream = scaffold.CommandStream();
        auto& meshData = scaffold.ImmutableData();
        std::vector<RenderCore::Assets::VertexData*> supplementGeo;

            //  First we need to bind each draw call to a material in our
            //  material scaffold. Then we need to find the superset of all bound textures
            //      This superset will be used to initialize all of the technique 
            //      input interfaces
            //  The final resolved texture is defined by the "meshCall" object and by the 
            //  "drawCall" objects. MeshCall gives us the material id, and the draw call 
            //  gives us the sub material id.

            //  Note that we can have cases where the same mesh is referenced multiple times by 
            //  a single "geo call". In these cases, we want the mesh data to be stored once
            //  in the vertex buffer / index buffer but for there to be multiple sets of "draw calls"
            //  So, we have to separate the mesh processing from the draw call processing here
        
        BuffersUnderConstruction workingBuffers;
        auto geoCallCount = cmdStream.GetGeoCallCount();
        auto skinCallCount = cmdStream.GetSkinCallCount();
        auto drawCallCount = 0;
        for (unsigned c=0; c<geoCallCount + skinCallCount; ++c) { drawCallCount += GetDrawCallCount(scaffold, c); }

            //  We should calculate the size we'll need for nascentIB & nascentVB first, 
            //  so we don't have to do any reallocations

            ////////////////////////////////////////////////////////////////////////
                //          u n s k i n n e d   g e o                           //
        std::vector<Pimpl::Mesh> meshes;
        std::vector<Pimpl::MeshAndDrawCall> drawCalls;
        std::vector<Pimpl::DrawCallResources> drawCallRes;
        drawCalls.reserve(drawCallCount);
        drawCallRes.reserve(drawCallCount);

        for (unsigned gi=0; gi<geoCallCount; ++gi) {
            auto& geoInst = cmdStream.GetGeoCall(gi);
            if (geoInst._levelOfDetail != levelOfDetail) { continue; }

                //  Check to see if this mesh has at least one valid draw call. If there
                //  is none, we can skip it completely
            assert(geoInst._geoId < meshData._geoCount);
            auto& geo = meshData._geos[geoInst._geoId];
            if (!AtLeastOneValidDrawCall(geo, scaffold, gi, materialResources)) { continue; }

                // if we encounter the same mesh multiple times, we don't need to store it every time
            auto mesh = FindIf(meshes, [=](const Pimpl::Mesh& mesh) { return mesh._id == geoInst._geoId; });
            if (mesh == meshes.end()) {
                FindSupplementGeo(supplementGeo, supplements, geoInst._geoId);
                meshes.push_back(
                    Pimpl::BuildMesh(
                        geoInst, geo,
                        MakeIteratorRange(supplementGeo), 
                        workingBuffers, sharedStateSet,
                        AsPointer(textureBindPoints.cbegin()), (unsigned)textureBindPoints.size(),
                        paramBoxDesc));
                mesh = meshes.end()-1;
            }

                // setup the "Draw call" objects next
            for (unsigned di=0; di<unsigned(geo._drawCalls.size()); ++di) {
                auto& d = geo._drawCalls[di];
                if (!d._indexCount) { continue; }

                auto scaffoldMatIndex = ScaffoldMaterialIndex(scaffold, gi, di);
                auto matResI = LowerBound(materialResources, scaffoldMatIndex);
                if (matResI == materialResources.cend() || matResI->first != scaffoldMatIndex) {
                    continue;   // missing shader name means a "no-draw" shader
                }
                const auto& matRes = matResI->second;

                    //  "Draw call resources" are used when performing this draw call.
                    //  They help select the right shader, and are also required for
                    //  setting the graphics state (bound textures and shader constants, etc)
                    //  We can initialise them now using some information from the geometry
                    //  object and some information from the material.
                Pimpl::DrawCallResources res(
                    matRes._shaderName,
                    mesh->_geoParamBox, matRes._matParams, 
                    matRes._texturesIndex, matRes._constantBuffer,
                    matRes._renderStateSet, matRes._delayStep, scaffoldMatIndex);
                drawCallRes.push_back(res);
                drawCalls.push_back(std::make_pair(gi, d));
            }
        }

            ////////////////////////////////////////////////////////////////////////
                //          s k i n n e d   g e o                           //
        std::vector<PimplWithSkinning::SkinnedMesh> skinnedMeshes;
        std::vector<Pimpl::MeshAndDrawCall> skinnedDrawCalls;
        std::vector<PimplWithSkinning::SkinnedMeshAnimBinding> skinnedBindings;

        for (unsigned gi=0; gi<skinCallCount; ++gi) {
            auto& geoInst = cmdStream.GetSkinCall(gi);
            if (geoInst._levelOfDetail != levelOfDetail) { continue; }

                //  Check to see if this mesh has at least one valid draw call. If there
                //  is none, we can skip it completely
            assert(geoInst._geoId < meshData._boundSkinnedControllerCount);
            auto& geo = meshData._boundSkinnedControllers[geoInst._geoId];
            if (!AtLeastOneValidDrawCall(geo, scaffold, unsigned(geoCallCount + gi), materialResources)) { continue; }

                // if we encounter the same mesh multiple times, we don't need to store it every time
            auto mesh = FindIf(skinnedMeshes, [=](const PimplWithSkinning::SkinnedMesh& mesh) { return mesh._id == geoInst._geoId; });
            if (mesh == skinnedMeshes.end()) {
                FindSupplementGeo(supplementGeo, supplements, unsigned(meshData._geoCount) + geoInst._geoId);
                skinnedMeshes.push_back(
                    PimplWithSkinning::BuildMesh(geoInst, geo, MakeIteratorRange(supplementGeo), workingBuffers, sharedStateSet, 
                        AsPointer(textureBindPoints.cbegin()), (unsigned)textureBindPoints.size(),
                        paramBoxDesc));
                skinnedBindings.push_back(
                    PimplWithSkinning::BuildAnimBinding(
                        geoInst, geo, sharedStateSet, 
                        AsPointer(textureBindPoints.cbegin()), (unsigned)textureBindPoints.size()));

                mesh = skinnedMeshes.end()-1;
            }

            for (unsigned di=0; di<unsigned(geo._drawCalls.size()); ++di) {
                auto& d = geo._drawCalls[di];
                if (!d._indexCount) { continue; }
                
                auto scaffoldMatIndex = ScaffoldMaterialIndex(scaffold, unsigned(geoCallCount + gi), di);
                auto subMatResI = LowerBound(materialResources, scaffoldMatIndex);
                if (subMatResI == materialResources.cend() || subMatResI->first != scaffoldMatIndex) {
                    continue;   // missing shader name means a "no-draw" shader
                }

                auto& matRes = subMatResI->second;
                Pimpl::DrawCallResources res(
                    matRes._shaderName,
                    mesh->_geoParamBox, matRes._matParams, 
                    matRes._texturesIndex, matRes._constantBuffer,
                    matRes._renderStateSet, matRes._delayStep, scaffoldMatIndex);

                drawCallRes.push_back(res);
                skinnedDrawCalls.push_back(std::make_pair(gi, d));
            }
        }

            ////////////////////////////////////////////////////////////////////////
                //
                //  We have to load the "large blocks" from the file here
                //      -- todo -- this part can be pushed into the background
                //          using the buffer uploads system
                //
        std::vector<uint8> nascentVB, nascentIB;
        nascentVB.resize(workingBuffers._vbSize);
        nascentIB.resize(workingBuffers._ibSize);

        {
			auto file = scaffold.OpenLargeBlocks();
			auto largeBlocksOffset = file->TellP();
            ReadImmediately(nascentIB, *file, largeBlocksOffset, MakeIteratorRange(workingBuffers._ibUploads));
            ReadImmediately(nascentVB, *file, largeBlocksOffset, MakeIteratorRange(workingBuffers._vbUploads));
        }

        for (unsigned s=0; s<supplements.size(); ++s) {
			auto file = supplements[s]->OpenLargeBlocks();
			auto largeBlocksOffset = file->TellP();
            ReadImmediately(nascentVB, *file, largeBlocksOffset, MakeIteratorRange(workingBuffers._vbUploads), s);
        }

            ////////////////////////////////////////////////////////////////////////
                //  now that we have a list of all of the sub materials used, and we know how large the resource 
                //  interface is, we build an array of deferred shader resources for shader inputs.
        std::vector<::Assets::rstring> boundTextureNames;
        auto boundTextures = BuildBoundTextures(
            scaffold, matScaffold, searchRules,
            materialResources, textureBindPoints, textureSetCount,
            boundTextureNames);

            ////////////////////////////////////////////////////////////////////////

		auto& objFactory = Metal::GetObjectFactory();

        std::vector<Metal::ConstantBuffer> finalConstantBuffers;
        for (auto cb=prescientMaterialConstantBuffers.cbegin(); cb!=prescientMaterialConstantBuffers.end(); ++cb) {
            assert(cb->size());
            finalConstantBuffers.emplace_back(
				Metal::MakeConstantBuffer(objFactory, MakeIteratorRange(*cb)));
        }

		auto vb = Metal::CreateResource(
			objFactory,
			CreateDesc(
				BindFlag::VertexBuffer, 0, GPUAccess::Read,
				LinearBufferDesc::Create((unsigned)nascentVB.size()),
				"ModelVB"),
			[&nascentVB](SubResourceId subr) {
				assert(subr._arrayLayer == 0 && subr._mip == 0);
				return SubResourceInitData { MakeIteratorRange(nascentVB) };
			});

		auto ib = Metal::CreateResource(
			objFactory,
			CreateDesc(
				BindFlag::IndexBuffer, 0, GPUAccess::Read,
				LinearBufferDesc::Create((unsigned)nascentIB.size()),
				"ModelIB"),
			[&nascentIB](SubResourceId subr) {
				assert(subr._arrayLayer == 0 && subr._mip == 0);
				return SubResourceInitData { MakeIteratorRange(nascentIB) };
			});

            ////////////////////////////////////////////////////////////////////////

        _validationCallback = ::Assets::GetDepValSys().Make();
        _validationCallback.RegisterDependency(scaffold.GetDependencyValidation());
        _validationCallback.RegisterDependency(matScaffold.GetDependencyValidation());
        for(auto i:cbLayouts) _validationCallback.RegisterDependency(i->GetDependencyValidation());
        // for (const auto& t:boundTextures) if (t) _validationCallback.RegisterDependency(t->GetDependencyValidation());       // rebuild the entire renderer if any texture changes

        auto pimpl = std::make_unique<PimplWithSkinning>();

        pimpl->_vertexBuffer = std::move(vb);
        pimpl->_indexBuffer = std::move(ib);
        pimpl->_meshes = std::move(meshes);
        pimpl->_skinnedMeshes = std::move(skinnedMeshes);
        pimpl->_skinnedBindings = std::move(skinnedBindings);

        pimpl->_drawCalls = std::move(drawCalls);
        pimpl->_drawCallRes = std::move(drawCallRes);
        pimpl->_skinnedDrawCalls = std::move(skinnedDrawCalls);

        pimpl->_boundTextures = std::move(boundTextures);
        pimpl->_constantBuffers = std::move(finalConstantBuffers);
        pimpl->_texturesPerMaterial = textureBindPoints.size();

        pimpl->_scaffold = &scaffold;
        pimpl->_levelOfDetail = levelOfDetail;
        
        #if defined(_DEBUG)
            pimpl->_vbSize = (unsigned)nascentVB.size();
            pimpl->_ibSize = (unsigned)nascentIB.size();
            pimpl->_boundTextureNames = std::move(boundTextureNames);
            pimpl->_paramBoxDesc = std::move(paramBoxDesc._descriptions);
        #endif
        _pimpl = std::move(pimpl);

        DEBUG_ONLY(LogReport());
    }

    ModelRenderer::~ModelRenderer()
    {}

    

    class ModelRenderingBox
    {
    public:
        class Desc {};

        Metal::ConstantBuffer _localTransformBuffer;
        ModelRenderingBox(const Desc&)
        {
            _localTransformBuffer = 
				Metal::MakeConstantBuffer(
					Metal::GetObjectFactory(), 
					sizeof(Techniques::LocalTransformConstants));
        }
        ~ModelRenderingBox() {}
    };

    SharedStateSet::BoundVariation ModelRenderer::Pimpl::BeginVariation(
        const ModelRendererContext& context,
        const SharedStateSet&   sharedStateSet,
        unsigned                drawCallIndex,
        SharedTechniqueInterface      techniqueInterface) const
    {
        const auto& res = _drawCallRes[drawCallIndex];
        sharedStateSet.BeginRenderState(context, res._renderStateSet);
        return sharedStateSet.BeginVariation(
            context, res._shaderName, techniqueInterface, res._geoParamBox, res._materialParamBox);
    }

    auto ModelRenderer::Pimpl::BeginGeoCall(
        const ModelRendererContext& context,
        Metal::ConstantBuffer&      localTransformBuffer,
        const MeshToModel&          transforms,
        const Float4x4&             modelToWorld,
        unsigned                    geoCallIndex) const -> SharedTechniqueInterface
    {
        auto& cmdStream = _scaffold->CommandStream();
        auto& geoCall = cmdStream.GetGeoCall(geoCallIndex);

        if (transforms.IsGood()) {
            auto meshToWorld = Combine(transforms.GetMeshToModel(geoCall._transformMarker), modelToWorld);
            auto trans = Techniques::MakeLocalTransform(meshToWorld, ExtractTranslation(context._parserContext->GetProjectionDesc()._cameraToWorld));
            localTransformBuffer.Update(*context._context, &trans, sizeof(trans));
        }

            // todo -- should be possible to avoid this search
        auto mesh = FindIf(_meshes, [=](const Pimpl::Mesh& mesh) { return mesh._id == geoCall._geoId; });
        assert(mesh != _meshes.end());

        auto& devContext = *context._context;
        devContext.Bind(*checked_cast<Metal::Resource*>(_indexBuffer.get()), mesh->_indexFormat, mesh->_ibOffset);

		return mesh->_techniqueInterface;
	}

    auto ModelRenderer::PimplWithSkinning::BeginSkinCall(
        const ModelRendererContext& context,
        Metal::ConstantBuffer&      localTransformBuffer,
        const MeshToModel&          transforms,
        const Float4x4&             modelToWorld,
        unsigned                    geoCallIndex,
        PreparedAnimation*          preparedAnimation) const -> SharedTechniqueInterface
    {
        auto& cmdStream = _scaffold->CommandStream();
        auto& geoCall = cmdStream.GetSkinCall(geoCallIndex);

        if (transforms.IsGood()) {
            auto meshToWorld = Combine(transforms.GetMeshToModel(geoCall._transformMarker), modelToWorld);
            auto trans = Techniques::MakeLocalTransform(meshToWorld, ExtractTranslation(context._parserContext->GetProjectionDesc()._cameraToWorld));
            localTransformBuffer.Update(*context._context, &trans, sizeof(trans));
        }

        auto cm = FindIf(_skinnedMeshes, [=](const PimplWithSkinning::SkinnedMesh& mesh) { return mesh._id == geoCall._geoId; });
        assert(cm != _skinnedMeshes.end());
        auto meshIndex = std::distance(_skinnedMeshes.cbegin(), cm);

        auto result = cm->_skinnedTechniqueInterface;

        auto& devContext = *context._context;
		devContext.Bind(*checked_cast<Metal::Resource*>(_indexBuffer.get()), cm->_indexFormat, cm->_ibOffset);

		#if GFXAPI_TARGET == GFXAPI_DX11    // platformtemp
			// unimplemented -- bind with BoundInputLayout path
			//		(ie, using ApplyBoundInputLayout)
			assert(0);
			/*
			auto animGeo = SkinnedMesh::VertexStreams::AnimatedGeo;
			UINT strides[] = { cm->_extraVbStride[animGeo], cm->_vertexStrides[0], cm->_vertexStrides[1], cm->_vertexStrides[2] };
			UINT offsets[] = { cm->_extraVbOffset[animGeo], cm->_vbOffsets[0], cm->_vbOffsets[1], cm->_vbOffsets[2] };
			ID3D::Buffer* underlyingVBs[] = { _vertexBuffer.GetUnderlying(), _vertexBuffer.GetUnderlying(), _vertexBuffer.GetUnderlying(), _vertexBuffer.GetUnderlying() };
			static_assert(dimof(underlyingVBs) == (MaxVertexStreams+1), "underlyingVBs doesn't match MaxVertexStreams");

				//  If we have a prepared animation, we have to replace the bindings
				//  with the data from there.
			if (preparedAnimation) {
				underlyingVBs[0] = preparedAnimation->_skinningBuffer.GetUnderlying();
				strides[0] = _skinnedBindings[meshIndex]._vertexStride;
				offsets[0] = preparedAnimation->_vbOffsets[meshIndex];
				result = _skinnedBindings[meshIndex]._techniqueInterface;
			}

			context._context->GetUnderlying()->IASetVertexBuffers(0, 2, underlyingVBs, strides, offsets);
			*/
		#endif

        return result;
    }

    void ModelRenderer::Pimpl::ApplyBoundUnforms(
        const ModelRendererContext&     context,
        Metal::BoundUniforms&           boundUniforms,
        unsigned                        resourcesIndex,
        unsigned                        constantsIndex,
		ConstantBufferView				cbvs[2])
    {
        const Metal::ShaderResourceView* srvs[16];
        assert(_texturesPerMaterial <= dimof(srvs));
        for (unsigned c=0; c<_texturesPerMaterial; c++) {
            auto t = _boundTextures[resourcesIndex * _texturesPerMaterial + c];
			auto a = t ? t->TryActualize() : nullptr;
            srvs[c] = a ? (&a->GetShaderResource()) : nullptr;
        }

		assert(_constantBuffers[constantsIndex].IsGood());
		ConstantBufferView matCbv[] = { &_constantBuffers[constantsIndex] };

		boundUniforms.Apply(*context._context, 0, context._parserContext->GetGlobalUniformsStream());
		boundUniforms.Apply(*context._context, 1, 
			UniformsStream {
				MakeIteratorRange(matCbv),
				UniformsStream::MakeResources(MakeIteratorRange(srvs, &srvs[_texturesPerMaterial]))
			});
		boundUniforms.Apply(*context._context, 2, 
			UniformsStream {
				MakeIteratorRange(cbvs, &cbvs[1])
			});
    }

	void ModelRenderer::Pimpl::ApplyBoundInputLayout(
        const ModelRendererContext& context,
		Metal::BoundInputLayout&	boundInputLayout,
        unsigned                    geoCallIndex) const
	{
		auto& cmdStream = _scaffold->CommandStream();
        auto& geoCall = cmdStream.GetGeoCall(geoCallIndex);

		    // todo -- should be possible to avoid this search
        auto mesh = FindIf(_meshes, [=](const Pimpl::Mesh& mesh) { return mesh._id == geoCall._geoId; });
        assert(mesh != _meshes.end());

        const VertexBufferView vbs[MaxVertexStreams] = { 
			{ _vertexBuffer, mesh->_vbOffsets[0] }, 
			{ _vertexBuffer, mesh->_vbOffsets[1] },
			{ _vertexBuffer, mesh->_vbOffsets[2] }
		};
        static_assert(dimof(vbs) == MaxVertexStreams, "Vertex buffer array size doesn't match vertex streams");
        assert(mesh->_vertexStreamCount <= MaxVertexStreams);
		boundInputLayout.Apply(*context._context, MakeIteratorRange(vbs, &vbs[mesh->_vertexStreamCount]));
    }

    auto ModelRenderer::Pimpl::BuildMesh(
        const RenderCore::Assets::ModelCommandStream::GeoCall& geoInst,
        const RenderCore::Assets::RawGeometry& geo,
        IteratorRange<RenderCore::Assets::VertexData**> supplements,
        ModelConstruction::BuffersUnderConstruction& workingBuffers,
        SharedStateSet& sharedStateSet,
        const uint64 textureBindPoints[], unsigned textureBindPointsCnt,
        ModelConstruction::ParamBoxDescriptions& paramBoxDesc,
        bool normalFromSkinning) -> Mesh
    {
        Mesh result;
        result._id = geoInst._geoId;

            // Source file locators & vb/ib allocations
        result._indexFormat = geo._ib._format;
        result._ibOffset = workingBuffers.AllocateIB(
            geo._ib._size, result._indexFormat);
        workingBuffers._ibUploads.push_back(
            PendingGeoUpload { geo._ib._offset, geo._ib._size, ~0u, result._ibOffset });

        result._vbOffsets[0] = workingBuffers.AllocateVB(geo._vb._size);
        result._vertexStrides[0] = geo._vb._ia._vertexStride;
        result._vertexStreamCount = 1;
        workingBuffers._vbUploads.push_back(
            PendingGeoUpload { geo._vb._offset, geo._vb._size, ~0u, result._vbOffsets[0] });

        #if defined(_DEBUG)
            result._vbSize = geo._vb._size;
            result._ibSize = geo._ib._size;
        #endif

            // Also set up vertex data from the supplements
            // (supplemental vertex data gets uploaded into the same vertex buffer)
        unsigned s2=0;
        for (; s2<supplements.size(); ++s2) {
            const auto& vb = *supplements[s2];
            result._vbOffsets[1+s2] = workingBuffers.AllocateVB(vb._size);
            result._vertexStrides[1+s2] = vb._ia._vertexStride;
            ++result._vertexStreamCount;
            workingBuffers._vbUploads.push_back(
                PendingGeoUpload { vb._offset, vb._size, s2, result._vbOffsets[1+s2] });
        }
        for (; s2<MaxVertexStreams-1; ++s2) {
            result._vbOffsets[1+s2] = 0;
            result._vertexStrides[1+s2] = 0;
        }

            // Build vertex input layout desc
        InputElementDesc inputDesc[12];
        unsigned vertexElementCount = BuildLowLevelInputAssembly(
            MakeIteratorRange(inputDesc), MakeIteratorRange(geo._vb._ia._elements));
        for (unsigned s=0; s!=supplements.size(); ++s)
            vertexElementCount += BuildLowLevelInputAssembly(
                MakeIteratorRange(&inputDesc[vertexElementCount], &inputDesc[dimof(inputDesc)]),
                MakeIteratorRange(supplements[s]->_ia._elements), 1+s);

            // Setup the geo param box and the technique interface
            // from the vertex input layout
        result._geoParamBox = ModelConstruction::BuildGeoParamBox(
            MakeIteratorRange(inputDesc, &inputDesc[vertexElementCount]),
            sharedStateSet, paramBoxDesc, normalFromSkinning);

        result._techniqueInterface = sharedStateSet.InsertTechniqueInterface(
            inputDesc, vertexElementCount, textureBindPoints, textureBindPointsCnt);

        return result;
    }

    auto ModelRenderer::PimplWithSkinning::BuildMesh(
        const RenderCore::Assets::ModelCommandStream::GeoCall& geoInst,
        const RenderCore::Assets::BoundSkinnedGeometry& geo,
        IteratorRange<RenderCore::Assets::VertexData**> supplements,
        ModelConstruction::BuffersUnderConstruction& workingBuffers,
        SharedStateSet& sharedStateSet,
        const uint64 textureBindPoints[], unsigned textureBindPointsCnt,
        ModelConstruction::ParamBoxDescriptions& paramBoxDesc) -> SkinnedMesh
    {
            // Build the mesh, starting with the same basic behaviour as 
            //  unskinned meshes.
            //  (there a sort-of "slice" here... It's a bit of a hack)

        bool skinnedNormal = ModelConstruction::HasElement(geo._animatedVertexElements._ia, "NORMAL");
        PimplWithSkinning::SkinnedMesh result;
        (Mesh&)result = Pimpl::BuildMesh(
            geoInst, (const RenderCore::Assets::RawGeometry&)geo, supplements, workingBuffers, sharedStateSet,
            textureBindPoints, textureBindPointsCnt,
            paramBoxDesc, skinnedNormal);

        auto animGeo = PimplWithSkinning::SkinnedMesh::VertexStreams::AnimatedGeo;
        auto skelBind = PimplWithSkinning::SkinnedMesh::VertexStreams::SkeletonBinding;
        const RenderCore::Assets::VertexData* vd[2];
        vd[animGeo] = &geo._animatedVertexElements;
        vd[skelBind] = &geo._skeletonBinding;

        for (unsigned c=0; c<2; ++c) {
            result._extraVbOffset[c] = workingBuffers.AllocateVB(vd[c]->_size);
            result._extraVbStride[c] = vd[c]->_ia._vertexStride;
            result._vertexCount[c] = vd[c]->_size / vd[c]->_ia._vertexStride;

            workingBuffers._vbUploads.push_back(
                PendingGeoUpload { vd[c]->_offset, vd[c]->_size, ~0u, result._extraVbOffset[c] });
        }

        ////////////////////////////////////////////////////////////////////////////////
            //  Build the input assembly we will use while rendering. This should 
            //  contain the unskinned the vertex elements, and also the skinned vertex
            //  elements.
            //
            //  There are 3 possible paths for the skinned vertex elements:
            //      1) we do the skinning in the vertex shader, as we encounter them.
            //          (In this case, we also need the skinning parameter vertex elements)
            //      2) we do the skinning in a geometry shader prepare step
            //      3) we do no skinning at all
            //
            //  In path 2, the geometry shader prepare step may change the format of the
            //  vertex elements. This typically occurs when using 16 bit floats (or maybe 
            //  even fixed point formats). That means we need another technique interface
            //  for the prepared animation case!
        {
            InputElementDesc inputDescForRender[12];
            unsigned eleCount = 
                BuildLowLevelInputAssembly(
                    MakeIteratorRange(inputDescForRender),
                    MakeIteratorRange(geo._animatedVertexElements._ia._elements));

                // (add the unanimated part)
            eleCount += 
                BuildLowLevelInputAssembly(
                    MakeIteratorRange(&inputDescForRender[eleCount], &inputDescForRender[dimof(inputDescForRender)]),
                    MakeIteratorRange(geo._vb._ia._elements), 1);

            result._skinnedTechniqueInterface = sharedStateSet.InsertTechniqueInterface(
                inputDescForRender, eleCount, 
                textureBindPoints, textureBindPointsCnt);
        }

        return result;
    }

    ModelRenderer::Pimpl::DrawCallResources::DrawCallResources()
    {
        _shaderName = SharedTechniqueConfig::Invalid;
        _geoParamBox = _materialParamBox = SharedParameterBox::Invalid;
        _textureSet = _constantBuffer = ~0u;
        _renderStateSet = SharedRenderStateSet::Invalid;
        _delayStep = DelayStep::OpaqueRender;
        _materialBindingGuid = 0;
    }

    ModelRenderer::Pimpl::DrawCallResources::DrawCallResources(
        SharedTechniqueConfig shaderName,
        SharedParameterBox geoParamBox, SharedParameterBox matParamBox,
        unsigned textureSet, unsigned constantBuffer,
        SharedRenderStateSet renderStateSet, DelayStep delayStep, MaterialGuid materialBindingGuid)
    {
        _shaderName = shaderName;
        _geoParamBox = geoParamBox;
        _materialParamBox = matParamBox;
        _textureSet = textureSet;
        _constantBuffer = constantBuffer;
        _renderStateSet = renderStateSet;
        _delayStep = delayStep;
        _materialBindingGuid = materialBindingGuid;
    }

    void    ModelRenderer::Render(
            const ModelRendererContext& context,
            const SharedStateSet&   sharedStateSet,
            const Float4x4&     modelToWorld,
            const MeshToModel&  transforms,
            PreparedAnimation*  preparedAnimation) const
    {
        auto& box = ConsoleRig::FindCachedBox<ModelRenderingBox>(ModelRenderingBox::Desc());
		ConstantBufferView pkts[] = { &box._localTransformBuffer, {} };

        unsigned currTextureSet = ~unsigned(0x0), currCB = ~unsigned(0x0), currGeoCall = ~unsigned(0x0);
        SharedTechniqueInterface currTechniqueInterface = SharedTechniqueInterface::Invalid;
        Metal::BoundUniforms* currUniforms = nullptr;
		Metal::BoundInputLayout* currInputLayout = nullptr;
        auto& devContext = *context._context;
        auto& scaffold = *_pimpl->_scaffold;
        auto& cmdStream = scaffold.CommandStream();

        if (!transforms.IsGood()) {
            Techniques::LocalTransformConstants trans;
            trans = Techniques::MakeLocalTransform(modelToWorld, ExtractTranslation(context._parserContext->GetProjectionDesc()._cameraToWorld));
            box._localTransformBuffer.Update(*context._context, &trans, sizeof(trans));
        }

        if (Tweakable("SkinnedAsStatic", false)) { preparedAnimation = nullptr; }

        CATCH_ASSETS_BEGIN

                // skinned and unskinned geometry are almost the same, except for
                // "BeginGeoCall" / "BeginSkinCall". Never the less, we need to split
                // them into separate loops

                //////////// Render un-skinned geometry ////////////

            // Metal::ConstantBuffer drawCallIndexBuffer(nullptr, sizeof(unsigned)*4);
            // devContext.BindGS(MakeResourceList(drawCallIndexBuffer));

            unsigned drawCallIndex = 0;
            for (auto md=_pimpl->_drawCalls.cbegin(); md!=_pimpl->_drawCalls.cend(); ++md, ++drawCallIndex) {

                if (md->first != currGeoCall) {
                    currTechniqueInterface = _pimpl->BeginGeoCall(
                        context, box._localTransformBuffer, transforms, modelToWorld, md->first);
                    currGeoCall = md->first;
					currInputLayout = nullptr;
                }

                auto boundVariation = _pimpl->BeginVariation(context, sharedStateSet, drawCallIndex, currTechniqueInterface);
				if (!boundVariation._inputLayout || !boundVariation._uniforms) continue;

                const auto& drawCallRes = _pimpl->_drawCallRes[drawCallIndex];
                if (    boundVariation._uniforms != currUniforms 
                    ||  drawCallRes._textureSet != currTextureSet 
                    ||  drawCallRes._constantBuffer != currCB) {

                    _pimpl->ApplyBoundUnforms(
                        context, *boundVariation._uniforms, drawCallRes._textureSet, drawCallRes._constantBuffer, pkts);

                    currTextureSet = drawCallRes._textureSet; currCB = drawCallRes._constantBuffer;
                    currUniforms = boundVariation._uniforms;
                }

				if (boundVariation._inputLayout != currInputLayout) {
					_pimpl->ApplyBoundInputLayout(context, *boundVariation._inputLayout, currGeoCall);
					currInputLayout = boundVariation._inputLayout;
				} 
            
                const auto& d = md->second;
                devContext.Bind(d._topology);

                    // -- this draw call index stuff is only required in some cases --
                    //      we need some way to customise the model rendering method for different purposes
                devContext.Bind(Techniques::CommonResources()._dssReadWriteWriteStencil, 1+drawCallIndex);  // write stencil buffer with draw index
                // unsigned drawCallIndexB[4] = { drawCallIndex, 0, 0, 0 };
                // drawCallIndexBuffer.Update(devContext, drawCallIndexB, sizeof(drawCallIndexB));
                    // -------------

                devContext.DrawIndexed(d._indexCount, d._firstIndex, d._firstVertex);
            }


                //////////// Render skinned geometry ////////////

            currGeoCall = ~unsigned(0x0);
            for (auto md=_pimpl->_skinnedDrawCalls.cbegin(); 
                md!=_pimpl->_skinnedDrawCalls.cend(); ++md, ++drawCallIndex) {

                if (md->first != currGeoCall) {
                    currTechniqueInterface = _pimpl->BeginSkinCall(
                        context, box._localTransformBuffer, transforms, modelToWorld, md->first, 
                        preparedAnimation);
                    currGeoCall = md->first;
                }

                auto boundVariation = _pimpl->BeginVariation(context, sharedStateSet, drawCallIndex, currTechniqueInterface);
                if (!boundVariation._inputLayout || !boundVariation._uniforms) continue;

                const auto& drawCallRes = _pimpl->_drawCallRes[drawCallIndex];
                if (    boundVariation._uniforms != currUniforms 
                    ||  drawCallRes._textureSet != currTextureSet 
                    ||  drawCallRes._constantBuffer != currCB) {

                    _pimpl->ApplyBoundUnforms(
                        context, *boundVariation._uniforms, drawCallRes._textureSet, drawCallRes._constantBuffer, pkts);

                    currTextureSet = drawCallRes._textureSet; currCB = drawCallRes._constantBuffer;
                    currUniforms = boundVariation._uniforms;
                }

				if (boundVariation._inputLayout != currInputLayout) {
					_pimpl->ApplyBoundInputLayout(context, *boundVariation._inputLayout, currGeoCall);
					currInputLayout = boundVariation._inputLayout;
				} 
            
                const auto& d = md->second;
                devContext.Bind(d._topology);  // do we really need to set the topology every time?

                    // -- this draw call index stuff is only required in some cases --
                    //      we need some way to customise the model rendering method for different purposes
                devContext.Bind(Techniques::CommonResources()._dssReadWriteWriteStencil, 1+drawCallIndex);  // write stencil buffer with draw index
                // unsigned drawCallIndexB[4] = { drawCallIndex, 0, 0, 0 };
                // drawCallIndexBuffer.Update(devContext, drawCallIndexB, sizeof(drawCallIndexB));
                    // -------------

                devContext.DrawIndexed(d._indexCount, d._firstIndex, d._firstVertex);
            }

        CATCH_ASSETS_END(*context._parserContext)
    }

////////////////////////////////////////////////////////////////////////////////

    static bool CompareDrawCall(const DelayedDrawCall& lhs, const DelayedDrawCall& rhs)
    {
        if (lhs._shaderVariationHash == rhs._shaderVariationHash) {
            if (lhs._renderer == rhs._renderer) {
                if (lhs._subMesh == rhs._subMesh) {
                    return lhs._drawCallIndex < rhs._drawCallIndex;
                }
                return lhs._subMesh < rhs._subMesh;
            }
            return lhs._renderer < rhs._renderer;
        }
        return lhs._shaderVariationHash < rhs._shaderVariationHash; 
    }

    void    ModelRenderer::Prepare(
        DelayedDrawCallSet& dest, 
        const SharedStateSet& sharedStateSet, 
        const Float4x4& modelToWorld,
        const MeshToModel& transforms) const
    {
        unsigned mainTransformIndex = ~unsigned(0x0);
        if (!transforms.IsGood()) {
            mainTransformIndex = (unsigned)dest._transforms.size();
            dest._transforms.push_back(modelToWorld);
        }

            //  After culling; submit all of the draw-calls in this mesh to a list to be sorted
            //  Note -- only unskinned geometry supported currently. In theory, we might be able
            //          to do the same with skinned geometry (at least, when not using the "prepare" step
        unsigned drawCallIndex = 0;
        for (auto md=_pimpl->_drawCalls.cbegin(); md!=_pimpl->_drawCalls.cend(); ++md, ++drawCallIndex) {
            const auto& drawCallRes = _pimpl->_drawCallRes[drawCallIndex];
            const auto& d = md->second;

            auto geoParamIndex = drawCallRes._geoParamBox;
            auto matParamIndex = drawCallRes._materialParamBox;
            auto shaderNameIndex = drawCallRes._shaderName;

            auto& cmdStream = _pimpl->_scaffold->CommandStream();
            auto& geoCall = cmdStream.GetGeoCall(md->first);
            auto mesh = FindIf(
                _pimpl->_meshes, [=](const Pimpl::Mesh& mesh) 
                { return mesh._id == geoCall._geoId; });
            assert(mesh != _pimpl->_meshes.end());

            auto step = unsigned(drawCallRes._delayStep);

            DelayedDrawCall entry;
            entry._drawCallIndex = drawCallIndex;
            entry._renderer = this;
            if (transforms.IsGood()) {
                auto trans = Combine(
                    transforms.GetMeshToModel(geoCall._transformMarker), 
                    modelToWorld);
                entry._meshToWorld = (unsigned)dest._transforms.size();
                dest._transforms.push_back(trans);
            } else {
                entry._meshToWorld = mainTransformIndex;
            }
            auto techniqueInterface = mesh->_techniqueInterface;
            entry._shaderVariationHash = techniqueInterface.Value() ^ (geoParamIndex.Value() << 12) ^ (matParamIndex.Value() << 15) ^ (shaderNameIndex.Value() << 24);  // simple hash of these indices. Note that collisions might be possible
            entry._indexCount = d._indexCount;
            entry._firstIndex = d._firstIndex;
            entry._firstVertex = d._firstVertex;
            entry._topology = d._topology;
            entry._subMesh = AsPointer(mesh);
            dest._entries[step].push_back(entry);
        }

            //  Also try to render skinned geometry... But we want to render this with skinning disabled 
            //  (this path is intended for rendering many static objects)
        for (auto md=_pimpl->_skinnedDrawCalls.cbegin(); md!=_pimpl->_skinnedDrawCalls.cend(); ++md, ++drawCallIndex) {
            const auto& drawCallRes = _pimpl->_drawCallRes[drawCallIndex];
            const auto& d = md->second;

            auto geoParamIndex = drawCallRes._geoParamBox;
            auto matParamIndex = drawCallRes._materialParamBox;
            auto shaderNameIndex = drawCallRes._shaderName;

            auto& cmdStream = _pimpl->_scaffold->CommandStream();
            auto& geoCall = cmdStream.GetSkinCall(md->first);
            auto mesh = FindIf(
                _pimpl->_skinnedMeshes, 
                [=](const Pimpl::Mesh& mesh) { return mesh._id == geoCall._geoId; });
            assert(mesh != _pimpl->_skinnedMeshes.end());

            auto step = unsigned(drawCallRes._delayStep);

            DelayedDrawCall entry;
            entry._drawCallIndex = drawCallIndex;
            entry._renderer = this;
            if (transforms.IsGood()) {
                auto trans = Combine(
                    transforms.GetMeshToModel(geoCall._transformMarker), 
                    modelToWorld);
                entry._meshToWorld = (unsigned)dest._transforms.size();
                dest._transforms.push_back(trans);
            } else {
                entry._meshToWorld = mainTransformIndex;
            }
            auto techniqueInterface = mesh->_skinnedTechniqueInterface;
            entry._shaderVariationHash = techniqueInterface.Value() ^ (geoParamIndex.Value() << 12) ^ (matParamIndex.Value() << 15) ^ (shaderNameIndex.Value() << 24);  // simple hash of these indices. Note that collisions might be possible
            entry._indexCount = d._indexCount;
            entry._firstIndex = d._firstIndex;
            entry._firstVertex = d._firstVertex;
            entry._topology = Topology(unsigned(d._topology) | 0x100u);
            entry._subMesh = AsPointer(mesh);
            dest._entries[step].push_back(entry);
        }
    }

    namespace WLTFlags { enum Enum { LocalToWorld = 1<<0, LocalSpaceView = 1<<1, MaterialGuid = 1<<2 }; }

    template<int Flags>
        void WriteLocalTransform(
            void* dest, 
            const ModelRendererContext& context, 
            const Float4x4& t, uint64 materialGuid)
    {
        auto* dst = (Techniques::LocalTransformConstants*)dest;

            //  Write some system constants that are supposed
            //  to be provided by the model renderer
        if (constant_expression<!!(Flags&WLTFlags::LocalToWorld)>::result()) {
            CopyTransform(dst->_localToWorld, t);
        }
        if (constant_expression<!!(Flags&WLTFlags::LocalSpaceView)>::result()) {
            auto worldSpaceView = ExtractTranslation(context._parserContext->GetProjectionDesc()._cameraToWorld);
            TransformPointByOrthonormalInverse(t, worldSpaceView);
            dst->_localSpaceView = worldSpaceView;
        }
    }

    void ModelRenderer::Sort(DelayedDrawCallSet& drawCalls)
    {
            // This sort could turn out to be expensive (particularly as CompareDrawCall
            // contains multiple comparisons).
            // Perhaps we would be better of doing the sorting on insertion (or using
            // a single sorting index value so that CompareDrawCall could be a single comparison)?
        for (unsigned c=0; c<(unsigned)DelayStep::Max; ++c) {
            auto& entries = drawCalls._entries[c];
            std::sort(entries.begin(), entries.end(), CompareDrawCall);
        }
    }

    template<bool HasCallback>
        void ModelRenderer::RenderPreparedInternal(
            const ModelRendererContext& context, const SharedStateSet& sharedStateSet,
            const DelayedDrawCallSet& drawCalls, DelayStep delayStep,
            const std::function<void(DrawCallEvent)>* callback)
    {
        if (drawCalls.GetRendererGUID() != typeid(ModelRenderer).hash_code())
            Throw(::Exceptions::BasicLabel("Delayed draw call set matched with wrong renderer type"));

        assert(unsigned(delayStep)<unsigned(DelayStep::Max));
        auto& entries = drawCalls._entries[(unsigned)delayStep];
        if (entries.empty()) return;

        Techniques::LocalTransformConstants localTrans;
        localTrans._localSpaceView = Float3(0.f, 0.f, 0.f);
        
        Metal::ConstantBuffer& localTransformBuffer = Techniques::CommonResources()._localTransformBuffer;
		ConstantBufferView pkts[] = { &localTransformBuffer, {} };

        const ModelRenderer::Pimpl::Mesh* currentMesh = nullptr;
		SharedStateSet::BoundVariation boundVariation = { nullptr, nullptr };
		Metal::BoundInputLayout* currInputLayout = nullptr;
        unsigned currentVariationHash = ~unsigned(0x0);
        unsigned currentTextureSet = ~unsigned(0x0);
        unsigned currentConstantBufferIndex = ~unsigned(0x0);
        SharedTechniqueInterface currentTechniqueInterface = SharedTechniqueInterface::Invalid;

        unsigned currentTopology = ~0u;

        for (auto d=entries.cbegin(); d!=entries.cend(); ++d) {
            auto& renderer = *(const ModelRenderer*)d->_renderer;
            const auto& drawCallRes = renderer._pimpl->_drawCallRes[d->_drawCallIndex];

            if (currentMesh != d->_subMesh) {
                const Pimpl::Mesh* mesh;
				if ((unsigned)d->_topology > 0xffu) {
					auto& sknmesh = *(const PimplWithSkinning::SkinnedMesh*)d->_subMesh;
					mesh = &sknmesh;
                    currentTechniqueInterface = sknmesh._skinnedTechniqueInterface;
				} else {
					mesh = (const Pimpl::Mesh*)d->_subMesh;
					currentTechniqueInterface = mesh->_techniqueInterface;
				}
                currentMesh = mesh;
                currentTextureSet = ~unsigned(0x0);
				currInputLayout = nullptr;
            }

                // Note --  At the moment, shader variation hash is the sorting priority.
                //          This reduces the shader changes to a minimum. It also means we
                //          do the work in "BeginVariation" to resolve the variation
                //          as rarely as possible. However, we could pre-resolve all of the
                //          variations that we're going to need and use another value as the
                //          sorting priority instead... That might reduce the low-level API 
                //          thrashing in some cases.
            if (currentVariationHash != d->_shaderVariationHash) {
                auto& mesh = *(const Pimpl::Mesh*)d->_subMesh;
                boundVariation = sharedStateSet.BeginVariation(
                    context, drawCallRes._shaderName, currentTechniqueInterface, drawCallRes._geoParamBox, 
                    drawCallRes._materialParamBox);
                currentVariationHash = d->_shaderVariationHash;
                currentTextureSet = ~unsigned(0x0);
            }

            if (!boundVariation._uniforms || !boundVariation._inputLayout) continue;

            sharedStateSet.BeginRenderState(context, drawCallRes._renderStateSet);

			#if GFXAPI_TARGET == GFXAPI_DX11
					// We have to do this transform update very frequently! isn't there a better way?
				{
					D3D11_MAPPED_SUBRESOURCE result;
					HRESULT hresult = context._context->GetUnderlying()->Map(
						localTransformBuffer.GetUnderlying(), 0, D3D11_MAP_WRITE_DISCARD, 0, &result);
					assert(SUCCEEDED(hresult) && result.pData); (void)hresult;
					WriteLocalTransform<WLTFlags::LocalToWorld|WLTFlags::MaterialGuid>(
						result.pData, context, drawCalls._transforms[d->_meshToWorld], drawCallRes._materialBindingGuid);
					context._context->GetUnderlying()->Unmap(localTransformBuffer.GetUnderlying(), 0);
				}
			#endif
            
            auto textureSet = drawCallRes._textureSet;
            auto constantBufferIndex = drawCallRes._constantBuffer;

                //  Sometimes the same render call may be rendered in several different locations. In these cases,
                //  we can reduce the API thrashing to the minimum by avoiding re-setting resources and constants
            if (textureSet != currentTextureSet || constantBufferIndex != currentConstantBufferIndex) {
                renderer._pimpl->ApplyBoundUnforms(
                    context, *boundVariation._uniforms,
                    textureSet, constantBufferIndex, pkts);

                currentTextureSet = textureSet;
                currentConstantBufferIndex = constantBufferIndex;
            }

			if (boundVariation._inputLayout != currInputLayout) {

				VertexBufferView vbv[] = {
					renderer._pimpl->_vertexBuffer, renderer._pimpl->_vertexBuffer, renderer._pimpl->_vertexBuffer, renderer._pimpl->_vertexBuffer
				};

				auto& mesh = *(const Pimpl::Mesh*)d->_subMesh;

				unsigned streamCount = 0;
				if ((unsigned)d->_topology > 0xffu) {
					auto& sknmesh = *(const PimplWithSkinning::SkinnedMesh*)d->_subMesh;
                    streamCount = 1+sknmesh._vertexStreamCount;
                    vbv[0]._offset = sknmesh._extraVbOffset[0];
					vbv[1]._offset = sknmesh._vbOffsets[0];
					vbv[2]._offset = sknmesh._vbOffsets[1];
					vbv[3]._offset = sknmesh._vbOffsets[2];
                } else {
					streamCount = mesh._vertexStreamCount;
					for (unsigned c=0; c<streamCount; ++c)
						vbv[c]._offset = mesh._vbOffsets[c];
                }

				assert(streamCount <= dimof(vbv));
				boundVariation._inputLayout->Apply(
					*context._context, 
					MakeIteratorRange(vbv, &vbv[streamCount]));

				context._context->Bind(Metal::AsResource(*renderer._pimpl->_indexBuffer), mesh._indexFormat, mesh._ibOffset);

				currInputLayout = boundVariation._inputLayout;
			}

            if (((unsigned)d->_topology & 0xff) != currentTopology) {
                currentTopology = (unsigned)d->_topology & 0xff;
                context._context->Bind((Topology)currentTopology);
            }
            if (constant_expression<HasCallback>::result()) {
                (*callback)(DrawCallEvent { d->_indexCount, d->_firstIndex, d->_firstVertex, d->_drawCallIndex });
            } else
                context._context->DrawIndexed(d->_indexCount, d->_firstIndex, d->_firstVertex);
        }
    }

    void ModelRenderer::RenderPrepared(
        const ModelRendererContext& context, const SharedStateSet& sharedStateSet,
        const DelayedDrawCallSet& drawCalls, DelayStep delayStep)
    {
        RenderPreparedInternal<false>(context, sharedStateSet, drawCalls, delayStep, nullptr);
    }

    void ModelRenderer::RenderPrepared(
        const ModelRendererContext& context, const SharedStateSet& sharedStateSet,
        const DelayedDrawCallSet& drawCalls, DelayStep delayStep,
        const std::function<void(DrawCallEvent)>& callback)
    {
        assert(callback);
        RenderPreparedInternal<true>(context, sharedStateSet, drawCalls, delayStep, &callback);
    }

        ////////////////////////////////////////////////////////////

    Float4x4 MeshToModel::GetMeshToModel(unsigned transformMarker) const
    {
            //  The "skeleton binding" tells us how to map from the matrices that
            //  are output from the transformation machine to the input matrices
            //  expected by the "transformMarker" index scheme
        if (_skeletonBinding) {
            assert(transformMarker < _skeletonBinding->GetModelJointCount());
            auto machineOutputIndex = _skeletonBinding->ModelJointToMachineOutput(transformMarker);
            if (machineOutputIndex == ~unsigned(0x0)) {
                    // no mapping... we just have to assume identity mesh-to-model
                return Identity<Float4x4>();
            } else if (machineOutputIndex >= _skeletonOutputCount) {
                assert(0);
                return Identity<Float4x4>();
            }
            return _skeletonOutput[machineOutputIndex];
        } else {            
            if (transformMarker >= _skeletonOutputCount) {
                assert(0);
                return Identity<Float4x4>();
            }
            return _skeletonOutput[transformMarker];
        }
    }

    MeshToModel::MeshToModel()
    {
        _skeletonOutput = nullptr;
        _skeletonOutputCount = 0;
        _skeletonBinding = nullptr;
    }

    MeshToModel::MeshToModel(
        const Float4x4 skeletonOutput[], unsigned skeletonOutputCount,
        const RenderCore::Assets::SkeletonBinding* binding)
    {
        _skeletonOutput = skeletonOutput;
        _skeletonOutputCount = skeletonOutputCount;
        _skeletonBinding = binding;
    }

    MeshToModel::MeshToModel(const RenderCore::Assets::ModelScaffold& model)
    {
            // just get the default transforms stored in the model scaffold
        _skeletonBinding = nullptr;
        if (model.ImmutableData()._defaultTransformCount) {
            _skeletonOutput = model.ImmutableData()._defaultTransforms;
            _skeletonOutputCount = (unsigned)model.ImmutableData()._defaultTransformCount;
        } else {
            _skeletonOutput = nullptr;
            _skeletonOutputCount = 0u;
        }
    }

    template<unsigned Size>
        static std::string Width(unsigned input)
    {
        static char buffer[Size+1];
        auto err = _itoa_s(std::min(input,unsigned(Size*10-1)), buffer, 10);
        if (!err) {
            auto length = XlStringSize(buffer);
            if (length < Size) {
                auto movement = Size-length;
                XlMoveMemory(&buffer[movement], buffer, length);
                XlSetMemory(buffer, ' ', movement);
                buffer[Size] = '\0';
            }
            return std::string(buffer);
        } else { return std::string("<<err>>"); }
    }

    std::vector<MaterialGuid> ModelRenderer::DrawCallToMaterialBinding() const
    {
        std::vector<MaterialGuid> result;
        result.reserve(_pimpl->_drawCallRes.size());
        for (auto i=_pimpl->_drawCallRes.begin(); i!=_pimpl->_drawCallRes.end(); ++i) {
            result.push_back(i->_materialBindingGuid);
        }
        return std::move(result);
    }

    MaterialGuid ModelRenderer::GetMaterialBindingForDrawCall(unsigned drawCallIndex) const
    {
        if (drawCallIndex < _pimpl->_drawCallRes.size())
            return _pimpl->_drawCallRes[drawCallIndex]._materialBindingGuid;
        return ~0ull;
    }

    ::Assets::AssetState ModelRenderer::GetAssetState() const
    {
            // If any of our dependencies is not ready, then we must return that state
            // Note that we can't check shaders because that depends on the global state
            // (eg the global technique state).
            // So even if we return "ready" from here, we should still throw pending/invalid
            // during rendering.
        bool gotPending = false;
        for (auto t=_pimpl->_boundTextures.cbegin(); t!=_pimpl->_boundTextures.cend(); ++t) {
            if (!*t) continue;
            auto tState = (*t)->GetAssetState();
            if (tState == ::Assets::AssetState::Invalid) return ::Assets::AssetState::Invalid;
            gotPending |= tState == ::Assets::AssetState::Pending;
        }
        return gotPending ? ::Assets::AssetState::Pending : ::Assets::AssetState::Ready;
    }

    ::Assets::AssetState ModelRenderer::TryResolve() const
    {
        bool gotPending = false;
        for (auto t=_pimpl->_boundTextures.cbegin(); t!=_pimpl->_boundTextures.cend(); ++t) {
            if (!*t) continue;
            auto tState = (*t)->StallWhilePending();
            if (tState == ::Assets::AssetState::Invalid) return ::Assets::AssetState::Invalid;
            gotPending |= tState == ::Assets::AssetState::Pending;
        }
        return gotPending ? ::Assets::AssetState::Pending : ::Assets::AssetState::Ready;
    }

    void ModelRenderer::LogReport() const
    {
        Log(Verbose) << "---<< Model Renderer (LOD: " << _pimpl->_levelOfDetail << ") >>---" << std::endl;
        Log(Verbose) << "  [" << _pimpl->_meshes.size() << "] meshes" << std::endl;
        Log(Verbose) << "  [" << _pimpl->_skinnedMeshes.size() << "] skinned meshes" << std::endl;
        Log(Verbose) << "  [" << _pimpl->_constantBuffers.size() << "] constant buffers" << std::endl;
        Log(Verbose) << "  [" << _pimpl->_drawCalls.size() << "] draw calls" << std::endl;
        Log(Verbose) << "  [" << _pimpl->_skinnedDrawCalls.size() << "] skinned draw calls" << std::endl;
        Log(Verbose) << "  [" << _pimpl->_boundTextures.size() << "] bound textures" << std::endl;
        Log(Verbose) << "  [" << _pimpl->_texturesPerMaterial << "] textures per material" << std::endl;
        DEBUG_ONLY(Log(Verbose) << "  [" << _pimpl->_vbSize / 1024.f << "k] VB size" << std::endl);
        DEBUG_ONLY(Log(Verbose) << "  [" << _pimpl->_ibSize / 1024.f << "k] IB size" << std::endl);
        Log(Verbose) << "  Draw calls |  Indxs | GeoC |  Shr | GeoP | MatP |  Tex |   CB |   RS " << std::endl;

        for (unsigned c=0; c<_pimpl->_drawCalls.size(); ++c) {
            const auto&m = _pimpl->_drawCalls[c].first;
            const auto&d = _pimpl->_drawCalls[c].second;
            const auto&r = _pimpl->_drawCallRes[c];
            Log(Verbose)
                << "  [" << Width<3>(c) << "] (M)  |"
                << Width<7>(d._indexCount) << " |"
                << Width<5>(m) << " |"
                << Width<5>(r._shaderName.Value()) << " |"
                << Width<5>(r._geoParamBox.Value()) << " |"
                << Width<5>(r._materialParamBox.Value()) << " |"
                << Width<5>(r._textureSet) << " |"
                << Width<5>(r._constantBuffer) << " |"
                << Width<5>(r._renderStateSet.Value())
				<< std::endl;
        }

        for (unsigned c=0; c<_pimpl->_skinnedDrawCalls.size(); ++c) {
            const auto&m = _pimpl->_skinnedDrawCalls[c].first;
            const auto&d = _pimpl->_skinnedDrawCalls[c].second;
            const auto&r = _pimpl->_drawCallRes[c + _pimpl->_drawCalls.size()];
            Log(Verbose)
                << "  [" << Width<3>(c) << "] (S)  |"
                << Width<7>(d._indexCount) << " |"
                << Width<5>(m) << " |"
                << Width<5>(r._shaderName.Value()) << " |"
                << Width<5>(r._geoParamBox.Value()) << " |"
                << Width<5>(r._materialParamBox.Value()) << " |"
                << Width<5>(r._textureSet) << " |"
                << Width<5>(r._constantBuffer) << " |"
                << Width<5>(r._renderStateSet.Value())
				<< std::endl;
        }

        Log(Verbose) << "  Meshes     | GeoC |  SrcVB |  SrcIB | VtxS | TchI | GeoP | IdxF" << std::endl;

        for (unsigned c=0; c<_pimpl->_meshes.size(); ++c) {
            const auto&m = _pimpl->_meshes[c];
            Log(Verbose)
                << "  [" << Width<3>(c) << "] (M)  |"
                << Width<5>(m._id) << " |"
                #if defined(_DEBUG)
                    << Width<6>(m._vbSize/1024) << "k |"
                    << Width<6>(m._ibSize/1024) << "k |"
                #else
                    << "      ? |      ? |"
                #endif
                << Width<5>(m._vertexStrides[0]) << " |"
                << Width<5>(m._techniqueInterface.Value()) << " |"
                << Width<5>(m._geoParamBox.Value()) << " |"
                << Width<5>(unsigned(m._indexFormat))
				<< std::endl;
        }
        for (unsigned c=0; c<_pimpl->_skinnedMeshes.size(); ++c) {
            const auto&m = _pimpl->_skinnedMeshes[c];
            Log(Verbose)
                << "  [" << Width<3>(c) << "] (S)  |"
                << Width<5>(m._id) << " |"
                #if defined(_DEBUG)
                    << Width<6>(m._vbSize/1024) << "k |"
                    << Width<6>(m._ibSize/1024) << "k |"
                #else
                    << "      ? |      ? |"
                #endif
                << Width<5>(m._vertexStrides[0]) << " |"
                << Width<5>(m._techniqueInterface.Value()) << " |"
                << Width<5>(m._geoParamBox.Value()) << " |"
                << Width<5>(unsigned(m._indexFormat))
				<< std::endl;
        }

        #if defined(_DEBUG)
            if (_pimpl->_texturesPerMaterial) {
                Log(Verbose) << "  Bound Textures" << std::endl;
                for (unsigned c=0; c<_pimpl->_boundTextureNames.size() / _pimpl->_texturesPerMaterial; ++c) {
                    StringMeld<512> temp;
                    for (unsigned q=0; q<_pimpl->_texturesPerMaterial; ++q) {
                        if (q) { temp << ", "; }
                        temp << _pimpl->_boundTextureNames[c*_pimpl->_texturesPerMaterial+q];
                    }
                    Log(Verbose) << "  [" << Width<3>(c) << "] " << temp << std::endl;
                }
            }

            Log(Verbose) << "  Parameter Boxes" << std::endl;
            for (unsigned c=0; c<_pimpl->_paramBoxDesc.size(); ++c) {
                auto& i = _pimpl->_paramBoxDesc[c];
                Log(Verbose) << "  [" << Width<3>(i.first.Value()) << "] " << i.second << std::endl;
            }
        #endif
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

}
