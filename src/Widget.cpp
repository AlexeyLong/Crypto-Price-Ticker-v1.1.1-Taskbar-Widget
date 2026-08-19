// Widget.cpp - Crypto Price Ticker v1.1.1 Release
#include "pch.h"
#include "Widget.h"

// ── Colors ───────────────────────────────────────────────────
static const COLORREF CLR_WHITE   = RGB(255, 255, 255);
static const COLORREF CLR_BLACK   = RGB(0,   0,   0);
static const COLORREF CLR_GRAY    = RGB(128, 128, 128);
static const COLORREF CLR_SEP     = RGB(80,  80,  80);
static const COLORREF CLR_UP      = RGB(50,  220, 50);
static const COLORREF CLR_DOWN    = RGB(255, 61,  48);
static const COLORREF CLR_BG      = RGB(0,   0,   0);

// ── F&G colors ───────────────────────────────────────────────
static COLORREF FgColor(int v)
{
    if (v <= 17)  return RGB(255, 0,   0);
    if (v <= 24)  return RGB(255, 165, 0);
    if (v <= 39)  return RGB(255, 255, 0);
    if (v <= 59)  return RGB(0,   255, 0);
    return              RGB(0,   180, 0);
}
static bool FgBlinks(int v) { return v <= 15 || v >= 65; }

// ─────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────
CWidget::CWidget()
    : m_cRef(1), m_pSite(nullptr), m_hwnd(nullptr)
    , m_hwndParent(nullptr), m_hwndTip(nullptr)
    , m_bandID(0), m_composited(FALSE)
    , m_pairIdx(0), m_intervalMs(INTERVAL_DEFAULT)
    , m_provider(Provider::Binance), m_fundingMode(FundingRateMode::Current)
    , m_loading(true)
    , m_fg(-1), m_fgOk(false), m_fgBlink(true)
    , m_frBtc(0.0), m_frBtcOk(false)
    , m_frSel(0.0), m_frSelOk(false)
    , m_updateTimer(0), m_fgTimer(0), m_blinkTimer(0)
    , m_fetching(false), m_font(nullptr)
{
    InterlockedIncrement(&g_cRef);
    LoadCfg();
    LoadSettings();
    if (m_pairIdx >= (int)m_pairs.size()) m_pairIdx = 0;
    BuildFont();
}

CWidget::~CWidget()
{
    StopUpdateTimer();
    if (m_hwnd && m_fgTimer)    { KillTimer(m_hwnd, m_fgTimer);    m_fgTimer    = 0; }
    if (m_hwnd && m_blinkTimer) { KillTimer(m_hwnd, m_blinkTimer); m_blinkTimer = 0; }
    if (m_thread.joinable()) m_thread.join();
    DestroyWnd();
    if (m_pSite) { m_pSite->Release(); m_pSite = nullptr; }
    if (m_font)  { DeleteObject(m_font); m_font = nullptr; }
    InterlockedDecrement(&g_cRef);
}

// ─────────────────────────────────────────────────────────────
// IUnknown
// ─────────────────────────────────────────────────────────────
ULONG CWidget::AddRef()  { return InterlockedIncrement(&m_cRef); }
ULONG CWidget::Release()
{
    ULONG c = InterlockedDecrement(&m_cRef);
    if (!c) delete this;
    return c;
}
HRESULT CWidget::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown)      ||
        IsEqualIID(riid, IID_IOleWindow)     ||
        IsEqualIID(riid, IID_IDockingWindow) ||
        IsEqualIID(riid, IID_IDeskBand)      ||
        IsEqualIID(riid, IID_IDeskBand2))
        *ppv = static_cast<IDeskBand2*>(this);
    else if (IsEqualIID(riid, IID_IObjectWithSite))
        *ppv = static_cast<IObjectWithSite*>(this);
    else if (IsEqualIID(riid, IID_IPersist) ||
             IsEqualIID(riid, IID_IPersistStream))
        *ppv = static_cast<IPersistStream*>(this);
    else return E_NOINTERFACE;
    AddRef();
    return S_OK;
}

// ─────────────────────────────────────────────────────────────
// IOleWindow / IDockingWindow
// ─────────────────────────────────────────────────────────────
HRESULT CWidget::GetWindow(HWND* p) { if (!p) return E_POINTER; *p = m_hwnd; return m_hwnd ? S_OK : E_FAIL; }
HRESULT CWidget::ShowDW(BOOL b)     { if (m_hwnd) ShowWindow(m_hwnd, b ? SW_SHOW : SW_HIDE); return S_OK; }
HRESULT CWidget::CloseDW(DWORD)     { ShowDW(FALSE); DestroyWnd(); return S_OK; }

