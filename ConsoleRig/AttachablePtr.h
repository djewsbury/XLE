// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Utility/FunctionUtils.h"		// (for VariantFunctions)
#include "../Core/Prefix.h"
#include "../Core/SelectConfiguration.h"
#include <memory>
#include <typeinfo>
#include <typeindex>

namespace ConsoleRig
{

///////////////////////////////////////////////////////////////////////////////////////////////////

	class CrossModule;
	template<typename Obj> class AttachablePtr;
	template<typename Obj> class WeakAttachablePtr;

	namespace Internal
	{
		// Using std::type_index is unsafe for KeyForType(), because that can be implemented
		// as just a pointer to the type_info. The issue is, we use this is a key in a cross-module
		// dictionary; and when modules are unloaded, the type_info pointer becomes invalidated. We
		// need the those keys to survive longer that the lifetime of any specific module
		// using TypeKey = std::type_index;
		// template<typename Obj> TypeKey KeyForType() { return std::type_index(typeid(Obj)); }
		using TypeKey = uint64_t;
		template<typename Obj> TypeKey KeyForType() { return typeid(Obj).hash_code(); }

		class InfraModuleManager
		{
		public:
			using RegisteredPointerId = unsigned;
			class IRegistrablePointer
			{
			public:
				virtual void PropagateChange(const std::shared_ptr<void>& newValue) = 0;
				virtual void ManagerShuttingDown(bool releaseFromInfraManager) = 0;
				virtual ~IRegistrablePointer() = default;
			};
			RegisteredPointerId Register(TypeKey id, IRegistrablePointer* ptr, bool strong);
			void Deregister(RegisteredPointerId);

			using AttachDetachSig = void(const std::shared_ptr<void>&);
			void ConfigureType(
				TypeKey id,
				AttachDetachSig* attachModuleFn,
				AttachDetachSig* detachModuleFn);

			void EnsureRegistered();

			InfraModuleManager(InfraModuleManager&&) = delete;
			InfraModuleManager& operator=(InfraModuleManager&& moveFrom) = delete;

			static InfraModuleManager& GetInstance() 
				#if COMPILER_ACTIVE == COMPILER_TYPE_CLANG
					__attribute__((visibility("hidden")));
				#else
					;
				#endif
		private:
			class Pimpl;
			std::unique_ptr<Pimpl> _pimpl;

			friend class ConsoleRig::CrossModule;
			void PropagateChange(TypeKey id, const std::shared_ptr<void>& obj);
			void CrossModuleShuttingDown();
			unsigned GetStrongCount(TypeKey id);

			InfraModuleManager();
			~InfraModuleManager();

			template<typename Obj>
				friend class ConsoleRig::AttachablePtr;

			template<typename Obj>
				friend class ConsoleRig::WeakAttachablePtr;

			auto Get(TypeKey id) -> std::shared_ptr<void>;
			void Reset(TypeKey id, const std::shared_ptr<void>& obj);
		};

		template<typename Obj>
			void TryConfigureType() 
				#if COMPILER_ACTIVE == COMPILER_TYPE_CLANG
					__attribute__((visibility("hidden")));
				#else
					;
				#endif
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
		void ManagerShuttingDown(bool releaseFromInfraManager) override;
	};

	template<typename Obj, typename... Args>
		AttachablePtr<Obj> MakeAttachablePtr(Args... a);

	template<typename Obj>
		class WeakAttachablePtr : Internal::InfraModuleManager::IRegistrablePointer
	{
	public:
		std::shared_ptr<Obj> lock() { return _internalPointer.lock(); }
		bool expired() const { return _internalPointer.expired(); }

        void PropagateChange(const std::shared_ptr<void>& newValue) override;
		void ManagerShuttingDown(bool releaseFromInfraManager) override;

		WeakAttachablePtr();
		~WeakAttachablePtr();
	private:
		std::weak_ptr<Obj> _internalPointer;
		Internal::InfraModuleManager::RegisteredPointerId _managerRegistry;
	};

	class CrossModule
	{
	public:
		VariantFunctions _services;

		void EnsureReady();
		void Shutdown();
		static CrossModule& GetInstance();
	private:
		friend class Internal::InfraModuleManager;
		using RegisteredInfraModuleManagerId = unsigned;
		RegisteredInfraModuleManagerId Register(Internal::InfraModuleManager* ptr);
		void Deregister(RegisteredInfraModuleManagerId);

		auto Get(Internal::TypeKey id) -> std::shared_ptr<void>;
		void Reset(Internal::TypeKey id, const std::shared_ptr<void>& obj, RegisteredInfraModuleManagerId owner);
		void CheckExtinction(Internal::TypeKey id);

