//
// Copyright (C) Microsoft. All rights reserved.
//

#include "stdafx.h"
#include "SourceNode.h"
#include "Strsafe.h"

SourceNode::SourceNode() :
    m_dispatchThreadId(0), // We are always created on the PDM thread so we get initialized with the dispatch thread id later
    m_docId(0),
    m_parentDocId(INVALID_ASDFILEHANDLE),
    m_textLength(0),
    m_isDynamicCode(false),
    m_isDirty(true)
{
}

void SourceNode::FinalRelease()
{
    // Remove the event listeners
    m_spListener->Deinitialize();
}

HRESULT SourceNode::Initialize(_In_ DWORD dispatchThreadId, _In_ SourceController* pSourceController, _In_ ULONG docId, _In_ IDebugApplicationNode* pDebugApplicationNode, _In_ ULONG parentDocId)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(dispatchThreadId), E_UNEXPECTED);
    ATLENSURE_RETURN_HR(pSourceController != nullptr, E_INVALIDARG);
    ATLENSURE_RETURN_HR(pDebugApplicationNode != nullptr, E_INVALIDARG);

    m_dispatchThreadId = dispatchThreadId;
    m_docId = docId;
    m_spDebugApplicationNode = pDebugApplicationNode;
    m_parentDocId = parentDocId;

    // Query for the document text (many files won't have this if they are managed by chakra and not trident)
    CComPtr<IDebugDocument> spDebugDocument;
    HRESULT hr = m_spDebugApplicationNode->GetDocument(&spDebugDocument);
    if (hr == S_OK)
    {
        hr = spDebugDocument->QueryInterface<IDebugDocumentText>(&m_spDebugDocumentText);
        if (hr != S_OK)
        {
            m_spDebugDocumentText.Release();
        }
    }

    // Populate the information
    hr = this->UpdateInfo();
    BPT_FAIL_IF_NOT_S_OK(hr);

    // Create object containing source and text event sinks
    CComObject<SourceEventListener>* pListener;
    hr = CComObject<SourceEventListener>::CreateInstance(&pListener);
    BPT_FAIL_IF_NOT_S_OK(hr);

    hr = pListener->Initialize(m_dispatchThreadId, pSourceController, m_docId, m_spDebugApplicationNode, m_spDebugDocumentText);
    BPT_FAIL_IF_NOT_S_OK(hr);

    m_spListener = pListener;

    return hr;
}

ULONG SourceNode::GetDocId() const 
{ 
    return m_docId; 
}

bool SourceNode::IsDirty() const
{
    return m_isDirty;
}

HRESULT SourceNode::GetDebugApplicationNode(_Out_ CComPtr<IDebugApplicationNode>& spDebugApplicationNode)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    spDebugApplicationNode = m_spDebugApplicationNode;
    
    return S_OK;
}

HRESULT SourceNode::AddChild(_In_ ULONG childDocId)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    ATLASSERT(m_childDocIdMap.find(childDocId) == m_childDocIdMap.end());
    m_childDocIdMap[childDocId] = true;

    return S_OK;
}

HRESULT SourceNode::RemoveChild(_In_ ULONG childDocId)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    ATLASSERT(m_childDocIdMap.find(childDocId) != m_childDocIdMap.end());
    m_childDocIdMap.erase(childDocId);

    return S_OK;
}

HRESULT SourceNode::GetChildren(_Out_ map<ULONG, bool>& childDocIdMap)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    childDocIdMap = m_childDocIdMap;

    return S_OK;
}

HRESULT SourceNode::UpdateInfo() 
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    this->UpdateUrl();
    this->UpdateIsDynamicCode(); // Must be done before mimetype to get the correct type
    this->UpdateMimeType();
    this->UpdateSourceTextLength();

    m_isDirty = true;

    return S_OK;
}

