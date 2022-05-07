// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Format.h"
#include "../../Math/Vector.h"
#include "../../Math/Matrix.h"
#include "../../Math/Quaternion.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace Assets { class BlockSerializer; }

namespace RenderCore { namespace Assets
{
	namespace GeoProc { class NascentAnimationSet; void SerializationOperator(::Assets::BlockSerializer&, const NascentAnimationSet&); }
	class RawAnimationCurve;
	class AnimatedParameterSet;

	/// <summary>Represents the state of animation effects on an object<summary>
	/// AnimationState is a placeholder for containing the states related to
	/// animating vertices in a model.
	class AnimationState
	{
	public:
			// only a single animation supported currently //
		float		_time = 0.f;
		uint64_t	_animation = 0;
	};

	enum class AnimSamplerType { Float1, Float3, Float4, Float4x4, Quaternion };
	enum class AnimSamplerComponent { None, Translation, Rotation, Scale, FullTransform };
	const char* AsString(AnimSamplerType value);
	const char* AsString(AnimSamplerComponent value);

	#pragma pack(push)
	#pragma pack(1)

	class AnimationSet
	{
	public:
			/////   A N I M A T I O N   D R I V E R   /////
		struct AnimationDriver
		{
			unsigned			_curveIndex = ~0u;
			unsigned			_parameterIndex = ~0u;
			AnimSamplerType		_samplerType = (AnimSamplerType)~0u;
			unsigned			_samplerOffset = ~0u;

			static const bool SerializeRaw = true;
		};

			/////   C O N S T A N T   D R I V E R   /////
		struct ConstantDriver
		{
			unsigned			_dataOffset = ~0u;
			unsigned			_parameterIndex = ~0u;
			Format				_format = (Format)0;
			AnimSamplerType		_samplerType = (AnimSamplerType)~0u;
			unsigned			_samplerOffset = ~0u;

			static const bool SerializeRaw = true;
		};

		struct Animation
		{
			unsigned	_beginDriver, _endDriver;
			unsigned	_beginConstantDriver, _endConstantDriver;
			float		_beginTime, _endTime;

			static const bool SerializeRaw = true;
		};
		using AnimationAndName = std::pair<uint64_t, Animation>;

		struct ParameterBindingRules
		{
			unsigned _outputOffset = ~0u;
			AnimSamplerType _samplerType = AnimSamplerType::Float1;
		};

		void CalculateOutput(
			IteratorRange<void*> outputBlock,			// outputBlock should be pre-initialized with the defaults
			const AnimationState& animState,
			IteratorRange<const ParameterBindingRules*> parameterBindingRules) const;

		Animation				FindAnimation(uint64_t animation) const;
		unsigned				FindParameter(uint64_t parameterName, AnimSamplerComponent component) const;
		StringSection<>			LookupStringName(uint64_t animation) const;

		IteratorRange<const AnimationDriver*> GetAnimationDrivers() const { return MakeIteratorRange(_animationDrivers); }
		IteratorRange<const ConstantDriver*> GetConstantDrivers() const { return MakeIteratorRange(_constantDrivers); }
		IteratorRange<const AnimationAndName*> GetAnimations() const { return MakeIteratorRange(_animations); }
		IteratorRange<const void*> GetConstantData() const { return MakeIteratorRange(_constantData); }
		IteratorRange<const RawAnimationCurve*>	GetCurves() const { return MakeIteratorRange(_curves); }

		struct OutputPart
		{
			uint64_t _name;
			AnimSamplerComponent _component;
			AnimSamplerType _samplerType;
		};
		using OutputInterface = IteratorRange<const OutputPart*>;
		OutputInterface	GetOutputInterface() const { return MakeIteratorRange(_outputInterface); }

		AnimationSet();
		~AnimationSet();

		AnimationSet(const AnimationSet&) = delete;
		AnimationSet& operator=(const AnimationSet&) = delete;

		friend void SerializationOperator(::Assets::BlockSerializer& serializer, const AnimationSet& obj);
	protected:
		SerializableVector<AnimationDriver>		_animationDrivers;
		SerializableVector<ConstantDriver>		_constantDrivers;
		SerializableVector<uint8_t>				_constantData;
		SerializableVector<AnimationAndName>	_animations;
		SerializableVector<OutputPart>			_outputInterface;
		SerializableVector<RawAnimationCurve>	_curves;

		SerializableVector<unsigned>			_stringNameBlockOffsets;
		SerializableVector<char>				_stringNameBlock;

		friend class GeoProc::NascentAnimationSet;
		friend void GeoProc::SerializationOperator(::Assets::BlockSerializer&, const GeoProc::NascentAnimationSet&);
	};

	#pragma pack(pop)


			//////////////////////////////////////////////////////////

#if 0
	class AnimatedParameterSet
	{
	public:
		IteratorRange<const float*>     GetFloat1Parameters() const     { return MakeIteratorRange(_float1Parameters);      }
		IteratorRange<const Float3*>    GetFloat3Parameters() const     { return MakeIteratorRange(_float3Parameters);      }
		IteratorRange<const Float4*>    GetFloat4Parameters() const     { return MakeIteratorRange(_float4Parameters);      }
		IteratorRange<const Float4x4*>	GetFloat4x4Parameters() const	{ return MakeIteratorRange(_float4x4Parameters);	}

		IteratorRange<float*>			GetFloat1Parameters()			{ return MakeIteratorRange(_float1Parameters);      }
		IteratorRange<Float3*>			GetFloat3Parameters()			{ return MakeIteratorRange(_float3Parameters);      }
		IteratorRange<Float4*>			GetFloat4Parameters()			{ return MakeIteratorRange(_float4Parameters);      }
		IteratorRange<Float4x4*>		GetFloat4x4Parameters()			{ return MakeIteratorRange(_float4x4Parameters);	}

		void Set(uint32_t index, float);
		void Set(uint32_t index, Float3);
		void Set(uint32_t index, Float4);
		void Set(uint32_t index, Quaternion);
		void Set(uint32_t index, const Float4x4&);
			
		AnimatedParameterSet();
		AnimatedParameterSet(AnimatedParameterSet&& moveFrom);
		AnimatedParameterSet& operator=(AnimatedParameterSet&& moveFrom);
		AnimatedParameterSet(const AnimatedParameterSet& copyFrom);
		AnimatedParameterSet& operator=(const AnimatedParameterSet& copyFrom);

		void    SerializeMethod(::Assets::BlockSerializer& outputSerializer) const;

	private:
		SerializableVector<Float4x4>    _float4x4Parameters;
		SerializableVector<Float4>      _float4Parameters;
		SerializableVector<Float3>      _float3Parameters;
		SerializableVector<float>       _float1Parameters;
	};
#endif

}}

