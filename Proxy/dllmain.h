//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

#include "resource.h"
#include "Proxy_h.h"

class CProxyModule : public CAtlDllModuleT<CProxyModule>
{
public:
    DECLARE_LIBID(LIBID_ProxyLib)
};

extern class CProxyModule _AtlModule;