// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/IteratorUtils.h"
#include <string>

namespace RenderCore
{
	#define VS_DefShaderModel   "vs_*"
	#define GS_DefShaderModel   "gs_*"
	#define PS_DefShaderModel   "ps_*"
	#define CS_DefShaderModel   "cs_*"
	#define HS_DefShaderModel   "hs_*"
	#define DS_DefShaderModel   "ds_*"

    enum class Format;

///////////////////////////////////////////////////////////////////////////////////////////////////

    enum class InputDataRate
    {
        PerVertex, PerInstance
    };

	/// <summary>Vertex input element description</summary>
	/// This is used to bind vertex buffer data to the vertex shader inputs. The structure helps
	/// specify the position and offsets of certain vertex elements. Those elements are matched
	/// against the vertex shader inputs.
    class InputElementDesc
    {
    public:
        std::string     _semanticName;
        unsigned        _semanticIndex;
        Format          _nativeFormat;
        unsigned        _inputSlot;
        unsigned        _alignedByteOffset;
        InputDataRate	_inputSlotClass;
        unsigned        _instanceDataStepRate;

        InputElementDesc();
        InputElementDesc(   const std::string& name, unsigned semanticIndex, 
							Format nativeFormat, unsigned inputSlot = 0,
                            unsigned alignedByteOffset = ~unsigned(0x0), 
                            InputDataRate inputSlotClass = InputDataRate::PerVertex,
                            unsigned instanceDataStepRate = 0);
    };

	using InputLayout = IteratorRange<const InputElementDesc*>;

#pragma pack(push)
#pragma pack(1)
    class MiniInputElementDesc
    {
    public:
        uint64_t  _semanticHash;
        Format	_nativeFormat;
        
        static const bool SerializeRaw = true;
    } attribute_packed;
#pragma pack(pop)

    unsigned CalculateVertexStrideForSlot(IteratorRange<const InputElementDesc*> elements, unsigned slot);
	std::vector<unsigned> CalculateVertexStrides(IteratorRange<const InputElementDesc*> layout);
	std::vector<InputElementDesc> NormalizeInputAssembly(IteratorRange<const InputElementDesc*> layout);
    unsigned HasElement(IteratorRange<const InputElementDesc*> elements, const char elementSemantic[]);
    unsigned FindElement(IteratorRange<const InputElementDesc*> elements, const char elementSemantic[], unsigned semanticIndex = 0);

    bool HasElement(IteratorRange<const MiniInputElementDesc*> elements, uint64_t semanticHash);
	unsigned CalculateVertexStride(IteratorRange<const MiniInputElementDesc*> elements, bool enforceAlignment=true);
    bool RequiresAlignmentSpacing(IteratorRange<const InputElementDesc*> layout);
    unsigned VertexAttributeRequiredAlignment(Format fmt);

    uint64_t HashInputAssembly(IteratorRange<const InputElementDesc*>, uint64_t seed);
    uint64_t HashInputAssembly(IteratorRange<const MiniInputElementDesc*>, uint64_t seed);
   
    /// Contains some common reusable vertex input layouts
    namespace GlobalInputLayouts
    {
        extern InputLayout P;
        extern InputLayout PC;
        extern InputLayout P2C;
        extern InputLayout P2CT;
        extern InputLayout PCT;
        extern InputLayout PT;
        extern InputLayout PN;
        extern InputLayout PNT;
        extern InputLayout PNTT;
    }

    namespace GlobalMiniInputLayouts
    {
        extern IteratorRange<const MiniInputElementDesc*> P;
        extern IteratorRange<const MiniInputElementDesc*> PC;
        extern IteratorRange<const MiniInputElementDesc*> P2C;
        extern IteratorRange<const MiniInputElementDesc*> P2CT;
        extern IteratorRange<const MiniInputElementDesc*> PCT;
        extern IteratorRange<const MiniInputElementDesc*> PT;
        extern IteratorRange<const MiniInputElementDesc*> PN;
        extern IteratorRange<const MiniInputElementDesc*> PNT;
        extern IteratorRange<const MiniInputElementDesc*> PNTT;
    }
    
///////////////////////////////////////////////////////////////////////////////////////////////////

	class StreamOutputInitializers
	{
	public:
		IteratorRange<const RenderCore::InputElementDesc*> _outputElements;
		IteratorRange<const unsigned*> _outputBufferStrides;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

	enum class ShaderStage
    {
        Vertex, Pixel, Geometry, Hull, Domain, Compute,
        Null,
        Max
    };

	const char* AsString(ShaderStage);

	enum class PipelineType { Graphics, Compute };

///////////////////////////////////////////////////////////////////////////////////////////////////


    struct ClearFilter { enum Enum { Depth = 1<<0, Stencil = 1<<1 }; using BitField = unsigned; };


///////////////////////////////////////////////////////////////////////////////////////////////////

	inline InputElementDesc::InputElementDesc()
	{
		_semanticIndex = 0;
		_nativeFormat = (Format)0; _inputSlot = 0;
		_alignedByteOffset = ~0u; _inputSlotClass = InputDataRate::PerVertex;
		_instanceDataStepRate = 0;
	}
	inline InputElementDesc::InputElementDesc(const std::string& name, unsigned semanticIndex,
		Format nativeFormat, unsigned inputSlot,
		unsigned alignedByteOffset,
		InputDataRate inputSlotClass,
		unsigned instanceDataStepRate)
	{
		_semanticName = name; _semanticIndex = semanticIndex;
		_nativeFormat = nativeFormat; _inputSlot = inputSlot;
		_alignedByteOffset = alignedByteOffset; _inputSlotClass = inputSlotClass;
		_instanceDataStepRate = instanceDataStepRate;
	}

}

