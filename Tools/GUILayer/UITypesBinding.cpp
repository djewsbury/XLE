// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "UITypesBinding.h"
#include "EngineForward.h"
#include "ExportedNativeTypes.h"
#include "../ToolsRig/ModelVisualisation.h"
#include "../ToolsRig/VisualisationUtils.h"
#include "../ToolsRig/DivergentAsset.h"
#include "../../PlatformRig/DebuggingDisplays/InvalidAssetDisplay.h"        // for ITrackedAssetList
#include "../../SceneEngine/IScene.h"
#include "../../RenderCore/Assets/MaterialScaffold.h"
#include "../../RenderCore/Assets/RawMaterial.h"
#include "../../Assets/AssetUtils.h"
#include "../../Assets/AssetServices.h"
#include "../../Assets/AssetSetManager.h"
#include "../../Assets/AssetsCore.h"
#include "../../Assets/ConfigFileContainer.h"
#include "../../Assets/AssetHeap.h"
#include "../../Math/MathSerialization.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Conversion.h"
#include <msclr/auto_gcroot.h>
#include <iomanip>

#pragma warning(disable:4512)

using namespace System;

namespace GUILayer
{

///////////////////////////////////////////////////////////////////////////////////////////////////

    class InvalidatePropertyGrid : public OSServices::OnChangeCallback
    {
    public:
        void    OnChange();

        InvalidatePropertyGrid(System::Windows::Forms::PropertyGrid^ linked);
        ~InvalidatePropertyGrid();
    protected:
        msclr::auto_gcroot<System::Windows::Forms::PropertyGrid^> _linked;
    };

    void    InvalidatePropertyGrid::OnChange()
    {
        if (_linked.get()) {
            _linked->Refresh();
        }
    }

    InvalidatePropertyGrid::InvalidatePropertyGrid(System::Windows::Forms::PropertyGrid^ linked) : _linked(linked) {}
    InvalidatePropertyGrid::~InvalidatePropertyGrid() {}

	VisCameraSettings::VisCameraSettings(std::shared_ptr<ToolsRig::VisCameraSettings> attached)
	{
		_object = std::move(attached);
	}
	VisCameraSettings::VisCameraSettings() { _object = std::make_shared<ToolsRig::VisCameraSettings>(); }
	VisCameraSettings::~VisCameraSettings() { _object.reset(); }

///////////////////////////////////////////////////////////////////////////////////////////////////

	std::shared_ptr<ToolsRig::VisOverlaySettings> VisOverlaySettings::ConvertToNative()
	{
		auto result = std::make_shared<ToolsRig::VisOverlaySettings>();
		result->_colourByMaterial = (unsigned)ColourByMaterial;
		result->_skeletonMode = (unsigned)SkeletonMode;
		result->_drawWireframe = DrawWireframe;
		result->_drawNormals = DrawNormals;
		return result;
	}

