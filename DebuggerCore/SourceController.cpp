//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "SourceController.h"
#include "DebugThreadWindowMessages.h"

// Ids start at 1 so that '0' can be classed as invalid
ULONG SourceController::s_nextDocId = 1;
ULONG SourceController::s_nextBpId = 1;

SourceController::SourceController() :
    m_dispatchThreadId(::GetCurrentThreadId()), // We are always created on the dispatch thread
    m_hwndDebugPipeHandler(nullptr),
    m_isBreakingOnNewWorker(false),
    m_shouldBindBreakpoints(true),
    m_isEnabled(false),
    m_isEventListenerRegistered(false)
{
}

HRESULT SourceController::Initialize(_In_ PDMEventMessageQueue* pMessageQueue, _In_ const HWND hwndDebugPipeHandler)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pMessageQueue != nullptr, E_INVALIDARG);

    m_spMessageQueue = pMessageQueue;
    m_hwndDebugPipeHandler = hwndDebugPipeHandler;

    return S_OK;
}

HRESULT SourceController::SetRemoteDebugApplication(_In_ IRemoteDebugApplication* pDebugApplication)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pDebugApplication != nullptr, E_INVALIDARG);

    m_spDebugApplication = pDebugApplication;
    return S_OK;
}

HRESULT SourceController::SetEnabledState(_In_ bool isEnabled)
{
    m_isEnabled = isEnabled;
    if (isEnabled)
    {
        RebindAllCodeBreakpoints();
        RebindAllEventBreakpoints(/*isEventListenerRegistered=*/ false);
    }
    else
    {
        // No need to remove the breakpoints from chakra since they have been discarded due to disabled debugging
        UnbindAllCodeBreakpoints(/*removeFromEngine=*/ false);

        // We do need to remove any event breakpoint listeners from the BHO since they would otherwise continue to hit
        UnbindAllEventBreakpoints(/*removeListeners=*/ true);
    }

    return S_OK;
}

HRESULT SourceController::Clear()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    // Document changes
    m_removedDocuments.clear();
    m_updatedDocumentInfoMap.clear();
    m_resolvedBreakpointsMap.clear();
    m_currentDocumentInfoMap.clear();

    // Source nodes
    m_sourceNodeMap.clear();
    m_documentInfoMap.clear();
    m_sourceNodeAtApplicationMap.clear();

    // Breakpoints
    m_breakpointsMap.clear();
    m_breakpointsAtDocIdMap.clear();
    m_breakpointsAtUrlMap.clear();

    return S_OK;
}

// PDM Operations
HRESULT SourceController::EnumerateSources() 
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spDebugApplication.p != nullptr, E_NOT_VALID_STATE);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    CComPtr<IDebugApplicationNode> pRootNode;
    if (m_spDebugApplication->GetRootNode(&pRootNode) == S_OK)
    {
        // Enumerate all the children of this root node. 
        // AddAllSourceNodes will be called recursively to build up the current tree of nodes that the PDM knows about. 
        HRESULT hr = this->AddAllSourceNodes(pRootNode, SourceController::CreateUniqueDocId());
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Fire an event for the initialization of sources
        m_spMessageQueue->Push(PDMEventType::SourceUpdated);    
    }

    return S_OK;
}

HRESULT SourceController::GetSourceLocationForCallFrame(_In_ IDebugCodeContext* pDebugCodeContext, _Out_ shared_ptr<SourceLocationInfo>& spSourceLocationInfo, _Out_ bool& isInternal)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spDebugApplication.p != nullptr, E_NOT_VALID_STATE);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    isInternal = false;
    spSourceLocationInfo.reset(new SourceLocationInfo());

    // Check to see if it's an internal frame by checking for S_OK and null context
    CComPtr<IDebugDocumentContext> spDebugDocumentContext;
    HRESULT hr = pDebugCodeContext->GetDocumentContext(&spDebugDocumentContext);
    if (hr == S_OK && spDebugDocumentContext == nullptr)
    {
        isInternal = true;
        return S_OK;
    }

    return this->GetSourceLocation(pDebugCodeContext, spSourceLocationInfo);
}

HRESULT SourceController::GetBreakpointInfoForBreak(_In_ ULONG docId, _In_ ULONG start, _Out_ shared_ptr<BreakpointInfo>& spBreakpointInfo)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spDebugApplication.p != nullptr, E_NOT_VALID_STATE);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    ULONG breakpointId;
    HRESULT hr = this->GetBreakpointIdFromSourceLocation(docId, start, breakpointId);
    if (hr == S_OK)
    {
        auto it = m_breakpointsMap.find(breakpointId);
        if (it != m_breakpointsMap.end())
        {
            spBreakpointInfo = it->second;
        }
        else
        {
            hr = E_NOT_FOUND;
        }
    }
    else if (hr >= 0)
    {
        // Since SAL annotates the HRESULT return value to be success on > 0, we need to ensure we
        // actually return a failure value in cases where our hr is S_FALSE or similar.
        hr = E_FAIL;
    }

    return hr;
}

HRESULT SourceController::GetBreakpointInfosForEventBreak(_In_ const CString& eventType, _Out_ vector<shared_ptr<BreakpointInfo>>& breakpointList)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spDebugApplication.p != nullptr, E_NOT_VALID_STATE);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    HRESULT hr = E_NOT_FOUND;

    CComBSTR type(eventType);
    auto itBreakpointsAtEvent = m_breakpointsAtEventTypeMap.find(type);
    if (itBreakpointsAtEvent != m_breakpointsAtEventTypeMap.end())
    {
        auto& list = itBreakpointsAtEvent->second;
        for (const auto& id : list)
        {
            auto itBreakpoint = m_breakpointsMap.find(id);
            if (itBreakpoint != m_breakpointsMap.end())
            {
                breakpointList.push_back(itBreakpoint->second);
                hr = S_OK;
            }
        }
    }

    return hr;
}

