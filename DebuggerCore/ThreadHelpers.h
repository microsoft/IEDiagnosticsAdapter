//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

namespace ThreadHelpers
{
    bool static IsOnDispatchThread(_In_ DWORD dispatchThreadId) { return (::GetCurrentThreadId() == dispatchThreadId); }
    bool static IsOnPDMThread(_In_ DWORD dispatchThreadId) { return (::GetCurrentThreadId() != dispatchThreadId); }
};
