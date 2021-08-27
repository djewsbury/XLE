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

		// Note that "placements" that interface methods in Placements are actually
		// very rarely called. So it should be fine to make those methods into virtual
		// methods, and use an abstract base class.
	class Placements
	{
	public:
		using BoundingBox = std::pair<Float3, Float3>;

		class ObjectReference
		{
		public:
			Float3x4    _localToCell;
			BoundingBox _cellSpaceBoundary;
			unsigned    _modelFilenameOffset;       // note -- hash values should be stored with the filenames
			unsigned    _materialFilenameOffset;
			unsigned    _supplementsOffset;
			uint64_t    _guid;
			Float3x3    _decomposedRotation;
			Float3      _decomposedScale;
		};
		
		const ObjectReference*  GetObjectReferences() const;
		unsigned                GetObjectReferenceCount() const;
		const void*             GetFilenamesBuffer() const;
		const uint64_t*           GetSupplementsBuffer() const;

		void Write(const Assets::ResChar destinationFile[]) const;
		::Assets::Blob Serialize() const;
		void LogDetails(const char title[]) const;

		const ::Assets::DependencyValidation& GetDependencyValidation() const	{ return _dependencyValidation; }
		static const auto CompileProcessType = ChunkType_Placements;
		static const ::Assets::ArtifactRequest ChunkRequests[1];

		Placements(IteratorRange<::Assets::ArtifactRequestResult*> chunks, const ::Assets::DependencyValidation& depVal);
		Placements();
		~Placements();
	protected:
		std::vector<ObjectReference>    _objects;
		std::vector<uint8_t>            _filenamesBuffer;
		std::vector<uint64_t>           _supplementsBuffer;

		::Assets::DependencyValidation				_dependencyValidation;
		void ReplaceString(const char oldString[], const char newString[]);
	};

	inline auto            Placements::GetObjectReferences() const -> const ObjectReference*	{ return AsPointer(_objects.begin()); }
	inline unsigned        Placements::GetObjectReferenceCount() const							{ return unsigned(_objects.size()); }
	inline const void*     Placements::GetFilenamesBuffer() const								{ return AsPointer(_filenamesBuffer.begin()); }
	inline const uint64_t*   Placements::GetSupplementsBuffer() const							{ return AsPointer(_supplementsBuffer.begin()); }

	class NascentPlacement
	{
	public:
		struct Resource
		{
			std::string _name, _material;
			std::pair<Float3, Float3> _aabb;
		};
		Float3x4 _localToCell;
		Resource _resource;
	};
	::Assets::Blob SerializePlacements(IteratorRange<const NascentPlacement*>);

}
