// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "PropertyDescriptorUtils.h"
#include "../../Tools/GUILayer/MarshalString.h"
#include "../../Utility/ParameterBox.h"

namespace XLEBridgeUtils
{
    /// <summary>Provides a IGetAndSetProperties interface for material objects</summary>
    /// Used when attaching material objects to PropertyGrid type controls. Provides
    /// dynamic properties that get stored as string name and typed value pairs.
    /// Could be moved into GUILayer.dll ...?
    public ref class RawMaterialShaderConstants_GetAndSet : public GUILayer::IGetAndSetProperties
    {
    public:
        virtual bool TryGetMember(String^ name, bool caseInsensitive, Type^ type, Object^% result) 
        {
                // We can choose to interact with the "BindingList" object attached the C++/CLI object
                // or the underlying C++ layer. If we change the C++ layer, then we need to manually
                // rebuild the binding list objects in the GUILayer part.
                // Using the C++ layer give us a little more flexibility. 
            if (name->Length < 2) return false;

            auto cName = name->Substring(2);
            decltype(_material->ShaderConstants) list = nullptr;
            if (name[0] == 'C' && name[1] == '-') {
                list = _material->ShaderConstants;
            } else if (name[0] == 'P' && name[1] == '-') {
                list = _material->MaterialParameterBox;
            } else if (name[0] == 'R' && name[1] == '-') {
                list = _material->ResourceBindings;
            } else {
				return false;
			}

            for each(auto v in list) {
                if (String::Compare(cName, v->Name) == 0) {
                    try {
                        result = Convert::ChangeType(v->Value, type);
                        return result != nullptr;
                    } catch (NotSupportedException^) {}
                    catch (FormatException^) 
                    {
                            // special case for booleans that fail the 
                            // normal conversion process.
                            //  We want booleans to generally become "0" or "1"
                            //  - but the standard .net conversion operations
                            //  don't support these forms. It's awkward to solve
                            //  this problem using TypeConverter classes... So let's
                            //  just do it with a special case here...
                        if (type == Boolean::typeid) {
                            auto parsed = ImpliedTyping::ConvertFullMatch<bool>(
                                clix::marshalString<clix::E_UTF8>(v->Value).c_str());
                            if (parsed.has_value()) {
                                result = gcnew Boolean(parsed.value());
                                return true;
                            }
                        }
                    }
                }
            }

                //  If we return false, then the entry will be blanked out.
                //  that's ok for now... we just have to hit the "default" 
                //  button to make it edittable

            return false;
        }

        virtual bool TrySetMember(String^ name, bool caseInsensitive, Object^ value)
        {
            auto cName = name->Substring(2);
            decltype(_material->ShaderConstants) list = nullptr;
            if (name[0] == 'C' && name[1] == '-') {
                list = _material->ShaderConstants;
            } else if (name[0] == 'P' && name[1] == '-') {
                list = _material->MaterialParameterBox;
            } else if (name[0] == 'R' && name[1] == '-') {
                list = _material->ResourceBindings;
            } else {
				return false;
			}

            for each(auto v in list) {
                if (String::Compare(cName, v->Name) == 0) {
                    auto asBoolean = dynamic_cast<Boolean^>(value);
                    if (asBoolean) v->Value = (*asBoolean) ? "1" : "0";
                    else v->Value = value->ToString();
                    return true;
                }
            }

                // no pre-existing value.. create a new one
            list->Add(GUILayer::RawMaterial::MakePropertyPair(cName, value->ToString()));
            return true;
        }

        RawMaterialShaderConstants_GetAndSet(GUILayer::RawMaterial^ material) : _material(material) {}
        ~RawMaterialShaderConstants_GetAndSet() {}
    protected:
        GUILayer::RawMaterial^ _material;
    };
}

