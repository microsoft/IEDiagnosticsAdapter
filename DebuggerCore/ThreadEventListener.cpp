//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "ThreadEventListener.h"

ThreadEventListener::ThreadEventListener() :
    m_dispatchThreadId(::GetCurrentThreadId()) // We are always created on the dispatch thread
{
}

HRESULT ThreadEventListener::Initialize(_In_ ThreadController* pThreadController)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pThreadController != nullptr, E_INVALIDARG);

    m_spThreadController = pThreadController;

    return S_OK;
}

STDMETHODIMP ThreadEventListener::QueryAlive(void) 
{ 
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    return S_OK; 
}

STDMETHODIMP ThreadEventListener::CreateInstanceAtDebugger( 
    _In_ REFCLSID rclsid,
    _In_ IUnknown __RPC_FAR *pUnkOuter,
    _In_ DWORD dwClsContext,
    _In_ REFIID riid,
    _Out_ IUnknown __RPC_FAR *__RPC_FAR *ppvObject)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    return ::CoCreateInstance(rclsid,
        pUnkOuter,
        dwClsContext,
        riid,
        (LPVOID*)ppvObject);
}

STDMETHODIMP ThreadEventListener::onDebugOutput( 
    _In_ LPCOLESTR /* pstr */)
{ 
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    return E_NOTIMPL; 
}

STDMETHODIMP ThreadEventListener::onHandleBreakPoint( 
    _In_ IRemoteDebugApplicationThread __RPC_FAR *prpt,
    _In_ BREAKREASON br,
    _In_ IActiveScriptErrorDebug __RPC_FAR *pError)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(prpt != nullptr, E_INVALIDARG);

    HRESULT hr = S_OK;

    CComBSTR bstrErrorDescription;
    BOOL fIsFirstChance = FALSE;
    BOOL fIsUserUnhandled = FALSE;
    WORD errorId = 0;

    // if this is an error, get the exception information
    if (br == BREAKREASON_ERROR && pError)
    {
        EXCEPINFO exceptionInfo;
        hr = pError->GetExceptionInfo(&exceptionInfo);
        if (hr == S_OK)
        {
            // get description and id
            bstrErrorDescription.Attach(exceptionInfo.bstrDescription);
            errorId = LOWORD(exceptionInfo.scode); // message id from LOWORD of scode

            // unused - just freeing from the struct
            CComBSTR bstrHelpFile;
            bstrHelpFile.Attach(exceptionInfo.bstrHelpFile);
            CComBSTR bstrSource;
            bstrSource.Attach(exceptionInfo.bstrSource);

            // get first chance exception info if available
            CComQIPtr<IActiveScriptErrorDebug110> spScriptErrorDebug110 = pError;
            ATLASSERT(spScriptErrorDebug110.p != nullptr);
            if (spScriptErrorDebug110.p != nullptr)
            {
                SCRIPT_ERROR_DEBUG_EXCEPTION_THROWN_KIND exceptionKind;
                if (spScriptErrorDebug110->GetExceptionThrownKind(&exceptionKind) == S_OK)
                {
                    if (exceptionKind == ETK_FIRST_CHANCE)
                    {
                        fIsFirstChance = TRUE;
                    } 
                    else if (exceptionKind == ETK_USER_UNHANDLED)
                    {
                        fIsUserUnhandled = TRUE;
                    }
                }
            }
        }
    }

    // Notify the controller
    if (m_spThreadController.p != nullptr)
    {
        m_spThreadController->OnPDMBreak(prpt, br, bstrErrorDescription, errorId, !!fIsFirstChance, !!fIsUserUnhandled);
    }

    return hr;
}

STDMETHODIMP ThreadEventListener::onClose(void) 
{ 
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    // Notify the controller
    if (m_spThreadController.p != nullptr)
    {
        m_spThreadController->OnPDMClose();
    }

    return S_OK;
}

STDMETHODIMP ThreadEventListener::onDebuggerEvent( 
    _In_ REFIID /* riid */,
    _In_ IUnknown __RPC_FAR * /* punk */)
{ 
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    return E_NOTIMPL; 
}
