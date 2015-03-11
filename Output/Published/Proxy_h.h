

/* this ALWAYS GENERATED file contains the definitions for the interfaces */


 /* File created by MIDL compiler version 8.00.0603 */
/* at Tue Mar 10 17:56:40 2015
 */
/* Compiler settings for Proxy.idl:
    Oicf, W1, Zp8, env=Win32 (32b run), target_arch=X86 8.00.0603 
    protocol : dce , ms_ext, c_ext, robust
    error checks: allocation ref bounds_check enum stub_data 
    VC __declspec() decoration level: 
         __declspec(uuid()), __declspec(selectany), __declspec(novtable)
         DECLSPEC_UUID(), MIDL_INTERFACE()
*/
/* @@MIDL_FILE_HEADING(  ) */

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

#ifndef __Proxy_h_h__
#define __Proxy_h_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __IProxySite_FWD_DEFINED__
#define __IProxySite_FWD_DEFINED__
typedef interface IProxySite IProxySite;

#endif 	/* __IProxySite_FWD_DEFINED__ */


#ifndef __ProxySite_FWD_DEFINED__
#define __ProxySite_FWD_DEFINED__

#ifdef __cplusplus
typedef class ProxySite ProxySite;
#else
typedef struct ProxySite ProxySite;
#endif /* __cplusplus */

#endif 	/* __ProxySite_FWD_DEFINED__ */


/* header files for imported files */
#include "oaidl.h"
#include "shobjidl.h"

#ifdef __cplusplus
extern "C"{
#endif 


#ifndef __IProxySite_INTERFACE_DEFINED__
#define __IProxySite_INTERFACE_DEFINED__

/* interface IProxySite */
/* [unique][uuid][object] */ 


EXTERN_C const IID IID_IProxySite;

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("E92F0BD1-2A98-41E2-99C6-611EBBEB28F4")
    IProxySite : public IUnknown
    {
    public:
    };
    
    
#else 	/* C style interface */

    typedef struct IProxySiteVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
            IProxySite * This,
            /* [in] */ REFIID riid,
            /* [annotation][iid_is][out] */ 
            _COM_Outptr_  void **ppvObject);
        
        ULONG ( STDMETHODCALLTYPE *AddRef )( 
            IProxySite * This);
        
        ULONG ( STDMETHODCALLTYPE *Release )( 
            IProxySite * This);
        
        END_INTERFACE
    } IProxySiteVtbl;

    interface IProxySite
    {
        CONST_VTBL struct IProxySiteVtbl *lpVtbl;
    };

    

#ifdef COBJMACROS


#define IProxySite_QueryInterface(This,riid,ppvObject)	\
    ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IProxySite_AddRef(This)	\
    ( (This)->lpVtbl -> AddRef(This) ) 

#define IProxySite_Release(This)	\
    ( (This)->lpVtbl -> Release(This) ) 


#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IProxySite_INTERFACE_DEFINED__ */



#ifndef __ProxyLib_LIBRARY_DEFINED__
#define __ProxyLib_LIBRARY_DEFINED__

/* library ProxyLib */
/* [version][uuid] */ 


EXTERN_C const IID LIBID_ProxyLib;

EXTERN_C const CLSID CLSID_ProxySite;

#ifdef __cplusplus

class DECLSPEC_UUID("6D6481C1-BD93-48D1-837D-BB0A1B55D97F")
ProxySite;
#endif
#endif /* __ProxyLib_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


