//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

using namespace ATL;
using namespace std;

enum class PDMEventType
{
    SourceUpdated,
    BreakpointHit,
    PDMClosed
};

// PDMEventMessageQueue is a class for managing the pushing of messages from the PDM thread
// (primarily) to the debugger dispatch thread.
// Should be created and deinitialized on the UI thread, otherwise free threaded.
class ATL_NO_VTABLE PDMEventMessageQueue :
    public CComObjectRootEx<CComMultiThreadModelNoCS>
{
public:
    BEGIN_COM_MAP(PDMEventMessageQueue)
    END_COM_MAP()

    PDMEventMessageQueue();

public:
    HRESULT Initialize(_In_ HWND messageWnd);
    HRESULT Deinitialize();

    void Push(_In_ PDMEventType eventType);
    void PopAll(_Out_ std::vector<PDMEventType>& messages);

private:
    DWORD m_dispatchThreadId;
    HWND m_hwndNotify;
    bool m_isValid;

    CComAutoCriticalSection m_csMessageArray;
    vector<PDMEventType> m_messages;
};
