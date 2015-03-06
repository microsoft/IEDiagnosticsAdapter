//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include "DebuggerCore_h.h"
#include "PDMEventMessageQueue.h"
#include "DebuggerStructs.h"
#include "SourceNode.h"

using namespace ATL;
using namespace std;

class SourceNode;

class ATL_NO_VTABLE SourceController :
    public CComObjectRootEx<CComMultiThreadModel>
{
public:
    DECLARE_NOT_AGGREGATABLE(SourceController)

    BEGIN_COM_MAP(SourceController)
    END_COM_MAP()

    SourceController();

public:
    HRESULT Initialize(_In_ PDMEventMessageQueue* pMessageQueue, _In_ const HWND hwndDebugPipeHandler);

    HRESULT SetRemoteDebugApplication(_In_ IRemoteDebugApplication* pDebugApplication);
    HRESULT SetEnabledState(_In_ bool isEnabled);
    HRESULT Clear();
    
    // PDM Operations
    HRESULT EnumerateSources();
    HRESULT GetSourceLocationForCallFrame(_In_ IDebugCodeContext* pDebugCodeContext, _Out_ shared_ptr<SourceLocationInfo>& spSourceLocationInfo, _Out_ bool& isInternal);
    HRESULT GetBreakpointInfoForBreak(_In_ ULONG docId, _In_ ULONG start, _Out_ shared_ptr<BreakpointInfo>& spBreakpointInfo);
    HRESULT GetBreakpointInfosForEventBreak(_In_ const CString& eventType, _Out_ vector<shared_ptr<BreakpointInfo>>& breakpointList);

    // Dispatch Operations
    HRESULT RefreshSources(_Inout_ vector<shared_ptr<DocumentInfo>>& added, _Inout_ vector<shared_ptr<DocumentInfo>>& updated, _Inout_ vector<ULONG>& removed, _Inout_ vector<shared_ptr<ReboundBreakpointInfo>>& rebound);
    HRESULT GetSourceText(_In_ ULONG docId, _Out_ CComBSTR& sourceText);
    HRESULT GetCodeContext(_In_ ULONG docId, _In_ ULONG start, _Out_ CComPtr<IDebugCodeContext>& spDebugCodeContext);
    HRESULT AddCodeBreakpoint(_In_ ULONG docId, _In_ ULONG start, _In_ const CComBSTR& condition, _In_ bool isTracepoint, _Out_ shared_ptr<BreakpointInfo>& spBreakpointInfo);
    HRESULT AddEventBreakpoint(_In_ shared_ptr<vector<CComBSTR>> eventTypes, _In_ bool isEnabled, _In_ const CComBSTR& condition, _In_ bool isTracepoint, _Out_ shared_ptr<BreakpointInfo>& spBreakpointInfo);
    HRESULT AddPendingBreakpoint(_In_ const CComBSTR& url, _In_ ULONG start, _In_ const CComBSTR& condition, _In_ bool isEnabled, _In_ bool isTracepoint, _Out_ ULONG& pBreakpointId);
    HRESULT RemoveBreakpoint(_In_ ULONG breakpointId);
    HRESULT UpdateBreakpoint(_In_ ULONG breakpointId, _In_ const CComBSTR& condition, _In_ bool isTracepoint);
    HRESULT SetBreakpointEnabledState(_In_ ULONG breakpointId, _In_ bool enable);
    HRESULT GetBreakpointId(_In_ ULONG docId, _In_ ULONG start, _Out_ ULONG& breakpointId);
    HRESULT RebindAllCodeBreakpoints();
    HRESULT UnbindAllCodeBreakpoints(_In_ const bool removeFromEngine);
    HRESULT RebindAllEventBreakpoints(_In_ const bool updateEventListener);
    HRESULT UnbindAllEventBreakpoints(_In_ const bool removeListeners);
    HRESULT GetIsMappedEventType(_In_ const CString& eventType, _Out_ bool& isMapped);
    HRESULT SetBreakOnNewWorker(_In_ bool enable);

    // PDM Event Notifications
    HRESULT OnAddChild(_In_ ULONG parentId, _In_ IDebugApplicationNode* pChildNode);
    HRESULT OnRemoveChild(_In_ ULONG parentId, _In_ IDebugApplicationNode* pChildNode);
    HRESULT OnUpdate(_In_ ULONG docId);

    HRESULT CreateUniqueBpIdForMutationBreakpoint(_Out_ ULONG& id);
private:
    HRESULT AddAllSourceNodes(_In_ IDebugApplicationNode* pRootNode, _In_ const ULONG& containerId);
    HRESULT AddNode(_In_ IDebugApplicationNode* pRootNode, _In_ const ULONG& parentId, _Out_ ULONG& newId);
    HRESULT RemoveNode(_In_ IDebugApplicationNode* pRootNode, _In_ const ULONG& parentId);
    HRESULT GetSourceLocation(_In_ IDebugCodeContext* pDebugCodeContext, _Out_ shared_ptr<SourceLocationInfo>& spSourceLocationInfo);
    HRESULT GetCodeContextFromDocId(_In_ ULONG docId, _In_ ULONG start, _Out_ CComPtr<IDebugCodeContext>& spDebugCodeContext);
    HRESULT GetBreakpointIdFromSourceLocation(_In_ ULONG docId, _In_ ULONG start, _Out_ ULONG& breakpointId);
    HRESULT FindExistingBreakpoint(_In_ const shared_ptr<SourceLocationInfo>& spSourceLocationInfo, _Out_ shared_ptr<BreakpointInfo>& spBreakpointInfo);
    HRESULT SetBreakpointState(_In_ shared_ptr<BreakpointInfo>& spBreakpointInfo, _In_ BREAKPOINT_STATE state);
    HRESULT UnbindBreakpoints(_In_ ULONG docId, _In_ bool removeFromEngine);
    HRESULT RebindBreakpoints(_In_ const shared_ptr<DocumentInfo>& spDocInfo);
    HRESULT ResolveBreakpoint(_In_ ULONG bpId, _In_ ULONG prevDocId, _In_ ULONG newDocId);
    HRESULT AddBreakpointInternal(_In_ const shared_ptr<BreakpointInfo>& spBreakpointInfo);
    HRESULT UpdateEventTypeMap(_In_ const vector<CComBSTR>& eventTypes, _In_ const ULONG breakpointId, _In_ const bool addEvents);

    // This must be static to ensure unique doc id's across debugging sessions.
    static ULONG s_nextDocId;
    static ULONG CreateUniqueDocId() { return SourceController::s_nextDocId++; }
    static ULONG s_nextBpId;
    static ULONG CreateUniqueBpId() { return SourceController::s_nextBpId++; } 

private:
    DWORD m_dispatchThreadId;
    HWND m_hwndDebugPipeHandler;
    bool m_isBreakingOnNewWorker;
    bool m_shouldBindBreakpoints;
    bool m_isEnabled;
    bool m_isEventListenerRegistered;

    CComAutoCriticalSection m_csSourcesLock;

    // Document changes
    map<ULONG, vector<ULONG>> m_resolvedBreakpointsMap;
    vector<ULONG> m_removedDocuments;
    map<ULONG, bool> m_updatedDocumentInfoMap;
    map<ULONG, bool> m_currentDocumentInfoMap;

    // Source nodes
    map<ULONG, CComObjPtr<SourceNode>> m_sourceNodeMap;
    map<CComPtr<IDebugApplicationNode>, ULONG> m_sourceNodeAtApplicationMap;
    map<ULONG, pair<shared_ptr<DocumentInfo>, CComPtr<IDebugDocumentText>>> m_documentInfoMap;

    // Breakpoints
    map<ULONG, shared_ptr<BreakpointInfo>> m_breakpointsMap;
    map<ULONG, vector<ULONG>> m_breakpointsAtDocIdMap;
    map<CComBSTR, vector<ULONG>> m_breakpointsAtUrlMap;
    map<CComBSTR, vector<ULONG>> m_breakpointsAtEventTypeMap;

    CComObjPtr<PDMEventMessageQueue> m_spMessageQueue;
    CComPtr<IRemoteDebugApplication> m_spDebugApplication;
};
