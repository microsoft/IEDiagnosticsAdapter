//
// Copyright (C) Microsoft. All rights reserved.
//

#include "StdAfx.h"
#include "ScriptHelpers.h"

using namespace std;
using namespace ATL;

namespace ScriptHelpers
{
    HRESULT GetScriptObject(_In_ IUnknown* pSite, _COM_Outptr_ IDispatchEx** ppScriptObject)
    {
        ATLENSURE_RETURN_HR(ppScriptObject != nullptr, E_INVALIDARG);
        *ppScriptObject = nullptr;
        ATLENSURE_RETURN_HR(pSite != nullptr, E_INVALIDARG);

        // Get the document directly from WPC
        HRESULT hr = S_OK;
        CComQIPtr<IHTMLDocument2> spHTMLDoc(pSite);
        if (spHTMLDoc.p == nullptr)
        {
            // Get the document via WebOC
            CComQIPtr<IWebBrowser2> spWebBrowser(pSite);
            ATLENSURE_RETURN_HR(spWebBrowser.p != nullptr, E_NOINTERFACE);

            CComPtr<IDispatch> spDispDoc;
            hr = spWebBrowser->get_Document(&spDispDoc.p);
            ATLENSURE_RETURN_HR(hr == S_OK && spDispDoc.p != nullptr, E_FAIL);

            spDispDoc.QueryInterface(&spHTMLDoc);
        }

        ATLENSURE_RETURN_HR(hr == S_OK && spHTMLDoc.p != nullptr, E_UNEXPECTED);

        CComPtr<IDispatch> spDispScript;
        hr = spHTMLDoc->get_Script(&spDispScript);
        ATLENSURE_RETURN_HR(hr == S_OK && spDispScript.p != nullptr, SUCCEEDED(hr) ? E_FAIL : hr);

        hr = spDispScript.QueryInterface(ppScriptObject);
        ATLENSURE_RETURN_HR(hr == S_OK && *ppScriptObject != nullptr, E_NOINTERFACE);

        return S_OK;
    }

    HRESULT CreateJScriptItem(_In_ IDispatchEx* pDispExScript, _In_ CString sType, _In_ const map<CString, CComVariant>& mapNameValuePairs, _Out_ CComVariant& newItem);

    HRESULT CreateJScriptArray(_In_ IDispatchEx* pDispExScript, _In_ const vector<CComVariant>& vValues, _Out_ CComVariant& newArray)
    {
        CComVariant createdArray;

        // Convert the vector into a map of name/value pairs
        map<CString, CComVariant> mapPairs;
        for (size_t i = 0; i < vValues.size(); i++)
        {
            // The name in an array is just the index starting at 0
            CString index;
            index.Format(L"%Iu", i);

            // Create the pair and add it to the map
            std::pair<CString, CComVariant> nameValuePair;
            nameValuePair.first = index;
            nameValuePair.second = vValues[i];
            mapPairs.insert(nameValuePair);
        }

        HRESULT hr = CreateJScriptItem(pDispExScript, L"Array", mapPairs, createdArray);
        BPT_FAIL_IF_NOT_S_OK(hr);

        newArray = createdArray;

        return hr;
    }

    HRESULT CreateJScriptObject(_In_ IDispatchEx* pDispExScript, _In_ const map<CString, CComVariant>& mapNameValuePairs, _Out_ CComVariant& newObject)
    {
        CComVariant createdObject;
        HRESULT hr = CreateJScriptItem(pDispExScript, L"Object", mapNameValuePairs, createdObject);
        BPT_FAIL_IF_NOT_S_OK(hr);

        newObject = createdObject;

        return hr;
    }

