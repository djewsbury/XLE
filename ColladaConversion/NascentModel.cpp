// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SCL_SECURE_NO_WARNINGS   // warning C4996: 'std::_Copy_impl': Function call with parameters that may be unsafe

#pragma warning(push)
#pragma warning(disable:4244)   // warning C4244: '=' : conversion from 'const double' to 'float', possible loss of data
#include <algorithm>
#pragma warning(pop)

#include "NascentModel.h"
#include "TableOfObjects.h"
#include "NascentRawGeometry.h"
#include "NascentMaterial.h"
#include "NascentAnimController.h"
#include "ConversionUtil.h"
#include "SkeletonRegistry.h"
#include "OpenCollada\OCInterface.h"
#include "OpenCollada\OCMisc.h"

#include "../RenderCore/Assets/ModelRunTime.h"
#include "../RenderCore/Assets/RawAnimationCurve.h"
#include "../RenderCore/Assets/Material.h"
#include "../RenderCore/Assets/AssetUtils.h"
#include "../RenderCore/Assets/ModelRunTimeInternal.h"
#include "../RenderCore/Techniques/TechniqueUtils.h"    // for CameraDesc

#include "../RenderCore/Metal/Buffer.h"
#include "../RenderCore/Metal/Shader.h"
#include "../RenderCore/Metal/Buffer.h"
#include "../RenderCore/Metal/DeviceContext.h"
#include "../RenderCore/Metal/TextureView.h"
#include "../RenderCore/Metal/State.h"
#include "../RenderCore/Metal/InputLayout.h"
#include "../Assets/BlockSerializer.h"
#include "../Assets/AssetUtils.h"

#include "../ConsoleRig/Console.h"
#include "../ConsoleRig/OutputStream.h"
#include "../ConsoleRig/GlobalServices.h"
#include "../OSServices/Log.h"

#include "../OSServices/RawFS.h"        // (for materials stuff)
#include "../Utility/Streams/Data.h"             // (for materials stuff)
#include "../Utility/Streams/Stream.h"
#include "../Utility/Streams/PathUtils.h"
#include "../Utility/Streams/StreamTypes.h"
#include "../Utility/ParameterBox.h"
#include "../Utility/StringFormat.h"

#pragma warning(push)
#pragma warning(disable:4201)       // nonstandard extension used : nameless struct/union
#pragma warning(disable:4245)       // conversion from 'int' to 'const COLLADAFW::SamplerID', signed/unsigned mismatch
#pragma warning(disable:4512)       // assignment operator could not be generated
    #include <COLLADAFWIWriter.h>
    #include <COLLADAFWImage.h>
    #include <COLLADAFWGeometry.h>
    #include <COLLADAFWEffect.h>
    #include <COLLADAFWMaterial.h>
    #include <COLLADAFWVisualScene.h>
    #include <COLLADAFWLibraryNodes.h>
    #include <COLLADAFWAnimation.h>
    #include <COLLADAFWAnimationCurve.h>
    #include <COLLADAFWAnimationList.h>
    #include <COLLADAFWSkinController.h>
    #include <COLLADAFWSkinControllerData.h>
    #include <COLLADAFWMorphController.h>
    #include <COLLADAFWRoot.h>

    #include <COLLADASaxFWLLoader.h>
    #include <COLLADASaxFWLIErrorHandler.h>
    #include <COLLADASaxFWLIError.h>
    #include <COLLADASaxFWLSaxParserError.h>
    #include <COLLADASaxFWLSaxFWLError.h>
    #include <COLLADASaxFWLInputUnshared.h>

    #include <COLLADASaxFWLIExtraDataCallbackHandler.h>
    #include <generated14\COLLADASaxFWLColladaParserAutoGen14Attributes.h>
    #include <generated15\COLLADASaxFWLColladaParserAutoGen15Attributes.h>

    #include <GeneratedSaxParserParserError.h>
#pragma warning(pop)

#include <regex>

#pragma warning(disable:4127)       // conditional expression is constant

namespace COLLADAFW
{
	class Formulas;
	class Formula;
}

namespace RenderCore { namespace ColladaConversion
{
    using ::Assets::Exceptions::FormatError;
    using ::Assets::ResChar;

    class Writer : public COLLADAFW::IWriter
    {
    public:
	    Writer() : _importConfig("colladaimport.dat")  
        {}

	    virtual ~Writer(){}

	    /** This method will be called if an error in the loading process occurred and the loader cannot
	    continue to to load. The writer should undo all operations that have been performed.
	    @param errorMessage A message containing informations about the error that occurred.
	    */
	    virtual void cancel(const COLLADAFW::String& errorMessage)
        {
            LogAlwaysError << "Got error while parsing Collada: " << errorMessage;
        }

	    /** This is the method called. The writer hast to prepare to receive data.*/
	    virtual void start(){}

	    /** This method is called after the last write* method. No other methods will be called after this.*/
	    virtual void finish(){}

	    /** When this method is called, the writer must write the global document asset.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeGlobalAsset ( const COLLADAFW::FileInfo* asset )
	    {
		    return true;
	    }

	    /** When this method is called, the writer must write the entire visual scene.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeScene ( const COLLADAFW::Scene* scene )
	    {
		    return true;
	    }

	    /** When this method is called, the writer must write the entire visual scene.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeVisualScene ( const COLLADAFW::VisualScene* visualScene );

	    /** When this method is called, the writer must handle all nodes contained in the 
	    library nodes.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeLibraryNodes ( const COLLADAFW::LibraryNodes* libraryNodes );

	    /** When this method is called, the writer must write the geometry.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeGeometry ( const COLLADAFW::Geometry* geometry );

	    /** When this method is called, the writer must write the material.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeMaterial( const COLLADAFW::Material* material );

	    /** When this method is called, the writer must write the effect.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeEffect( const COLLADAFW::Effect* effect );

	    /** When this method is called, the writer must write the camera.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeCamera( const COLLADAFW::Camera* camera )
	    {
		    return true;
	    }

	    /** When this method is called, the writer must write the image.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeImage( const COLLADAFW::Image* image );

	    /** When this method is called, the writer must write the light.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeLight( const COLLADAFW::Light* light )
	    {
		    return true;
	    }

	    /** Writes the animation.
	    @return True on succeeded, false otherwise.*/
	    virtual bool writeAnimation( const COLLADAFW::Animation* animation );

