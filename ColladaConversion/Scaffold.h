// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ScaffoldDataFlow.h"
#include "../Math/Vector.h"
#include "../Math/Matrix.h"
#include "../Utility/Streams/XmlStreamFormatter.h"
#include "../Utility/Streams/StreamDOM.h"
#include <vector>
#include <memory>
#include <functional>

namespace ColladaConversion
{
    using Formatter = XmlInputStreamFormatter<utf8>;
    using Section = Formatter::InteriorSection;
    using SubDoc = Utility::StreamDOM<Formatter>;
    using String = std::basic_string<Formatter::value_type>;

    class Effect;
    class MeshGeometry;
    class VisualScene;
    class SkinController;
    class Material;
    class VertexInputs;
    class DocumentScaffold;
    class Image;

    class AssetDesc
    {
    public:
        float _metersPerUnit;
        enum class UpAxis { X, Y, Z };
        UpAxis _upAxis;

        AssetDesc();
        AssetDesc(Formatter& formatter);
    };

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      E F F E C T S   A N D   M A T E R I A L S
///////////////////////////////////////////////////////////////////////////////////////////////////

    enum class SamplerAddress
    {
        Wrap, Mirror, Clamp, Border, MirrorOnce
    };

    enum class SamplerFilter { Point, Linear, Anisotropic };
    enum class SamplerDimensionality { T2D, T3D, Cube };
    
    class ParameterSet
    {
    public:
        class BasicParameter
        {
        public:
            Section _sid;
            Section _type;
            Section _value;
        };

        class SamplerParameter
        {
        public:
            Section _sid;
            Section _type;
            Section _instanceImage;     // (collada 1.5)
            Section _source;            // (collada 1.4.1)
            SamplerDimensionality _dimensionality;
            SamplerAddress _addressS, _addressT, _addressQ;
            SamplerFilter _minFilter, _maxFilter, _mipFilter;
            Float4 _borderColor;
            unsigned _minMipLevel, _maxMipLevel;
            float _mipMapBias;
            unsigned _maxAnisotropy;
            SubDoc _extra;

            SamplerParameter();
            SamplerParameter(Formatter& formatter, Section sid, Section eleName);
            ~SamplerParameter();

			#if defined(COMPILER_DEFAULT_IMPLICIT_OPERATORS)
				SamplerParameter(SamplerParameter&&) = default;
				SamplerParameter& operator=(SamplerParameter&&) = default;
			#endif
        };

        class SurfaceParameter
        {
        public:
            Section _sid;
            Section _type;
            Section _initFrom;

            SurfaceParameter() {}
            SurfaceParameter(Formatter& formatter, Section sid, Section eleName);
        };

        unsigned GetBasicParameterCount() const     { return (unsigned)_parameters.size(); }
        unsigned GetSamplerParameterCount() const   { return (unsigned)_samplerParameters.size(); }
        unsigned GetSurfaceParameterCount() const   { return (unsigned)_surfaceParameters.size(); }

        const BasicParameter& GetBasicParameter(unsigned index) const       { return _parameters[index]; }
        const SamplerParameter& GetSamplerParameter(unsigned index) const   { return _samplerParameters[index]; }
        const SurfaceParameter& GetSurfaceParameter(unsigned index) const   { return _surfaceParameters[index]; }

        void ParseParam(Formatter& formatter);
        
        ParameterSet();
        ~ParameterSet();
        ParameterSet(ParameterSet&& moveFrom) never_throws;
        ParameterSet& operator=(ParameterSet&&) never_throws;

    protected:
        std::vector<BasicParameter> _parameters;
        std::vector<SamplerParameter> _samplerParameters;
        std::vector<SurfaceParameter> _surfaceParameters;
    };

    class TechniqueValue
    {
    public:
        enum class Type { Color, Texture, Float, Param, None };
        Type _type;
        Section _reference; // texture or parameter reference
        Section _texCoord;
        Float4 _value;

        TechniqueValue(Formatter& formatter);
    };

    class Effect
    {
    public:
        Section GetName() const { return _name; }
        DocScopeId GetId() const { return _id; }
        const ParameterSet& GetParams() const { return _params; }

        class Profile
        {
        public:
            ParameterSet _params;
            String _profileType;
            String _shaderName;        // (phong, blinn, etc)
            std::vector<std::pair<Section, TechniqueValue>> _values;

            SubDoc _extra;
            SubDoc _techniqueExtra;
            Section _techniqueSid;

            const ParameterSet& GetParams() const { return _params; }

