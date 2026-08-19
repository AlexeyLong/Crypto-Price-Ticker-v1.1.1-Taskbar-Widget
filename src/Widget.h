// Widget.h - Crypto Price Ticker v1.1.1 Release
#pragma once
#include "pch.h"

// ── App info ─────────────────────────────────────────────────
#define APP_NAME        L"Crypto Price Ticker"
#define APP_VERSION     L"v1.1.1 Release"
#define APP_REG_KEY     L"Software\\CryptoPriceTicker"
#define APP_CFG_FILE    L"PairsList.cfg"
#define APP_WND_CLASS   L"CryptoPriceTickerWnd"

// ── Band dimensions ──────────────────────────────────────────
#define BAND_HEIGHT     30
#define BAND_PAD        8
#define BAND_MIN_W      350
#define BAND_MAX_W      500

// ── Font ─────────────────────────────────────────────────────
#define FONT_SIZE_PT    9

// ── Update intervals ─────────────────────────────────────────
#define INTERVAL_2S     2000
#define INTERVAL_20S    20000
#define INTERVAL_2M     120000
#define INTERVAL_DEFAULT INTERVAL_20S

// ── F&G timer: fires every minute, updates on :00/:15/:30/:45
#define FG_TIMER_MS     60000

// ── Flash duration for Extreme Fear / Extreme Greed ──────────
#define FG_BLINK_MS     2000

// ── WM_TIMER IDs ─────────────────────────────────────────────
#define TIMER_UPDATE    1
#define TIMER_FG        2
#define TIMER_BLINK     3

// ── Custom window messages ────────────────────────────────────
#define WMU_DATA_READY  (WM_USER + 1)
#define WMU_DATA_FAIL   (WM_USER + 2)

// ── Menu command IDs ─────────────────────────────────────────
#define IDM_PAIR_BASE              3000    // 3000..3099 dynamic pair IDs
#define IDM_FREQ_2S                2011
#define IDM_FREQ_20S               2012
#define IDM_FREQ_2M                2013
#define IDM_PROV_BINANCE           2021
#define IDM_PROV_BYBIT             2022
#define IDM_PROV_OKX               2023
#define IDM_FUNDING_CURRENT        2031
#define IDM_FUNDING_TIME_WEIGHTED  2032
#define IDM_FUNDING_CUMULATIVE     2033

// ── COM CLSID ────────────────────────────────────────────────
// {C1D2E3F4-A5B6-7C8D-9E0F-A1B2C3D4E5F6}
static const CLSID CLSID_Widget =
{ 0xc1d2e3f4, 0xa5b6, 0x7c8d,
  { 0x9e, 0x0f, 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6 } };

// ── Enumerations ─────────────────────────────────────────────
enum class Provider  { Binance, Bybit, OKX };
enum class FundingRateMode { Current, TimeWeighted, Cumulative };

// ── Pair config (one entry from .cfg file) ───────────────────
struct PairCfg
{
    std::wstring label;      // display ticker: "BTC", "ETH", "HYPE"
    std::wstring binSym;     // Binance: "BTCUSDT"
    std::wstring bybitSym;   // Bybit:   "BTCUSDT"
    std::wstring okxSym;     // OKX:     "BTC-USDT-SWAP"
};

// ── Shared price block (BTC or selected pair) ────────────────
struct PriceBlock
{
    double   price     = 0.0;
    double   change24h = 0.0;
    bool     priceOk   = false;
    bool     changeOk  = false;
};

extern HINSTANCE g_hInst;
extern LONG      g_cRef;

