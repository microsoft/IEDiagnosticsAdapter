//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

#include <activdbg100.h>

class ATL_NO_VTABLE BrowserMessageQueue :
    public CComObjectRootEx<CComMultiThreadModel>,
    public IRemoteDebugApplicationEvents,
    public IDebugApplicationThreadEvents110
{
public:
    BEGIN_COM_MAP(BrowserMessageQueue)
        COM_INTERFACE_ENTRY(IRemoteDebugApplicationEvents)
        COM_INTERFACE_ENTRY(IDebugApplicationThreadEvents110)
    END_COM_MAP()

    BrowserMessageQueue();

    // IRemoteDebugApplicationEvents
    STDMETHOD(OnConnectDebugger)(__RPC__in_opt IApplicationDebugger* pad) { return S_OK; }
    STDMETHOD(OnDisconnectDebugger)() { return S_OK; }
    STDMETHOD(OnSetName)(__RPC__in LPCOLESTR pstrName) { return S_OK; }
    STDMETHOD(OnDebugOutput)(__RPC__in LPCOLESTR pstr) { return S_OK; }
    STDMETHOD(OnClose)() { return S_OK; }
    STDMETHOD(OnEnterBreakPoint)(__RPC__in_opt IRemoteDebugApplicationThread* prdat);
    STDMETHOD(OnLeaveBreakPoint)(__RPC__in_opt IRemoteDebugApplicationThread* prdat);
    STDMETHOD(OnCreateThread)(__RPC__in_opt IRemoteDebugApplicationThread* prdat) { return S_OK; }
    STDMETHOD(OnDestroyThread)(__RPC__in_opt IRemoteDebugApplicationThread* prdat) { return S_OK; }
    STDMETHOD(OnBreakFlagChange)(APPBREAKFLAGS abf, __RPC__in_opt IRemoteDebugApplicationThread* prdatSteppingThread) { return S_OK; }

    // IDebugApplicationThreadEvents110
    STDMETHOD(OnSuspendForBreakPoint)();
    STDMETHOD(OnResumeFromBreakPoint)();
    STDMETHOD(OnThreadRequestComplete)();
    STDMETHOD(OnBeginThreadRequest)();

    void Initialize(_In_ IDebugApplication110* pDebugApplication, _In_ IDebugThreadCall* pCall, _In_ HWND messageWnd, bool notifyOnBreak);
    void Deinitialize();
    void NotifyBreakOccurred();
    void Push(_In_ shared_ptr<MessagePacket> spMessage);
    void PopAll(_Inout_ vector<shared_ptr<MessagePacket>>& messages);
    BOOL PostProcessPacketsMessage(bool postIfAny = false);
    void Remove(_In_ const shared_ptr<MessagePacket>& spMessage);
    HRESULT TriggerThreadCall();

private:
    class ATL_NO_VTABLE PassthroughDebugThreadCall :
        public CComObjectRootEx<CComMultiThreadModel>,
        public IDebugThreadCall
    {
    public:
        BEGIN_COM_MAP(PassthroughDebugThreadCall)
            COM_INTERFACE_ENTRY(IDebugThreadCall)
        END_COM_MAP()

        void Initialize(_In_ IDebugApplicationThread110* pTarget, _In_ IDebugThreadCall* pReceiver, DWORD_PTR messageID)
        {
            m_spDebugApplicationThreadTarget = pTarget;
            m_spReceiver = pReceiver;
            m_messageID = messageID;
        }

        STDMETHOD(ThreadCallHandler)(
            /* [in] */ DWORD_PTR /*dwParam1*/,
            /* [in] */ DWORD_PTR /*dwParam2*/,
            /* [in] */ DWORD_PTR /*dwParam3*/) override
        {
            // Now we're running on the main browser UI thread through the PDM's thread call mechanisms.  
            // Before actually forwarding any messages (which might end up executing JavaScript), we need to check that it's safe to do so. 
            // We must ensure that:
            // 1. No other messages are already executing and that the thread
            // 2. The thread is actually still suspended for a breakpoint and not at some other stage that triggered a thread switch allowing us to run.
            // In either case, we'll get a chance to requeue again either during the SuspendForBreakPoint event, the resumefrombreakpoint event (where we postmessage),
            // or in the ThreadRequestComplete event.

            ATLENSURE_RETURN_HR(m_spReceiver.p != NULL, E_NOT_VALID_STATE);
            ATLENSURE_RETURN_HR(m_spDebugApplicationThreadTarget.p != NULL, E_NOT_VALID_STATE);

            BOOL suspendedForBreakPoint;
            HRESULT hr = m_spDebugApplicationThreadTarget->IsSuspendedForBreakPoint(&suspendedForBreakPoint);
            ATLENSURE_RETURN_HR(hr == S_OK, hr);

            if (suspendedForBreakPoint)
            {
                UINT uiCount;
                hr = m_spDebugApplicationThreadTarget->GetActiveThreadRequestCount(&uiCount);
                ATLENSURE_RETURN_HR(hr == S_OK, hr);

                ATLASSERT(uiCount >= 1);

                // If uiCount is 1, only we are running.  
                // If it's greater than 1, we are running but so is something else. 
                // We will get picked up again when we handle the threadrequestcomplete event and execute then.
                if (uiCount <= 1)
                {
                    return m_spReceiver->ThreadCallHandler(m_messageID, NULL, NULL);
                }
            }

            return S_FALSE;
        }

    private:
        CComPtr<IDebugApplicationThread110> m_spDebugApplicationThreadTarget;
        CComPtr<IDebugThreadCall> m_spReceiver;
        DWORD_PTR m_messageID;
    };

    HRESULT AsyncCallOnMainThread(DWORD_PTR messageID);
    void TryMainThreadAdvise();
    void ProcessMessagesWithDebugger();

private:
    CComAutoCriticalSection m_csCOMObjects;
    CComAutoCriticalSection m_csMessageArray;
    vector<shared_ptr<MessagePacket>> m_messages;
    CComPtr<IDebugApplication110> m_spDebugApplication;
    CComPtr<IDebugThreadCall> m_spCall;

    bool m_isValid;
    bool m_isAppAdvised;
    bool m_isThreadAdvised;
    bool m_notifyOnBreak;
    volatile bool m_safeToEvalScriptDuringDebugThreadCall;

    DWORD m_appCookie;
    DWORD m_threadCookie;
    DWORD m_mainThreadId;
    HWND m_messageWindow;
};

