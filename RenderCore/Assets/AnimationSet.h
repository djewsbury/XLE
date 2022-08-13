// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Format.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/Streams/SerializationUtils.h"

namespace Assets { class BlockSerializer; }

namespace RenderCore { namespace Assets
{
	namespace GeoProc { class NascentAnimationSet; void SerializationOperator(::Assets::BlockSerializer&, const NascentAnimationSet&); }
	class RawAnimationCurve;
	class AnimatedParameterSet;

	struct AnimationState
	{
		float		_time = 0.f;
		uint64_t	_animation = 0;
	};

	enum class AnimSamplerType : unsigned { Float1, Float3, Float4, Float4x4, Quaternion };
	enum class AnimSamplerComponent : unsigned { None, Translation, Rotation, Scale, FullTransform, TranslationGeoSpace };
	enum class CurveInterpolationType : unsigned; //  : unsigned { Linear, Bezier, Hermite, CatmullRom, NURBS };
	const char* AsString(AnimSamplerType value);
	const char* AsString(AnimSamplerComponent value);
	const char* AsString(CurveInterpolationType value);

	#pragma pack(push)
	#pragma pack(1)

	class AnimationSet
	{
	public:
		struct ParameterBindingRules
		{
			unsigned _outputOffset = ~0u;
			AnimSamplerType _samplerType = AnimSamplerType::Float1;
		};

		void CalculateOutput(
			IteratorRange<void*> outputBlock,			// outputBlock should be pre-initialized with the defaults
			const AnimationState& animState,
			IteratorRange<const ParameterBindingRules*> parameterBindingRules) const;

		struct AnimationQuery
		{
			unsigned _durationInFrames = 0;
			float _framesPerSecond = 0.f;
			StringSection<> _stringName;
		};
		std::optional<AnimationQuery>	FindAnimation(uint64_t animation) const;

			/////   S E M I - P R O T E C T E D   I N T E R F A C E   /////
		struct AnimationDriver
		{
			unsigned				_curveIndex = ~0u;
			unsigned				_parameterIndex = ~0u;
			CurveInterpolationType 	_interpolationType = CurveInterpolationType(0);

			static const bool SerializeRaw = true;
		};

		struct ConstantDriver
		{
			unsigned			_dataOffset = ~0u;
			unsigned			_parameterIndex = ~0u;
			Format				_format = (Format)0;

			static const bool SerializeRaw = true;
		};

		struct AnimationBlock
		{
			unsigned	_beginDriver, _endDriver;
			unsigned	_beginConstantDriver, _endConstantDriver;
			unsigned	_beginFrame, _endFrame;

			static const bool SerializeRaw = true;
		};

		struct Animation
		{
			unsigned 	_startBlock, _endBlock;
			float		_framesPerSecond;

			static const bool SerializeRaw = true;
		};
		using AnimationAndName = std::pair<uint64_t, Animation>;

		unsigned FindParameter(uint64_t parameterName, AnimSamplerComponent component) const;

		IteratorRange<const AnimationDriver*> GetAnimationDrivers() const { return MakeIteratorRange(_animationDrivers); }
		IteratorRange<const ConstantDriver*> GetConstantDrivers() const { return MakeIteratorRange(_constantDrivers); }
		IteratorRange<const AnimationBlock*> GetAnimationBlocks() const { return MakeIteratorRange(_animationBlocks); }
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
		SerializableVector<AnimationBlock>		_animationBlocks;
		SerializableVector<AnimationAndName>	_animations;
		SerializableVector<OutputPart>			_outputInterface;
		SerializableVector<RawAnimationCurve>	_curves;

		SerializableVector<unsigned>			_stringNameBlockOffsets;
		SerializableVector<char>				_stringNameBlock;

		friend class GeoProc::NascentAnimationSet;
		friend void GeoProc::SerializationOperator(::Assets::BlockSerializer&, const GeoProc::NascentAnimationSet&);
	};

	#pragma pack(pop)

}}

