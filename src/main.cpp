// main.cpp
//
// Native Win32 + GDI application shell. No MFC, no WinRT/XAML, no
// RichEdit/common-controls, no network, no CRT-heavy startup: just
// CreateWindowExW + a WndProc + GDI drawing. This is what keeps process
// start to a handful of milliseconds -- user32.dll/gdi32.dll are always
// already resident in the OS, so there is essentially nothing extra to
// load.

#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <algorithm>
#include "expr_tree.h"
#include "layout.h"
#include "evaluator.h"
#include "workspace.h"

namespace {

// ------------------------------------------------------------- app state

struct ButtonDef {
    RECT rect{};
    std::wstring label;
    int action = 0; // Action enum below
    bool isOperator = false;
    bool isAccent = false;
};

enum Action {
    ActDigit0, ActDigit1, ActDigit2, ActDigit3, ActDigit4,
    ActDigit5, ActDigit6, ActDigit7, ActDigit8, ActDigit9,
    ActDot, ActPlus, ActMinus, ActMul, ActFrac,
    ActOpenParen, ActCloseParen, ActPower, ActSqrt,
    ActEquals, ActClear, ActClearAll, ActToggleTheme, ActBackspace
};

struct App {
    HWND hwnd = nullptr;
    Workspace workspace;
    bool dark = false;
    bool caretVisible = true;
    int scrollY = 0;
    bool scrollToBottomPending = false;
    std::vector<ButtonDef> buttons;
    RECT topBarRect{}, historyRect{}, editorRect{}, buttonAreaRect{};
    HFONT uiFont = nullptr;
    HFONT uiFontSmall = nullptr;
} g;

constexpr UINT_PTR kCaretTimerId = 1;
constexpr int kMinWidth = 340;
constexpr int kMinHeight = 480;

// ------------------------------------------------------------- layout

void layoutButtons(int areaLeft, int areaTop, int areaW, int areaH) {
    g.buttons.clear();
    const int cols = 4;
    const int rows = 6; // last row is the wide "=" button spanning all cols
    int gap = 6;
    int cellW = (areaW - gap * (cols + 1)) / cols;
    int cellH = (areaH - gap * (rows + 1)) / rows;

    auto place = [&](int col, int row, int colSpan, const wchar_t* label, Action act,
                      bool isOp = false, bool accent = false) {
        ButtonDef b;
        b.rect.left = areaLeft + gap + col * (cellW + gap);
        b.rect.top = areaTop + gap + row * (cellH + gap);
        b.rect.right = b.rect.left + cellW * colSpan + gap * (colSpan - 1);
        b.rect.bottom = b.rect.top + cellH;
        b.label = label;
        b.action = act;
        b.isOperator = isOp;
        b.isAccent = accent;
        g.buttons.push_back(b);
    };

    place(0, 0, 1, L"(", ActOpenParen);
    place(1, 0, 1, L")", ActCloseParen);
    place(2, 0, 1, L"x^y", ActPower);
    place(3, 0, 1, L"\u221A", ActSqrt);

    place(0, 1, 1, L"7", ActDigit7);
    place(1, 1, 1, L"8", ActDigit8);
    place(2, 1, 1, L"9", ActDigit9);
    place(3, 1, 1, L"\u00F7", ActFrac, true);

    place(0, 2, 1, L"4", ActDigit4);
    place(1, 2, 1, L"5", ActDigit5);
    place(2, 2, 1, L"6", ActDigit6);
    place(3, 2, 1, L"\u00D7", ActMul, true);

    place(0, 3, 1, L"1", ActDigit1);
    place(1, 3, 1, L"2", ActDigit2);
    place(2, 3, 1, L"3", ActDigit3);
    place(3, 3, 1, L"\u2212", ActMinus, true);

    place(0, 4, 1, L"C", ActClear);
    place(1, 4, 1, L"0", ActDigit0);
    place(2, 4, 1, L".", ActDot);
    place(3, 4, 1, L"+", ActPlus, true);

    place(0, 5, 4, L"=", ActEquals, false, true);
}

void recomputeLayout() {
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    int topBarH = 40;
    int editorH = std::max(70, h / 6);
    int buttonAreaH = std::min(360, std::max(260, h * 5 / 12));

    g.topBarRect = { 0, 0, w, topBarH };
    g.buttonAreaRect = { 0, h - buttonAreaH, w, h };
    g.editorRect = { 0, g.buttonAreaRect.top - editorH, w, g.buttonAreaRect.top };
    g.historyRect = { 0, topBarH, w, g.editorRect.top };

    layoutButtons(g.buttonAreaRect.left, g.buttonAreaRect.top,
                  w, g.buttonAreaRect.bottom - g.buttonAreaRect.top);
}

// ------------------------------------------------------------- actions

void ensureCaretVisible() {
    // Simple heuristic: keep caret blinking "on" right after any edit so
    // the change is immediately visible.
    g.caretVisible = true;
    KillTimer(g.hwnd, kCaretTimerId);
    SetTimer(g.hwnd, kCaretTimerId, 530, nullptr);
}

void doAction(int action) {
    Expression& cur = g.workspace.current();
    switch (action) {
        case ActDigit0: insertDigit(cur, '0'); break;
        case ActDigit1: insertDigit(cur, '1'); break;
        case ActDigit2: insertDigit(cur, '2'); break;
        case ActDigit3: insertDigit(cur, '3'); break;
        case ActDigit4: insertDigit(cur, '4'); break;
        case ActDigit5: insertDigit(cur, '5'); break;
        case ActDigit6: insertDigit(cur, '6'); break;
        case ActDigit7: insertDigit(cur, '7'); break;
        case ActDigit8: insertDigit(cur, '8'); break;
        case ActDigit9: insertDigit(cur, '9'); break;
        case ActDot: insertDigit(cur, '.'); break;
        case ActPlus: insertOperator(cur, '+'); break;
        case ActMinus: insertOperator(cur, '-'); break;
        case ActMul: insertOperator(cur, '*'); break;
        case ActFrac: insertFraction(cur); break;
        case ActOpenParen: insertOpenParen(cur); break;
        case ActCloseParen: insertCloseParen(cur); break;
        case ActPower: insertPower(cur); break;
        case ActSqrt: insertSqrt(cur); break;
        case ActBackspace: backspace(cur); break;
        case ActEquals:
            if (g.workspace.commitCurrent()) g.scrollToBottomPending = true;
            break;
        case ActClear:
            g.workspace.current() = Expression();
            break;
        case ActClearAll:
            g.workspace.clearAll();
            break;
        case ActToggleTheme:
            g.dark = !g.dark;
            break;
        default: break;
    }
    ensureCaretVisible();
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

// ------------------------------------------------------------- painting

RECT themeToggleRect() {
    RECT r = g.topBarRect;
    r.left = r.right - 60;
    r.right -= 8;
    r.top += 6;
    r.bottom -= 6;
    return r;
}

void paintButton(HDC hdc, const ButtonDef& b, const Theme& theme) {
    COLORREF fill = theme.panelBackground;
    COLORREF fg = theme.text;
    if (b.isAccent) { fill = theme.accent; fg = RGB(255, 255, 255); }
    else if (b.isOperator) { fg = theme.operatorColor; }

    HBRUSH brush = CreateSolidBrush(fill);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
    HPEN pen = CreatePen(PS_SOLID, 1, theme.divider);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, b.rect.left, b.rect.top, b.rect.right, b.rect.bottom, 10, 10);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    HFONT old = (HFONT)SelectObject(hdc, g.uiFont);
    RECT r = b.rect;
    DrawTextW(hdc, b.label.c_str(), -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, old);
}

void paint(HDC hdc, RECT client) {
    Theme theme = g.dark ? darkTheme() : lightTheme();

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, client.right, client.bottom);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bgBrush = CreateSolidBrush(theme.background);
    FillRect(mem, &client, bgBrush);
    DeleteObject(bgBrush);

