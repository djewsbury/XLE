// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../IDevice_Forward.h"
#include "../ResourceDesc.h"
#include "../../Assets/AssetsCore.h"
#include "../../Utility/StringUtils.h"

namespace Utility { class ParameterBox; }
namespace Assets { class DirectorySearchRules; }
namespace BufferUploads { using CommandListID = uint32_t; class IAsyncDataSource; }
namespace RenderCore { namespace Assets { class TextureCompilationRequest; }}
namespace BufferUploads { using TransactionID = uint64_t; }

namespace RenderCore { namespace Techniques 
{
    /// <summary>Prepares a shader resource from a file source</summary>
    /// This is used to load a file from disk, as use as a shader resource (eg, a texture).
    /// Disk access and GPU upload are performed in background threads. While
    /// the resource is being loaded and upload, GetShaderResource() will throw
    /// PendingAsset().
    ///
    /// The filename can have flags appended after a colon. For example:
    ///   texture.dds:l1
    /// each character after the colon represents one flag or setting.
    /// Valid flags are:
    ///   'L' -- the data in this texture is in linear color space
    ///   'S' -- the data in this texture is in SRGB color space
    ///   'T' -- do not generate mipmaps (however, if mipmaps are already present in the file, they will be used)
    ///
    /// Because all disk access is performed in background threads, the constructor will
    /// not know immediately if the requested file is missing (or invalid). So, if you
    /// attempt to load a texture, but the texture file is missing, the construction will
    /// appear to complete successfully. However, eventually GetShaderResource() will
    /// throw an InvalidAsset() exception.
    ///
    /// The system can load a variety of texture formats, and can perform data massaging as
    /// necessary (for example, building mip-map). This work will happen within the buffer
    /// uploads system, and in background threads.
    ///
    /// However, note that some texture file formats (like tga, tif, etc) cannot contain
    /// precomputed mipmaps. This means the mip-map generation must occur while the texture
    /// is loaded. Building mipmaps for a lot of textures can end up being a large amount of
    /// work -- so it is recommended to use .dds files with precompiled mip maps (.dds files
    /// also allow compressed texture formats).
    class DeferredShaderResource
    {
    public:
		const std::shared_ptr<IResourceView>&   GetShaderResource() const			{ return _srv; }
        const ::Assets::DependencyValidation&   GetDependencyValidation() const     { return _depVal; }
		StringSection<>							Initializer() const					{ return MakeStringSection(_initializer); }
        BufferUploads::CommandListID            GetCompletionCommandList() const    { return _completionCommandList; }

        DeferredShaderResource(
			const std::shared_ptr<IResourceView>& srv,
			const std::string& initializer,
            BufferUploads::CommandListID completionCommandList,
			const ::Assets::DependencyValidation& depVal);
        ~DeferredShaderResource();

		static void ConstructToPromise(
			std::promise<std::shared_ptr<DeferredShaderResource>>&&,
			StringSection<> initializer);
        static void ConstructToPromise(
			std::promise<std::shared_ptr<DeferredShaderResource>>&&,
			const Assets::TextureCompilationRequest& compileRequest);

        static BufferUploads::TransactionID ConstructToTrackablePromise(
			std::promise<std::shared_ptr<DeferredShaderResource>>&&,
			StringSection<> initializer);
    private:
		std::shared_ptr<IResourceView> _srv;
        std::string _initializer;
		::Assets::DependencyValidation _depVal;
        BufferUploads::CommandListID _completionCommandList;
    };

    std::shared_ptr<IResource> CreateResourceImmediately(
        IThreadContext& threadContext,
        BufferUploads::IAsyncDataSource& pkt,
        BindFlag::BitField bindFlags);

    std::shared_ptr<IResource> DestageResource(
        IThreadContext& threadContext,
        const std::shared_ptr<IResource>& input);
    
	/// <summary>Set the RES_HAS technique materials parameter</summary>
    /// There are a few standard RES_HAS shader parameters that are defined by
    /// the resources that are bound to a technique material. This function builds
    /// a parameter box with these parameters set.
    ParameterBox TechParams_SetResHas(
        const ParameterBox& inputMatParameters, const ParameterBox& resBindings,
        const ::Assets::DirectorySearchRules& searchRules);
}}