	    /** When this method is called, the writer must write the AnimationList.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeAnimationList( const COLLADAFW::AnimationList* animationList );

	    /** When this method is called, the writer must write the skin controller data.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeSkinControllerData( const COLLADAFW::SkinControllerData* skinControllerData );

	    /** When this method is called, the writer must write the controller.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeController( const COLLADAFW::Controller* controller );

	    /** When this method is called, the writer must write the formula.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeFormulas( const COLLADAFW::Formulas* formulas )
	    { 
		    return true; 
	    }

	    /** When this method is called, the writer must write the formula.
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeFormula( const COLLADAFW::Formula* formulas )
	    { 
		    return true; 
	    }

	    /** When this method is called, the writer must write the kinematics scene. 
	    @return The writer should return true, if writing succeeded, false otherwise.*/
	    virtual bool writeKinematicsScene( const COLLADAFW::KinematicsScene* kinematicsScene )		
	    { 
		    return true; 
	    }

        ColladaConversion::TableOfObjects               _objects;
        ColladaConversion::NascentModelCommandStream    _visualScene;
        ColladaConversion::NascentAnimationSet          _animationSet;
        ColladaConversion::NascentSkeleton              _skeleton;

        class AnimationLink
        {
        public:
            RenderCore::Assets::AnimationParameterId    _animationListName;
            COLLADAFW::UniqueId     _animationId;

            unsigned        _samplerWidth;
            unsigned        _samplerOffset;

            AnimationLink(
                RenderCore::Assets::AnimationParameterId animationListName, 
                const COLLADAFW::UniqueId& animationId, 
                unsigned samplerWidth, unsigned samplerOffset)
            : _animationListName(animationListName), _animationId(animationId)
            , _samplerWidth(samplerWidth), _samplerOffset(samplerOffset) {}
        };

        std::vector<AnimationLink>  _animationLinks;

        void CompleteProcessing();

	    // private function declarations
    private:
    /** Disable default copy ctor. */
	    Writer( const Writer& pre );
    /** Disable default assignment operator. */
	    const Writer& operator= ( const Writer& pre );

        void HandleFormatError(const FormatError& error);

        ImportConfiguration _importConfig;
    };

    void Writer::HandleFormatError(const FormatError& error)
    {
        LogAlwaysWarningF("Supressing format error on Collada import. See description below:\n");
        LogAlwaysWarningF("%s\n", error.what());
    }

    bool Writer::writeImage(const COLLADAFW::Image* image)
    {
        TRY {
            ColladaConversion::ReferencedTexture texture = ColladaConversion::Convert(image);
            _objects.Add(   
                ColladaConversion::Convert(image->getUniqueId()),
                image->getName(),
                image->getOriginalId(),
                std::move(texture));
            return true;
        } CATCH(const FormatError& error) {
            HandleFormatError(error);
        } CATCH_END

        return true;
    }

    bool Writer::writeGeometry(const COLLADAFW::Geometry* geometry)
	{
        TRY {
            ColladaConversion::NascentRawGeometry geo = ColladaConversion::Convert(geometry);
            _objects.Add(
                ColladaConversion::Convert(geometry->getUniqueId()),
                geometry->getName(),
                geometry->getOriginalId(),
                std::move(geo));
            return true;
        } CATCH(const FormatError& error) {
            HandleFormatError(error);
        } CATCH_END

        return false;
	}

    static const utf8 DefaultDiffuseTextureBinding[] = "DiffuseTexture";

    static void AddBoundTexture( 
        const COLLADAFW::Effect* effect, unsigned commonEffectIndex,
        const TableOfObjects& objects,
        ParameterBox& bindings, const utf8 bindingName[], COLLADAFW::SamplerID samplerId)
    {
        auto hash = ParameterBox::MakeParameterNameHash(bindingName);
        if (bindings.HasParameter(hash)) return;

            //
            //      Note that Collada has a method for associating texture coords
            //      with the bound textures. We're ignoring this currently,
            //      it only really useful if we want different texture coordinates
            //      for diffuse and specular parameters map (which we don't normally)
            //
            //      Only really need the diffuse map texture from here. Collada
            //      isn't good for passing the other parameters from Max. Some of
            //      the specular parameters come through -- but it isn't great.
            //

        auto& effectCommons = effect->getCommonEffects();
        if (commonEffectIndex >= effectCommons.getCount() || !effectCommons[commonEffectIndex]) { return; }

        auto& ec = *effectCommons[commonEffectIndex];
        if (    samplerId < ec.getSamplerPointerArray().getCount()
            &&  ec.getSamplerPointerArray()[samplerId]) {

            const auto& sampler = *ec.getSamplerPointerArray()[samplerId];
            const auto* refTexture = objects.Get<ReferencedTexture>(
                ColladaConversion::Convert(sampler.getSourceImage()));
            if (refTexture)
                bindings.SetParameter(bindingName, refTexture->_resourceName);

        }
    }

////////////////////////////////////////////////////////////////////////

    bool Writer::writeEffect( const COLLADAFW::Effect* effect )
    {
        TRY {

            RenderCore::Assets::RawMaterial matSettings;

                //  Any settings from the Collada file should override what we read
                //  in the material settings file. This means that we have 
                //  clear out the settings in the Collada file if we want the .material
                //  file to show through. This pattern works best if we can use 
                //  the .material files to specify the default settings, and then use
                //  the collada data to specialise the settings of specific parts of geometry.

            using namespace COLLADAFW;
            using namespace ColladaConversion;

                // bind the texture in the "common" effects part
            const CommonEffectPointerArray& commonEffects = effect->getCommonEffects();
            for (unsigned c=0; c<commonEffects.getCount(); ++c) {
                auto& diffuse = commonEffects[c]->getDiffuse();
                if (diffuse.getType() == ColorOrTexture::TEXTURE) {
                    AddBoundTexture(
                        effect, c, _objects, matSettings._resourceBindings,
                        DefaultDiffuseTextureBinding, 
                        diffuse.getTexture().getSamplerId());
                } else if (diffuse.getType() == ColorOrTexture::COLOR) {
                    matSettings._constants.SetParameter(
                        (const utf8*)"MaterialDiffuse", 
                        Float3((float)diffuse.getColor().getRed(), (float)diffuse.getColor().getGreen(), (float)diffuse.getColor().getBlue()));
                }
            }

                //  Also bind the "extra textures" part. Note that the
                //  way OpenCollada works, there must be at least one profile_COMMON part
                //  Let's assume the sampler indices refer to the samplers stored in the 
                //  first profile_COMMON object.

            auto& xts = const_cast<COLLADAFW::Effect*>(effect)->getExtraTextures();
            for (unsigned c=0; c<xts.getCount(); ++c) {
                auto xt = xts[c];
                if (!xt) { continue; }

                    // texture coords set and binding point are encoded together as a single string.
                std::regex decode("<([^>]*)><([^>]*)>");
                std::smatch match;
                auto r = std::regex_match(xt->texCoord, match, decode);
                if (r && match.size() >= 2) {
                    auto bindPoint = _importConfig.GetResourceBindings().AsNative((const utf8*)AsPointer(match[2].first), (const utf8*)AsPointer(match[2].second));
                    if (!bindPoint.empty()) {
                        AddBoundTexture(
                            effect, 0, _objects, matSettings._resourceBindings,
                            (const utf8*)bindPoint.c_str(), xt->samplerId);
                    }
                }
            }

            _objects.Add(
                ColladaConversion::Convert(effect->getUniqueId()),
                effect->getName(),
                effect->getOriginalId(),
                std::move(matSettings));
            return true;

        } CATCH(const FormatError& error) {
            HandleFormatError(error);
        } CATCH_END

        return true;
    }

