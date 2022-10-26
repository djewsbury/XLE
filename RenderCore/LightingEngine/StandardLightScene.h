// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ILightScene.h"
#include "StandardLightOperators.h"
#include "../../Math/Transformations.h"
#include "../../Math/ProjectionMath.h"
#include "../../Utility/BitUtils.h"

namespace RenderCore { namespace Techniques { class ParsingContext; }}
namespace RenderCore { namespace LightingEngine { class ShadowProbes; }}

namespace RenderCore { namespace LightingEngine { namespace Internal
{
	class ILightBase
	{
	public:
		virtual void* QueryInterface(uint64_t interfaceTypeCode) = 0;
		virtual ~ILightBase();
	};

	class ILightSceneComponent
	{
	public:
		virtual void RegisterLight(unsigned setIdx, unsigned lightIdx, ILightBase& light) = 0;
		virtual void DeregisterLight(unsigned setIdx, unsigned lightIdx) = 0;
		virtual bool BindToSet(ILightScene::LightOperatorId, ILightScene::ShadowOperatorId, unsigned setIdx) = 0;
		virtual void* QueryInterface(unsigned setIdx, unsigned lightIdx, uint64_t interfaceTypeCode) = 0;
		virtual ~ILightSceneComponent();
	};

	template<typename T>
		class PageHeap
	{
	public:
		struct Page
		{
			uint8_t _data[sizeof(T)*64];
		};
		std::vector<std::unique_ptr<Page>> _pages;
		BitHeap _allocationFlags;

		using Indexor = unsigned;
		struct Iterator;

		template<typename... Params>
			Iterator Allocate(Params&&...);
		template<typename... Params>
			Iterator AllocateAtIndex(Indexor index, Params&&...);
		void Deallocate(Indexor);
		Iterator Get(Indexor);
		T& GetObject(Indexor);
		const T& GetObject(Indexor) const;

		Iterator begin();
		Iterator end();
		Iterator at(Indexor);
		bool empty() const;

		PageHeap();
		~PageHeap();
		PageHeap(PageHeap&&);
		PageHeap& operator=(PageHeap&&);
	};

	template<typename T>
		struct PageHeap<T>::Iterator
	{
		Indexor _idxOffset = ~0u;
		uint64_t _q = 0;
		IteratorRange<const uint64_t*> _allocationFlags;
		PageHeap<T>* _owner = nullptr;

		Indexor GetIndex() const;
		T& get();
		const T& get() const;
		Iterator& operator++();

		T& operator*();
		T* operator->();
		const T& operator*() const;
		const T* operator->() const;

		friend bool operator==(const typename PageHeap<T>::Iterator& lhs, const typename PageHeap<T>::Iterator& rhs)
		{
			assert(lhs._owner == rhs._owner);
			return lhs._q == rhs._q && lhs._idxOffset == rhs._idxOffset && lhs._allocationFlags == rhs._allocationFlags;
		}
		friend bool operator!=(const typename PageHeap<T>::Iterator& lhs, const typename PageHeap<T>::Iterator& rhs)
		{
			assert(lhs._owner == rhs._owner);
			return lhs._q != rhs._q || lhs._idxOffset != rhs._idxOffset || lhs._allocationFlags != rhs._allocationFlags;
		}
	};

	class StandardPositionalLight;

	struct StandardPositionLightFlags
	{
		enum Enum { SupportFiniteRange = 1<<0 };
		using BitField = unsigned;
	};

	class StandardLightScene : public ILightScene
	{
	public:
		struct LightSet
		{
			LightOperatorId _operatorId = ~0u;
			ShadowOperatorId _shadowOperatorId = ~0u;
			PageHeap<StandardPositionalLight> _baseData;
			std::vector<std::shared_ptr<ILightSceneComponent>> _boundComponents;
			StandardPositionLightFlags::BitField _flags = 0;
		};
		std::vector<LightSet> _tileableLightSets;

		struct LightSetAndIndex { unsigned _lightSet, _lightIndex; };
		std::vector<std::pair<LightSourceId, LightSetAndIndex>> _lookupTable;

