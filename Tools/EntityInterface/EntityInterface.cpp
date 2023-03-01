// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "EntityInterface.h"
#include "../../Assets/ContinuationUtil.h"
#include "../../Formatters/IDynamicFormatter.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Threading/Mutex.h"
#include <random>

namespace EntityInterface
{
	static FilenameRules s_fnRules('/', true);

	class FormatOverlappingDocuments : public Formatters::IDynamicInputFormatter
	{
	public:
		Formatters::FormatterBlob PeekNext() override
		{
			for (;;) {
				auto virtualState = GetVirtualElementsBlob();
				if (virtualState != Formatters::FormatterBlob::None)
					return virtualState;

				if (_activeMount == _mounts.end()) return Formatters::FormatterBlob::None;

				auto next = _activeMount->_formatter->PeekNext();
				if (next == Formatters::FormatterBlob::None) {
					_state = State::EndVirtualElements;
				} else
					return next;
			}
		}

		Formatters::FormatterBlob GetVirtualElementsBlob()
		{
			if (_state == State::BeginVirtualElements) {
				if (_pendingVirtualBeginElement)
					return Formatters::FormatterBlob::BeginElement;
				assert(_activeMount != _mounts.end());
				if (_activeFormatterExternalMountIterator != _activeMount->_externalMountPoint.end())
					return Formatters::FormatterBlob::KeyedItem;
				_state = State::Formatter;
			} else if (_state == State::EndVirtualElements) {
				assert(_activeMount != _mounts.end());
				if (_activeFormatterExternalMountIterator != _activeMount->_externalMountPoint.begin())
					return Formatters::FormatterBlob::EndElement;
				++_activeMount;
				BeginActiveFormatter();
				return GetVirtualElementsBlob();
			} else if (_activeMount != _mounts.end()) {
				// If the underlying formatter just ended, we need to transition to the EndVirtualElements state
				if (_activeMount->_formatter->PeekNext() == Formatters::FormatterBlob::None) {
					_state = State::EndVirtualElements;
					return GetVirtualElementsBlob();
				}
			}

			return Formatters::FormatterBlob::None;
		}

        bool TryBeginElement() override
		{
			auto virtualState = GetVirtualElementsBlob();
			if (virtualState == Formatters::FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				auto result = _activeMount->_formatter->TryBeginElement();
				if (!result && _activeMount->_formatter->PeekNext() == Formatters::FormatterBlob::None) {
					_state = State::EndVirtualElements;
					return TryBeginElement();
				}
				_currentElementDepth += unsigned(result);
				return result;
			} else {
				if (virtualState == Formatters::FormatterBlob::BeginElement) {
					_pendingVirtualBeginElement = false;
					return true;
				}
				return false;
			}
		}

