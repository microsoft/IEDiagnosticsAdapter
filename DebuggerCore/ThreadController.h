//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include "DebuggerCore_h.h"
#include "PDMEventMessageQueue.h"
#include "DebuggerStructs.h"
#include "EvalCallback.h"
#include "SourceController.h"
#include "ThreadEventListener.h"
#include "CallFrame.h"

using namespace ATL;
using namespace std;

class ThreadEventListener;

class ATL_NO_VTABLE ThreadController :
    public CComObjectRootEx<CComMultiThreadModel>
{
public:
    DECLARE_NOT_AGGREGATABLE(ThreadController)

    BEGIN_COM_MAP(ThreadController)
    END_COM_MAP()

    ThreadController();

public:
    HRESULT Initialize(_In_ HWND hwndDebugPipeHandler, _In_ HANDLE hBreakNotificationComplete, _In_ bool isDocked, _In_ PDMEventMessageQueue* pMessageQueue, _In_ SourceController* pSourceController);

    bool IsConnected() const;
    bool IsAtBreak();
    bool IsEnabled();

    HRESULT SetRemoteDebugApplication(_In_ IRemoteDebugApplication* pDebugApplication);
    HRESULT SetDockState(_In_ bool isDocked);
    HRESULT SetEnabledState(_In_ bool isEnabled);

    // Debugger Actions
    HRESULT Connect();
    HRESULT Disconnect();
    HRESULT Pause();
    HRESULT Resume(_In_ BREAKRESUMEACTION breakResumeAction, _In_ ERRORRESUMEACTION errorResumeAction = ERRORRESUMEACTION_AbortCallAndReturnErrorToCaller);
    HRESULT GetThreadDescriptions(_Inout_ vector<CComBSTR>& spThreadDescriptions);
    HRESULT SetBreakOnFirstChanceExceptions(_In_ bool breakOnFirstChance);
    HRESULT SetBreakOnNewWorker(_In_ bool enable);
    HRESULT SetNextEvent(_In_ const CString& eventBreakpointType);

    // Break Mode Actions
    HRESULT GetCurrentThreadDescription(_Out_ CComBSTR& threadDescription);
    HRESULT GetBreakEventInfo(_Out_ shared_ptr<BreakEventInfo>& spBreakInfo);
    HRESULT GetCallFrames(_Inout_ vector<shared_ptr<CallFrameInfo>>& spFrames);
    HRESULT GetLocals(_In_ ULONG frameId, _Out_ ULONG& propertyId);
    HRESULT EnumeratePropertyMembers(_In_ ULONG propertyId, _In_ UINT radix, _Inout_ vector<shared_ptr<PropertyInfo>>& spPropertyInfos);
    HRESULT SetPropertyValueAsString(_In_ ULONG propertyId, _In_ BSTR* pValue, _In_ UINT radix);
    HRESULT Eval(_In_ ULONG frameId, _In_ const CComBSTR& evalString, _In_ ULONG radix, _Out_ shared_ptr<PropertyInfo>& spPropertyInfo);
    HRESULT CanSetNextStatement(_In_ ULONG docId, _In_ ULONG start);
    HRESULT SetNextStatement(_In_ ULONG docId, _In_ ULONG start);

    // PDM Event Notifications
    HRESULT OnPDMBreak(_In_ IRemoteDebugApplicationThread* pRDAThread, _In_ BREAKREASON br, _In_ const CComBSTR& description, _In_ WORD errorId, _In_ bool isFirstChance, _In_ bool isUserUnhandled);
    HRESULT OnPDMClose();
    HRESULT OnWaitCallback();

    // IE Break Notification
    HRESULT BeginBreakNotification();

private:
    HRESULT GetRemoteDebugApplication(_Out_ CComPtr<IRemoteDebugApplication>& spRemoteDebugApplication);
    HRESULT CallDebuggerThread(_In_ PDMThreadCallbackMethod method, _In_opt_ DWORD_PTR pController, _In_opt_ DWORD_PTR pArgs);
    HRESULT PopulateCallStack();
    HRESULT EvaluateConditionalBreakpoint(_In_ ULONG frameId, _In_ CComBSTR& condition, _In_ bool isTracepoint, _Out_ bool& result);
    HRESULT PopulatePropertyInfo(_Inout_ DebugPropertyInfo& propInfo, _Out_ shared_ptr<PropertyInfo>& spPropertyInfo);
    HRESULT WaitForBreakNotification();
    HRESULT ClearBreakInfo();

    // This must be static to ensure unique call frame id's across debugging sessions.
    static ULONG s_nextFrameId;
    static ULONG CreateUniqueFrameId() { return ThreadController::s_nextFrameId++; }

    static ULONG s_nextPropertyId;
    static ULONG CreateUniquePropertyId() { return ThreadController::s_nextPropertyId++; }

    static const int s_maxConcurrentEvalCount;
    static const CString s_f12EvalIdentifierPrefix;
    static const CString s_f12EvalIdentifierPrefixEnd;
    static const CString s_f12EvalIdentifier;

    // public static XHR_BREAKPOINT_FLAG: string in breakpoint.ts needs to be kept in sync with this string in order to display the correct message in the call stack window when an XHR breakpoint is hit
    static const CComBSTR s_xhrBreakpointFlag;

private:
    DWORD m_dispatchThreadId;
    HWND m_hwndDebugPipeHandler;
    HANDLE m_hBreakNotificationComplete;
    bool m_isDocked;
    bool m_isConnected;
    CString m_nextBreakEventType;

    DWORD m_mainIEScriptThreadId;
    DWORD m_currentPDMThreadId;

    // Thread safety locks
    CComAutoCriticalSection m_csThreadControllerLock;
    CComAutoCriticalSection m_csCallFramesLock;

    // Call frames
    vector<ULONG> m_callFrames;
    map<ULONG, shared_ptr<CallFrameInfo>> m_callFramesInfoMap;
    map<ULONG, CComObjPtr<CallFrame>> m_callFrameMap;

    // Locals, evals, and watches
    map<ULONG, ULONG> m_localsMap;
    map<ULONG, CComPtr<IDebugStackFrame>> m_debugFrameMap;
    map<ULONG, CComPtr<IDebugProperty>> m_propertyMap;
    vector<pair<CComPtr<IDebugExpression>, CComObjPtr<EvalCallback>>> m_evalCallbacks;

    // Debugging
    shared_ptr<BreakEventInfo> m_spCurrentBreakInfo;
    CComPtr<IRemoteDebugApplicationThread> m_spCurrentBrokenThread;
    CComPtr<IRemoteDebugApplication> m_spDebugApplication;

    // Components
    CComObjPtr<ThreadEventListener> m_spThreadEventListener;
    CComObjPtr<PDMEventMessageQueue> m_spMessageQueue;
    CComObjPtr<SourceController> m_spSourceController;
};

