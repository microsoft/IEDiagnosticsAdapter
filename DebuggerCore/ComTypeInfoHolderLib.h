//
// Copyright (C) Microsoft. All rights reserved.
//

// The default CComTypeInfoHolder (atlcom.h) doesn't support having RegFree Dispatch implementations that
// have the TYPELIB embedded in the dll as anything other than the first one
// To get around this we need to change the GetTI function to use the correct TYPELIB index
// We cannot just derive from the existing CComTypeInfoHolder due to the way IDispatchImpl binds to the class
// So we use the existing implementation and change GetTI().

#pragma once

#include <atlcomcli.h>

using namespace ATL;

class CComTypeInfoHolderLib
{
    // Should be 'protected' but can cause compiler to generate fat code.
public:
    const GUID* m_pguid;
    const GUID* m_plibid;
    WORD m_wMajor;
    WORD m_wMinor;

    ITypeInfo* m_pInfo;
    long m_dwRef;
    struct stringdispid
    {
        CComBSTR bstr;
        int nLen;
        DISPID id;
        stringdispid() : nLen(0), id(DISPID_UNKNOWN){}
    };
    stringdispid* m_pMap;
    int m_nCount;

    // Custom typelib index
    int m_typelibIndex;

public:
    HRESULT GetTI(
        _In_ LCID lcid, 
        _Outptr_result_maybenull_ ITypeInfo** ppInfo)
    {
        ATLASSERT(ppInfo != NULL);
        if (ppInfo == NULL)
            return E_POINTER;

        HRESULT hr = S_OK;
        if (m_pInfo == NULL)
            hr = GetTI(lcid);
        *ppInfo = m_pInfo;
        if (m_pInfo != NULL)
        {
            m_pInfo->AddRef();
            hr = S_OK;
        }
        return hr;
    }
    HRESULT GetTI(_In_ LCID lcid);
    HRESULT EnsureTI(_In_ LCID lcid)
    {
        HRESULT hr = S_OK;
        if (m_pInfo == NULL || m_pMap == NULL)
            hr = GetTI(lcid);
        return hr;
    }

    // This function is called by the module on exit
    // It is registered through _pAtlModule->AddTermFunc()
    static void __stdcall Cleanup(_In_ DWORD_PTR dw);

    HRESULT GetTypeInfo(
        _In_ UINT itinfo, 
        _In_ LCID lcid, 
        _Outptr_result_maybenull_ ITypeInfo** pptinfo)
    {
        if (itinfo != 0)
        {
            return DISP_E_BADINDEX;
        }
        return GetTI(lcid, pptinfo);
    }

    HRESULT GetIDsOfNames(
        _In_ REFIID /* riid */, 
        _In_reads_(cNames) LPOLESTR* rgszNames, 
        _In_range_(0,16384) UINT cNames,
        LCID lcid, 
        _Out_ DISPID* rgdispid)
    {
        HRESULT hRes = EnsureTI(lcid);
        _Analysis_assume_(m_pInfo != NULL || FAILED(hRes));
        if (m_pInfo != NULL)
        {
            hRes = E_FAIL;
            // Look in cache if
            // cache is populated
            // parameter names are not requested
            if (m_pMap != NULL && cNames == 1)
            {
                int n = int( ocslen(rgszNames[0]) );
                for (int j=m_nCount-1; j>=0; j--)
                {
                    if ((n == m_pMap[j].nLen) &&
                        (memcmp(m_pMap[j].bstr, rgszNames[0], m_pMap[j].nLen * sizeof(OLECHAR)) == 0))
                    {
                        rgdispid[0] = m_pMap[j].id;
                        hRes = S_OK;
                        break;
                    }
                }
            }
            // if cache is empty or name not in cache or parameter names are requested,
            // delegate to ITypeInfo::GetIDsOfNames
            if (FAILED(hRes))
            {
                hRes = m_pInfo->GetIDsOfNames(rgszNames, cNames, rgdispid);
            }
        }
        return hRes;
    }

