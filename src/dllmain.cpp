// dllmain.cpp - DLL entry point and COM registration
#include "pch.h"
#include "Widget.h"
#include "ClassFactory.h"

HINSTANCE g_hInst = nullptr;
LONG      g_cRef  = 0;

CWidget* CreateWidget() { return new (std::nothrow) CWidget(); }

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { g_hInst = hMod; DisableThreadLibraryCalls(hMod); }
    return TRUE;
}

STDAPI DllCanUnloadNow() { return g_cRef == 0 ? S_OK : S_FALSE; }

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (!IsEqualCLSID(rclsid, CLSID_Widget)) return CLASS_E_CLASSNOTAVAILABLE;
    CClassFactory* p = new (std::nothrow) CClassFactory();
    if (!p) return E_OUTOFMEMORY;
    HRESULT hr = p->QueryInterface(riid, ppv);
    p->Release();
    return hr;
}

STDAPI DllRegisterServer()
{
    wchar_t dll[MAX_PATH];
    GetModuleFileNameW(g_hInst, dll, MAX_PATH);

    const wchar_t* clsid = L"{C1D2E3F4-A5B6-7C8D-9E0F-A1B2C3D4E5F6}";
    wchar_t key[256];
    HKEY hk = nullptr;

    wsprintfW(key, L"CLSID\\%s", clsid);
    RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hk, nullptr);
    RegSetValueExW(hk, nullptr, 0, REG_SZ, (BYTE*)APP_NAME, (DWORD)(wcslen(APP_NAME)+1)*2);
    RegCloseKey(hk);

    wsprintfW(key, L"CLSID\\%s\\InprocServer32", clsid);
    RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hk, nullptr);
    RegSetValueExW(hk, nullptr, 0, REG_SZ, (BYTE*)dll, (DWORD)(wcslen(dll)+1)*2);
    const wchar_t* apt = L"Apartment";
    RegSetValueExW(hk, L"ThreadingModel", 0, REG_SZ, (BYTE*)apt, (DWORD)(wcslen(apt)+1)*2);
    RegCloseKey(hk);

    ICatRegister* pCR = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_ICatRegister, (void**)&pCR)))
    {
        CATID cat = CATID_DeskBand;
        pCR->RegisterClassImplCategories(CLSID_Widget, 1, &cat);
        pCR->Release();
    }
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    const wchar_t* clsid = L"{C1D2E3F4-A5B6-7C8D-9E0F-A1B2C3D4E5F6}";
    wchar_t key[256];

    ICatRegister* pCR = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_StdComponentCategoriesMgr, nullptr,
        CLSCTX_INPROC_SERVER, IID_ICatRegister, (void**)&pCR)))
    {
        CATID cat = CATID_DeskBand;
        pCR->UnRegisterClassImplCategories(CLSID_Widget, 1, &cat);
        pCR->Release();
    }
    wsprintfW(key, L"CLSID\\%s\\InprocServer32", clsid);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
    wsprintfW(key, L"CLSID\\%s", clsid);
    RegDeleteKeyW(HKEY_CLASSES_ROOT, key);
    return S_OK;
}
