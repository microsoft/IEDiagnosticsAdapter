//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "resource.h"
#include "dllmain.h"

CProxyModule _AtlModule;
extern "C" void RegisterDebuggerAtlTypeLib(_In_ int typelibIndex);

// DLL Entry Point
extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hInstance);

    // Register the DebuggerDispatch as the second typelib in our dll
    ::RegisterDebuggerAtlTypeLib(2);

    return _AtlModule.DllMain(dwReason, lpReserved);
}

// Used to determine whether the DLL can be unloaded by OLE.
STDAPI DllCanUnloadNow(void)
{
    return _AtlModule.DllCanUnloadNow();
}

// Returns a class factory to create an object of the requested type.
STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _Outptr_ LPVOID* ppv)
{
    return _AtlModule.DllGetClassObject(rclsid, riid, ppv);
}

// DllRegisterServer - Adds entries to the system registry.
STDAPI DllRegisterServer(void)
{
    return _AtlModule.DllRegisterServer();
}

// DllUnregisterServer - Removes entries from the system registry.
STDAPI DllUnregisterServer(void)
{
    return _AtlModule.DllUnregisterServer();
}

// DllInstall - Adds/Removes entries to the system registry per user per machine.
STDAPI DllInstall(BOOL bInstall, _In_opt_ LPCWSTR pszCmdLine)
{
    return E_NOTIMPL;
}