// Dispatch Operations
HRESULT SourceController::RefreshSources(_Inout_ vector<shared_ptr<DocumentInfo>>& added, _Inout_ vector<shared_ptr<DocumentInfo>>& updated, _Inout_ vector<ULONG>& removed, _Inout_ vector<shared_ptr<ReboundBreakpointInfo>>& rebound)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spDebugApplication.p != nullptr, E_NOT_VALID_STATE);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    // Search the removed documents
    if (!m_removedDocuments.empty())
    {
        removed = m_removedDocuments;

        // Sort them into reverse order so that children will be removed before parents
        std::sort(removed.begin(), removed.end(), [](const ULONG id1, const ULONG id2)
        {
            return id1 > id2;
        });

        // Remove them from the current list
        for (const auto& docId : removed)
        {
            m_currentDocumentInfoMap.erase(docId);
        }

        m_removedDocuments.clear();
    }

    // Search the updated documents for ones that are new or updated, and then add them to the current map
    for (const auto& dirtyDoc : m_updatedDocumentInfoMap)
    {
        ULONG docId = dirtyDoc.first;

        // Get the actual docInfo
        auto docInfoTextPair = m_documentInfoMap.find(docId);
        if (docInfoTextPair != m_documentInfoMap.end()) 
        {
            const shared_ptr<DocumentInfo>& docInfo = docInfoTextPair->second.first;

            auto itDocument = m_currentDocumentInfoMap.find(docId);
            if (itDocument == m_currentDocumentInfoMap.end())
            {
                // Newly added document
                m_currentDocumentInfoMap[docId] = true;
                added.push_back(docInfo);
            }
            else
            {
                // Updated document
                updated.push_back(docInfo);
            }
        }
    }

    // Update the resolved breakpoints
    for (auto& resolvedPair : m_resolvedBreakpointsMap)
    {
        for (auto& bpId : resolvedPair.second)
        {
            auto bp = m_breakpointsMap.find(bpId);
            if (bp != m_breakpointsMap.end())
            {
                // Create the information
                shared_ptr<ReboundBreakpointInfo> spReboundInfo(new ReboundBreakpointInfo());
                spReboundInfo->breakpointId = bpId;
                spReboundInfo->newDocId = resolvedPair.first;
                spReboundInfo->isBound = bp->second->isBound;

                if (bp->second->spSourceLocation != nullptr)
                {
                    spReboundInfo->start = bp->second->spSourceLocation->charPosition;
                    spReboundInfo->length = bp->second->spSourceLocation->contextCount;
                }
                else
                {
                    // Event breakpoints don't have a source location but must have a type
                    ATLENSURE_RETURN_HR(bp->second->spEventTypes != nullptr, E_UNEXPECTED);
                    spReboundInfo->start = 0;
                    spReboundInfo->length = 0;
                }

                rebound.push_back(spReboundInfo);
            }
        }
    }

    m_resolvedBreakpointsMap.clear();
    m_updatedDocumentInfoMap.clear();

    return S_OK;
}

HRESULT SourceController::GetSourceText(_In_ ULONG docId, _Out_ CComBSTR& sourceText)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spDebugApplication.p != nullptr, E_NOT_VALID_STATE);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    HRESULT hr = S_OK;

    auto it = m_documentInfoMap.find(docId);
    if (it != m_documentInfoMap.end())
    {
        CComPtr<IDebugDocumentText> spDebugDocumentText(it->second.second);

        if (spDebugDocumentText == nullptr)
        {
            // Document without text, e.g. root node
            sourceText = L"";
        }
        else
        {
            // Get the source text size
            ULONG cLines = 0;
            ULONG cChars = 0;
            hr = spDebugDocumentText->GetSize(&cLines, &cChars);
            BPT_FAIL_IF_NOT_S_OK(hr);

            if (cChars == 0)
            {
                // No text
                sourceText = L"";
                return S_OK;
            }

            // Allocate buffer for source text and attributes
            ULONG size = cChars * sizeof(WCHAR);
            std::shared_ptr<void> spSrcText(::CoTaskMemAlloc(size), ::CoTaskMemFree);
            ATLENSURE_RETURN_HR(spSrcText.get() != nullptr, E_OUTOFMEMORY);

            // Get the source text and attributes
            ULONG cchSrcText = 0;
            hr = spDebugDocumentText->GetText(0, (WCHAR*)spSrcText.get(), nullptr, &cchSrcText, cChars);

            if (cchSrcText != cChars)
            {
                // The page was probably loading between GetSize(...) and GetText(...) calls.
                return E_NOT_VALID_STATE;
            }

            sourceText.Attach(::SysAllocStringLen((const WCHAR*)spSrcText.get(), cChars));
        }
    }
    else
    {
        hr = E_NOT_FOUND;
    }

    return hr;
}

HRESULT SourceController::GetCodeContext(_In_ ULONG docId, _In_ ULONG start, _Out_ CComPtr<IDebugCodeContext>& spDebugCodeContext)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spDebugApplication.p != nullptr, E_NOT_VALID_STATE);

    return this->GetCodeContextFromDocId(docId, start, spDebugCodeContext);
}

HRESULT SourceController::AddCodeBreakpoint(_In_ ULONG docId, _In_ ULONG start, _In_ const CComBSTR& condition, _In_ bool isTracepoint, _Out_ shared_ptr<BreakpointInfo>& spBreakpointInfo)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spDebugApplication.p != nullptr, E_NOT_VALID_STATE);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    HRESULT hr = S_OK;

    // Fill in the new breakpoint
    spBreakpointInfo.reset(new BreakpointInfo());
    spBreakpointInfo->id = 0;
    spBreakpointInfo->url = L"";
    spBreakpointInfo->condition = condition;
    spBreakpointInfo->isTracepoint = isTracepoint; 
    spBreakpointInfo->isBound = false;   
    spBreakpointInfo->isEnabled = false;

    // Try to add to this location
    CComPtr<IDebugCodeContext> spDebugCodeContext;
    HRESULT hrContext = this->GetCodeContext(docId, start, spDebugCodeContext);
    if (hrContext == S_OK || hrContext == E_PENDING)
    {
        shared_ptr<SourceLocationInfo> spSourceLocationInfo;
        if (hrContext == S_OK)
        {
            // Valid code context, so try for a source location
            HRESULT hrSourceLoc = this->GetSourceLocation(spDebugCodeContext, spSourceLocationInfo);
            if (hrSourceLoc != S_OK)
            {
                // Failed to get the source location, so we cannot add a breakpoint here
                // Propagate that error to the caller
                return hrSourceLoc;
            }
        }
        else
        {
            // E_PENDING requires a refresh before we can bind the breakpoint
            // so generate a temp source location
            spSourceLocationInfo.reset(new SourceLocationInfo());
            spSourceLocationInfo->docId = docId;
            spSourceLocationInfo->contextCount = 0;
            spSourceLocationInfo->charPosition = start;
            spSourceLocationInfo->lineNumber = start;
            spSourceLocationInfo->columnNumber = start;
        }

        // Make sure there isn't an existing one already here
        shared_ptr<BreakpointInfo> spExistingBp;
        hr = this->FindExistingBreakpoint(spSourceLocationInfo, spExistingBp);
        if (hr == S_OK)
        {
            // Found an existing breakpoint
            spBreakpointInfo = spExistingBp;
        }
        else
        {
            // Create a new breakpoint

            // Get the info for the URL
            auto itMap = m_documentInfoMap.find(docId);
            ATLENSURE_RETURN_HR(itMap != m_documentInfoMap.end(), E_NOT_FOUND); // This shouldn't happen since we need to have the info in order to get the source location above

            shared_ptr<DocumentInfo> spDocumentInfo(itMap->second.first);

            // Success, so populate the final information
            spBreakpointInfo->url = spDocumentInfo->url;
            spBreakpointInfo->spSourceLocation = spSourceLocationInfo;
            spBreakpointInfo->isBound = (hrContext == S_OK); // E_PENDING from the code context means that we require a refresh before binding can occur

            hr = this->AddBreakpointInternal(spBreakpointInfo);
            BPT_FAIL_IF_NOT_S_OK(hr);
        }

        if (!m_shouldBindBreakpoints)
        {
            // If we should not bind breakpoints (because we are profiling), just assume a successful enable
            spBreakpointInfo->isBound = false;
            spBreakpointInfo->isEnabled = true;
        }
        else
        {
            // Enable the breakpoint at the code context if it isn't already and we are binding breakpoints (not profiling)
            if (!spBreakpointInfo->isEnabled)
            {
                // Enable new breakpoints, or E_PENDING ones
                hr = (hrContext == S_OK ? spDebugCodeContext->SetBreakPoint(BREAKPOINT_ENABLED) : S_OK);
                spBreakpointInfo->isEnabled = (hr == S_OK);
            }
        }

        // Return a success result, even if enable fails
        hr = S_OK; 
    }
    else
    {
        // Failed to add breakpoint
        // Return the failed HRESULT to the caller
        hr = hrContext;
    }

    return hr;
}