            Profile(Formatter& formatter, String profileType);
            Profile(Profile&& moveFrom) never_throws;
            Profile& operator=(Profile&& moveFrom) never_throws;

        protected:
            void ParseTechnique(Formatter& formatter);
            void ParseShaderType(Formatter& formatter);

        };

        unsigned GetProfileCount() const { return (unsigned)_profiles.size(); }
        const Profile& GetProfile(unsigned index) const { return _profiles[index]; }
        const Profile* FindProfile(const utf8 name[]) const;

        SubDoc _extra;

        Effect(Formatter& formatter);
        Effect(Effect&& moveFrom) never_throws;
        Effect& operator=(Effect&& moveFrom) never_throws;

    protected:
        Section _name;
        DocScopeId _id;
        ParameterSet _params;

        std::vector<Profile> _profiles;
    };

    class Material
    {
    public:
        DocScopeId _id;
        Section _name;
        Section _effectReference;   // uri
        SubDoc _extra;

        const DocScopeId& GetId() const { return _id; }

        Material() {}
        Material(Formatter& formatter);

    protected:
        void ParseInstanceEffect(Formatter& formatter);
    };

    class Image
    {
    public:
        SubDoc _extra;

        const DocScopeId& GetId() const { return _id; }
        Section GetName() const         { return _name; }
        Section GetInitFrom() const     { return _initFrom; }

        Image();
        Image(Formatter& formatter);
        Image(Image&& moveFrom) never_throws;
        Image& operator=(Image&& moveFrom) never_throws;
    protected:
        DocScopeId _id;
        Section _name;
        Section _initFrom;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      G E O M E T R Y
///////////////////////////////////////////////////////////////////////////////////////////////////

    class GeometryPrimitives
    {
    public:
        size_t GetInputCount() const { return _inputCount; }
        const DataFlow::Input& GetInput(size_t index) const
        {
            assert(index < _inputCount);
            if (index < dimof(_inputs)) return _inputs[index];
            return _inputsOverflow[index - dimof(_inputs)];
        }

        size_t GetPrimitiveDataCount() const { return _primitiveDataCount; }
        Section GetPrimitiveData(size_t index) const
        {
            assert(index < _primitiveDataCount);
            if (index < dimof(_primitiveData)) return _primitiveData[index];
            return _primitiveDataOverflow[index - dimof(_primitiveData)];
        }

        Section GetType() const { return _type; }
        Section GetVCountArray() const { return _vcount; }
        Section GetMaterialBinding() const { return _materialBinding; }
        unsigned GetPrimitiveCount() const { return _primitiveCount; }

        const StreamLocation& GetLocation() const { return _location; }

        GeometryPrimitives(Formatter& formatter, Section type);
        GeometryPrimitives();
        GeometryPrimitives(GeometryPrimitives&& moveFrom) never_throws;
        GeometryPrimitives& operator=(GeometryPrimitives&& moveFrom) never_throws;

    protected:
        Section _type;

        DataFlow::Input _inputs[6];
        std::vector<DataFlow::Input> _inputsOverflow;
        unsigned _inputCount;

            // in most cases, there is only one "_primitiveData" element
            // but for trianglestrip, there may be multiple
        Section _primitiveData[1];
        std::vector<Section> _primitiveDataOverflow;
        unsigned _primitiveDataCount;

        Section _vcount;
        Section _materialBinding;
        unsigned _primitiveCount;
        StreamLocation _location;
    };

    class MeshGeometry
    {
    public:
        size_t GetPrimitivesCount() const { return _geoPrimitives.size(); }
        auto GetPrimitives(size_t index) const -> const GeometryPrimitives& { return _geoPrimitives[index]; }

        const DocScopeId& GetId() const { return _id; }
        Section GetName() const { return _name; }

        MeshGeometry(Formatter& formatter, DocumentScaffold& pub);

        MeshGeometry();
        MeshGeometry(MeshGeometry&& moveFrom) never_throws;
        MeshGeometry& operator=(MeshGeometry&& moveFrom) never_throws;

    protected:
        void ParseMesh(Formatter& formatter, DocumentScaffold& pub);

        std::vector<GeometryPrimitives> _geoPrimitives;
        SubDoc _extra;

        DocScopeId _id;
        Section _name;
    };

    class InputsCollection
    {
    public:
        size_t GetCount() const { return _vertexInputs.size(); }
        auto GetInput(size_t index) const -> const DataFlow::InputUnshared& { return _vertexInputs[index]; }
        auto FindInputBySemantic(const utf8 semantic[]) const -> const DataFlow::InputUnshared*;