		LightSourceId _nextLightSource = 0;

		std::vector<std::shared_ptr<ILightSceneComponent>> _components;

		void RegisterComponent(std::shared_ptr<ILightSceneComponent>);
		void DeregisterComponent(ILightSceneComponent&);
		void AssociateFlag(LightOperatorId, StandardPositionLightFlags::BitField);

		virtual LightSourceId CreateLightSource(LightOperatorId operatorId) override;
		virtual void* TryGetLightSourceInterface(LightSourceId, uint64_t interfaceTypeCode) override;
		virtual void DestroyLightSource(LightSourceId) override;
		virtual void SetShadowOperator(LightSourceId, ShadowOperatorId) override;
		virtual void Clear() override;
		virtual void* QueryInterface(uint64_t) override;
		StandardLightScene();
		~StandardLightScene();

	protected:

		// move the given lights into a set with the given shadow operator assignment (but do nothing else)
		void ChangeLightsShadowOperator(
			IteratorRange<const LightSourceId*> lights,
			ShadowOperatorId shadowOperatorId);

		void ReserveLightSourceIds(unsigned idCount); 
		unsigned GetLightSet(LightOperatorId, ShadowOperatorId);

		void AddToLookupTable(LightSourceId, LightSetAndIndex);
		void ChangeLightSet(
			std::vector<std::pair<LightSourceId, LightSetAndIndex>>::iterator i,
			unsigned newSetIdx);

		std::vector<std::pair<LightOperatorId, StandardPositionLightFlags::BitField>> _associatedFlags;
	};

	class StandardPositionalLight : public ILightBase, public IPositionalLightSource, public IUniformEmittance, public IFiniteLightSource
	{
	public:
		Float3x3    _orientation;
		Float3      _position;
		Float2      _radii;

		float       _cutoffRange;
		Float3      _brightness;
		float       _diffuseWideningMin;
		float       _diffuseWideningMax;

 		virtual void SetLocalToWorld(const Float4x4& localToWorld) override
		{
			ScaleRotationTranslationM srt(localToWorld);
			_orientation = srt._rotation;
			_position = srt._translation;
			_radii = Truncate(srt._scale); 
		}

		virtual Float4x4 GetLocalToWorld() const override
		{
			ScaleRotationTranslationM srt { Expand(_radii, 1.f), _orientation, _position };
			return AsFloat4x4(srt);
		}

		virtual void SetCutoffRange(float cutoff) override { _cutoffRange = cutoff; }
		virtual float GetCutoffRange() const override { return _cutoffRange; }

		virtual void SetCutoffBrightness(float cutoffBrightness) override
		{
			// distance attenuation formula:
			//		1.0f / (distanceSq+1)
			// 
			// brightness / (distanceSq+1) = cutoffBrightness
			// (distanceSq+1) / brightness = 1.0f / cutoffBrightness
			// distanceSq = brightness / cutoffBrightness - 1
			float brightness = std::max(std::max(_brightness[0], _brightness[1]), _brightness[2]);
			if (cutoffBrightness < brightness) {
				SetCutoffRange(std::sqrt(brightness / cutoffBrightness - 1.0f));
			} else {
				// The light can't actually get as bright as the cutoff brightness.. just set to a small value
				SetCutoffRange(1e-3f);
			}
		}

		virtual void SetBrightness(Float3 rgb) override { _brightness = rgb; }
		virtual Float3 GetBrightness() const override { return _brightness; }
		virtual void SetDiffuseWideningFactors(Float2 minAndMax) override
		{
			_diffuseWideningMin = minAndMax[0];
			_diffuseWideningMax = minAndMax[1];
		}
		virtual Float2 GetDiffuseWideningFactors() const override
		{
			return Float2 { _diffuseWideningMin, _diffuseWideningMax };
		}
	
		virtual void* QueryInterface(uint64_t interfaceTypeCode) override;
		void* QueryInterface(uint64_t interfaceTypeCode, StandardPositionLightFlags::BitField flags);