// ─────────────────────────────────────────────────────────────
// IDeskBand
// ─────────────────────────────────────────────────────────────
HRESULT CWidget::GetBandInfo(DWORD dwID, DWORD, DESKBANDINFO* p)
{
    if (!p) return E_INVALIDARG;
    m_bandID = dwID;
    int w = CalcWidth();
    if (w < BAND_MIN_W) w = BAND_MIN_W;
    if (w > BAND_MAX_W) w = BAND_MAX_W;
    if (p->dwMask & DBIM_MINSIZE)   { p->ptMinSize.x = w; p->ptMinSize.y = BAND_HEIGHT; }
    if (p->dwMask & DBIM_MAXSIZE)   { p->ptMaxSize.x = w; p->ptMaxSize.y = BAND_HEIGHT; }
    if (p->dwMask & DBIM_ACTUAL)    { p->ptActual.x  = w; p->ptActual.y  = BAND_HEIGHT; }
    if (p->dwMask & DBIM_TITLE)     { p->wszTitle[0] = L'\0'; }
    if (p->dwMask & DBIM_MODEFLAGS) { p->dwModeFlags = DBIMF_NORMAL | DBIMF_VARIABLEHEIGHT; }
    if (p->dwMask & DBIM_BKCOLOR)   { p->dwMask &= ~DBIM_BKCOLOR; }
    return S_OK;
}

// ─────────────────────────────────────────────────────────────
// IDeskBand2
// ─────────────────────────────────────────────────────────────
HRESULT CWidget::CanRenderComposited(BOOL* p) { if (p) *p = TRUE; return S_OK; }
HRESULT CWidget::SetCompositionState(BOOL b)  { m_composited = b; if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE); return S_OK; }
HRESULT CWidget::GetCompositionState(BOOL* p) { if (p) *p = m_composited; return S_OK; }

// ─────────────────────────────────────────────────────────────
// IObjectWithSite
// ─────────────────────────────────────────────────────────────
HRESULT CWidget::SetSite(IUnknown* pSite)
{
    if (m_pSite) { m_pSite->Release(); m_pSite = nullptr; }
    DestroyWnd();
    StopUpdateTimer();
    if (!pSite) return S_OK;

    IOleWindow* pOW = nullptr;
    if (FAILED(pSite->QueryInterface(IID_IOleWindow, (void**)&pOW))) return E_FAIL;
    pOW->GetWindow(&m_hwndParent);
    pOW->Release();
    if (!m_hwndParent) return E_FAIL;

    m_pSite = pSite;
    m_pSite->AddRef();

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = CWidget::WndProc;
    wc.hInstance     = g_hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = APP_WND_CLASS;
    RegisterClassExW(&wc);

    CreateWnd(m_hwndParent);
    CreateTooltip();
    StartUpdateTimer();
    ScheduleFGTimer();
    Fetch();
    return S_OK;
}
HRESULT CWidget::GetSite(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;
    if (!m_pSite) { *ppv = nullptr; return E_FAIL; }
    return m_pSite->QueryInterface(riid, ppv);
}
HRESULT CWidget::GetClassID(CLSID* p) { if (!p) return E_POINTER; *p = CLSID_Widget; return S_OK; }