        const DocScopeId& GetId() const { return _id; }

        void Add(const DataFlow::InputUnshared& newItem) { _vertexInputs.push_back(newItem); }

        InputsCollection(Formatter& formatter);
        InputsCollection();
        InputsCollection(InputsCollection&& moveFrom) never_throws;
        InputsCollection& operator=(InputsCollection&& moveFrom) never_throws;
    protected:
        std::vector<DataFlow::InputUnshared> _vertexInputs;
        DocScopeId _id;
    };

    class SkinController
    {
    public:
        const DocScopeId& GetId() const                 { return _id; }
        Section GetName() const                         { return _name; }
        Section GetBaseMesh() const                     { return _baseMesh; }
        Section GetBindShapeMatrix() const              { return _bindShapeMatrix; }
        StreamLocation GetLocation() const              { return _location; }
        unsigned GetVerticesWithWeightsCount() const    { return _verticesWithWeightsCount; }

        const InputsCollection& GetJointInputs() const { return _jointInputs; }

        size_t GetInfluenceInputCount() const { return _influenceInputs.size(); }
        const DataFlow::Input& GetInfluenceInput(unsigned index) const { return _influenceInputs[index]; }
        const DataFlow::Input* GetInfluenceInputBySemantic(const utf8 semantic[]) const;

        Section GetInfluenceCountPerVertexArray() const { return _influenceCountPerVertex; }
        Section GetInfluencesArray() const              { return _influences; }

        SkinController(Formatter& formatter, Section id, Section name, DocumentScaffold& pub);
        SkinController(SkinController&& moveFrom) never_throws;
        SkinController& operator=(SkinController&& moveFrom) never_throws;
        SkinController();
        ~SkinController();

    protected:
        void ParseVertexWeights(Formatter& formatter);

        Section _baseMesh;
        DocScopeId _id;
        Section _name;
        SubDoc _extra;

        Section _bindShapeMatrix;
        unsigned _verticesWithWeightsCount;
        Section _influenceCountPerVertex;   // (this the <vcount> element)
        Section _influences;                // (this is the <v> element)
        std::vector<DataFlow::Input> _influenceInputs;

        StreamLocation _location;

        // std::vector<DataFlow::Source> _sources;
        InputsCollection _jointInputs;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      T R A N S F O R M A T I O N S
///////////////////////////////////////////////////////////////////////////////////////////////////

    class Transformation;

    class TransformationSet
    {
    public:
        enum class Type 
        {
            None,
            LookAt, Matrix4x4, Rotate, 
            Scale, Skew, Translate 
        };

        static bool IsTransform(Section section);
        unsigned ParseTransform(
            Formatter& formatter, Section elementName, 
            unsigned previousSibling = ~unsigned(0));

        Transformation Get(unsigned index) const;

        TransformationSet();
        TransformationSet(TransformationSet&& moveFrom) never_throws;
        TransformationSet& operator=(TransformationSet&& moveFrom) never_throws;
        ~TransformationSet();

    protected:
        class RawOperation;
        std::vector<RawOperation> _operations;

        friend class Transformation;
    };

    class Transformation
    {
    public:
        TransformationSet::Type GetType() const;
        Transformation GetNext() const;
        const void* GetUnionData() const;

        Section GetSid() const;

        operator bool() const;
        bool operator!() const;

    protected:
        unsigned _index;
        const TransformationSet* _set;

        Transformation(const TransformationSet& set, unsigned index);
        friend class TransformationSet;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      I N S T A N T I A T E
///////////////////////////////////////////////////////////////////////////////////////////////////

    class InstanceGeometry
    {
    public:
        class MaterialBinding
        {
        public:
            Section _technique;
            Section _reference;
            Section _bindingSymbol;

            MaterialBinding(Formatter& formatter, Section technique);
        };

        Section _reference;
        std::vector<MaterialBinding> _matBindings;

        InstanceGeometry(Formatter& formatter);
        InstanceGeometry();
        InstanceGeometry(InstanceGeometry&& moveFrom) never_throws;
        InstanceGeometry& operator=(InstanceGeometry&& moveFrom) never_throws;

    protected:
        void ParseBindMaterial(Formatter& formatter);
        void ParseTechnique(Formatter& formatter, Section techniqueProfile);
    };

    class InstanceController : public InstanceGeometry
    {
    public:
        const Section& GetSkeleton() const { return _skeleton; }

        InstanceController(Formatter& formatter);
        InstanceController(InstanceController&& moveFrom) never_throws;
        InstanceController& operator=(InstanceController&& moveFrom) never_throws;

