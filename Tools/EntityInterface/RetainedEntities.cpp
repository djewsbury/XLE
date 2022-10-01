// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "RetainedEntities.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/PtrUtils.h"
#include "../../Utility/IteratorUtils.h"
#include "../../Utility/Streams/StreamFormatter.h"
#include "../../Utility/Streams/FormatterUtils.h"
#include "../../Utility/StreamUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Conversion.h"

namespace EntityInterface
{
    bool RetainedEntities::SetSingleProperties(
        RetainedEntity& dest, const RegisteredObjectType& type, const PropertyInitializer& prop) const
    {
        if (prop._prop == 0 || prop._prop > type._properties.size()) return false;
        if (prop._data.empty()) return false;

        const auto& propertyName = type._properties[prop._prop-1];
        dest._properties.SetParameter(MakeStringSection(propertyName), prop._data, prop._type);
        return true;
    }

    auto RetainedEntities::GetObjectType(EntityTypeId id) const -> RegisteredObjectType*
    {
		auto i = LowerBound(_registeredObjectTypes, id);
        if (i != _registeredObjectTypes.end() && i->first == id)
			return &i->second;
        return nullptr;
    }

    unsigned RetainedEntities::RegisterCallback(EntityTypeId typeId, OnChangeDelegate&& onChange)
    {
        auto type = GetObjectType(typeId);
        if (!type) Throw(std::runtime_error("Unknown type in RegisterCallback"));
        type->_onChange.push_back(std::make_pair(_nextCallbackId, std::move(onChange)));
        return _nextCallbackId++;
    }

    void RetainedEntities::DeregisterCallback(unsigned callbackId)
    {
        for (auto&type:_registeredObjectTypes)
            for (auto i=type.second._onChange.begin(); i!=type.second._onChange.end();) {
                if (i->first == callbackId) {
                    i=type.second._onChange.erase(i);
                } else {
                    ++i;
                }
            }
    }

    void RetainedEntities::InvokeOnChange(RegisteredObjectType& type, RetainedEntity& obj, ChangeType changeType) const
    {
        for (auto i=type._onChange.begin(); i!=type._onChange.end(); ++i)
            (i->second)(*this, obj._id, changeType);

        if ((   changeType == ChangeType::SetProperty || changeType == ChangeType::ChildSetProperty 
            ||  changeType == ChangeType::AddChild || changeType == ChangeType::RemoveChild
            ||  changeType == ChangeType::ChangeHierachy || changeType == ChangeType::Delete)
            &&  obj._parent != 0) {

            ChangeType newChangeType = ChangeType::ChildSetProperty;
            if (    changeType == ChangeType::AddChild || changeType == ChangeType::RemoveChild
                ||  changeType == ChangeType::ChangeHierachy || changeType == ChangeType::Delete)
                newChangeType = ChangeType::ChangeHierachy;

            for (auto i=_objects.begin(); i!=_objects.end(); ++i)
                if (i->_id == obj._parent) {
                    auto type2 = GetObjectType(i->_type);
                    if (type2) 
                        InvokeOnChange(*type2, *i, newChangeType);
                }
        }
    }

    auto RetainedEntities::GetEntity(EntityId objId) const -> const RetainedEntity*
    {
        for (auto i=_objects.begin(); i!=_objects.end(); ++i)
            if (i->_id == objId)
                return AsPointer(i);
        return nullptr;
    }

    auto RetainedEntities::GetEntityWriteable(EntityId objId) -> RetainedEntity*
    {
        for (auto i=_objects.begin(); i!=_objects.end(); ++i)
            if (i->_id == objId)
                return AsPointer(i);
        return nullptr;
    }

    auto RetainedEntities::FindEntitiesOfType(EntityTypeId typeId) const -> std::vector<const RetainedEntity*>
    {
        std::vector<const RetainedEntity*> result;
        for (auto i=_objects.begin(); i!=_objects.end(); ++i)
            if (i->_type == typeId) {
                result.push_back(AsPointer(i));
            }
        return result;
    }

    EntityTypeId RetainedEntities::GetTypeId(StringSection<> name) const
    {
        for (auto i=_registeredObjectTypes.cbegin(); i!=_registeredObjectTypes.cend(); ++i)
            if (XlEqStringI(name, i->second._name))
                return i->first;
        
        _registeredObjectTypes.push_back(
            std::make_pair(_nextObjectTypeId, RegisteredObjectType(name.AsString())));
        return _nextObjectTypeId++;
    }

