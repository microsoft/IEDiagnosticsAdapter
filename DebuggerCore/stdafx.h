//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

#include <SDKDDKVer.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS
#define _SCL_SECURE_NO_WARNINGS
#define USE_EDGEMODE_JSRT 

#include <windows.h>
#include <MsHtml.h>
#include <atlbase.h>
#include <atlcom.h>
#include <atlstr.h>
#include <atlwin.h>
#include <algorithm>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <vector>
#include <jsrt.h>

#include "ComObjPtr.h"
#include "ThreadHelpers.h"

using namespace ATL;
using namespace std;

#define BPT_FAIL_IF_NOT_S_OK(hr) ATLENSURE_RETURN_HR(hr == S_OK, SUCCEEDED(hr) ? E_FAIL : hr)
#define BPT_THROW_IF_NOT_S_OK(hr) ATLENSURE_THROW(hr == S_OK, SUCCEEDED(hr) ? E_FAIL : hr)

#include "DebuggerDispatch.h"

// External function used for ensuring the LIB gets loaded when combined into a different dll
// And for setting the dispatch's TYPELIB index, to allow multiple IDispatchs to exist in that same dll.
extern "C" void RegisterDebuggerAtlTypeLib(_In_ int typelibIndex)
{
    SetTypeLibIndexForDispatch<CDebuggerDispatch>(typelibIndex);
};