    _Check_return_ HRESULT Invoke(
        _Inout_ IDispatch* p, 
        _In_ DISPID dispidMember, 
        _In_ REFIID /* riid */,
        _In_ LCID lcid, 
        _In_ WORD wFlags, 
        _In_ DISPPARAMS* pdispparams, 
        _Out_opt_ VARIANT* pvarResult,
        _Out_opt_ EXCEPINFO* pexcepinfo, 
        _Out_opt_ UINT* puArgErr)
    {
        HRESULT hRes = EnsureTI(lcid);
        _Analysis_assume_(m_pInfo != NULL || FAILED(hRes));
        if (m_pInfo != NULL)
            hRes = m_pInfo->Invoke(p, dispidMember, wFlags, pdispparams, pvarResult, pexcepinfo, puArgErr);
        return hRes;
    }

    _Check_return_ HRESULT LoadNameCache(_Inout_ ITypeInfo* pTypeInfo)
    {
        TYPEATTR* pta;
        HRESULT hr = pTypeInfo->GetTypeAttr(&pta);
        if (SUCCEEDED(hr))
        {
            stringdispid* pMap = NULL;
            m_nCount = pta->cFuncs;
            m_pMap = NULL;
            if (m_nCount != 0)
            {
                pMap = _ATL_NEW stringdispid[m_nCount];
                if (pMap == NULL)
                {
                    pTypeInfo->ReleaseTypeAttr(pta);
                    return E_OUTOFMEMORY;
                }
            }
            for (int i=0; i<m_nCount; i++)
            {
                FUNCDESC* pfd;
                if (SUCCEEDED(pTypeInfo->GetFuncDesc(i, &pfd)))
                {
                    CComBSTR bstrName;
                    if (SUCCEEDED(pTypeInfo->GetDocumentation(pfd->memid, &bstrName, NULL, NULL, NULL)))
                    {
                        pMap[i].bstr.Attach(bstrName.Detach());
                        pMap[i].nLen = SysStringLen(pMap[i].bstr);
                        pMap[i].id = pfd->memid;
                    }
                    pTypeInfo->ReleaseFuncDesc(pfd);
                }
            }
            m_pMap = pMap;
            pTypeInfo->ReleaseTypeAttr(pta);
        }
        return S_OK;
    }
};

inline void __stdcall CComTypeInfoHolderLib::Cleanup(_In_ DWORD_PTR dw)
{
    ATLASSERT(dw != 0);
    if (dw == 0)
        return;

    CComTypeInfoHolderLib* p = (CComTypeInfoHolderLib*) dw;
    if (p->m_pInfo != NULL)
        p->m_pInfo->Release();
    p->m_pInfo = NULL;
    delete [] p->m_pMap;
    p->m_pMap = NULL;
}