	VisOverlaySettings^ VisOverlaySettings::ConvertFromNative(const ToolsRig::VisOverlaySettings& input)
	{
		VisOverlaySettings^ result = gcnew VisOverlaySettings;
		result->ColourByMaterial = (ColourByMaterialType)input._colourByMaterial;
		result->SkeletonMode = (SkeletonModes)input._skeletonMode;
		result->DrawWireframe = input._drawWireframe;
		result->DrawNormals = input._drawNormals;
		return result;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

    System::String^ VisMouseOver::IntersectionPt::get()
    {
        if (_object->_hasMouseOver) {
            return clix::marshalString<clix::E_UTF8>(
                std::string(StringMeld<64>()
                    << std::setprecision(5)
                    << _object->_intersectionPt[0] << ","
                    << _object->_intersectionPt[1] << ","
                    << _object->_intersectionPt[2]));
        } else {
            return "<<no intersection>>";
        }
    }

    unsigned VisMouseOver::DrawCallIndex::get() 
    { 
        if (_object->_hasMouseOver) {
            return _object->_drawCallIndex;
        } else {
            return ~unsigned(0x0);
        }
    }

	static String^ DescriptiveMaterialName(String^ fullName)
    {
        if (String::IsNullOrEmpty(fullName) == 0 || fullName->StartsWith("<")) return fullName;
        auto split = fullName->Split(';');
        if (split && split->Length > 0) {
            auto s = split[split->Length-1];
            int index = s->LastIndexOf(':');
            return s->Substring((index>=0) ? (index+1) : 0);
        }
        return fullName;
    }

    System::String^ VisMouseOver::MaterialName::get() 
    {
        auto fullName = FullMaterialName;
        if (fullName)
            return DescriptiveMaterialName(fullName);
        return "<<no material>>";
    }

    System::String^ VisMouseOver::ModelName::get() 
    {
		if (_scene.get()) {
			auto* visContent = dynamic_cast<ToolsRig::IVisContent*>(_scene.get());
			if (visContent)
				return clix::marshalString<clix::E_UTF8>(visContent->GetDrawCallDetails(_object->_drawCallIndex, _object->_materialGuid)._modelName);
		}
		return nullptr;
    }

    bool VisMouseOver::HasMouseOver::get()
    {
        return _object->_hasMouseOver;
    }

    System::String^ VisMouseOver::FullMaterialName::get()
    {
		if (_scene.get()) {
			auto* visContent = dynamic_cast<ToolsRig::IVisContent*>(_scene.get());
			if (visContent)
				return clix::marshalString<clix::E_UTF8>(visContent->GetDrawCallDetails(_object->_drawCallIndex, _object->_materialGuid)._materialName);
		}
		return nullptr;
    }

    uint64 VisMouseOver::MaterialBindingGuid::get()
    {
        if (_object->_hasMouseOver) {
            return _object->_materialGuid;
        } else {
            return ~uint64(0x0);
        }
    }

    void VisMouseOver::AttachCallback(System::Windows::Forms::PropertyGrid^ callback)
    {
        _object->_changeEvent._callbacks.push_back(
            std::shared_ptr<OSServices::OnChangeCallback>(new InvalidatePropertyGrid(callback)));
    }

    VisMouseOver::VisMouseOver(
        std::shared_ptr<ToolsRig::VisMouseOver> attached,
        std::shared_ptr<SceneEngine::IScene> scene)
    {
        _object = std::move(attached);
        _scene = scene;
    }

    VisMouseOver::VisMouseOver()
    {
        _object = std::make_shared<ToolsRig::VisMouseOver>();
    }

    VisMouseOver::~VisMouseOver() 
    { 
        _object.reset(); 
        _scene.reset(); 
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

	System::Collections::Generic::IEnumerable<VisAnimationState::AnimationDetails^>^ VisAnimationState::AnimationList::get()
	{
		auto result = gcnew System::Collections::Generic::List<AnimationDetails^>();
		for (const auto&a:_animState->_animationList) {
			AnimationDetails^ animDetails = gcnew AnimationDetails;
			animDetails->Name = clix::marshalString<clix::E_UTF8>(a._name);
			animDetails->BeginTime = a._beginTime;
			animDetails->EndTime = a._endTime;
			result->Add(animDetails);
		}
		return result;
	}

    System::String^ VisAnimationState::ActiveAnimation::get()
	{
		return clix::marshalString<clix::E_UTF8>(_animState->_activeAnimation);
	}

	void VisAnimationState::ActiveAnimation::set(System::String^ value)
	{
		_animState->_activeAnimation = clix::marshalString<clix::E_UTF8>(value);
	}

    float VisAnimationState::AnimationTimeAtAnchor::get()
	{
		return _animState->_animationTime;
	}

	void VisAnimationState::AnimationTimeAtAnchor::set(float value)
	{
		_animState->_animationTime = value;
	}

	float VisAnimationState::CurrentAnimationTime::get()
	{
        if (_animState->_state == ToolsRig::VisAnimationState::State::Playing) {
            auto now = std::chrono::steady_clock::now();
            return _animState->_animationTime + std::chrono::duration_cast<std::chrono::microseconds>(now - _animState->_anchorTime).count() / 1000000.0f;
        } else {
            return _animState->_animationTime;
        }
	}
	
	void VisAnimationState::CurrentAnimationTime::set(float value)
	{
		_animState->_animationTime = value;
        _animState->_anchorTime = std::chrono::steady_clock::now();
	}

    void VisAnimationState::RefreshAnimationTimeAnchor()
    {
        auto now = std::chrono::steady_clock::now();
        if (_animState->_state == ToolsRig::VisAnimationState::State::Playing)
            _animState->_animationTime = _animState->_animationTime + std::chrono::duration_cast<std::chrono::microseconds>(now - _animState->_anchorTime).count() / 1000000.0f;
        _animState->_anchorTime = now;
    }

	VisAnimationState::State VisAnimationState::CurrentState::get()
	{
		switch (_animState->_state) {
		case ToolsRig::VisAnimationState::State::Playing: return State::Playing;
		case ToolsRig::VisAnimationState::State::BindPose: return State::BindPose;
		default: return State::Stopped;
		}
	}

	void VisAnimationState::CurrentState::set(VisAnimationState::State value)
	{
		switch (value) {
		case State::Playing: _animState->_state = ToolsRig::VisAnimationState::State::Playing; break;
		case State::BindPose: _animState->_state = ToolsRig::VisAnimationState::State::BindPose; break;
		default: _animState->_state = ToolsRig::VisAnimationState::State::Stopped; break;
		}
	}

	class DelegateChangeEvent : public OSServices::OnChangeCallback
	{
	public:
		void    OnChange()
		{
			(_del.get())();
		}

		DelegateChangeEvent(VisAnimationState::OnChangedCallback^ del) : _del(del) {}
		~DelegateChangeEvent() {}
	private:
		msclr::auto_gcroot<VisAnimationState::OnChangedCallback^> _del;
	};

	void VisAnimationState::AddOnChangedCallback(OnChangedCallback^ del)
	{
		_animState->_changeEvent._callbacks.push_back(
			std::make_shared<DelegateChangeEvent>(del));
	}

    VisAnimationState::VisAnimationState(const std::shared_ptr<ToolsRig::VisAnimationState>& attached)
	: _animState(attached)
	{
	}

	VisAnimationState::VisAnimationState() {}
	VisAnimationState::~VisAnimationState() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename NameType, typename ValueType>
        NameType PropertyPair<NameType, ValueType>::Name::get() { return _name; }

    template<typename NameType, typename ValueType>
        void PropertyPair<NameType, ValueType>::Name::set(NameType newValue)
    {
        _name = newValue;
        NotifyPropertyChanged("Name");
    }

    template<typename NameType, typename ValueType>
        ValueType PropertyPair<NameType, ValueType>::Value::get() { return _value; } 

    template<typename NameType, typename ValueType>
        void PropertyPair<NameType, ValueType>::Value::set(ValueType newValue)
    {
        _value = newValue;
        NotifyPropertyChanged("Value");
    }

    template<typename NameType, typename ValueType>
        void PropertyPair<NameType, ValueType>::NotifyPropertyChanged(System::String^ propertyName)
    {
        PropertyChanged(this, gcnew PropertyChangedEventArgs(propertyName));
    }

    public ref class BindingConv
    {
    public:
        static BindingList<StringStringPair^>^ AsBindingList(const ParameterBox& paramBox);
        static ParameterBox AsParameterBox(BindingList<StringStringPair^>^);
        static ParameterBox AsParameterBox(BindingList<StringIntPair^>^);
    };

    BindingList<StringStringPair^>^ BindingConv::AsBindingList(const ParameterBox& paramBox)
    {
        auto result = gcnew BindingList<StringStringPair^>();
        std::vector<std::pair<const utf8*, std::string>> stringTable;
        BuildStringTable(stringTable, paramBox);

        for (auto i=stringTable.cbegin(); i!=stringTable.cend(); ++i) {
            result->Add(
                gcnew StringStringPair(
                    clix::marshalString<clix::E_UTF8>(i->first),
                    clix::marshalString<clix::E_UTF8>(i->second)));
        }

        return result;
    }

    ParameterBox BindingConv::AsParameterBox(BindingList<StringStringPair^>^ input)
    {
        ParameterBox result;
        for each(auto i in input) {
                //  We get items with null names when they are being added, but
                //  not quite finished yet. We have to ignore in this case.
            if (i->Name && i->Name->Length > 0) {
                result.SetParameter(
                    (const utf8*)clix::marshalString<clix::E_UTF8>(i->Name).c_str(),
                    i->Value ? clix::marshalString<clix::E_UTF8>(i->Value).c_str() : nullptr);
            }
        }
        return result;
    }

    ParameterBox BindingConv::AsParameterBox(BindingList<StringIntPair^>^ input)
    {
        ParameterBox result;
        for each(auto i in input) {
                //  We get items with null names when they are being added, but
                //  not quite finished yet. We have to ignore in this case.
            if (i->Name && i->Name->Length > 0) {
                result.SetParameter(
                    (const utf8*)clix::marshalString<clix::E_UTF8>(i->Name).c_str(),
                    i->Value);
            }
        }
        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    BindingList<StringStringPair^>^ 
        RawMaterial::MaterialParameterBox::get()
    {
        if (!_underlying) { return nullptr; }
        if (!_materialParameterBox) {
            _materialParameterBox = BindingConv::AsBindingList(_underlying->GetWorkingAsset()->_selectors);
            _materialParameterBox->ListChanged += 
                gcnew ListChangedEventHandler(
                    this, &RawMaterial::ParameterBox_Changed);
            _materialParameterBox->AllowNew = true;
            _materialParameterBox->AllowEdit = true;
			_materialParameterBox->AllowRemove = true;
        }
        return _materialParameterBox;
    }

    BindingList<StringStringPair^>^ RawMaterial::ShaderConstants::get()
    {
        if (!_underlying) { return nullptr; }
        if (!_shaderConstants) {
            _shaderConstants = BindingConv::AsBindingList(_underlying->GetWorkingAsset()->_uniforms);
            _shaderConstants->ListChanged += 
                gcnew ListChangedEventHandler(
                    this, &RawMaterial::ParameterBox_Changed);
            _shaderConstants->AllowNew = true;
            _shaderConstants->AllowEdit = true;
			_shaderConstants->AllowRemove = true;
        }
        return _shaderConstants;
    }

    BindingList<StringStringPair^>^ RawMaterial::ResourceBindings::get()
    {
        if (!_underlying) { return nullptr; }
        if (!_resourceBindings) {
            _resourceBindings = BindingConv::AsBindingList(_underlying->GetWorkingAsset()->_resources);
            _resourceBindings->ListChanged += 
                gcnew ListChangedEventHandler(
                    this, &RawMaterial::ResourceBinding_Changed);
            _resourceBindings->AllowNew = true;
            _resourceBindings->AllowEdit = true;
			_resourceBindings->AllowRemove = true;
        }
        return _resourceBindings;
    }

    void RawMaterial::ParameterBox_Changed(System::Object^ obj, ListChangedEventArgs^e)
    {
            //  Commit these changes back to the native object by re-creating the parameter box
            //  Ignore a couple of cases... 
            //      - moving an item is unimportant
            //      - added a new item with a null name (this occurs when the new item
            //          hasn't been fully filled in yet)
            //   Similarly, don't we really need to process a removal of an item with 
            //   an empty name.. but there's no way to detect this case
        if (e->ListChangedType == ListChangedType::ItemMoved) {
            return;
        }

        using Box = BindingList<StringStringPair^>;

        if (e->ListChangedType == ListChangedType::ItemAdded) {
            assert(e->NewIndex < ((Box^)obj)->Count);
			// When a new item is added, prevent creating the underlying parameter before the
			// name of the new entry is fully filled in (otherwise we end up in a sort of 
			// partial constructed state)
            if (!((Box^)obj)[e->NewIndex]->Name || ((Box^)obj)[e->NewIndex]->Name->Length <= 0) {
                return;
            }
        }

        if (!!_underlying) {
			bool isMatParams = obj == _materialParameterBox;
			bool isMatConstants = obj == _shaderConstants;
            if (isMatParams) {
                auto transaction = _underlying->Transaction_Begin("Material parameter");
                if (transaction) {
                    transaction->GetAsset()._selectors = BindingConv::AsParameterBox((BindingList<StringStringPair^>^)obj);
                    _transId = transaction->Commit();
                }
            } else if (isMatConstants) {
                auto transaction = _underlying->Transaction_Begin("Material constant");
                if (transaction) {
                    transaction->GetAsset()._uniforms = BindingConv::AsParameterBox((BindingList<StringStringPair^>^)obj);
					_transId = transaction->Commit();
                }
            }
        }
    }

    void RawMaterial::ResourceBinding_Changed(System::Object^ obj, ListChangedEventArgs^ e)
    {
        if (e->ListChangedType == ListChangedType::ItemMoved) {
            return;
        }

        using Box = BindingList<StringStringPair^>;

        if (e->ListChangedType == ListChangedType::ItemAdded) {
            assert(e->NewIndex < ((Box^)obj)->Count);
            if (!((Box^)obj)[e->NewIndex]->Name || ((Box^)obj)[e->NewIndex]->Name->Length > 0) {
                return;
            }
        }

        if (!!_underlying) {
            if (obj == _resourceBindings) {
				auto transaction = _underlying->Transaction_Begin("Resource Binding");
				if (transaction) {
					transaction->GetAsset()._resources = BindingConv::AsParameterBox((BindingList<StringStringPair^>^)obj);
					_transId = transaction->Commit();
				}
			}
        }
    }

    System::String^ RawMaterial::BuildInheritanceList()
    {
        if (!!_underlying) {
            auto& asset = *_underlying->GetWorkingAsset();
            auto searchRules = ::Assets::DefaultDirectorySearchRules(
                MakeStringSection(clix::marshalString<clix::E_UTF8>(Filename)));
            
            System::String^ result = "";
            auto inheritted = asset.ResolveInherited(searchRules);
            for (auto i = inheritted.cbegin(); i != inheritted.cend(); ++i) {
                if (!String::IsNullOrEmpty(result)) result += ";";
                result += clix::marshalString<clix::E_UTF8>(*i);
            }
            return result;
        }
        return nullptr;
    }

    void RawMaterial::AddInheritted(String^ item)
    {
            // we could consider converting the filename "item"
            // into a path relative to the main material file here..?
        auto transaction = _underlying->Transaction_Begin("Add inheritted");
        if (transaction)
            transaction->GetAsset()._inherit.push_back(clix::marshalString<clix::E_UTF8>(item));
    }

    void RawMaterial::RemoveInheritted(String^ item)
    {
        assert(0); // not implemented!
    }

	void RawMaterial::MergeIn(RawMaterial^ src)
	{
		auto transaction = _underlying->Transaction_Begin("Merge In");
		if (transaction) {
			transaction->GetAsset().MergeIn(*_underlying->GetWorkingAsset());
			_transId = transaction->Commit();
		}
	}

	bool RawMaterial::TryGetConstantInt(String^ label, [Out] int% value)
	{
		auto param = _underlying->GetWorkingAsset()->_uniforms.GetParameter<int>(
			MakeStringSection(clix::marshalString<clix::E_UTF8>(label)));
		if (!param.has_value())
			return false;
		
		value = param.value();
		return true;
	}

	bool RawMaterial::TryGetConstantFloat(String^ label, [Out] float% value)
	{
		auto param = _underlying->GetWorkingAsset()->_uniforms.GetParameter<float>(
			MakeStringSection(clix::marshalString<clix::E_UTF8>(label)));
		if (!param.has_value())
			return false;
		
		value = param.value();
		return true;
	}

	bool RawMaterial::TryGetConstantBool(String^ label, [Out] bool% value)
	{
		auto param = _underlying->GetWorkingAsset()->_uniforms.GetParameter<bool>(
			MakeStringSection(clix::marshalString<clix::E_UTF8>(label)));
		if (!param.has_value())
			return false;
		
		value = param.value();
		return true;
	}

	bool RawMaterial::TryGetConstantFloat2(String^ label, array<float>^ value)
	{
		auto param = _underlying->GetWorkingAsset()->_uniforms.GetParameter<Float2>(
			MakeStringSection(clix::marshalString<clix::E_UTF8>(label)));
		if (!param.has_value())
			return false;
		
		auto p = param.value();
		value[0] = p[0];
		value[1] = p[1];
		return true;
	}

	bool RawMaterial::TryGetConstantFloat3(String^ label, array<float>^ value)
	{
		auto param = _underlying->GetWorkingAsset()->_uniforms.GetParameter<Float3>(
			MakeStringSection(clix::marshalString<clix::E_UTF8>(label)));
		if (!param.has_value())
			return false;
		
		auto p = param.value();
		value[0] = p[0];
		value[1] = p[1];
		value[2] = p[2];
		return true;
	}

	bool RawMaterial::TryGetConstantFloat4(String^ label, array<float>^ value)
	{
		auto param = _underlying->GetWorkingAsset()->_uniforms.GetParameter<Float4>(
			MakeStringSection(clix::marshalString<clix::E_UTF8>(label)));
		if (!param.has_value())
			return false;
		
		auto p = param.value();
		value[0] = p[0];
		value[1] = p[1];
		value[2] = p[2];
		value[3] = p[3];
		return true;
	}

	bool RawMaterial::HasConstant(System::String^ label)
	{
		return _underlying->GetWorkingAsset()->_uniforms.HasParameter(
			MakeStringSection(clix::marshalString<clix::E_UTF8>(label)));
	}

	void RawMaterial::RemoveConstant(System::String^ label)
	{
		auto e = ShaderConstants->GetEnumerator();
		while (e->MoveNext()) {
			if (String::Equals(e->Current->Name, label)) {
				ShaderConstants->Remove(e->Current);
				return;
			}
		}
	}

    System::String^ RawMaterial::Filename::get()
    { 
        auto native = MakeFileNameSplitter(clix::marshalString<clix::E_UTF8>(_initializer)).AllExceptParameters();
        return clix::marshalString<clix::E_UTF8>(native);
    }
    System::String^ RawMaterial::Initializer::get() { return _initializer; }

    const RenderCore::Assets::RawMaterial* RawMaterial::GetUnderlying() 
    { 
        return (!!_underlying) ? _underlying->GetWorkingAsset().get() : nullptr; 
    }

	std::shared_ptr<RenderCore::Assets::RawMaterial> RawMaterial::GetUnderlyingPtr()
	{
		return (!!_underlying) ? _underlying->GetWorkingAsset() : nullptr;
	}

    /*String^ RawMaterial::TechniqueConfig::get() { return clix::marshalString<clix::E_UTF8>(_underlying->GetWorkingAsset()->_techniqueConfig); }

    void RawMaterial::TechniqueConfig::set(String^ value)
    {
        auto native = Conversion::Convert<::Assets::rstring>(clix::marshalString<clix::E_UTF8>(value));
        if (_underlying->GetWorkingAsset()->_techniqueConfig != native) {
            auto transaction = _underlying->Transaction_Begin("Technique Config");
            if (transaction) {
                transaction->GetAsset()._techniqueConfig = native;
                _transId = transaction->Commit();
            }
        }
    }*/

    static RawMaterial::RawMaterial()
    {
        s_table = gcnew Dictionary<String^, WeakReference^>();
    }

    RawMaterial^ RawMaterial::Get(String^ initializer)
    {
        // Creates a raw material for the given initializer, or constructs
        // a new one if one hasn't been created yet.
        // Note -- there's a problem here because different initializers could
        // end up resolving to the same native object. That may not be a problem
        // in all cases... But it could throw off the change tracking.
        System::Diagnostics::Debug::Assert(!String::IsNullOrEmpty(initializer));
        
        WeakReference^ ref;
        if (s_table->TryGetValue(initializer, ref)) {
            auto o = ref->Target;
            if (o) return (RawMaterial^)o;
        
            // if the reference expired, we have to remove it -- we will create it again afterwards...
            s_table->Remove(initializer);
        }
        
        auto result = gcnew RawMaterial(initializer);
        s_table->Add(initializer, gcnew WeakReference(result));
        return result;
    }

    RawMaterial^ RawMaterial::CreateUntitled() 
    { 
        static unsigned counter = 0;
        RawMaterial^ result = gcnew RawMaterial(nullptr);
        result->_initializer = "untitled" + (counter++) + ".material";
        s_table->Add(result->_initializer, gcnew WeakReference(result));
        return result;
    }

    RawMaterial::RawMaterial(System::String^ initialiser)
    {
		_transId = 0;
        if (initialiser) {
            _initializer = initialiser;
            auto nativeInit = clix::marshalString<clix::E_UTF8>(initialiser);
            _underlying = ToolsRig::CreateDivergentAsset<RenderCore::Assets::RawMaterial>(MakeStringSection(nativeInit));
        } else {
            _underlying = ToolsRig::DefaultConstructNewDivergentAsset<RenderCore::Assets::RawMaterial>();
        }
        _renderStateSet = gcnew RenderStateSet(_underlying.GetNativePtr());
    }

    RawMaterial::~RawMaterial()
    {
        delete _renderStateSet;
        _underlying.reset();
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    auto RenderStateSet::DoubleSided::get() -> CheckState
    {
        auto& stateSet = _underlying->GetWorkingAsset()->_stateSet;
        if (stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::DoubleSided) {
            if (stateSet._doubleSided) return CheckState::Checked;
            else return CheckState::Unchecked;
        }
        return CheckState::Indeterminate;
    }
    
    void RenderStateSet::DoubleSided::set(CheckState checkState)
    {
        auto transaction = _underlying->Transaction_Begin("RenderState");
        auto& stateSet = transaction->GetAsset()._stateSet;
        if (checkState == CheckState::Indeterminate) {
            stateSet._flag &= ~RenderCore::Assets::RenderStateSet::Flag::DoubleSided;
        } else {
            stateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::DoubleSided;
            stateSet._doubleSided = (checkState == CheckState::Checked);
        }
        transaction->Commit();
        NotifyPropertyChanged("DoubleSided");
    }

    System::Windows::Forms::CheckState RenderStateSet::Wireframe::get()
    {
        auto& stateSet = _underlying->GetWorkingAsset()->_stateSet;
        if (stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::Wireframe) {
            if (stateSet._wireframe) return CheckState::Checked;
            else return CheckState::Unchecked;
        }
        return CheckState::Indeterminate;
    }

    void RenderStateSet::Wireframe::set(CheckState checkState)
    {
        auto transaction = _underlying->Transaction_Begin("RenderState");
        auto& stateSet = transaction->GetAsset()._stateSet;
        if (checkState == CheckState::Indeterminate) {
            stateSet._flag &= ~RenderCore::Assets::RenderStateSet::Flag::Wireframe;
        } else {
            stateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::Wireframe;
            stateSet._wireframe = (checkState == CheckState::Checked);
        }
        transaction->Commit();
        NotifyPropertyChanged("Wireframe");
    }

    auto RenderStateSet::DeferredBlend::get() -> DeferredBlendState     { return DeferredBlendState::Unset; }
    void RenderStateSet::DeferredBlend::set(DeferredBlendState)         { NotifyPropertyChanged("DeferredBlend"); }

    using BlendOp = RenderCore::BlendOp;
	using Blend = RenderCore::Blend;
    using BlendType = RenderCore::Assets::RenderStateSet::BlendType;

    class StandardBlendDef
    {
    public:
        StandardBlendModes _standardMode;
        BlendType _blendType;
        RenderCore::BlendOp _op;
        RenderCore::Blend _src;
        RenderCore::Blend _dst;
    };

    static const StandardBlendDef s_standardBlendDefs[] = 
    {
        { StandardBlendModes::NoBlending, BlendType::Basic, BlendOp::NoBlending, Blend::One, Blend::Zero },
        
        { StandardBlendModes::Transparent, BlendType::Basic, BlendOp::Add, Blend::SrcAlpha, Blend::InvSrcAlpha },
        { StandardBlendModes::TransparentPremultiplied, BlendType::Basic, BlendOp::Add, Blend::One, Blend::InvSrcAlpha },

        { StandardBlendModes::Add, BlendType::Basic, BlendOp::Add, Blend::One, Blend::One },
        { StandardBlendModes::AddAlpha, BlendType::Basic, BlendOp::Add, Blend::SrcAlpha, Blend::One },
        { StandardBlendModes::Subtract, BlendType::Basic, BlendOp::Subtract, Blend::One, Blend::One },
        { StandardBlendModes::SubtractAlpha, BlendType::Basic, BlendOp::Subtract, Blend::SrcAlpha, Blend::One },

        { StandardBlendModes::Min, BlendType::Basic, BlendOp::Min, Blend::One, Blend::One },
        { StandardBlendModes::Max, BlendType::Basic, BlendOp::Max, Blend::One, Blend::One },

        { StandardBlendModes::OrderedTransparent, BlendType::Ordered, BlendOp::Add, Blend::SrcAlpha, Blend::InvSrcAlpha },
        { StandardBlendModes::OrderedTransparentPremultiplied, BlendType::Ordered, BlendOp::Add, Blend::One, Blend::InvSrcAlpha },
        { StandardBlendModes::Decal, BlendType::DeferredDecal, BlendOp::NoBlending, Blend::One, Blend::Zero }
    };

    StandardBlendModes AsStandardBlendMode(
        const RenderCore::Assets::RenderStateSet& stateSet)
    {
        auto op = stateSet._forwardBlendOp;
        auto src = stateSet._forwardBlendSrc;
        auto dst = stateSet._forwardBlendDst;

        if (!(stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::ForwardBlend)) {
            if (    stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::BlendType
                &&  stateSet._blendType == BlendType::DeferredDecal)
                return StandardBlendModes::Decal;
            return StandardBlendModes::Inherit;
        }

        if (op == BlendOp::NoBlending) {
            if (    stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::BlendType
                &&  stateSet._blendType == BlendType::DeferredDecal)
                    return StandardBlendModes::Decal;
            return StandardBlendModes::NoBlending;
        }

        auto blendType = BlendType::Basic;
        if (stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::BlendType)
            blendType = stateSet._blendType;

        for (unsigned c=0; c<dimof(s_standardBlendDefs); ++c)
            if (    op == s_standardBlendDefs[c]._op
                &&  src == s_standardBlendDefs[c]._src
                &&  dst == s_standardBlendDefs[c]._dst
                &&  blendType == s_standardBlendDefs[c]._blendType)
                return s_standardBlendDefs[c]._standardMode;

        return StandardBlendModes::Complex;
    }

    auto RenderStateSet::StandardBlendMode::get() -> StandardBlendModes
    {
        const auto& underlying = *_underlying->GetWorkingAsset();
        return AsStandardBlendMode(underlying._stateSet);
    }
    
    void RenderStateSet::StandardBlendMode::set(StandardBlendModes newMode)
    {
        if (newMode == StandardBlendModes::Complex) return;
        if (newMode == StandardBlendMode) return;

        if (newMode == StandardBlendModes::Inherit) {
            auto transaction = _underlying->Transaction_Begin("RenderState");
            auto& stateSet = transaction->GetAsset()._stateSet;
            stateSet._forwardBlendOp = BlendOp::NoBlending;
            stateSet._forwardBlendSrc = Blend::One;
            stateSet._forwardBlendDst = Blend::Zero;
            stateSet._blendType = RenderCore::Assets::RenderStateSet::BlendType::Basic;
            stateSet._flag &= ~RenderCore::Assets::RenderStateSet::Flag::ForwardBlend;
            stateSet._flag &= ~RenderCore::Assets::RenderStateSet::Flag::BlendType;
            NotifyPropertyChanged("StandardBlendMode");
            transaction->Commit();
            return;
        }

        for (unsigned c=0; c<dimof(s_standardBlendDefs); ++c)
            if (s_standardBlendDefs[c]._standardMode == newMode) {
                auto transaction = _underlying->Transaction_Begin("RenderState");
                auto& stateSet = transaction->GetAsset()._stateSet;

                stateSet._forwardBlendOp = s_standardBlendDefs[c]._op;
                stateSet._forwardBlendSrc = s_standardBlendDefs[c]._src;
                stateSet._forwardBlendDst = s_standardBlendDefs[c]._dst;
                stateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::ForwardBlend;

                stateSet._blendType = s_standardBlendDefs[c]._blendType;
                stateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::BlendType;

                transaction->Commit();
                NotifyPropertyChanged("StandardBlendMode");
                return;
            }
    }

    RenderStateSet::RenderStateSet(std::shared_ptr<NativeConfig> underlying)
    {
        _underlying = std::move(underlying);
        _propertyChangedContext = System::Threading::SynchronizationContext::Current;
    }

    RenderStateSet::~RenderStateSet()
    {
        _underlying.reset();
    }

    void RenderStateSet::NotifyPropertyChanged(System::String^ propertyName)
    {
            //  This only works correctly in the UI thread. However, given that
            //  this event can be raised by low-level engine code, we might be
            //  in some other thread. We can get around that by using the 
            //  synchronisation functionality in .net to post a message to the
            //  UI thread... That requires creating a delegate type and passing
            //  propertyName to it. It's easy in C#. But it's a little more difficult
            //  in C++/CLI.
        PropertyChanged(this, gcnew PropertyChangedEventArgs(propertyName));
        // _propertyChangedContext->Send(
        //     gcnew System::Threading::SendOrPostCallback(
        //         o => PropertyChanged(this, gcnew PropertyChangedEventArgs(propertyName))
        //     ), nullptr);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    ref struct InvalidAssetListHelper
    {
        InvalidAssetList^ _ial;
    };

    static unsigned BindHelper(
        PlatformRig::Overlays::ITrackedAssetList& trackedAssetList,
        InvalidAssetList^ thisHandle)
    {
        // We have to jump through a ton of hoops to make this work
        // auto_gcroot to a managed class that contains a reference counted pointer
        // to the original InvalidAssetList
        // However, the auto_gcroot has to be contained in a std::shared_ptr<> because
        // we can't copy it during the conversion from lambda -> std::function<...> in BindOnChange

        struct Helper2
        {
            msclr::auto_gcroot<InvalidAssetListHelper^> _helper;
        };

        auto helper = std::make_shared<Helper2>();
        helper ->_helper = gcnew InvalidAssetListHelper;
        helper->_helper->_ial = thisHandle;
        return trackedAssetList.BindOnChange(
            [helper=std::move(helper)]() {
                helper->_helper->_ial->InvokeOnChange(nullptr);
            });
    }

	InvalidAssetList::InvalidAssetList()
    {
        auto tracking = ::Assets::Services::GetAssetSetsPtr();
        _trackedAssetList = PlatformRig::Overlays::CreateTrackedAssetList(tracking, ::Assets::AssetState::Invalid);
        _onChangeContext = System::Threading::SynchronizationContext::Current;
        _onChangeSignalId = BindHelper(*_trackedAssetList.get(), this);
    }

    InvalidAssetList::~InvalidAssetList()
    {
        _trackedAssetList->UnbindOnChange(_onChangeSignalId);
        _trackedAssetList.reset();
    }

    void InvalidAssetList::InvokeOnChange(System::Object^)
    {
        if (System::Threading::SynchronizationContext::Current != _onChangeContext) {
            // shift callback into the original thread, to avoid calling out to C# from native threads too much
            _onChangeContext->Send(
                gcnew System::Threading::SendOrPostCallback(this, &InvalidAssetList::InvokeOnChange), nullptr);
        } else {
            _onChange();
        }
    }

    IEnumerable<Tuple<String^, String^>^>^ InvalidAssetList::AssetList::get() 
    {
        auto result = gcnew List<Tuple<String^, String^>^>();
        
        _trackedAssetList->Lock();
        TRY {
            auto records = _trackedAssetList->GetCurrentRecords();
            for (const auto& i : records) {
			    std::string logStr;
			    if (i.second._actualizationLog && !i.second._actualizationLog->empty()) {
				    logStr = std::string(
					    (const char*)AsPointer(i.second._actualizationLog->begin()),
					    (const char*)AsPointer(i.second._actualizationLog->end()));
			    } else {
				    logStr = "<<no actualization log>>";
			    }

                result->Add(gcnew Tuple<String^, String^>(
                    clix::marshalString<clix::E_UTF8>(i.second._initializer),
                    clix::marshalString<clix::E_UTF8>(logStr)));
            }

        } CATCH(...) {
            _trackedAssetList->Unlock();
            throw;
        } CATCH_END
        _trackedAssetList->Unlock();

        return result;
    }

    bool InvalidAssetList::HasInvalidAssets()
    {
        bool result = false;
        _trackedAssetList->Lock();
        TRY {
            result = !_trackedAssetList->GetCurrentRecords().empty();
        } CATCH(...) {
            _trackedAssetList->Unlock();
            throw;
        } CATCH_END
        _trackedAssetList->Unlock();
		return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template PropertyPair<System::String^, unsigned>;
    template PropertyPair<System::String^, System::String^>;
}

