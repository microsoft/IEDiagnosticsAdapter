//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "IEDiagnosticsAdapter.h"
#include "Helpers.h"
#include <iostream>
#include <Shellapi.h>
#include <Shlobj.h>
#include "Aclapi.h"

CHandle hChromeProcess;

BOOL WINAPI OnClose(DWORD reason)
{
    if (hChromeProcess.m_h)
    {
        ::TerminateProcess(hChromeProcess.m_h, 0);
    }
    return TRUE;
}

int wmain(int argc, wchar_t* argv[])
{
    // Initialize COM and deinitialize when we go out of scope
    HRESULT hrCoInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    shared_ptr<HRESULT> spCoInit(&hrCoInit, [](const HRESULT* hrCom) -> void { if (SUCCEEDED(*hrCom)) { ::CoUninitialize(); } });
    {
        // Set a close handler to shutdown the chrome instance we launch
        ::SetConsoleCtrlHandler(OnClose, TRUE);

        // Launch chrome
        {
            CString chromePath;

            // Find the chrome install location via the registry
            CString keyPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe";

            CRegKey regKey;

            // First see if we can find where Chrome is installed from the registry. This will only succeed if Chrome is installed for all users
            if (regKey.Open(HKEY_LOCAL_MACHINE, keyPath, KEY_READ) == ERROR_SUCCESS)
            {
                ULONG bufferSize = MAX_PATH;
                CString path;
                LRESULT result = regKey.QueryStringValue(nullptr, path.GetBufferSetLength(bufferSize), &bufferSize);
                path.ReleaseBufferSetLength(bufferSize);
                if (result == ERROR_SUCCESS)
                {
                    chromePath = path;
                }
            }

            if (chromePath.GetLength() == 0)
            {
                // If Chrome is only installed for the current user, look in \AppData\Local\Google\Chrome\Application\ for Chrome.exe
                CString appPath;
                ::SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appPath.GetBuffer(MAX_PATH + 1));
                appPath.ReleaseBuffer();
                chromePath = appPath + L"\\Google\\Chrome\\Application\\chrome.exe";
            }

            // Get a temp location
            CString temp;
            Helpers::ExpandEnvironmentString(L"%Temp%", temp);

            // Set arguments for the chrome that we launch
            CString arguments;
            arguments.Format(L"about:blank --remote-debugging-port=9223 --window-size=0,0 --silent-launch --no-first-run --no-default-browser-check --user-data-dir=\"%s\"", temp);

            // Launch the process
            STARTUPINFO si = { 0 };
            PROCESS_INFORMATION pi = { 0 };
            si.cb = sizeof(si);
            si.wShowWindow = SW_MINIMIZE;

            BOOL result = ::CreateProcess(
                chromePath,
                arguments.GetBuffer(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                temp,
                &si,
                &pi);
            arguments.ReleaseBuffer();

            if (result)
            {
                // Store the handles
                CHandle hThread(pi.hThread);
                hChromeProcess.Attach(pi.hProcess);
                DWORD waitResult = ::WaitForInputIdle(hChromeProcess, 30000);
            }
            else
            {
                std::cerr << "Could not open Chrome. Please ensure that Chrome is installed." << std::endl;
                system("pause");
                return -1;
            }
        }
        
        // Get the current path that we are running from
        CString fullPath;
        DWORD count = ::GetModuleFileName(nullptr, fullPath.GetBuffer(MAX_PATH), MAX_PATH);
        fullPath.ReleaseBufferSetLength(count);

        LPWSTR buffer = fullPath.GetBuffer();
        LPWSTR newPath = ::PathFindFileName(buffer);
        if (newPath && newPath != buffer)
        {
            fullPath.ReleaseBufferSetLength((newPath - buffer));
        }
        else
        {
            fullPath.ReleaseBuffer();
        }

		// Check to make sure that the dll has the ACLs to load in an appcontainer
		// We're doing this here as the adapter has no setup script and should be xcopy deployable/removeable

		PACL pOldDACL = NULL, pNewDACL = NULL;
		PSECURITY_DESCRIPTOR pSD = NULL;
		EXPLICIT_ACCESS ea;
		SECURITY_INFORMATION si = DACL_SECURITY_INFORMATION;

		// The check is done on the folder and should be inherited to all objects
		DWORD dwRes = GetNamedSecurityInfo(fullPath, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &pOldDACL, NULL, &pSD);

		// Initialize an EXPLICIT_ACCESS structure for the new ACE. 
		ZeroMemory(&ea, sizeof(EXPLICIT_ACCESS));
		ea.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
		ea.grfAccessMode = SET_ACCESS;
		ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
		ea.Trustee.TrusteeForm = TRUSTEE_IS_NAME;
		ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
		ea.Trustee.ptstrName = L"ALL APPLICATION PACKAGES";

		// Create a new ACL that merges the new ACE into the existing DACL.
		dwRes = SetEntriesInAcl(1, &ea, pOldDACL, &pNewDACL);
		if (dwRes == ERROR_SUCCESS)
		{
			dwRes = SetNamedSecurityInfo(fullPath.GetBuffer(), SE_FILE_OBJECT, si, NULL, NULL, pNewDACL, NULL);
			if (dwRes == ERROR_SUCCESS)
			{

			}
		}

		if (dwRes != ERROR_SUCCESS)
		{
			// The ACL was not set, this isn't fatal as it only impacts IE in EPM and Edge and the user can set it manually
			wcout << L"Could not set ACL to allow access to IE EPM or Edge.";
			wcout << L"\n";
			wcout << Helpers::GetLastErrorMessage().GetBuffer();
			wcout << L"\n";
			wcout << L"You can set the ACL manually by adding Read & Execute permissions for 'All APPLICATION PACAKGES' to each dll.";
			wcout << L"\n";
		}

		if (pSD != NULL)
		{
			LocalFree((HLOCAL)pSD);
		}
		if (pNewDACL != NULL)
		{
			LocalFree((HLOCAL)pNewDACL);
		}




        // Load the proxy server
        IEDiagnosticsAdapter proxy(fullPath);

        // Create a message pump
        MSG msg;
        ::PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

        // Thread message loop
        BOOL getMessageRet;
        while ((getMessageRet = ::GetMessage(&msg, NULL, 0, 0)) != 0)
        {
            if (getMessageRet != -1)
            {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
            }
        }

        // Leave the window open so we can read the log file
        wcout << endl << L"Press [ENTER] to exit." << endl;
        cin.ignore();
    }

    return 0;
}
