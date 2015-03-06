//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

#include <vector>
#include <map>
#include <functional>

// Implements event handling for an object.  Always use this class as it follows
// specific rules described below
class EventHelper
{
private:
    typedef std::vector<ATL::CComPtr<IDispatch>> listenerVector;
    typedef listenerVector::iterator listenerIterator;
    typedef std::map<ATL::CString, listenerVector> eventMap;
    typedef eventMap::iterator eventIterator;

    IUnknown* m_pParent; // weak reference to parent since we should be a non-pointer member of it.
    eventMap m_mappedEvents;
    eventMap m_queuedEventListeners;
    int m_numFire_EventsRunning;
    void ClearEmptyListeners();
    void AddQueuedListeners();

public:
    // This class should be a non-pointer member of pParent since
    // it only maintains a weak reference unless it's iterating.
    EventHelper(_In_ IUnknown* pParent);
    virtual ~EventHelper(void);

    static void MakeVariantParameter(_In_opt_ VARIANT* receivedVariant, _Out_ VARIANT& passMeToJs);

    // Calls all handlers for the event eventName in the order they were added with the parameters passed as args.
    // If an event is added multiple times, it will be called once for each time it was added, in the order it was added
    // with respect to other handlers (for instance if you add handlers 1,2,1 in that order, Fire_Event will call 1, then 2, then 1.
    // Optionally allows processing of the return value with EventReturnFunctor
    void Fire_Event(_In_ const ATL::CString& eventName, 
                    _In_opt_count_x_(argLength) VARIANTARG* args, 
                    _In_ const UINT argLength, 
                    _In_opt_ std::function<void(ATL::CComVariant *returnValue)> processReturn = [](ATL::CComVariant *){});

    // Adds listener to the list of event handlers to be called in Fire_Event for eventName.  
    // A single handler can be added multiple times (it will be called once for each time it is added).
    HRESULT addEventListener(_In_ BSTR eventName, _In_ IDispatch* listener);

    // Removes listener from the list of handlers to be called when eventName is fired.
    // Does nothing if the event has not been added.
    // If the event has been added multiple times, removes the latest addition
    HRESULT removeEventListener(_In_ BSTR eventName, _In_ IDispatch* listener);

    // Checks to see if the listener has already been added as a handler for eventName.
    // If not, returns 0 in the attachedCount out param.
    // If it is in the list, returns the number of occurences in the list.
    // If listener is NULL, returns the total number of handlers for the given event in attachedCount
    HRESULT isEventListenerAttached(_In_ BSTR eventName, _In_opt_ IDispatch* listener, _Out_ ULONG* pAttachedCount);

    // Clears all event listeners for all events on this object.  Useful for avoiding memory leaks.
    HRESULT removeAllEventListeners();
};
