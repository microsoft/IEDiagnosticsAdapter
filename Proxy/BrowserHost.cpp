//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "BrowserHost.h"

BrowserHost::BrowserHost() :
    m_websocketHwnd(0),
    m_enableDebuggingInjectedCode(true)
{
    // Create our message window used to handle window messages
    HWND createdWindow = this->Create(HWND_MESSAGE);
    ATLENSURE_THROW(createdWindow != NULL, E_FAIL);
}

BrowserHost::~BrowserHost()
{
    if (::IsWindow(m_hWnd))
    {
        this->DestroyWindow();
    }
}

HRESULT BrowserHost::Initialize(_In_ HWND proxyHwnd, _In_ IUnknown* pWebControl)
{
    m_proxyHwnd = proxyHwnd;
    m_spWebControl = pWebControl;

    CComPtr<IDispatch> spDispatch;
    HRESULT hr = Helpers::GetDocumentFromSite(m_spWebControl, spDispatch);
    FAIL_IF_NOT_S_OK(hr);

    CComQIPtr<IServiceProvider> spSP(spDispatch);
    ATLENSURE_RETURN_HR(spSP != nullptr, E_NOINTERFACE);

    CComPtr<IDiagnosticsScriptEngineProvider> spEngineProvider;
    hr = spSP->QueryService(IID_IDiagnosticsScriptEngineProvider, &spEngineProvider);
    FAIL_IF_NOT_S_OK(hr);

    hr = spEngineProvider->CreateDiagnosticsScriptEngine(this, m_enableDebuggingInjectedCode, 0, &m_spDiagnosticsEngine);
    FAIL_IF_NOT_S_OK(hr);

    hr = this->DispEventAdvise(m_spWebControl);
    FAIL_IF_NOT_S_OK(hr);

    return hr;
}

// IDiagnosticsScriptEngineSite
STDMETHODIMP BrowserHost::OnMessage(_In_reads_(ulDataCount)  LPCWSTR* pszData, ULONG ulDataCount)
{
    const wchar_t postMessageId[] = L"postMessage";

    if (::CompareStringOrdinal(pszData[0], -1, postMessageId, _countof(postMessageId) - 1, false) == CSTR_EQUAL)
    {
        ATLENSURE_RETURN_HR(ulDataCount == 2, E_INVALIDARG);

        CComBSTR messageBstr(pszData[1]);
        BSTR param = messageBstr.Detach();
        BOOL succeeded = ::PostMessage(m_websocketHwnd, WM_MESSAGE_SEND, reinterpret_cast<WPARAM>(param), 0);
        if (!succeeded)
        {
            messageBstr.Attach(param);
        }
    }

    return S_OK;
}

STDMETHODIMP BrowserHost::OnScriptError(_In_ IActiveScriptError* pScriptError)
{
    return S_OK;
}

// Window Messages
LRESULT BrowserHost::OnSetMessageHwnd(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    // Store the HWND we will use for posting back to the proxy server
    m_websocketHwnd = reinterpret_cast<HWND>(lParam);

    return 0;
}

LRESULT BrowserHost::OnMessageReceived(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/)
{
    CComBSTR message;
    message.Attach(reinterpret_cast<BSTR>(wParam));

    HRESULT hr = S_OK;

    if (message.Length() > 7)
    {
        if (::wcsncmp(message, L"inject:", 7) == 0)
        {
            CString script(&message[7]);

            int index = script.Find(L":");
            if (index >= 0)
            {
                // Execute the script so it gets injected into our Chakra runtime
                hr = m_spDiagnosticsEngine->EvaluateScript(script.Mid(index + 1), L"main.js");
                FAIL_IF_NOT_S_OK(hr);
            }

            // Done
            return 0;
        }
    }

    LPCWSTR propertyNames[] = { L"id", L"data" };
    LPCWSTR propertyValues[] = { L"onmessage", message };
    hr = m_spDiagnosticsEngine->FireScriptMessageEvent(propertyNames, propertyValues, _countof(propertyNames));
    FAIL_IF_NOT_S_OK(hr);

    return 0;
}

// DWebBrowserEvents2
STDMETHODIMP_(void) BrowserHost::DWebBrowserEvents2_NavigateComplete2(LPDISPATCH pDisp, VARIANT* URL)
{
    if (URL->vt == VT_BSTR)
    {
        CString url(URL->bstrVal);
        CComPtr<IUnknown> spUnknown(pDisp);
        this->OnNavigateCompletePrivate(spUnknown, url);
    }
}

// Helper Functions
HRESULT BrowserHost::OnNavigateCompletePrivate(_In_ CComPtr<IUnknown>& spUnknown, _In_ CString& url)
{
    // Pass in the window object to avoid permission denied script errors
    // for non script accessible objects
    HRESULT hr = S_OK;

    // First make sure we actually have an object to look at
    if (spUnknown.p != nullptr)
    {
        if (m_spWebControl.IsEqualObject(spUnknown))
        {
            LPCWSTR propertyNames[] = { L"id", L"url" };
            LPCWSTR propertyValues[] = { L"onnavigation", url };
            hr = m_spDiagnosticsEngine->FireScriptMessageEvent(propertyNames, propertyValues, _countof(propertyNames));
            FAIL_IF_NOT_S_OK(hr);
        }
    }

    return hr;
}