    bool Writer::writeMaterial(const COLLADAFW::Material* material)
    {
        TRY {

            using namespace COLLADAFW;
            using namespace ColladaConversion;

                //  In Collada, geometry instances reference a material
                //  Each material contains an "effect". It's the effect
                //  that contains the real material information.
                //
                //  Both the material and the effect have string ids. But
                //  only the material has a name. So we should use this 
                //  material name when binding to a .material file.

            const UniqueId& effect = material->getInstantiatedEffect();
            _objects.Add(
                ColladaConversion::Convert(material->getUniqueId()),
                material->getName(), 
                material->getOriginalId(),
                ReferencedMaterial(
                    ColladaConversion::Convert(effect), 
                    RenderCore::Assets::MakeMaterialGuid(
                        AsPointer(material->getName().cbegin()), AsPointer(material->getName().cend())),
                    material->getName()));
            
            return true;

        } CATCH(const FormatError& error) {
            HandleFormatError(error);
        } CATCH_END

        return true;
    }

    void Writer::CompleteProcessing()
    {
            //
            //      Any parameters in the transformation machine that don't have 
            //      animation drivers associated with them should get constant
            //      drivers. This is required because we still need to apply constant
            //      transforms in the skeleton information when applying this
            //      animation to an object.
            //
            //      Basically we're just shifting information from the skeleton into
            //      the animation set
            //
        auto defaultParameters = _skeleton.GetSkeletonMachine().GetDefaultParameters();
        struct ParameterType { size_t count; AnimSamplerType samplerType; const void* arrayStart; };
        ParameterType parameterTypes[] = 
        {
            { defaultParameters.GetFloat4x4ParametersCount(),   Assets::AnimatedParameterSet::Type::Float4x4, defaultParameters.GetFloat4x4Parameters() },
            { defaultParameters.GetFloat4ParametersCount(),     Assets::AnimatedParameterSet::Type::Float4,   defaultParameters.GetFloat4Parameters() },
            { defaultParameters.GetFloat3ParametersCount(),     Assets::AnimatedParameterSet::Type::Float3,   defaultParameters.GetFloat3Parameters() },
            { defaultParameters.GetFloat1ParametersCount(),     Assets::AnimatedParameterSet::Type::Float1,   defaultParameters.GetFloat1Parameters() },
        };
        for (size_t t=0; t<dimof(parameterTypes); ++t) {
            for (size_t c=0; c<parameterTypes[t].count; ++c) {
                auto name  = _skeleton.GetSkeletonMachine().GetParameterName(parameterTypes[t].samplerType, (uint32)c);
                std::string stringName      = _skeleton.GetSkeletonMachine().HashedIdToStringId(name);
                if (!_animationSet.HasAnimationDriver(stringName)) {
                    _animationSet.AddConstantDriver(
                        stringName, PtrAdd(parameterTypes[t].arrayStart, c*SamplerSize(parameterTypes[t].samplerType)),
                        parameterTypes[t].samplerType, 0);
                }
            }
        }
    }

    bool Writer::writeVisualScene(const COLLADAFW::VisualScene* visualScene)
    {
        TRY {
                //
                //      Here is a scene of nodes. Let's convert this into a format
                //      that's more useful to use.
                //          (Also remove any nodes that useless to us)
                //
            using namespace COLLADAFW;
            ColladaConversion::NascentModelCommandStream    commandStream;

            size_t rootNodeCount = visualScene->getRootNodes().getCount();
            SkeletonRegistry jointRefs;
            for (size_t c=0; c<rootNodeCount; ++c) {
                    //      are we really going to instantiate every skin controller we find?)
                    //      If we decide we don't need all of them, we can filter out some here.
                FindInstancedSkinControllers(*visualScene->getRootNodes()[c], _objects, jointRefs);
            }

            for (size_t c=0; c<rootNodeCount; ++c) {
                const Node* node = visualScene->getRootNodes()[c];
                if (ColladaConversion::IsUseful(*node, _objects, jointRefs)) {
                    PushNode(_skeleton, *node, _objects, jointRefs);
                    PushNode(commandStream, *node, _objects, jointRefs);
                }
            }

                //
                //      Second pass -- find and instantiate all of the referenced controllers
                //
            for (size_t c=0; c<rootNodeCount; ++c) {
                const Node* node = visualScene->getRootNodes()[c];
                InstantiateControllers(commandStream, *node, _objects, _objects);
            }

                //
                //      Now, read the animation links and add "AnimationDriver" objects as required
                //
            for (auto i=_animationLinks.begin(); i!=_animationLinks.end(); ++i) {
                _animationSet.AddAnimationDriver(
                    _skeleton.GetSkeletonMachine().HashedIdToStringId(i->_animationListName),
                    _objects.GetIndex<Assets::RawAnimationCurve>(ColladaConversion::Convert(i->_animationId)), 
                    SamplerWidthToType(i->_samplerWidth), i->_samplerOffset);
            }

            if (!commandStream.IsEmpty()) {
                _visualScene = std::move(commandStream);
            }
            return true;

        } CATCH(const FormatError& error) {
            HandleFormatError(error);
        } CATCH_END

        return true;
    }

