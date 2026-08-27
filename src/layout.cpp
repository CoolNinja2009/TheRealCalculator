// layout.cpp
#include "layout.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

Theme lightTheme() {
    Theme t;
    t.background      = RGB(0xFA, 0xFA, 0xFA);
    t.panelBackground = RGB(0xFF, 0xFF, 0xFF);
    t.text             = RGB(0x1B, 0x1B, 0x1B);
    t.operatorColor    = RGB(0x00, 0x5A, 0x9E);
    t.resultColor      = RGB(0x00, 0x78, 0xD4);
    t.caret             = RGB(0x00, 0x5A, 0x9E);
    t.placeholder       = RGB(0xC6, 0xC6, 0xC6);
    t.divider           = RGB(0xE1, 0xE1, 0xE1);
    t.accent            = RGB(0x00, 0x78, 0xD4);
    t.isDark = false;
    return t;
}

Theme darkTheme() {
    Theme t;
    t.background      = RGB(0x20, 0x20, 0x20);
    t.panelBackground = RGB(0x2B, 0x2B, 0x2B);
    t.text             = RGB(0xF0, 0xF0, 0xF0);
    t.operatorColor    = RGB(0x6C, 0xC2, 0xFF);
    t.resultColor      = RGB(0x60, 0xCD, 0xFF);
    t.caret             = RGB(0x6C, 0xC2, 0xFF);
    t.placeholder       = RGB(0x5A, 0x5A, 0x5A);
    t.divider           = RGB(0x3A, 0x3A, 0x3A);
    t.accent            = RGB(0x60, 0xCD, 0xFF);
    t.isDark = true;
    return t;
}

