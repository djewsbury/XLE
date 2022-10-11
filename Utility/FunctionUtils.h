// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "IteratorUtils.h"
#include "PtrUtils.h"
#include "../OSServices/RawFS.h"
#include "../Core/Exceptions.h"
#include "../Core/SelectConfiguration.h"
#include <functional>
#include <utility>
#include <vector>
#include <stdint.h>
#include <stdexcept>

#if !defined(__CLR_VER)	// cannot be included into CLR builds, for reasons known only to Microsoft
#include "Threading/Mutex.h"
#endif

namespace Utility
{
        //
        //  MakeFunction is based on a stack overflow answer:
        //      http://stackoverflow.com/questions/21738775/c11-how-to-write-a-wrapper-function-to-make-stdfunction-objects?lq=1
        //  
        //  It allows us to convert various function pointers to std::function<> form.
        //  Note that this is not something we want to do frequently! Often it's better to
        //  keep things in earlier form (eg, the result of a std::bind, or a pointer to a 
        //  lambda function). But we want to use std::function<> form for the VariantFunctions
        //  object below.
        //  

    // For generic types that are functors, delegate to its 'operator()'
    template <typename T>
    struct FunctionTraits
      : public FunctionTraits<decltype(&std::remove_reference<T>::type::operator())>
    {};

    template<> struct FunctionTraits<std::nullptr_t> {};
    template<> struct FunctionTraits<std::nullptr_t&> {};
    template<> struct FunctionTraits<const std::nullptr_t&> {};
    template<> struct FunctionTraits<std::nullptr_t&&> {};
 
    // for pointers to member function
    template <typename ClassType, typename ReturnType_, typename... Args>
    struct FunctionTraits<ReturnType_(ClassType::*)(Args...) const> {
      enum { arity = sizeof...(Args) };
      using ArgumentTuple = std::tuple<Args...>;
      using ReturnType = ReturnType_;
      typedef std::function<ReturnType_ (Args...)> f_type;
    };
 
    // for pointers to member function
    template <typename ClassType, typename ReturnType_, typename... Args>
    struct FunctionTraits<ReturnType_(ClassType::*)(Args...) > {
      enum { arity = sizeof...(Args) };
      using ArgumentTuple = std::tuple<Args...>;
      using ReturnType = ReturnType_;
      typedef std::function<ReturnType_ (Args...)> f_type;
    };
 
    // for function pointers
    template <typename ReturnType_, typename... Args>
    struct FunctionTraits<ReturnType_ (*)(Args...)>  {
      enum { arity = sizeof...(Args) };
      using ArgumentTuple = std::tuple<Args...>;
      using ReturnType = ReturnType_;
      typedef std::function<ReturnType_ (Args...)> f_type;
    };
 
    template <typename L> 
    static typename FunctionTraits<L>::f_type MakeFunction(L l){
      return (typename FunctionTraits<L>::f_type)(l);
    }
 
    //handles bind & multiple function call operator()'s
    template<typename ReturnType, typename... Args, class T>
    auto MakeFunction(T&& t) 
      -> std::function<decltype(ReturnType(t(std::declval<Args>()...)))(Args...)> 
    {return {std::forward<T>(t)};}
 
    //handles explicit overloads
    template<typename ReturnType, typename... Args>
    auto MakeFunction(ReturnType(*p)(Args...))
        -> std::function<ReturnType(Args...)> {
      return {p};
    }
 
    //handles explicit overloads
    template<typename ReturnType, typename... Args, typename ClassType>
    auto MakeFunction(ReturnType(ClassType::*p)(Args...)) 
        -> std::function<ReturnType(Args...)> { 
      return {p};
    }