    bool Writer::writeLibraryNodes ( const COLLADAFW::LibraryNodes* libraryNodes )
    {
        TRY {

            using namespace COLLADAFW;

            size_t rootNodeCount = libraryNodes->getNodes().getCount();

            SkeletonRegistry instancedSkinControllers;
            for (size_t c=0; c<rootNodeCount; ++c) {
                    //      are we really going to instantiate every skin controller we find?)
                    //      If we decide we don't need all of them, we can filter out some here.
                FindInstancedSkinControllers(*libraryNodes->getNodes()[c], _objects, instancedSkinControllers);
            }

            for (size_t c=0; c<rootNodeCount; ++c) {
                const Node* node = libraryNodes->getNodes()[c];
                if (IsUseful(*node, _objects, instancedSkinControllers)) {
                    ColladaConversion::NascentModelCommandStream    commandStream;
                    PushNode(commandStream, *node, _objects, instancedSkinControllers);

                    _objects.Add(   
                        ColladaConversion::Convert(node->getUniqueId()),
                        node->getName(),
                        node->getOriginalId(),
                        std::move(commandStream));
                }
            }

        } CATCH(const FormatError& error) {
            HandleFormatError(error);
        } CATCH_END

        return true;
    }

    bool Writer::writeSkinControllerData( const COLLADAFW::SkinControllerData* skinControllerData )
    {
        TRY {

            ColladaConversion::UnboundSkinController unboundController = 
                ColladaConversion::Convert(skinControllerData);

            _objects.Add(   
                ColladaConversion::Convert(skinControllerData->getUniqueId()),
                skinControllerData->getName(),
                skinControllerData->getOriginalId(),
                std::move(unboundController));

            return true;

        } CATCH(const FormatError& error) {
            HandleFormatError(error);
        } CATCH_END

        return true;
    }

    bool Writer::writeController( const COLLADAFW::Controller* controller )
    {
        TRY {

                //
                //      A controller is a binding between some raw geometry and
                //      an animation. 
                //
                //      In the skinning case, 
                //          We need to create a list of joints & their names
                //          We also need to build vertex buffer information for
                //          skinning weights.
                //
                //      Let's put the skinning weights in a vertex buffer separate
                //      from the name vertex buffer. This will allow us to (theoretically
                //      attach multiple skeletons to the same basic geometry)
                //          -- and also render some geometry without skinning enabled.
                //

            using namespace COLLADAFW;
            if (controller->getControllerType() == Controller::CONTROLLER_TYPE_SKIN) {

                const COLLADAFW::SkinController* skinController = objectSafeCast<const COLLADAFW::SkinController>(controller);
                COLLADAFW::UniqueId id = skinController->getSkinControllerData();

                    //  Find this id within our table of objects
                const auto* obj = _objects.Get<ColladaConversion::UnboundSkinController>(
                    ColladaConversion::Convert(id));
                if (obj==nullptr) {
                    LogAlwaysWarningF("Missing skin controller data for skin controller. There must have been a failure while processing this object!\n");
                    return false;
                }

                    //
                    //      The "controller" now has a list of joint ids to attach to the 
                    //      unbound controller data. 
                    //
                    //      This is an extra step only required for OpenCollada... The basic
                    //      Collada standard has all the joint ids already in the original skin object.
                    //      (but maybe it's similar to the "skeleton" attribute of the instance_controller...?
                    //
                    //      For our purposes, we need to match the joint ids up with output matrices
                    //      (ie, the index of the matrix written by the transformation machine)
                    //
                using namespace ColladaConversion;
                UnboundSkinControllerAndAttachedSkeleton result;
                result._unboundControllerId = ColladaConversion::Convert(id);
                result._jointIds.reserve(skinController->getJoints().getCount());
                result._source = ColladaConversion::Convert(skinController->getSource());
                for (size_t c=0; c<skinController->getJoints().getCount(); ++c)
                    result._jointIds.push_back(ColladaConversion::Convert(skinController->getJoints()[c]));

                _objects.Add(
                    ColladaConversion::Convert(skinController->getUniqueId()),
                    "UnnamedSkinController", "UnnamedSkinController", 
                    std::move(result));
                return true;

            } else if (controller->getControllerType() == Controller::CONTROLLER_TYPE_MORPH) {

                    //
                    //      Sometimes we get simple morph controllers with just a single target. In this case,
                    //      we can just treat it like static geometry
                    //
                const COLLADAFW::MorphController* morphController = objectSafeCast<const COLLADAFW::MorphController>(controller);
                if (morphController->getMorphTargets().getCount() > 0) {
                    UnboundMorphController newController;
                    newController._source = ColladaConversion::Convert(morphController->getMorphTargets()[0]);
                    _objects.Add(
                        ColladaConversion::Convert(morphController->getUniqueId()),
                        "UnnamedMorphController", 
                        "UnnamedMorphController", 
                        std::move(newController));
                    return true;
                }

            } else {
                Throw(FormatError(
                    "Unknown controller type found (%s). This isn't currently supported!", controller->getUniqueId().toAscii().c_str()));
            }

            return true;

        } CATCH(const FormatError& error) {
            HandleFormatError(error);
        } CATCH_END

        return true;
    }

    bool Writer::writeAnimation(const COLLADAFW::Animation* animation)
    {
        TRY {

            using namespace ColladaConversion;
            Assets::RawAnimationCurve convertedCurve = Convert(*animation);
            _objects.Add(   ColladaConversion::Convert(animation->getUniqueId()),
                            animation->getName(),
                            animation->getOriginalId(),
                            std::move(convertedCurve));
            return true;

        } CATCH (const FormatError& error) {
            HandleFormatError(error);
        } CATCH_END

        return true;
    }

    std::pair<int, int> AsInterpolatorType(COLLADAFW::AnimationList::AnimationClass input)
    {
        switch (input) {
        case COLLADAFW::AnimationList::TIME:
        case COLLADAFW::AnimationList::ANGLE:
        case COLLADAFW::AnimationList::FLOAT:               return std::make_pair(1, 0);
        case COLLADAFW::AnimationList::COLOR_R:             return std::make_pair(1, 0);
		case COLLADAFW::AnimationList::COLOR_G:             return std::make_pair(1, 1);
		case COLLADAFW::AnimationList::COLOR_B:             return std::make_pair(1, 2);
		case COLLADAFW::AnimationList::COLOR_A:             return std::make_pair(1, 3);
        case COLLADAFW::AnimationList::POSITION_X:          return std::make_pair(1, 0);
		case COLLADAFW::AnimationList::POSITION_Y:          return std::make_pair(1, 1);
		case COLLADAFW::AnimationList::POSITION_Z:          return std::make_pair(1, 2);
        
        case COLLADAFW::AnimationList::POSITION_XYZ:  
        case COLLADAFW::AnimationList::COLOR_RGB:           return std::make_pair(3, 0);

        case COLLADAFW::AnimationList::COLOR_RGBA:    
        case COLLADAFW::AnimationList::AXISANGLE:           return std::make_pair(4, 0);
		
        case COLLADAFW::AnimationList::MATRIX4X4:           return std::make_pair(16, 0);
		
        default:
        case COLLADAFW::AnimationList::UNKNOWN_CLASS:
        case COLLADAFW::AnimationList::ARRAY_ELEMENT_1D:
        case COLLADAFW::AnimationList::ARRAY_ELEMENT_2D:    return std::make_pair(0, 0);
        }
    }

