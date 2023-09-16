/***************************************************************
*
*   Module Name:
*      msclr_auto_ptr.h
*
*   Abstract:
*      A auto pointer that can be   used as a member of
*      a CLR class.
*      
*   Author:
*      Denis N. Shevchenko [dsheva@gmail.com] 1-Feb-2006
*
*   License:
*      You may freely use and modify this class as long as you
*      include this copyright.
*
*      This code is provided "as is" without express
*      or implied warranty. 
*
*      Copyright (c) Denis N. Shevchenko, 2006.
*
*    From the comments at:
*       http://www.codeproject.com/Articles/12771/CAutoNativePtr-A-managed-smart-pointer-for-using-n
*
***************************************************************/
 
#pragma once
 
#include "DelayedDeleteQueue.h"
#include <msclr\safebool.h>
#include <assert.h>
#include <memory>

#pragma push_macro("Log")
#undef Log
 
namespace clix
{
    template<class T>
		public ref class auto_ptr sealed
    {
    public:
        typedef T element_type;
 
        auto_ptr() /*throw()*/ : p_(0) {}
        explicit auto_ptr(T *p) /*throw()*/ : p_(p) {}
        auto_ptr(auto_ptr<T> %rhs) /*throw()*/ : p_(rhs.release()) {}
 
        template<class T2>
            auto_ptr(auto_ptr<T2> %rhs) /*throw()*/
                : p_(rhs.release()) {}
 
        ~auto_ptr() /*throw()*/ { delete p_; p_ = nullptr; }
   
        !auto_ptr() /*throw()*/
        {
            if (p_) {
			    #if defined(_DEBUG)
				    System::Diagnostics::Debugger::Log(0, "clix::auto_ptr<>", "Finalizer used! The variable deleted in non-deterministic way.");
			    #endif
                // delete p_;
                GUILayer::DelayedDeleteQueue::Add(System::IntPtr(p_), gcnew GUILayer::DeletionCallback(DeleteFn));
            }
        }
 
        template<class T2>
            operator auto_ptr<T2>() /*throw()*/
            {
                return auto_ptr<T2>(*this);
            }
 
        auto_ptr<T>% operator=(auto_ptr<T> %rhs) /*throw()*/
        {
            reset(rhs.release());
            return *this;
        }
 
        template<class T2>
            auto_ptr<T>% operator=(auto_ptr<T2> %rhs) /*throw()*/
            {
                reset(rhs.release());
                return *this;
            }
 
        static T& operator*(auto_ptr %ap) /*const throw()*/
        {
            if(!ap.p_)
                throw gcnew System::NullReferenceException("clix::auto_ptr<>");
            return *ap.p_;
        }
 
        T* operator->() /*const throw()*/   { return &**this; }
        T* get() /*const throw()*/          { return p_; }
 
        T* release() /*throw()*/
        {
            T *p = p_;
            p_ = 0;
            return p;
        }
 
        void reset() /*throw()*/    { reset(0); }
 
        void reset(T *p) /*throw()*/
        {
            if (p != p_) { delete p_; }
            p_ = p;
        }
 
        // Additional functionality like in msclr::auto_handle
        void swap(auto_ptr<T> %rhs) /*throw()*/ 
        {
            auto_ptr<T> tmp = rhs;
            rhs = *this;
            *this = tmp;
        }
 
        // for use when auto_ptr appears in a conditional
        operator msclr::_detail_class::_safe_bool() /*throw()*/
        {
            return p_ != 0 ? msclr::_detail_class::_safe_true : msclr::_detail_class::_safe_false;
        }
 
        // for use when auto_ptr appears in a conditional
        bool operator!() /*throw()*/        { return p_ == 0; }
 
    private:
        T *p_;

        static void DeleteFn(System::IntPtr ptr) { delete (T*)ptr.ToPointer(); }
    };

    template<typename T>
        void swap(auto_ptr<T> %lhs, auto_ptr<T> %rhs) 
        {
           lhs.swap(rhs);
        }

    ///<summary>Wrap a std::shared_ptr<> for use in managed types</summary>
    /// note  -- this code based on code shared in a stack-overflow page
    /// Managed types can't have native members directly. And worse, we can't
    /// return a templated native type from a managed function (like std::shared_ptr<>)
    /// For reference counted objects that aren't derived from std::enabled_shared_from_this,
    /// this presents some problems.
    /// As a work-around, we can this this generic wrapper to get std::shared_ptr<> like
    /// behaviour int the class definition and method signatures of managed types.
    template <class T>
        public ref class shared_ptr sealed
    {
    public:
        shared_ptr() 
            : pPtr(new std::shared_ptr<T>()) 
        {}

        shared_ptr(T* t) 
        {
            pPtr = new std::shared_ptr<T>(t);
        }

        shared_ptr(std::shared_ptr<T>&& t)
        {
            pPtr = new std::shared_ptr<T>(std::move(t));
        }

        shared_ptr(const std::shared_ptr<T>& copyFrom)
        {
            pPtr = new std::shared_ptr<T>(copyFrom);
        }

        shared_ptr(shared_ptr<T>% t) 
        {
            pPtr = new std::shared_ptr<T>(*t.pPtr);
        }

        !shared_ptr() 
        {
            if (pPtr) {
			    #if defined(_DEBUG)
				    System::Diagnostics::Debugger::Log(0, "clix::shared_ptr<>", "Finalizer used! The variable deleted in non-deterministic way.");
			    #endif
                // delete pPtr;
                GUILayer::DelayedDeleteQueue::Add(System::IntPtr(pPtr), gcnew GUILayer::DeletionCallback(DeleteFn));
            }
        }

        ~shared_ptr() 
        {
            delete pPtr;
            pPtr = nullptr;
        }

        operator std::shared_ptr<T>() 
        {
            return *pPtr;
        }

        std::shared_ptr<T>& GetNativePtr()
        {
            return *pPtr;
        }

        void* GetNativeOpaque()
        {
            return pPtr;
        }

        shared_ptr<T>% operator=(T* ptr) 
        {
            delete pPtr;
            pPtr = new std::shared_ptr<T>(ptr);
            return *this;
        }

        shared_ptr<T>% operator=(shared_ptr<T>% ptr) 
        {
            delete pPtr;
            pPtr = new std::shared_ptr<T>(*ptr.pPtr);
            return *this;
        }

        shared_ptr<T>% operator=(const std::shared_ptr<T>& copyFrom)
        {
            (*pPtr) = copyFrom;
            return *this;
        }

        shared_ptr<T>% operator=(std::shared_ptr<T>&& moveFrom)
        {
            (*pPtr) = std::move(moveFrom);
            return *this;
        }

        T* operator->() 
        {
            if(!(*pPtr).get())
                throw gcnew System::NullReferenceException("clix::shared_ptr<>");
            return (*pPtr).get();
        }

        T* get()
        {
            return (*pPtr).get();
        }

        void reset() 
        {
            pPtr->reset();
        }

        operator msclr::_detail_class::_safe_bool() /*throw()*/
        {
            return pPtr->get() != 0 ? msclr::_detail_class::_safe_true : msclr::_detail_class::_safe_false;
        }
 
        // for use when auto_ptr appears in a conditional
        bool operator!() /*throw()*/        { return pPtr->get() == 0; }

    private:
        std::shared_ptr<T>* pPtr;

        static void DeleteFn(System::IntPtr ptr) { delete (std::shared_ptr<T>*)ptr.ToPointer(); }
    };

}

#pragma pop_macro("Log")
