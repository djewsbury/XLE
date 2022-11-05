// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <vector>
#include "../../../Utility/StringUtils.h"
#include "../../../Utility/IteratorUtils.h"
#include <iostream>

namespace RenderCore { class StreamOutputInitializers; }

namespace RenderCore { namespace Metal_Vulkan
{
    std::vector<uint32_t> PatchUpStreamOutput(
        IteratorRange<const void*> byteCode,
        const StreamOutputInitializers& soInit);
        
    class SPIRVReflection
    {
    public:
        using ObjectId = unsigned;
        using MemberId = std::pair<ObjectId, unsigned>;
        using Name = StringSection<>;
        
        //
        //      Names
        //
        std::vector<std::pair<ObjectId, Name>> _names;
        std::vector<std::pair<MemberId, Name>> _memberNames;

        //
        //      Bindings for uniforms and interface elements
        //
        struct Binding
        {
            unsigned _location;
            unsigned _bindingPoint;
            unsigned _descriptorSet;
            unsigned _offset;
            unsigned _inputAttachmentIndex;

            Binding(unsigned location = ~0x0u, unsigned bindingPoint= ~0x0u, unsigned descriptorSet= ~0x0u, unsigned offset = ~0x0, unsigned inputAttachmentIndex = ~0x0)
            : _location(location), _bindingPoint(bindingPoint), _descriptorSet(descriptorSet), _offset(offset), _inputAttachmentIndex(inputAttachmentIndex) {}
        };
        std::vector<std::pair<ObjectId, Binding>>	_bindings;
        std::vector<std::pair<MemberId, Binding>>	_memberBindings;
        std::vector<std::pair<uint64_t, Binding>>	_uniformQuickLookup;

        //
        //      Types
        //
        enum class BasicType { Int, Float, Bool, Image, Sampler, SampledImage };
        enum class StorageClass { UniformConstant, Input, Uniform, Output, Workgroup, CrossWorkgroup, Private, Function, Generic, PushConstant, AtomicCounter, Image, StorageBuffer, Unknown };
        struct VectorType { ObjectId _componentType; unsigned _componentCount; };
        struct PointerType { ObjectId _targetType; StorageClass _storage; };
        struct ArrayType { ObjectId _elementType; unsigned _elementCount; };

        enum class ResourceCategory { Image1D, Image2D, Image3D, ImageCube, Buffer, InputAttachment, Unknown };
        struct ResourceType
        { 
            ResourceCategory _category = ResourceCategory::Unknown;
            bool _arrayVariation = false;
            bool _multisampleVariation = false;
            bool _readWriteVariation = false;    // (eg, RWTexture2D and RWBuffer<> texel buffers)
        };

        std::vector<std::pair<ObjectId, BasicType>>     _basicTypes;
        std::vector<std::pair<ObjectId, VectorType>>    _vectorTypes;
        std::vector<std::pair<ObjectId, PointerType>>   _pointerTypes;
        std::vector<ObjectId>                           _structTypes;
        std::vector<ObjectId>                           _runtimeArrayStructTypes;
        std::vector<std::pair<ObjectId, ArrayType>>     _arrayTypes;
        std::vector<std::pair<ObjectId, ResourceType>>  _resourceTypes;

        struct Variable { ObjectId _type; StorageClass _storage; };
        std::vector<std::pair<ObjectId, Variable>>      _variables;

        std::vector<std::pair<ObjectId, unsigned>>      _integerConstants;

        //
        //      Interface (eg, vertex input)
        //
        struct EntryPoint
        {
            ObjectId _id;
            StringSection<> _name;
            std::vector<ObjectId> _interface;
        };
        EntryPoint _entryPoint;

        struct InputInterfaceElement
        {
            ObjectId _type;
            unsigned _location;
        };
        std::vector<std::pair<uint64_t, InputInterfaceElement>> _inputInterfaceQuickLookup;

		struct PushConstantsVariable
		{
			ObjectId _variable;		// maps into the "_variables" array
			ObjectId _type;
		};
		std::vector<std::pair<uint64_t, PushConstantsVariable>> _pushConstantsQuickLookup;

		std::ostream& DescribeVariable(std::ostream& str, ObjectId variable) const;
		StringSection<> GetName(ObjectId objectId) const;
        ObjectId DecayType(ObjectId type) const;

        SPIRVReflection(IteratorRange<const void*> byteCode);
        SPIRVReflection();
        ~SPIRVReflection();

        SPIRVReflection(const SPIRVReflection& cloneFrom) = default;
        SPIRVReflection& operator=(const SPIRVReflection& cloneFrom) = default;
        SPIRVReflection(SPIRVReflection&& moveFrom) never_throws = default;
        SPIRVReflection& operator=(SPIRVReflection&& moveFrom) never_throws = default;
    };

	std::ostream& operator<<(std::ostream& str, const SPIRVReflection& refl);
    std::ostream& DiassembleByteCode(std::ostream& str, IteratorRange<const void*> byteCode);
}}