	PropertyId RetainedEntities::GetPropertyId(EntityTypeId typeId, StringSection<> name) const
    {
        auto type = GetObjectType(typeId);
        if (!type) return 0;

        for (auto i=type->_properties.cbegin(); i!=type->_properties.cend(); ++i)
            if (XlEqStringI(name, *i)) 
                return (PropertyId)(1+std::distance(type->_properties.cbegin(), i));
        
        type->_properties.push_back(name.AsString());
        return (PropertyId)type->_properties.size();
    }

	ChildListId RetainedEntities::GetChildListId(EntityTypeId typeId, StringSection<> name) const
    {
        auto type = GetObjectType(typeId);
        if (!type) return 0;

        for (auto i=type->_childLists.cbegin(); i!=type->_childLists.cend(); ++i)
            if (XlEqStringI(name, *i)) 
                return (ChildListId)(1+std::distance(type->_childLists.cbegin(), i));
        
        type->_childLists.push_back(name.AsString());
        return (ChildListId)type->_childLists.size();
    }

    std::string RetainedEntities::GetTypeName(EntityTypeId typeId) const
    {
        auto i = LowerBound(_registeredObjectTypes, typeId);
        if (i != _registeredObjectTypes.end() && i->first == typeId)
            return i->second._name;
		return {};
    }

	std::string RetainedEntities::GetPropertyName(EntityTypeId typeId, PropertyId propertyId) const
	{
		if (propertyId == 0) return {};
		auto i = LowerBound(_registeredObjectTypes, typeId);
		if (i != _registeredObjectTypes.end() && i->first == typeId)
			if (propertyId <= (unsigned)i->second._properties.size())
				return i->second._properties[propertyId-1];
		return {};
	}

	std::string RetainedEntities::GetChildListName(EntityTypeId typeId, ChildListId childListId) const
	{
		if (childListId == 0) return {};
		auto i = LowerBound(_registeredObjectTypes, typeId);
		if (i != _registeredObjectTypes.end() && i->first == typeId)
			if (childListId <= (unsigned)i->second._childLists.size())
				return i->second._childLists[childListId-1];
		return {};
	}

	void RetainedEntities::PrintEntity(std::ostream& stream, const RetainedEntity& entity, StringSection<> childListName, unsigned indent) const
	{
		stream << StreamIndent(indent) << "[" << entity._id << "] type: " << GetTypeName(entity._type);
		if (!childListName.IsEmpty())
			stream << ", childList: " << childListName;
		stream << std::endl;
		for (auto p : entity._properties)
			stream << StreamIndent(indent + 2) << p.Name() << " = " << p.ValueAsString() << std::endl;

		for (auto c : entity._children) {
			auto child = GetEntity(c.second);
			if (!child) {
				stream << StreamIndent(indent + 2) << "<<Could not find child for id " << c.second << ">>" << std::endl;
				continue;
			}
			stream << "";
			PrintEntity(stream, *child, GetChildListName(entity._type, c.first), indent + 2);
		}
	}

	void RetainedEntities::PrintDocument(std::ostream& stream, unsigned indent) const
	{
		// Find the root entities in this document, and print them (and their children)
		for (const auto&o : _objects)
			if (o._parent == 0)
				PrintEntity(stream, o, {}, indent);
	}

	IteratorRange<RetainedEntities::ChildConstIterator> RetainedEntities::GetChildren(EntityId parentObj, ChildListId childList) const
	{
		auto parent = GetEntity(parentObj);
		if (!parent) return {};
		return GetChildren(*parent, childList);
	}

	IteratorRange<RetainedEntities::ChildConstIterator> RetainedEntities::GetChildren(const RetainedEntity& parent, ChildListId childList) const
	{
		auto i = std::find_if(
			parent._children.begin(), parent._children.end(),
			[childList](const std::pair<ChildListId, EntityId>& p) { return p.first == childList; });
		return IteratorRange<RetainedEntities::ChildConstIterator>(
			ChildConstIterator{ *this, parent, i, childList },
			ChildConstIterator{ *this, parent, parent._children.end(), childList });
	}

    RetainedEntities::RetainedEntities()
    {
        _nextObjectTypeId = 1;
        _nextEntityId = 1;
        _nextCallbackId = 0;
    }

    RetainedEntities::~RetainedEntities() {}

///////////////////////////////////////////////////////////////////////////////////////////////////

