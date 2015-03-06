//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include "DebuggerStructs.h"
#include "SourceController.h"
#include "SourceEventListener.h"

using namespace ATL;
using namespace std;

class SourceController;
class SourceEventListener;

class ATL_NO_VTABLE SourceNode :
    public CComObjectRootEx<CComSingleThreadModel>
{
public:
    BEGIN_COM_MAP(SourceNode)
    END_COM_MAP()

    SourceNode();
    void FinalRelease();

    HRESULT Initialize(_In_ DWORD dispatchThreadId, _In_ SourceController* pSourceController, _In_ ULONG docId, _In_ IDebugApplicationNode* pDebugApplicationNode, _In_ ULONG parentDocId);

    ULONG GetDocId() const;
    bool IsDirty() const;

    HRESULT GetDebugApplicationNode(_Out_ CComPtr<IDebugApplicationNode>& spDebugApplicationNode);

    HRESULT AddChild(_In_ ULONG childDocId);
    HRESULT RemoveChild(_In_ ULONG childDocId);
    HRESULT GetChildren(_Out_ map<ULONG, bool>& childDocIdMap);

    HRESULT UpdateInfo();
    HRESULT GetDocumentInfo(_Out_ shared_ptr<DocumentInfo>& spDocumentInfo, _Out_ CComPtr<IDebugDocumentText>& spDebugDocumentText);

private:
    void UpdateUrl();
    void UpdateIsDynamicCode();
    void UpdateMimeType();
    void UpdateSourceTextLength();
    void UpdateSourceText();

private:
    DWORD m_dispatchThreadId;
    ULONG m_docId;
    ULONG m_parentDocId;

    ULONG m_textLength;

    bool m_isDynamicCode;
    bool m_isDirty;

    CComBSTR m_bstrUrl;
    CComBSTR m_bstrMimeType;
    CComBSTR m_bstrText;

    CComObjPtr<SourceEventListener> m_spListener;

    CComPtr<IDebugApplicationNode> m_spDebugApplicationNode;
    CComPtr<IDebugDocumentText> m_spDebugDocumentText;

    shared_ptr<DocumentInfo> m_spDocumentInfo;
    map<ULONG, bool> m_childDocIdMap;
};
