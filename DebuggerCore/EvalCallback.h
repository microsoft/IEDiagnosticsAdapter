//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include "comdef.h"
#include "Helpers.h"

class ATL_NO_VTABLE EvalCallback :
    public CComObjectRootEx<CComMultiThreadModelNoCS>,
    public IDebugExpressionCallBack
{
public:
    BEGIN_COM_MAP(EvalCallback)
        COM_INTERFACE_ENTRY(IDebugExpressionCallBack)
        COM_INTERFACE_ENTRY2(IUnknown, IDebugExpressionCallBack)
    END_COM_MAP()

    EvalCallback() : 
        m_isCompleted(false)
    {
    }

    STDMETHOD(Initialize)(void)
    {
        HANDLE handle = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
        ATLENSURE_RETURN_HR(handle != nullptr, ::AtlHresultFromLastError());
        m_hCompletionEvent.Attach(handle);

        return S_OK;
    }

    // IDebugExpressionCallBack
    STDMETHOD(onComplete)(void)
    {
        ATLENSURE_RETURN_HR(m_hCompletionEvent != nullptr, E_NOT_VALID_STATE);
        m_isCompleted = true;

        ::SetEvent(m_hCompletionEvent);
        return S_OK;
    }

    // Eval Operations
    HRESULT WaitForCompletion(void)
    {
        ATLENSURE_RETURN_HR(m_hCompletionEvent != nullptr, E_NOT_VALID_STATE);

        if (F12::WaitAndPumpMessages(m_hCompletionEvent) == S_OK)
        {
            ::ResetEvent(m_hCompletionEvent);
        }

        if (m_isCompleted) 
        {
            return S_OK;
        }

        return E_ABORT;
    }

    HRESULT Abort() 
    {
        ATLENSURE_RETURN_HR(m_hCompletionEvent != nullptr, E_NOT_VALID_STATE);
        ::SetEvent(m_hCompletionEvent);
        return S_OK;
    }

private:
    CHandle m_hCompletionEvent;
    bool m_isCompleted;
};
