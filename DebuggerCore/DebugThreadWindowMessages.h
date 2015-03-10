//
// Copyright (C) Microsoft. All rights reserved.
//
#pragma once

// The following #defines give string names to timer events so they can be treated the same as DOM events which have a type name
// They should remain in sync with the names defined in eventBreakpointDescriptions.json which must match these names for the registration to work
#define TIMER_EVENT_NAME_ANIMATION_FRAME            L"requestanimationframe"
#define TIMER_EVENT_NAME_IMMEDIATE                  L"setimmediate"
#define TIMER_EVENT_NAME_INTERVAL                   L"setinterval"
#define TIMER_EVENT_NAME_TIMEOUT                    L"settimeout"

#define IDM_DEBUGGERDYNAMICATTACH       15202
#define IDM_DEBUGGERDYNAMICDETACH       15203
#define IDM_DEBUGGERDYNAMICATTACHSOURCERUNDOWN 15204
#define IDM_GETDEBUGGERSTATE            15205

// Message sent to the DebugThreadController
#define WM_CONTROLLERCOMMAND_FIRST       WM_USER + 400  
#define WM_ENABLEDYNAMICDEBUGGING        WM_CONTROLLERCOMMAND_FIRST      // wParam is NULL
#define WM_DISABLEDYNAMICDEBUGGING       WM_CONTROLLERCOMMAND_FIRST + 1  // wParam is NULL
#define WM_TOOLDETACHED                  WM_CONTROLLERCOMMAND_FIRST + 2  // wParam is NULL
#define WM_FORCEDISABLEDYNAMICDEBUGGING  WM_CONTROLLERCOMMAND_FIRST + 3  // wParam is NULL
#define WM_CONTROLLERCOMMAND_LAST        WM_CONTROLLERCOMMAND_FIRST + 3

// Message that are sent to the DebuggerCore
#define WM_DEBUGGERNOTIFICATION_FIRST    WM_USER + 410   
#define WM_PROCESSDEBUGGERPACKETS        WM_DEBUGGERNOTIFICATION_FIRST      // wParam is NULL - Process any packets in the debugger message queue.
#define WM_DEBUGGINGENABLED              WM_DEBUGGERNOTIFICATION_FIRST + 1  // wParam is BOOL (indicates success or failure), lParam is IUnknown* (an IRemoteDebugApplication)
#define WM_DEBUGGINGDISABLED             WM_DEBUGGERNOTIFICATION_FIRST + 2  // wParam is BOOL (indicates success or failure)
#define WM_BREAKNOTIFICATIONEVENT        WM_DEBUGGERNOTIFICATION_FIRST + 3  // wParam is HANDLE - The event to wait on.
#define WM_DOCKEDSTATECHANGED            WM_DEBUGGERNOTIFICATION_FIRST + 4  // wParam is BOOL (true = docked, false = undocked)
#define WM_WAITFORATTACH                 WM_DEBUGGERNOTIFICATION_FIRST + 5  // wParam is HANDLE_PTR (the event to signal when attached), lParam is BOOL (TRUE to wait, FALSE to signal the attach completed)
#define WM_DEBUGAPPLICATIONCREATE        WM_DEBUGGERNOTIFICATION_FIRST + 6  // wParam is BOOL (true = profiling, false = not profiling)
#define WM_WEBWORKERSTARTED              WM_DEBUGGERNOTIFICATION_FIRST + 7  // wParam is int (uniqueId of the thread), lParam is BSTR (label of the webworker)
#define WM_WEBWORKERFINISHED             WM_DEBUGGERNOTIFICATION_FIRST + 8  // wParam is int (uniqueId of the thread)
#define WM_EVENTBREAKPOINT               WM_DEBUGGERNOTIFICATION_FIRST + 9  // wParam is BOOL (true = starting invoke, false = invoke complete), lParam is BSTR (eventType of the breakpoint)
#define WM_EVENTLISTENERUPDATED          WM_DEBUGGERNOTIFICATION_FIRST + 10 // wParam is BOOL (true = added, false = removed)
#define WM_DEBUGGERNOTIFICATION_LAST     WM_DEBUGGERNOTIFICATION_FIRST + 10

// Message that are sent to the BHO
#define WM_BHONOTIFICATION_FIRST         WM_USER + 450 
#define WM_RUNDOWNWORKERS                WM_BHONOTIFICATION_FIRST      // wParam is NULL
#define WM_BREAKMODECHANGED              WM_BHONOTIFICATION_FIRST + 1  // wParam is BOOL (true = break, false = resume)
#define WM_REGISTEREVENTLISTENER         WM_BHONOTIFICATION_FIRST + 2  // wParam is BOOL (true = add listener, false = remove listener)
#define WM_BHONOTIFICATION_LAST          WM_BHONOTIFICATION_FIRST + 2
