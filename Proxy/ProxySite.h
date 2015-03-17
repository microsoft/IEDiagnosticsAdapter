//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include "Proxy_h.h"
#include "../DebuggerCore/DebugThreadWindowMessages.h"
#include "BrowserHost.h"
#include "BrowserMessageQueue.h"

struct ThreadParams
{
    HANDLE m_hEvent;
    shared_ptr<HRESULT> m_spResult;
    shared_ptr<HWND> m_spHwnd;
    HWND m_mainHwnd;
    CComPtr<IUnknown> m_spUnknown;
    CComObjPtr<BrowserMessageQueue> m_spMessageQueue;

    ThreadParams(
        _In_ HANDLE hEvent,
        _In_ shared_ptr<HRESULT> spResult,
        _In_ shared_ptr<HWND> spHwnd,
        _In_ HWND mainHwnd,
        _In_opt_ IUnknown* pUnknown,
        _In_opt_ BrowserMessageQueue* pMessageQueue)
        :
        m_hEvent(hEvent),
        m_spResult(spResult),
        m_spHwnd(spHwnd),
        m_mainHwnd(mainHwnd),
        m_spUnknown(pUnknown),
        m_spMessageQueue(pMessageQueue)
    { }
};

DWORD WINAPI WebSocketThreadProc(_In_ LPVOID lpParameter);
DWORD WINAPI DebuggerThreadProc(_In_ LPVOID lpParameter);

struct ThreadInfo
{
    HWND m_hwnd;
    DWORD m_id;
    CHandle m_handle;

    ThreadInfo() :
        m_hwnd(0),
        m_id(0),
        m_handle(NULL)
    { }
};

class ProxySite :
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<ProxySite, &CLSID_ProxySite>,
    public IObjectWithSiteImpl<ProxySite>,
    public CWindowImpl<ProxySite>,
    public IOleWindow,
    public IDebugThreadCall,
    public IProxySite
{
public:
    DECLARE_NO_REGISTRY()
    DECLARE_NOT_AGGREGATABLE(ProxySite)

    BEGIN_COM_MAP(ProxySite)
        COM_INTERFACE_ENTRY(IObjectWithSite)
        COM_INTERFACE_ENTRY(IOleWindow)
        COM_INTERFACE_ENTRY(IDebugThreadCall)
        COM_INTERFACE_ENTRY(IProxySite)
    END_COM_MAP()

    BEGIN_MSG_MAP(ProxySite)
        MESSAGE_HANDLER(WM_MESSAGE_IN_QUEUE, OnMessageInQueue)
        MESSAGE_HANDLER(WM_CREATE_ENGINE, OnCreateEngine)
        MESSAGE_RANGE_HANDLER(WM_CONTROLLERCOMMAND_FIRST, WM_CONTROLLERCOMMAND_LAST, OnControllerCommand)
    END_MSG_MAP()

    ProxySite();
    ~ProxySite();

    // IObjectWithSite
    STDMETHOD(SetSite)(_In_opt_ IUnknown* pUnkSite);

    // IOleWindow
    STDMETHOD(GetWindow)(__RPC__deref_out_opt HWND* pHwnd);
    STDMETHOD(ContextSensitiveHelp)(BOOL fEnterMode) { return E_NOTIMPL; }

    // IDebugThreadCall
    STDMETHOD(ThreadCallHandler)(_In_ DWORD_PTR dwParam1, _In_ DWORD_PTR dwParam2, _In_ DWORD_PTR dwParam3);

    // Window Messages
    LRESULT OnMessageInQueue(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);
    LRESULT OnCreateEngine(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);
    LRESULT OnControllerCommand(UINT nMsg, WPARAM wParam, LPARAM lParam, _Inout_ BOOL& /*bHandled*/);

private:
    HRESULT CreateThread(_In_ HWND mainHwnd, _In_opt_ IUnknown* pUnknown, _In_opt_ BrowserMessageQueue* pMessageQueue, _In_ LPTHREAD_START_ROUTINE threadProc, _Out_ DWORD& threadId, _Out_ CHandle& threadHandle, _Out_ HWND& threadMessageHwnd);
    HRESULT EnableSourceRundown(_Out_ CComPtr<IRemoteDebugApplication>& spRemoteDebugApplication);
    HRESULT EnableDynamicDebugging(_Out_ CComPtr<IRemoteDebugApplication>& spRemoteDebugApplication);
    HRESULT LoadPDM(_In_ DWORD attachType, _Out_ CComPtr<IRemoteDebugApplication>& spRemoteDebugApplication);
    HRESULT IOleCommandTargetExec(_In_ DWORD cmdId, _Inout_ CComPtr<IDispatch>& spDispatch, _Inout_ CComVariant& spResult);
    HRESULT CreateEngine(_In_ CComBSTR& id);

private:
    ThreadInfo m_websocketThread;
    ThreadInfo m_debuggerThread;

    map<CComBSTR, CComPtr<BrowserHost>> m_browserEngines;
    CComObjPtr<BrowserMessageQueue> m_spMessageQueue;
};

OBJECT_ENTRY_AUTO(__uuidof(ProxySite), ProxySite)
