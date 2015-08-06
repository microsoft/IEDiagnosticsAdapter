//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

// Messages used internally in the same process, but across threads
#define WM_PROCESSCOPYDATA          WM_USER + 1
#define WM_SET_MESSAGE_HWND         WM_USER + 2 // WPARAM is BSTR (id), LPARAM is HWND

#define WM_MESSAGE_RECEIVE          WM_USER + 3 // WPARAM is BSTR (MessagePacket), LPARAM is NULL
#define WM_MESSAGE_SEND             WM_USER + 4 // WPARAM is BSTR (data), LPARAM is NULL
#define WM_MESSAGE_IN_QUEUE         WM_USER + 5

#define WM_CREATE_ENGINE            WM_USER + 6 // WPARAM is BSTR (id), LPARAM is NULL
#define WM_BREAK_OCCURRED           WM_USER + 7
#define WM_TEST_TIMEOUT             WM_USER + 8
#define WM_TEST_START               WM_USER + 9

// Messages used across processes
UINT Get_WM_SET_CONNECTION_HWND();              // WPARAM is HWND (connectBackTo), LPARAM is NULL

enum class MessageType
{
    Inject,
    Execute,
    ExecuteAtBreak
};

struct MessagePacket
{
    MessageType m_messageType;
    CComBSTR m_engineId;
    CComBSTR m_scriptName;
    CComBSTR m_message;
};

// Used to send a string across processes
enum CopyDataPayload_ProcSignature : ULONG_PTR
{
    StringMessage_Signature
};

#pragma pack(push, 1)
struct CopyDataPayload_StringMessage_Data
{
    UINT uMessageOffset;
};
#pragma pack(pop)

PCOPYDATASTRUCT MakeCopyDataStructCopy(_In_ const PCOPYDATASTRUCT pCopyDataStruct);
void FreeCopyDataStructCopy(_In_ PCOPYDATASTRUCT pCopyDataStructCopy);
