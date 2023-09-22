// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "IResourceQueryService.h"
#include "MarshalString.h"
#include "CLIXAutoPtr.h"
#include "GUILayerUtil.h"
#include "../ToolsRig/MiscUtils.h"
#include "../../RenderCore/Assets/RawMaterial.h"
#include "../../Assets/IFileSystem.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/IntermediateCompilers.h"
#include "../../OSServices/Log.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Conversion.h"

using namespace System;
using namespace System::Collections::Generic;
using namespace System::ComponentModel::Composition;

#pragma warning(disable:4505) // 'XLEBridgeUtils::Marshal': unreferenced local function has been removed)

#undef new

namespace GUILayer
{
	public ref class ResourceFolderBridge : public IOpaqueResourceFolder
	{
	public:
		property IEnumerable<IOpaqueResourceFolder^>^ Subfolders { 
			virtual IEnumerable<IOpaqueResourceFolder^>^ get() override;
		}
		property bool IsLeaf { 
			virtual bool get() override;
		}
        property IEnumerable<Object^>^ Resources { 
			virtual IEnumerable<Object^>^ get() override;
		}
        property IOpaqueResourceFolder^ Parent { 
			virtual IOpaqueResourceFolder^ get() { return nullptr; }
		}
        property String^ Name {
			virtual String^ get() override { return _name; }
		}

		static ResourceFolderBridge^ BeginFromRoot();
		static ResourceFolderBridge^ BeginFrom(String^);

		ResourceFolderBridge();
		ResourceFolderBridge(::Assets::FileSystemWalker&& walker, String^ name);
		~ResourceFolderBridge();
	private:
		clix::shared_ptr<::Assets::FileSystemWalker> _walker;
		String^ _name;
	};

	static String^ Marshal(StringSection<utf8> str)
	{
		return clix::detail::StringMarshaler<clix::detail::NetFromCxx>::marshalCxxString<clix::E_UTF8>(str.begin(), str.end());
	}

	IEnumerable<IOpaqueResourceFolder^>^ ResourceFolderBridge::Subfolders::get()
	{
		auto result = gcnew List<IOpaqueResourceFolder^>();
		for (auto i=_walker->begin_directories(); i!=_walker->end_directories(); ++i)
			result->Add(gcnew ResourceFolderBridge(*i, clix::marshalString<clix::E_UTF8>(i.Name())));
		return result;
	}

	bool ResourceFolderBridge::IsLeaf::get()
	{
		return _walker->begin_directories() == _walker->end_directories();
	}

	IEnumerable<Object^>^ ResourceFolderBridge::Resources::get()
	{
		auto result = gcnew List<Object^>();
		for (auto i=_walker->begin_files(); i!=_walker->end_files(); ++i) {
			auto markerAndFS = *i;
			static_assert(sizeof(decltype(markerAndFS._marker)::value_type)==1, "Math here assumes markers are vectors of byte types");
			auto res = gcnew array<uint8_t>(int(markerAndFS._marker.size() + sizeof(::Assets::FileSystemId)));
			{
				pin_ptr<uint8_t> pinnedBytes = &res[0];
				*(::Assets::FileSystemId*)pinnedBytes = markerAndFS._fs;
				memcpy(&pinnedBytes[sizeof(::Assets::FileSystemId)], markerAndFS._marker.data(), markerAndFS._marker.size());
			}
			result->Add(res);
		}
		return result;
	}

	ResourceFolderBridge^ ResourceFolderBridge::BeginFromRoot()
	{
		return gcnew ResourceFolderBridge(::Assets::MainFileSystem::BeginWalk(), "<root>");
	}

	ResourceFolderBridge^ ResourceFolderBridge::BeginFrom(String^ base)
	{
		auto nativeBase = clix::marshalString<clix::E_UTF8>(base);
		return gcnew ResourceFolderBridge(::Assets::MainFileSystem::BeginWalk(nativeBase), base);
	}

	ResourceFolderBridge::ResourceFolderBridge()
	{
	}

	ResourceFolderBridge::ResourceFolderBridge(::Assets::FileSystemWalker&& walker, String^ name)
	: _name(name)
	{
		_walker = std::make_shared<::Assets::FileSystemWalker>(std::move(walker));
	}