	bool RetainedEntities::ChildConstIterator::operator==(const ChildConstIterator& other)
	{
		return	_parentObject == other._parentObject
			&&	_childIdx == other._childIdx;
	}

	bool RetainedEntities::ChildConstIterator::operator!=(const ChildConstIterator& other)
	{
		return	_parentObject != other._parentObject
			||	_childIdx != other._childIdx;
	}

	void RetainedEntities::ChildConstIterator::operator++()
	{
		assert(_childListId != 0);

		auto nextChildIdx = _childIdx + 1;
		while (nextChildIdx < (ptrdiff_t)_parentObject->_children.size()) {
			if (_parentObject->_children[nextChildIdx].first == _childListId) {
				_childIdx = nextChildIdx;
				return;
			}
		}

		// We can off the end of the array while looking for the next child with the given
		// child index. We will now point just off the end of the array, and become an "end"
		// iterator
		_childIdx = _parentObject->_children.size();
	}

	void RetainedEntities::ChildConstIterator::operator--()
	{
		assert(_childListId != 0);
		assert(_childIdx > 0);

		auto nextChildIdx = _childIdx - 1;
		while (nextChildIdx >= 0) {
			if (_parentObject->_children[nextChildIdx].first == _childListId) {
				_childIdx = nextChildIdx;
				return;
			}
		}

		// We can off the end of the array while looking for the next child with the given
		// child index.
		// We must end up pointing to the element before the first
		_childIdx = -1;
	}

	bool operator<(const RetainedEntities::ChildConstIterator& lhs, const RetainedEntities::ChildConstIterator& rhs)
	{
		return lhs._childIdx < rhs._childIdx;
	}

	RetainedEntities::ChildConstIterator operator+(const RetainedEntities::ChildConstIterator& lhs, ptrdiff_t advance)
	{
		if (advance == 0)
			return lhs;
		assert(advance > 0);	// advancing backwards not implemented

		RetainedEntities::ChildConstIterator result = lhs;
		auto nextChildIdx = result._childIdx + 1;
		while (nextChildIdx < (ptrdiff_t)result._parentObject->_children.size()) {
			if (result._parentObject->_children[nextChildIdx].first == result._childListId) {
				--advance;
				if (!advance) {
					result._childIdx = nextChildIdx;
					return result;
				}
			}
		}

		// Hit the end -- become an "end" iterator
		result._childIdx = result._parentObject->_children.size();
		return result;
	}

	auto RetainedEntities::ChildConstIterator::operator*() const -> reference
	{
		assert(_parentObject && _entitySystem);
		// If you hit the following assert, you're probably deferencing an "end" iterator,
		// or you just ran off the end of the array of children
		assert(_childIdx < (ptrdiff_t)_parentObject->_children.size());
		const auto* obj = _entitySystem->GetEntity(_parentObject->_children[_childIdx].second);
		assert(obj);
		return *obj;
	}

	auto RetainedEntities::ChildConstIterator::operator->() const -> reference
	{
		return operator*();
	}

	auto RetainedEntities::ChildConstIterator::operator[](size_t idx) const -> reference
	{
		return *(*this + idx);
	}

	RetainedEntities::ChildConstIterator::ChildConstIterator(
		const RetainedEntities& entitySystem,
		const RetainedEntity& parent, UnderlyingIterator i, ChildListId childList)
	: _entitySystem(&entitySystem), _parentObject(&parent), _childListId(childList)
	{
		_childIdx = std::distance(parent._children.begin(), i);
	}

	RetainedEntities::ChildConstIterator::ChildConstIterator()
	{
		_entitySystem = nullptr;
		_parentObject = nullptr;
		_childListId = 0;
		_childIdx = 0;
	}

	RetainedEntities::ChildConstIterator::ChildConstIterator(nullptr_t)
	{
		_entitySystem = nullptr;
		_parentObject = nullptr;
		_childListId = 0;
		_childIdx = 0;
	}

///////////////////////////////////////////////////////////////////////////////////////////////////

	std::optional<EntityId> RetainedEntitiesAdapter::CreateEntity( 
        EntityTypeId typeId, IteratorRange<const PropertyInitializer*> props)
    {
        auto type = _scene->GetObjectType(typeId);
        if (!type) return {};

        RetainedEntity newObject;
        newObject._id = _scene->_nextEntityId++;
        newObject._type = typeId;
        newObject._parent = 0;

        for (const auto& p:props)
            _scene->SetSingleProperties(newObject, *type, p);

        _scene->_objects.push_back(std::move(newObject));

        _scene->InvokeOnChange(*type, _scene->_objects[_scene->_objects.size()-1], RetainedEntities::ChangeType::Create);
        return newObject._id;
    }

