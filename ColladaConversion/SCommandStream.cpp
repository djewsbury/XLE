// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "SCommandStream.h"
#include "SRawGeometry.h"
#include "SAnimation.h"

#include "Scaffold.h"
#include "ScaffoldParsingUtil.h"

#include "../RenderCore/GeoProc/NascentCommandStream.h"
#include "../RenderCore/GeoProc/NascentRawGeometry.h"
#include "../RenderCore/GeoProc/NascentAnimController.h"
#include "../RenderCore/GeoProc/GeoProcUtil.h"

#include "../RenderCore/Assets/MaterialScaffold.h"  // for MakeMaterialGuid
#include "../RenderCore/Format.h"
#include "../OSServices/Log.h"
#include "../Utility/MemoryUtils.h"
#include "../Utility/StringFormat.h"
#include <string>

namespace ColladaConversion
{
    static RenderCore::Assets::GeoProc::NascentObjectGuid   AsObjectGuid(const Node& node);

///////////////////////////////////////////////////////////////////////////////////////////////////

    LODDesc GetLevelOfDetail(const ::ColladaConversion::Node& node)
    {
        // We're going assign a level of detail to this node based on naming conventions. We'll
        // look at the name of the node (rather than the name of the geometry object) and look
        // for an indicator that it includes a LOD number.
        // We're looking for something like "$lod" or "_lod". This should be followed by an integer,
        // and with an underscore following.
        if (    XlBeginsWithI(node.GetName(), MakeStringSection("_lod"))
            ||  XlBeginsWithI(node.GetName(), MakeStringSection("$lod"))) {

            auto nextSection = MakeStringSection(node.GetName().begin()+4, node.GetName().end());
            uint32 lod = 0;
            auto* parseEnd = FastParseValue(nextSection, lod);
            if (parseEnd < nextSection.end() && *parseEnd == '_')
                return LODDesc { lod, true, MakeStringSection(parseEnd+1, node.GetName().end()) };
            Log(Warning) << "Node name (" << node.GetName() << ") looks like it contains a lod index, but parse failed. Defaulting to lod 0." << std::endl;
        }
        return LODDesc { 0, false, StringSection<utf8>() };
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	//#pragma warning(disable:4505) // 'RenderCore::ColladaConversion::FloatBits' : unreferenced local function has been removed)
    static unsigned int FloatBits(float input)
    {
            // (or just use a reinterpret cast)
        union Converter { float f; unsigned int i; };
        Converter c; c.f = float(input); 
        return c.i;
    }
    //
    //static unsigned int FloatBits(double input)
    //{
    //        // (or just use a reinterpret cast)
    //    union Converter { float f; unsigned int i; };
    //    Converter c; c.f = float(input); 
    //    return c.i;
    //}

    unsigned PushTransformations(
        RenderCore::Assets::GeoProc::NascentSkeleton& dst,
        const Transformation& transformations,
        const char nodeName[],
        const std::function<bool(StringSection<>)>& predicate)
    {
		using RenderCore::Assets::TransformCommand;

        if (!transformations)
            return 0;

            //
            //      Push in the commands for this node
            //

        dst.WritePushLocalToWorld();
        const unsigned pushCount = 1;

        #if defined(_DEBUG)
            unsigned typesOfEachTransform[7] = {0,0,0,0,0,0};
        #endif

            //
            //      First, push in the transformations information.
            //      We're going to push in just the raw data from Collada
            //      This is most useful for animating stuff; because we
            //      can just change the parameters exactly as they appear
            //      in the raw data stream.
            //

        Transformation trans = transformations;
        for (; trans; trans = trans.GetNext()) {
            auto type = trans.GetType();
            if (type == TransformationSet::Type::None) continue;

            #if defined(_DEBUG)
                unsigned typeIndex = 0;
                if (unsigned(type) < dimof(typesOfEachTransform)) {
                    typeIndex = typesOfEachTransform[(unsigned)type];
                    ++typesOfEachTransform[(unsigned)type];
                }
            #endif

                //
                //      Sometimes the transformation is static -- and it's better
                //      to combine multiple transforms into one. 
                //
                //      However, we should do this after the full transformation
                //      stream has been made. That way we can use the same logic
                //      to combine transformations from multiple nodes into one.
                //

            enum ParameterType
            {
                ParameterType_Embedded,
                ParameterType_AnimationConstant,
                ParameterType_Animated
            } parameterType;

                // Note -- in Collada, we can assume that any transform without a "sid" is not
                //  animated (because normally the animation controller should use the sid to
                //  reference it)
            auto paramName = std::string(nodeName) + "/" + AsString(trans.GetSid());
            const bool isAnimated = !trans.GetSid().IsEmpty() && predicate(paramName);
            parameterType = isAnimated ? ParameterType_Animated : ParameterType_Embedded;

                // We ignore transforms that are too close to their identity...
            const auto transformThreshold   = 1e-3f;
            const auto translationThreshold = 1e-3f;
            const auto rotationThreshold    = 1e-3f;    // (in radians)
            const auto scaleThreshold       = 1e-3f;

            if  (type == TransformationSet::Type::Matrix4x4) {

                RenderCore::Assets::GeoProc::NascentSkeleton::Transform transform;
                transform._fullTransform = *(const Float4x4*)trans.GetUnionData();

                if (parameterType != ParameterType_Embedded) {
                    dst.WriteParameterizedTransform(paramName, transform);
                } else {
                    if (Equivalent(*(Float4x4*)trans.GetUnionData(), Identity<Float4x4>(), transformThreshold)) {
                        // ignore transform by identity
                    } else {
                        dst.WriteStaticTransform(transform);
                    }
                }

            } else if (type == TransformationSet::Type::Translate) {

                RenderCore::Assets::GeoProc::NascentSkeleton::Transform transform;
                transform._translation = *(const Float3*)trans.GetUnionData();

                if (    parameterType != ParameterType_Embedded) {
                    dst.WriteParameterizedTransform(paramName, transform);
                } else {
                    if (Equivalent(*(Float3*)trans.GetUnionData(), Float3(0.f, 0.f, 0.f), translationThreshold)) {
                        // ignore translate by zero
                    } else {
                        dst.WriteStaticTransform(transform);
                    }
                }

            } else if (type == TransformationSet::Type::Rotate) {

                const auto& rot = *(const ArbitraryRotation*)trans.GetUnionData();
                RenderCore::Assets::GeoProc::NascentSkeleton::Transform transform;
                transform._rotationAsAxisAngle = rot;

                if (parameterType != ParameterType_Embedded) {

                        // Post animation, this may become a rotation around any axis. So
                        // we can't perform an optimisation to squish it to rotation around
                        // one of the cardinal axes
                    dst.WriteParameterizedTransform(paramName, transform);

                } else {

                    if (Equivalent(rot._angle, 0.f, rotationThreshold)) {
                        // the angle is too small -- just ignore it
                    } else if (signed x = rot.IsRotationX()) {
                        dst.GetSkeletonMachine().PushCommand(TransformCommand::RotateX_Static);
                        dst.GetSkeletonMachine().PushCommand(FloatBits(float(x) * rot._angle));
                    } else if (signed y = rot.IsRotationY()) {
                        dst.GetSkeletonMachine().PushCommand(TransformCommand::RotateY_Static);
                        dst.GetSkeletonMachine().PushCommand(FloatBits(float(y) * rot._angle));
                    } else if (signed z = rot.IsRotationZ()) {
                        dst.GetSkeletonMachine().PushCommand(TransformCommand::RotateZ_Static);
                        dst.GetSkeletonMachine().PushCommand(FloatBits(float(z) * rot._angle));
                    } else {
                        dst.WriteStaticTransform(transform);
                    }

                }
                        
            } else if (type == TransformationSet::Type::Scale) {

                    //
                    //      If the scale values start out uniform, let's assume
                    //      they stay uniform over all animations.
                    //
                    //      We can't guarantee that case. For example, and object
                    //      may start with (1,1,1) scale, and change to (2,1,1)
                    //
                    //      But, let's just ignore that possibility for the moment.
                    //
                auto scale = *(const Float3*)trans.GetUnionData();
                bool isUniform = Equivalent(scale[0], scale[1], scaleThreshold) && Equivalent(scale[0], scale[2], scaleThreshold);

                if (parameterType != ParameterType_Embedded) {
                    if (isUniform) {
                        RenderCore::Assets::GeoProc::NascentSkeleton::Transform transform;
                        transform._uniformScale = scale[0];
                        dst.WriteParameterizedTransform(paramName, transform);
                    } else {
                        RenderCore::Assets::GeoProc::NascentSkeleton::Transform transform;
                        transform._arbitraryScale = scale;
                        dst.WriteParameterizedTransform(paramName, transform);
                    }
                } else {

                    if (Equivalent(scale, Float3(1.f, 1.f, 1.f), scaleThreshold)) {
                        // scaling by 1 -- just ignore
                    } else if (isUniform) {
                        dst.GetSkeletonMachine().PushCommand(TransformCommand::UniformScale_Static);
                        dst.GetSkeletonMachine().PushCommand(FloatBits(scale[0]));
                    } else {
                        dst.GetSkeletonMachine().PushCommand(TransformCommand::ArbitraryScale_Static);
                        dst.GetSkeletonMachine().PushCommand(&scale, sizeof(scale));
                    }
                }

            } else {

				Log(Warning) << "Warning -- unsupported transformation type found in node (" << nodeName << ") -- type (" << (unsigned)type << ")" << std::endl;

            }
        }

        return pushCount;
    }

	void BuildSkeleton(RenderCore::Assets::GeoProc::NascentSkeleton& skeleton, const Node& node, StringSection<> skeletonName)
    {
        auto nodeId = AsObjectGuid(node);
        auto bindingName = SkeletonBindingName(node);

		auto pushCount = PushTransformations(
			skeleton,
			node.GetFirstTransform(), bindingName.c_str(),
			[](StringSection<>) { return true; });

		skeleton.WriteOutputMarker(skeletonName, bindingName);

            // note -- also consider instance_nodes?

        auto child = node.GetFirstChild();
        while (child) {
			BuildSkeleton(skeleton, child, skeletonName);
            child = child.GetNextSibling();
        }

        skeleton.WritePopLocalToWorld(pushCount);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	auto BuildMaterialTableStrings(
        IteratorRange<const InstanceGeometry::MaterialBinding*> bindings,
        const std::vector<uint64>& rawGeoBindingSymbols,
        const URIResolveContext& resolveContext)

        -> std::vector<std::string>
    {

            //
            //  For each material referenced in the raw geometry, try to 
            //  match it with a material we've built during collada processing
            //      We have to map it via the binding table in the InstanceGeometry
            //
                        
        std::vector<std::string> materialGuids;
        materialGuids.resize(rawGeoBindingSymbols.size());

        for (auto b=bindings.begin(); b<bindings.end(); ++b) {
            auto hashedSymbol = Hash64(b->_bindingSymbol._start, b->_bindingSymbol._end);

            for (auto i=rawGeoBindingSymbols.cbegin(); i!=rawGeoBindingSymbols.cend(); ++i) {
                if (*i != hashedSymbol) continue;
            
                auto index = std::distance(rawGeoBindingSymbols.cbegin(), i);

                std::string newMaterialGuid;

                GuidReference ref(b->_reference);
                auto* file = resolveContext.FindFile(ref._fileHash);
                if (file) {
                    const auto* mat = file->FindMaterial(ref._id);
                    if (mat) {
                        newMaterialGuid = mat->_name.AsString();
                    }
                }


                if (!materialGuids[index].empty() && materialGuids[index] != newMaterialGuid) {

                        // Some collada files can actually have multiple instance_material elements for
                        // the same binding symbol. Let's throw an exception in this case (but only
                        // if the bindings don't agree)
                    Throw(::Exceptions::BasicLabel("Single material binding symbol is bound to multiple different materials in geometry instantiation"));
                }

                materialGuids[index] = newMaterialGuid;
            }
        }

        return std::move(materialGuids);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    std::string SkeletonBindingName(const Node& node)
    {
        // Both "name" and "id" are optional.
		// It turns out we must priortize the name here, because of cross-file binding.
		// There's no real guarantee that 2 nodes will have the same ids in different files,
		// since the ids are algorithmically generated from the names.
		// However if we keep to a convention of not duplicating names, we 
        if (!node.GetName().IsEmpty())
            return ColladaConversion::AsString(node.GetName()); 
		if (!node.GetId().IsEmpty())
            return ColladaConversion::AsString(node.GetId().GetOriginal());
        return XlDynFormatString("Unnamed%i", (unsigned)node.GetIndex());
    }

    static RenderCore::Assets::GeoProc::NascentObjectGuid AsObjectGuid(const Node& node)
    { 
        if (!node.GetId().IsEmpty())
			return { node.GetId().GetHash() }; 
        if (!node.GetName().IsEmpty())
			return { Hash64(node.GetName().begin(), node.GetName().end()) };

        // If we have no name & no id -- it is truly anonymous. 
        // We can just use the index of the node, it's the only unique
        // thing we have.
		return { node.GetIndex() };
    }

}