	bool Writer::writeAnimationList(const COLLADAFW::AnimationList* animationList)
    {
        TRY  {

                    //
                    //      We can use this to attach an animation curve to a specific 
                    //      parameter in the command stream. Each parameter in the 
                    //      command stream has a parameter has value that represents
                    //      the unique id of the animation list. So there should be
                    //      a fairly direct attachment.
                    //          (but does this become before or after we write
                    //          the node tree?)
                    //

            for (unsigned c=0; c<animationList->getAnimationBindings().getCount(); ++c) {
                const COLLADAFW::AnimationList::AnimationBinding& sourceBinding = animationList->getAnimationBindings()[c];
                auto interpolatorType = AsInterpolatorType(sourceBinding.animationClass);
                AnimationLink animationLink(
                    BuildAnimParameterId(animationList->getUniqueId()),
                    sourceBinding.animation, 
                    interpolatorType.first, interpolatorType.second);

                        //      If we've build the visual scene already, hook up the animation driver
                std::string stringId = _skeleton.GetSkeletonMachine().HashedIdToStringId(animationLink._animationListName);
                if (stringId.empty()) {
                    LogAlwaysWarningF("Couldn't bind animation driver. Sometimes this happens when there is an unsupported animation type (eg, animated material parameters)\n");
                } else {
                    if (XlFindStringI(stringId.c_str(), "rope")) {
                        continue;   // ignore everything applying to "rope" nodes
                    }
                    _animationSet.AddAnimationDriver(
                        stringId, 
                        _objects.GetIndex<Assets::RawAnimationCurve>(Convert(animationLink._animationId)), 
                        SamplerWidthToType(animationLink._samplerWidth), animationLink._samplerOffset);
                }

                _animationLinks.push_back(animationLink);
            }

            return true;

        } CATCH (const FormatError& error) {
            HandleFormatError(error);
        } CATCH_END

        return true;
    }

    class ValidationErrorHandler : public COLLADASaxFWL::IErrorHandler
    {
    public:
	    ValidationErrorHandler();
	    virtual ~ValidationErrorHandler();

	    bool    handleError(const COLLADASaxFWL::IError* error);
	    bool    hasHandledSaxParserError() const    { return mHasHandledSaxParserError; }
	    bool    hasHandledSaxFWLError() const       { return mHasHandledSaxFWLError; }
	    bool    getFileNotFound() const             { return mFileNotFound; }

    private:
	    /** Disable default copy ctor. */
	    ValidationErrorHandler( const ValidationErrorHandler& pre );
	    /** Disable default assignment operator. */
	    const ValidationErrorHandler& operator= ( const ValidationErrorHandler& pre );

        bool mHasHandledSaxParserError;
	    bool mHasHandledSaxFWLError;
	    bool mFileNotFound;
    };

    //--------------------------------------------------------------------
    ValidationErrorHandler::ValidationErrorHandler()
	: mHasHandledSaxParserError(false)
	, mHasHandledSaxFWLError(false)
	, mFileNotFound(false)
    {
    }

    //--------------------------------------------------------------------
    ValidationErrorHandler::~ValidationErrorHandler()
    {
    }

    //--------------------------------------------------------------------
    bool ValidationErrorHandler::handleError( const COLLADASaxFWL::IError* error )
    {
        if (error->getSeverity() != COLLADASaxFWL::IError::SEVERITY_ERROR_NONCRITICAL) {
            int t = 0;
            (void)t;
        }

	    if ( error->getErrorClass() == COLLADASaxFWL::IError::ERROR_SAXPARSER )
	    {
		    COLLADASaxFWL::SaxParserError* saxParserError = (COLLADASaxFWL::SaxParserError*) error;
		    const GeneratedSaxParser::ParserError& parserError = saxParserError->getError();

		    // Workaround to avoid wrong error
		    if ( parserError.getErrorType() == GeneratedSaxParser::ParserError::ERROR_VALIDATION_MIN_OCCURS_UNMATCHED)
		    {
			    if ( strcmp(parserError.getElement(), "effect") == 0 )
			    {
				    return false;
			    }
		    }
		    if ( parserError.getErrorType() == GeneratedSaxParser::ParserError::ERROR_VALIDATION_SEQUENCE_PREVIOUS_SIBLING_NOT_PRESENT)
		    {
			    if (    (strcmp(parserError.getElement(), "extra") == 0) 
				    &&  (strcmp(parserError.getAdditionalText().c_str(), "sibling: fx_profile_abstract") == 0)) 
			    {

				    return false;
			    }
		    }

		    if ( parserError.getErrorType() == GeneratedSaxParser::ParserError::ERROR_COULD_NOT_OPEN_FILE)
		    {
			    mFileNotFound = true;
		    }

		    LogAlwaysWarningF("Schema validation error: (%s)\n", parserError.getErrorMessage().c_str());
		    mHasHandledSaxParserError = true;
	    }
	    else if ( error->getErrorClass() == COLLADASaxFWL::IError::ERROR_SAXFWL )
	    {
		    COLLADASaxFWL::SaxFWLError* saxFWLError = (COLLADASaxFWL::SaxFWLError*) error;
		    LogAlwaysWarningF("Sax FWL Error: (%s)\n", saxFWLError->getErrorMessage().c_str());
		    mHasHandledSaxFWLError = true;
	    }
	    return false;
    }

    class ExtraDataCallback : public COLLADASaxFWL::IExtraDataCallbackHandler
    {
    public:
        typedef COLLADASaxFWL::ParserChar ParserChar;
        typedef COLLADASaxFWL::StringHash StringHash;

        bool elementBegin(const ParserChar* elementName, const GeneratedSaxParser::xmlChar** attributes);
        bool elementEnd(const ParserChar* elementName);
        bool textData(const ParserChar* text, size_t textLength);
        bool parseElement(
            const ParserChar* profileName, 
            const StringHash& elementHash, 
            const COLLADAFW::UniqueId& uniqueId,
			COLLADAFW::Object* object);

        ExtraDataCallback();
		~ExtraDataCallback();

        class ExtraTexture
        {
        public:
            COLLADAFW::UniqueId     _attached;
            std::string             _bindingPoint;
            std::string             _samplerName;
            std::string             _textureCoordsChannel;
        };
        std::vector<ExtraTexture> _extraTextures;

