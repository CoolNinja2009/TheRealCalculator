// evaluator.cpp
#include "evaluator.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <algorithm>

namespace {

struct RowParser {
    const std::vector<std::unique_ptr<Item>>& items;
    size_t pos = 0;

    explicit RowParser(const Row* row) : items(row->items) {}

    bool atEnd() const { return pos >= items.size(); }
    const Item* peek() const { return atEnd() ? nullptr : items[pos].get(); }

    bool peekIsOperatorChar(char c) const {
        const Item* it = peek();
        return it && it->type == ItemType::Operator && it->opChar == c;
    }

    double parseAtom() {
        if (atEnd())
            throw std::runtime_error("Incomplete expression");
        const Item* it = items[pos].get();
        switch (it->type) {
            case ItemType::Number: {
                pos++;
                if (it->numText.empty() || it->numText == ".")
                    throw std::runtime_error("Invalid number");
                try {
                    return std::stod(it->numText);
                } catch (...) {
                    throw std::runtime_error("Invalid number");
                }
            }
            case ItemType::Fraction: {
                pos++;
                double n = evaluate(it->a.get());
                double d = evaluate(it->b.get());
                if (d == 0.0) throw std::runtime_error("Division by zero");
                return n / d;
            }
            case ItemType::Paren: {
                pos++;
                return evaluate(it->a.get());
            }
            case ItemType::Power: {
                pos++;
                double base = evaluate(it->a.get());
                double exp = evaluate(it->b.get());
                return std::pow(base, exp);
            }
            case ItemType::Sqrt: {
                pos++;
                double v = evaluate(it->a.get());
                if (v < 0.0) throw std::runtime_error("Root of negative number");
                return std::sqrt(v);
            }
            case ItemType::Operator:
                throw std::runtime_error("Unexpected operator");
        }
        throw std::runtime_error("Unknown item");
    }

    double parseFactor() {
        bool neg = false;
        while (peekIsOperatorChar('-') || peekIsOperatorChar('+')) {
            if (peekIsOperatorChar('-')) neg = !neg;
            pos++;
        }
        double v = parseAtom();
        return neg ? -v : v;
    }

    // '*' explicit, or bare adjacency of two atoms (implicit multiplication,
    // e.g. "2(3+4)" or "2\u221A3").
    double parseTerm() {
        double v = parseFactor();
        for (;;) {
            if (peekIsOperatorChar('*')) {
                pos++;
                v *= parseFactor();
                continue;
            }
            // Implicit multiplication: next token exists and is not a
            // flat operator (+, -, *) -> another atom starts here.
            const Item* nxt = peek();
            if (nxt && nxt->type != ItemType::Operator) {
                v *= parseFactor();
                continue;
            }
            break;
        }
        return v;
    }

    double parseRow() {
        if (atEnd()) return 0.0; // empty row evaluates to 0 (e.g. empty exponent)
        double v = parseTerm();
        for (;;) {
            if (peekIsOperatorChar('+')) {
                pos++;
                v += parseTerm();
            } else if (peekIsOperatorChar('-')) {
                pos++;
                v -= parseTerm();
            } else {
                break;
            }
        }
        if (!atEnd())
            throw std::runtime_error("Malformed expression");
        return v;
    }
};

} // namespace

double evaluate(const Row* root) {
    if (!root) return 0.0;
    RowParser p(root);
    return p.parseRow();
}

std::string evaluateToString(const Row* root) {
    try {
        double v = evaluate(root);
        if (!std::isfinite(v))
            return std::isnan(v) ? "Error" : (v > 0 ? "Infinity" : "-Infinity");

        // Prefer plain fixed notation; fall back to scientific for very
        // large/small magnitudes.
        double av = std::fabs(v);
        char buf[64];
        if (av != 0.0 && (av >= 1e15 || av < 1e-9)) {
            std::snprintf(buf, sizeof(buf), "%.6e", v);
        } else {
            std::snprintf(buf, sizeof(buf), "%.10f", v);
            std::string s(buf);
            // trim trailing zeros, then trailing '.'
            size_t dot = s.find('.');
            if (dot != std::string::npos) {
                size_t last = s.find_last_not_of('0');
                if (last == dot) last--; // strip the dot too
                s.erase(last + 1);
            }
            return s;
        }
        return std::string(buf);
    } catch (const std::exception& e) {
        return std::string(e.what());
    }
}
