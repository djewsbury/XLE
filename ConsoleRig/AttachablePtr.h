// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/FunctionUtils.h"
#include <memory>
#include <typeinfo>
#include <typeindex>

namespace ConsoleRig
{

///////////////////////////////////////////////////////////////////////////////////////////////////

	class CrossModule;
	template<typename Obj> class AttachablePtr;

	namespace Internal
	{
		using TypeKey = std::type_index;
		template<typename Obj> TypeKey KeyForType() { return std::type_index(typeid(Obj)); }

		class InfraModuleManager
		{
		public:
			using RegisteredPointerId = unsigned;
			class IRegistrablePointer
			{
			public:
				virtual void PropagateChange(const std::shared_ptr<void>& newValue) = 0;
				virtual void ManagerShuttingDown() = 0;
				virtual ~IRegistrablePointer() = default;
			};
			RegisteredPointerId Register(TypeKey id, IRegistrablePointer* ptr);
			void Deregister(RegisteredPointerId);

			void ConfigureType(
				TypeKey id,
				std::function<void(const std::shared_ptr<void>&)>&& attachModuleFn,
				std::function<void(const std::shared_ptr<void>&)>&& detachModuleFn);

			InfraModuleManager(InfraModuleManager&&) = delete;
			InfraModuleManager& operator=(InfraModuleManager&& moveFrom) = delete;

			static InfraModuleManager& GetInstance() __attribute__((visibility("hidden")));
		private:
			class Pimpl;
			std::unique_ptr<Pimpl> _pimpl;

			friend class ConsoleRig::CrossModule;
			void PropagateChange(TypeKey id, const std::shared_ptr<void>& obj);
			void CrossModuleShuttingDown();

			InfraModuleManager();
			~InfraModuleManager();

			template<typename Obj>
				friend class ConsoleRig::AttachablePtr;

			auto Get(TypeKey id) -> std::shared_ptr<void>;
			void Reset(TypeKey id, const std::shared_ptr<void>& obj);
		};
	}

	/**
	<summary>All AttachablePtrs of the same type point to the same object, even across modules</summary>

	When using multiple modules (ie, shared libraries), we often want to share singletons between them.
	This also needs to work early initialization, and it should behave in an intelligent way when 
	libraries are attached and detached at runtime. And furthermore it should work in the same way
	when using different platforms and compiler ecosystems.

	This makes all of this more complicated that it might seem at first. But our solution here is a pointer
	type that will automatically propagate it's value such that all pointers that point to the same type
	have the same value. In other words, it points to singleton types that can be used across modules.

	If you declare an AttachablePtr in code without providing a value, it will be embued with the current
	singleton of that type, if it exists. Or nullptr, if no singleton of that type exists.
	If you assign an AttachablePtr to a value, that value will be propagated out to all other AttachablePtrs
	of the same type, and they will all start to point to that same new object.

	When a module is unloaded, any pointers that were initailized by that module will automatically be
	nulled out. This handles cases when a singleton is created by one module, but used in another. It's not
	safe to use a singleton after it's creating module has been unloaded -- because if there are any methods,
	the code for those methods has probably been unloaded. But this automatic nulling pattern makes it possible
	for modules to publish singletons, and then automatically revoke them when the module is unloaded.
	*/
	template<typename Obj>
		class AttachablePtr : public Internal::InfraModuleManager::IRegistrablePointer
	{
	public:
		Obj* get() { return _internalPointer.get(); }
		Obj* operator->() { return _internalPointer.operator->(); }
		operator bool() { return _internalPointer.operator bool(); }
		Obj& operator*() { return _internalPointer.operator*(); }

