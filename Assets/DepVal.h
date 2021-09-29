// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/IteratorUtils.h"
#include "../Utility/StringUtils.h"

namespace Assets
{
    using ResChar = char;
    using DependencyValidationMarker = unsigned;
    constexpr DependencyValidationMarker DependencyValidationMarker_Invalid = ~DependencyValidationMarker(0);
    class DependentFileState;

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
        void            RegisterDependency(StringSection<>);
        void            RegisterDependency(DependentFileState& state);

        void            IncreaseValidationIndex();      // (also increases validation index for any depvals dependent on this one)

        operator bool() const { return _marker != DependencyValidationMarker_Invalid; }
        friend bool operator==(const DependencyValidation& lhs, const DependencyValidation& rhs) { return lhs._marker == rhs._marker; }
        friend bool operator!=(const DependencyValidation& lhs, const DependencyValidation& rhs) { return lhs._marker != rhs._marker; }
        friend bool operator<(const DependencyValidation& lhs, const DependencyValidation& rhs) { return lhs._marker < rhs._marker; }

        DependencyValidation();
        DependencyValidation(DependencyValidation&&) never_throws;
        DependencyValidation& operator=(DependencyValidation&&) never_throws;
        ~DependencyValidation();

        DependencyValidation(const DependencyValidation&);
        DependencyValidation& operator=(const DependencyValidation&);

    private:
        friend class DependencyValidationSystem;
        DependencyValidation(DependencyValidationMarker marker);
        DependencyValidationMarker _marker = DependencyValidationMarker_Invalid;
    };

    class DependentFileState
    {
    public:
        std::string _filename;
        
        enum class Status { Normal, Shadowed, DoesNotExist };
        uint64_t _timeMarker;
        Status _status;

        DependentFileState() : _timeMarker(0ull), _status(Status::Normal) {}
        DependentFileState(StringSection<ResChar> filename, uint64_t timeMarker, Status status=Status::Normal)
        : _filename(filename.AsString()), _timeMarker(timeMarker), _status(status) {}
		DependentFileState(const std::basic_string<ResChar>& filename, uint64_t timeMarker, Status status=Status::Normal)
		: _filename(filename), _timeMarker(timeMarker), _status(status) {}

		friend bool operator<(const DependentFileState& lhs, const DependentFileState& rhs)
		{
			if (lhs._filename < rhs._filename) return true;
			if (lhs._filename > rhs._filename) return false;
			if (lhs._timeMarker < rhs._timeMarker) return true;
			if (lhs._timeMarker > rhs._timeMarker) return false;
			return (int)lhs._status < (int)rhs._status;
		}

		friend bool operator==(const DependentFileState& lhs, const DependentFileState& rhs)
		{
			return lhs._filename == rhs._filename && lhs._timeMarker == rhs._timeMarker && lhs._status == rhs._status;
		}
    };

    class IDependencyValidationSystem
    {
    public:
        virtual DependencyValidation Make(IteratorRange<const StringSection<>*> filenames) = 0;
        virtual DependencyValidation Make(IteratorRange<const DependentFileState*> filestates) = 0;
        virtual DependencyValidation Make() = 0;

        inline DependencyValidation Make(StringSection<> filename);
        inline DependencyValidation Make(const DependentFileState& filestate);

        virtual unsigned GetValidationIndex(DependencyValidationMarker marker) = 0;
        virtual DependentFileState GetDependentFileState(StringSection<> filename) = 0;
        virtual void ShadowFile(StringSection<> filename) = 0;

        /// <summary>Registers a dependency on a file on disk</summary>
        /// Registers a dependency on a file. The system will monitor that file for changes.
        /// <param name="validationMarker">Callback to receive invalidation events</param>
        /// <param name="filename">Normally formatted filename</param>
        virtual void RegisterFileDependency(
            DependencyValidationMarker validationMarker, 
            StringSection<> filename) = 0;

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

        virtual ~IDependencyValidationSystem() = default;
    };

    IDependencyValidationSystem& GetDepValSys();
    std::shared_ptr<IDependencyValidationSystem> CreateDepValSys();

    inline DependencyValidation IDependencyValidationSystem::Make(StringSection<> filename)
    {
        return Make(MakeIteratorRange(&filename, &filename+1));
    }
    inline DependencyValidation IDependencyValidationSystem::Make(const DependentFileState& filestate)
    {
        return Make(MakeIteratorRange(&filestate, &filestate+1));
    }
}
