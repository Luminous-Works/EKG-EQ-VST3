#include "ekgeq_editor.h"
#include <commctrl.h>
#include <cmath>
#include <cstdio>
#include <cstring>

// ── Layout constants ─────────────────────────────────────────────────
static const int COLS     = 6;
static const int COL_W    = EDITOR_W / COLS;   // 140
static const int SR       = 1000;               // slider range 0-1000
static const int HEADER_H = 30;                 // top bar height

static const int Y_BAND  = HEADER_H + 6;    // 36  band name (painted)
static const int Y_GL    = HEADER_H + 26;   // 56  GAIN label
static const int Y_GS    = HEADER_H + 42;   // 72  GAIN slider
static const int Y_GV    = HEADER_H + 66;   // 96  GAIN value
static const int Y_FL    = HEADER_H + 90;   // 120 FREQ label
static const int Y_FS    = HEADER_H + 106;  // 136 FREQ slider
static const int Y_FV    = HEADER_H + 130;  // 160 FREQ value
static const int Y_QL    = HEADER_H + 154;  // 184 Q label
static const int Y_QS    = HEADER_H + 170;  // 200 Q slider
static const int Y_QV    = HEADER_H + 194;  // 224 Q value

// ── Colours ──────────────────────────────────────────────────────────
static const COLORREF BG       = RGB(7,   7,  13);
static const COLORREF HDR_BG   = RGB(12, 12,  22);
static const COLORREF TEAL     = RGB(0, 212, 184);
static const COLORREF DIM      = RGB(80,  90, 100);

// Per-band accent colors
static const COLORREF BAND_COLORS[6] = {
    RGB(120,  90, 255),  // SUB    — violet
    RGB( 40, 200, 100),  // BASS   — green
    RGB(  0, 212, 184),  // LO-MID — teal
    RGB(212, 168,  67),  // MID    — amber (Luminous DNA)
    RGB(255, 140,   0),  // HI-MID — orange
    RGB(140, 200, 255),  // AIR    — ice blue
};

// ── Band / param metadata ────────────────────────────────────────────
static const char* BAND_LABELS[6]  = {"SUB","BASS","LO-MID","MID","HI-MID","AIR"};
static const char* PARAM_LABELS[3] = {"GAIN","FREQ","Q"};

static const ParamID PID[6][3] = {
    {kSub_Gain,   kSub_Freq,   kSub_Q  },
    {kBass_Gain,  kBass_Freq,  kBass_Q },
    {kLoMid_Gain, kLoMid_Freq, kLoMid_Q},
    {kMid_Gain,   kMid_Freq,   kMid_Q  },
    {kHiMid_Gain, kHiMid_Freq, kHiMid_Q},
    {kAir_Gain,   kAir_Freq,   kAir_Q  },
};

// ── Helpers ──────────────────────────────────────────────────────────
static inline int   bandOf(int idx) { return idx / 3; }
static inline int   paramOf(int idx){ return idx % 3; }
static inline int   sliderCtrlId(int idx) { return 200 + idx; }

static void formatValue(int param, double norm, char* buf, int sz) {
    if (param == 0) {
        double db = norm * 48.0 - 24.0;
        snprintf(buf, sz, "%+.1f dB", db);
    } else if (param == 1) {
        double hz = 20.0 * std::pow(1000.0, norm);
        if (hz >= 1000.0) snprintf(buf, sz, "%.1f kHz", hz / 1000.0);
        else              snprintf(buf, sz, "%.0f Hz",  hz);
    } else {
        double q = 0.1 + norm * 17.9;
        snprintf(buf, sz, "%.2f Q", q);
    }
}

// ── Static flag ──────────────────────────────────────────────────────
bool EKGEQEditor::s_classRegistered = false;

// ── Constructor / Destructor ─────────────────────────────────────────
EKGEQEditor::EKGEQEditor(EditController* c) : _controller(c) {}

EKGEQEditor::~EKGEQEditor() {
    if (_hwnd) { DestroyWindow(_hwnd); _hwnd = nullptr; }
}

