// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "../Assets/DepVal.h"
#include "../Assets/IArtifact.h"
#include "../Math/Vector.h"
#include "../Math/Matrix.h"
#include <vector>

namespace SceneEngine
{
	static const uint64_t ChunkType_Placements = ConstHash64<'Plac','emen','ts'>::Value;

	class PlacementsScaffold
	{
	public:
		using BoundingBox = std::pair<Float3, Float3>;

		struct ObjectReference
		{
			Float3x4    _localToCell;
			unsigned    _modelFilenameOffset;       // note -- hash values should be stored with the filenames
			unsigned    _materialFilenameOffset;
			unsigned    _supplementsOffset;
			uint64_t    _guid;
			Float3x3    _decomposedRotation;
			Float3      _decomposedScale;
		};
		
		IteratorRange<const ObjectReference*>	GetObjectReferences() const;
		IteratorRange<const BoundingBox*>		GetCellSpaceBoundaries() const;
		const void*								GetFilenamesBuffer() const;
		const uint64_t*							GetSupplementsBuffer() const;

		void LogDetails(const char title[]) const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const	{ return _dependencyValidation; }
		static const auto CompileProcessType = ChunkType_Placements;
		static const ::Assets::ArtifactRequest ChunkRequests[1];

		PlacementsScaffold(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
		PlacementsScaffold();
		~PlacementsScaffold();
	protected:
		std::vector<ObjectReference>    _objects;
		std::vector<BoundingBox>		_cellSpaceBoundaries;
		std::vector<uint8_t>            _filenamesBuffer;
		std::vector<uint64_t>           _supplementsBuffer;

		::Assets::DependencyValidation				_dependencyValidation;
		void ReplaceString(const char oldString[], const char newString[]);
		friend class DynamicPlacements;
	};

	inline auto            	PlacementsScaffold::GetObjectReferences() const -> IteratorRange<const ObjectReference*>	{ return _objects; }
	inline auto            	PlacementsScaffold::GetCellSpaceBoundaries() const -> IteratorRange<const BoundingBox*>	{ return _cellSpaceBoundaries; }
	inline const void*     	PlacementsScaffold::GetFilenamesBuffer() const								{ return AsPointer(_filenamesBuffer.begin()); }
	inline const uint64_t*	PlacementsScaffold::GetSupplementsBuffer() const							{ return AsPointer(_supplementsBuffer.begin()); }

	class NascentPlacement
	{
	public:
		struct Resource
		{
			std::string _name, _material;
			std::pair<Float3, Float3> _cellSpaceBoundary;
		};
		Float3x4 _localToCell;
		Resource _resource;
	};
	::Assets::Blob SerializePlacements(IteratorRange<const NascentPlacement*>);

}
