//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include "ThreadController.h"

using namespace ATL;

class ThreadController;

class ATL_NO_VTABLE ThreadEventListener :
    public CComObjectRootEx<CComSingleThreadModel>,
    public IApplicationDebugger
{
public:
    BEGIN_COM_MAP(ThreadEventListener)
        COM_INTERFACE_ENTRY(IApplicationDebugger)
        COM_INTERFACE_ENTRY2(IUnknown, IApplicationDebugger)
    END_COM_MAP()

    ThreadEventListener();

    // IApplicationDebugger
    STDMETHOD(QueryAlive)(void);
    STDMETHOD(CreateInstanceAtDebugger)( 
        _In_ REFCLSID rclsid,
        _In_ IUnknown __RPC_FAR *pUnkOuter,
        _In_ DWORD dwClsContext,
        _In_ REFIID riid,
        _Out_ IUnknown __RPC_FAR *__RPC_FAR *ppvObject);

    STDMETHOD(onDebugOutput)( 
        _In_ LPCOLESTR pstr);
    STDMETHOD(onHandleBreakPoint)( 
        _In_ IRemoteDebugApplicationThread __RPC_FAR *prpt,
        _In_ BREAKREASON br,
        _In_ IActiveScriptErrorDebug __RPC_FAR *pError);
    STDMETHOD(onClose)(void);
    STDMETHOD(onDebuggerEvent)( 
        _In_ REFIID riid,
        _In_ IUnknown __RPC_FAR *punk);

public:
    HRESULT Initialize(_In_ ThreadController* pThreadController);

private:
    DWORD m_dispatchThreadId;

    CComObjPtr<ThreadController> m_spThreadController;
};