// ── Main COM class ───────────────────────────────────────────
class CWidget : public IDeskBand2,
                public IObjectWithSite,
                public IPersistStream
{
public:
    CWidget();
    virtual ~CWidget();

    // IUnknown
    STDMETHOD_(ULONG, AddRef)()  override;
    STDMETHOD_(ULONG, Release)() override;
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;

    // IOleWindow
    STDMETHOD(GetWindow)(HWND* phwnd) override;
    STDMETHOD(ContextSensitiveHelp)(BOOL) override { return E_NOTIMPL; }

    // IDockingWindow
    STDMETHOD(ShowDW)(BOOL bShow) override;
    STDMETHOD(CloseDW)(DWORD) override;
    STDMETHOD(ResizeBorderDW)(const RECT*, IUnknown*, BOOL) override { return E_NOTIMPL; }

    // IDeskBand
    STDMETHOD(GetBandInfo)(DWORD dwID, DWORD dwMode, DESKBANDINFO* pdbi) override;

    // IDeskBand2
    STDMETHOD(CanRenderComposited)(BOOL* p) override;
    STDMETHOD(SetCompositionState)(BOOL b) override;
    STDMETHOD(GetCompositionState)(BOOL* p) override;

    // IObjectWithSite
    STDMETHOD(SetSite)(IUnknown* pSite) override;
    STDMETHOD(GetSite)(REFIID riid, void** ppv) override;

    // IPersist
    STDMETHOD(GetClassID)(CLSID* p) override;

    // IPersistStream (stubs)
    STDMETHOD(IsDirty)()                   override { return S_FALSE; }
    STDMETHOD(Load)(IStream*)              override { return S_OK; }
    STDMETHOD(Save)(IStream*, BOOL)        override { return S_OK; }
    STDMETHOD(GetSizeMax)(ULARGE_INTEGER*) override { return E_NOTIMPL; }

private:
    // Window
    void CreateWnd(HWND parent);
    void DestroyWnd();
    void BuildFont();
    int  CalcWidth();

    // WndProc
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l);
    LRESULT OnMsg(HWND h, UINT m, WPARAM w, LPARAM l);
    void    OnCmd(WORD id);
    void    OnPaint(HWND h);

    // Menu
    void    ShowMenu(HWND h);

    // Tooltip (single TOOLTIPS_CLASS window, TTF_SUBCLASS)
    void    CreateTooltip();

    // Config
    void    LoadCfg();
    std::wstring CfgPath() const;

    // Timers
    void StartUpdateTimer();
    void StopUpdateTimer();
    void ScheduleFGTimer();   // aligns to next :00/:15/:30/:45

    // Fetch (background thread)
    void    Fetch();
    void    FetchThread();
    bool    FetchPrice(const PairCfg& pc, double& out);
    bool    FetchBinance(const std::wstring& sym, double& out);
    bool    FetchBybit  (const std::wstring& sym, double& out);
    bool    FetchOKX    (const std::wstring& sym, double& out);
    bool    FetchKlines (const std::wstring& sym, double& out);
    bool    FetchFG     ();
    bool    FetchFundingRate(const std::wstring& sym, double& out);

    // HTTP
    bool    HttpGet (const std::wstring& host, const std::wstring& path,
                     std::string& body, const std::wstring& header = L"");

    // JSON
    static bool GetJsonDouble(const std::string& j, const std::string& key, double& v);
 
    // Settings
    void LoadSettings();
    void SaveSettings();

    // Helpers
    static std::wstring Trim(const std::wstring& s);
    static std::wstring FmtPrice(double p);
    static std::wstring FmtChange(double c, bool valid);
    std::wstring GetSymbol(const PairCfg& pc) const; // label without "USD" suffix

private:
    // COM / window
    LONG      m_cRef;
    IUnknown* m_pSite;
    HWND      m_hwnd;
    HWND      m_hwndParent;
    HWND      m_hwndTip;
    DWORD     m_bandID;
    BOOL      m_composited;

    // Settings (persisted)
    int        m_pairIdx;
    DWORD      m_intervalMs;
    Provider   m_provider;
    FundingRateMode m_fundingMode;

    // Pairs loaded from .cfg
    std::vector<PairCfg> m_pairs;

    // Market data
    std::mutex  m_mtx;
    PriceBlock  m_btc;
    PriceBlock  m_sel;
    bool        m_loading;   // show "Loading..."

    // F&G
    int   m_fg;           // -1 = not loaded
    bool  m_fgOk;
    bool  m_fgBlink;

    // Funding rate
    double m_frBtc;
    bool   m_frBtcOk;
    double m_frSel;
    bool   m_frSelOk;      // toggled by TIMER_BLINK for Extreme Fear/Greed

    // Timers
    UINT_PTR  m_updateTimer;
    UINT_PTR  m_fgTimer;
    UINT_PTR  m_blinkTimer;

    // Fetch thread
    std::thread       m_thread;
    std::atomic<bool> m_fetching;

    // GDI
    HFONT m_font;
};