		T1(Obj2) friend bool operator==(const AttachablePtr<Obj2>& lhs, const std::shared_ptr<Obj2>& rhs);
		T1(Obj2) friend bool operator==(const std::shared_ptr<Obj2>& lhs, const AttachablePtr<Obj2>& rhs);
		T1(Obj2) friend bool operator==(const AttachablePtr<Obj2>& lhs, std::nullptr_t);
		T1(Obj2) friend bool operator==(std::nullptr_t, const AttachablePtr<Obj2>& rhs);
		T1(Obj2) friend bool operator!=(const AttachablePtr<Obj2>& lhs, const std::shared_ptr<Obj2>& rhs);
		T1(Obj2) friend bool operator!=(const std::shared_ptr<Obj2>& lhs, const AttachablePtr<Obj2>& rhs);
		T1(Obj2) friend bool operator!=(const AttachablePtr<Obj2>& lhs, std::nullptr_t);
		T1(Obj2) friend bool operator!=(std::nullptr_t, const AttachablePtr<Obj2>& rhs);

		AttachablePtr();
		AttachablePtr(const std::shared_ptr<Obj>& copyFrom);
		AttachablePtr& operator=(const std::shared_ptr<Obj>& copyFrom);
		~AttachablePtr();

		AttachablePtr(AttachablePtr&& moveFrom) = delete;
		AttachablePtr& operator=(AttachablePtr&& moveFrom) = delete;
		AttachablePtr(const AttachablePtr& copyFrom) = delete;
		AttachablePtr& operator=(const AttachablePtr& copyFrom) = delete;
	private:
		std::shared_ptr<Obj> _internalPointer;
		Internal::InfraModuleManager::RegisteredPointerId _managerRegistry;

		void PropagateChange(const std::shared_ptr<void>& newValue) override;
		void ManagerShuttingDown() override;
		static void TryConfigureType() __attribute__((visibility("hidden")));
	};

	template<typename Obj, typename... Args>
		AttachablePtr<Obj> MakeAttachablePtr(Args... a);

	class CrossModule
	{
	public:
		VariantFunctions _services;

		static CrossModule& GetInstance();
	private:
		friend class Internal::InfraModuleManager;
		using RegisteredInfraModuleManagerId = unsigned;
		RegisteredInfraModuleManagerId Register(Internal::InfraModuleManager* ptr);
		void Deregister(RegisteredInfraModuleManagerId);

		auto Get(Internal::TypeKey id) -> std::shared_ptr<void>;
		void Reset(Internal::TypeKey id, const std::shared_ptr<void>& obj, RegisteredInfraModuleManagerId owner);

		CrossModule();
		~CrossModule();
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////

	namespace Internal
	{
		template<typename T> struct HasAttachCurrentModule
		{
			template<typename U, void (U::*)()> struct FunctionSignature {};
			template<typename U> static std::true_type Test1(FunctionSignature<U, &U::AttachCurrentModule>*);
			template<typename U> static std::false_type Test1(...);
			static const bool Result = decltype(Test1<T>(0))::value;
		};

		template<typename Obj, typename std::enable_if<HasAttachCurrentModule<Obj>::Result>::type* = nullptr>
			static void TryAttachCurrentModule(Obj& obj)
		{
			obj.AttachCurrentModule();
		}

		template<typename Obj, typename std::enable_if<HasAttachCurrentModule<Obj>::Result>::type* = nullptr>
			static void TryDetachCurrentModule(Obj& obj)
		{
			obj.DetachCurrentModule();
		}

		template<typename Obj, typename std::enable_if<!HasAttachCurrentModule<Obj>::Result>::type* = nullptr>
			static void TryAttachCurrentModule(Obj& obj) {}

		template<typename Obj, typename std::enable_if<!HasAttachCurrentModule<Obj>::Result>::type* = nullptr>
			static void TryDetachCurrentModule(Obj& obj) {}
	}

///////////////////////////////////////////////////////////////////////////////////////////////////
	
	template<typename Obj>
		AttachablePtr<Obj>::AttachablePtr(const std::shared_ptr<Obj>& copyFrom)
	{
		TryConfigureType();
		_managerRegistry = Internal::InfraModuleManager::GetInstance().Register(Internal::KeyForType<Obj>(), this);
		Internal::InfraModuleManager::GetInstance().Reset(Internal::KeyForType<Obj>(), copyFrom);
	}