    HRESULT CreateJScriptItem(_In_ IDispatchEx* pDispExScript, _In_ CString sType, _In_ const map<CString, CComVariant>& mapNameValuePairs, _Out_ CComVariant& newItem)
    {
        HRESULT hr = S_OK;

        // Create the variables we need
        DISPPARAMS dispParamsNoArgs = {NULL, NULL, 0, 0};
        DISPID putId = DISPID_PROPERTYPUT;
        CComVariant varCreatedObject;
        DISPID dispId;

        // Find the javascript object using the IDispatchEx of the script engine
        CComBSTR name(sType);
        hr = pDispExScript->GetDispID(name, 0, &dispId);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Create the jscript object by calling its constructor
        hr = pDispExScript->InvokeEx(dispId, LOCALE_USER_DEFAULT, DISPATCH_CONSTRUCT, &dispParamsNoArgs, &varCreatedObject, NULL, NULL);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Query the IDispatchEx interface from the newly created object so we can add properties to it
        CComQIPtr<IDispatchEx> pDispExObj(varCreatedObject.pdispVal);
        ATLENSURE_RETURN_HR(pDispExObj != NULL, E_UNEXPECTED);

        // Iterate over the map and build up each property on the object
        // Using a const_iterator since we are not going to change the values.
        for (map<CString, CComVariant>::const_iterator it = mapNameValuePairs.begin(); it != mapNameValuePairs.end(); ++it)
        {
            // Create the property by using the special fdexNameEnsure flag
            CComBSTR propertyName(it->first);
            hr = pDispExObj->GetDispID(propertyName, fdexNameEnsure, &dispId);
            BPT_FAIL_IF_NOT_S_OK(hr);

            VARIANTARG params[1];
            params[0] = it->second;

            DISPPARAMS dispParams = {
                params, // parameter
                &putId, // DISPID of the action (In our case PropertyPut)
                1,      // no. of parameters
                1 };    // no. of actions

            // Add the value to the dispId of the newly created property
            hr = pDispExObj->InvokeEx(dispId, LOCALE_USER_DEFAULT, 
                DISPATCH_PROPERTYPUTREF, &dispParams,
                NULL, NULL, NULL);
            BPT_FAIL_IF_NOT_S_OK(hr);
        }

        CComVariant result(varCreatedObject.pdispVal);
        newItem = result;

        return hr;
    }

    HRESULT GetGlobalNamespaceFromBrowser(_In_ IWebBrowser2* pBrowser, _COM_Outptr_ IDispatchEx** ppGlobalNamespace)
    {
        ATLENSURE_RETURN_HR(pBrowser != nullptr, E_INVALIDARG);
        ATLENSURE_RETURN_HR(ppGlobalNamespace != nullptr, E_INVALIDARG);

        CComPtr<IDispatch> spDispDoc;
        HRESULT hr = pBrowser->get_Document(&spDispDoc.p);
        ATLENSURE_RETURN_HR(hr == S_OK && spDispDoc.p != nullptr, E_FAIL);

        CComQIPtr<IHTMLDocument2> spHTMLDoc(spDispDoc);
        ATLENSURE_RETURN_HR(spHTMLDoc.p != nullptr, E_UNEXPECTED);

        return GetGlobalNamespaceFromBrowser(spHTMLDoc, ppGlobalNamespace);
    }

    HRESULT GetGlobalNamespaceFromBrowser(_In_ IHTMLDocument2* pDocument, _COM_Outptr_ IDispatchEx** ppGlobalNamespace)
    {
        ATLENSURE_RETURN_HR(pDocument != nullptr, E_INVALIDARG);
        ATLENSURE_RETURN_HR(ppGlobalNamespace != nullptr, E_INVALIDARG);

        CComPtr<IDispatch> spDispScript;
        HRESULT hr = pDocument->get_Script(&spDispScript);
        ATLENSURE_RETURN_HR(hr == S_OK && spDispScript.p != nullptr, E_FAIL);

        hr = spDispScript.QueryInterface(ppGlobalNamespace);
        ATLENSURE_RETURN_HR((hr == S_OK) && ((*ppGlobalNamespace) != nullptr), E_NOINTERFACE);

        return S_OK;
    }

    HRESULT GetIntegerOrDefault(_In_ const VARIANTARG& var, int defaultValue, _Out_ int* number)
    {
        ATLENSURE_RETURN_HR(number, E_INVALIDARG);

        if (var.vt == VT_ERROR && var.scode == DISP_E_PARAMNOTFOUND)
        {
            *number = defaultValue;
        }
        else
        {
            switch (var.vt)
            {
            case VT_I2:
                *number = var.iVal;
                break;

            case VT_I4:
                *number = var.lVal;
                break;

            default:
                return E_INVALIDARG;
            }
        }

        return S_OK;
    }

