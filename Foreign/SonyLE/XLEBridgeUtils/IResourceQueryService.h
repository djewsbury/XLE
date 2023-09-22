// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using namespace System;
using namespace System::Collections::Generic;

namespace XLEBridgeUtils
{
    public ref class IOpaqueResourceFolder
    {
    public:
        property IEnumerable<IOpaqueResourceFolder^>^ Subfolders { 
            virtual IEnumerable<IOpaqueResourceFolder^>^ get() = 0;
        }
        property IEnumerable<Object^>^ Resources {
            virtual IEnumerable<Object^>^ get() = 0;
        }
        property bool IsLeaf { 
            virtual bool get() = 0;
        }
        property String^ Name { 
            virtual String^ get() = 0;
        }
    };

    public enum class ResourceTypeFlags : unsigned
    {
        Model = 1 << 0,
        Animation = 1 << 1,
        Material = 1 << 2,
        Texture = 1 << 3,
        Skeleton = 1 << 4,
    };

    public value struct ResourceDesc
    {
        property String^ ShortName;
        property String^ MountedName;
        property String^ NaturalName;
        property String^ Filesystem;
        property UInt64 SizeInBytes;
        property unsigned Types;                  // ResourceTypeFlags
        property DateTime^ ModificationTime;
    };

    public ref class IResourceQueryService
    {
    public:
        virtual Nullable<ResourceDesc> GetDesc(Object^ identifier) = 0;
    };
}