HRESULT SourceController::AddEventBreakpoint(_In_ shared_ptr<vector<CComBSTR>> eventTypes, _In_ bool isEnabled,  _In_ const CComBSTR& condition, _In_ bool isTracepoint, _Out_ shared_ptr<BreakpointInfo>& spBreakpointInfo)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spDebugApplication.p != nullptr, E_NOT_VALID_STATE);
    ATLENSURE_RETURN_HR(eventTypes != nullptr, E_INVALIDARG);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    // Fill in the new breakpoint
    spBreakpointInfo.reset(new BreakpointInfo());
    spBreakpointInfo->id = 0;
    spBreakpointInfo->spEventTypes = eventTypes;
    spBreakpointInfo->url = static_cast<LPCSTR>(nullptr);
    spBreakpointInfo->condition = condition;
    spBreakpointInfo->isTracepoint = isTracepoint; 
    spBreakpointInfo->isBound = m_isEventListenerRegistered;
    spBreakpointInfo->isEnabled = isEnabled;

    HRESULT hr = this->AddBreakpointInternal(spBreakpointInfo);
    BPT_FAIL_IF_NOT_S_OK(hr);

    if (m_isEnabled)
    {
        // Update the event type map to add the new set of events
        this->UpdateEventTypeMap(*eventTypes.get(), spBreakpointInfo->id, /*addEvents=*/ isEnabled);

        // Inform BHO, only if the breakpoint is enabled
        if (isEnabled)
        {
            // Now inform the BHO that we want to register for event breakpoints
            // Since we talk to the devtoolwindow thread it is guaranteed to not be blocked at a breakpoint.
            // The BHO will return the result asynchronously as a call to WM_EVENTBREAKPOINTREGISTERED
            // so we handle that later and rebind the event breakpoints
            BOOL succeeded = ::PostMessageW(m_hwndDebugPipeHandler, WM_REGISTEREVENTLISTENER, /*add=*/ TRUE, NULL);
            hr = (succeeded ? S_OK : AtlHresultFromLastError());
            BPT_FAIL_IF_NOT_S_OK(hr);
        }
    }

    return hr;
}

HRESULT SourceController::AddPendingBreakpoint(_In_ const CComBSTR& url, _In_ ULONG start, _In_ const CComBSTR& condition, _In_ bool isEnabled, _In_ bool isTracepoint, _Out_ ULONG& pBreakpointId)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(m_spDebugApplication.p != nullptr, E_NOT_VALID_STATE);
    ATLASSERT(url.Length() > 0);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    // Create a default source and update its start position for the pending breakpoint
    shared_ptr<SourceLocationInfo> spDefaultLocation(new SourceLocationInfo());
    spDefaultLocation->docId = 0;
    spDefaultLocation->contextCount = 0;
    spDefaultLocation->charPosition = start;
    spDefaultLocation->lineNumber = start;
    spDefaultLocation->columnNumber = start;

    // Fill in the new breakpoint
    shared_ptr<BreakpointInfo> spBreakpointInfo;
    spBreakpointInfo.reset(new BreakpointInfo());
    spBreakpointInfo->spSourceLocation = spDefaultLocation;
    spBreakpointInfo->url = url;
    spBreakpointInfo->condition = condition;
    spBreakpointInfo->isTracepoint = isTracepoint; 
    spBreakpointInfo->isBound = false;   
    spBreakpointInfo->isEnabled = isEnabled;

    // Add breakpoint to the maps, so that it is bound when a new document with that url is added
    HRESULT hr = this->AddBreakpointInternal(spBreakpointInfo);
    BPT_FAIL_IF_NOT_S_OK(hr);

    pBreakpointId = spBreakpointInfo->id;

    if (m_isEnabled)
    {
        hr = this->RebindAllCodeBreakpoints();
        BPT_FAIL_IF_NOT_S_OK(hr);
    }
    return hr;
}