    protected:
        std::stack<std::string>     _stackOfElements;
        COLLADAFW::UniqueId         _attachedObject;
        COLLADAFW::Effect*          _attachedObjectPtr;
    };

    bool ExtraDataCallback::elementBegin(const ParserChar* elementName, const GeneratedSaxParser::xmlChar** attributes) 
    { 
        if (!XlCompareStringI(elementName, "texture")) {
                //  this is an extra texture definition
                //  we should get a "texture" attribute with a reference to a sampler
            ExtraTexture tex;
            tex._attached = _attachedObject;
            if (!_stackOfElements.empty()) { tex._bindingPoint = _stackOfElements.top(); }
            for (auto a = attributes; *a && *(a+1); a+=2) {
                if (!XlCompareStringI(*a, "texture")) {
                    tex._samplerName = *(a+1);
                }
                if (!XlCompareStringI(*a, "texcoord")) {
                    tex._textureCoordsChannel = *(a+1);
                }
            }
            _extraTextures.push_back(tex);

            if (_attachedObjectPtr) {
                auto* extraTex = _attachedObjectPtr->createExtraTextureAttributes();
                if (extraTex) {
                    extraTex->textureMapId = 0;     // gets filled in by handleExtraEffectTextures, based on texCoord string (getTextureMapIdBySematic(tex._parameterName);)
                    extraTex->samplerId = 0;        // gets filled in by handleExtraEffectTextures
                    extraTex->textureSampler = tex._samplerName;
                    extraTex->texCoord = "<" + tex._textureCoordsChannel + "><" + tex._bindingPoint + ">";
                }
            }
        }

        _stackOfElements.push(elementName);
        return true; 
    }
    
    bool ExtraDataCallback::elementEnd(const ParserChar* elementName) 
    {
        assert(!XlCompareStringI(_stackOfElements.top().c_str(), elementName));
        _stackOfElements.pop();
        return true; 
    }

    bool ExtraDataCallback::textData(const ParserChar* text, size_t textLength) 
    { 
        return false; 
    }

    bool ExtraDataCallback::parseElement(
        const ParserChar* profileName, 
        const StringHash& elementHash, 
        const COLLADAFW::UniqueId& uniqueId,
        COLLADAFW::Object* object)
    {
        assert(_stackOfElements.empty());
        _attachedObjectPtr = nullptr;
        _attachedObject = COLLADAFW::UniqueId::INVALID;

            //  We can use "elementHash" to determine the type
            //  of the parent element. We get two common cases:
            //      HASH_ELEMENT_TECHNIQUE
            //      HASH_ELEMENT_EFFECT
        if (    elementHash == COLLADASaxFWL14::HASH_ELEMENT_TECHNIQUE
            ||  elementHash == COLLADASaxFWL15::HASH_ELEMENT_TECHNIQUE) {

            if (uniqueId.getClassId() == COLLADAFW::COLLADA_TYPE::EFFECT) {

                    //  "OpenCOLLADA3dsMax" element inside <technique> in an effect will
                    //  be a list of extra textures we need.
                if (!XlCompareStringI(profileName, "OpenCOLLADA3dsMax")) {
                    _attachedObject = uniqueId;
                    // assert(dynamic_cast<COLLADAFW::Effect*>(object) == static_cast<COLLADAFW::Effect*>(object));
                    _attachedObjectPtr = static_cast<COLLADAFW::Effect*>(object);    // have to do a cast here
                    return true;
                }
            }
        }

        return false;
    }

    ExtraDataCallback::ExtraDataCallback() 
    {
        _attachedObjectPtr = nullptr;
        _attachedObject = COLLADAFW::UniqueId::INVALID;
    }

	ExtraDataCallback::~ExtraDataCallback() {}

    NascentModel::NascentModel(const ResChar identifier[])
    {
            ////////////////////
        #if 0   // (some basic checks for transform math functions)
            {
                Float4x4 transform = Identity<Float4x4>();
                Combine_IntoRHS(Float3(0.18012f, 0.62315f, 0.33130f),                   transform);
                Combine_IntoRHS(RotationX(-90.0f),                                      transform);
                Combine_IntoRHS(RotationY(0.0f),                                        transform);
                Combine_IntoRHS(RotationZ(-82.46461f),                                  transform);
                Combine_IntoRHS(ArbitraryScale(Float3(0.11485f, 0.11485f, 0.15507f)),   transform);

                const Float3 demoPoint(2.f, 1.5f, 3.f);
                Float4 R1 = transform * Expand(demoPoint, 1.f);

                Float4 R2 = AsFloat4x4(ArbitraryScale(Float3(0.11485f, 0.11485f, 0.15507f))) * Expand(demoPoint, 1.f);
                R2 = AsFloat4x4(RotationZ(-82.46461f)) * R2;
                R2 = AsFloat4x4(RotationY(0.0f)) * R2;
                R2 = AsFloat4x4(RotationX(-90.0f)) * R2;
                R2 = AsFloat4x4(Float3(0.18012f, 0.62315f, 0.33130f)) * R2;

                    // [4x1] = [4x4][4x4][4x4][4x1]

                Float4x4 rotX1 = Identity<Float4x4>(); Combine_IntoRHS(RotationX(-82.46461f), rotX1);
                Float4x4 rotX2 = Combine(MakeRotationMatrix(Float3(1,0,0), -82.46461f),         Identity<Float4x4>());
            
                Float4x4 rotY1 = Identity<Float4x4>(); Combine_IntoRHS(RotationY(-82.46461f), rotY1);
                Float4x4 rotY2 = Combine(MakeRotationMatrix(Float3(0,1,0), -82.46461f),         Identity<Float4x4>());
            
                Float4x4 rotZ1 = Identity<Float4x4>(); Combine_IntoRHS(RotationZ(-82.46461f), rotZ1);
                Float4x4 rotZ2 = Combine(MakeRotationMatrix(Float3(0,0,1), -82.46461f),         Identity<Float4x4>());

                Float4x4 cameraToWorld = Identity<Float4x4>();
                Combine_IntoRHS(Float3(2.22403f, -1.63879f, 1.98092f),  cameraToWorld);
                Combine_IntoRHS(RotationZ(-44.29106f),                  cameraToWorld);
                Combine_IntoRHS(RotationY(-2.38208f),                   cameraToWorld);
                Combine_IntoRHS(RotationX(60.80816f),                   cameraToWorld);
                Float4x4 worldToCamera = InvertOrthonormalTransform(cameraToWorld);

                Float4x4 shouldBeIdentity = Combine(cameraToWorld, worldToCamera);

                #pragma warning(disable:4189)
                int c = 0;
            }
        #endif

        auto extraDataCallback = std::make_unique<ExtraDataCallback>();

            ////////////////////
        if (identifier) {
                // note --  libxml is allocating some globals that never get
                //          cleaned up properly. eg, we can't call "xmlCleanupCharEncodingHandlers" from here!
            ValidationErrorHandler  errorHandler;
            COLLADASaxFWL::Loader   loader(&errorHandler);
	        loader.registerExtraDataCallbackHandler(extraDataCallback.get());

            Writer writer;
	        COLLADAFW::Root root(&loader, &writer);
            
	        root.loadDocument(identifier);
            writer.CompleteProcessing();

            _objects        = std::move(writer._objects);
            _visualScene    = std::move(writer._visualScene);
            _animationSet   = std::move(writer._animationSet);
            _skeleton       = std::move(writer._skeleton);

            ConsoleRig::GetWarningStream().Flush();
        }

        if (identifier) {
            _name = identifier;
        }
        _extraDataCallback = std::move(extraDataCallback);
    }