HRESULT SourceNode::GetDocumentInfo(_Out_ shared_ptr<DocumentInfo>& spDocumentInfo, _Out_ CComPtr<IDebugDocumentText>& spDebugDocumentText)
{
    ATLENSURE_RETURN_HR(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), E_UNEXPECTED);

    if (m_isDirty)
    {
        m_spDocumentInfo.reset(new DocumentInfo());
        m_spDocumentInfo->docId = m_docId;
        m_spDocumentInfo->parentId = m_parentDocId;
        m_spDocumentInfo->mimeType = m_bstrMimeType;
        m_spDocumentInfo->url = m_bstrUrl;
        m_spDocumentInfo->textLength = m_textLength;
        m_spDocumentInfo->isDynamicCode = m_isDynamicCode;

        // Set m_spDocumentInfo->longDocumentId to a string representing the doc pointer
        UINT64 docPtr = (UINT64)(m_spDebugDocumentText.p);
        CString longDocId;
        longDocId.Format(L"%llu", docPtr);
        CComBSTR ccombstr(longDocId);
        ccombstr.Append(_T(""));
        m_spDocumentInfo->longDocumentId = ccombstr;

        CComBSTR sourceMapUrlFromHeader;

        HRESULT hr = m_spDebugApplicationNode->GetName(DOCUMENTNAMETYPE_SOURCE_MAP_URL, &sourceMapUrlFromHeader);
        if (hr == S_OK)
        {
            m_spDocumentInfo->sourceMapUrlFromHeader = sourceMapUrlFromHeader;
        }
        else
        {
            m_spDocumentInfo->sourceMapUrlFromHeader = L"";
        }
    }

    spDocumentInfo = m_spDocumentInfo;
    spDebugDocumentText = m_spDebugDocumentText;

    m_isDirty = false;

    return S_OK;
}

// Helper functions
void SourceNode::UpdateUrl()
{
    ATLENSURE_RETURN_VAL(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), );

    m_isDirty = true;

    CComBSTR url;
    HRESULT hr = m_spDebugApplicationNode->GetName(DOCUMENTNAMETYPE_URL, &url);

    if (hr != S_OK)
    {
        hr = m_spDebugApplicationNode->GetName(DOCUMENTNAMETYPE_UNIQUE_TITLE, &url);

        if (hr != S_OK)
        {
            hr = m_spDebugApplicationNode->GetName(DOCUMENTNAMETYPE_APPNODE, &url);
        }
    }

    if (hr == S_OK && url != static_cast<LPCSTR>(nullptr))
    {
        m_bstrUrl = url;
    }
}

void SourceNode::UpdateIsDynamicCode()
{
    ATLENSURE_RETURN_VAL(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), );

    m_isDirty = true;
    m_isDynamicCode = false;

    // Determine if this is dynamic code purely based on the name
    // Note that IE always specifies URLs here, so this is only possible from Chakra
    LPCWSTR dynamicCodes[] = {
        L"Function code",
        L"eval code",
        L"script block",
        L"Unknown script code"
    };

    size_t urlLength = (size_t)m_bstrUrl.Length();

    for (size_t i = 0; i < ARRAYSIZE(dynamicCodes); i++)
    {
        size_t codesLength;
        if (::StringCchLength(dynamicCodes[i], STRSAFE_MAX_CCH, &codesLength) != S_OK)
        {
            m_isDynamicCode = false;
            break;
        }

        if (codesLength <= urlLength &&
            ::CompareStringOrdinal(m_bstrUrl, (int) codesLength, dynamicCodes[i], (int) codesLength, FALSE) == CSTR_EQUAL)
        {
            m_isDynamicCode = true;
            break;
        }
    }
}

void SourceNode::UpdateMimeType()
{
    ATLENSURE_RETURN_VAL(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), );

    m_isDirty = true;

    // Determine the mimetype of a script document
    if (m_spDebugDocumentText != nullptr)
    {
        TEXT_DOC_ATTR textAttr = 0;
        HRESULT hr = m_spDebugDocumentText->GetDocumentAttributes(&textAttr);
        if (hr == S_OK)
        {
            if ((textAttr & TEXT_DOC_ATTR_TYPE_SCRIPT) || (textAttr & TEXT_DOC_ATTR_TYPE_WORKER) || m_isDynamicCode)
            {
                // This could be some other kind of script, we're assuming javascript
                m_bstrMimeType = L"text/javascript";
            }
            else
            {
                // This could be xml or svg, we're assuming html
                m_bstrMimeType = "text/html";
            }

            return;
        }
    }

    // if there's no document, we're only going to send plain text for source
    m_bstrMimeType = L"text/plain";
}

void SourceNode::UpdateSourceTextLength()
{
    ATLENSURE_RETURN_VAL(ThreadHelpers::IsOnPDMThread(m_dispatchThreadId), );

    m_isDirty = true;

    ULONG charCount = 0;

    if (m_spDebugDocumentText.p != nullptr)
    {
        // Get the source text size
        #pragma prefast(suppress: __WARNING_INVALID_PARAM_VALUE_1, "activdbg.h annotation wrong, _Out_opt_ in dochelp.cpp");
        HRESULT hr = m_spDebugDocumentText->GetSize(nullptr, &charCount);
        if (hr != S_OK)
        {
            charCount = 0;
        }
    }

    m_textLength = charCount;
}