// ── IPlugView ────────────────────────────────────────────────────────
tresult PLUGIN_API EKGEQEditor::isPlatformTypeSupported(FIDString type) {
    return (strcmp(type, kPlatformTypeHWND) == 0) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API EKGEQEditor::getSize(ViewRect* r) {
    if (!r) return kResultFalse;
    r->left = 0; r->top = 0; r->right = EDITOR_W; r->bottom = EDITOR_H;
    return kResultOk;
}

tresult PLUGIN_API EKGEQEditor::attached(void* parent, FIDString type) {
    if (strcmp(type, kPlatformTypeHWND) != 0) return kResultFalse;
    HWND parentWnd = (HWND)parent;

    if (!s_classRegistered) {
        WNDCLASSEXA wc   = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleA(nullptr);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = "EKGEQEditorWnd";
        RegisterClassExA(&wc);
        s_classRegistered = true;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    _hwnd = CreateWindowExA(0, "EKGEQEditorWnd", "",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, EDITOR_W, EDITOR_H,
        parentWnd, nullptr, GetModuleHandleA(nullptr), this);

    if (!_hwnd) return kResultFalse;
    createControls(_hwnd);
    syncFromController();
    return kResultOk;
}

tresult PLUGIN_API EKGEQEditor::removed() {
    if (_hwnd) { DestroyWindow(_hwnd); _hwnd = nullptr; }
    return kResultOk;
}

// ── Control creation ─────────────────────────────────────────────────
void EKGEQEditor::createControls(HWND parent) {
    HINSTANCE hi = GetModuleHandleA(nullptr);
    HFONT font   = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    for (int b = 0; b < 6; ++b) {
        int x = b * COL_W;
        // Band name is painted in WM_PAINT (per-band color) — no STATIC here

        for (int p = 0; p < 3; ++p) {
            int idx = b*3+p;
            int yL  = (p==0)?Y_GL:(p==1)?Y_FL:Y_QL;
            int yS  = (p==0)?Y_GS:(p==1)?Y_FS:Y_QS;
            int yV  = (p==0)?Y_GV:(p==1)?Y_FV:Y_QV;

            // Param label
            HWND pl = CreateWindowExA(0,"STATIC",PARAM_LABELS[p],
                WS_CHILD|WS_VISIBLE|SS_CENTER, x+4,yL,COL_W-8,14,
                parent, nullptr, hi, nullptr);
            SendMessage(pl, WM_SETFONT, (WPARAM)font, TRUE);

            // Slider
            _sliders[idx] = CreateWindowExA(0, TRACKBAR_CLASSA,"",
                WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_NOTICKS,
                x+4, yS, COL_W-8, 20,
                parent, (HMENU)(intptr_t)sliderCtrlId(idx), hi, nullptr);
            SendMessage(_sliders[idx], TBM_SETRANGE, TRUE, MAKELONG(0,SR));
            SendMessage(_sliders[idx], TBM_SETPAGESIZE, 0, 10);

            // Value label
            _valLbls[idx] = CreateWindowExA(0,"STATIC","---",
                WS_CHILD|WS_VISIBLE|SS_CENTER, x+4,yV,COL_W-8,14,
                parent, (HMENU)(intptr_t)(300+idx), hi, nullptr);
            SendMessage(_valLbls[idx], WM_SETFONT, (WPARAM)font, TRUE);
        }
    }

    // Bypass — placed in the header bar, far right, clear of all columns
    _bypassBtn = CreateWindowExA(0,"BUTTON","BYPASS",
        WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
        EDITOR_W-86, 6, 80, 18,
        parent, (HMENU)400, hi, nullptr);
    SendMessage(_bypassBtn, WM_SETFONT, (WPARAM)font, TRUE);
}

// ── Sync from controller ─────────────────────────────────────────────
void EKGEQEditor::syncFromController() {
    if (!_controller || !_hwnd) return;
    _syncing = true;
    for (int idx = 0; idx < 18; ++idx) {
        double norm = _controller->getParamNormalized(PID[bandOf(idx)][paramOf(idx)]);
        SendMessage(_sliders[idx], TBM_SETPOS, TRUE, (LPARAM)(norm * SR));
        updateLabel(idx, norm);
    }
    double byp = _controller->getParamNormalized(kBypass);
    if (_bypassBtn)
        SendMessage(_bypassBtn, BM_SETCHECK, byp >= 0.5 ? BST_CHECKED : BST_UNCHECKED, 0);
    _syncing = false;
}

void EKGEQEditor::updateLabel(int idx, double norm) {
    char buf[32];
    formatValue(paramOf(idx), norm, buf, sizeof(buf));
    if (_valLbls[idx]) SetWindowTextA(_valLbls[idx], buf);
}

// ── Slider / bypass callbacks ────────────────────────────────────────
void EKGEQEditor::onSliderChange(int ctrlId, int pos) {
    if (_syncing || !_controller) return;
    int idx  = ctrlId - 200;
    if (idx < 0 || idx >= 18) return;
    double norm = pos / (double)SR;
    ParamID pid = PID[bandOf(idx)][paramOf(idx)];
    _controller->beginEdit(pid);
    _controller->performEdit(pid, norm);
    _controller->endEdit(pid);
    updateLabel(idx, norm);
}

void EKGEQEditor::onBypassToggle(bool on) {
    if (!_controller) return;
    double norm = on ? 1.0 : 0.0;
    _controller->beginEdit(kBypass);
    _controller->performEdit(kBypass, norm);
    _controller->endEdit(kBypass);
}

// ── WndProc ──────────────────────────────────────────────────────────
LRESULT CALLBACK EKGEQEditor::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    EKGEQEditor* self = nullptr;
    if (msg == WM_CREATE) {
        self = (EKGEQEditor*)((CREATESTRUCTA*)lp)->lpCreateParams;
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = (EKGEQEditor*)GetWindowLongPtrA(hwnd, GWLP_USERDATA);
    }

    static HBRUSH hBgBrush  = nullptr;
    static HBRUSH hHdrBrush = nullptr;
    if (!hBgBrush)  hBgBrush  = CreateSolidBrush(BG);
    if (!hHdrBrush) hHdrBrush = CreateSolidBrush(HDR_BG);

    switch (msg) {
        case WM_ERASEBKGND: {
            RECT r; GetClientRect(hwnd, &r);
            FillRect((HDC)wp, &r, hBgBrush);
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, TEAL);
            SetBkColor(hdc, BG);
            return (LRESULT)hBgBrush;
        }
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wp;
            SetTextColor(hdc, TEAL);
            SetBkColor(hdc, HDR_BG);
            return (LRESULT)hHdrBrush;
        }
        case WM_HSCROLL: {
            HWND slider = (HWND)lp;
            int  cid    = GetDlgCtrlID(slider);
            int  pos    = (int)SendMessage(slider, TBM_GETPOS, 0, 0);
            if (self) self->onSliderChange(cid, pos);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == 400 && self && self->_bypassBtn) {
                bool on = (SendMessage(self->_bypassBtn, BM_GETCHECK, 0, 0) == BST_CHECKED);
                self->onBypassToggle(on);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // ── Header bar ────────────────────────────────────────────
            RECT hdrRect = { 0, 0, EDITOR_W, HEADER_H };
            FillRect(hdc, &hdrRect, hHdrBrush);

            // "EKG·EQ" title  (\xB7 = middot in Windows-1252)
            HFONT titleFont = CreateFontA(-14, 0, 0, 0, FW_BOLD,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_MODERN, "Courier New");
            HFONT prevFont = (HFONT)SelectObject(hdc, titleFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, TEAL);
            RECT titleRect = { 8, 0, 250, HEADER_H };
            DrawTextA(hdc, "EKG\xB7" "EQ", -1, &titleRect,
                DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            // ── Band name labels (per-band colour) ────────────────────
            for (int b = 0; b < 6; ++b) {
                int x = b * COL_W;
                SetTextColor(hdc, BAND_COLORS[b]);
                RECT bandRect = { x+4, Y_BAND, x+COL_W-4, Y_BAND+16 };
                DrawTextA(hdc, BAND_LABELS[b], -1, &bandRect,
                    DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }

            SelectObject(hdc, prevFont);
            DeleteObject(titleFont);

            // ── Header separator (teal line) ──────────────────────────
            HPEN hdrPen = CreatePen(PS_SOLID, 1, TEAL);
            HPEN prevPen = (HPEN)SelectObject(hdc, hdrPen);
            MoveToEx(hdc, 0, HEADER_H - 1, nullptr);
            LineTo(hdc, EDITOR_W, HEADER_H - 1);

            // ── Column separators ─────────────────────────────────────
            HPEN sepPen = CreatePen(PS_SOLID, 1, DIM);
            SelectObject(hdc, sepPen);
            DeleteObject(hdrPen);
            for (int i = 1; i < COLS; ++i) {
                int x = i * COL_W;
                MoveToEx(hdc, x, 0, nullptr);
                LineTo(hdc, x, EDITOR_H);
            }
            SelectObject(hdc, prevPen);
            DeleteObject(sepPen);

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}