		StandardPositionalLight()
		{
			_position = Normalize(Float3(-.1f, 0.33f, 1.f));
			_orientation = Identity<Float3x3>();
			_cutoffRange = 10000.f;
			_radii = Float2(1.f, 1.f);
			_brightness = Float3(1.f, 1.f, 1.f);

			_diffuseWideningMin = 0.5f;
			_diffuseWideningMax = 2.5f;
		}
	};

	template<typename T>
		template<typename... Params>
			auto PageHeap<T>::Allocate(Params&&... p) -> Iterator
	{
		auto index = _allocationFlags.Allocate();
		unsigned pageIdx = index >> 6;
		unsigned idxWithinPage = index & 0x3f;
		while (pageIdx >= _pages.size()) _pages.push_back(std::make_unique<Page>());
		auto& page = *_pages[pageIdx];
		auto* data = &page._data[idxWithinPage*sizeof(T)];
		#pragma push_macro("new")
		#undef new
		new (data) T(std::forward<Params>(p)...);
		#pragma pop_macro("new")
		return Get(index);
	}

	template<typename T>
		template<typename... Params>
			auto PageHeap<T>::AllocateAtIndex(Indexor index, Params&&...) -> Iterator
	{
		assert(!_allocationFlags.IsAllocated(index));
		_allocationFlags.Allocate(index);
		unsigned pageIdx = index >> 6;
		unsigned idxWithinPage = index & 0x3f;
		while (pageIdx >= _pages.size()) _pages.push_back(std::make_unique<Page>());
		auto& page = *_pages[pageIdx];
		auto* data = &page._data[idxWithinPage*sizeof(T)];
		new (data) T(std::forward<Params>(p)...);
		return Get(index);	
	}

	template<typename T>
		void PageHeap<T>::Deallocate(Indexor index)
	{
		assert(_allocationFlags.IsAllocated(index));
		_allocationFlags.Deallocate(index);
		unsigned pageIdx = index >> 6;
		unsigned idxWithinPage = index & 0x3f;
		assert(pageIdx < _pages.size());
		auto& page = *_pages[pageIdx];
		auto* data = &page._data[idxWithinPage*sizeof(T)];
		((T*)data)->~T();
	}

	template<typename T>
		auto PageHeap<T>::Get(Indexor index) -> Iterator
	{
		assert(_allocationFlags.IsAllocated(index));
		unsigned pageIdx = index >> 6;
		unsigned idxWithinPage = index & 0x3f;
		assert(pageIdx < _pages.size());
		auto allocationFlagsArray = _allocationFlags.InternalArray();
		assert(!allocationFlagsArray.empty());
		auto q = allocationFlagsArray[0];
		q |= (1ull << uint64_t(idxWithinPage)) - 1ull;	// clear all earlier entries from this "q"
		q = ~q;
		allocationFlagsArray.first += pageIdx+1;
		return { pageIdx * 64, q, allocationFlagsArray, this };
	}

	template<typename T>
		T& PageHeap<T>::GetObject(Indexor index)
	{
		assert(_allocationFlags.IsAllocated(index));
		unsigned pageIdx = index >> 6;
		unsigned idxWithinPage = index & 0x3f;
		return *(T*)&_pages[pageIdx]->_data[sizeof(T)*idxWithinPage];
	}

	template<typename T>
		const T& PageHeap<T>::GetObject(Indexor index) const
	{
		assert(_allocationFlags.IsAllocated(index));
		unsigned pageIdx = index >> 6;
		unsigned idxWithinPage = index & 0x3f;
		return *(const T*)&_pages[pageIdx]->_data[sizeof(T)*idxWithinPage];
	}

	template<typename T>
		auto PageHeap<T>::begin() -> Iterator
	{
		auto allocationFlagsArray = _allocationFlags.InternalArray();
		if (!allocationFlagsArray.empty()) {
			unsigned idxOffset = 0;
			uint64_t q;
			while (!allocationFlagsArray.empty()) {
				q = ~allocationFlagsArray[0];
				allocationFlagsArray.first++;
				if (q) break;
			}
			if (!allocationFlagsArray.empty())
				return { 0, q, allocationFlagsArray, this };
		}
			
		return { ~0u, 0, {}, this };
	}

