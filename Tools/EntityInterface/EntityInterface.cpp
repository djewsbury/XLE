// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "EntityInterface.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Threading/Mutex.h"

namespace EntityInterface
{
	static FilenameRules s_fnRules('/', true);

	class FormatOverlappingDocuments : public IDynamicFormatter
	{
	public:
		FormatterBlob PeekNext() override
		{
			for (;;) {
				auto virtualState = GetVirtualElementsBlob();
				if (virtualState != FormatterBlob::None)
					return virtualState;

				if (_activeMount == _mounts.end()) return FormatterBlob::None;

				auto next = _activeMount->_formatter->PeekNext();
				if (next == FormatterBlob::None) {
					_state = State::EndVirtualElements;
				} else
					return next;
			}
		}

		FormatterBlob GetVirtualElementsBlob()
		{
			if (_state == State::BeginVirtualElements) {
				if (_pendingVirtualBeginElement)
					return FormatterBlob::BeginElement;
				assert(_activeMount != _mounts.end());
				if (_activeFormatterExternalMountIterator != _activeMount->_externalMountPoint.end())
					return FormatterBlob::KeyedItem;
				_state = State::Formatter;
			} else if (_state == State::EndVirtualElements) {
				assert(_activeMount != _mounts.end());
				if (_activeFormatterExternalMountIterator != _activeMount->_externalMountPoint.begin())
					return FormatterBlob::EndElement;
				++_activeMount;
				BeginActiveFormatter();
				return GetVirtualElementsBlob();
			} else if (_activeMount != _mounts.end()) {
				// If the underlying formatter just ended, we need to transition to the EndVirtualElements state
				if (_activeMount->_formatter->PeekNext() == FormatterBlob::None) {
					_state = State::EndVirtualElements;
					return GetVirtualElementsBlob();
				}
			}

			return FormatterBlob::None;
		}

        bool TryBeginElement() override
		{
			auto virtualState = GetVirtualElementsBlob();
			if (virtualState == FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				auto result = _activeMount->_formatter->TryBeginElement();
				if (!result && _activeMount->_formatter->PeekNext() == FormatterBlob::None) {
					_state = State::EndVirtualElements;
					return TryBeginElement();
				}
				_currentElementDepth += unsigned(result);
				return result;
			} else {
				if (virtualState == FormatterBlob::BeginElement) {
					_pendingVirtualBeginElement = false;
					return true;
				}
				return false;
			}
		}

		bool TryEndElement() override
		{
			auto virtualState = GetVirtualElementsBlob();
			if (virtualState == FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				bool result = _activeMount->_formatter->TryEndElement();
				_currentElementDepth -= unsigned(result);
				return result;
			} else {
				if (virtualState == FormatterBlob::EndElement) {
					assert(!_pendingVirtualBeginElement);
					assert(_activeFormatterExternalMountIterator != _activeMount->_externalMountPoint.begin());
					// drop back one section in _activeMount->_externalMountPoint
					while (_activeFormatterExternalMountIterator != _activeMount->_externalMountPoint.begin() && *(_activeFormatterExternalMountIterator-1) != s_fnRules.GetSeparator<char>())
						--_activeFormatterExternalMountIterator;
					while (_activeFormatterExternalMountIterator != _activeMount->_externalMountPoint.begin() && *(_activeFormatterExternalMountIterator-1) == s_fnRules.GetSeparator<char>())
						--_activeFormatterExternalMountIterator;		// past the separator
					return true;
				}
				return false;
			}
		}

		bool TryKeyedItem(StringSection<>& name) override
		{
			auto virtualState = GetVirtualElementsBlob();
			if (virtualState == FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				auto result = _activeMount->_formatter->TryKeyedItem(name);
				if (!result && _activeMount->_formatter->PeekNext() == FormatterBlob::None) {
					++_activeMount;
					BeginActiveFormatter();
					return TryKeyedItem(name);
				}
				return result;
			} else {
				if (virtualState == FormatterBlob::KeyedItem) {
					auto i = _activeFormatterExternalMountIterator;
					while (i!=_activeMount->_externalMountPoint.end() && *i != s_fnRules.GetSeparator<char>()) ++i;
					name = MakeStringSection(_activeFormatterExternalMountIterator, i);
					while (i!=_activeMount->_externalMountPoint.end() && *i == s_fnRules.GetSeparator<char>()) ++i;
					_activeFormatterExternalMountIterator = i;
					_pendingVirtualBeginElement = true;
					return true;
				}
				return false;
			}
		}

		bool TryValue(StringSection<>& value) override
		{
			if (GetVirtualElementsBlob() == FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				auto result = _activeMount->_formatter->TryValue(value);
				if (!result && _activeMount->_formatter->PeekNext() == FormatterBlob::None) {
					++_activeMount;
					BeginActiveFormatter();
					return TryValue(value);
				}
				return result;
			} else {
				return true;
			}
		}

