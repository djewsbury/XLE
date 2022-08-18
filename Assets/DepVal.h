// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IFileSystem.h"
#include "../Utility/IteratorUtils.h"
#include "../Utility/StringUtils.h"

namespace Assets
{
    using ResChar = char;
    using DependencyValidationMarker = unsigned;
    constexpr DependencyValidationMarker DependencyValidationMarker_Invalid = ~DependencyValidationMarker(0);
    struct DependentFileState;
    struct DependencyUpdateReport;

    /// <summary>Handles resource invalidation events</summary>
    /// Utility class used for detecting resource invalidation events (for example, if
    /// a shader source file changes on disk). 
    /// Resources that can receive invalidation events should use this class to declare
    /// that dependency. 
    class DependencyValidation
    {
    public:
        unsigned        GetValidationIndex() const;

        void            RegisterDependency(const DependencyValidation&);
        void            RegisterDependency(const DependentFileState& state);

        void            IncreaseValidationIndex();      // (also increases validation index for any depvals dependent on this one)

        void            CollateDependentFileStates(std::vector<DependentFileState>&) const;
        void            CollateDependentFileUpdates(std::vector<DependencyUpdateReport>&) const;

        operator bool() const { return _marker != DependencyValidationMarker_Invalid; }
        operator DependencyValidationMarker() const { return _marker; }
        friend bool operator==(const DependencyValidation& lhs, const DependencyValidation& rhs) { return lhs._marker == rhs._marker; }
        friend bool operator!=(const DependencyValidation& lhs, const DependencyValidation& rhs) { return lhs._marker != rhs._marker; }
        friend bool operator<(const DependencyValidation& lhs, const DependencyValidation& rhs) { return lhs._marker < rhs._marker; }

        DependencyValidation();
        DependencyValidation(DependencyValidation&&) never_throws;
        DependencyValidation& operator=(DependencyValidation&&) never_throws;
        ~DependencyValidation();

        DependencyValidation(const DependencyValidation&);
        DependencyValidation& operator=(const DependencyValidation&);

        static DependencyValidation SafeCopy(const DependencyValidation&);      // copies with an additional check to ensure the global DepVal sys is still up

    private:
        friend class DependencyValidationSystem;
        DependencyValidation(DependencyValidationMarker marker);
        DependencyValidationMarker _marker = DependencyValidationMarker_Invalid;
    };

    class IDependencyValidationSystem
    {
    public:
        virtual DependencyValidation Make(IteratorRange<const StringSection<>*> filenames) = 0;
        virtual DependencyValidation Make(IteratorRange<const DependentFileState*> filestates) = 0;
        virtual DependencyValidation MakeOrReuse(IteratorRange<const DependencyValidationMarker*> dependencyAssets) = 0;
        virtual DependencyValidation Make() = 0;

        DependencyValidation Make(StringSection<> filename);
        DependencyValidation Make(const DependentFileState& filestate);

        virtual unsigned GetValidationIndex(DependencyValidationMarker marker) = 0;
        virtual DependentFileState GetDependentFileState(StringSection<> filename) = 0;
        virtual void ShadowFile(StringSection<> filename) = 0;
        virtual void CollateDependentFileStates(std::vector<DependentFileState>&, DependencyValidationMarker) = 0;
        virtual void CollateDependentFileUpdates(std::vector<DependencyUpdateReport>&, DependencyValidationMarker) = 0;

        /// <summary>Registers a dependency on a file on disk</summary>
        /// Registers a dependency on a file. The system will monitor that file for changes.
        /// <param name="validationMarker">Callback to receive invalidation events</param>
        /// <param name="filename">Normally formatted filename</param>
        virtual void RegisterFileDependency(
            DependencyValidationMarker validationMarker, 
            const DependentFileState& fileState) = 0;

        /// <summary>Registers a dependency on another resource</summary>
        /// Sometimes resources are dependent on other resources. This function helps registers a 
        /// dependency between resources.
        /// If <paramref name="dependency"/> ever gets a OnChange() message, then <paramref name="dependentResource"/> 
        /// will also receive the OnChange() message.
        virtual void RegisterAssetDependency(
            DependencyValidationMarker dependentResource, 
            DependencyValidationMarker dependency) = 0;

        virtual void IncreaseValidationIndex(DependencyValidationMarker depVal) = 0;

        virtual void AddRef(DependencyValidationMarker) = 0;
        virtual void Release(DependencyValidationMarker) = 0;

        virtual unsigned GlobalChangeIndex() = 0;

        virtual ~IDependencyValidationSystem() = default;
    };

    IDependencyValidationSystem& GetDepValSys();
    std::shared_ptr<IDependencyValidationSystem> CreateDepValSys();

    struct DependentFileState
    {
        std::string _filename;
        FileSnapshot _snapshot;

        DependentFileState() : _snapshot({FileSnapshot::State::Normal, 0}) {}
        DependentFileState(StringSection<char> filename, uint64_t timeMarker, FileSnapshot::State status=FileSnapshot::State::Normal)
        : _filename(filename.AsString()), _snapshot({status, timeMarker}) {}
		DependentFileState(const std::string& filename, uint64_t timeMarker, FileSnapshot::State status=FileSnapshot::State::Normal)
		: _filename(filename), _snapshot({status, timeMarker}) {}
		DependentFileState(const std::string& filename, const FileSnapshot& snapshot)
		: _filename(filename), _snapshot(snapshot) {}

		friend bool operator<(const DependentFileState& lhs, const DependentFileState& rhs)
		{
			if (lhs._filename < rhs._filename) return true;
			if (lhs._filename > rhs._filename) return false;
			return lhs._snapshot < rhs._snapshot;
		}

		friend bool operator==(const DependentFileState& lhs, const DependentFileState& rhs)
		{
			return lhs._filename == rhs._filename && lhs._snapshot == rhs._snapshot;
		}
    };

    struct DependencyUpdateReport
    {
        std::string _filename;
        FileSnapshot _registeredSnapshot, _currentStateSnapshot;
    };
}