	template<typename T>
		auto PageHeap<T>::end() -> Iterator
	{
		return { ~0u, 0, {}, this };
	}

	template<typename T>
		bool PageHeap<T>::empty() const
	{
		return _allocationFlags.AllocatedCount() == 0;
	}

	template<typename T> auto PageHeap<T>::at(Indexor index) -> Iterator { return Get(index); }

	template<typename T>
		PageHeap<T>::PageHeap() = default;
	template<typename T>
		PageHeap<T>::~PageHeap() = default;
	template<typename T>
		PageHeap<T>::PageHeap(PageHeap&&) = default;
	template<typename T>
		PageHeap<T>& PageHeap<T>::operator=(PageHeap&&) = default;

	template<typename T>
		auto PageHeap<T>::Iterator::GetIndex() const -> Indexor
	{
		return _idxOffset + xl_ctz8(_q);
	}

	template<typename T>
		T& PageHeap<T>::Iterator::get()
	{
		assert(_owner && _idxOffset != ~0u);
		auto pageIdx = _idxOffset >> 6;
		auto indexInPage = xl_ctz8(_q);
		return *(T*)&_owner->_pages[pageIdx]->_data[indexInPage*sizeof(T)];
	}
	template<typename T>
		const T& PageHeap<T>::Iterator::get() const
	{
		assert(_owner && _idxOffset != ~0u);
		auto pageIdx = _idxOffset >> 6;
		auto indexInPage = xl_ctz8(_q);
		return *(const T*)&_owner->_pages[pageIdx]->_data[indexInPage*sizeof(T)];
	}
	template<typename T>
		auto PageHeap<T>::Iterator::operator++() -> Iterator&
	{
		auto lastIdx = xl_ctz8(_q);
		assert(lastIdx != 64);
		_q ^= 1ull << uint64_t(lastIdx);
		while (!_q) {
			if (_allocationFlags.empty()) {
				// reached the end; reset
				_idxOffset = ~0u;
				_q = 0;
				_allocationFlags = {};
				return *this;
			}
			_q = ~_allocationFlags[0];
			++_allocationFlags.first;
		}
		return *this;
	}

	template<typename T>
		T& PageHeap<T>::Iterator::operator*() { return get(); }
	template<typename T>
		T* PageHeap<T>::Iterator::operator->() { return &get(); }
	template<typename T>
		const T& PageHeap<T>::Iterator::operator*() const { return get(); }
	template<typename T>
		const T* PageHeap<T>::Iterator::operator->() const { return &get(); }

}}}


namespace RenderCore { namespace LightingEngine
{

	////////////  temp ----->
	enum class SkyTextureType { HemiCube, Cube, Equirectangular, HemiEquirectangular };
	
#if 0
	class EnvironmentalLightingDesc
	{
	public:
		std::string   _skyTexture;   ///< use "<texturename>_*" when using a half cube style sky texture. The system will fill in "_*" with appropriate characters	
		SkyTextureType _skyTextureType;

		std::string   _diffuseIBL;   ///< Diffuse IBL map. Sometimes called irradiance map or ambient map
		std::string   _specularIBL;  ///< Prefiltered specular IBL map.

		Float3	_ambientLight = Float3(0.f, 0.f, 0.f);

		float   _skyBrightness = 1.f;
		float   _skyReflectionScale = 1.0f;
		float   _skyReflectionBlurriness = 2.f;

		bool    _doRangeFog = false;
		Float3  _rangeFogInscatter = Float3(0.f, 0.f, 0.f);
		float   _rangeFogThickness = 10000.f;     // optical thickness for range based fog

		bool    _doAtmosphereBlur = false;
		float   _atmosBlurStdDev = 1.3f;
		float   _atmosBlurStart = 1000.f;
		float   _atmosBlurEnd = 1500.f;
	};
#endif

}}