namespace {

constexpr int kBaseFontPx = 26;
constexpr double kShrink = 0.74;
constexpr int kMinFontPx = 10;
constexpr int kMaxDepth = 6;

std::unordered_map<int, HFONT> g_fontCache;

int fontPxForDepth(int depth) {
    if (depth > kMaxDepth) depth = kMaxDepth;
    double px = kBaseFontPx;
    for (int i = 0; i < depth; ++i) px *= kShrink;
    int ipx = (int)std::round(px);
    return std::max(ipx, kMinFontPx);
}

HFONT fontForDepth(int depth) {
    int px = fontPxForDepth(depth);
    auto it = g_fontCache.find(px);
    if (it != g_fontCache.end()) return it->second;
    HFONT f = CreateFontW(
        -px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fontCache[px] = f;
    return f;
}

struct FontMetricsCache {
    TEXTMETRICW tm;
    bool have = false;
};
std::unordered_map<int, FontMetricsCache> g_tmCache;

TEXTMETRICW textMetricsForDepth(HDC hdc, int depth) {
    int px = fontPxForDepth(depth);
    auto& c = g_tmCache[px];
    if (!c.have) {
        HFONT f = fontForDepth(depth);
        HFONT old = (HFONT)SelectObject(hdc, f);
        GetTextMetricsW(hdc, &c.tm);
        SelectObject(hdc, old);
        c.have = true;
    }
    return c.tm;
}

int scaledPx(int basePx, int depth) {
    // Small structural paddings (gaps, bar thickness, paren glyph widths)
    // scale down gently with depth so nested structures don't look chunky.
    double factor = 1.0;
    for (int i = 0; i < depth && i < kMaxDepth; ++i) factor *= 0.85;
    int v = (int)std::round(basePx * factor);
    return std::max(v, 1);
}

// Forward decls (measure/draw are mutually recursive through Row<->Item).
Size measureRowImpl(HDC hdc, const Row* row, int depth);
Size measureItemImpl(HDC hdc, const Item* it, int depth);
void drawRowImpl(HDC hdc, const Row* row, int depth, int x, int baselineY,
                  const Theme& theme, const Cursor* cursor, CaretInfo* outCaret);
void drawItemImpl(HDC hdc, const Item* it, int depth, int x, int baselineY,
                   const Theme& theme, const Cursor* cursor, CaretInfo* outCaret);

Size placeholderSize(int depth) {
    // Approximate without an HDC: use fontPx directly for a stable box.
    int px = fontPxForDepth(depth);
    Size s;
    s.width = std::max(px / 2, 10);
    s.ascent = (int)(px * 0.72);
    s.descent = (int)(px * 0.22);
    return s;
}

Size measureNumber(HDC hdc, const Item* it, int depth) {
    HFONT f = fontForDepth(depth);
    HFONT old = (HFONT)SelectObject(hdc, f);
    SIZE sz{};
    std::wstring w(it->numText.begin(), it->numText.end());
    if (w.empty()) w = L" ";
    GetTextExtentPoint32W(hdc, w.c_str(), (int)w.size(), &sz);
    SelectObject(hdc, old);
    TEXTMETRICW tm = textMetricsForDepth(hdc, depth);
    Size s;
    s.width = sz.cx;
    s.ascent = tm.tmAscent;
    s.descent = tm.tmDescent;
    return s;
}

Size measureVariable(HDC hdc, const Item* it, int depth) {
    Item display(ItemType::Number);
    display.numText = std::string(1, it->variableName);
    return measureNumber(hdc, &display, depth);
}

const wchar_t* opGlyph(char c) {
    switch (c) {
        case '+': return L" + ";
        case '-': return L" \u2212 ";
        case '*': return L" \u00D7 ";
        case '!': return L"!";
        default: return L" ";
    }
}

Size measureOperator(HDC hdc, const Item* it, int depth) {
    HFONT f = fontForDepth(depth);
    HFONT old = (HFONT)SelectObject(hdc, f);
    SIZE sz{};
    const wchar_t* g = opGlyph(it->opChar);
    GetTextExtentPoint32W(hdc, g, (int)wcslen(g), &sz);
    SelectObject(hdc, old);
    TEXTMETRICW tm = textMetricsForDepth(hdc, depth);
    Size s;
    s.width = sz.cx;
    s.ascent = tm.tmAscent;
    s.descent = tm.tmDescent;
    return s;
}

HFONT createParenFont(int depth, int innerHeight) {
    int height = std::max(fontPxForDepth(depth), innerHeight + scaledPx(6, depth));
    return CreateFontW(-height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

int measureParenWidth(HDC hdc, int depth, int innerHeight) {
    HFONT font = createParenFont(depth, innerHeight);
    HFONT old = (HFONT)SelectObject(hdc, font);
    SIZE size{};
    GetTextExtentPoint32W(hdc, L"(", 1, &size);
    SelectObject(hdc, old);
    DeleteObject(font);
    return size.cx;
}

Size measureEquals(HDC hdc, int depth) {
    HFONT f = fontForDepth(depth);
    HFONT old = (HFONT)SelectObject(hdc, f);
    SIZE sz{};
    const wchar_t* glyph = L" = ";
    GetTextExtentPoint32W(hdc, glyph, 3, &sz);
    SelectObject(hdc, old);
    TEXTMETRICW tm = textMetricsForDepth(hdc, depth);
    return { sz.cx, tm.tmAscent, tm.tmDescent };
}

Size measureRowImpl(HDC hdc, const Row* row, int depth) {
    if (rowIsEmpty(row)) return placeholderSize(depth);
    Size total;
    for (auto& it : row->items) {
        Size s = measureItemImpl(hdc, it.get(), depth);
        total.width += s.width;
        total.ascent = std::max(total.ascent, s.ascent);
        total.descent = std::max(total.descent, s.descent);
    }
    return total;
}

Size measureItemImpl(HDC hdc, const Item* it, int depth) {
    switch (it->type) {
        case ItemType::Number:   return measureNumber(hdc, it, depth);
        case ItemType::Variable: return measureVariable(hdc, it, depth);
        case ItemType::Equals:   return measureEquals(hdc, depth);
        case ItemType::CloseParen: {
            Item display(ItemType::Number);
            display.numText = ")";
            return measureNumber(hdc, &display, depth);
        }
        case ItemType::Operator: return measureOperator(hdc, it, depth);
        case ItemType::Fraction: {
            Size num = measureRowImpl(hdc, it->a.get(), depth + 1);
            Size den = measureRowImpl(hdc, it->b.get(), depth + 1);
            int gap = scaledPx(4, depth);
            int bar = scaledPx(2, depth);
            int hpad = scaledPx(6, depth);
            Size s;
            s.width = std::max(num.width, den.width) + 2 * hpad;
            s.ascent = num.height() + gap + bar;
            s.descent = den.height() + gap;
            return s;
        }
        case ItemType::Power: {
            // Power's "base" is a row (whatever atom the '^' key wrapped),
            // measured at the same depth; only the exponent shrinks.
            Size baseRow = measureRowImpl(hdc, it->a.get(), depth);
            Size exp = measureRowImpl(hdc, it->b.get(), depth + 1);
            int raise = (int)(baseRow.ascent * 0.55);
            Size s;
            s.width = baseRow.width + exp.width + scaledPx(2, depth);
            s.ascent = std::max(baseRow.ascent, raise + exp.height());
            s.descent = baseRow.descent;
            return s;
        }
        case ItemType::Paren: {
            Size inner = measureRowImpl(hdc, it->a.get(), depth);
            int glyphW = measureParenWidth(hdc, depth, inner.height());
            Size s;
            s.width = inner.width + 2 * glyphW;
            s.ascent = inner.ascent + scaledPx(2, depth);
            s.descent = inner.descent + scaledPx(2, depth);
            return s;
        }
        case ItemType::Sqrt: {
            Size inner = measureRowImpl(hdc, it->a.get(), depth);
            int radicalW = scaledPx(16, depth);
            int roofPad = scaledPx(5, depth);
            Size s;
            s.width = inner.width + radicalW + scaledPx(4, depth);
            s.ascent = inner.ascent + roofPad;
            s.descent = inner.descent;
            return s;
        }
    }
    return Size{};
}

Size measureExpressionImpl(HDC hdc, const Row* root) { return measureRowImpl(hdc, root, 0); }

void maybeCaptureCaret(const Row* row, int index, int x, int baselineY,
                        int rowAscent, int rowDescent,
                        const Cursor* cursor, CaretInfo* outCaret) {
    if (!cursor || !outCaret || outCaret->valid) return;
    if (cursor->row == row && cursor->index == index) {
        outCaret->valid = true;
        outCaret->x = x;
        outCaret->top = baselineY - rowAscent;
        outCaret->bottom = baselineY + rowDescent;
    }
}

void drawRowImpl(HDC hdc, const Row* row, int depth, int x, int baselineY,
                  const Theme& theme, const Cursor* cursor, CaretInfo* outCaret) {
    if (rowIsEmpty(row)) {
        Size ph = placeholderSize(depth);
        HPEN pen = CreatePen(PS_DOT, 1, theme.placeholder);
        HPEN old = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, x, baselineY - ph.ascent, x + ph.width, baselineY + ph.descent);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, old);
        DeleteObject(pen);
        maybeCaptureCaret(row, 0, x, baselineY, ph.ascent, ph.descent, cursor, outCaret);
        return;
    }

    Size rowSize = measureRowImpl(hdc, row, depth);
    int curX = x;
    for (size_t i = 0; i < row->items.size(); ++i) {
        maybeCaptureCaret(row, (int)i, curX, baselineY, rowSize.ascent, rowSize.descent, cursor, outCaret);
        Item* it = row->items[i].get();
        Size s = measureItemImpl(hdc, it, depth);
        drawItemImpl(hdc, it, depth, curX, baselineY, theme, cursor, outCaret);
        curX += s.width;
    }
    maybeCaptureCaret(row, (int)row->items.size(), curX, baselineY, rowSize.ascent, rowSize.descent, cursor, outCaret);
}

void drawText(HDC hdc, int depth, int x, int baselineY, const std::wstring& w, COLORREF color) {
    HFONT f = fontForDepth(depth);
    HFONT old = (HFONT)SelectObject(hdc, f);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    TEXTMETRICW tm = textMetricsForDepth(hdc, depth);
    TextOutW(hdc, x, baselineY - tm.tmAscent, w.c_str(), (int)w.size());
    SelectObject(hdc, old);
}

void drawItemImpl(HDC hdc, const Item* it, int depth, int x, int baselineY,
                   const Theme& theme, const Cursor* cursor, CaretInfo* outCaret) {
    switch (it->type) {
        case ItemType::Number: {
            std::wstring w(it->numText.begin(), it->numText.end());
            drawText(hdc, depth, x, baselineY, w, theme.text);
            return;
        }
        case ItemType::Variable: {
            std::wstring w(1, (wchar_t)it->variableName);
            drawText(hdc, depth, x, baselineY, w, theme.operatorColor);
            return;
        }
        case ItemType::Equals:
            drawText(hdc, depth, x, baselineY, L" = ", theme.operatorColor);
            return;
        case ItemType::CloseParen:
            drawText(hdc, depth, x, baselineY, L")", theme.text);
            return;
        case ItemType::Operator: {
            drawText(hdc, depth, x, baselineY, opGlyph(it->opChar), theme.operatorColor);
            return;
        }
        case ItemType::Fraction: {
            Size num = measureRowImpl(hdc, it->a.get(), depth + 1);
            Size den = measureRowImpl(hdc, it->b.get(), depth + 1);
            int gap = scaledPx(4, depth);
            int bar = scaledPx(2, depth);
            int hpad = scaledPx(6, depth);
            int w = std::max(num.width, den.width) + 2 * hpad;

            int numBaselineY = baselineY - gap - bar - num.descent;
            int denBaselineY = baselineY + gap + bar + den.ascent;
            int numX = x + hpad + (std::max(num.width, den.width) - num.width) / 2;
            int denX = x + hpad + (std::max(num.width, den.width) - den.width) / 2;

            drawRowImpl(hdc, it->a.get(), depth + 1, numX, numBaselineY, theme, cursor, outCaret);
            drawRowImpl(hdc, it->b.get(), depth + 1, denX, denBaselineY, theme, cursor, outCaret);

            HPEN pen = CreatePen(PS_SOLID, bar, theme.text);
            HPEN old = (HPEN)SelectObject(hdc, pen);
            MoveToEx(hdc, x + 1, baselineY, nullptr);
            LineTo(hdc, x + w - 1, baselineY);
            SelectObject(hdc, old);
            DeleteObject(pen);
            return;
        }
        case ItemType::Power: {
            Size baseRow = measureRowImpl(hdc, it->a.get(), depth);
            int raise = (int)(baseRow.ascent * 0.55);
            drawRowImpl(hdc, it->a.get(), depth, x, baselineY, theme, cursor, outCaret);
            int expX = x + baseRow.width + scaledPx(2, depth);
            int expBaselineY = baselineY - raise;
            drawRowImpl(hdc, it->b.get(), depth + 1, expX, expBaselineY, theme, cursor, outCaret);
            return;
        }
        case ItemType::Paren: {
            Size inner = measureRowImpl(hdc, it->a.get(), depth);
            int glyphW = measureParenWidth(hdc, depth, inner.height());
            HFONT parenFont = createParenFont(depth, inner.height());
            HFONT oldFont = (HFONT)SelectObject(hdc, parenFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, theme.text);
            TEXTMETRICW metrics{};
            GetTextMetricsW(hdc, &metrics);
            TextOutW(hdc, x, baselineY - metrics.tmAscent, L"(", 1);
            drawRowImpl(hdc, it->a.get(), depth, x + glyphW, baselineY, theme, cursor, outCaret);
            TextOutW(hdc, x + glyphW + inner.width, baselineY - metrics.tmAscent, L")", 1);
            SelectObject(hdc, oldFont);
            DeleteObject(parenFont);
            return;
        }
        case ItemType::Sqrt: {
            Size inner = measureRowImpl(hdc, it->a.get(), depth);
            int radicalW = scaledPx(16, depth);
            int roofPad = scaledPx(5, depth);
            int top = baselineY - inner.ascent - roofPad;
            int bot = baselineY + inner.descent;

            HPEN pen = CreatePen(PS_SOLID, std::max(1, scaledPx(2, depth) / 2), theme.text);
            HPEN old = (HPEN)SelectObject(hdc, pen);
            POINT pts[4] = {
                { x, top + (bot - top) * 2 / 3 },
                { x + radicalW / 3, bot },
                { x + radicalW, top },
                { x + radicalW + inner.width + scaledPx(4, depth), top }
            };
            Polyline(hdc, pts, 4);
            SelectObject(hdc, old);
            DeleteObject(pen);

            drawRowImpl(hdc, it->a.get(), depth, x + radicalW, baselineY, theme, cursor, outCaret);
            return;
        }
    }
}

} // namespace

Size measureExpression(HDC hdc, const Row* root) { return measureExpressionImpl(hdc, root); }

void drawExpression(HDC hdc, const Row* root, int originX, int baselineY,
                     const Theme& theme, const Cursor* cursor, CaretInfo* outCaret) {
    if (outCaret) *outCaret = CaretInfo{};
    drawRowImpl(hdc, root, 0, originX, baselineY, theme, cursor, outCaret);
}

int fontHeightForDepth(int depth) { return fontPxForDepth(depth); }

void shutdownFonts() {
    for (auto& kv : g_fontCache) DeleteObject(kv.second);
    g_fontCache.clear();
    g_tmCache.clear();
}
