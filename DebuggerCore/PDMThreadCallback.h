//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once

using namespace ATL;

class PDMThreadCallback :
    public CComObjectRootEx<CComSingleThreadModel>,
    public IDebugThreadCall
{
public:
    BEGIN_COM_MAP(PDMThreadCallback)
        COM_INTERFACE_ENTRY(IDebugThreadCall)
        COM_INTERFACE_ENTRY2(IUnknown, IDebugThreadCall)
    END_COM_MAP()

    // IDebugThreadCall
    STDMETHODIMP ThreadCallHandler(DWORD_PTR param_dwMethod, DWORD_PTR param_pIn, DWORD_PTR param_pvArg)
    {
        ATLENSURE_RETURN_HR(param_pIn != 0, E_INVALIDARG);

        DWORD dwMethod = (DWORD)param_dwMethod;

        HRESULT hr = S_OK;

        switch (dwMethod)
        {
        case PDMThreadCallbackMethod::Rundown:
            {
                SourceController* pSourceController = (SourceController*)param_pIn;
                hr = pSourceController->EnumerateSources();
            }
            break;

        case PDMThreadCallbackMethod::Pause:
            {
                IDebugApplication* pDebugAppIn = (IDebugApplication*)param_pIn;
                hr = pDebugAppIn->CauseBreak();
            }
            break;

        case PDMThreadCallbackMethod::Resume:
            {
                ATLENSURE_RETURN_HR(param_pvArg != 0, E_INVALIDARG);

                IDebugApplication* pDebugAppIn = (IDebugApplication*)param_pIn;
                unique_ptr<ResumeFromBreakpointInfo> spInfo((ResumeFromBreakpointInfo*) param_pvArg);
                hr = pDebugAppIn->ResumeFromBreakPoint(spInfo->spDebugThread, spInfo->breakResumeAction, spInfo->errorResumeAction);
            }
            break;

        case PDMThreadCallbackMethod::CompleteEvals:
            {
                // This function is called synchronously to ensure that all async PDM queue messages have been completed,
                // So we just return S_OK to the caller.
                hr = S_OK;
            }
            break;

        case PDMThreadCallbackMethod::WaitForBreak:
            {
                ThreadController* pThreadController = (ThreadController*)param_pIn;
                hr = pThreadController->OnWaitCallback();
            }
            break;

        default:
            hr = E_FAIL;
            break;
        }

        return hr;
    }
};
