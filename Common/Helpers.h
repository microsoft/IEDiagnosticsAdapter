//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include <functional>

// Define some assert helper macros
#define FAIL_IF_NOT_S_OK(hr) ATLENSURE_RETURN_HR(hr == S_OK, SUCCEEDED(hr) ? E_FAIL : hr)
#define THROW_IF_NOT_S_OK(hr) ATLENSURE_THROW(hr == S_OK, SUCCEEDED(hr) ? E_FAIL : hr)
#define FAIL_IF_ERROR(jec) ATLENSURE_RETURN_HR(jec == JsNoError, jec)
#define THROW_IF_ERROR(jec) ATLENSURE_THROW(jec == JsNoError, jec)

namespace Helpers
{
    BOOL EnumWindowsHelper(_In_ const function<BOOL(HWND)>& callbackFunc);
    BOOL EnumChildWindowsHelper(HWND hwndParent, _In_ const function<BOOL(HWND)>& callbackFunc);
    bool IsWindowClass(_In_ const HWND hwnd, _In_ LPCWSTR pszWindowClass);

    HRESULT GetCurrentModuleWithoutRef(_Out_ HMODULE& moduleOut);
    HRESULT ReadFileFromModule(_In_ LPCWSTR resourceName, _Out_ CString& sFileData);
    HRESULT ExpandEnvironmentString(_In_z_ LPCWSTR path, _Out_ CString& expandedString);
    bool Is64OS();

    HRESULT GetDocumentFromSite(_In_ IUnknown* spSite, _Out_ CComPtr<IDispatch>& spDocumentOut);
    HRESULT GetDocumentFromHwnd(_In_ const HWND browserHwnd, _Out_ CComPtr<IHTMLDocument2>& spDocument);
    UINT GetHtmlDocumentMessage();
    static UINT s_getHtmlDocumentMessage;

    HRESULT StartDiagnosticsMode(_In_ IHTMLDocument2* pDocument, REFCLSID clsid, _In_ LPCWSTR path, REFIID iid, _COM_Outptr_opt_ void** ppOut);

    CStringA EscapeJsonString(_In_ const CString& value);
    CStringA GetFileVersion(_In_ LPCWSTR filePath);

	CStringA GetLastErrorMessage();
}