		CrossModule();
		~CrossModule();
		class Pimpl;
		std::unique_ptr<Pimpl> _pimpl;
	};

///////////////////////////////////////////////////////////////////////////////////////////////////
	
	template<typename Obj>
		AttachablePtr<Obj>::AttachablePtr(const std::shared_ptr<Obj>& copyFrom)
	{
		Internal::TryConfigureType<Obj>();
		_managerRegistry = Internal::InfraModuleManager::GetInstance().Register(Internal::KeyForType<Obj>(), this, true);
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
		void AttachablePtr<Obj>::ManagerShuttingDown(bool releaseFromInfraManager)
	{
		// This is called when either the CrossModule manager or InfraModuleManager is being shut down
		// In either case, we want to release the pointer
		// However, only when the InfraModuleManager is begin shutdown do we clear _managerRegistry
		// The InfraModuleManager can only be started up and shut down once (even though CrossModule manager
		// can be cycled multiple times)
		// We can't fully control the order in which any of these objects are destroyed. But if the
		// InfraModuleManager is destroyed before the pointer is destroyed, we must not attempt to use it
		// again (because it's gone and never coming back)
		// Still, we must retain our registration with the InfraModuleManager if the CrossModule manager
		// shutdown -- because the CrossModule manager may be initialized again, and we may attempt to use this
		// pointer again 
		_internalPointer.reset();
		if (releaseFromInfraManager)
			_managerRegistry = ~0u;
	}

	template<typename Obj>
		AttachablePtr<Obj>::AttachablePtr()
	{
		Internal::TryConfigureType<Obj>();
		auto& man = Internal::InfraModuleManager::GetInstance();
		_managerRegistry = man.Register(Internal::KeyForType<Obj>(), this, true);
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

	namespace Internal
	{
		template<typename T> struct HasAttachCurrentModule
		{
			template<typename U, void (U::*)()> struct FunctionSignature {};
			template<typename U> static std::true_type Test1(FunctionSignature<U, &U::AttachCurrentModule>*);
			template<typename U> static std::false_type Test1(...);
			static const bool Result = decltype(Test1<T>(0))::value;
		};

		template<typename Obj>
			void TryConfigureType()
		{
			#if COMPILER_ACTIVE == COMPILER_TYPE_CLANG
				static __attribute__((visibility("hidden"))) bool s_typeConfigured = false;
			#else
				static bool s_typeConfigured = false;
			#endif
			if (!s_typeConfigured) {
				if constexpr (HasAttachCurrentModule<Obj>::Result) {
					Internal::InfraModuleManager::GetInstance().ConfigureType(
						Internal::KeyForType<Obj>(),
						[](const std::shared_ptr<void>& singleton) {
							static_cast<Obj*>(singleton.get())->AttachCurrentModule();
						},
						[](const std::shared_ptr<void>& singleton) {
							static_cast<Obj*>(singleton.get())->DetachCurrentModule();
						});
				} else {
					Internal::InfraModuleManager::GetInstance().ConfigureType(Internal::KeyForType<Obj>(), nullptr, nullptr);
				}
				s_typeConfigured = true;
			}
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

	template<typename Obj>
        void WeakAttachablePtr<Obj>::PropagateChange(const std::shared_ptr<void>& newValue)
    {
        _internalPointer = std::static_pointer_cast<Obj>(newValue);
    }

    template<typename Obj>
        void WeakAttachablePtr<Obj>::ManagerShuttingDown(bool releaseFromInfraManager)
    {
        _internalPointer.reset();
		if (releaseFromInfraManager)
			_managerRegistry = ~0u;
    }

    template<typename Obj>
        WeakAttachablePtr<Obj>::WeakAttachablePtr()
    {
        Internal::TryConfigureType<Obj>();
        auto& man = Internal::InfraModuleManager::GetInstance();
        _managerRegistry = man.Register(Internal::KeyForType<Obj>(), this, false);
        _internalPointer = std::static_pointer_cast<Obj>(man.Get(Internal::KeyForType<Obj>()));
    }

    template<typename Obj>
        WeakAttachablePtr<Obj>::~WeakAttachablePtr()
    {
        auto oldValue = std::move(_internalPointer);
        // The manager may have shutdown before us; in which case we should avoid attempting to deregister ourselves
        if (_managerRegistry != ~0u)
            Internal::InfraModuleManager::GetInstance().Deregister(_managerRegistry);
        oldValue.reset();
    }

////////////////////////////////////////////////////////////////////////////////////////////////////

	template<typename Object, typename... Args>
		AttachablePtr<Object> MakeAttachablePtr(Args... a)
	{
		return std::make_shared<Object>(std::forward<Args>(a)...);
	}

}