    // --- top bar ---
    HBRUSH panelBrush = CreateSolidBrush(theme.panelBackground);
    FillRect(mem, &g.topBarRect, panelBrush);
    DeleteObject(panelBrush);
    {
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, theme.text);
        HFONT old = (HFONT)SelectObject(mem, g.uiFont);
        RECT r = g.topBarRect; r.left += 12;
        DrawTextW(mem, L"Natural Calculator", -1, &r, DT_VCENTER | DT_SINGLELINE);
        SelectObject(mem, old);
    }
    {
        RECT tr = themeToggleRect();
        HBRUSH b = CreateSolidBrush(theme.panelBackground);
        HPEN p = CreatePen(PS_SOLID, 1, theme.divider);
        HBRUSH ob = (HBRUSH)SelectObject(mem, b);
        HPEN op = (HPEN)SelectObject(mem, p);
        RoundRect(mem, tr.left, tr.top, tr.right, tr.bottom, 8, 8);
        SelectObject(mem, ob); SelectObject(mem, op);
        DeleteObject(b); DeleteObject(p);
        SetTextColor(mem, theme.text);
        HFONT old = (HFONT)SelectObject(mem, g.uiFontSmall);
        DrawTextW(mem, g.dark ? L"Light" : L"Dark", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(mem, old);
    }
    HPEN divPen = CreatePen(PS_SOLID, 1, theme.divider);
    HPEN oldPen = (HPEN)SelectObject(mem, divPen);
    MoveToEx(mem, 0, g.topBarRect.bottom, nullptr);
    LineTo(mem, client.right, g.topBarRect.bottom);
    SelectObject(mem, oldPen);
    DeleteObject(divPen);

    // --- history (scrollable) ---
    {
        HRGN clip = CreateRectRgnIndirect(&g.historyRect);
        SelectClipRgn(mem, clip);

        const auto& hist = g.workspace.history();
        int pad = 14;
        int entrySpacing = 10;

        // First pass: compute total content height so we can clamp scroll.
        int totalH = 0;
        std::vector<Size> sizes(hist.size());
        for (size_t i = 0; i < hist.size(); ++i) {
            sizes[i] = measureExpression(mem, hist[i]->expr->root.get());
            totalH += sizes[i].height() + entrySpacing + 22 /* result line */ + entrySpacing;
        }
        int viewH = g.historyRect.bottom - g.historyRect.top;
        int maxScroll = std::max(0, totalH - viewH + pad);
        if (g.scrollToBottomPending) { g.scrollY = maxScroll; g.scrollToBottomPending = false; }
        g.scrollY = std::clamp(g.scrollY, 0, maxScroll);

        int y = g.historyRect.top + pad - g.scrollY;
        for (size_t i = 0; i < hist.size(); ++i) {
            const Size& s = sizes[i];
            int baseline = y + s.ascent;
            if (baseline + s.descent >= g.historyRect.top && baseline - s.ascent <= g.historyRect.bottom) {
                drawExpression(mem, hist[i]->expr->root.get(), g.historyRect.left + pad, baseline,
                                theme, nullptr, nullptr);

                HFONT old = (HFONT)SelectObject(mem, g.uiFontSmall);
                SetTextColor(mem, hist[i]->isError ? RGB(0xD8, 0x3B, 0x3B) : theme.resultColor);
                SetBkMode(mem, TRANSPARENT);
                std::wstring res = L"= " + std::wstring(hist[i]->result.begin(), hist[i]->result.end());
                RECT rr = { g.historyRect.left + pad, y + s.height() + 2,
                            g.historyRect.right - pad, y + s.height() + 24 };
                DrawTextW(mem, res.c_str(), -1, &rr, DT_LEFT | DT_SINGLELINE);
                SelectObject(mem, old);
            }
            y += s.height() + entrySpacing + 22 + entrySpacing;
        }
        SelectClipRgn(mem, nullptr);
        DeleteObject(clip);
    }

    // --- editor (current expression) ---
    {
        HBRUSH b = CreateSolidBrush(theme.panelBackground);
        FillRect(mem, &g.editorRect, b);
        DeleteObject(b);
        HPEN p = CreatePen(PS_SOLID, 1, theme.divider);
        HPEN op = (HPEN)SelectObject(mem, p);
        MoveToEx(mem, 0, g.editorRect.top, nullptr);
        LineTo(mem, client.right, g.editorRect.top);
        SelectObject(mem, op);
        DeleteObject(p);

        Expression& cur = g.workspace.current();
        Size s = measureExpression(mem, cur.root.get());
        int midY = (g.editorRect.top + g.editorRect.bottom) / 2;
        int baseline = midY + (s.ascent - s.descent) / 2;
        CaretInfo caret;
        drawExpression(mem, cur.root.get(), g.editorRect.left + 16, baseline, theme, &cur.cursor, &caret);

        if (caret.valid && g.caretVisible) {
            HPEN cp = CreatePen(PS_SOLID, 2, theme.caret);
            HPEN ocp = (HPEN)SelectObject(mem, cp);
            MoveToEx(mem, caret.x, caret.top, nullptr);
            LineTo(mem, caret.x, caret.bottom);
            SelectObject(mem, ocp);
            DeleteObject(cp);
        }
    }

    // --- button panel ---
    {
        HBRUSH b = CreateSolidBrush(theme.background);
        FillRect(mem, &g.buttonAreaRect, b);
        DeleteObject(b);
        for (auto& btn : g.buttons) paintButton(mem, btn, theme);
    }

    BitBlt(hdc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
}

// ------------------------------------------------------------- WndProc

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g.hwnd = hwnd;
            g.uiFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g.uiFontSmall = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            recomputeLayout();
            SetTimer(hwnd, kCaretTimerId, 530, nullptr);
            return 0;

        case WM_SIZE:
            recomputeLayout();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            mmi->ptMinTrackSize.x = kMinWidth;
            mmi->ptMinTrackSize.y = kMinHeight;
            return 0;
        }

        case WM_ERASEBKGND:
            return 1; // avoid flicker; WM_PAINT double-buffers the whole client area

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            paint(hdc, client);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_TIMER:
            if (wParam == kCaretTimerId) {
                g.caretVisible = !g.caretVisible;
                InvalidateRect(hwnd, &g.editorRect, FALSE);
            }
            return 0;

        case WM_LBUTTONDOWN: {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            RECT toggleRect = themeToggleRect();
            if (PtInRect(&toggleRect, pt)) { doAction(ActToggleTheme); return 0; }
            for (auto& b : g.buttons) {
                if (PtInRect(&b.rect, pt)) { doAction(b.action); return 0; }
            }
            SetFocus(hwnd);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g.scrollY -= delta / 2;
            if (g.scrollY < 0) g.scrollY = 0;
            InvalidateRect(hwnd, &g.historyRect, FALSE);
            return 0;
        }

        case WM_CHAR: {
            wchar_t c = (wchar_t)wParam;
            if (c == 8) { doAction(ActBackspace); return 0; }
            if (c == 13) { doAction(ActEquals); return 0; }
            if (c >= '0' && c <= '9') { doAction(ActDigit0 + (c - '0')); return 0; }
            switch (c) {
                case L'.': doAction(ActDot); return 0;
                case L'+': doAction(ActPlus); return 0;
                case L'-': doAction(ActMinus); return 0;
                case L'*': doAction(ActMul); return 0;
                case L'/': doAction(ActFrac); return 0;
                case L'(': doAction(ActOpenParen); return 0;
                case L')': doAction(ActCloseParen); return 0;
                case L'^': doAction(ActPower); return 0;
                default: return 0;
            }
        }

        case WM_KEYDOWN: {
            Expression& cur = g.workspace.current();
            switch (wParam) {
                case VK_LEFT: moveLeft(cur); ensureCaretVisible(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case VK_RIGHT: moveRight(cur); ensureCaretVisible(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case VK_UP: moveUp(cur); ensureCaretVisible(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case VK_DOWN: moveDown(cur); ensureCaretVisible(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case VK_DELETE: doDelete(cur); ensureCaretVisible(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case VK_ESCAPE: doAction(ActClear); return 0;
                default: return 0;
            }
        }

        case WM_DESTROY:
            KillTimer(hwnd, kCaretTimerId);
            if (g.uiFont) DeleteObject(g.uiFont);
            if (g.uiFontSmall) DeleteObject(g.uiFontSmall);
            shutdownFonts();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // we paint everything ourselves
    wc.lpszClassName = L"NaturalCalcWindowClass";
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"Natural Calculator",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 640,
        nullptr, nullptr, hInstance, nullptr);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