	ResourceFolderBridge::~ResourceFolderBridge() 
	{
		_walker.reset();
		delete _walker;
	}

	[Export(IResourceQueryService::typeid)]
    [PartCreationPolicy(CreationPolicy::Shared)]
	public ref class ResourceQueryService : public IResourceQueryService
	{
	public:
		virtual Nullable<ResourceDesc> GetDesc(System::Object^ input) override
		{
			array<byte>^ markerAndFS = dynamic_cast<array<byte>^>(input);
			if (!markerAndFS) 
				return Base_GetDesc(input);

			::Assets::IFileSystem::Marker marker;
			::Assets::IFileSystem* fs = nullptr;
			std::basic_string<utf8> mountBase;
			{
				pin_ptr<uint8_t> pinnedBytes = &markerAndFS[0];
				auto fsId = *(const ::Assets::FileSystemId*)pinnedBytes;
				fs = ::Assets::MainFileSystem::GetFileSystem(fsId);
				if (!fs)
					return Base_GetDesc(input);

				auto markerSize = markerAndFS->Length - sizeof(::Assets::FileSystemId);
				marker.resize(markerSize);
				memcpy(marker.data(), &pinnedBytes[sizeof(::Assets::FileSystemId)], markerSize);
				mountBase = ::Assets::MainFileSystem::GetMountPoint(fsId);
			}

			auto desc = fs->TryGetDesc(marker);
			if (desc._snapshot._state != ::Assets::FileSnapshot::State::Normal)
				return Base_GetDesc(input);

			ResourceDesc result;
			result.MountedName = clix::marshalString<clix::E_UTF8>(mountBase) + clix::marshalString<clix::E_UTF8>(desc._mountedName);
			result.NaturalName = clix::marshalString<clix::E_UTF8>(desc._naturalName);
			auto naturalNameSplitter = MakeFileNameSplitter(desc._naturalName);
			result.ShortName = Marshal(naturalNameSplitter.FileAndExtension());
			result.ModificationTime = DateTime::FromFileTime(desc._snapshot._modificationTime);
			result.SizeInBytes = desc._size;
			result.Filesystem = "IFileSystem";

			// figure out what types we can compile this into
			auto temp = naturalNameSplitter.Extension();
			auto targets = GUILayer::Utils::FindCompilationTargets(clix::marshalString<clix::E_UTF8>(temp));
			result.Types = 0u;
			if (targets & (uint32_t)GUILayer::CompilationTargetFlag::Model)
				result.Types |= (uint)ResourceTypeFlags::Model;
			if (targets & (uint32_t)GUILayer::CompilationTargetFlag::Animation)
				result.Types |= (uint)ResourceTypeFlags::Animation;
			if (targets & (uint32_t)GUILayer::CompilationTargetFlag::Skeleton)
				result.Types |= (uint)ResourceTypeFlags::Skeleton;
			if (targets & (uint32_t)GUILayer::CompilationTargetFlag::Material)
				result.Types |= (uint)ResourceTypeFlags::Material;
			return result;
		}

		Nullable<ResourceDesc> Base_GetDesc(Object^ identifier)
        {
            Uri^ resourceUri = dynamic_cast<Uri^>(identifier);
            if (resourceUri != nullptr && resourceUri->IsAbsoluteUri)
            {
                // note that we can't call LocalPath on a relative uri -- and therefore this is only valid for absolute uris

                ResourceDesc result;
                result.NaturalName = resourceUri->LocalPath;
                result.MountedName = resourceUri->LocalPath;
                result.ShortName = System::IO::Path::GetFileName(resourceUri->LocalPath);
                result.Filesystem = "RawFS";
                result.Types = 0;
                try
                {
					System::IO::FileInfo^ fileInfo = gcnew System::IO::FileInfo(resourceUri->LocalPath);
                    result.SizeInBytes = (UInt64)fileInfo->Length;
                    result.ModificationTime = fileInfo->LastWriteTime;
                }
                catch (System::IO::IOException^)
                {
                    result.SizeInBytes = 0;
                    result.ModificationTime = gcnew DateTime();
                }
                return result;
            }
			return {};
        }
	};


}