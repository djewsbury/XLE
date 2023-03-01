// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/Streams/XmlFormatter.h"
#include "../Utility/Streams/StreamDOM.h"
#include <vector>

namespace ColladaConversion 
{
    using Formatter = XmlInputFormatter<utf8>;
    using Section = Formatter::InteriorSection;

    class DocScopeId
    {
    public:
        uint64 GetHash() const          { return _hash; }
        Section GetOriginal() const     { return _section; }
        bool IsEmpty() const            { return _section.IsEmpty(); }

        DocScopeId(Section section);
        DocScopeId() : _hash(0) {}
    protected:
        uint64 _hash;
        Section _section;
    };
}

namespace ColladaConversion { namespace DataFlow
{
    using Formatter = XmlInputFormatter<utf8>;
    using Section = Formatter::InteriorSection;
    
    /// <summary>Data type for a collada array</summary>
    /// Collada only supports a limited number of different types within
    /// "source" arrays. These store the most of the "big" information within
    /// Collada files (like vertex data, animation curves, etc).
    enum class ArrayType
    {
        Unspecified, Int, Float, Name, Bool, IdRef, SidRef
    };


    class Accessor
    {
    public:
        unsigned GetStride() const { return _stride; }
        unsigned GetOffset() const { return _offset; }
        unsigned GetCount() const { return _count; }

        class Param
        {
        public:
            Section _name;
            Section _type;
            unsigned _offset;
            Section _semantic;
            Param() : _offset(~unsigned(0)), _type() {}
        };

        size_t GetParamCount() const { return _paramCount; }
        const Param& GetParam(size_t index) const
        {
            if (index < dimof(_params)) return _params[index];
            return _paramsOverflow[index-dimof(_params)];
        }

        Accessor();
        Accessor(Formatter& formatter);
        Accessor(Accessor&& moveFrom) never_throws;
        Accessor& operator=(Accessor&&moveFrom) never_throws;
        ~Accessor();

    protected:
        Section _source;        // uri
        unsigned _count;
        unsigned _stride;
        unsigned _offset;

        Param _params[4];
        std::vector<Param> _paramsOverflow;
        size_t _paramCount;
    };



    class Source
    {
    public:
        Section GetArrayData() const    { return _arrayData; }
        size_t GetCount() const         { return _arrayCount; }
        ArrayType GetType() const       { return _type; }

        DocScopeId GetId() const        { return _id; }
        Section GetArrayId() const      { return _arrayId; }

        size_t GetAccessorCount() const { return _accessorsCount; }
        Section GetTechniqueForAccessor(size_t index) const 
        { 
            assert(index < _accessorsCount);
            if (index < dimof(_accessors))
                return _accessors[index].second;
            return _accessorsOverflow[index-dimof(_accessors)].second;
        }

        const Accessor& GetAccessor(size_t index) const 
        { 
            assert(index < _accessorsCount);
            if (index < dimof(_accessors))
                return _accessors[index].first;
            return _accessorsOverflow[index-dimof(_accessors)].first;
        }

        const Accessor* FindAccessorForTechnique(const utf8 techniqueProfile[] = "technique_common") const;

        const StreamLocation GetLocation() const { return _location; }

        Source() : _type(ArrayType::Unspecified), _arrayCount(0), _accessorsCount(0) {}
        Source(Formatter& formatter);
        Source(Source&& moveFrom) never_throws;
        Source& operator=(Source&& moveFrom) never_throws;
        ~Source();

    protected:
        void ParseTechnique(Formatter& formatter, Section techniqueProfile);

        DocScopeId _id;
        Section _arrayId;
        Section _arrayData;
        ArrayType _type;
        size_t _arrayCount;

            // accessors, and the technique profile that applies to them
        std::pair<Accessor, Section> _accessors[1];
        std::vector<std::pair<Accessor, Section>> _accessorsOverflow;
        unsigned _accessorsCount;

        StreamLocation _location;
    };


    class Input
    {
    public:
        Section _source;            // urifragment_type
        Section _semantic;
        unsigned _semanticIndex;
        unsigned _indexInPrimitive; // this is the index into the <p> or <v> in the parent

        Input();
        Input(Formatter& formatter);
    };


    class InputUnshared
    {
    public:
        Section _semantic;
        Section _source;        // urifragment_type

        InputUnshared();
        InputUnshared(Formatter& formatter);
    };

}}


