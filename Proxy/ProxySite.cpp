//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "ProxySite.h"
#include "BrowserHost.h"
#include "DebuggerHost.h"
#include "WebSocketClientHost.h"

ProxySite::ProxySite()
{
    // Create our message window used to handle window messages
    HWND createdWindow = this->Create(HWND_MESSAGE);
    ATLENSURE_THROW(createdWindow != NULL, E_FAIL);
}


ProxySite::~ProxySite()
{
    if (::IsWindow(m_hWnd))
    {
        this->DestroyWindow();
    }
}

// IObjectWithSite
STDMETHODIMP ProxySite::SetSite(IUnknown* pUnkSite)
{
    HRESULT hr = S_OK;

    // Create a wrapper that will release the site's addref if this function fails
    std::shared_ptr<int> spSiteRefCountWrapper(0, [this, &hr](int* /*pInt*/) { if (hr != S_OK) IObjectWithSiteImpl<ProxySite>::SetSite(nullptr); });

    // Call the base class first so that we have access to the new m_spUnkSite
    hr = IObjectWithSiteImpl<ProxySite>::SetSite(pUnkSite);
    FAIL_IF_NOT_S_OK(hr);

    if (pUnkSite != nullptr)
    {
        // Create the websocket thread
        hr = this->CreateThread(m_hWnd, nullptr, &WebSocketThreadProc, m_websocketThread.m_id, m_websocketThread.m_handle, m_websocketThread.m_hwnd);
        FAIL_IF_NOT_S_OK(hr);
    }
    else
    {
        // Deinitializing
    }

    return hr;
}

// IOleWindow
STDMETHODIMP ProxySite::GetWindow(__RPC__deref_out_opt HWND* pHwnd)
{
    (*pHwnd) = 0;

    if (::IsWindow(m_websocketThread.m_hwnd))
    {
        (*pHwnd) = m_websocketThread.m_hwnd;
        return S_OK;
    }

    return E_NOT_VALID_STATE;
}

// Window Messages
LRESULT ProxySite::OnCreateEngine(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    CComBSTR id;
    id.Attach(reinterpret_cast<BSTR>(wParam));

    HWND resultHwnd = NULL;

    if (id.Length() == 8 && wcsncmp(id, L"debugger", 8) == 0)
    {
        ATLENSURE_RETURN_VAL(!m_debuggerThread.m_hwnd, -1);

        // Enable debugging
        CComPtr<IRemoteDebugApplication> spRemoteDebugApplication;
        HRESULT hr = this->EnableDynamicDebugging(spRemoteDebugApplication);
        FAIL_IF_NOT_S_OK(hr);

        // Create the debugger thread
        hr = this->CreateThread(m_hWnd, spRemoteDebugApplication, &DebuggerThreadProc, m_debuggerThread.m_id, m_debuggerThread.m_handle, m_debuggerThread.m_hwnd);
        FAIL_IF_NOT_S_OK(hr);

        resultHwnd = m_debuggerThread.m_hwnd;
    }
    else
    {
        // Create a new engine on this thread
        ATLENSURE_RETURN_VAL(m_browserEngines.find(id) == m_browserEngines.end(), -1);

        CComObject<BrowserHost>* pBrowserHost;
        HRESULT hr = CComObject<BrowserHost>::CreateInstance(&pBrowserHost);
        FAIL_IF_NOT_S_OK(hr);

        CComPtr<BrowserHost> spBrowserHost(pBrowserHost);
        hr = spBrowserHost->Initialize(m_hWnd, m_spUnkSite);
        FAIL_IF_NOT_S_OK(hr);

        m_browserEngines[id] = spBrowserHost;

        resultHwnd = spBrowserHost->m_hWnd;
    }

    ::PostMessage(resultHwnd, WM_SET_MESSAGE_HWND, 0, reinterpret_cast<LPARAM>(m_websocketThread.m_hwnd));

    // Tell the websocket controller about this new engine
    BSTR param = id.Detach();
    BOOL succeeded = ::PostMessage(m_websocketThread.m_hwnd, WM_SET_MESSAGE_HWND, reinterpret_cast<WPARAM>(param), reinterpret_cast<LPARAM>(resultHwnd));
    if (!succeeded)
    {
        id.Attach(param);
        ATLASSERT(false);
    }

    return 0;
}