	bool RetainedEntitiesAdapter::DeleteEntity(EntityId entity)
    {
        for (auto i=_scene->_objects.begin(); i!=_scene->_objects.end(); ++i)
            if (i->_id == entity) {
                RetainedEntity copy(std::move(*i));
                _scene->_objects.erase(i);

                auto type = _scene->GetObjectType(copy._type);
                if (type)
                    _scene->InvokeOnChange(*type, copy, RetainedEntities::ChangeType::Delete);
                return true;
            }
        return false;
    }

	bool RetainedEntitiesAdapter::SetProperty(
        EntityId entity, IteratorRange<const PropertyInitializer*> props)
    {
        

        for (auto i=_scene->_objects.begin(); i!=_scene->_objects.end(); ++i)
            if (i->_id == entity) {
                auto type = _scene->GetObjectType(i->_type);
                if (!type) return false;

                bool gotChange = false;
                for (const auto& p:props)
                    gotChange |= _scene->SetSingleProperties(*i, *type, p);
                if (gotChange) _scene->InvokeOnChange(*type, *i, RetainedEntities::ChangeType::SetProperty);
                return true;
            }

        return false;
    }

	std::optional<ImpliedTyping::TypeDesc> RetainedEntitiesAdapter::GetProperty(EntityId entity, PropertyId prop, IteratorRange<void*> destinationBuffer) const
    {
        for (auto i=_scene->_objects.begin(); i!=_scene->_objects.end(); ++i)
            if (i->_id == entity) {
                auto type = _scene->GetObjectType(i->_type);
                if (!type) return {};
                if (prop == 0 || prop > type->_properties.size()) return {};

                ParameterBox::ParameterName pname = type->_properties[prop-1];
                auto ptype = i->_properties.GetParameterType(pname);
                if (ptype._type != ImpliedTyping::TypeCat::Void) {
                    auto res = i->_properties.GetParameterRawValue(pname);
                    assert(res.size() == ptype.GetSize());
                    std::memcpy(destinationBuffer.begin(), res.begin(), std::min(res.size(), destinationBuffer.size()));
                    return ptype;
                }
                return {};
            }

        return {};
    }

    bool RetainedEntitiesAdapter::SetParent(
        EntityId child, EntityId parent,
		ChildListId childList, int insertionPosition)
    {
        auto* childObj = _scene->GetEntityWriteable(child);
        if (!childObj)
            return false;

        auto childType = _scene->GetObjectType(childObj->_type);
        if (!childType) return false;

        if (childObj->_parent != 0) {
            auto* oldParent = _scene->GetEntityWriteable(childObj->_parent);
            if (oldParent) {
                auto i = std::find_if(
					oldParent->_children.begin(), oldParent->_children.end(), 
					[child](const std::pair<ChildListId, EntityId>& p) { return p.second == child; });
                assert(i != oldParent->_children.end());
                oldParent->_children.erase(i);

                auto oldParentType = _scene->GetObjectType(oldParent->_type);
                if (oldParentType)
                    _scene->InvokeOnChange(
                        *oldParentType, *oldParent, 
                        RetainedEntities::ChangeType::RemoveChild);
            }

            childObj->_parent = 0;
        }

///////////////////////////////////////////////////////////////////////////////////////////////////
            // if parent is set to 0, then this is a "remove from parent" operation
        if (!parent) {
            _scene->InvokeOnChange(*childType, *childObj, RetainedEntities::ChangeType::SetParent);
            return true;
        }

        auto* parentObj = _scene->GetEntityWriteable(parent);
        if (!parentObj) {
            _scene->InvokeOnChange(*childType, *childObj, RetainedEntities::ChangeType::SetParent);
            return false;
        }

        if (insertionPosition < 0 || insertionPosition >= (int)parentObj->_children.size()) {
			parentObj->_children.push_back({ childList, child });
        } else {
            parentObj->_children.insert(
                parentObj->_children.begin() + insertionPosition,
				{ childList, child });
        }
        childObj->_parent = parentObj->_id;

        _scene->InvokeOnChange(*childType, *childObj, RetainedEntities::ChangeType::SetParent);

        auto parentType = _scene->GetObjectType(parentObj->_type);
        if (parentType)
            _scene->InvokeOnChange(*parentType, *parentObj, RetainedEntities::ChangeType::AddChild);

        return true;
    }