inline HRESULT CComTypeInfoHolderLib::GetTI(_In_ LCID lcid)
{
    //If this assert occurs then most likely didn't initialize properly
    ATLASSUME(m_plibid != NULL && m_pguid != NULL);

    if (m_pInfo != NULL && m_pMap != NULL)
        return S_OK;

    CComCritSecLock<CComCriticalSection> lock(_pAtlModule->m_csStaticDataInitAndTypeInfo, false);
    HRESULT hRes = lock.Lock();
    if (FAILED(hRes))
    {
        ATLTRACE(atlTraceCOM, 0, _T("ERROR : Unable to lock critical section in CComTypeInfoHolderLib::GetTI\n"));
        ATLASSERT(0);
        return hRes;
    }
    hRes = E_FAIL;
    if (m_pInfo == NULL)
    {
        ITypeLib* pTypeLib = NULL;
        if (InlineIsEqualGUID(CAtlModule::m_libid, *m_plibid) && m_wMajor == 0xFFFF && m_wMinor == 0xFFFF)
        {
            TCHAR szFilePath[MAX_PATH];
            DWORD dwFLen = ::GetModuleFileName(_AtlBaseModule.GetModuleInstance(), szFilePath, MAX_PATH);
            if( dwFLen != 0 && dwFLen != MAX_PATH )
            {
                USES_CONVERSION_EX;
                LPOLESTR pszFile = T2OLE_EX(szFilePath, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
#ifndef _UNICODE
                if (pszFile == NULL)
                    return E_OUTOFMEMORY;
#endif
                // Append the typelib index onto the file string
                CString withIndex(pszFile);
                withIndex.AppendFormat(L"\\%d", m_typelibIndex);

                hRes = LoadTypeLib(withIndex, &pTypeLib);
            }
        }
        else
        {
            ATLASSUME(!InlineIsEqualGUID(*m_plibid, GUID_NULL) && "Module LIBID not initialized. See DECLARE_LIBID documentation.");
            hRes = LoadRegTypeLib(*m_plibid, m_wMajor, m_wMinor, lcid, &pTypeLib);
#ifdef _DEBUG
            if (SUCCEEDED(hRes))
            {
                // Trace out an warning if the requested TypelibID is the same as the modules TypelibID
                // and versions do not match.
                // 
                // In most cases it is due to wrong version template parameters to IDispatchImpl, 
                // IProvideClassInfoImpl or IProvideClassInfo2Impl.
                // Set major and minor versions to 0xFFFF if the modules type lib has to be loaded
                // irrespective of its version.
                // 
                // Get the module's file path
                TCHAR szFilePath[MAX_PATH];
                DWORD dwFLen = ::GetModuleFileName(_AtlBaseModule.GetModuleInstance(), szFilePath, MAX_PATH);
                if( dwFLen != 0 && dwFLen != MAX_PATH )
                {
                    USES_CONVERSION_EX;
                    CComPtr<ITypeLib> spTypeLibModule;
                    HRESULT hRes2 = S_OK;
                    LPOLESTR pszFile = T2OLE_EX(szFilePath, _ATL_SAFE_ALLOCA_DEF_THRESHOLD);
                    if (pszFile == NULL)
                        hRes2 = E_OUTOFMEMORY;
                    else
                        hRes2 = LoadTypeLib(pszFile, &spTypeLibModule);
                    if (SUCCEEDED(hRes2))
                    {
                        TLIBATTR* pLibAttr;
                        hRes2 = spTypeLibModule->GetLibAttr(&pLibAttr);
                        if (SUCCEEDED(hRes2))
                        {
                            if (InlineIsEqualGUID(pLibAttr->guid, *m_plibid) &&
                                (pLibAttr->wMajorVerNum != m_wMajor ||
                                pLibAttr->wMinorVerNum != m_wMinor))
                            {
                                ATLTRACE(atlTraceCOM, 0, _T("Warning : CComTypeInfoHolderLib::GetTI : Loaded typelib does not match the typelib in the module : %s\n"), szFilePath);
                                ATLTRACE(atlTraceCOM, 0, _T("\tSee IDispatchImpl overview help topic for more information\n"));
                            }
                            spTypeLibModule->ReleaseTLibAttr(pLibAttr);
                        }
                    }
                }
            }
            else
            {
                ATLTRACE(atlTraceCOM, 0, _T("ERROR : Unable to load Typelibrary. (HRESULT = 0x%x)\n"), hRes);
                ATLTRACE(atlTraceCOM, 0, _T("\tVerify TypelibID and major version specified with\n"));
                ATLTRACE(atlTraceCOM, 0, _T("\tIDispatchImpl, CStockPropImpl, IProvideClassInfoImpl or IProvideCLassInfo2Impl\n"));
            }
#endif
        }
        if (SUCCEEDED(hRes))
        {
            CComPtr<ITypeInfo> spTypeInfo;
            hRes = pTypeLib->GetTypeInfoOfGuid(*m_pguid, &spTypeInfo);
            if (SUCCEEDED(hRes))
            {
                CComPtr<ITypeInfo> spInfo(spTypeInfo);
                CComPtr<ITypeInfo2> spTypeInfo2;
                if (SUCCEEDED(spTypeInfo->QueryInterface(&spTypeInfo2)))
                    spInfo = spTypeInfo2;

                m_pInfo = spInfo.Detach();
                _pAtlModule->AddTermFunc(Cleanup, (DWORD_PTR)this);
            }
            pTypeLib->Release();
        }
    }
    else
    {
        // Another thread has loaded the typeinfo so we're OK.
        hRes = S_OK;
    }

    if (m_pInfo != NULL && m_pMap == NULL)
    {
        hRes=LoadNameCache(m_pInfo);
    }

    return hRes;
}

template <class T, const IID* piid, class tihclass = CComTypeInfoHolder>
class ATL_NO_VTABLE IDispatchExImpl :
    public IDispatchImpl<T, piid, &CAtlModule::m_libid, 0xFFFF, 0xFFFF, tihclass>
{
public:
    // IDispatchEx
    STDMETHOD(GetDispID)(
        _In_ BSTR bstrName,
        _In_ DWORD /* grfdex */,
        _Out_ DISPID* pid)
    {
        return this->GetIDsOfNames(
            IID_NULL,
            &bstrName,
            1,
            MAKELCID(MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), SORT_DEFAULT),
            pid);
    }

    STDMETHOD(InvokeEx)(
        _In_  DISPID id,
        _In_  LCID lcid,
        _In_  WORD wFlags,
        _In_  DISPPARAMS* pdp,
        _Out_opt_  VARIANT* pvarRes,
        _Out_opt_  EXCEPINFO* pei,
        _In_opt_  IServiceProvider* pspCaller)
    {
        UNREFERENCED_PARAMETER(pspCaller);

        UINT argError = 0;
        return this->Invoke(id, IID_NULL, lcid, wFlags, pdp, pvarRes, pei, &argError);
    }

    STDMETHOD(DeleteMemberByName)(
        _In_ BSTR /* bstrName */,
        _In_ DWORD /* grfdex */)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(DeleteMemberByDispID)(
        _In_ DISPID /* id */)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetMemberProperties)(
        _In_ DISPID /* id */,
        _In_ DWORD /* grfdexFetch */,
        _Out_ DWORD* /* pgrfdex */)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetMemberName)(
        _In_ DISPID /* id */,
        _Out_opt_ BSTR* /* pbstrName */)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetNextDispID)(
        _In_ DWORD /* grfdex */,
        _In_ DISPID /* id */,
        _Out_ DISPID* /* pid */)
    {
        return E_NOTIMPL;
    }

    STDMETHOD(GetNameSpaceParent)(
        _Out_opt_ IUnknown** /* ppunk */)
    {
        return E_NOTIMPL;
    }
};