LRESULT ProxySite::OnControllerCommand(UINT nMsg, WPARAM wParam, LPARAM lParam, BOOL& /*bHandled*/)
{
    HRESULT hr = S_OK;

    switch (nMsg)
    {
    case WM_ENABLEDYNAMICDEBUGGING:
    {
        CComPtr<IRemoteDebugApplication> spRemoteDebugApplication;
        hr = this->EnableDynamicDebugging(spRemoteDebugApplication);

        ::PostMessageW(m_debuggerThread.m_hwnd, WM_DEBUGGINGENABLED, TRUE, (LPARAM)spRemoteDebugApplication.p);
    }
    break;
    }

    return 0;
}

// Helper functions
HRESULT ProxySite::CreateThread(_In_ HWND mainHwnd, _In_opt_ IUnknown* pUnknown, _In_ LPTHREAD_START_ROUTINE threadProc, _Out_ DWORD& threadId, _Out_ CHandle& threadHandle, _Out_ HWND& threadMessageHwnd)
{
    HRESULT hr = S_OK;

    CHandle threadInitializedEvent(::CreateEvent(nullptr, TRUE, FALSE, nullptr));
    ATLENSURE_RETURN_HR(threadInitializedEvent != nullptr, ::AtlHresultFromLastError());

    shared_ptr<HRESULT> hrResult(new HRESULT(S_OK));
    shared_ptr<HWND> hwndThread(new HWND(0));
    unique_ptr<ThreadParams> spParams(new ThreadParams(threadInitializedEvent, hrResult, hwndThread, mainHwnd, pUnknown));

    // Create the thread
    ThreadParams* pParams = spParams.release();
    HANDLE hThread = ::CreateThread(nullptr, 0, threadProc, (LPVOID)pParams, 0, &threadId);
    if (hThread == nullptr)
    {
        hr = ::AtlHresultFromLastError();
        spParams.reset(pParams);
        FAIL_IF_NOT_S_OK(hr);
    }

    threadHandle.Attach(hThread);

    // Wait for the thread to finish initialization
    DWORD waitResult = ::WaitForSingleObject(threadInitializedEvent, INFINITE);
    switch (waitResult)
    {
    case WAIT_OBJECT_0:
        hr = (*hrResult);
        break;
    case WAIT_TIMEOUT:
        hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        break;
    case WAIT_ABANDONED:
        hr = HRESULT_FROM_WIN32(WAIT_ABANDONED);
        break;
    case WAIT_FAILED:
        hr = AtlHresultFromLastError();
        break;
    default:
        hr = E_UNEXPECTED;
        break;
    }

    if (hr != S_OK)
    {
        threadHandle.Close();
    }

    threadMessageHwnd = (*hwndThread);

    return hr;
}

HRESULT ProxySite::EnableDynamicDebugging(_Out_ CComPtr<IRemoteDebugApplication>& spRemoteDebugApplication)
{
    HRESULT hr = S_OK;

    CComPtr<IDispatch> spDispatch;
    CComVariant spResult;
    hr = IOleCommandTargetExec(IDM_DEBUGGERDYNAMICATTACH, spDispatch, spResult);
    if (hr == S_OK)
    {
        CComQIPtr<IServiceProvider> spServiceProvider(spDispatch);
        _ASSERT(spServiceProvider.p != nullptr);

        if (spServiceProvider.p != nullptr)
        {
            // Get the debug application now that source rundown mode has initiated the PDM
            spServiceProvider->QueryService(IID_IDebugApplication, &(spRemoteDebugApplication.p));
            ATLASSERT(spRemoteDebugApplication.p != nullptr);
        }
    }

    return hr;
}