		bool TryEndElement() override
		{
			auto virtualState = GetVirtualElementsBlob();
			if (virtualState == Formatters::FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				bool result = _activeMount->_formatter->TryEndElement();
				_currentElementDepth -= unsigned(result);
				return result;
			} else {
				if (virtualState == Formatters::FormatterBlob::EndElement) {
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
			if (virtualState == Formatters::FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				auto result = _activeMount->_formatter->TryKeyedItem(name);
				if (!result && _activeMount->_formatter->PeekNext() == Formatters::FormatterBlob::None) {
					++_activeMount;
					BeginActiveFormatter();
					return TryKeyedItem(name);
				}
				return result;
			} else {
				if (virtualState == Formatters::FormatterBlob::KeyedItem) {
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

		bool TryKeyedItem(uint64_t& name) override
		{
			auto virtualState = GetVirtualElementsBlob();
			if (virtualState == Formatters::FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				auto result = _activeMount->_formatter->TryKeyedItem(name);
				if (!result && _activeMount->_formatter->PeekNext() == Formatters::FormatterBlob::None) {
					++_activeMount;
					BeginActiveFormatter();
					return TryKeyedItem(name);
				}
				return result;
			} else {
				if (virtualState == Formatters::FormatterBlob::KeyedItem) {
					auto i = _activeFormatterExternalMountIterator;
					while (i!=_activeMount->_externalMountPoint.end() && *i != s_fnRules.GetSeparator<char>()) ++i;
					name = Hash64(MakeStringSection(_activeFormatterExternalMountIterator, i));
					while (i!=_activeMount->_externalMountPoint.end() && *i == s_fnRules.GetSeparator<char>()) ++i;
					_activeFormatterExternalMountIterator = i;
					_pendingVirtualBeginElement = true;
					return true;
				}
				return false;
			}
		}

		bool TryStringValue(StringSection<>& value) override
		{
			if (GetVirtualElementsBlob() == Formatters::FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				auto result = _activeMount->_formatter->TryStringValue(value);
				if (!result && _activeMount->_formatter->PeekNext() == Formatters::FormatterBlob::None) {
					++_activeMount;
					BeginActiveFormatter();
					return TryStringValue(value);
				}
				return result;
			} else {
				return true;
			}
		}

		bool TryRawValue(IteratorRange<const void*>& value, ImpliedTyping::TypeDesc& typeDesc) override
		{
			if (GetVirtualElementsBlob() == Formatters::FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return false;
				auto result = _activeMount->_formatter->TryRawValue(value, typeDesc);
				if (!result && _activeMount->_formatter->PeekNext() == Formatters::FormatterBlob::None) {
					++_activeMount;
					BeginActiveFormatter();
					return TryRawValue(value, typeDesc);
				}
				return result;
			} else {
				return false;
			}
		}

		virtual bool TryCastValue(IteratorRange<void*> destination, const ImpliedTyping::TypeDesc& type) override
		{
			if (GetVirtualElementsBlob() == Formatters::FormatterBlob::None) {
				if (_activeMount == _mounts.end()) return {};
				auto result = _activeMount->_formatter->TryCastValue(destination, type);
				if (!result && _activeMount->_formatter->PeekNext() == Formatters::FormatterBlob::None) {
					++_activeMount;
					BeginActiveFormatter();
					return TryCastValue(destination, type);
				}
				return result;
			} else {
				return {};
			}
		}

		void SkipValueOrElement() override
		{
			auto virtualState = GetVirtualElementsBlob();
			if (virtualState != Formatters::FormatterBlob::None) {
				if (virtualState == Formatters::FormatterBlob::BeginElement) {
					// just swap state from BeginVirtualElements to EndVirtualElements
					assert(_state == State::BeginVirtualElements);
					_state = State::EndVirtualElements;
					assert(GetVirtualElementsBlob() == Formatters::FormatterBlob::EndElement);
					TryEndElement();		// since we advance _activeFormatterExternalMountIterator in TryKeyedItem, we must reverse back over that now
				}
			} else {
				if (_activeMount == _mounts.end()) return;
				_activeMount->_formatter->SkipValueOrElement();
			}
		}

        Formatters::StreamLocation GetLocation() const override 
		{
			if (_activeMount == _mounts.end()) return {};
			return _activeMount->_formatter->GetLocation();
		}

        ::Assets::DependencyValidation GetDependencyValidation() const override 
		{
			if (!_depVal) {
				_depVal = ::Assets::GetDepValSys().Make();
				for (unsigned c=0; c<_mounts.size(); ++c)
					_depVal.RegisterDependency(_mounts[c]._formatter->GetDependencyValidation());
			}
			return _depVal;
		}

		FormatOverlappingDocuments(
			IteratorRange<std::shared_ptr<Formatters::IDynamicInputFormatter>*> mounts,
			IteratorRange<const std::string*> externalMountPoints,
			::Assets::Blob log)
		: _log(std::move(log))
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
			std::shared_ptr<Formatters::IDynamicInputFormatter> _formatter;
			std::string _externalMountPoint;
		};
		std::vector<Mount> _mounts;
		std::vector<Mount>::iterator _activeMount;
		std::string::iterator _activeFormatterExternalMountIterator;
		signed _currentElementDepth = 0;
		enum class State { BeginVirtualElements, Formatter, EndVirtualElements };
		State _state;
		bool _pendingVirtualBeginElement = false;
		mutable ::Assets::DependencyValidation _depVal;
		::Assets::Blob _log;

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

		std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>> BeginFormatter(StringSection<> inputMountPoint) const
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
				return {};

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

			struct Helper
			{
				std::vector<std::future<std::shared_ptr<Formatters::IDynamicInputFormatter>>> _futureFormatters;
				std::vector<std::string> _externalPosition;
			};
			auto helper = std::make_shared<Helper>();
			helper->_futureFormatters.reserve(overlappingMounts.size());
			helper->_externalPosition.reserve(overlappingMounts.size());
			for (auto& o:overlappingMounts) {
				helper->_futureFormatters.push_back(o._srcIterator->_document->BeginFormatter(o._internalPosition));
				helper->_externalPosition.push_back(o._externalPosition);
			}

			std::promise<std::shared_ptr<Formatters::IDynamicInputFormatter>> promise;
			auto future = promise.get_future();
			::Assets::PollToPromise(
				std::move(promise),
				[helper](auto timeout) {
					auto timeoutTime = std::chrono::steady_clock::now() + timeout;
					for (auto& p:helper->_futureFormatters)
						if (p.wait_until(timeoutTime) != std::future_status::ready)
							return ::Assets::PollStatus::Continue;
					return ::Assets::PollStatus::Finish;
				},
				[helper, actualizationLog]() mutable {
					std::vector<std::shared_ptr<Formatters::IDynamicInputFormatter>> actualized;
					actualized.resize(helper->_futureFormatters.size());
					auto a=actualized.begin();
					for (auto& p:helper->_futureFormatters) {
						*a = p.get();
						assert(*a);
						++a;
					}

					return std::make_shared<FormatOverlappingDocuments>(
						MakeIteratorRange(actualized), MakeIteratorRange(helper->_externalPosition), actualizationLog);
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

	StringAndHash MakeStringAndHash(StringSection<> str)
	{
		return { str, Hash64(str) };
	}

	class MultiInterfaceDocument : public IMutableEntityDocument
	{
	public:
		EntityId AssignEntityId() override
		{
			for (;;) {
				auto id = _rng();
				auto i = LowerBound(_assignedIds, id);
				if (i == _assignedIds.end() || i->first != id) {
					_assignedIds.insert(i, {id, ~0u});
					return id;
				}
			}
		}

		bool CreateEntity(StringAndHash objType, EntityId id, IteratorRange<const PropertyInitializer*> props) override
		{
			unsigned documentIdx = ~0u;
			bool result = false;

			auto t = LowerBound(_pairedTypes, objType.second);
			if (t != _pairedTypes.end() && t->first == objType.second)
				documentIdx = t->second;

			if (documentIdx != ~0u) {
				auto i = LowerBound(_assignedIds, id);
				assert(i != _assignedIds.end() && i->first == id);
				if (i != _assignedIds.end() && i->first == id)
					i->second = documentIdx;

				result |= _subDocs[documentIdx]->CreateEntity(objType, id, props);
			}

			if (_catchAllDocument != ~0u)
				result |= _subDocs[_catchAllDocument]->CreateEntity(objType, id, props);
			return result;
		}

		bool DeleteEntity(EntityId id) override
		{
			bool result = false;
			auto i = LowerBound(_assignedIds, id);
			if (i != _assignedIds.end() && i->first == id) {
				if (i->second != ~0u && _subDocs[i->second])
					result |= _subDocs[i->second]->DeleteEntity(id);
				if (_catchAllDocument != ~0u)
					result |= _subDocs[_catchAllDocument]->DeleteEntity(id);
				_assignedIds.erase(i);
			}
			return result;
		}

		bool SetProperty(EntityId id, IteratorRange<const PropertyInitializer*> props) override
		{
			bool result = false;
			auto i = LowerBound(_assignedIds, id);
			if (i != _assignedIds.end() && i->first == id) {
				if (i->second != ~0u && _subDocs[i->second])
					result |= _subDocs[i->second]->SetProperty(id, props);
				if (_catchAllDocument != ~0u)
					result |= _subDocs[_catchAllDocument]->SetProperty(id, props);
			}
			return result;
		}

		std::optional<ImpliedTyping::TypeDesc> GetProperty(EntityId id, StringAndHash prop, IteratorRange<void*> destinationBuffer) const override
		{
			auto i = LowerBound(_assignedIds, id);
			if (i != _assignedIds.end() && i->first == id) {
				if (i->second != ~0u && _subDocs[i->second])
					if (auto res = _subDocs[i->second]->GetProperty(id, prop, destinationBuffer))
						return res;
				if (_catchAllDocument != ~0u)
					return _subDocs[_catchAllDocument]->GetProperty(id, prop, destinationBuffer);
			}
			return {};
		}

		bool SetParent(EntityId child, EntityId parent, StringAndHash childList, int insertionPosition) override
		{
			bool result = false;
			auto childI = LowerBound(_assignedIds, child);
			auto parentI = LowerBound(_assignedIds, parent);
			if (childI != _assignedIds.end() && childI->first == child
				&& parentI != _assignedIds.end() && parentI->first == parent) {

				if (childI->second == parentI->second) {		// expecting both to exist within the same subdocument
					if (childI->second != ~0u && _subDocs[childI->second])
						result |= _subDocs[childI->second]->SetParent(child, parent, childList, insertionPosition);
				}

				if (_catchAllDocument != ~0u)
					result |= _subDocs[_catchAllDocument]->SetParent(child, parent, childList, insertionPosition);
			}
			return result;
		}

		void RegisterSubDocument(uint64_t entityType, std::shared_ptr<IMutableEntityDocument> subDoc)
		{
			auto i = _subDocs.begin();
			for (;i!=_subDocs.end(); ++i)
				if (i->get() == subDoc.get())
					break;
			if (i == _subDocs.end())
				i = _subDocs.insert(_subDocs.end(), std::move(subDoc));

			auto docIdx = (unsigned)std::distance(_subDocs.begin(), i);
			
			auto q = LowerBound(_pairedTypes, entityType);
			if (q != _pairedTypes.end() && q->first == entityType) {
				q->second = docIdx;
			} else
				q = _pairedTypes.insert(q, {entityType, docIdx});
		}

		void RegisterCatchAllDocument(std::shared_ptr<IMutableEntityDocument> subDoc)
		{
			auto i = _subDocs.begin();
			for (;i!=_subDocs.end(); ++i)
				if (i->get() == subDoc.get())
					break;
			if (i == _subDocs.end())
				i = _subDocs.insert(_subDocs.end(), std::move(subDoc));
			_catchAllDocument = (unsigned)std::distance(_subDocs.begin(), i);
		}

		void TryRemoveSubDocument(IMutableEntityDocument& subDoc)
		{
			for (auto& d:_subDocs)
				if (d.get() == &subDoc)
					d.reset();
		}

		MultiInterfaceDocument()
		: _rng{std::random_device().operator()()}
		{}
		~MultiInterfaceDocument() {}
	private:
		std::mt19937_64 _rng;
		std::vector<std::pair<EntityId, unsigned>> _assignedIds;
		std::vector<std::shared_ptr<IMutableEntityDocument>> _subDocs;
		std::vector<std::pair<uint64_t, unsigned>> _pairedTypes;
		unsigned _catchAllDocument = ~0u;
	};

	auto Switch::CreateDocument(StringSection<> docType, StringSection<> initializer) -> DocumentId
	{
		for (auto i=_documentTypes.begin(); i!=_documentTypes.end(); ++i)
			if (XlEqString(docType, i->first)) {
				auto result = _nextDocumentId++;
				auto newDoc = i->second->CreateDocument(initializer, result);
				_documents.emplace_back(result, std::move(newDoc));
				return result;
			}
		return ~DocumentId(0);
	}

	auto Switch::CreateDocument(std::shared_ptr<IMutableEntityDocument> doc) -> DocumentId
	{
		auto result = _nextDocumentId++;
		_documents.emplace_back(result, std::move(doc));
		return result;
	}

	bool Switch::DeleteDocument(DocumentId docId)
	{
		auto i = LowerBound(_documents, docId);
		if (i != _documents.end() && i->first == docId) {
			// check to see if it's registered as one of our defaults, and erase if so
			if (_defaultDocument)
				_defaultDocument->TryRemoveSubDocument(*i->second);
			_documents.erase(i);
			return true;
		}
		return false;
	}

	IMutableEntityDocument* Switch::GetInterface(DocumentId docId)
	{
		auto i = LowerBound(_documents, docId);
		if (i != _documents.end() && i->first == docId)
			return i->second.get();
		return _defaultDocument.get();
	}

	void Switch::RegisterDocumentType(StringSection<> name, std::shared_ptr<IDocumentType> docType)
	{
		for (const auto& d:_documentTypes)
			assert(!XlEqString(name, d.first));
		_documentTypes.emplace_back(name.AsString(), std::move(docType));
	}

	void Switch::DeregisterDocumentType(StringSection<> name)
	{
		for (auto i=_documentTypes.begin(); i!=_documentTypes.end(); ++i)
			if (XlEqString(name, i->first)) {
				_documentTypes.erase(i);
				return;
			}
		assert(0);		// couldn't be found
	}

	void Switch::RegisterDefaultDocument(StringAndHash objType, DocumentId docId)
	{
		auto i = LowerBound(_documents, docId);
		if (i != _documents.end() && i->first == docId) {
			if (!_defaultDocument)
				_defaultDocument = std::make_shared<MultiInterfaceDocument>();
			_defaultDocument->RegisterSubDocument(objType.second, i->second);
			return;
		}
		assert(0);		// didn't find document with the given id
	}

	void Switch::RegisterDefaultDocument(DocumentId docId)
	{
		auto i = LowerBound(_documents, docId);
		if (i != _documents.end() && i->first == docId) {
			if (!_defaultDocument)
				_defaultDocument = std::make_shared<MultiInterfaceDocument>();
			_defaultDocument->RegisterCatchAllDocument(i->second);
			return;
		}
		assert(0);		// didn't find document with the given id
	}

	Switch::Switch() {}
	Switch::~Switch() {}
}