// ─────────────────────────────────────────────────────────────
// Window helpers
// ─────────────────────────────────────────────────────────────
void CWidget::CreateWnd(HWND parent)
{
    m_hwnd = CreateWindowExW(WS_EX_TRANSPARENT, APP_WND_CLASS, nullptr,
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 80, BAND_HEIGHT, parent, nullptr, g_hInst, this);
}
void CWidget::DestroyWnd()
{
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}
void CWidget::BuildFont()
{
    if (m_font) { DeleteObject(m_font); m_font = nullptr; }
    LOGFONTW lf = {};
    HDC hdc = GetDC(nullptr);
    lf.lfHeight  = -MulDiv(FONT_SIZE_PT, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(nullptr, hdc);
    lf.lfWeight  = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    m_font = CreateFontIndirectW(&lf);
}

void CWidget::CreateTooltip()
{
    if (!m_hwnd) return;

    // Create the system tooltip window using Unicode class name
    m_hwndTip = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        0, 0, 0, 0, m_hwnd, nullptr, g_hInst, nullptr);

    if (!m_hwndTip) return;

    TOOLINFOW ti = {};
    ti.cbSize = sizeof(ti);

    // TTF_IDISHWND indicates that uId is the window handle and the tooltip covers the entire window area
    ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
    ti.hwnd = m_hwnd;
    ti.uId = (UINT_PTR)m_hwnd;

    // Safely cast the static string literal to LPWSTR
    ti.lpszText = const_cast<LPWSTR>(APP_NAME L" " APP_VERSION);

    GetClientRect(m_hwnd, &ti.rect);

    SendMessageW(m_hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

int CWidget::CalcWidth()
{
    std::wstring s;
    if (m_loading && !m_btc.priceOk && !m_sel.priceOk)
        s = L"Loading...";
    else
    {
        s += L"BTC $000,000.00 +00.00%  " +
            (!m_pairs.empty() ? m_pairs[m_pairIdx].label : L"") +
            L" $000,000.00 +00.00%  00";
    }
    HDC   hdc  = GetDC(nullptr);
    HFONT hold = (HFONT)SelectObject(hdc, m_font);
    SIZE  sz   = {};
    GetTextExtentPoint32W(hdc, s.c_str(), (int)s.size(), &sz);
    SelectObject(hdc, hold);
    ReleaseDC(nullptr, hdc);
    int w = sz.cx + BAND_PAD;
    if (w > BAND_MAX_W) w = BAND_MAX_W;
    return w;
}

// ─────────────────────────────────────────────────────────────
// WndProc
// ─────────────────────────────────────────────────────────────
LRESULT CALLBACK CWidget::WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    CWidget* p = nullptr;
    if (m == WM_NCCREATE)
    {
        p = reinterpret_cast<CWidget*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p);
    }
    else
        p = reinterpret_cast<CWidget*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    return p ? p->OnMsg(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT CWidget::OnMsg(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:      OnPaint(h); return 0;

    case WM_TIMER:
        if ((UINT_PTR)w == m_updateTimer)
        {
            Fetch();
        }
        else if ((UINT_PTR)w == m_fgTimer)
        {
            // F&G timer fires every minute; only fetch on :00/:15/:30/:45
            SYSTEMTIME st;
            GetSystemTime(&st);
            int min = st.wMinute % 15;
            if (min == 0)
            {
                if (m_thread.joinable()) m_thread.join();
                m_fetching = true;
                m_thread = std::thread([this]() {
                    FetchFG();
                    double fb=0,fs=0;
                    bool ob=FetchFundingRate(L"BTCUSDT",fb);
                    bool os=(!m_pairs.empty())?FetchFundingRate(m_pairs[m_pairIdx].binSym,fs):false;
                    { std::lock_guard<std::mutex> lk(m_mtx); if(ob){m_frBtc=fb;m_frBtcOk=true;} if(os){m_frSel=fs;m_frSelOk=true;} }
                    m_fetching = false;
                    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
                });
            }
        }
        else if ((UINT_PTR)w == m_blinkTimer)
        {
            m_fgBlink = !m_fgBlink;
            if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;

    case WMU_DATA_READY:
    {
        int bw = CalcWidth();
        SetWindowPos(h, nullptr, 0, 0, bw, BAND_HEIGHT,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SendMessageW(GetParent(h), WM_SIZE, 0, 0);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WMU_DATA_FAIL: InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_RBUTTONUP: ShowMenu(h); return 0;
    case WM_LBUTTONDBLCLK: Fetch(); return 0;
    case WM_COMMAND: OnCmd(LOWORD(w)); return 0;
    case WM_DESTROY: StopUpdateTimer(); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void CWidget::OnCmd(WORD id)
{
    if (id >= IDM_PAIR_BASE && id < (WORD)(IDM_PAIR_BASE + (int)m_pairs.size()))
    {
        int ni = id - IDM_PAIR_BASE;
        if (ni != m_pairIdx)
        {
            m_pairIdx = ni;
            m_sel = PriceBlock{};
            m_loading = true;
            Fetch();
            SaveSettings();
        }
        return;
    }
    switch (id)
    {
    case IDM_FREQ_2S:  m_intervalMs = INTERVAL_2S;  StopUpdateTimer(); StartUpdateTimer(); SaveSettings(); break;
    case IDM_FREQ_20S: m_intervalMs = INTERVAL_20S; StopUpdateTimer(); StartUpdateTimer(); SaveSettings(); break;
    case IDM_FREQ_2M:  m_intervalMs = INTERVAL_2M;  StopUpdateTimer(); StartUpdateTimer(); SaveSettings(); break;
    case IDM_PROV_BINANCE: m_provider = Provider::Binance; m_btc = {}; m_sel = {}; Fetch(); SaveSettings(); break;
    case IDM_PROV_BYBIT:   m_provider = Provider::Bybit;   m_btc = {}; m_sel = {}; Fetch(); SaveSettings(); break;
    case IDM_PROV_OKX:     m_provider = Provider::OKX;     m_btc = {}; m_sel = {}; Fetch(); SaveSettings(); break;
    case IDM_FUNDING_CURRENT: m_fundingMode = FundingRateMode::Current; SaveSettings(); if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE); break;
    case IDM_FUNDING_TIME_WEIGHTED: m_fundingMode = FundingRateMode::TimeWeighted; SaveSettings(); if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE); break;
    case IDM_FUNDING_CUMULATIVE: m_fundingMode = FundingRateMode::Cumulative; SaveSettings(); if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE); break;
    }
}

// ─────────────────────────────────────────────────────────────
// Paint
// ─────────────────────────────────────────────────────────────
void CWidget::OnPaint(HWND h)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    int w = rc.right, ht = rc.bottom;

    HDC     hdcM = CreateCompatibleDC(hdc);
    HBITMAP hBmp = CreateCompatibleBitmap(hdc, w, ht);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcM, hBmp);

    // Background
    HBRUSH hbr = CreateSolidBrush(CLR_BG);
    FillRect(hdcM, &rc, hbr);
    DeleteObject(hbr);

    SelectObject(hdcM, m_font);
    SetBkMode(hdcM, TRANSPARENT);

    // Loading state
    if (m_loading && !m_btc.priceOk && !m_sel.priceOk)
    {
        SetTextColor(hdcM, CLR_WHITE);
        DrawTextW(hdcM, L"Loading...", -1, &rc,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        BitBlt(hdc, 0, 0, w, ht, hdcM, 0, 0, SRCCOPY);
        SelectObject(hdcM, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcM);
        EndPaint(h, &ps);
        return;
    }

    // Build segments as (text, color) pairs
    struct Seg { std::wstring text; COLORREF clr; };
    std::vector<Seg> segs;

    // ── BTC block ──
    if (m_btc.priceOk)
    {
        segs.push_back({ L"BTC $" + FmtPrice(m_btc.price), CLR_WHITE });
        std::wstring chg = FmtChange(m_btc.change24h, m_btc.changeOk);
        COLORREF cc = CLR_WHITE;
        if (!m_btc.changeOk)          cc = CLR_GRAY;
        else if (m_btc.change24h > 0) cc = CLR_UP;
        else if (m_btc.change24h < 0) cc = CLR_DOWN;
        segs.push_back({ L" " + chg, cc });
    }
    else
        segs.push_back({ L"BTC n/a", CLR_GRAY });
    
    // BTC funding block
    if (m_frBtcOk)
    {
        wchar_t buf[32];
        double truncated_pct = (double)((long long)(m_frBtc * 1000000.0)) / 10000.0; // Truncates the value to 4 decimal places instead of rounding it
        swprintf_s(buf, L" %+0.4f", truncated_pct);

        COLORREF fc = CLR_WHITE;
        if (m_frBtc > 0)      fc = CLR_UP;
        else if (m_frBtc < 0) fc = CLR_DOWN;

        segs.push_back({ buf, fc });
    }
    else
    {
        segs.push_back({ L" n/a", CLR_GRAY });
    }

    // Separator
    segs.push_back({ L"  ", CLR_SEP });

    // ── Selected pair block ──
    std::wstring sym = m_pairs.empty() ? L"?" : GetSymbol(m_pairs[m_pairIdx]);
    if (m_sel.priceOk)
    {
        segs.push_back({ sym + L" $" + FmtPrice(m_sel.price), CLR_WHITE });
        std::wstring chg = FmtChange(m_sel.change24h, m_sel.changeOk);
        COLORREF cc = CLR_WHITE;
        if (!m_sel.changeOk)          cc = CLR_GRAY;
        else if (m_sel.change24h > 0) cc = CLR_UP;
        else if (m_sel.change24h < 0) cc = CLR_DOWN;
        segs.push_back({ L" " + chg, cc });
    }
    else
        segs.push_back({ sym + L" n/a", CLR_GRAY });
   
    // Selected pair funding block
    if (m_frSelOk)
    {
        wchar_t buf[32];
        double truncated_pct = (double)((long long)(m_frSel * 1000000.0)) / 10000.0; // Truncates the value to 4 decimal places instead of rounding it
        swprintf_s(buf, L" %+0.4f", truncated_pct);

        COLORREF fc = CLR_WHITE;
        if (m_frSel > 0)      fc = CLR_UP;
        else if (m_frSel < 0) fc = CLR_DOWN;

        segs.push_back({ buf, fc });
    }
    else
    {
        segs.push_back({ L" n/a", CLR_GRAY });
    }

    // ── F&G ──
    if (m_fgOk && m_fg >= 0)
    {
        wchar_t buf[8];
        swprintf_s(buf, L"  %d", m_fg);

        COLORREF color = FgColor(m_fg);

        if (FgBlinks(m_fg) && !m_fgBlink)
            color = CLR_BLACK;              // Black

        segs.push_back({ buf, color });
    }

    // Measure total width
    auto measure = [&](const std::wstring& s) -> int {
        SIZE sz = {};
        GetTextExtentPoint32W(hdcM, s.c_str(), (int)s.size(), &sz);
        return sz.cx;
    };

    int total = 0;
    for (auto& sg : segs) total += measure(sg.text);

    // Center vertically
    SIZE szH = {};
    GetTextExtentPoint32W(hdcM, L"A", 1, &szH);
    int yTop = (ht - szH.cy) / 2;

    // Draw segments left to right, centered
    //int x = (w - total) / 2;
    // Press it to the right edge of the tray (minus 4 pixels of padding for beauty)
    int x = w - total - 4;

    for (auto& sg : segs)
    {
        int sw = measure(sg.text);
        RECT rSeg = { x, yTop, x + sw, yTop + szH.cy };
        SetTextColor(hdcM, sg.clr);
        DrawTextW(hdcM, sg.text.c_str(), -1, &rSeg,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        x += sw;
    }

    BitBlt(hdc, 0, 0, w, ht, hdcM, 0, 0, SRCCOPY);
    SelectObject(hdcM, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcM);
    EndPaint(h, &ps);
}

// ─────────────────────────────────────────────────────────────
// Context menu
// ─────────────────────────────────────────────────────────────
void CWidget::ShowMenu(HWND h)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    AppendMenuW(hMenu, MF_STRING | MF_GRAYED | MF_DISABLED, 0, APP_NAME L" " APP_VERSION);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Update Frequency
    HMENU hFreq = CreatePopupMenu();
    AppendMenuW(hFreq, MF_STRING | (m_intervalMs==INTERVAL_2S  ? MF_CHECKED:0), IDM_FREQ_2S,  L"2 seconds");
    AppendMenuW(hFreq, MF_STRING | (m_intervalMs==INTERVAL_20S ? MF_CHECKED:0), IDM_FREQ_20S, L"20 seconds");
    AppendMenuW(hFreq, MF_STRING | (m_intervalMs==INTERVAL_2M  ? MF_CHECKED:0), IDM_FREQ_2M,  L"2 minutes");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFreq, L"Update Frequency");

    // Cryptocurrencies
    HMENU hPairs = CreatePopupMenu();
    for (int i = 0; i < (int)m_pairs.size(); i++)
        AppendMenuW(hPairs, MF_STRING | (m_pairIdx==i ? MF_CHECKED:0),
                    (UINT_PTR)(IDM_PAIR_BASE + i), m_pairs[i].label.c_str());
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hPairs, L"Cryptocurrencies");

    // Market Data Provider
    HMENU hProv = CreatePopupMenu();
    AppendMenuW(hProv, MF_STRING | (m_provider==Provider::Binance ? MF_CHECKED:0), IDM_PROV_BINANCE, L"Binance Futures");
    AppendMenuW(hProv, MF_STRING | (m_provider==Provider::Bybit   ? MF_CHECKED:0), IDM_PROV_BYBIT,   L"Bybit Linear Perpetual");
    AppendMenuW(hProv, MF_STRING | (m_provider==Provider::OKX     ? MF_CHECKED:0), IDM_PROV_OKX,     L"OKX Perpetual Swap");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hProv, L"Market Data Provider");

    // Funding Rate
    HMENU hFunding = CreatePopupMenu();
    AppendMenuW(hFunding, MF_STRING | (m_fundingMode == FundingRateMode::Current ? MF_CHECKED : 0), IDM_FUNDING_CURRENT, L"Current Funding Rate");
    AppendMenuW(hFunding, MF_STRING | (m_fundingMode == FundingRateMode::TimeWeighted ? MF_CHECKED : 0), IDM_FUNDING_TIME_WEIGHTED, L"Time-Weighted Funding Rate");
    AppendMenuW(hFunding, MF_STRING | (m_fundingMode == FundingRateMode::Cumulative ? MF_CHECKED : 0), IDM_FUNDING_CUMULATIVE, L"24h Cumulative Funding Rate");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFunding, L"Funding Rate");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(h);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_VERPOSANIMATION | TPM_RECURSE, pt.x, pt.y, 0, h, nullptr);

    DestroyMenu(hPairs);
    DestroyMenu(hFreq);
    DestroyMenu(hProv);
    DestroyMenu(hFunding);
    DestroyMenu(hMenu);
}

