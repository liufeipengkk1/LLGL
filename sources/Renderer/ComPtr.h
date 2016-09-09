/*
 * ComPtr.h
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#ifndef __LLGL_COM_PTR_H__
#define __LLGL_COM_PTR_H__


#include <algorithm>


namespace LLGL
{


//! Smart pointer class for COM (Component Object Model) objects.
template <typename T>
class ComPtr
{

    public:

        ComPtr() throw() :
            ptr_( nullptr )
        {
        }

        ComPtr(decltype(nullptr)) throw() :
            ptr_( nullptr )
        {
        }

        template <typename U>
        ComPtr(U* rhs) throw() :
            ptr_( rhs )
        {
            AddRef();
        }

        ComPtr(const ComPtr& rhs) throw() :
            ptr_( rhs.ptr_ )
        {
            AddRef();
        }

        ComPtr(ComPtr&& rhs) throw() :
            ptr_( nullptr )
        {
            if (this != reinterpret_cast<ComPtr*>(&reinterpret_cast<unsigned char&>(rhs)))
                Swap(rhs);
        }

        ~ComPtr() throw()
        {
            Release();
        }

        T* Get() const throw()
        {
            return ptr_;
        }

        T* operator -> () const throw()
        {
            return ptr_;
        }

        operator bool () throw()
        {
            return (ptr_ != nullptr);
        }

        ComPtr& operator = (decltype(nullptr)) throw()
        {
            Release();
            return *this;
        }

        ComPtr& operator = (T* rhs) throw()
        {
            if (ptr_ != rhs)
                ComPtr(rhs).Swap(*this);
            return *this;
        }

        template <typename U>
        ComPtr& operator = (U* rhs) throw()
        {
            ComPtr(rhs).Swap(*this);
            return *this;
        }

        ComPtr& operator = (const ComPtr& rhs) throw()
        {
            if (ptr_ != rhs.ptr_)
                ComPtr(rhs).Swap(*this);
            return *this;
        }

        template <typename U>
        ComPtr& operator = (const ComPtr<U>& rhs) throw()
        {
            ComPtr(rhs).Swap(*this);
            return *this;
        }

        ComPtr& operator = (ComPtr&& rhs) throw()
        {
            ComPtr(static_cast<ComPtr&&>(rhs)).Swap(*this);
            return *this;
        }

        template <typename U>
        ComPtr& operator = (ComPtr<U>&& rhs) throw()
        {
            ComPtr(static_cast<ComPtr<U>&&>(rhs)).Swap(*this);
            return *this;
        }

        T* const* operator & () const throw()
        {
            return GetAddressOf();
        }

        T** operator & () throw()
        {
            return GetAddressOf();
        }

        //! Returns the constant reference of the internal pointer.
        T* const* GetAddressOf() const throw()
        {
            return &ptr_;
        }

        //! Returns the reference of ths internal pointer.
        T** GetAddressOf() throw()
        {
            return &ptr_;
        }

        //! Detaches the internal pointer from this smart COM pointer and returns this internal pointer.
        T* Detach() throw()
        {
            auto ptr = ptr_;
            ptr_ = nullptr;
            return ptr;
        }

        unsigned long Reset()
        {
            return Release();
        }

        void Swap(ComPtr&& rhs) throw()
        {
            std::swap(ptr_, rhs.ptr_);
        }

        void Swap(ComPtr& rhs) throw()
        {
            std::swap(ptr_, rhs.ptr_);
        }

    private:

        void AddRef()
        {
            if (ptr_ != nullptr)
                ptr_->AddRef();
        }

        unsigned long Release()
        {
            unsigned long ref = 0;
            
            if (ptr_ != nullptr)
            {
                ref = ptr_->Release();
                ptr_ = nullptr;
            }

            return ref;
        }

        T* ptr_ = nullptr;

};


} // /namespace LLGL


#endif



// ================================================================================