		bool TryCharacterData(StringSection<>& cdata) override
		{
			if (GetVirtualElementsBlob() == FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				auto result = _activeMount->_formatter->TryCharacterData(cdata);
				if (!result && _activeMount->_formatter->PeekNext() == FormatterBlob::None) {
					++_activeMount;
					BeginActiveFormatter();
					return TryCharacterData(cdata);
				}
				return result;
			} else {
				return false;
			}
		}

        StreamLocation GetLocation() const override { assert(0); return {}; }
        ::Assets::DependencyValidation GetDependencyValidation() const override { /*assert(0);*/ return {}; }

		FormatOverlappingDocuments(
			IteratorRange<std::shared_ptr<IDynamicFormatter>*> mounts,
			IteratorRange<const std::string*> externalMountPoints)
		{
			assert(mounts.size() == externalMountPoints.size());
			assert(!mounts.empty());
			_mounts.reserve(mounts.size());
			for (unsigned c=0; c<mounts.size(); ++c)
				_mounts.push_back({std::move(mounts[c]), externalMountPoints[c]});
			_activeMount = _mounts.begin();
			BeginActiveFormatter();
		}

		FormatOverlappingDocuments(const FormatOverlappingDocuments&) = delete;
		FormatOverlappingDocuments& operator=(const FormatOverlappingDocuments&) = delete;

	private:
		struct Mount
		{
			std::shared_ptr<IDynamicFormatter> _formatter;
			std::string _externalMountPoint;
		};
		std::vector<Mount> _mounts;
		std::vector<Mount>::iterator _activeMount;
		std::string::iterator _activeFormatterExternalMountIterator;
		signed _currentElementDepth = 0;
		enum class State { BeginVirtualElements, Formatter, EndVirtualElements };
		State _state;
		bool _pendingVirtualBeginElement = false;

		void BeginActiveFormatter()
		{
			assert(_activeMount >= _mounts.begin() && _activeMount <= _mounts.end());
			assert(!_currentElementDepth);
			if (_activeMount != _mounts.end()) {
				_state = State::BeginVirtualElements;
				_activeFormatterExternalMountIterator = _activeMount->_externalMountPoint.begin();
				_pendingVirtualBeginElement = false;
				assert(_activeMount->_externalMountPoint.empty() || *_activeMount->_externalMountPoint.begin() != s_fnRules.GetSeparator<char>());		// ideally we don't want separators at the beginning or ending
				assert(_activeMount->_externalMountPoint.empty() || *(_activeMount->_externalMountPoint.end()-1) != s_fnRules.GetSeparator<char>());
			} else {
				_state = State::Formatter;
			}
		}
	};

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
			ScopedLock(_mountsLock);
			Mount mnt;
			mnt._mountPoint = SimplifyMountPoint(mountPount.AsString(), s_fnRules);
			mnt._mountPointSplit = MakeSplitPath(mnt._mountPoint);