    NascentModel::~NascentModel()
    {
    }

    unsigned    NascentModel::CameraCount() const
    {
        return (unsigned)_visualScene._cameraInstances.size();
    }

    auto        NascentModel::Camera(unsigned index) const -> Techniques::CameraDesc
    {
            //
            //      We have to run the transformations machine to calculate
            //      the current state of the camera.
            //      But we don't have all of the animation parameters, etc...
            //
        if (index >= _visualScene._cameraInstances.size()) return Techniques::CameraDesc();
        auto finalMatrices = _skeleton.GetSkeletonMachine().GenerateOutputTransforms(
            _skeleton.GetSkeletonMachine().GetDefaultParameters());
        Techniques::CameraDesc result;
        result._cameraToWorld       = finalMatrices[_visualScene._cameraInstances[index]._localToWorldId];
        result._nearClip            = 0.1f;
        result._farClip             = 1000.f;
        result._verticalFieldOfView = Deg2Rad(34.8246f);
        return result;
    }

    std::pair<Float3, Float3> CalculateBoundingBox
        (
            const NascentModelCommandStream& scene,
            const TableOfObjects& objects,
            const Float4x4* transformsBegin, 
            const Float4x4* transformsEnd
        )
    {
            //
            //      For all the parts of the model, calculate the bounding box.
            //      We just have to go through each vertex in the model, and
            //      transform it into model space, and calculate the min and max values
            //      found;
            //
        using namespace ColladaConversion;
        auto result = InvalidBoundingBox();
        // const auto finalMatrices = 
        //     _skeleton.GetSkeletonMachine().GenerateOutputTransforms(
        //         _animationSet.BuildTransformationParameterSet(0.f, nullptr, _skeleton, _objects));

            //
            //      Do the unskinned geometry first
            //

        for (auto i=scene._geometryInstances.cbegin(); i!=scene._geometryInstances.cend(); ++i) {
            const NascentModelCommandStream::GeometryInstance& inst = *i;

            const NascentRawGeometry*  geo = objects.GetByIndex<NascentRawGeometry>(inst._id);
            if (!geo) continue;

            Float4x4 localToWorld = Identity<Float4x4>();
            if ((transformsBegin + inst._localToWorldId) < transformsEnd)
                localToWorld = *(transformsBegin + inst._localToWorldId);

            const void*         vertexBuffer = geo->_vertices.get();
            const unsigned      vertexStride = geo->_mainDrawInputAssembly._vertexStride;

            Metal::InputElementDesc positionDesc = FindPositionElement(
                AsPointer(geo->_mainDrawInputAssembly._vertexInputLayout.begin()),
                geo->_mainDrawInputAssembly._vertexInputLayout.size());

            if (positionDesc._nativeFormat != Metal::NativeFormat::Unknown && vertexStride) {
                AddToBoundingBox(
                    result, vertexBuffer, vertexStride, 
                    geo->_vertices.size() / vertexStride, positionDesc, localToWorld);
            }
        }

            //
            //      Now also do the skinned geometry. But use the default pose for
            //      skinned geometry (ie, don't apply the skinning transforms to the bones).
            //      Obvious this won't give the correct result post-animation.
            //

        for (auto i=scene._skinControllerInstances.cbegin(); i!=scene._skinControllerInstances.cend(); ++i) {
            const NascentModelCommandStream::SkinControllerInstance& inst = *i;

            const NascentBoundSkinnedGeometry* controller = objects.GetByIndex<NascentBoundSkinnedGeometry>(inst._id);
            if (!controller) continue;

            Float4x4 localToWorld = Identity<Float4x4>();
            if ((transformsBegin + inst._localToWorldId) < transformsEnd)
                localToWorld = *(transformsBegin + inst._localToWorldId);

                //  We can't get the vertex position data directly from the vertex buffer, because
                //  the "bound" object is already using an opaque hardware object. However, we can
                //  transform the local space bounding box and use that.

            const unsigned indices[][3] = 
            {
                {0,0,0}, {0,1,0}, {1,0,0}, {1,1,0},
                {0,0,1}, {0,1,1}, {1,0,1}, {1,1,1}
            };

            const Float3* A = (const Float3*)&controller->_localBoundingBox.first;
            for (unsigned c=0; c<dimof(indices); ++c) {
                Float3 position(A[indices[c][0]][0], A[indices[c][1]][1], A[indices[c][2]][2]);
                AddToBoundingBox(result, position, localToWorld);
            }
        }

        return result;
    }

    static void DestroyChunkArray(const void* chunkArray) { delete (std::vector<NascentChunk>*)chunkArray; }

