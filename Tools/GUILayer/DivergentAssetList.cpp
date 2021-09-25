// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "NativeEngineDevice.h"
#include "DivergentAssetList.h"
#include "EngineDevice.h"
#include "ExportedNativeTypes.h"
#include "../ToolsRig/DivergentAsset.h"
#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Assets/IFileSystem.h"
#include "../../OSServices/RawFS.h"
#include "../../Utility/Streams/StreamTypes.h"
#include "../../Utility/Streams/Data.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/OutputStreamFormatter.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Threading/ThreadingUtils.h"
#include "../../Utility/Conversion.h"
#include "MarshalString.h"
#include "CLIXAutoPtr.h"

#include "../../RenderCore/Assets/RawMaterial.h"
#include <sstream>

using namespace System;
using namespace System::Collections::Generic;

namespace GUILayer
{
	static String^ GetAssetTypeName(uint64_t typeCode)
	{
		using MatType = RenderCore::Assets::RawMaterial;
		if (typeCode == typeid(MatType).hash_code()) {
			return "Material";
		}
		return String::Empty;
	}

	private ref class ResourceImages
	{
	public:
		static System::Drawing::Image^ s_materialImage = nullptr;
	};

	static System::Drawing::Image^ GetAssetTypeImage(uint64_t typeCode)
	{
		using MatType = RenderCore::Assets::RawMaterial;
		if (typeCode == typeid(MatType).hash_code()) {
			if (!ResourceImages::s_materialImage) {
				System::IO::Stream^ stream = System::Reflection::Assembly::GetExecutingAssembly()->GetManifestResourceStream("materialS.png");
				ResourceImages::s_materialImage = System::Drawing::Image::FromStream(stream);
			}
			return ResourceImages::s_materialImage;
		}
		return nullptr;
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    public ref class AssetItem
    {
    public:
        property virtual System::String^ Label;
		PendingSaveList::Entry^ _pendingSave; 
		uint64_t _idInAssetHeap;
        
        property virtual PendingSaveList::Action Action
        {
			PendingSaveList::Action get()
            { 
                return _pendingSave ? _pendingSave->_action : PendingSaveList::Action::Ignore;
            }
            void set(PendingSaveList::Action action)
            {
                if (_pendingSave) {
                    _pendingSave->_action = action; 
                }
            }
        }

        AssetItem(const ::ToolsRig::DivergentAssetManager::Record& record, PendingSaveList::Entry^ pendingSave) 
		: _idInAssetHeap(record._idInAssetHeap)
		, _pendingSave(pendingSave)
        {
            Label = clix::marshalString<clix::E_UTF8>(record._identifier);
        }

		AssetItem(String^ label, PendingSaveList::Entry^ pendingSave) 
		{
			Label = label;
			_pendingSave = pendingSave;
		}
    };

    public ref class AssetTypeItem
    {
    public:
        property virtual System::String^ Label;
		property virtual System::Drawing::Image^ Icon;

		uint64_t _typeCode;
        List<AssetItem^>^ _children;

		PendingSaveList::Action _action;

        property virtual PendingSaveList::Action Action
        {
			PendingSaveList::Action get()
            {
                return _action;
            }

            void set(PendingSaveList::Action action)
            {
				_action = action;
                if (_children!=nullptr)
                {
                    for each(auto child in _children)
                        child->Action = action;
                }
            }
        }

        AssetTypeItem(uint64_t typeCode)
		: _typeCode(typeCode)
        {
            _children = gcnew List<AssetItem^>();
			Label = GetAssetTypeName(typeCode);
			if (String::IsNullOrEmpty(Label))
				// Label = clix::marshalString<clix::E_UTF8>(set.GetTypeName());
				Label = Convert::ToString(typeCode);
			Icon = GetAssetTypeImage(typeCode);
            _action = PendingSaveList::Action::Save;
        }

		AssetTypeItem(System::String^ label, System::Drawing::Image^ icon)
		{
			_children = gcnew List<AssetItem^>();
			Label = label;
			Icon = icon;
			_action = PendingSaveList::Action::Save;
		}
    };

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    public ref class TransactionItem
    {
    public:
        property virtual System::String^ Label;

        TransactionItem(const ToolsRig::IDivergentTransaction& transaction)
        {
            Label = clix::marshalString<clix::E_UTF8>(transaction.GetName());
        }
    };

    System::Collections::IEnumerable^ DivergentAssetList::GetChildren(TreePath^ treePath)
    {
        // we will build a tree like this:
        //      <asset type>
        //          <asset name>
        //              <transaction>
        //              <transaction>
        //          <asset name>
        //              <transaction>
        //              <transaction>
        //  ..etc..

        if (treePath->IsEmpty()) {

            auto result = gcnew List<AssetTypeItem^>();

			auto divAssets = ToolsRig::DivergentAssetManager::GetInstance().GetAssets();
			for (const auto&d:divAssets) {
				if (!d._hasChanges) continue;

				AssetTypeItem^ item = nullptr;
				for each (AssetTypeItem^ i in result) {
					if (i->_typeCode == d._typeCode) {
						item = i;
						break;
					}
				}
				if (!item) {
					item = gcnew AssetTypeItem(d._typeCode);
					result->Add(item);
				}
				item->_children->Add(gcnew AssetItem(d, _saveList->GetEntry(d._typeCode, d._idInAssetHeap)));
			}

            return result;

        } else {

            auto lastItem = treePath->LastNode;
            {
                auto item = dynamic_cast<AssetTypeItem^>(lastItem);
                if (item) {
					return item->_children;
                }
            }

#if 0
            if (_undoQueue) {
                auto item = dynamic_cast<AssetItem^>(lastItem);
                if (item) {
                        // expecting the list of committed transactions on this asset
                        // we need to search through the undo queue for all of the transactions
                        // that match this asset type code and id. We'll add the most recent
                        // one first.
                    auto result = gcnew List<TransactionItem^>();
                    auto id = item->_id;
                    auto typeCode = item->_set->GetTypeCode();
                    auto undoQueueCount = _undoQueue->GetCount();
                    for (unsigned c = 0; c < undoQueueCount; ++c) {
                        auto* trans = _undoQueue->GetTransaction(undoQueueCount - 1 - c);
                        if (!trans) continue;
                        if (trans->GetAssetId() == id && trans->GetTypeCode() == typeCode) {
                            result->Add(gcnew TransactionItem(*trans));
                        }
                    }

                    return result;
                }
            }
#endif

        }

        return nullptr;
    }

    bool DivergentAssetList::IsLeaf(TreePath^ treePath)
    {
        auto item = dynamic_cast<AssetItem^>(treePath->LastNode);
        return item != nullptr;
    }


    DivergentAssetList::DivergentAssetList(EngineDevice^ engine, PendingSaveList^ saveList)
    {
        _assetSets = &engine->GetNative().GetAssetServices()->GetAssetSets();
        _saveList = saveList;
    }

    DivergentAssetList::~DivergentAssetList() {}
 


    void PendingSaveList::Add(uint64_t typeCode, uint64_t id, Entry^ entry)
    {
        for each(auto e in _entries)
            if (e._typeCode == typeCode && e._id == id) {
                assert(0);
                return;
            }
        
        auto e = gcnew E();
        e->_typeCode = typeCode;
        e->_id = id;
        e->_entry = entry;
        _entries->Add(*e);
    }

    auto PendingSaveList::GetEntry(uint64_t typeCode, uint64_t id) -> Entry^
    {
        for each(auto e in _entries)
            if (e._typeCode == typeCode && e._id == id) {
                return e._entry;
            }

        return nullptr;
    }

    PendingSaveList::PendingSaveList()
    {
        _entries = gcnew List<E>();
    }

    PendingSaveList::~PendingSaveList()
    {}

	static ::Assets::Blob TryLoadOriginalFileAsBlob(StringSection<> filename)
	{
		TRY{
			auto file = ::Assets::MainFileSystem::OpenFileInterface(filename, "rb");

			file->Seek(0, OSServices::FileSeekAnchor::End);
			size_t size = file->TellP();
			file->Seek(0);

			auto result = std::make_shared<std::vector<uint8_t>>(size);
			file->Read(result->data(), size);
			return result;
		} CATCH(const std::exception& e) {
			std::stringstream str;
			str << "Error while opening input file " << filename.AsString() << ". Exception message follows: " << e.what();
			auto s = str.str();
			return ::Assets::AsBlob(MakeIteratorRange(s));
		} CATCH(...) {
			std::stringstream str;
			str << "Error while opening input file " << filename.AsString() << ". Unknown exception type.";
			auto s = str.str();
			return ::Assets::AsBlob(MakeIteratorRange(s));
		} CATCH_END
	}

    array<Byte>^ AsByteArray(const uint8* begin, const uint8* end)
    {
        using System::Runtime::InteropServices::Marshal;

        array<Byte>^ result = gcnew array<Byte>(int(end-begin));
        Marshal::Copy(IntPtr(const_cast<uint8*>(begin)), result, 0, result->Length);
        return result;
    }

	array<Byte>^ AsByteArray(const ::Assets::Blob& blob)
	{
		return AsByteArray((const uint8*)AsPointer(blob->begin()), (const uint8*)AsPointer(blob->end()));
	}

    static auto DeserializeAllMaterials(InputStreamFormatter<utf8>& formatter, const ::Assets::DirectorySearchRules& searchRules)
        -> std::vector<std::pair<::Assets::rstring, RenderCore::Assets::RawMaterial>>
    {
        std::vector<std::pair<::Assets::rstring, RenderCore::Assets::RawMaterial>> result;

        using Blob = InputStreamFormatter<utf8>::Blob;
        StringSection<> name;
        while (formatter.TryKeyedItem(name)) {
            switch(formatter.PeekNext()) {
            case Blob::BeginElement:
                {
                    formatter.TryBeginElement();
                    RenderCore::Assets::RawMaterial mat(formatter, searchRules, ::Assets::DependencyValidation());
                    result.emplace_back(
                        std::make_pair(
                            Conversion::Convert<::Assets::rstring>(name.AsString()), std::move(mat)));

                    if (!formatter.TryEndElement())
                        Throw(Utility::FormatException("Expecting end element", formatter.GetLocation()));
                }
                break;

            case Blob::Value:
                RequireStringValue(formatter);
                break;

            default:
                Throw(Utility::FormatException("Expected either value or element", formatter.GetLocation()));
            }
        }

        return result;
    }

    static void SerializeAllMaterials(OutputStreamFormatter& formatter, std::vector<std::pair<std::string, RenderCore::Assets::RawMaterial>>& mats)
    {
        for (const auto&m:mats) {
            auto ele = formatter.BeginKeyedElement(m.first);
            SerializationOperator(formatter, m.second);
            formatter.EndElement(ele);
        }
    }

    static void MergeAndSerialize(
        OutputStreamFormatter& output,
        IteratorRange<const void*> originalFile,
        StringSection<::Assets::ResChar> filename,
        StringSection<::Assets::ResChar> section,
        const RenderCore::Assets::RawMaterial& mat)
    {
            //  Sometimes mutliple assets will write to different parts of the same
            //  file. It's a bit wierd, but we want the before and after parts to
            //  only show changes related to this particular asset
        using namespace RenderCore::Assets;

        std::vector<std::pair<::Assets::rstring, RenderCore::Assets::RawMaterial>> preMats;
        if (!originalFile.empty()) {
            auto searchRules = ::Assets::DefaultDirectorySearchRules(filename);
            InputStreamFormatter<utf8> formatter(originalFile);
            preMats = DeserializeAllMaterials(formatter, searchRules);
        }

        auto i = std::find_if(
            preMats.begin(), preMats.end(),
            [section](const std::pair<std::string, RenderCore::Assets::RawMaterial>& e)
            { return XlEqString(MakeStringSection(e.first), section); });

        if (i != preMats.end()) {
            i->second = mat;
        } else {
            preMats.push_back(std::make_pair(section.AsString(), mat));
        }

        SerializeAllMaterials(output, preMats);
    }

	struct AssetSaveEntry
	{
		::Assets::Blob		_originalFile;
		::Assets::Blob		_newFile;
		::Assets::rstring	_name;
	};

	template<typename Asset>
		const std::shared_ptr<Asset>& GetWorkingCopy(const ToolsRig::IDivergentAsset& divAsset)
		{
			return checked_cast<const ToolsRig::DivergentAsset<Asset>*>(&divAsset)->GetWorkingAsset();
		}

	static AssetSaveEntry BuildAssetSaveEntry(uint64_t typeCode, const StringSection<::Assets::ResChar> identifier, const ToolsRig::IDivergentAsset& divAsset)
	{
		// HACK -- special case for RawMaterial objects!
		if (typeCode == typeid(RenderCore::Assets::RawMaterial).hash_code()) {
			auto splitName = MakeFileNameSplitter(identifier);
			auto originalFile = TryLoadOriginalFileAsBlob(splitName.AllExceptParameters());

			MemoryOutputStream<utf8> strm;
			OutputStreamFormatter fmtter(strm);
			MergeAndSerialize(fmtter, MakeIteratorRange(*originalFile),
				splitName.AllExceptParameters(), splitName.Parameters(),
				*GetWorkingCopy<RenderCore::Assets::RawMaterial>(divAsset));
			auto newFile = ::Assets::AsBlob(MakeIteratorRange(strm.GetBuffer().Begin(), strm.GetBuffer().End()));

			return AssetSaveEntry { std::move(originalFile), std::move(newFile), splitName.AllExceptParameters().AsString() };
		}

		return AssetSaveEntry {};
	}

    PendingSaveList^ PendingSaveList::Create()
    {
        auto result = gcnew PendingSaveList();

            //  Find all divergent assets that can be saved, and 
            //  add in an entry for them here. Each entry should
            //  contain the destination filename to write to, the
            //  previous contents of the file and the new contents.
            //  Typically our divergent assets should be small, and
            //  should serialise quickly... But if we have many large
            //  assets, this could get expensive quickly!

		auto& divAssetMan = ToolsRig::DivergentAssetManager::GetInstance();
		for (const auto&d : divAssetMan.GetAssets()) {
			if (!d._hasChanges) continue;

			auto asset = divAssetMan.GetAsset(d._typeCode, d._idInAssetHeap);
			auto saveEntry = BuildAssetSaveEntry(d._typeCode, MakeStringSection(d._identifier), *asset);
			if (!saveEntry._newFile || saveEntry._newFile->empty()) continue;

			result->Add(
				d._typeCode, d._idInAssetHeap,
				gcnew PendingSaveList::Entry(
					clix::marshalString<clix::E_UTF8>(saveEntry._name),
					AsByteArray(saveEntry._originalFile),
					AsByteArray(saveEntry._newFile)));
		}

        return result;
    }

    auto PendingSaveList::Commit() -> CommitResult^
    {
		std::stringstream errorMessages;

		using namespace RenderCore::Assets;
		auto& divAssetMan = ToolsRig::DivergentAssetManager::GetInstance();
		for (const auto&d : divAssetMan.GetAssets()) {
			if (!d._hasChanges) continue;

			// only RawMaterial objects supported here currently
			if (d._typeCode != typeid(RawMaterial).hash_code())
				continue;

            auto entry = GetEntry(d._typeCode, d._idInAssetHeap);
            if (!entry || entry->_action == Action::Ignore) continue;

			auto asset = divAssetMan.GetAsset(d._typeCode, d._idInAssetHeap);
			if (entry->_action == Action::Abandon) {
				asset->AbandonChanges();
				continue;
			}

			assert(entry->_action == Action::Save);
            
                //  Sometimes mutliple assets will write to different parts of the same
                //  file. It's a bit wierd, but we want the before and after parts to
                //  only show changes related to this particular asset
			auto filename = MakeStringSection(d._identifier);
            auto splitName = MakeFileNameSplitter(filename);
            auto originalFile = TryLoadOriginalFileAsBlob(splitName.AllExceptParameters());
            
            MemoryOutputStream<utf8> strm;
            OutputStreamFormatter fmtter(strm);
            MergeAndSerialize(fmtter, MakeIteratorRange(*originalFile), 
                splitName.AllExceptParameters(), splitName.Parameters(), 
				*GetWorkingCopy<RawMaterial>(*asset));

			auto dstFile = splitName.AllExceptParameters().AsString();
            TRY
            {
				auto outputFile = ::Assets::MainFileSystem::OpenFileInterface(dstFile.c_str(), "wb");
				outputFile->Write(strm.GetBuffer().Begin(), 1, size_t(strm.GetBuffer().End()) - size_t(strm.GetBuffer().Begin()));

				// abandon changes now to allow us to reload the asset from disk
				// asset->AbandonChanges();
            } CATCH(const std::exception& e) {
				errorMessages << "Error while opening input file " << filename << ". Exception message follows: " << std::endl << e.what() << std::endl;
			} CATCH (...) {
				errorMessages << "Error while opening input file " << filename.AsString() << ". Unknown exception type." << std::endl;
            } CATCH_END
        }

		auto result = gcnew CommitResult;
		result->ErrorMessages = clix::marshalString<clix::E_UTF8>(errorMessages.str());
		return result;
    }

	bool PendingSaveList::HasModifiedAssets()
    {
		auto& divAssetMan = ToolsRig::DivergentAssetManager::GetInstance();
		for (const auto&d : divAssetMan.GetAssets())
			if (d._hasChanges) return true;
		return false;
    }
}