			mnt._partialHashes.reserve(mnt._mountPointSplit.GetSectionCount());
			auto hash = s_FNV_init64;
			for (auto i:mnt._mountPointSplit.GetSections()) {
				hash = HashFilename(i, s_fnRules, hash);
				mnt._partialHashes.push_back(hash);
			}
			mnt._hash = hash;
			mnt._depth = mnt._mountPointSplit.GetSectionCount();
			mnt._document = std::move(doc);
			auto result = mnt._documentId = _nextDocumentId++;
			auto insertPoint = std::upper_bound(
				_mounts.begin(), _mounts.end(), mnt._depth,
				[](unsigned lhs, const Mount& rhs) { return lhs < rhs._depth; });
			_mounts.insert(insertPoint, std::move(mnt));
			return result;
		}

		bool UnmountDocument(DocumentId doc)
		{
			ScopedLock(_mountsLock);
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

		::Assets::PtrToFuturePtr<IDynamicFormatter> BeginFormatter(StringSection<> inputMountPoint) const
		{
			ScopedLock(_mountsLock);

			struct OverlappingMount
			{
				std::vector<Mount>::const_iterator _srcIterator;
				std::string _internalPosition;
				std::string _externalPosition;
			};
			std::vector<OverlappingMount> overlappingMounts;
			overlappingMounts.reserve(_mounts.size());

			auto inputSplit = MakeSplitPath(inputMountPoint);
			unsigned hashedInputSplitSections = 0;
			uint64_t inputSplitHash = s_FNV_init64;

			auto mnti = _mounts.begin();
			while (mnti != _mounts.end()) {
				assert(mnti->_depth >= hashedInputSplitSections);		// mounts sorted by increasing depth
				if (mnti->_depth > inputSplit.GetSectionCount()) break;
				while (hashedInputSplitSections < mnti->_depth) {
					inputSplitHash = HashFilename(inputSplit.GetSections()[hashedInputSplitSections], s_fnRules, inputSplitHash);
					++hashedInputSplitSections;
				}

				if (mnti->_hash == inputSplitHash) {
					if (hashedInputSplitSections < inputSplit.GetSectionCount()) {
						auto internalPoint = MakeStringSection(inputSplit.GetSections()[hashedInputSplitSections].begin(), inputMountPoint.end());
						overlappingMounts.push_back({mnti, internalPoint.AsString()});
					} else {
						overlappingMounts.push_back({mnti});
					}
				}
				++mnti;
			}

			// there might be some partial matches that we need to check as well (in other words, mounts that are deeper than "inputMountPoint")
			while (mnti != _mounts.end()) {
				assert(mnti->_depth > inputSplit.GetSectionCount());
				assert(mnti->_partialHashes.size() > inputSplit.GetSectionCount());
				
				if (mnti->_partialHashes[inputSplit.GetSectionCount()] != inputSplitHash) {
					std::string externalPosition { 
						mnti->_mountPointSplit.GetSections()[inputSplit.GetSectionCount()].begin(), 
						mnti->_mountPointSplit.GetSections()[mnti->_mountPointSplit.GetSectionCount()-1].end() };
					overlappingMounts.push_back({mnti, std::string{}, externalPosition});
				}
				++mnti;
			}

			if (overlappingMounts.empty())
				return nullptr;

			// if just a single document; we can return a formatter directly from there
			if (overlappingMounts.size() == 1 && overlappingMounts[0]._externalPosition.empty())
				return overlappingMounts[0]._srcIterator->_document->BeginFormatter(overlappingMounts[0]._internalPosition);

			::Assets::Blob actualizationLog;
			if (_flags & MountingTreeFlags::LogMountPoints) {
				std::stringstream str;
				for (const auto&mnt:overlappingMounts)
					str << "[" << mnt._srcIterator->_mountPoint << "] internal: " << mnt._internalPosition << " external: " << mnt._externalPosition << std::endl;
				actualizationLog = ::Assets::AsBlob(str.str());
			}

			std::vector<Assets::PtrToFuturePtr<IDynamicFormatter>> futureFormatters;
			std::vector<std::string> externalPosition;
			futureFormatters.reserve(overlappingMounts.size());
			externalPosition.reserve(overlappingMounts.size());
			for (auto& o:overlappingMounts) {
				futureFormatters.push_back(o._srcIterator->_document->BeginFormatter(o._internalPosition));
				externalPosition.push_back(o._externalPosition);
			}

			auto future = std::make_shared<::Assets::FuturePtr<IDynamicFormatter>>();
			future->SetPollingFunction(
				[futureFormatters=std::move(futureFormatters), externalPosition=std::move(externalPosition), actualizationLog](::Assets::FuturePtr<IDynamicFormatter>& thatFuture) {
					std::shared_ptr<IDynamicFormatter> actualized[futureFormatters.size()];
					auto a=actualized;
					::Assets::Blob queriedLog;
					::Assets::DependencyValidation queriedDepVal;
					for (const auto& p:futureFormatters) {
						auto state = p->CheckStatusBkgrnd(*a, queriedDepVal, queriedLog);
						if (state != ::Assets::AssetState::Ready) {
							if (state == ::Assets::AssetState::Invalid) {
								thatFuture.SetInvalidAsset(queriedDepVal, queriedLog);
								return false;
							} else 
								return true;
						}
						assert(*a);
						++a;
					}

					thatFuture.SetAsset(
						std::make_shared<FormatOverlappingDocuments>(
							MakeIteratorRange(actualized, actualized+futureFormatters.size()), MakeIteratorRange(externalPosition)), 
							actualizationLog);
					return false;
				});
			return future;
		}

		MountingTree(MountingTreeFlags::BitField flags) : _flags(flags) {}

	private:
		struct Mount
		{
			uint64_t _hash;
			unsigned _depth;

			std::shared_ptr<IEntityDocument> _document;
			std::vector<uint64_t> _partialHashes;

			std::string _mountPoint;
			SplitPath<> _mountPointSplit;
			DocumentId _documentId;

			Mount() {}
			Mount(Mount&& moveFrom)
			{
				_hash = std::move(moveFrom._hash);
				_depth = std::move(moveFrom._depth);
				_document = std::move(moveFrom._document);
				_partialHashes = std::move(moveFrom._partialHashes);
				_mountPoint = std::move(moveFrom._mountPoint);
				_documentId = std::move(moveFrom._documentId);
				_mountPointSplit = MakeSplitPath(_mountPoint);
			}
			Mount& operator=(Mount&& moveFrom)
			{
				if (&moveFrom == this) return *this;
				_hash = std::move(moveFrom._hash);
				_depth = std::move(moveFrom._depth);
				_document = std::move(moveFrom._document);
				_partialHashes = std::move(moveFrom._partialHashes);
				_mountPoint = std::move(moveFrom._mountPoint);
				_documentId = std::move(moveFrom._documentId);
				_mountPointSplit = MakeSplitPath(_mountPoint);
				return *this;
			}
		};
		std::vector<Mount> _mounts;
		DocumentId _nextDocumentId = 1;
		MountingTreeFlags::BitField _flags = 0;
		mutable Threading::Mutex _mountsLock;
	};

	std::shared_ptr<IEntityMountingTree> CreateMountingTree(MountingTreeFlags::BitField flags)
	{
		return std::make_shared<MountingTree>(flags);
	}
}