HRESULT SourceController::RemoveBreakpoint(_In_ ULONG breakpointId)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    HRESULT hr = S_OK;

    auto it = m_breakpointsMap.find(breakpointId);
    if (it != m_breakpointsMap.end())
    {
        hr = this->SetBreakpointState(it->second, BREAKPOINT_DELETED);

        // Incase of breakpoints in html files, we download the content as we continue script execution. If we are broken in debugger, and we
        // try to delete a breakpoint in the location which is not yet downloaded, we get an invalid syntax error while parsing the file. Also we 
        // get a similar error when trying to delete a breakpoint in the file not loaded in the debugger.
        // In that case, if the breakpoint is unbound, just delete it on our side so that we don't bother binding it.
        if (hr == S_OK || it->second->isBound == false)
        {
            // Remove it from our maps
            if (it->second->spSourceLocation != nullptr) // Only Code breakpoints have a source location
            {
                auto itBreakpointsAtDocId = m_breakpointsAtDocIdMap.find(it->second->spSourceLocation->docId);
                if (itBreakpointsAtDocId != m_breakpointsAtDocIdMap.end())
                {
                    auto& list = itBreakpointsAtDocId->second;
                    list.erase(std::remove_if(list.begin(), list.end(), [breakpointId](const ULONG& id) { 
                        return id == breakpointId; 
                    }),
                        list.end());

                    if (list.empty())
                    {
                        m_breakpointsAtDocIdMap.erase(itBreakpointsAtDocId);
                    }
                }

                auto itBreakpointsAtUrl = m_breakpointsAtUrlMap.find(it->second->url);
                if (itBreakpointsAtUrl != m_breakpointsAtUrlMap.end())
                {
                    auto& list = itBreakpointsAtUrl->second;
                    list.erase(std::remove_if(list.begin(), list.end(), [breakpointId](const ULONG& id) { 
                        return id == breakpointId; 
                    }),
                        list.end());

                    if (list.empty())
                    {
                        m_breakpointsAtUrlMap.erase(itBreakpointsAtUrl);
                    }
                }
            }

            m_breakpointsMap.erase(it);

            hr = S_OK;
        }
    }
    else
    {
        hr = E_NOT_FOUND;
    }

    return hr;
}

HRESULT SourceController::UpdateBreakpoint(_In_ ULONG breakpointId, _In_ const CComBSTR& condition, _In_ bool isTracepoint)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    HRESULT hr = S_OK;

    auto it = m_breakpointsMap.find(breakpointId);
    if (it != m_breakpointsMap.end())
    {
        shared_ptr<BreakpointInfo> spBreakpointInfo(it->second);

        spBreakpointInfo->condition = condition;
        spBreakpointInfo->isTracepoint = isTracepoint;
    }
    else
    {
        hr = E_NOT_FOUND;
    }

    return hr;
}

HRESULT SourceController::SetBreakpointEnabledState(_In_ ULONG breakpointId, _In_ bool enable)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    HRESULT hr = S_OK;

    auto it = m_breakpointsMap.find(breakpointId);
    if (it != m_breakpointsMap.end())
    {
        hr = this->SetBreakpointState(it->second, (enable ? BREAKPOINT_ENABLED : BREAKPOINT_DISABLED));
    }
    else
    {
        hr = E_NOT_FOUND;
    }

    return hr;
}

HRESULT SourceController::GetBreakpointId(_In_ ULONG docId, _In_ ULONG start, _Out_ ULONG& breakpointId)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    return this->GetBreakpointIdFromSourceLocation(docId, start, breakpointId);
}

HRESULT SourceController::RebindAllCodeBreakpoints()
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    m_shouldBindBreakpoints = true;

    for (const auto& map : m_documentInfoMap)
    {
        this->RebindBreakpoints(map.second.first);
    }

    // Fire an event for the rebind
    m_spMessageQueue->Push(PDMEventType::SourceUpdated);  

    return S_OK;
}

HRESULT SourceController::UnbindAllCodeBreakpoints(_In_ const bool removeFromEngine)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    m_shouldBindBreakpoints = false;

    for (const auto& map : m_documentInfoMap)
    {
        this->UnbindBreakpoints(map.first, removeFromEngine);
    }

    // Fire an event for the unbind
    m_spMessageQueue->Push(PDMEventType::SourceUpdated);  

    return S_OK;
}

HRESULT SourceController::RebindAllEventBreakpoints(_In_ const bool updateEventListener)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    if (updateEventListener)
    {
        // If we are told to update the event listener, this must be because it was just added
        m_isEventListenerRegistered = true;
    }

    bool updated = false;
    for (const auto& breakpointPair : m_breakpointsMap)
    {
        const auto& bpInfo = breakpointPair.second;
        if (bpInfo->spEventTypes != nullptr)
        {
            if (!bpInfo->isBound)
            {
                if (updateEventListener)
                {
                    bpInfo->isBound = true;
                    m_resolvedBreakpointsMap[0].push_back(breakpointPair.first);
                    updated = true;

                    if (bpInfo->isEnabled)
                    {
                        this->UpdateEventTypeMap(*bpInfo->spEventTypes, bpInfo->id, /*addEvents=*/ true);
                    }
                }
                else
                {
                    // Tell the BHO to add the event listener now that we have found an unbound event bp
                    BOOL succeeded = ::PostMessageW(m_hwndDebugPipeHandler, WM_REGISTEREVENTLISTENER, /*add=*/ TRUE, NULL);
                    HRESULT hr = (succeeded ? S_OK : AtlHresultFromLastError());
                    BPT_FAIL_IF_NOT_S_OK(hr);
                    break;
                }
            }
        }
    }

    if (updated)
    {
        // Fire an event for the rebind
        m_spMessageQueue->Push(PDMEventType::SourceUpdated);
    }

    return S_OK;
}

HRESULT SourceController::UnbindAllEventBreakpoints(_In_ const bool removeListeners)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    m_isEventListenerRegistered = false;

    vector<CComBSTR> eventTypes;

    bool updated = false;
    for (const auto& breakpointPair : m_breakpointsMap)
    {
        const auto& bpInfo = breakpointPair.second;
        if (bpInfo->spEventTypes != nullptr)
        {
            if (bpInfo->isBound)
            {
                bpInfo->isBound = false;
                m_resolvedBreakpointsMap[0].push_back(breakpointPair.first);
                updated = true;
            }
        }
    }

    HRESULT hr = S_OK;
    if (removeListeners)
    {
        // Get rid of all the stored event types since we are removing the listener in the BHO
        m_breakpointsAtEventTypeMap.clear();

        // Tell the BHO to remove all the event types listeners
        BOOL succeeded = ::PostMessageW(m_hwndDebugPipeHandler, WM_REGISTEREVENTLISTENER, /*add=*/ FALSE, NULL);
        hr = (succeeded ? S_OK : AtlHresultFromLastError());
        BPT_FAIL_IF_NOT_S_OK(hr);
    }

    if (updated)
    {
        // Fire an event for the unbind
        m_spMessageQueue->Push(PDMEventType::SourceUpdated);
    }

    return hr;
}

HRESULT SourceController::GetIsMappedEventType(_In_ const CString& eventType, _Out_ bool& isMapped)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    isMapped = false;

    CComBSTR type(eventType);
    if (m_breakpointsAtEventTypeMap.find(type) != m_breakpointsAtEventTypeMap.end())
    {
        isMapped = true;
    }

    return S_OK;
}

HRESULT SourceController::SetBreakOnNewWorker(_In_ bool enable)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    m_isBreakingOnNewWorker = enable;

    return S_OK;
}