// ─────────────────────────────────────────────────────────────
// Timers
// ─────────────────────────────────────────────────────────────
void CWidget::StartUpdateTimer()
{
    if (m_hwnd) m_updateTimer = SetTimer(m_hwnd, TIMER_UPDATE, m_intervalMs, nullptr);
}
void CWidget::StopUpdateTimer()
{
    if (m_hwnd && m_updateTimer) { KillTimer(m_hwnd, m_updateTimer); m_updateTimer = 0; }
}
void CWidget::ScheduleFGTimer()
{
    if (!m_hwnd) return;
    // Fire every minute; OnMsg checks if minute % 15 == 0
    m_fgTimer = SetTimer(m_hwnd, TIMER_FG, FG_TIMER_MS, nullptr);
}

// ─────────────────────────────────────────────────────────────
// Fetch orchestration
// ─────────────────────────────────────────────────────────────
void CWidget::Fetch()
{
    if (m_fetching) return;
    if (m_thread.joinable()) m_thread.join();
    m_fetching = true;
    m_thread = std::thread([this]() { FetchThread(); });
}

void CWidget::FetchThread()
{
    PriceBlock btcNew, selNew;
    bool okBtc = false, okSel = false;

    // BTC price
    if (!m_pairs.empty())
    {
        // BTC is always BTCUSDT/BTC-USDT-SWAP
        PairCfg btcCfg;
        btcCfg.binSym   = L"BTCUSDT";
        btcCfg.bybitSym = L"BTCUSDT";
        btcCfg.okxSym   = L"BTC-USDT-SWAP";
        btcCfg.label    = L"BTC";
        okBtc = FetchPrice(btcCfg, btcNew.price);
        btcNew.priceOk = okBtc;
        if (okBtc)
            btcNew.changeOk = FetchKlines(btcCfg.binSym, btcNew.change24h);

        // Selected pair price
        const PairCfg& pc = m_pairs[m_pairIdx];
        okSel = FetchPrice(pc, selNew.price);
        selNew.priceOk = okSel;
        if (okSel)
            selNew.changeOk = FetchKlines(pc.binSym, selNew.change24h);
    }

    // F&G on first run
    if (!m_fgOk) FetchFG();

    // Funding rate
    double frBtc=0.0, frSel=0.0;
    bool okFrBtc=FetchFundingRate(L"BTCUSDT",frBtc);
    bool okFrSel=(!m_pairs.empty())?FetchFundingRate(m_pairs[m_pairIdx].binSym,frSel):false;

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_loading = false;

        if (okBtc)
        {
            m_btc = btcNew;
        }

        if (okSel)
        {
            m_sel = selNew;
        }

        if (okFrBtc) { m_frBtc=frBtc; m_frBtcOk=true; }
        if (okFrSel) { m_frSel=frSel; m_frSelOk=true; }

        // Manage blink timer for F&G Extreme values
        if (m_fgOk && FgBlinks(m_fg))
        {
            if (!m_blinkTimer && m_hwnd)
                m_blinkTimer = SetTimer(m_hwnd, TIMER_BLINK, FG_BLINK_MS, nullptr);
        }
        else
        {
            if (m_blinkTimer && m_hwnd) { KillTimer(m_hwnd, m_blinkTimer); m_blinkTimer = 0; }
            m_fgBlink = true;
        }
    }

    m_fetching = false;
    if (m_hwnd)
        PostMessageW(m_hwnd, (okBtc || okSel) ? WMU_DATA_READY : WMU_DATA_FAIL, 0, 0);
}