	template<typename Obj>
		AttachablePtr<Obj>& AttachablePtr<Obj>::operator=(const std::shared_ptr<Obj>& copyFrom)
	{
		if (copyFrom.get() != get()) {
			auto oldValue = std::move(_internalPointer);
			Internal::InfraModuleManager::GetInstance().Reset(Internal::KeyForType<Obj>(), copyFrom);

			// We don't actually release our reference on the old _internal pointer until after all of the 
			// pointer changes have propaged through. This is generally preferable with singleton type objects,
			// when assigning pointers to nullptr during destruction, because it means that by the time we enter 
			// the destructor for the singleton, we've already cleared out the singleton instance pointers
			oldValue.reset();
		}
		return *this;
	}

	template<typename Obj>
		void AttachablePtr<Obj>::PropagateChange(const std::shared_ptr<void>& newValue)
	{
		if (newValue.get() != get())
			_internalPointer = std::static_pointer_cast<Obj>(newValue);
	}

	template<typename Obj>
		void AttachablePtr<Obj>::ManagerShuttingDown()
	{
		_managerRegistry = ~0u;
		_internalPointer = nullptr;
	}

	template<typename Obj>
		AttachablePtr<Obj>::AttachablePtr()
	{
		TryConfigureType();
		auto& man = Internal::InfraModuleManager::GetInstance();
		_managerRegistry = man.Register(Internal::KeyForType<Obj>(), this);
		_internalPointer = std::static_pointer_cast<Obj>(man.Get(Internal::KeyForType<Obj>()));
	}
   
	template<typename Obj>
		AttachablePtr<Obj>::~AttachablePtr()
	{
		auto oldValue = std::move(_internalPointer);
		// The manager may have shutdown before us; in which case we should avoid attempting to deregister ourselves
		if (_managerRegistry != ~0u)
			Internal::InfraModuleManager::GetInstance().Deregister(_managerRegistry);
		oldValue.reset();
	}

	template<typename Obj>
		void AttachablePtr<Obj>::TryConfigureType()
    {
		static __attribute__((visibility("hidden"))) bool s_typeConfigured = false;
		if (!s_typeConfigured) {
			Internal::InfraModuleManager::GetInstance().ConfigureType(
				Internal::KeyForType<Obj>(),
				[](const std::shared_ptr<void>& singleton) {
					Internal::TryAttachCurrentModule(*static_cast<Obj*>(singleton.get()));
				},
				[](const std::shared_ptr<void>& singleton) {
					Internal::TryDetachCurrentModule(*static_cast<Obj*>(singleton.get()));
				});
			s_typeConfigured = true;
		}
    }

	T1(Obj) bool operator==(const AttachablePtr<Obj>& lhs, const std::shared_ptr<Obj>& rhs) { return lhs._internalPointer == rhs; }
	T1(Obj) bool operator==(const std::shared_ptr<Obj>& lhs, const AttachablePtr<Obj>& rhs) { return lhs == rhs._internalPointer; }
	T1(Obj) bool operator==(const AttachablePtr<Obj>& lhs, std::nullptr_t) { return lhs._internalPointer == nullptr; }
	T1(Obj) bool operator==(std::nullptr_t, const AttachablePtr<Obj>& rhs) { return rhs._internalPointer == nullptr; }
	T1(Obj) bool operator!=(const AttachablePtr<Obj>& lhs, const std::shared_ptr<Obj>& rhs) { return lhs._internalPointer != rhs; }
	T1(Obj) bool operator!=(const std::shared_ptr<Obj>& lhs, const AttachablePtr<Obj>& rhs) { return lhs != rhs._internalPointer; }
	T1(Obj) bool operator!=(const AttachablePtr<Obj>& lhs, std::nullptr_t) { return lhs._internalPointer != nullptr; }
	T1(Obj) bool operator!=(std::nullptr_t, const AttachablePtr<Obj>& rhs) { return rhs._internalPointer != nullptr; }

////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Object, typename... Args>
		AttachablePtr<Object> MakeAttachablePtr(Args... a)
	{
		return std::make_shared<Object>(std::forward<Args>(a)...);
	}

}