// PDM Event Notifications
HRESULT SourceController::OnAddChild(_In_ ULONG parentId, _In_ IDebugApplicationNode* pChildNode)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pChildNode != nullptr, E_INVALIDARG);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    // AddNode will rebind the breakpoints for this url
    ULONG newId = 0;
    HRESULT hr = this->AddNode(pChildNode, parentId, newId);
    BPT_FAIL_IF_NOT_S_OK(hr);

    // Should we break on new files incase its a webworker
    // If m_spDebugApplication is null, we are in source rundown mode which gives us events, but cannot cause an async break
    if (m_isBreakingOnNewWorker && m_spDebugApplication.p != nullptr)
    {
        // Release the sources lock since we no longer need it
        lock.Unlock();
        hr = m_spDebugApplication->CauseBreak();
    }

    // Fire an event for the addition
    m_spMessageQueue->Push(PDMEventType::SourceUpdated);  

    return hr;
}

HRESULT SourceController::OnRemoveChild(_In_ ULONG parentId, _In_ IDebugApplicationNode* pChildNode)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pChildNode != nullptr, E_INVALIDARG);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    // RemoveNode will unbind breakpoints and remove all children of this node
    HRESULT hr = this->RemoveNode(pChildNode, parentId);
    BPT_FAIL_IF_NOT_S_OK(hr);

    // Fire an event for the removal
    m_spMessageQueue->Push(PDMEventType::SourceUpdated);  

    return hr;
}

HRESULT SourceController::OnUpdate(_In_ ULONG docId)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    auto it = m_sourceNodeMap.find(docId);
    if (it != m_sourceNodeMap.end())
    {
        CComObjPtr<SourceNode> spNode(it->second);

        HRESULT hr = spNode->UpdateInfo();
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Update the information to our map of updated docs
        shared_ptr<DocumentInfo> spDocInfo;
        CComPtr<IDebugDocumentText> spDebugDocumentText;
        hr = spNode->GetDocumentInfo(spDocInfo, spDebugDocumentText);
        BPT_FAIL_IF_NOT_S_OK(hr);

        m_documentInfoMap[docId] = pair<shared_ptr<DocumentInfo>, CComPtr<IDebugDocumentText>>(spDocInfo, spDebugDocumentText);
        m_updatedDocumentInfoMap[docId] = true;

        // Since the text was just updated, we need to rebind breakpoints for this url in case more text
        // was added that has an unbound breakpoint waiting on it.
        this->RebindBreakpoints(spDocInfo);

        // Fire an event for the update
        m_spMessageQueue->Push(PDMEventType::SourceUpdated);  
    }

    return S_OK;
}

// Helper functions
HRESULT SourceController::AddAllSourceNodes(_In_ IDebugApplicationNode* pRootNode, _In_ const ULONG& parentId)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pRootNode != nullptr, E_INVALIDARG);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    CComPtr<IDebugApplicationNode> spRootNode(pRootNode);

    // Actually create the new node
    ULONG newId = 0;
    HRESULT hr = this->AddNode(pRootNode, parentId, newId);
    BPT_FAIL_IF_NOT_S_OK(hr);

    // Add child nodes recursively
    CComPtr<IEnumDebugApplicationNodes> spEnumDebugApp;
    hr = spRootNode->EnumChildren(&spEnumDebugApp);
    if (hr == S_OK)
    {
        hr = spEnumDebugApp->Reset();
        BPT_FAIL_IF_NOT_S_OK(hr);

        IDebugApplicationNode* pChildNode;
        ULONG nFetched;
        while (spEnumDebugApp->Next(1, &pChildNode, &nFetched) == S_OK && (nFetched == 1))
        {
            this->AddAllSourceNodes(pChildNode, newId);
            pChildNode->Release();
        }
    }

    return hr;
}

HRESULT SourceController::AddNode(_In_ IDebugApplicationNode* pRootNode, _In_ const ULONG& parentId, _Out_ ULONG& newId)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pRootNode != nullptr, E_INVALIDARG);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    HRESULT hr = S_OK;

    // See if we have a parent node
    ULONG parentDocId = INVALID_ASDFILEHANDLE;
    CComObjPtr<SourceNode> spParentNode;
    auto it = m_sourceNodeMap.find(parentId);
    if (it != m_sourceNodeMap.end())
    {
        spParentNode = it->second;
        parentDocId = spParentNode->GetDocId();
    }

    // Get a new identifier for this source node
    newId = SourceController::CreateUniqueDocId();

    // Create object that represents the source file
    CComObject<SourceNode>* pNode;
    hr = CComObject<SourceNode>::CreateInstance(&pNode);
    BPT_FAIL_IF_NOT_S_OK(hr);

    hr = pNode->Initialize(m_dispatchThreadId, this, newId, pRootNode, parentDocId);
    BPT_FAIL_IF_NOT_S_OK(hr);

    // Add this child to the parent's list
    if (spParentNode.p != nullptr)
    {
        spParentNode->AddChild(newId);
    }

    // Add to our maps
    m_sourceNodeMap[newId] = pNode;
    m_sourceNodeAtApplicationMap[pRootNode] = newId;

    // Get the information about this source node
    shared_ptr<DocumentInfo> spDocInfo;
    CComPtr<IDebugDocumentText> spDebugDocumentText;
    hr = pNode->GetDocumentInfo(spDocInfo, spDebugDocumentText);
    BPT_FAIL_IF_NOT_S_OK(hr);

    // Store that info in our maps
    m_documentInfoMap[newId] = pair<shared_ptr<DocumentInfo>, CComPtr<IDebugDocumentText>>(spDocInfo, spDebugDocumentText);
    m_updatedDocumentInfoMap[newId] = true;

    // Rebind breakpoints for this url
    this->RebindBreakpoints(spDocInfo);

    return hr;
}