    inline auto MakeFunction(std::nullptr_t) -> std::function<void()> {
      return nullptr;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////
    //      V A R I A N T   F U N C T I O N S
///////////////////////////////////////////////////////////////////////////////////////////////////


    /// <summary>A collection of functions with varying signatures<summary>
    /// 
    /// VariantFunctions can be used to store function objects with arbitrary 
    /// signatures. These functions can then be called by using a hash key id.
    ///
    /// It is similar to ParameterBox. But where ParameterBox is used to store
    /// primitive data values, VariantFunctions is used to store functions and
    /// procedures.
    ///
    /// VariantFunctions can be used with any of function-like object. That 
    /// includes function pointers, std::bind expressions, lambda functions,
    /// std::function<> objects and other functor objects. In the case of 
    /// lamdba expressions and functor objects, extra data associated in the 
    /// object will be stored (and freed when the VariantFunctions object 
    /// is destroyed).
    ///
    /// Callers will specify the id of the function they want to call, plus the
    /// signature (ie, return type and argument types). Callers must get the 
    /// signature exactly right. An incorrect signature will result in an 
    /// exception (that is, the system is type safe, in a very strict way).
    ///
    /// But callers won't know what type of function object they are calling
    /// (function pointer, std::function<>, lambda function, etc). All types of
    /// functions work in the same way.
    ///
    /// <example>
    ///     Consider the following example:
    ///      <code>\code{.cpp}
    ///         VariantFunctions fns;
    ///         fns.Add(Hash64("Inc"), [](int i) { return i+1; });
    ///             ....
    ///         auto result = fns.Call<int>(Hash64("Inc"), 2);
    ///     \endcode</code>
    ///
    ///     This will call the lamdba function, and pass "2" as the parameter.
    ///
    ///     Stored functions can also store captured data. Consider:
    ///     <code>\code{.cpp}
    ///         void Publish(std::shared_ptr<Foo> foo)
    ///         {
    ///             std::weak_ptr<Foo> weakPtrToFoo = foo;
    ///             s_variantFunctions.Add(Hash64("GetFoo"), 
    ///                 [weakPtrToFoo]() { return weakPtrToFoo.lock(); });
    ///         }
    ///     \endcode</code>
    ///     
    ///     In the above example, a std::weak_ptr is capured by the lambda.
    ///     Its lifetime will be managed correctly; the std::weak_ptr will be 
    ///     stored in the VariantFunctions, until the VariantFunctions object 
    ///     is destroyed. Any type of object can be captured like this
    ///     (including std::shared_ptr<>).
    ///
    ///     This means that it is possible to store arbitrarily complex data within
    ///     a VariantFunctions instance (simply by adding "get" accessor functions 
    //      to return that data).
    ///
    ///     Bind expressions also work. Consider:
    ///     <code>\code{.cpp}
    ///         void Foo::AddEvents(VariantFunctions& fn)
    ///         {
    ///             using namespace std::placeholders;
    ///             fn.Add(Hash64("OnMouseClick"), 
    ///                 std::bind(&Foo::OnMouseClick, shared_from_this(), _1, _2, _3));
    ///         }
    ///     \endcode</code>
    ///
    ///     Here, a class registers a pointer to its member function "OnMouseClick",
    ///     using std::bind to bind a class instance pointer.
    /// </example>
    ///
    /// <seealso cref="Utility::ParameterBox"/>
    /// <seealso cref="Utility::MakeFunction"/>
    class VariantFunctions
    {
    public:
        typedef uint64_t Id;

        template<typename Result, typename... Args>
            Result Call(Id id, Args... args) const;

        template<typename Result, typename... Args>
            Result CallDefault(Id id, const Result& defaultResult, Args... args) const;

        template<typename Result, typename... Args>
            bool TryCall(Result& res, Id id, Args... args) const;

        template<typename FnSig>
            std::function<FnSig>& Get(Id id) const;

        template<typename FnSig>
            bool Has(Id id) const;

        template<typename Fn>
            void Add(Id guid, std::function<Fn>&& fn);

        template<typename ReturnType, typename... Args>
            void Add(Id id, ReturnType (*p) (Args...));

        template <typename L> 
            void Add(Id id, L&& l);

        template<typename ReturnType, typename... Args, typename ClassType>
            void Add(Id id, ReturnType(ClassType::*p)(Args...));

        bool Remove(Id id);
		bool IsEmpty() const { return _fns.empty(); }
		void InvalidateCurrentModule();
		void InvalidateModule(size_t moduleId);

        class DuplicateFunction;
        class NoFunction;
        class SignatureMismatch;

        VariantFunctions();
        ~VariantFunctions();

        VariantFunctions(const VariantFunctions&) = delete;
        VariantFunctions& operator=(const VariantFunctions&) = delete;
    protected:
        class StoredFunction
        {
        public:
            size_t  _offset;
            size_t  _size;
            size_t  _typeHashCode;
            void (*_destructor)(void*);
            void (*_moveConstructor)(void*, void*);

			OSServices::ModuleId _moduleId;
        };
        std::vector<uint8_t> _buffer;
        std::vector<std::pair<Id, StoredFunction>> _fns;

        void ExpandBuffer(size_t newCapacity);
    };

    namespace Internal
    {
        template<typename Type>
            static void Destructor(void* obj)
            {
                reinterpret_cast<Type*>(obj)->~Type();
            }

        template<typename Type>
            static void MoveConstructor(void* dst, void* src)
            {
                auto* d = reinterpret_cast<Type*>(dst);
                auto* s = reinterpret_cast<Type*>(src);
                #pragma push_macro("new")
                #undef new
                    new (d) Type(std::move(*s));
                #pragma pop_macro("new")
            }

        template<typename Type>
            static void CallOpaqueFunction(void* obj)
            {
                (*reinterpret_cast<Type*>(obj))();
            }
    }

    class VariantFunctions::DuplicateFunction : public std::runtime_error
    {
    public:
        DuplicateFunction() : std::runtime_error("Attempting to push multiple functions with the same id into a variant functions set") {}
    };

    class VariantFunctions::NoFunction : public std::runtime_error
    {
    public:
        NoFunction() : std::runtime_error("No function found matching the requested id") {}
    };

    class VariantFunctions::SignatureMismatch : public std::runtime_error
    {
    public:
        SignatureMismatch() : std::runtime_error("Function signature does not match expected signature") {}
    };

    template<typename Fn>
        void VariantFunctions::Add(Id id, std::function<Fn>&& fn)
    {
        auto i = LowerBound(_fns, id);
        if (i != _fns.end() && i->first == id) { Throw(DuplicateFunction()); } // duplicate of one already here!

        StoredFunction sfn;
        sfn._offset = _buffer.size();
        sfn._size = sizeof(std::function<Fn>);
        sfn._destructor = &Internal::Destructor<std::function<Fn>>;
        sfn._moveConstructor = &Internal::MoveConstructor<std::function<Fn>>;
        sfn._typeHashCode = typeid(std::function<Fn>).hash_code();
		sfn._moduleId = OSServices::GetCurrentModuleId();
        
        if ((_buffer.size() + sfn._size) > _buffer.capacity())
            ExpandBuffer((_buffer.size() + sfn._size) * 2);
        assert((_buffer.size() + sfn._size) <= _buffer.capacity());
        _buffer.insert(_buffer.end(), sfn._size, uint8_t(0xcd));

        _fns.insert(i, std::make_pair(id, sfn));

        auto* dst = reinterpret_cast<std::function<Fn>*>(PtrAdd(AsPointer(_buffer.begin()), sfn._offset));
        #pragma push_macro("new")
        #undef new
            new(dst) std::function<Fn>(std::move(fn));
        #pragma pop_macro("new")
    }

    template<typename Result, typename... Args>
        Result VariantFunctions::Call(Id id, Args... args) const
    {
        auto i = LowerBound(_fns, id);
        if (i == _fns.end() || i->first != id)
            Throw(NoFunction());
        
        using FnType = std::function<Result (Args...)>;
        auto expectedSize = sizeof(FnType);
        if (i->second._size != expectedSize || typeid(FnType).hash_code() != i->second._typeHashCode)
            Throw(SignatureMismatch());

        auto* obj = (void*)PtrAdd(AsPointer(_buffer.begin()), i->second._offset);
        auto* fn = reinterpret_cast<std::function<Result (Args...)>*>(obj);

        return (*fn)(args...);
    }

    template<typename Result, typename... Args>
        bool VariantFunctions::TryCall(Result& result, Id id, Args... args) const
    {
        auto i = LowerBound(_fns, id);
        if (i == _fns.end() || i->first != id)
            return false;
        
        using FnType = std::function<Result (Args...)>;
        auto expectedSize = sizeof(FnType);
        if (i->second._size != expectedSize || typeid(FnType).hash_code() != i->second._typeHashCode)
            return false;

        auto* obj = (void*)PtrAdd(AsPointer(_buffer.begin()), i->second._offset);
        auto* fn = reinterpret_cast<FnType*>(obj);

        result = (*fn)(args...);
        return true;
    }

    template<typename Result, typename... Args>
        Result VariantFunctions::CallDefault(Id id, const Result& defaultResult, Args... args) const
    {
        auto i = LowerBound(_fns, id);
        if (i == _fns.end() || i->first != id)
            return defaultResult;
        
        using FnType = std::function<Result (Args...)>;
        auto expectedSize = sizeof(FnType);
        if (i->second._size != expectedSize || typeid(FnType).hash_code() != i->second._typeHashCode)
            Throw(SignatureMismatch());

        auto* obj = (void*)PtrAdd(AsPointer(_buffer.begin()), i->second._offset);
        auto* fn = reinterpret_cast<std::function<Result (Args...)>*>(obj);

        return (*fn)(args...);
    }

    template<typename FnSig>
        std::function<FnSig>& VariantFunctions::Get(Id id) const
    {
        auto i = LowerBound(_fns, id);
        if (i == _fns.end() || i->first != id)
            Throw(NoFunction());
        
        using FnType = std::function<FnSig>;
        auto expectedSize = sizeof(FnType);
        if (i->second._size != expectedSize || typeid(FnType).hash_code() != i->second._typeHashCode)
            Throw(SignatureMismatch());

        auto* obj = (void*)PtrAdd(AsPointer(_buffer.begin()), i->second._offset);
        auto* fn = reinterpret_cast<std::function<FnSig>*>(obj);

        return (*fn);
    }

    template<typename FnSig>
        bool VariantFunctions::Has(Id id) const
    {
        auto i = LowerBound(_fns, id);
        if (i == _fns.end() || i->first != id)
            return false;
        
            // Verify the type matches. If we get a mis-match, we will throw
            // an exception. (this is the only use for the FnSig argument
        using FnType = std::function<FnSig>;
        auto expectedSize = sizeof(FnType);
        if (i->second._size != expectedSize || typeid(FnType).hash_code() != i->second._typeHashCode)
            Throw(SignatureMismatch());

        return true;
    }

    template<typename ReturnType, typename... Args>
        void VariantFunctions::Add(Id id, ReturnType (*p) (Args...))
    {
        Add(id, std::function<ReturnType(Args...)>(p));
    }

    template<typename ReturnType, typename... Args, typename ClassType>
        void VariantFunctions::Add(Id id, ReturnType(ClassType::*p)(Args...))
    {
        Add(id, std::function<ReturnType(Args...)>(p));
    }

    template <typename L> 
        void VariantFunctions::Add(Id id, L&& l)
    {
        Add(id, (typename FunctionTraits<L>::f_type)(l));
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    class AutoCleanup
    {
    public:
        AutoCleanup() {}
        AutoCleanup(std::function<void()>&& fn) : _fn(std::move(fn)) {}
        AutoCleanup(AutoCleanup&& moveFrom) : _fn(std::move(moveFrom._fn)) {}
        AutoCleanup& operator=(AutoCleanup&& moveFrom) { _fn = std::move(moveFrom._fn); return *this; }
        ~AutoCleanup() { if (_fn) (_fn)(); }
    protected:
        std::function<void()> _fn;

        AutoCleanup(const AutoCleanup& ) = delete;
        AutoCleanup& operator=(const AutoCleanup& ) = delete;
    };
        
    template<typename Fn>
        auto MakeAutoCleanup(Fn&& fn) -> AutoCleanup
        {
            return AutoCleanup(MakeFunction(fn));
        }

///////////////////////////////////////////////////////////////////////////////////////////////////

    using SignalDelegateId = unsigned;
    enum class SignalDelegateResult { Continue, Unbind };

#if !defined(__CLR_VER)		// (due to mutex dependencies, must be excluded from CLR builds)

    /// <summary>Bind multiple callbacks to an event</summary>
    ///
    /// This is a common concept exposed by many libraries and languages. It's simply a way to expose
    /// an event, and allow users to bind mutliple callbacks to that event. Think signals & slots from
    /// QT, or events from C#.
    ///
    /// A "signal" is an event (such as onClick, etc), and a "delegate" is a callback function bound
    /// to it. To raise the event, use operator() -- in other words, call the signal like a function. To
    /// bind a delegate, use the Bind() function.
    ///
    /// Signals can have arbitrary arguments, or no arguments. The arguments passed to operator() are
    /// passed to the delegates as expected. But note that since we can have multiple delegates attached,
    /// we can't use perfect forwarding (in other words, arguments passed by value are copied, not moved).
    ///
    /// There are two types of delegates:
    /// <list type="number">
    ///     <item>Delegates that return "void"</item>
    ///     <item>Delegates that return "SignalDelegateResult"</item>
    /// </list>
    ///
    /// With the first type, the caller must unbind the delegate manually. The Bind() method will return
    /// a SignalDelegateId; this can be used to unbind the delegate from the signal.
    ///
    /// With the second type, the delegate itself must decide whether it should be bound or unbound
    /// every time it is called. If it returns SignalDelegateResult::Continue, the delegate will remain
    /// bound to the signal; but if it returns SignalDelegateResult::Unbind, the delegate will be unbound.
    ///
    /// This second type are called "independent delegates", and in typical usage patterns the unbind
    /// operation will result in the destruction of the std::function<> object.
    ///
    /// The order in which multiple attached delegates are called is undefined.
    ///
    /// <seealso cref="SignalDelegateResult"/>
    template<typename... Args>
        class Signal
        {
        public:
            void operator()(Args... args) const;
            void Invoke(Args... args) const { operator()(std::forward<Args>(args)...); }
            bool AtLeastOneBind() const { return _atLeastOneBind.load(); }

            SignalDelegateId Bind(std::function<void(Args...)>&& fn);
            std::function<void(Args...)> Unbind(SignalDelegateId);

            void BindIndependent(std::function<SignalDelegateResult(Args...)>&& fn);

            Signal() {}
            ~Signal() {}
            Signal& operator=(const Signal&) = delete;
            Signal(const Signal&) = delete;

        private:
            struct AttachedDelegate
            {
                std::function<void(Args...)> _function;
                SignalDelegateId _id;
            };
            std::vector<AttachedDelegate> _delegates;
            mutable std::vector<std::function<SignalDelegateResult(Args...)>> _independentDelegates;
            SignalDelegateId _nextDelegateId = 0;
            mutable Threading::RecursiveMutex _delegatesLock;
            mutable bool _preventChangesToDelegates = false;
            mutable std::atomic<bool> _atLeastOneBind;
        };

////////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename... Args>
        void Signal<Args...>::operator()(Args... args) const
        {
            ScopedLock(_delegatesLock);
            _preventChangesToDelegates = true;
            try
            {
                for (const auto&d:_delegates) {
                    (d._function)(args...);
                }
                // Call each delegate in _independentDelegates, but erase them when they return
                // SignalDelegateResult::Unbind. Note that we can't call std::remove_if, because
                // we want to maintain a stable order (it will be confusing if the relative order of
                // two delegates changes because a third delegate was removed!). Note that this stable
                // version of remove_if can result in more "move" operations than the typical std::remove_if
                auto i = _independentDelegates.begin();
                auto targetSpot = _independentDelegates.begin();
                while (i!=_independentDelegates.end()) {
                    auto result = (*i)(args...);
                    if (result == SignalDelegateResult::Continue) {
                        if (i != targetSpot)
                            *targetSpot = std::move(*i);    // shift down into it's new location
                        ++targetSpot;
                    }
                    ++i;
                }
                _independentDelegates.erase(targetSpot, _independentDelegates.end());
                _atLeastOneBind = !_independentDelegates.empty() || !_delegates.empty();
            }
            catch (...)
            {
                _preventChangesToDelegates = false;
                throw;
            }
            _preventChangesToDelegates = false;
        }

    template<typename... Args>
        SignalDelegateId Signal<Args...>::Bind(std::function<void(Args...)>&& fn)
        {
            ScopedLock(_delegatesLock);
            assert(!_preventChangesToDelegates);
            auto delegateId = _nextDelegateId++;
            _delegates.emplace_back(AttachedDelegate{std::move(fn), delegateId});
            _atLeastOneBind = true;
            return delegateId;
        }

    template<typename... Args>
        void Signal<Args...>::BindIndependent(std::function<SignalDelegateResult(Args...)>&& fn)
        {
            ScopedLock(_delegatesLock);
            assert(!_preventChangesToDelegates);
            _independentDelegates.emplace_back(std::move(fn));
            _atLeastOneBind = true;
        }

    template<typename... Args>
        std::function<void(Args...)> Signal<Args...>::Unbind(SignalDelegateId delegateId)
        {
            ScopedLock(_delegatesLock);
            assert(!_preventChangesToDelegates);
            if (_delegates.empty()) {
                return nullptr;
            }
            auto i = std::find_if(_delegates.begin(), _delegates.end(),
                [delegateId](const AttachedDelegate& ad) { return ad._id == delegateId; });
            assert(i!=_delegates.end());
            auto result = std::move(i->_function);
            _delegates.erase(i);
            _atLeastOneBind = !_independentDelegates.empty() || !_delegates.empty();
            return result;
        }

#endif

	///////////////////////////////////////////////////////////////////////////////////////////////////
		//      S T A T I C   C O N S T R U C T O R   F U N C T I O N
	///////////////////////////////////////////////////////////////////////////////////////////////////

	template<void(*ctor)()>
		struct static_constructor
	{
		struct constructor { constructor() { ctor(); } };
		static constructor c;
	};

	template<void(*ctor)()>
		typename static_constructor<ctor>::constructor static_constructor<ctor>::c;

}

using namespace Utility;

