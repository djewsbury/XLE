// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Utility/IteratorUtils.h"
#include <any>
#include <functional>
#include <vector>

namespace SceneEngine
{
	using MetadataProvider = std::function<std::any(uint64_t)>;

	class DrawableMetadataLookupContext
	{
	public:
		unsigned NextIndex() const;
		bool Finished() const;
		unsigned PktIndex() const;
		void AddProviderAndAdvance(MetadataProvider&&);
		void AdvanceIndexOffset(unsigned);

		IteratorRange<MetadataProvider*> GetProviders() { return MakeIteratorRange(_providers); }

		DrawableMetadataLookupContext(IteratorRange<const unsigned*> searchIndices, unsigned pktIdx = 0);
		~DrawableMetadataLookupContext();
	private:
		IteratorRange<const unsigned*> _searchIndices;
		unsigned _searchIndicesOffset;
		unsigned _pktIndex;
		std::vector<MetadataProvider> _providers;
	};

////////////////////////////////////////////////////////////////////////////////////////////////////////

	inline bool DrawableMetadataLookupContext::Finished() const
	{
		return _searchIndices.empty();
	}
	inline unsigned DrawableMetadataLookupContext::NextIndex() const
	{
		assert(!Finished());
		return *_searchIndices.begin() - _searchIndicesOffset;
	}
	inline unsigned DrawableMetadataLookupContext::PktIndex() const
	{
		return _pktIndex;
	}
	inline void DrawableMetadataLookupContext::AddProviderAndAdvance(MetadataProvider&& provider)
	{
		assert(!Finished());
		_providers.emplace_back(std::move(provider));
		++_searchIndices.first;
	}
	inline void DrawableMetadataLookupContext::AdvanceIndexOffset(unsigned offsetIncrease)
	{
		assert(!Finished() && NextIndex() >= offsetIncrease);
		_searchIndicesOffset += offsetIncrease;
	}

	inline DrawableMetadataLookupContext::DrawableMetadataLookupContext(IteratorRange<const unsigned*> searchIndices, unsigned pktIdx)
	: _searchIndices(searchIndices)
	, _searchIndicesOffset(0)
	, _pktIndex(pktIdx)
	{
		// drawableIndices must be sorted on entry, so we can easily tell which is the next drawable to query
		assert(std::is_sorted(_searchIndices.begin(), _searchIndices.end()));
		_providers.reserve(_searchIndices.size());
	}

	inline DrawableMetadataLookupContext::~DrawableMetadataLookupContext() {}

}