    HRESULT GetNumber(_In_ const VARIANTARG& var, BOOL roundUp, _Out_ LONGLONG* number)
    {
        HRESULT hr = S_OK;

        switch (var.vt)
        {
        case VT_I2:
            *number = var.iVal;
            break;

        case VT_I4:
            *number = var.lVal;
            break;

        case VT_R4:
            {
                float fltVal = roundUp ? ceil(var.fltVal) : floor(var.fltVal);
                *number = static_cast<LONGLONG>(fltVal);
            }
            break;

        case VT_R8:
            {
                double dblVal = roundUp ? ceil(var.dblVal) : floor(var.dblVal);
                if (dblVal >= MAXLONGLONG)
                {
                    *number = MAXLONGLONG;
                }
                else if (dblVal <= MINLONGLONG)
                {
                    *number = MINLONGLONG;
                }
                else
                {
                    *number = static_cast<LONGLONG>(dblVal);
                }
            }
            break;

        default:
            hr = E_INVALIDARG;
        }

        return hr;
    }

    HRESULT GetVectorFromScriptArray(_In_ const VARIANT& scriptArrayVar, _Out_ vector<CComVariant>& result)
    {
        result.clear();

        ATLENSURE_RETURN_HR(scriptArrayVar.vt == VT_DISPATCH, E_INVALIDARG);
        ATLENSURE_RETURN_HR(scriptArrayVar.pdispVal != nullptr, E_INVALIDARG);

        HRESULT hr;

        CComPtr<IDispatch> spArrayDispatch(scriptArrayVar.pdispVal);

        CComVariant lengthVar;
        hr = spArrayDispatch.GetPropertyByName(L"length", &lengthVar);
        BPT_FAIL_IF_NOT_S_OK(hr);
        ATLENSURE_RETURN_HR(
            (lengthVar.vt == VT_I4 && lengthVar.iVal >= 0) || 
            (lengthVar.vt == VT_I8 && lengthVar.llVal >= 0), E_FAIL);

        size_t length = static_cast<size_t>(lengthVar.vt == VT_I4 ? lengthVar.iVal : lengthVar.llVal);
        result.reserve(length);

        CString indexName;
        for (size_t i = 0; i < length; i++)
        {
            result.push_back(CComVariant());
            indexName.Format(L"%Iu", i);
            hr = spArrayDispatch.GetPropertyByName(indexName, &(result.back()));
            BPT_FAIL_IF_NOT_S_OK(hr);
        }

        return S_OK;
    }

    HRESULT JSONStringify(_In_ IDispatchEx* pDispExScript, _In_ CComVariant& objectToStringify, _Out_ CComBSTR& jsonString)
    {
        ATLENSURE_RETURN_HR(pDispExScript != nullptr, E_INVALIDARG);

        jsonString.Empty();

        // Find the javascript JSON object id using the IDispatchEx of the script engine
        CComBSTR name(L"JSON");
        DISPID jsonId;
        HRESULT hr = pDispExScript->GetDispID(name, 0, &jsonId);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Get the actual JSON object using the id
        DISPPARAMS dispParamsNoArgs = {NULL, NULL, 0, 0};
        CComVariant varJSONObject;
        hr = pDispExScript->InvokeEx(jsonId, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET, &dispParamsNoArgs, &varJSONObject, NULL, NULL);
        BPT_FAIL_IF_NOT_S_OK(hr);

        CComQIPtr<IDispatchEx> spJSON(varJSONObject.pdispVal);
        if (spJSON.p != nullptr)
        {
            // Find the stringify method's id from the JSON object
            CComBSTR stringifyName(L"stringify");
            DISPID stringifyId;
            hr = spJSON->GetDispID(stringifyName, 0, &stringifyId);
            BPT_FAIL_IF_NOT_S_OK(hr);

            // Create the parameter from the object passed in
            DISPPARAMS dispParamsStringify = {NULL, NULL, 0, 0};
            dispParamsStringify.rgvarg = &objectToStringify;
            dispParamsStringify.cArgs = 1;

            // Call the stringify function
            CComVariant varJSONString;
            hr = spJSON->InvokeEx(stringifyId, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &dispParamsStringify, &varJSONString, NULL, NULL);
            BPT_FAIL_IF_NOT_S_OK(hr);

            jsonString = varJSONString.bstrVal;
        }

        return hr;
    }
}