HRESULT ProxySite::IOleCommandTargetExec(_In_ DWORD cmdId, _Inout_ CComPtr<IDispatch>& spDispatch, _Inout_ CComVariant& spResult)
{
    ATLENSURE_RETURN_HR(m_spUnkSite.p != nullptr, E_NOT_VALID_STATE);

    // Get the IOleCommandTarget interface from the site
    HRESULT hr = E_NOINTERFACE;
    hr = Helpers::GetDocumentFromSite(m_spUnkSite, spDispatch);
    if (hr == S_OK && spDispatch.p != nullptr)
    {
        hr = E_NOINTERFACE;
        CComQIPtr<IOleCommandTarget> spCmdTarget(spDispatch);
        if (spCmdTarget.p != nullptr)
        {
            hr = spCmdTarget->Exec(&CGID_MSHTML, cmdId, 0, NULL, &spResult);
        }
    }

    return hr;
}

// Thread procs
static DWORD WINAPI WebSocketThreadProc(_In_ LPVOID pThreadParam)
{
    unique_ptr<ThreadParams> spParams(reinterpret_cast<ThreadParams*>(pThreadParam));
    HANDLE initializedEvent = spParams->m_hEvent;
    shared_ptr<HRESULT> spResult = spParams->m_spResult;
    shared_ptr<HWND> spHwnd = spParams->m_spHwnd;
    HWND mainHwnd = spParams->m_mainHwnd;
    spParams.reset();

    // Initialize OLE COM and uninitialize when it goes out of scope
    HRESULT hrCoInit = ::OleInitialize(nullptr);
    std::shared_ptr<HRESULT> spCoInit(&hrCoInit, [](const HRESULT* hrCom) { ::OleUninitialize(); hrCom; });
    ATLENSURE_RETURN_VAL(hrCoInit == S_OK, 1);

    // Create a message pump
    MSG msg;
    ::PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    // Scope for the thread window
    {
        shared_ptr<WebSocketClientHost> spThreadWindow(new WebSocketClientHost());
        spThreadWindow->Initialize(mainHwnd);

        // Done initializing
        (*spHwnd) = spThreadWindow->m_hWnd;
        (*spResult) = S_OK;
        ::SetEvent(initializedEvent);

        // Thread message loop
        BOOL getMessageRet;
        while ((getMessageRet = ::GetMessage(&msg, NULL, 0, 0)) != 0)
        {
            if (getMessageRet != -1)
            {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
            }
        }
    }

    return 0;
}

static DWORD WINAPI DebuggerThreadProc(_In_ LPVOID pThreadParam)
{
    unique_ptr<ThreadParams> spParams(reinterpret_cast<ThreadParams*>(pThreadParam));
    HANDLE initializedEvent = spParams->m_hEvent;
    shared_ptr<HRESULT> spResult = spParams->m_spResult;
    shared_ptr<HWND> spHwnd = spParams->m_spHwnd;
    HWND mainHwnd = spParams->m_mainHwnd;
    CComQIPtr<IRemoteDebugApplication> spRemoteDebugApplication(spParams->m_spUnknown);
    spParams.reset();

    // Initialize OLE COM and uninitialize when it goes out of scope
    HRESULT hrCoInit = ::OleInitialize(nullptr);
    std::shared_ptr<HRESULT> spCoInit(&hrCoInit, [](const HRESULT* hrCom) { ::OleUninitialize(); hrCom; });
    ATLENSURE_RETURN_VAL(hrCoInit == S_OK, 1);

    // Create a message pump
    MSG msg;
    ::PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    // Scope for the thread window
    {
        shared_ptr<DebuggerHost> spThreadWindow(new DebuggerHost());
        HRESULT hr = spThreadWindow->Initialize(mainHwnd, spRemoteDebugApplication);

        // Done initializing
        (*spHwnd) = spThreadWindow->m_hWnd;
        (*spResult) = hr;
        ::SetEvent(initializedEvent);

        if (hr == S_OK)
        {
            // Thread message loop
            BOOL getMessageRet;
            while ((getMessageRet = ::GetMessage(&msg, NULL, 0, 0)) != 0)
            {
                if (getMessageRet != -1)
                {
                    ::TranslateMessage(&msg);
                    ::DispatchMessage(&msg);
                }
            }
        }
    }

    return 0;
}