    protected:
        Section _skeleton;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      V I S U A L   S C E N E   S E C T I O N
///////////////////////////////////////////////////////////////////////////////////////////////////

    class Node;
    class InstanceGeometry;
    class InstanceController;

    class VisualScene
    {
    public:
        SubDoc _extra;

        Node GetRootNode() const;

        const InstanceGeometry& GetInstanceGeometry(unsigned index) const;
        Node GetInstanceGeometry_Attach(unsigned index) const;
        unsigned GetInstanceGeometryCount() const;

        const InstanceController& GetInstanceController(unsigned index) const;
        Node GetInstanceController_Attach(unsigned index) const;
        unsigned GetInstanceControllerCount() const;

        const DocScopeId& GetId() const     { return _id; }
        Section GetName() const             { return _name; }

        VisualScene(Formatter& formatter);
        VisualScene();
        VisualScene(VisualScene&& moveFrom) never_throws;
        VisualScene& operator=(VisualScene&& moveFrom) never_throws;

    protected:
        using IndexIntoNodes = unsigned;
        using TransformationSetIndex = unsigned;
        static const IndexIntoNodes IndexIntoNodes_Invalid = ~IndexIntoNodes(0);
        static const TransformationSetIndex TransformationSetIndex_Invalid = ~TransformationSetIndex(0);

        class RawNode;
        std::vector<RawNode> _nodes;
        std::vector<std::pair<IndexIntoNodes, InstanceGeometry>> _geoInstances;
        std::vector<std::pair<IndexIntoNodes, InstanceController>> _controllerInstances;
        TransformationSet _transformSet;

        DocScopeId _id;
        Section _name;

        friend class Node;
    };

    class Node
    {
    public:
            // ------------ Hierarchy --------------
        Node                GetNextSibling() const;
        Node                GetFirstChild() const;
        Node                GetParent() const;

            // ------------ Transforms --------------
        Transformation      GetFirstTransform() const;

            // ------------ ID --------------
        Section             GetName() const;
        const DocScopeId&   GetId() const;
        const DocScopeId&	GetSid() const;
        VisualScene::IndexIntoNodes GetIndex() const    { return _index; }

        const VisualScene& GetScene() const             { return *_scene; }

            // ------------ Utilities --------------
        Node FindBreadthFirst(std::function<bool(const Node&)>&& predicate) const;
        std::vector<Node> FindAllBreadthFirst(std::function<bool(const Node&)>&& predicate) const;
        
        operator bool() const;
        bool operator!() const;

        friend bool operator==(const Node& lhs, const Node& rhs)
        {
            return (lhs._index == rhs._index) && (lhs._scene == rhs._scene);
        }

        Node();
        Node(nullptr_t);
    protected:
        VisualScene::IndexIntoNodes _index;
        const VisualScene* _scene;

        Node(const VisualScene& scene, VisualScene::IndexIntoNodes index);
        friend class VisualScene;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      A N I M A T I O N   S E C T I O N
///////////////////////////////////////////////////////////////////////////////////////////////////

    class Channel
    {
    public:
        Section GetSource() const { return _source; }
        Section GetTarget() const { return _target; }

        Channel(Formatter& formatter);
        Channel();

    protected:
        Section _source;    // urifragment_type
        Section _target;    // sidref_type
    };

    class Sampler
    {
    public:
        enum class Behaviour
        {
            Unspecified,
            Constant,
            Gradient,
            Cycle,
            Oscillate,
            CycleRelative
        };

        DocScopeId GetId() const { return _id; }
        Behaviour GetPrebehaviour() const { return _prebehaviour; }
        Behaviour GetPostbehaviour() const { return _postbehaviour; }
        const InputsCollection& GetInputsCollection() const { return _inputs; }

        Sampler(Formatter& formatter);
        Sampler();
        Sampler(Sampler&& moveFrom) never_throws;
        Sampler& operator=(Sampler&& moveFrom) never_throws;

    protected:
        DocScopeId _id;
        Behaviour _prebehaviour;
        Behaviour _postbehaviour;

        InputsCollection _inputs;
    };

    class Animation
    {
    public:
        unsigned GetChannelCount() const { return (unsigned)_channels.size(); }
        const Channel& GetChannel(unsigned index) const { return _channels[index]; }

        Animation(Formatter& formatter, DocumentScaffold& pub);
        Animation();
        Animation(Animation&& moveFrom) never_throws;
        Animation& operator=(Animation&& moveFrom) never_throws;
    protected:
        std::vector<Channel> _channels;
        std::vector<Animation> _subAnimations;

