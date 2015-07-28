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

HRESULT BrowserHost::SetWebSocketHwnd(_In_ HWND websocketHwnd)
{
    // Store the HWND we will use for posting back to the proxy server
    m_websocketHwnd = websocketHwnd;

    return S_OK;
}

HRESULT BrowserHost::ProcessMessage(_In_ shared_ptr<MessagePacket> spPacket)
{
    HRESULT hr = S_OK;

    // Check if this is a script injection message
    if (spPacket->m_messageType == MessageType::Inject)
    {
        // Execute the script so it gets injected into our Chakra runtime
        hr = m_spDiagnosticsEngine->EvaluateScript(spPacket->m_message, spPacket->m_scriptName);
        FAIL_IF_NOT_S_OK(hr);

        // Done
        return 0;
    }

    LPCWSTR propertyNames[] = { L"id", L"data" };
    LPCWSTR propertyValues[] = { L"onmessage", spPacket->m_message };
    hr = m_spDiagnosticsEngine->FireScriptMessageEvent(propertyNames, propertyValues, _countof(propertyNames));
    FAIL_IF_NOT_S_OK(hr);

    return hr;
}

CString BrowserHost::m_getClientId()
{
	/*
	CString keyPath = L"";

	CRegKey regKey;
	CString clientId;

	// First see if ther is already a key
	if (regKey.Open(HKEY_LOCAL_MACHINE, keyPath, KEY_READ) == ERROR_SUCCESS)
	{

	}
	else 
	{
		_TUCHAR *guidStr = 0x00;

		GUID *pguid = 0x00;

		pguid = new GUID;

		CoCreateGuid(pguid);
		UuidToString(pguid, &guidStr);

		delete pguid;

	}
	*/
	return L"Test_ID";
}

// IDiagnosticsScriptEngineSite
STDMETHODIMP BrowserHost::OnMessage(_In_reads_(ulDataCount)  LPCWSTR* pszData, ULONG ulDataCount)
{
    const wchar_t postMessageId[] = L"postMessage";
    const wchar_t alertMessageId[] = L"alert";

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
    else if (::CompareStringOrdinal(pszData[0], -1, alertMessageId, _countof(alertMessageId) - 1, false) == CSTR_EQUAL)
    {
        ATLENSURE_RETURN_HR(ulDataCount == 2, E_INVALIDARG);

        CComBSTR messageBstr(pszData[1]);
        ::MessageBox(NULL, messageBstr, L"Message from browser", 0);
    }

    return S_OK;
}

STDMETHODIMP BrowserHost::OnScriptError(_In_ IActiveScriptError* pScriptError)
{
    return S_OK;
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
