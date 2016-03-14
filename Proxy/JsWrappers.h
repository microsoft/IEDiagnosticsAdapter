//
// Copyright (C) Microsoft. All rights reserved.
//

#pragma once
#define USE_EDGEMODE_JSRT 
#include <jsrt.h>

class JsValueRefPtr
{
public:
    JsValueRefPtr() :
        m_value(nullptr)
    {
    }

    JsValueRefPtr(_Inout_opt_ JsValueRef value) :
        m_value(value)
    {
        if (m_value != nullptr)
        {
            ::JsAddRef(m_value, nullptr);
        }
    }

    ~JsValueRefPtr()
    {
        this->Release();
    }

    operator JsValueRef() const
    {
        return m_value;
    }

    bool operator==(const JsValueRefPtr& rhs) {
        return m_value == rhs.m_value;
    }

    bool operator==(const JsValueRef& rhs) {
        return m_value == rhs;
    }

    void Release()
    {
        JsValueRef temp = m_value;
        if (temp != nullptr)
        {
            m_value = nullptr;
            ::JsRelease(temp, nullptr);
        }
    }

    JsValueRef m_value;
};

class JsContextPtr
{
public:
    JsContextPtr(JsContextRef value) :
        m_value(value)
    {
        ::JsGetCurrentContext(&m_previousContext);

        if (m_value != nullptr)
        {
            ::JsSetCurrentContext(m_value);
        }
    }

    ~JsContextPtr()
    {
        if (m_previousContext != nullptr)
        {
            ::JsSetCurrentContext(m_previousContext);
        }
        else
        {
            ::JsSetCurrentContext(JS_INVALID_REFERENCE);
        }
    }

    JsContextRef m_value;

private:
    JsContextRef m_previousContext;
};