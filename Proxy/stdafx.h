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

using namespace ATL;
using namespace std;

#include "Helpers.h"
#include "Messages.h"