	EntityTypeId    RetainedEntitiesAdapter::GetTypeId(StringSection<> name) const
    {
        return _scene->GetTypeId(name);
    }

	PropertyId      RetainedEntitiesAdapter::GetPropertyId(EntityTypeId typeId, StringSection<> name) const
    {
        return _scene->GetPropertyId(typeId, name);
    }

	ChildListId     RetainedEntitiesAdapter::GetChildListId(EntityTypeId typeId, StringSection<> name) const
    {
        return _scene->GetChildListId(typeId, name);
    }

	RetainedEntitiesAdapter::RetainedEntitiesAdapter(std::shared_ptr<RetainedEntities> flexObjects)
    : _scene(std::move(flexObjects))
    {}

	RetainedEntitiesAdapter::~RetainedEntitiesAdapter()
    {}

///////////////////////////////////////////////////////////////////////////////////////////////////

    static EntityId DeserializeEntity(
        InputStreamFormatter<utf8>& formatter,
        IMutableEntityDocument& interf,
        StringSection<> objType)
    {
        using Blob = InputStreamFormatter<utf8>::Blob;
        using Section = InputStreamFormatter<utf8>::InteriorSection;
                
        auto beginLoc = formatter.GetLocation();
        auto typeId = interf.GetTypeId(objType);

        std::vector<PropertyInitializer> inits;
        std::vector<char> initsBuffer;
        initsBuffer.reserve(256);

        std::vector<EntityId> children;

        StringSection<> name;
        while (formatter.TryKeyedItem(name)) {
            auto name = RequireKeyedItem(formatter);
            auto next = formatter.PeekNext();
            if (next == Blob::BeginElement) {
                RequireBeginElement(formatter);
                auto child = DeserializeEntity(formatter, interf, name);
                if (child)
                    children.push_back(child);
                RequireEndElement(formatter);
            } else {
                auto value = RequireStringValue(formatter);

                    // parse the value and add it as a property initializer
                char intermediateBuffer[64];
                auto type = ImpliedTyping::ParseFullMatch(
                    value,
                    MakeIteratorRange(intermediateBuffer));

                size_t bufferOffset = initsBuffer.size();
                
                if (type._type == ImpliedTyping::TypeCat::Void) {
                    type._type = ImpliedTyping::TypeCat::UInt8;
                    type._arrayCount = uint16(value._end - value._start);
                    type._typeHint = ImpliedTyping::TypeHint::String;
                    initsBuffer.insert(initsBuffer.end(), value._start, value._end);
                } else {
                    auto size = std::min(type.GetSize(), (unsigned)sizeof(intermediateBuffer));
                    initsBuffer.insert(initsBuffer.end(), intermediateBuffer, PtrAdd(intermediateBuffer, size));
                }
        
                auto id = interf.GetPropertyId(typeId, name);

                PropertyInitializer i;
                i._prop = id;
                i._data = { (void*)bufferOffset, (void*)initsBuffer.size() };		// note -- temporarily storing the offset here, because we convert to a pointer in just below before calling CreateObject
                i._type = type;

                inits.push_back(i);
            }
        };

        if (typeId != ~EntityTypeId(0x0)) {
            for (auto&i : inits)
                i._data = { PtrAdd(AsPointer(initsBuffer.cbegin()), size_t(i._data.first)), PtrAdd(AsPointer(initsBuffer.cbegin()), size_t(i._data.second)) };

            auto id = interf.CreateEntity(typeId, inits);
            if (!id)
                Throw(FormatException("Error while creating object in entity deserialisation", beginLoc));

            for (const auto&c:children)
                interf.SetParent(c, id.value(), 0, -1);

            typeId = ~EntityTypeId(0x0);
            initsBuffer.clear();

            return id.value();
        }

        return 0;
    }

    void Deserialize(
        InputStreamFormatter<utf8>& formatter,
        IMutableEntityDocument& interf)
    {
            // Parse the input file, and send the result to the given entity interface
            // we expect only a list of entities in the root (no attributes)
        StringSection<> name;
        while (formatter.TryKeyedItem(name)) {
            RequireBeginElement(formatter);
            DeserializeEntity(formatter, interf, name);
            RequireEndElement(formatter);
        }
    }

}