    NascentChunkArray NascentModel::SerializeSkin() const
    {
        BlockSerializer serializer;
        std::vector<uint8> largeResourcesBlock;

        const bool traceSkeleton = false;
        if (constant_expression<traceSkeleton>::result()) {
            auto i = _skeleton.GetSkeletonMachine().GetCommandStream();
            Assets::TraceTransformationMachine(
                ConsoleRig::GetWarningStream(), 
                AsPointer(i.begin()), AsPointer(i.end()));
            ConsoleRig::GetWarningStream().Flush();
        }

        SerializationOperator(serializer, _visualScene);
        _objects.SerializeSkin(serializer, largeResourcesBlock);

        SerializationOperator(serializer, _skeleton);

            // Generate the default transforms and serialize them out
            // unfortunately this requires we use the run-time types to
            // work out the transforms.
            // And that requires a bit of hack to get pointers to those 
            // run-time types
        {
            auto tempBlock = serializer.AsMemoryBlock();
            using namespace RenderCore::Assets;

            Block_Initialize(tempBlock.get());
            auto* immData = (const ModelImmutableData*)Block_GetFirstObject(tempBlock.get());

            const auto& transMachine = immData->_embeddedSkeleton;
            auto defTransformCount = transMachine.GetOutputMatrixCount();
            auto skeletonOutput = std::make_unique<Float4x4[]>(defTransformCount);
            transMachine.GenerateOutputTransforms(
                skeletonOutput.get(), defTransformCount,
                &transMachine.GetDefaultParameters());

            RenderCore::Assets::SkeletonBinding skelBinding(
                transMachine.GetOutputInterface(), 
                immData->_visualScene.GetInputInterface());

            auto finalMatrixCount = immData->_visualScene.GetInputInterface()._jointCount;
            auto reordered = std::make_unique<Float4x4[]>(finalMatrixCount);
            for (size_t c = 0; c < finalMatrixCount; ++c) {
                auto machineOutputIndex = skelBinding.ModelJointToMachineOutput(c);
                if (machineOutputIndex == ~unsigned(0x0)) {
                    reordered[c] = Identity<Float4x4>();
                } else {
                    reordered[c] = skeletonOutput[machineOutputIndex];
                }
            }

                // if we have any non-identity internal transforms, then we should 
                // write a default set of transformations. But many models don't have any
                // internal transforms -- in this case all of the generated transforms
                // will be identity. If we find this case, they we should write zero
                // default transforms.
            bool hasNonIdentity = false;
            const float tolerance = 1e-6f;
            for (unsigned c=0; c<finalMatrixCount; ++c)
                hasNonIdentity |= !Equivalent(reordered[c], Identity<Float4x4>(), tolerance);
            if (!hasNonIdentity)
                finalMatrixCount = 0;

            serializer.SerializeSubBlock(reordered.get(), &reordered[finalMatrixCount]);
            serializer.SerializeValue(finalMatrixCount);

            auto boundingBox = CalculateBoundingBox(
                _visualScene, _objects,
                reordered.get(), &reordered[finalMatrixCount]);
            SerializationOperator(serializer, boundingBox.first);
            SerializationOperator(serializer, boundingBox.second);
            
            immData->~ModelImmutableData();
        }

        ConsoleRig::GetWarningStream().Flush();

        auto block = serializer.AsMemoryBlock();
        size_t size = Block_GetSize(block.get());

        ChunkFile::ChunkHeader scaffoldChunk(
            RenderCore::Assets::ChunkType_ModelScaffold, 0, _name.c_str(), unsigned(size));
        ChunkFile::ChunkHeader largeBlockChunk(
            RenderCore::Assets::ChunkType_ModelScaffoldLargeBlocks, 0, _name.c_str(), (unsigned)largeResourcesBlock.size());

        NascentChunkArray result(new std::vector<NascentChunk>(), &DestroyChunkArray);
        result->push_back(NascentChunk(scaffoldChunk, std::vector<uint8>(block.get(), PtrAdd(block.get(), size))));
        result->push_back(NascentChunk(largeBlockChunk, std::move(largeResourcesBlock)));
        return std::move(result);
    }

    NascentChunkArray NascentModel::SerializeAnimationSet() const
    {
        BlockSerializer serializer;

        SerializationOperator(serializer, _animationSet);
        _objects.SerializeAnimationSet(serializer);
        ConsoleRig::GetWarningStream().Flush();

        auto block = serializer.AsMemoryBlock();
        size_t size = Block_GetSize(block.get());

        ChunkFile::ChunkHeader scaffoldChunk(
            RenderCore::Assets::ChunkType_AnimationSet, 0, _name.c_str(), unsigned(size));

        NascentChunkArray result(new std::vector<NascentChunk>(), &DestroyChunkArray);
        result->push_back(NascentChunk(scaffoldChunk, std::vector<uint8>(block.get(), PtrAdd(block.get(), size))));
        return std::move(result);
    }

    NascentChunkArray NascentModel::SerializeSkeleton() const
    {
        BlockSerializer serializer;

        SerializationOperator(serializer, _skeleton);
        ConsoleRig::GetWarningStream().Flush();

        auto block = serializer.AsMemoryBlock();
        size_t size = Block_GetSize(block.get());

        ChunkFile::ChunkHeader scaffoldChunk(
            RenderCore::Assets::ChunkType_Skeleton, 0, _name.c_str(), unsigned(size));

        NascentChunkArray result(new std::vector<NascentChunk>(), &DestroyChunkArray);
        result->push_back(NascentChunk(scaffoldChunk, std::vector<uint8>(block.get(), PtrAdd(block.get(), size))));
        return std::move(result);
    }

    NascentChunkArray NascentModel::SerializeMaterials() const
    {
        std::string matSettingsFile;
        {
            ResChar settingsName[MaxPath];
            XlBasename(settingsName, dimof(settingsName), _name.c_str());
            XlChopExtension(settingsName);
            XlCatString(settingsName, dimof(settingsName), ".material");
            matSettingsFile = settingsName;
        }

        auto table = _objects.SerializeMaterial();

        MemoryOutputStream<char> strm;
        auto root = std::make_unique<Data>();
        for (auto i=table.begin(); i!=table.end(); ++i) {
            root->Add(i->release());
        }
        root->SaveToOutputStream(strm);

            // convert into a chunk...

        auto finalSize = size_t(strm.GetBuffer().End()) - size_t(strm.GetBuffer().Begin());

        ChunkFile::ChunkHeader scaffoldChunk(
            RenderCore::Assets::ChunkType_RawMat, 0, _name.c_str(), ChunkFile::SizeType(finalSize));

        NascentChunkArray result(new std::vector<NascentChunk>(), &DestroyChunkArray);
        result->push_back(NascentChunk(
            scaffoldChunk, 
            std::vector<uint8>(strm.GetBuffer().Begin(), strm.GetBuffer().End())));
        return std::move(result);
    }

    void         NascentModel::MergeAnimationData(const NascentModel& source, const char animationName[])
    {
        // _animationSet.MergeAnimation(source._animationSet, animationName, source._objects, _objects);
    }

    static void DestroyModel(const void* model) { delete (NascentModel*)model; }

    std::unique_ptr<NascentModel, Internal::CrossDLLDeletor>    OCCreateModel(const ResChar identifier[])
    {
        return std::unique_ptr<NascentModel, Internal::CrossDLLDeletor>(
            std::make_unique<NascentModel>(identifier).release(), 
            Internal::CrossDLLDeletor(&DestroyModel));
    }
}}

