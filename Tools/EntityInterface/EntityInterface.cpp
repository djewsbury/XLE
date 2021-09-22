// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "EntityInterface.h"
#include "../../Utility/Streams/PathUtils.h"

namespace EntityInterface
{
	static FilenameRules s_fnRules('/', true);

	static std::string SimplifyMountPoint(StringSection<> input, const FilenameRules& fnRules)
	{
		auto split = MakeSplitPath(input);
		split.BeginsWithSeparator() = false;
		split.EndsWithSeparator() = true;
		return split.Simplify().Rebuild(fnRules);
	}

	class MountingTree : public IEntityMountingTree
	{
	public:

		DocumentId MountDocument(
			StringSection<> mountPount,
			std::shared_ptr<IEntityDocument> doc)
		{
			Mount mnt;
			mnt._mountPoint = SimplifyMountPoint(mountPount.AsString(), s_fnRules);
			mnt._mountPointSplit = MakeSplitPath(mnt._mountPoint);

			auto hash = s_FNV_init64;
			for (auto i:mnt._mountPointSplit.GetSections())
				hash = HashFilename(i, s_fnRules, hash);
			mnt._hash = hash;
			mnt._depth = mnt._mountPointSplit.GetSectionCount();
			mnt._document = std::move(doc);
			auto result = mnt._documentId = _nextDocumentId++;
			_mounts.push_back(std::move(mnt));
			return result;
		}

		bool UnmountDocument(DocumentId doc)
		{
			for (auto mnt=_mounts.begin(); mnt!=_mounts.end(); ++mnt)
				if (mnt->_documentId == doc) {
					_mounts.erase(mnt);
					return true;
				}
			return false;
		}

		::Assets::DependencyValidation GetDependencyValidation(StringSection<> mountPount) const
		{
			return {};
		}

		::Assets::PtrToFuturePtr<IDynamicFormatter> BeginFormatter(StringSection<> mountPoint) const
		{
			return nullptr;
		}

	private:
		struct Mount
		{
			uint64_t _hash;
			unsigned _depth;

			std::shared_ptr<IEntityDocument> _document;

			std::string _mountPoint;
			SplitPath<> _mountPointSplit;
			DocumentId _documentId;
		};
		std::vector<Mount> _mounts;
		DocumentId _nextDocumentId = 1;
	};

	std::shared_ptr<IEntityMountingTree> CreateMountingTree()
	{
		return std::make_shared<MountingTree>();
	}
}
