//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

#include <atlcomcli.h>
#include <sal.h>

//+----------------------------------------------------------------------------
//
//  Class:      CComObjPtr
//
//  Synopsis:   A CComPtr style smart pointer that can be used directly on 
//              COM Objects rather than just on interfaces.
//              One of the limitations of CComPtr is it will only work 
//              properly when the inheritance chain for a class has only one 
//              path to IUnknown. If it has more than one path, the following 
//              error will be issued when you attempt to assign a value of 
//              type T to the CComPtr:
//              error C2594: 'argument' : ambiguous conversions from 
//              'SomeClass *' to 'IUnknown *'
//              The reason behind this is the operator= for CComPtr uses 
//              AtlComPtrAssign to change the references.  The right hand side
//              of the assignment is passed to this function as IUnknown.  
//              Since there are multiple paths to IUnknown the C++ compiler 
//              cannot implicitly perform the cast and issues the above error. 
//              To work around this, this new CComObjPtr class inherits from
//              CComPtr base.  It defines the same operators as CComPtr but 
//              uses a Copy Constructor and Swap to perform the = which 
//              gets around the multiple paths to IUnknown.  
//              The rest of the functions are identical to CComPtr.  
//
//              NOTE: CComObjPtr should not be used for interfaces, but only 
//              for the root object that has multiple paths to IUnknown.
//
//
template <class T>
class CComObjPtr : public ATL::CComPtr<T>
{
public:
    CComObjPtr() throw()
    {
    }
    CComObjPtr(int nNull) throw() :
    ATL::CComPtr<T>(nNull)
    {
    }
    CComObjPtr(_Inout_opt_ T* lp) throw() :
    ATL::CComPtr<T>(lp)
    {
    }
    CComObjPtr(_Inout_ const CComObjPtr<T>& lp) throw() :
    ATL::CComPtr<T>(lp.p)
    {
    }

    T* operator=(_Inout_opt_ T* lp) throw()
    {
        if(*this!=lp)
        {
            CComObjPtr<T> sp(lp);
            Swap(&p, &sp.p);
        }
        return *this;
    }

    T* operator=(_Inout_ const CComObjPtr<T>& lp) throw()
    {
        if(*this!=lp)
        {
            CComObjPtr<T> sp(lp);
            Swap(&p, &sp.p);
        }
        return *this;
    }

private:

    static void Swap(_Inout_ T **x, _Inout_ T **y) 
    {
        T *t = *x;
        *x = *y;
        *y = t;
    }
};

