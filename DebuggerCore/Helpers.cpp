//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "Helpers.h"

namespace F12
{
    HRESULT WaitAndPumpMessages(_In_ HANDLE hEvent)
    {
        HRESULT hr = S_OK;
        while (hr == S_OK)
        {
            DWORD dwWaitResult = ::MsgWaitForMultipleObjects(1, &hEvent, FALSE, INFINITE, QS_ALLINPUT);
            if (dwWaitResult != WAIT_FAILED)
            {
                if (dwWaitResult == WAIT_OBJECT_0)
                {
                    break;
                }
                else
                {
                    // There was a message put into our queue, so we need to dispose of it
                    MSG msg;
                    while (::PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
                    {
                        ::TranslateMessage(&msg);
                        ::DispatchMessage(&msg);
                        // Check if the event was triggered before pumping more messages
                        if (::WaitForSingleObject(hEvent, 0) == WAIT_OBJECT_0)
                        {
                            break;
                        }
                    }
                }
            }
            else
            {
                hr = ::AtlHresultFromLastError();
            }
        }
        return hr;
    }
}