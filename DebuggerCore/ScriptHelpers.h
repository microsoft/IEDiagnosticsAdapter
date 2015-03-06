//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include "stdafx.h"

// Helpers for using the script engine's IDispatchEx to create script objects
namespace ScriptHelpers
{
    // Returns the script object from the given site. The script object can be used in the other calls below
    HRESULT GetScriptObject(_In_ IUnknown* pSite, _COM_Outptr_ IDispatchEx** ppScriptObject);

    // Creates a JS Array object with the variants in vValues as array items.
    HRESULT CreateJScriptArray(_In_ IDispatchEx* pDispExScript, _In_ const std::vector<ATL::CComVariant>& vValues, _Out_ ATL::CComVariant& newArray);

    // Creates a JS Object with the key value pairs lsited in mapNameValuePairs.
    HRESULT CreateJScriptObject(_In_ IDispatchEx* pDispExScript, _In_ const std::map<ATL::CString, ATL::CComVariant>& mapNameValuePairs, _Out_ ATL::CComVariant& newObject);

    // Get global namespace from browser object (doc->get_Script)
    HRESULT GetGlobalNamespaceFromBrowser(_In_ IWebBrowser2* pBrowser, _COM_Outptr_ IDispatchEx** ppGlobalNamespace);
    HRESULT GetGlobalNamespaceFromBrowser(_In_ IHTMLDocument2* pDocument, _COM_Outptr_ IDispatchEx** ppGlobalNamespace);

    HRESULT GetIntegerOrDefault(_In_ const VARIANTARG& var, int defaultValue, _Out_ int* number);

    // Gets a number from the given VARIANT input. The possible values of the input are: 2-byte integer, 4-byte integer, 4-byte float, 8-byte double.
    // The roundUp argument specifies the behaviour with float/doubles as to round up or down.
    HRESULT GetNumber(_In_ const VARIANTARG& var, BOOL roundUp, _Out_ LONGLONG* number);

    // Returns a vector of CComVariant from the script array IDispatch
    HRESULT GetVectorFromScriptArray(_In_ const VARIANT& scriptArrayVar, _Out_ std::vector<ATL::CComVariant>& result);

    // Turns a CComVariant into a json string
    HRESULT JSONStringify(_In_ IDispatchEx* pDispExScript, _In_ ATL::CComVariant& objectToStringify, _Out_ ATL::CComBSTR& jsonString);
};