// ─────────────────────────────────────────────────────────────
// Price fetch dispatcher
// ─────────────────────────────────────────────────────────────
bool CWidget::FetchPrice(const PairCfg& pc, double& out)
{
    switch (m_provider)
    {
    case Provider::Binance: return FetchBinance(pc.binSym,   out);
    case Provider::Bybit:   return FetchBybit  (pc.bybitSym, out);
    case Provider::OKX:     return FetchOKX    (pc.okxSym,   out);
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
// Binance Futures: GET /fapi/v1/ticker/price?symbol=BTCUSDT
// JSON: {"price":"65000.5"}
// ─────────────────────────────────────────────────────────────
bool CWidget::FetchBinance(const std::wstring& sym, double& out)
{
    std::string body;
    if (!HttpGet(L"fapi.binance.com", L"/fapi/v1/ticker/price?symbol=" + sym, body))
        return false;
    return GetJsonDouble(body, "\"price\"", out);
}

// ─────────────────────────────────────────────────────────────
// Bybit Linear: GET /v5/market/tickers?category=linear&symbol=BTCUSDT
// JSON: {"result":{"list":[{"lastPrice":"65000.5"}]}}
// ─────────────────────────────────────────────────────────────
bool CWidget::FetchBybit(const std::wstring& sym, double& out)
{
    std::string body;
    std::wstring path = L"/v5/market/tickers?category=linear&symbol=" + sym;
    if (!HttpGet(L"api.bybit.com", path, body)) return false;

    // Navigate into result.list[0].lastPrice
    // Find "list" then first "[" then "{"  then "lastPrice"
    size_t listPos = body.find("\"list\"");
    if (listPos == std::string::npos) return false;
    size_t arrOpen = body.find('[', listPos);
    if (arrOpen == std::string::npos) return false;
    size_t objOpen = body.find('{', arrOpen);
    if (objOpen == std::string::npos) return false;
    // Parse lastPrice from within the first object
    std::string obj = body.substr(objOpen);
    return GetJsonDouble(obj, "\"lastPrice\"", out);
}

// ─────────────────────────────────────────────────────────────
// OKX Perpetual Swap: GET /api/v5/market/ticker?instId=BTC-USDT-SWAP
// JSON: {"data":[{"last":"65000.5"}]}
// ─────────────────────────────────────────────────────────────
bool CWidget::FetchOKX(const std::wstring& sym, double& out)
{
    std::string body;
    if (!HttpGet(L"www.okx.com", L"/api/v5/market/ticker?instId=" + sym, body))
        return false;
    return GetJsonDouble(body, "\"last\"", out);
}

// ─────────────────────────────────────────────────────────────
// Binance Spot Klines: change% from UTC daily open
// GET /api/v3/klines?symbol=BTCUSDT&interval=1d&limit=1
// ─────────────────────────────────────────────────────────────
bool CWidget::FetchKlines(const std::wstring& sym, double& out)
{
    std::string body;
    std::wstring path = L"/api/v3/klines?symbol=" + sym + L"&interval=1d&limit=1";
    if (!HttpGet(L"api.binance.com", path, body)) return false;

    // [[openTime,"openPrice","high","low","closePrice",...]]
    size_t outer = body.find('[');
    if (outer == std::string::npos) return false;
    size_t inner = body.find('[', outer + 1);
    if (inner == std::string::npos) return false;
    inner++;

    // Skip openTime (element 0)
    size_t c1 = body.find(',', inner);
    if (c1 == std::string::npos) return false;
    c1++;
    while (c1 < body.size() && (body[c1]==' ' || body[c1]=='"')) c1++;

    double openPrice = 0.0;
    try { openPrice = std::stod(body.substr(c1)); }
    catch (...) { return false; }
    if (openPrice <= 0.0) return false;

    // Skip to element 4 (closePrice): 3 more commas
    size_t pos = c1;
    for (int i = 0; i < 3; i++)
    {
        pos = body.find(',', pos);
        if (pos == std::string::npos) return false;
        pos++;
    }
    while (pos < body.size() && (body[pos]==' ' || body[pos]=='"')) pos++;

    double closePrice = 0.0;
    try { closePrice = std::stod(body.substr(pos)); }
    catch (...) { return false; }
    if (closePrice <= 0.0) return false;

    out = ((closePrice - openPrice) / openPrice) * 100.0;
    return true;
}

// ─────────────────────────────────────────────────────────────
// Binance Futures funding rate
// GET /fapi/v1/fundingRate?symbol=BTCUSDT&limit=3
// Current: uses the latest single funding rate from the response
// TimeWeighted: linear TWMA average where newer rates have higher weight
// Cumulative: rolling sum of the last 3 sessions (24-hour aggregate)
// ─────────────────────────────────────────────────────────────
bool CWidget::FetchFundingRate(const std::wstring& sym, double& out)
{
    std::string body;
    if (!HttpGet( L"fapi.binance.com", L"/fapi/v1/fundingRate?symbol=" + sym + L"&limit=3", body)) return false;
    const std::string key = "\"fundingRate\"";
    size_t pos = 0;
    double latest = 0.0;        // Latest funding rate
    double weightedSum = 0.0;   // Sum of values multiplied by their weights
    double simpleSum = 0.0;     // Cumulative sum of all funding rates (FR1 + FR2 + ... + FR_count)
    int count = 0;
    while ((pos = body.find(key, pos)) != std::string::npos)
    {
        pos = body.find(':', pos);
        if (pos == std::string::npos)
            break;
        pos++;
        while (pos < body.size() &&
            (body[pos] == ' ' || body[pos] == '"' || body[pos] == '\t'))
            pos++;
        try
        {
            double value = std::stod(body.substr(pos));
            count++;

            // 1. Current: Keep the newest funding rate for Current mode.
            latest = value;

            // 2. TimeWeighted: Binance returns data from oldest to newest. Higher weight is assigned to newer funding rates.
            weightedSum += value * count;

            // 3. Cumulative: Accumulate values for Cumulative mode.
            simpleSum += value;
        }
        catch (...)
        {
        }
        pos++;
    }
    if (count == 0)
        return false;

    // 1. Current mode
    if (m_fundingMode == FundingRateMode::Current)
    {
        // Latest funding rate only
        out = latest;
    }
    // 2. TimeWeighted mode
    else if (m_fundingMode == FundingRateMode::TimeWeighted)
    {
        // Weighted average: 1 + 2 + 3 + ... + count
        double weightSum = count * (count + 1) / 2.0;

        out = weightedSum / weightSum;
    }
        // 3. Cumulative mode
    else
    {
        // Cumulative sum of all fetched funding rates: FR1 + FR2 + ... + FR_count
        out = simpleSum;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────
// CoinMarketCap Fear & Greed
// GET /v3/fear-and-greed/latest  (API key in header)
// ─────────────────────────────────────────────────────────────
bool CWidget::FetchFG()
{
    std::string body;
    if (!HttpGet(L"pro-api.coinmarketcap.com", L"/v3/fear-and-greed/latest", body,
                 L"X-CMC_PRO_API_KEY: c12f2c5656u843fbb9c0210b15f0011a\r\n"))
        return false;

    double v = 0.0;
    if (!GetJsonDouble(body, "\"value\"", v)) return false;
    int vi = (int)v;
    if (vi < 0 || vi > 100) return false;

    std::lock_guard<std::mutex> lk(m_mtx);
    m_fg   = vi;
    m_fgOk = true;
    return true;
}

// ─────────────────────────────────────────────────────────────
// HTTP GET via WinHTTP
// ─────────────────────────────────────────────────────────────
bool CWidget::HttpGet(const std::wstring& host, const std::wstring& path,
                      std::string& body, const std::wstring& header)
{
    body.clear();
    HINTERNET hS = WinHttpOpen(L"CryptoPriceTicker/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS) return false;

    HINTERNET hC = WinHttpConnect(hS, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hC) { WinHttpCloseHandle(hS); return false; }

    HINTERNET hR = WinHttpOpenRequest(hC, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hR) { WinHttpCloseHandle(hC); WinHttpCloseHandle(hS); return false; }

    // Timeout: 10s for F&G (header present), 5s otherwise
    DWORD tmo = header.empty() ? 5000 : 10000;
    WinHttpSetOption(hR, WINHTTP_OPTION_RECEIVE_TIMEOUT, &tmo, sizeof(tmo));

    const wchar_t* hdrPtr  = header.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header.c_str();
    DWORD          hdrLen  = header.empty() ? 0 : (DWORD)header.size();

    bool ok = false;
    if (WinHttpSendRequest(hR, hdrPtr, hdrLen, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hR, nullptr))
    {
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status == 200)
        {
            DWORD read = 0; char buf[4096];
            do {
                WinHttpReadData(hR, buf, sizeof(buf)-1, &read);
                if (read) { buf[read]='\0'; body+=buf; }
            } while (read > 0);
            ok = !body.empty();
        }
    }
    WinHttpCloseHandle(hR);
    WinHttpCloseHandle(hC);
    WinHttpCloseHandle(hS);
    return ok;
}

// ─────────────────────────────────────────────────────────────
// JSON helpers
// ─────────────────────────────────────────────────────────────
bool CWidget::GetJsonDouble(const std::string& j, const std::string& key, double& v)
{
    size_t pos = j.find(key);
    if (pos == std::string::npos) return false;
    pos = j.find(':', pos);
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < j.size() && (j[pos]==' ' || j[pos]=='\t' || j[pos]=='"')) pos++;
    try { v = std::stod(j.substr(pos)); return true; }
    catch (...) { return false; }
}

// ─────────────────────────────────────────────────────────────
// Price / change formatting
// ─────────────────────────────────────────────────────────────
std::wstring CWidget::FmtPrice(double p)
{
    wchar_t buf[64];
    if (p >= 1.0)          swprintf_s(buf, L"%.2f", p);
    else if (p >= 0.00001) swprintf_s(buf, L"%.4f", p);
    else                   swprintf_s(buf, L"%.8f", p);

    std::wstring s(buf);
    size_t dot    = s.find(L'.');
    size_t intEnd = (dot == std::wstring::npos) ? s.size() : dot;
    int ins = (int)intEnd - 3;
    while (ins > 0) { s.insert(ins, 1, L','); ins -= 3; }
    return s;
}

std::wstring CWidget::FmtChange(double c, bool valid)
{
    if (!valid) return L"n/a";
    wchar_t buf[32];
    if      (c > 0.0) swprintf_s(buf, L"+%.2f%%", c);
    else if (c < 0.0) swprintf_s(buf, L"%.2f%%",  c);
    else              swprintf_s(buf, L"0.00%%");
    return buf;
}

std::wstring CWidget::GetSymbol(const PairCfg& pc) const
{
    // Remove trailing "USD" or "USDT" suffix from label for display
    std::wstring s = pc.label;
    if (s.size() > 3 && s.substr(s.size()-4) == L"USDT") s = s.substr(0, s.size()-4);
    else if (s.size() > 3 && s.substr(s.size()-3) == L"USD") s = s.substr(0, s.size()-3);
    return s;
}

// ─────────────────────────────────────────────────────────────
// Config file
// ─────────────────────────────────────────────────────────────
std::wstring CWidget::CfgPath() const
{
    wchar_t dll[MAX_PATH] = {};
    GetModuleFileNameW(g_hInst, dll, MAX_PATH);
    std::wstring p(dll);
    size_t sl = p.find_last_of(L"\\/");
    p = (sl != std::wstring::npos) ? p.substr(0, sl+1) : p + L"\\";
    return p + APP_CFG_FILE;
}

std::wstring CWidget::Trim(const std::wstring& s)
{
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

void CWidget::LoadCfg()
{
    m_pairs.clear();
    FILE* f = nullptr;
    if (_wfopen_s(&f, CfgPath().c_str(), L"r, ccs=UTF-8") != 0 || !f)
    {
        // Fallback: single BTC entry
        m_pairs.push_back({ L"BTC", L"BTCUSDT", L"BTCUSDT", L"BTC-USDT-SWAP" });
        return;
    }

    wchar_t line[512];
    while (fgetws(line, 512, f))
    {
        std::wstring s = Trim(std::wstring(line));
        if (s.empty() || s[0] == L'#') continue;

        // Format: LABEL=BinSym,BybitSym,OkxSym
        size_t eq = s.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring label = Trim(s.substr(0, eq));
        std::wstring val   = Trim(s.substr(eq+1));
        if (label.empty() || val.empty()) continue;

        std::wstring parts[3];
        int pi = 0;
        size_t pos = 0;
        while (pi < 3)
        {
            size_t cm = val.find(L',', pos);
            if (cm == std::wstring::npos) { parts[pi++] = Trim(val.substr(pos)); break; }
            parts[pi++] = Trim(val.substr(pos, cm-pos));
            pos = cm + 1;
        }
        if (pi < 3 || parts[0].empty() || parts[1].empty() || parts[2].empty()) continue;

        m_pairs.push_back({ label, parts[0], parts[1], parts[2] });
    }
    fclose(f);
    if (m_pairs.empty())
        m_pairs.push_back({ L"BTC", L"BTCUSDT", L"BTCUSDT", L"BTC-USDT-SWAP" });
}

// ─────────────────────────────────────────────────────────────
// Registry settings
// ─────────────────────────────────────────────────────────────
void CWidget::LoadSettings()
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, KEY_READ, &hk) != ERROR_SUCCESS) return;
    DWORD v, sz = sizeof(DWORD);
    #define RQ(n,d) sz=sizeof(DWORD); if(RegQueryValueExW(hk,n,nullptr,nullptr,(BYTE*)&v,&sz)==ERROR_SUCCESS) d=(decltype(d))v;
    RQ(L"PairIndex",  m_pairIdx)
    RQ(L"IntervalMs", m_intervalMs)
    RQ(L"Provider",   m_provider)
    #undef RQ
    RegCloseKey(hk);
}
void CWidget::SaveSettings()
{
    HKEY hk = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, APP_REG_KEY, 0, nullptr,
        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hk, nullptr);
    if (!hk) return;
    #define RS(n,v) { DWORD _v=(DWORD)(v); RegSetValueExW(hk,n,0,REG_DWORD,(BYTE*)&_v,sizeof(_v)); }
    RS(L"PairIndex",  m_pairIdx)
    RS(L"IntervalMs", m_intervalMs)
    RS(L"Provider",   m_provider)
    #undef RS
    RegCloseKey(hk);
}