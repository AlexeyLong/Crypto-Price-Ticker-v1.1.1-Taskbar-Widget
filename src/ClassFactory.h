// ClassFactory.h - COM IClassFactory
#pragma once
#include "pch.h"

class CWidget;
extern LONG      g_cRef;
extern CWidget*  CreateWidget();

class CClassFactory : public IClassFactory
{
public:
    CClassFactory()  : m_ref(1) { InterlockedIncrement(&g_cRef); }
    ~CClassFactory()             { InterlockedDecrement(&g_cRef); }

    STDMETHOD_(ULONG, AddRef)() override  { return InterlockedIncrement(&m_ref); }
    STDMETHOD_(ULONG, Release)() override
    {
        ULONG c = InterlockedDecrement(&m_ref);
        if (!c) delete this;
        return c;
    }
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory))
        { *ppv = static_cast<IClassFactory*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHOD(CreateInstance)(IUnknown* pOuter, REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (pOuter) return CLASS_E_NOAGGREGATION;
        CWidget* p = CreateWidget();
        if (!p) return E_OUTOFMEMORY;
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    STDMETHOD(LockServer)(BOOL b) override
    {
        if (b) InterlockedIncrement(&g_cRef);
        else   InterlockedDecrement(&g_cRef);
        return S_OK;
    }
private:
    LONG m_ref;
};
