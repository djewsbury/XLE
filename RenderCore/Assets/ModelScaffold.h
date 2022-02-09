// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/ChunkFileContainer.h"
#include "../../Assets/AssetsCore.h"
#include "../../Math/Vector.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Core/Prefix.h"
#include <vector>
#include <memory>

namespace Assets {  class IFileInterface; }

namespace RenderCore { namespace Assets
{
    class ModelCommandStream;
    class SkeletonMachine;
    
    class MaterialScaffold;
    class ModelSupplementScaffold;

    class AnimationImmutableData;
    class ModelImmutableData;
    class ModelSupplementImmutableData;

    typedef uint64_t MaterialGuid;

////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>Structural data describing a model</summary>
    /// The "scaffold" of a model contains the structural data of a model, without the large
    /// assets and without any platform-api resources.
    ///
    /// Normally the platform api sources are instantiated in a "ModelRenderer". These two
    /// classes work closely together.
    ///
    /// However, a scaffold can be used independently from a renderer. The scaffold is a very
    /// light weight object. That means many can be loaded into memory at a time. It also
    /// means that we can load and query information from model files, without any executing
    /// any platform-specific code (for tools and for extracting metrics information).
    ///
    /// see RenderCore::Assets::Simple::ModelScaffold for more information about the scaffold
    /// concept.
    ///
    /// The ModelScaffold is loaded from a chunk-format file. See the Assets::ChunkFile
    /// namespace for information about chunk files.
    ///
    /// <seealso cref="ModelRenderer"/>
    class ModelScaffold
    {
    public:
        const ModelCommandStream&       CommandStream() const;
        const ModelImmutableData&       ImmutableData() const;
        const SkeletonMachine&			EmbeddedSkeleton() const;
        std::pair<Float3, Float3>       GetStaticBoundingBox(unsigned lodIndex = 0) const;
        unsigned                        GetMaxLOD() const;

		const ::Assets::DependencyValidation&					GetDependencyValidation() const { return _depVal; }
		std::shared_ptr<::Assets::IFileInterface>	OpenLargeBlocks() const;

        static const auto CompileProcessType = ConstHash64<'Mode', 'l'>::Value;
		static const ::Assets::ArtifactRequest ChunkRequests[2];

        ModelScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
        ModelScaffold(ModelScaffold&& moveFrom) never_throws;
        ModelScaffold& operator=(ModelScaffold&& moveFrom) never_throws;
        ModelScaffold();
        ~ModelScaffold();

    private:
        std::unique_ptr<uint8[], PODAlignedDeletor>		_rawMemoryBlock;
		::Assets::ArtifactReopenFunction				_largeBlocksReopen;
		::Assets::DependencyValidation								_depVal;
    };
    
////////////////////////////////////////////////////////////////////////////////////////////
    
    /// <summary>Supplemental vertex data associated with a model</summary>
    /// Some techniques require adding extra vertex data onto a model.
    /// For example, internal model static ambient occlusion might add another
    /// vertex element for each vertex.
    ///
    /// A ModelSupplement is a separate file that contains extra vertex 
    /// streams associated with some separate model file.
    ///
    /// This is especially useful for vertex elements that are only required
    /// in some quality modes. In the example mode, low quality mode might 
    /// disable the internal ambient occlusion -- and in this case we might
    /// skip loading the model supplement.
    ///
    /// The supplement can only add extra vertex elements to vertices that 
    /// already exist in the main model. It can't add new vertices.
    ///
    /// <seealso cref="ModelScaffold"/>
    /// <seealso cref="ModelRenderer"/>
    class ModelSupplementScaffold
    {
    public:
        unsigned LargeBlocksOffset() const;
        const ModelSupplementImmutableData& ImmutableData() const;

		const ::Assets::DependencyValidation&					GetDependencyValidation() const { return _depVal; }
		std::shared_ptr<::Assets::IFileInterface>	OpenLargeBlocks() const;

        ModelSupplementScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
        ModelSupplementScaffold(ModelSupplementScaffold&& moveFrom) never_throws;
        ModelSupplementScaffold& operator=(ModelSupplementScaffold&& moveFrom) never_throws;
        ModelSupplementScaffold();
        ~ModelSupplementScaffold();

		static const auto CompileProcessType = ConstHash64<'Mode', 'l'>::Value;
		static const ::Assets::ArtifactRequest ChunkRequests[2];

    private:
        std::unique_ptr<uint8[], PODAlignedDeletor>	_rawMemoryBlock;
		::Assets::ArtifactReopenFunction			_largeBlocksReopen;
		::Assets::DependencyValidation							_depVal;
    };

////////////////////////////////////////////////////////////////////////////////////////////

    /// <summary>Structural data for a skeleton</summary>
    /// Represents the skeleton of a model.
    ///
    /// Animated models are split into 3 parts:
    ///     <list>
    ///         <item> AnimationSetScaffold
    ///         <item> SkeletonScaffold
    ///         <item> ModelScaffold (skin)
    ///     </list>
    ///
    /// Each is bound to other using interfaces of strings. The AnimationSetScaffold provides
    /// the current state of animatable parameters. The SkeletonScaffold converts those
    /// parameters into a set of low level local-to-world transformations. And finally, the 
    /// ModelScaffold knows how to render a skin over the transformations.
    ///
    /// Here, SkeletonScaffold is intentionally designed with a flattened non-hierarchical
    /// data structure. In the 3D editing tool, the skeleton will be represented by a 
    /// hierarchy of nodes. But in our run-time representation, that hierarchy has become a 
    /// a linear list of instructions, with push/pop operations. It's similar to converting
    /// a recursive method into a loop with a stack.
    ///
    /// The vertex weights are defined in the ModelScaffold. The skeleton only defines 
    /// information related to the bones, not the vertices bound to them.
    ///
    /// <seealso cref="ModelScaffold" />
    /// <seealso cref="AnimationSetScaffold" />
    class SkeletonScaffold
    {
    public:
        const SkeletonMachine&			GetSkeletonMachine() const;

		const ::Assets::DependencyValidation&					GetDependencyValidation() const { return _depVal;  }

        static const auto CompileProcessType = ConstHash64<'Skel', 'eton'>::Value;
		static const ::Assets::ArtifactRequest ChunkRequests[1];

        SkeletonScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
        SkeletonScaffold(SkeletonScaffold&& moveFrom) never_throws;
        SkeletonScaffold& operator=(SkeletonScaffold&& moveFrom) never_throws;
        SkeletonScaffold();
        ~SkeletonScaffold();

    private:
        std::unique_ptr<uint8[], PODAlignedDeletor>    _rawMemoryBlock;
		::Assets::DependencyValidation _depVal;
    };

    /// <summary>Structural data for animation</summary>
    /// Represents a set of animation that can potentially be applied to a skeleton.
    ///
    /// See SkeletonScaffold for more information.
    ///
    /// <seealso cref="ModelScaffold" />
    /// <seealso cref="SkeletonScaffold" />
    class AnimationSetScaffold
    {
    public:
        const AnimationImmutableData&   ImmutableData() const;

		const ::Assets::DependencyValidation&					GetDependencyValidation() const { return _depVal; }

        static const auto CompileProcessType = ConstHash64<'Anim', 'Set'>::Value;
		static const ::Assets::ArtifactRequest ChunkRequests[1];

        AnimationSetScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
        AnimationSetScaffold(AnimationSetScaffold&& moveFrom) never_throws;
        AnimationSetScaffold& operator=(AnimationSetScaffold&& moveFrom) never_throws;
        AnimationSetScaffold();
        ~AnimationSetScaffold();

    private:
        std::unique_ptr<uint8[], PODAlignedDeletor>    _rawMemoryBlock;
		::Assets::DependencyValidation _depVal;
    };


}}