HRESULT SourceController::RemoveNode(_In_ IDebugApplicationNode* pRootNode, _In_ const ULONG& /* parentId */)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pRootNode != nullptr, E_INVALIDARG);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    HRESULT hr = S_OK;

    auto it = m_sourceNodeAtApplicationMap.find(pRootNode);
    if (it != m_sourceNodeAtApplicationMap.end())
    {
        ULONG docId = it->second;

        // Get the actual node
        auto itSourceNode = m_sourceNodeMap.find(docId);
        if (itSourceNode != m_sourceNodeMap.end())
        {
            // Remove all the children recursively
            map<ULONG, bool> childrenMap;
            itSourceNode->second->GetChildren(childrenMap);

            for (const auto& childIdPair : childrenMap)
            {
                ULONG childId = childIdPair.first;
                auto itChildNode = m_sourceNodeMap.find(childId);
                if (itChildNode != m_sourceNodeMap.end())
                {
                    CComPtr<IDebugApplicationNode> spDebugApplicationNode;
                    hr = itChildNode->second->GetDebugApplicationNode(spDebugApplicationNode);
                    BPT_FAIL_IF_NOT_S_OK(hr);

                    this->RemoveNode(spDebugApplicationNode, docId);
                }
            }
        }

        auto current = m_currentDocumentInfoMap.find(docId);
        if (current != m_currentDocumentInfoMap.end())
        {
            // Remember that this document has been removed since the last refresh
            m_removedDocuments.push_back(docId);
        }

        // Remove anything that was updated prior to this removal
        m_updatedDocumentInfoMap.erase(docId);
        m_resolvedBreakpointsMap.erase(docId);

        // Remove the actual stored node
        m_sourceNodeMap.erase(docId);
        m_documentInfoMap.erase(docId);

        m_sourceNodeAtApplicationMap.erase(it);

        // Unbind any breakpoints on this document, no need to remove them, since the document has gone already
        this->UnbindBreakpoints(docId, /*removeFromEngine=*/ false);
    }

    return hr;
}

HRESULT SourceController::GetSourceLocation(_In_ IDebugCodeContext* pDebugCodeContext, _Out_ shared_ptr<SourceLocationInfo>& spSourceLocationInfo)
{
    ATLENSURE_RETURN_HR(pDebugCodeContext != nullptr, E_INVALIDARG);

    // This can be called by either thread.

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    CComPtr<IDebugDocumentContext> spDebugDocumentContext;
    HRESULT hr = pDebugCodeContext->GetDocumentContext(&spDebugDocumentContext);
    // This can fail for cross-engine contexts (where other engine is not in debug mode)
    if (hr == S_OK)
    {
        CComPtr<IDebugDocument> spDebugDocument;
        hr = spDebugDocumentContext->GetDocument(&spDebugDocument);
        BPT_FAIL_IF_NOT_S_OK(hr);

        // Populate the location line and columns
        CComQIPtr<IDebugDocumentText> spDebugDocumentText = spDebugDocument;
        ATLENSURE_RETURN_HR(spDebugDocumentText != nullptr, E_NOINTERFACE);

        // Initializa a blank source location info
        spSourceLocationInfo.reset(new SourceLocationInfo());

        hr = spDebugDocumentText->GetPositionOfContext(spDebugDocumentContext, &(spSourceLocationInfo->charPosition), &(spSourceLocationInfo->contextCount));
        BPT_FAIL_IF_NOT_S_OK(hr);

        hr = spDebugDocumentText->GetLineOfPosition(spSourceLocationInfo->charPosition, &(spSourceLocationInfo->lineNumber), &(spSourceLocationInfo->columnNumber));
        if (hr == E_FAIL)
        {
            // This call can fail if the source text has changed since it was first parsed by the pdm.
            // This can happen when you step thorugh a document.write() which will cause the document text to re-generate during the step.
            // We handle this gracefully by setting the line and column number to 1. The callstack window will still display the correct position
            // in the front end as the charPosition value is still accurate.
            spSourceLocationInfo->lineNumber = 1;
            spSourceLocationInfo->columnNumber = 1;
            hr = S_OK;
        }    

        // Populate the docId by finding the node in our map
        CComQIPtr<IDebugDocumentHelper> spDocumentHelper(spDebugDocument);
        ATLENSURE_RETURN_HR(spDocumentHelper.p != nullptr, E_NOINTERFACE);

        CComPtr<IDebugApplicationNode> spDebugApplicationNode;
        hr = spDocumentHelper->GetDebugApplicationNode(&spDebugApplicationNode);
        BPT_FAIL_IF_NOT_S_OK(hr);

        auto sourceNodeIt = m_sourceNodeAtApplicationMap.find(spDebugApplicationNode);
        if (sourceNodeIt != m_sourceNodeAtApplicationMap.end())
        {
            spSourceLocationInfo->docId = sourceNodeIt->second;
        } 
        else
        {
            // Due to an issue with document.write(), the source file may have been removed already, so we default the id to the main html page
            auto docInfoIt = m_currentDocumentInfoMap.begin();
            if (m_currentDocumentInfoMap.size() > 1)
            {
                docInfoIt++;
            }
            spSourceLocationInfo->docId = docInfoIt->first;

            // Also re-write the line and column info, since this is obviously not going to point to the correct location anymore
            spSourceLocationInfo->lineNumber = 1;
            spSourceLocationInfo->columnNumber = 1;
            spSourceLocationInfo->contextCount = 1;
            spSourceLocationInfo->charPosition = 1;

            // Still return a bad HR, so the caller can decide if they want to use this fake data or not
            hr = E_NOT_FOUND;
        }
    }
    else if (hr >= 0)
    {
        // Since SAL annotates the HRESULT return value to be success on > 0, we need to ensure we
        // actually return a failure value in cases where our hr is S_FALSE or similar.
        hr = E_FAIL;
    }
    else if (hr == E_UNEXPECTED || hr == E_FAIL)
    {
        // This occurs when the script has already been freed by chakra, we return an error 
        // so that the caller can decide to either use the following fake data, or throw an assert.
        spSourceLocationInfo.reset(new SourceLocationInfo());
        spSourceLocationInfo->docId = m_currentDocumentInfoMap.begin()->first;

        hr = E_NOT_FOUND;
    }

    return hr;
}

HRESULT SourceController::GetCodeContextFromDocId(_In_ ULONG docId, _In_ ULONG start, _Out_ CComPtr<IDebugCodeContext>& spDebugCodeContext)
{
    // This can be called by either thread.

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    HRESULT hr = S_OK;

    auto it = m_documentInfoMap.find(docId);
    if (it != m_documentInfoMap.end())
    {
        CComPtr<IDebugDocumentText> spDebugDocumentText(it->second.second);

        CComPtr<IDebugDocumentContext> spDebugDocumentContext;
        hr = spDebugDocumentText->GetContextOfPosition(start, 1,  &spDebugDocumentContext);
        if (hr == S_OK)
        {
            CComPtr<IEnumDebugCodeContexts> spEnumDebugCodeContexts;
            hr = spDebugDocumentContext->EnumCodeContexts(&spEnumDebugCodeContexts);

            if (hr == S_OK)
            {
                // Context found
                ULONG ulFetched;
                hr = spEnumDebugCodeContexts->Next(1, &spDebugCodeContext, &ulFetched);
            }
            else if (hr == E_FAIL || (hr == E_UNEXPECTED && !m_isEnabled))
            {
                // Document found but code context not available.
                // This can happen in two cases.
                // 1. When the script is embedded in html such as <script type="text/javascript">/*<![CDATA[*/if(self!=top) ...
                // This code can be freed from chakra before we show it to the user, so breakpoints can't be set in there until the page is refreshed.
                // 2. Profiling is enabled and we are in source rundown mode. In this case pdm returns E_UNEXPECTED, so we also check that debugging 
                // is disabled.

                // Check to see if failure is caused by a freed script or profiling is enabled and we have a valid location
                CComPtr<IDebugDocument> spDebugDoc;
                hr = spDebugDocumentContext->GetDocument(&spDebugDoc);
                if (hr == S_OK)
                {
                    // Not a valid code context location
                    // Return invalid arg to match the PDM failure of a context not being found
                    hr = E_INVALIDARG;
                }
            }
        }
    }
    else
    {
        hr = E_NOT_FOUND;
    }

    if (hr != S_OK && hr >= 0)
    {
        // Since SAL annotates the HRESULT return value to be success on > 0, we need to ensure we
        // actually return a failure value in cases where our hr is S_FALSE or similar.
        hr = E_FAIL;
    }

    return hr;
}

