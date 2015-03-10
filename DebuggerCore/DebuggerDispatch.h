//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#include "DebuggerCore_h.h"
#include "EventHelper.h"
#include "DebugThreadWindowMessages.h"
#include "PDMEventMessageQueue.h"
#include "SourceController.h"
#include "ThreadController.h"
#include "ComTypeInfoHolderLib.h"

using namespace ATL;
using namespace std;

static const int EVAL_RADIX = 10;


class ATL_NO_VTABLE CDebuggerDispatch :
    public CComObjectRootEx<CComSingleThreadModel>,
    public CComCoClass<CDebuggerDispatch, &CLSID_F12DebuggerDispatch>,
    public IDispatchImplLib<IF12DebuggerDispatch, &IID_IF12DebuggerDispatch>
{
public:
    DECLARE_NOT_AGGREGATABLE(CDebuggerDispatch)

    DECLARE_NO_REGISTRY()

    BEGIN_COM_MAP(CDebuggerDispatch)
        COM_INTERFACE_ENTRY(IF12DebuggerDispatch)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()

    CDebuggerDispatch();

    // IF12DebuggerExtension
    STDMETHOD(Initialize)(_In_ HWND hwndDebugPipeHandler, _In_ IDispatchEx* pScriptDispatchEx, _In_ HANDLE hBreakNotificationComplete, _In_ BOOL isDocked, _In_ IUnknown* pDebugApplication);
    STDMETHOD(Deinitialize)();
    STDMETHOD(OnDebuggerNotification)(_In_ const UINT notification, _In_ const UINT_PTR wParam, _In_ const LONG_PTR lParam);

    // IScriptEventProvider methods
    STDMETHOD(addEventListener)(_In_ BSTR eventName, _In_ IDispatch* listener);
    STDMETHOD(removeEventListener)(_In_ BSTR eventName, _In_ IDispatch* listener);
    STDMETHOD(isEventListenerAttached)(_In_ BSTR eventName, _In_opt_ IDispatch* listener, _Out_ ULONG* pAttachedCount);
    STDMETHOD(removeAllEventListeners)();

    // IF12DebuggerDispatch
    STDMETHOD(enable)(_Out_ VARIANT_BOOL* pSuccess);
    STDMETHOD(disable)(_Out_ VARIANT_BOOL* pSuccess);
    STDMETHOD(isEnabled)(_Out_ VARIANT_BOOL* isEnabled);

    STDMETHOD(connect)(_In_ BOOL enable, _Out_ LONG* pResult);
    STDMETHOD(disconnect)(_Out_ VARIANT_BOOL* pSuccess);
    STDMETHOD(shutdown)(_Out_ VARIANT_BOOL* pSuccess);
    STDMETHOD(isConnected)(_Out_ VARIANT_BOOL* pIsConnected);

    STDMETHOD(causeBreak)(_In_ ULONG causeBreakAction, _In_ ULONG workerId, _Out_ VARIANT_BOOL* pSuccess);
    STDMETHOD(resume)(_In_ ULONG breakResumeAction, _Out_ VARIANT_BOOL* pSuccess);

    STDMETHOD(addCodeBreakpoint)(_In_ ULONG docId, _In_ ULONG start, _In_ BSTR condition, _In_ BOOL isTracepoint, _Out_ VARIANT* pvActualBp);
    STDMETHOD(addEventBreakpoint)(_In_ VARIANT* pvEventTypes, _In_ BOOL isEnabled, _In_ BSTR condition, _In_ BOOL isTracepoint, _Out_ VARIANT* pvActualBp);
    STDMETHOD(addPendingBreakpoint)(_In_ BSTR url, _In_ ULONG start, _In_ BSTR condition, _In_ BOOL isEnabled, _In_ BOOL isTracepoint, _Out_ ULONG* pBreakpointId);
    STDMETHOD(removeBreakpoint)(_In_ ULONG breakpointId, _Out_ VARIANT_BOOL* pSuccess);
    STDMETHOD(updateBreakpoint)(_In_ ULONG breakpointId, _In_ BSTR condition, _In_ BOOL isTracepoint, _Out_ VARIANT_BOOL* pSuccess);
    STDMETHOD(setBreakpointEnabledState)(_In_ ULONG breakpointId, _In_ BOOL enable, _Out_ VARIANT_BOOL* pSuccess);
    STDMETHOD(getBreakpointIdFromSourceLocation)(_In_ ULONG docId, _In_ ULONG start, _Out_ ULONG* pBreakpointId);

    STDMETHOD(getThreadDescription)(_Out_ BSTR* pThreadDescription);
    STDMETHOD(getThreads)(_Out_ VARIANT* pvThreadDescriptions);
    STDMETHOD(getFrames)(_In_ LONG framesNeeded, _Out_ VARIANT* pvFramesArray);
    STDMETHOD(getSourceText)(_In_ ULONG docId, _Out_ VARIANT* pvSourceTextResult);
    STDMETHOD(getLocals)(_In_ ULONG frameId, _Out_ ULONG* pPropertyId);
    STDMETHOD(eval)(_In_ ULONG frameId, _In_ BSTR evalString, _Out_ VARIANT* pvPropertyInfo);
    STDMETHOD(getChildProperties)(_In_ ULONG propertyId, _In_ ULONG start, _In_ ULONG length, _Out_ VARIANT* pvPropertyInfosObject);
    STDMETHOD(setPropertyValueAsString)(_In_ ULONG propertyId, _In_ BSTR* pValue, _Out_ VARIANT_BOOL* pSuccess);

    STDMETHOD(canSetNextStatement)(_In_ ULONG docId, _In_ ULONG position, _Out_ VARIANT_BOOL* pSuccess);
    STDMETHOD(setNextStatement)(_In_ ULONG docId, _In_ ULONG position, _Out_ VARIANT_BOOL* pSuccess);
    STDMETHOD(setBreakOnFirstChanceExceptions)(_In_ BOOL fValue, _Out_ VARIANT_BOOL* pSuccess);

private:
    HRESULT SetRemoteDebugApplication(_In_ IUnknown* pDebugApplication);
    HRESULT DockedStateChanged(_In_ BOOL isDocked);
    HRESULT DebuggingDisabled();
    HRESULT FireQueuedEvents();
    HRESULT OnEventBreakpoint(_In_ const bool isStarting, _In_ const CString& eventType);
    HRESULT OnEventListenerUpdated(_In_ bool const isListening);
    HRESULT FireDocumentEvent(_In_ const CString& eventName, _In_ vector<CComVariant>& objects);
    HRESULT GetPropertyObjectFromPropertyInfo(_In_ const shared_ptr<PropertyInfo>& spPropertyInfo, _Out_ CComVariant& propertyInfoObject);
    HRESULT CreateScriptBreakpointInfo(_In_ const BreakpointInfo& breakpoint, _Out_ CComVariant& breakpointObject);
    HRESULT CreateSetMutationBreakpointResult(_In_ bool success, _In_ ULONG breakpointId, _In_ CComBSTR& objectName, _Out_ CComVariant& breakpointObject);

private:
    DWORD m_dispatchThreadId;
    EventHelper m_eventHelper;

    bool m_hasHitBreak;

    HWND m_hwndDebugPipeHandler;
    CComPtr<IDispatchEx> m_spScriptDispatch;

    CComObjPtr<PDMEventMessageQueue> m_spMessageQueue;
    CComObjPtr<ThreadController> m_spThreadController;
    CComObjPtr<SourceController> m_spSourceController;
};

OBJECT_ENTRY_AUTO(__uuidof(F12DebuggerDispatch), CDebuggerDispatch)