        DocScopeId _id;
        Section _name;
        SubDoc _extra;
    };

///////////////////////////////////////////////////////////////////////////////////////////////////
        //      D O C U M E N T   R E L A T E D
///////////////////////////////////////////////////////////////////////////////////////////////////

    class IDocScopeIdResolver
    {
    public:
        virtual const DataFlow::Source* FindSource(uint64 guid) const = 0;
        virtual const InputsCollection* FindVertexInputs(uint64 guid) const = 0;
        virtual const MeshGeometry*     FindMeshGeometry(uint64 guid) const = 0;
        virtual const Material*         FindMaterial(uint64 guid) const = 0;
        virtual const VisualScene*      FindVisualScene(uint64 guid) const = 0;
        virtual const Image*            FindImage(uint64 guid) const = 0;
        virtual const SkinController*   FindSkinController(uint64 guid) const = 0;
        virtual Node                    FindNode(uint64 guid) const = 0;
		virtual Node                    FindNodeBySid(uint64 guid) const = 0;
        virtual const Sampler*          FindSampler(uint64 guid) const = 0;
        virtual ~IDocScopeIdResolver();
    };

    class GuidReference
    {
    public:
        uint64 _id;
        uint64 _fileHash;

        GuidReference(Section uri);
        GuidReference(uint64 id, uint64 fileHash) : _id(id), _fileHash(fileHash) {}

        friend bool operator==(const GuidReference& lhs, const GuidReference& rhs)
        {
            return (lhs._id == rhs._id) && (lhs._fileHash == rhs._fileHash);
        }
    };

    class URIResolveContext
    {
    public:
        const IDocScopeIdResolver* FindFile(uint64) const;

        URIResolveContext(std::shared_ptr<IDocScopeIdResolver> localDoc);
        URIResolveContext();
        ~URIResolveContext();
    protected:
        std::vector<std::pair<uint64, std::shared_ptr<IDocScopeIdResolver>>> _files;
    };

    class DocumentScaffold : public IDocScopeIdResolver
    {
    public:
        void Parse(Formatter& formatter);

        void Parse_LibraryEffects(Formatter& formatter);
        void Parse_LibraryGeometries(Formatter& formatter);
        void Parse_LibraryVisualScenes(Formatter& formatter);
        void Parse_LibraryControllers(Formatter& formatter);
        void Parse_LibraryMaterials(Formatter& formatter);
        void Parse_LibraryImages(Formatter& formatter);
        void Parse_LibraryAnimations(Formatter& formatter);
        void Parse_Scene(Formatter& formatter);

        DocumentScaffold();
        ~DocumentScaffold();

        void Add(DataFlow::Source&& element);
        void Add(InputsCollection&& vertexInputs);
        void Add(Sampler&& sampler);

        const DataFlow::Source* FindSource(uint64 guid) const;
        const InputsCollection* FindVertexInputs(uint64 guid) const;
        const MeshGeometry*     FindMeshGeometry(uint64 guid) const;
        const Material*         FindMaterial(uint64 guid) const;
        const VisualScene*      FindVisualScene(uint64 guid) const;
        const Image*            FindImage(uint64 guid) const;
        const SkinController*   FindSkinController(uint64 guid) const;
        Node                    FindNode(uint64 guid) const;
		Node                    FindNodeBySid(uint64 guid) const;
        const Sampler*          FindSampler(uint64 guid) const;

        Section GetMainVisualScene() const { return _visualScene; }
        const AssetDesc& GetAssetDesc() const { return _rootAsset; }

    // protected:
        AssetDesc _rootAsset;

        std::vector<Effect> _effects;
        std::vector<MeshGeometry> _geometries;
        std::vector<VisualScene> _visualScenes;
        std::vector<SkinController> _skinControllers;
        std::vector<Material> _materials;
        std::vector<Image> _images;
        std::vector<Animation> _animations;

        Section _visualScene;
        Section _physicsScene;
        Section _kinematicsScene;

        std::vector<std::pair<uint64, DataFlow::Source>> _sources;
        std::vector<std::pair<uint64, InputsCollection>> _vertexInputs;
        std::vector<std::pair<uint64, Sampler>> _samplers;
    };
    

    template<typename Element>
        Element FindElement(
            const GuidReference& ref,
            const URIResolveContext& resolveContext,
            Element (IDocScopeIdResolver::*fn)(uint64) const)
    {
        auto* file = resolveContext.FindFile(ref._fileHash);
        if (!file) return nullptr;
        return (file->*fn)(ref._id);
    }

}