HRESULT SourceController::GetBreakpointIdFromSourceLocation(_In_ ULONG docId, _In_ ULONG start, _Out_ ULONG& breakpointId)
{
    // This can be called by either thread.

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    breakpointId = 0;  // 0 is not a valid breakpointId

    // Get the context for the given document id
    CComPtr<IDebugCodeContext> spDebugCodeContext;
    HRESULT hr = this->GetCodeContextFromDocId(docId, start, spDebugCodeContext);
    if (hr == S_OK)
    {
        // Valid code context, so try for a source location
        shared_ptr<SourceLocationInfo> spSourceLocationInfo;
        HRESULT hrSourceLoc = this->GetSourceLocation(spDebugCodeContext, spSourceLocationInfo);
        if (hrSourceLoc != S_OK)
        {
            // Failed to get the source location, so we cannot find a breakpoint here
            // Propagate that error to the caller
            return hrSourceLoc;
        }

        // Find an existing breakpoint here
        shared_ptr<BreakpointInfo> spExistingBp;
        hr = this->FindExistingBreakpoint(spSourceLocationInfo, spExistingBp);
        if (hr == S_OK)
        {
            // Found an existing breakpoint
            breakpointId = spExistingBp->id;
        }
    }

    return hr;
}

HRESULT SourceController::FindExistingBreakpoint(_In_ const shared_ptr<SourceLocationInfo>& spSourceLocationInfo, _Out_ shared_ptr<BreakpointInfo>& spBreakpointInfo)
{
    // This can be called by either thread.

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    for (const auto& bp : m_breakpointsMap)
    {
        shared_ptr<BreakpointInfo> spBP(bp.second);

        if (spBP->spSourceLocation != nullptr &&
            spBP->spSourceLocation->docId == spSourceLocationInfo->docId && 
            spBP->spSourceLocation->charPosition == spSourceLocationInfo->charPosition &&
            spBP->spSourceLocation->contextCount == spSourceLocationInfo->contextCount)
        {
            spBreakpointInfo = spBP;
            return S_OK;
        }
    }

    return E_NOT_FOUND;
}

HRESULT SourceController::SetBreakpointState(_In_ shared_ptr<BreakpointInfo>& spBreakpointInfo, _In_ BREAKPOINT_STATE state)
{
    // This can be called by either thread.

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    HRESULT hr = S_OK;

    if ((!spBreakpointInfo->isEnabled && state == BREAKPOINT_ENABLED) ||
        (spBreakpointInfo->isEnabled && state == BREAKPOINT_DISABLED) ||
        (state == BREAKPOINT_DELETED))
    {
        // Only update the engine if we are enabled
        if (m_isEnabled)
        {
            if (spBreakpointInfo->spSourceLocation != nullptr)
            {
                // Check that the breakpoint is still in an active document
                // If the document has already been unloaded, then the engine has already discarded the breakpoint
                CComPtr<IDebugCodeContext> spDebugCodeContext;
                hr = this->GetCodeContextFromDocId(spBreakpointInfo->spSourceLocation->docId, spBreakpointInfo->spSourceLocation->charPosition, spDebugCodeContext);
                if (hr == S_OK)
                {
                    hr = spDebugCodeContext->SetBreakPoint(state);
                }
                else if (state != BREAKPOINT_DELETED)
                {
                    // If we can't find the source file, and we aren't trying to delete it, this must be a failure.
                    hr = E_FAIL;
                }
            } 
            else if (spBreakpointInfo->spEventTypes != nullptr) 
            {
                // Add or remove items to the event type map
                bool wasEmpty = m_breakpointsAtEventTypeMap.empty();
                bool isEnabling = (state == BREAKPOINT_ENABLED);
                hr = this->UpdateEventTypeMap(*spBreakpointInfo->spEventTypes, spBreakpointInfo->id, /*addEvents=*/ isEnabling);
                BPT_FAIL_IF_NOT_S_OK(hr);

                if ((wasEmpty && isEnabling) || (m_breakpointsAtEventTypeMap.empty() && !isEnabling))
                {
                    // Now inform the BHO that we want to register or unregister for event breakpoints
                    // Since we talk to the devtoolwindow thread it is guaranteed to not be blocked at a breakpoint.
                    // The BHO will return the result asynchronously as a call to WM_EVENTBREAKPOINTREGISTERED
                    // so we handle that later and update the event breakpoints
                    BOOL succeeded = ::PostMessageW(m_hwndDebugPipeHandler, WM_REGISTEREVENTLISTENER, /*add=*/ isEnabling, (LPARAM)spBreakpointInfo->spEventTypes.get());
                    hr = (succeeded ? S_OK : AtlHresultFromLastError());
                }
            }
        }

        // If the breakpoint is not deleted, mark its state locally. So that the next time it is bound, we update its state appropriately.
        if (state != BREAKPOINT_DELETED) 
        {
            spBreakpointInfo->isEnabled = (state == BREAKPOINT_ENABLED);

            // Because we have at least marked the breakpoint state locally
            hr = S_OK;
        }
    }

    return hr;
}

