//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

using namespace ATL;
using namespace std;

// ConnectionResult definition must match TypeScript definition in $\src\bpt\diagnostics\debugger\Manager\commonStructs.ts
enum class ConnectionResult
{
    Succeeded = 0,
    Failed = 1,
    FailedAlreadyAttached = 2
};

// CauseBreakAction definition must match TypeScript values in $\src\bpt\diagnostics\debugger\Manager\CommonStructs.ts
enum class CauseBreakAction
{
    BreakOnAny = 0,
    BreakOnAnyNewWorkerStarting = 1,
    BreakIntoSpecificWorker = 2,
    UnsetBreakOnAnyNewWorkerStarting = 3
};

enum class PDMThreadCallbackMethod
{
    Rundown,
    Pause,
    Resume,
    CompleteEvals,
    WaitForBreak
};

// Document
struct DocumentInfo
{
    ULONG docId;
    ULONG parentId;
    CComBSTR url;
    CComBSTR mimeType;
    CComBSTR sourceMapUrlFromHeader;
    CComBSTR longDocumentId;
    ULONG textLength;
    bool isDynamicCode;

    DocumentInfo() :
        docId(0),
        parentId(0),
        textLength(0),
        isDynamicCode(false)
    {
    }
};

// Source Location
struct SourceLocationInfo
{
    ULONG docId;
    ULONG lineNumber;
    ULONG columnNumber;
    ULONG charPosition;
    ULONG contextCount;

    SourceLocationInfo() :
        docId(0),
        lineNumber(0),
        columnNumber(0),
        charPosition(0),
        contextCount(0)
    {
    }
};

// Callstack
struct CallFrameInfo
{
    ULONG id;
    BSTR functionName;
    bool isInTryBlock;
    bool isInternal;
    SourceLocationInfo sourceLocation;

    CallFrameInfo() :
        id(0),
        isInTryBlock(false),
        isInternal(false)
    {
    }
};

// Breakpoint
struct BreakpointInfo
{
    ULONG id;
    shared_ptr<SourceLocationInfo> spSourceLocation;
    shared_ptr<std::vector<CComBSTR>> spEventTypes;
    CComBSTR url;
    CComBSTR condition;
    bool isTracepoint;
    bool isBound;
    bool isEnabled;

    BreakpointInfo() :
        id(0),
        spSourceLocation(nullptr),
        spEventTypes(nullptr),
        isTracepoint(false),
        isBound(false),
        isEnabled(false)
    {
    }
};

// Breakpoint rebind mapping
struct ReboundBreakpointInfo
{
    ULONG breakpointId;
    ULONG newDocId;
    ULONG start;  // Legacy info needed for front end
    ULONG length; // Legacy info needed for front end
    bool isBound;

    ReboundBreakpointInfo() :
        breakpointId(0),
        newDocId(0),
        start(0),
        length(0),
        isBound(true)
    {
    }
};

// Break Event
struct BreakEventInfo
{
    ULONG firstFrameId;
    WORD errorId;
    BREAKREASON breakReason;
    CComBSTR description;
    bool isFirstChanceException;
    bool isUserUnhandled;
    std::vector<shared_ptr<BreakpointInfo>> breakpoints; // Applies only when the breakreason is breakpoint
    DWORD systemThreadId;
    CComBSTR breakEventType;

    BreakEventInfo() :
        firstFrameId(0),
        errorId(0),
        breakReason(BREAKREASON_ERROR),
        description(),
        isFirstChanceException(false),
        isUserUnhandled(false),
        systemThreadId(0),
        breakEventType()
    {
    }
};

// Property
struct PropertyInfo
{
    ULONG propertyId;
    CComBSTR name;
    CComBSTR type;
    CComBSTR value;
    CComBSTR fullName;
    bool isExpandable;
    bool isReadOnly;
    bool isFake;
    bool isInvalid;
    bool isReturnValue;

    PropertyInfo() :
        propertyId(0),
        isExpandable(false),
        isReadOnly(false),
        isFake(false),
        isInvalid(false),
        isReturnValue(false)
    {
    }
};

// Resume Event
struct ResumeFromBreakpointInfo
{
    CComPtr<IRemoteDebugApplicationThread> spDebugThread;
    BREAKRESUMEACTION breakResumeAction;
    ERRORRESUMEACTION errorResumeAction;
};

const  ULONG INVALID_ASDFILEHANDLE = (ULONG)-1l;
const  ULONG INVALID_CALLFRAMEID   = (ULONG)-1l;
