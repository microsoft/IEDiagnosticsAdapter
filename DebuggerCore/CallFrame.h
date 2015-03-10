//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include "DebuggerStructs.h"
#include "SourceController.h"

using namespace ATL;
using namespace std;

class ATL_NO_VTABLE CallFrame :
    public CComObjectRootEx<CComSingleThreadModel>
{
public:
    BEGIN_COM_MAP(CallFrame)
    END_COM_MAP()

    CallFrame();

    HRESULT Initialize(_In_ DWORD dispatchThreadId, _In_ ULONG id, _In_ shared_ptr<DebugStackFrameDescriptor> spFrameDescriptor, _In_ SourceController* pSourceController);

    HRESULT UpdateInfo();
    HRESULT GetCallFrameInfo(_Out_ shared_ptr<CallFrameInfo>& spFrameInfo, _Out_ CComPtr<IDebugStackFrame>& spStackFrame, _Out_ CComPtr<IDebugProperty>& spLocalsDebugProperty);

private:
    DWORD m_dispatchThreadId;
    ULONG m_id;
    bool m_isInternal;

    CComBSTR m_bstrFunctionName;
    shared_ptr<SourceLocationInfo> m_spSourceLocationInfo;
    shared_ptr<DebugStackFrameDescriptor> m_spFrameDescriptor;

    CComObjPtr<SourceController> m_spSourceController;
};