HRESULT SourceController::UnbindBreakpoints(_In_ ULONG docId, _In_ bool removeFromEngine)
{
    // This can be called by either thread.

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    // Get all the breakpoints at the given docId
    auto itBpsAtDocId = m_breakpointsAtDocIdMap.find(docId);
    if (itBpsAtDocId != m_breakpointsAtDocIdMap.end())
    {
        // Loop through each one and update its bound flag
        for (auto& bpId : itBpsAtDocId->second)
        {
            auto itBreakpoint = m_breakpointsMap.find(bpId);
            if (itBreakpoint != m_breakpointsMap.end())
            {
                shared_ptr<BreakpointInfo> spBreakpointInfo(itBreakpoint->second);
                if (spBreakpointInfo->spSourceLocation != nullptr)
                {
                    // Try to disable the breakpoint so that it won't actually get hit in the engine
                    // This could fail if the document has already been removed, but we ignore that failure
                    CComPtr<IDebugCodeContext> spDebugCodeContext;
                    HRESULT hr = this->GetCodeContextFromDocId(spBreakpointInfo->spSourceLocation->docId, spBreakpointInfo->spSourceLocation->charPosition, spDebugCodeContext);
                    if (hr == S_OK)
                    {
                        hr = spDebugCodeContext->SetBreakPoint(removeFromEngine ? BREAKPOINT_DELETED : BREAKPOINT_DISABLED);
                        ATLASSERT(hr == S_OK);
                    }
                }

                spBreakpointInfo->isBound = false;
            }
        }
    }

    return S_OK;
}

HRESULT SourceController::RebindBreakpoints(_In_ const shared_ptr<DocumentInfo>& spDocInfo)
{
    // This can be called by either thread.

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    if (!m_shouldBindBreakpoints)
    {
        // We are profiling, so no need to rebind breakpoints
        return S_OK;
    }

    // Get all the breakpoints at this url
    map<ULONG, bool> addedStartOffsets;
    auto itBpsAtUrl = m_breakpointsAtUrlMap.find(spDocInfo->url);
    if (itBpsAtUrl != m_breakpointsAtUrlMap.end())
    {
        for (auto& bpId : itBpsAtUrl->second)
        {
            // Now get the actual breakpoint info from the map
            auto itBreakpoint = m_breakpointsMap.find(bpId);
            if (itBreakpoint != m_breakpointsMap.end())
            {
                auto breakpoint = itBreakpoint->second;
                if (breakpoint->spSourceLocation != nullptr)
                {
                    ULONG start = breakpoint->spSourceLocation->charPosition;
                    if (addedStartOffsets.find(start) == addedStartOffsets.end())
                    {
                        // Check that the breakpoint is inside the (perhaps partially downloaded) file
                        if (!breakpoint->isBound && start <= spDocInfo->textLength)
                        {
                            CComPtr<IDebugCodeContext> spDebugCodeContext;
                            HRESULT hr = this->GetCodeContextFromDocId(spDocInfo->docId, start, spDebugCodeContext);
                            if (hr == S_OK)
                            {
                                shared_ptr<SourceLocationInfo> spSourceLocationInfo;
                                hr = this->GetSourceLocation(spDebugCodeContext, spSourceLocationInfo);
                                if (hr == S_OK)
                                {
                                    // Save the previous docId
                                    ULONG prevDocId = breakpoint->spSourceLocation->docId;

                                    // Bind the breakpoint and update the doc id
                                    hr = spDebugCodeContext->SetBreakPoint((breakpoint->isEnabled ? BREAKPOINT_ENABLED : BREAKPOINT_DISABLED));
                                    if (hr == S_OK)
                                    {
                                        // Update the breakpoint
                                        breakpoint->spSourceLocation = spSourceLocationInfo;
                                        breakpoint->isBound = true;

                                        // Update the maps
                                        this->ResolveBreakpoint(bpId, prevDocId, spDocInfo->docId);
                                        addedStartOffsets[start] = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Any failure will just result in an unbound breakpoint, so return S_OK
    return S_OK;
}

HRESULT SourceController::ResolveBreakpoint(_In_ ULONG bpId, _In_ ULONG prevDocId, _In_ ULONG newDocId)
{
    // This can be called by either thread.

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    // Find all the breakpoints at the given docId
    auto itBps = m_breakpointsAtDocIdMap.find(prevDocId);
    if (itBps != m_breakpointsAtDocIdMap.end())
    {
        // Now find the matching breakpoint in the vector for this docId
        auto pList = &itBps->second;
        for (size_t i = 0; i < pList->size(); i++)
        {
            if (pList->at(i) == bpId)
            {
                // Move to the new docId
                m_breakpointsAtDocIdMap[newDocId].push_back(bpId);
                m_resolvedBreakpointsMap[newDocId].push_back(bpId);

                // Delete from the previous docId
                pList->erase(pList->begin() + i);
                break;
            }
        }

        // Remove an empty vector
        if (itBps->second.empty())
        {
            m_breakpointsAtDocIdMap.erase(itBps);
        }
    }

    return S_OK;
}

HRESULT SourceController::AddBreakpointInternal(_In_ const shared_ptr<BreakpointInfo>& spBreakpointInfo)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(spBreakpointInfo != nullptr, E_INVALIDARG);
    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    spBreakpointInfo->id = SourceController::CreateUniqueBpId();
    m_breakpointsMap[spBreakpointInfo->id] = spBreakpointInfo;
    if (spBreakpointInfo->spSourceLocation != nullptr)
    {
        m_breakpointsAtDocIdMap[spBreakpointInfo->spSourceLocation->docId].push_back(spBreakpointInfo->id);
        m_breakpointsAtUrlMap[spBreakpointInfo->url].push_back(spBreakpointInfo->id);
    }

    return S_OK;
}

HRESULT SourceController::CreateUniqueBpIdForMutationBreakpoint(_Out_ ULONG& id)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);
    id = SourceController::CreateUniqueBpId();

    return S_OK;
}

HRESULT SourceController::UpdateEventTypeMap(_In_ const vector<CComBSTR>& eventTypes, _In_ const ULONG breakpointId, _In_ const bool addEvents)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnDispatchThread(m_dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(!eventTypes.empty(), E_INVALIDARG);

    // Lock the source access
    CComCritSecLock<CComAutoCriticalSection> lock(m_csSourcesLock);

    for (const auto& type : eventTypes)
    {
        if (addEvents)
        {
            // Add the breakpoint id to this event type
            m_breakpointsAtEventTypeMap[type].push_back(breakpointId);
        }
        else
        {
            auto itBreakpointsAtEvent = m_breakpointsAtEventTypeMap.find(type);
            if (itBreakpointsAtEvent != m_breakpointsAtEventTypeMap.end())
            {
                auto& list = itBreakpointsAtEvent->second;
                list.erase(std::remove_if(list.begin(), list.end(), [breakpointId](const ULONG& id) { 
                    return id == breakpointId; 
                }),
                    list.end());

                if (list.empty())
                {
                    m_breakpointsAtEventTypeMap.erase(itBreakpointsAtEvent);
                }
            }
        }
    }

    return S_OK;
}
