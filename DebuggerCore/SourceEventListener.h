//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include "SourceController.h"
#include "ComObjPtr.h"

using namespace ATL;
using namespace std;

class SourceController;

class ATL_NO_VTABLE SourceEventListener :
    public CComObjectRootEx<CComSingleThreadModel>,
    public IDebugApplicationNodeEvents,
    public IDebugDocumentTextEvents
{
public:
    BEGIN_COM_MAP(SourceEventListener)
        COM_INTERFACE_ENTRY(IDebugApplicationNodeEvents)
        COM_INTERFACE_ENTRY(IDebugDocumentTextEvents)
    END_COM_MAP()

    SourceEventListener();

    HRESULT Initialize(_In_ DWORD dispatchThreadId, _In_ SourceController* pSourceController, _In_ ULONG ownerId, _In_ IDebugApplicationNode* pDebugApplicationNode, _In_opt_ IDebugDocumentText* pDebugDocumentText);
    HRESULT Deinitialize();

    // IDebugApplicationNodeEvents
    STDMETHOD(onAddChild)(_In_ IDebugApplicationNode* prddpChild);
    STDMETHOD(onRemoveChild)(_In_ IDebugApplicationNode* prddpChild);
    STDMETHOD(onDetach)();
    STDMETHOD(onAttach)(_In_ IDebugApplicationNode* prddpParent);

    // IDebugDocumentTextEvents
    STDMETHOD(onDestroy)();
    STDMETHOD(onInsertText)(_In_ ULONG cCharacterPosition, _In_ ULONG cNumToInsert);
    STDMETHOD(onRemoveText)(_In_ ULONG cCharacterPosition, _In_ ULONG cNumToRemove);
    STDMETHOD(onReplaceText)(_In_ ULONG cCharacterPosition, _In_ ULONG cNumToReplace);
    STDMETHOD(onUpdateTextAttributes)(_In_ ULONG cCharacterPosition, _In_ ULONG cNumToUpdate);
    STDMETHOD(onUpdateDocumentAttributes)(_In_ TEXT_DOC_ATTR textdocattr);

private:
    HRESULT OnSourceTextChange();

private:
    DWORD m_dispatchThreadId;
    DWORD m_dwNodeCookie;
    DWORD m_dwTextCookie;

    bool m_advisedNodeEvents;
    bool m_advisedTextEvents;
    CComPtr<IDebugApplicationNode> m_spDebugApplicationNode;
    CComPtr<IDebugDocumentText> m_spDebugDocumentText;

    ULONG m_ownerId;
    CComObjPtr<SourceController> m_spSourceController;
};