template <class T, const IID* piid>
class ATL_NO_VTABLE IDispatchExImplLib :
    public IDispatchExImpl<T, piid, CComTypeInfoHolderLib>
{
public:
    static void SetTypeLibIndex(_In_ int index)
    {
        IDispatchExImpl<T, piid, CComTypeInfoHolderLib>::_tih.m_typelibIndex = index;
    };
};

template <class T, const IID* piid>
class ATL_NO_VTABLE IDispatchImplLib :
    public IDispatchImpl<T, piid, &CAtlModule::m_libid, /*wMajor=*/ 0xffff, /*wMinor=*/ 0xffff, CComTypeInfoHolderLib>
{
public:
    static void SetTypeLibIndex(_In_ int index)
    {
        IDispatchImpl<T, piid, &CAtlModule::m_libid, 0xffff, /*wMinor =*/ 0xffff, CComTypeInfoHolderLib>::_tih.m_typelibIndex = index;
    };
};

// Special function used for ensuring the LIB gets loaded when combined into a different dll
// And for setting the dispatch's TYPELIB index, to allow multiple IDispatchs to exist in that same dll.
template <class T>
void SetTypeLibIndexForDispatch(_In_ int typelibIndex)
{
    T::SetTypeLibIndex(typelibIndex);
};
