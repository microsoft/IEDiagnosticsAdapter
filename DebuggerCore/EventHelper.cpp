//
// Copyright (C) Microsoft. All rights reserved.
//

#include "StdAfx.h"
#include "EventHelper.h"
#include <algorithm>

using namespace ATL;
using namespace std;

EventHelper::EventHelper(_In_ IUnknown* pParent) : m_numFire_EventsRunning(0), m_pParent(pParent)
{
}

EventHelper::~EventHelper(void)
{
}

HRESULT EventHelper::addEventListener(_In_ BSTR eventName, _In_ IDispatch* listener)
{
    ATLENSURE_RETURN_HR(eventName != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(listener != nullptr, E_INVALIDARG);

    // Since we return size info in a ULONG to jscript, and jscript cannot understand ULONGLONG's
    // and size_t is typically unsigned int (equivalent to ULONG), let's make sure we don't add more than 
    // ULONG, even though this isn't likely ever to happen.
    ATLENSURE_RETURN_HR(m_mappedEvents[eventName].size() < ULONG_MAX, E_OUTOFMEMORY);

    if (m_numFire_EventsRunning == 0)
    {
        try
        {
            m_mappedEvents[eventName].push_back(CComPtr<IDispatch>(listener));  // CComPtr will addref as it's constructed 
                                                                                // on add and release when it's destructed
        }
        catch (bad_alloc)
        {
            return E_OUTOFMEMORY;
        }
    }
    else
    {
        try
        {
            // We are currently iterating through the events, so just add these to a queue to append later
            m_queuedEventListeners[eventName].push_back(CComPtr<IDispatch>(listener));
        }
        catch (bad_alloc)
        {
            return E_OUTOFMEMORY;
        }
    }

    return S_OK;
}

HRESULT EventHelper::removeEventListener(_In_ BSTR eventName, _In_ IDispatch* listener)
{
    ATLENSURE_RETURN_HR(eventName != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(listener != nullptr, E_INVALIDARG);
    
    HRESULT hr = S_FALSE;

    // Try to find the event in the queue, before finding in the current mapping, since we
    // want to remove the very latest added one first
    if (m_queuedEventListeners.find(eventName) != m_queuedEventListeners.end())
    {
        // Find the last occurence of listener using reverse iterators
        vector<CComPtr<IDispatch>>::reverse_iterator eraseMe;
        eraseMe = find
                    (
                        m_queuedEventListeners[eventName].rbegin(),
                        m_queuedEventListeners[eventName].rend(),
                        CComPtr<IDispatch>(CComPtr<IDispatch>(listener))
                    );
        if (eraseMe != m_queuedEventListeners[eventName].rend())
        {
            m_queuedEventListeners[eventName].erase(eraseMe.base() - 1);
            hr = S_OK;
        }
    }

    // Check to see if we already found one to remove
    if (hr != S_OK)
    {
        // We didn't remove one already, so try to find one in the current mappings
        if (m_mappedEvents.find(eventName) != m_mappedEvents.end())
        {
            // Find the last occurence of listener using reverse iterators
            vector<CComPtr<IDispatch>>::reverse_iterator eraseMe;
            eraseMe = find
                        (
                            m_mappedEvents[eventName].rbegin(),
                            m_mappedEvents[eventName].rend(),
                            CComPtr<IDispatch>(CComPtr<IDispatch>(listener))
                        );
            if (eraseMe != m_mappedEvents[eventName].rend())
            {
                if (m_numFire_EventsRunning == 0)
                {
                    // erase it (erase needs a forward iterator, so get it from 
                    // the reverse iterator by calling base and subtracting 1
                    // since a reverse iterator dereferenced is one past the
                    // equivalent iterator dereferenced.
                    m_mappedEvents[eventName].erase(eraseMe.base() - 1);
                }
                else
                {
                    // we're in the middle of at least one Fire_Event run and
                    // erasing this now would mess up that run.  Let's release
                    // it and set it to NULL instead and it will be cleaned
                    // up on the last Fire_Event run's finishing
                    eraseMe->Release();
                    eraseMe->p = nullptr; // CComPtr should do this for us, but if we ever 
                                          // change this to a raw ptr, I just want to make
                                          // sure we don't forget to NULL it since that's
                                          // what tell's Fire_Event to ignore it.
                }
                hr = S_OK;
            }
        }
    }
    return hr;
}

HRESULT EventHelper::isEventListenerAttached(_In_ BSTR eventName, _In_opt_ IDispatch* listener, _Out_ ULONG* pAttachedCount)
{
    ATLENSURE_RETURN_HR(eventName != NULL, E_INVALIDARG);
    ATLENSURE_RETURN_HR(pAttachedCount != nullptr, E_INVALIDARG);
    *pAttachedCount = 0;

    if (m_queuedEventListeners.find(eventName) != m_queuedEventListeners.end())
    {
        listenerIterator it;
        for (it = m_queuedEventListeners[eventName].begin();
             it != m_queuedEventListeners[eventName].end();
             it++)
        {
            if ((listener == nullptr) ||
                (listener != nullptr && *it == listener))
            {
                (*pAttachedCount)++;
            }
        }
    }

    if (m_mappedEvents.find(eventName) != m_mappedEvents.end())
    {
        listenerIterator it;
        for (it = m_mappedEvents[eventName].begin();
             it != m_mappedEvents[eventName].end();
             it++)
        {
            if ((listener == nullptr && *it != nullptr) ||
                (listener != nullptr && *it == listener))
            {
                (*pAttachedCount)++;
            }
        }
    }

    return S_OK;
}

HRESULT EventHelper::removeAllEventListeners()
{
    // Mark this as iterating through the events
    m_numFire_EventsRunning++;

    // Loop through all the events and remove them
    eventIterator e_it;
    for (e_it = m_mappedEvents.begin(); e_it != m_mappedEvents.end(); e_it++)
    {
        listenerIterator l_it;
        for (l_it = e_it->second.begin(); l_it != e_it->second.end(); l_it++)
        {
            // Check that this hasn't already been marked for removal
            if (l_it->p != nullptr)
            {
                // Remove it
                removeEventListener(CComBSTR(e_it->first), *l_it);
            }
        }
    }

    m_queuedEventListeners.clear();

    // No longer iterating
    m_numFire_EventsRunning--;

    if (m_numFire_EventsRunning == 0)
    {
        // Clean up the waiting add and remove events
        AddQueuedListeners();
        ClearEmptyListeners();
    }
    return S_OK;
}

void EventHelper::MakeVariantParameter(_In_opt_ VARIANT* receivedVariant, _Out_ VARIANT& passMeToJs)
{
    passMeToJs.vt = VT_BYREF | VT_VARIANT;
    if (receivedVariant == nullptr || receivedVariant->vt == VT_EMPTY)
    {
        passMeToJs.pvarVal = nullptr;
    }
    else if (receivedVariant->vt == (VT_BYREF|VT_VARIANT))
    {
        passMeToJs = * (receivedVariant->pvarVal);
    }
    else
    {
        passMeToJs.pvarVal = receivedVariant;
    }
}

void EventHelper::Fire_Event(_In_ const CString& eventName,
                             _In_opt_count_x_(argLength) VARIANTARG* args,
                             _In_ const UINT argLength,
                             _In_opt_ function<void(CComVariant *returnValue)> processReturn)
{
    CComPtr<IUnknown> spKeepParentAlive(m_pParent);
    ATLENSURE(spKeepParentAlive.p != nullptr);

    if (m_mappedEvents.find(eventName) != m_mappedEvents.end())
    {
        m_numFire_EventsRunning++;
        listenerIterator it;
        for (it = m_mappedEvents[eventName].begin(); it != m_mappedEvents[eventName].end(); it++)
        {
            // We NULL values in RemoveEventListener if this loop is running instead of 
            // erasing them because erase would mess up the iterator.
            // Skip them here
            if (it->p != nullptr)
            {
                // Call the javascript callback
                DISPPARAMS dispParams = {
                    args,      // parameters
                    nullptr,   // Named Parameters = JScript doesn't understand this
                    argLength, // no. of parameters
                    0 };       // no. of named parameters

                CComVariant jsReturn;

                // In case the listener tries to remove itself and we are its last
                // outstanding reference, keep it alive with a local addref
                CComPtr<IDispatch> spListener(*it);

                HRESULT hr = spListener->Invoke(
                    DISPID_VALUE,
                    IID_NULL,
                    LOCALE_USER_DEFAULT,
                    DISPATCH_METHOD,
                    &dispParams,
                    &jsReturn,
                    nullptr,
                    nullptr);
                (hr);
                // We don't need to throw if javascript throws.
                // ATLASSERT(SUCCEEDED(hr));

                processReturn(&jsReturn);

                // exit the loop if the listener cleared either the mapped events or the listeners on this event
                if (m_mappedEvents.size() == 0 || m_mappedEvents[eventName].size() == 0)
                {
                    break;
                }
            }
        }
        m_numFire_EventsRunning--;
        _ASSERT_EXPR(m_numFire_EventsRunning >= 0, L"There should never be a negative number of event listeners.");
        if (m_numFire_EventsRunning == 0)
        {
            AddQueuedListeners();
            ClearEmptyListeners();
        }
    }
}

void EventHelper::ClearEmptyListeners()
{
    ATLENSURE(m_numFire_EventsRunning == 0);

    for (eventIterator e_it = m_mappedEvents.begin();
         e_it != m_mappedEvents.end();
         e_it++)
    {
        listenerVector& listener = e_it->second;
        for (listenerIterator l_it = listener.begin();
             l_it < listener.end();
             // Do nothing here since we'll advance the iterator as we remove elements (or don't)
            )
        {
            if (l_it->p == nullptr) // it was removed during a Fire_Event loop, let's erase it now
            {
                l_it = listener.erase(l_it);
            }
            else
            {
                l_it++;
            }
        }
    }
}

void EventHelper::AddQueuedListeners()
{
    ATLENSURE(m_numFire_EventsRunning == 0);

    // Loop through all the queued listeners and add them now that we are out of the loop
    for (eventIterator e_it = m_queuedEventListeners.begin();
         e_it != m_queuedEventListeners.end();
         e_it++)
    {
        listenerVector& listener = e_it->second;
        for (listenerIterator l_it = listener.begin();
             l_it < listener.end();
             l_it++
            )
        {
            // Add this to the main mapped listeners
            m_mappedEvents[e_it->first].push_back(*l_it);
        }
    }

    // Now clear the queue
    m_queuedEventListeners.clear();
}