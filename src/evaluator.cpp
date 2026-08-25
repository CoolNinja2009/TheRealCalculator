// evaluator.cpp
#include "evaluator.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <algorithm>

namespace {

struct RowParser {
    const std::vector<std::unique_ptr<Item>>& items;
    const EvaluationContext& context;
    size_t pos = 0;

    RowParser(const Row* row, const EvaluationContext& values)
        : items(row->items), context(values) {}

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
            case ItemType::Variable:
                pos++;
                if (it->variableName == 'x') return context.x;
                if (it->variableName == 'y') return context.y;
                throw std::runtime_error("Unknown variable");
            case ItemType::Fraction: {
                pos++;
                double n = evaluate(it->a.get(), context);
                double d = evaluate(it->b.get(), context);
                if (d == 0.0) throw std::runtime_error("Division by zero");
                return n / d;
            }
            case ItemType::Paren: {
                pos++;
                return evaluate(it->a.get(), context);
            }
            case ItemType::Power: {
                pos++;
                double base = evaluate(it->a.get(), context);
                double exp = evaluate(it->b.get(), context);
                return std::pow(base, exp);
            }
            case ItemType::Sqrt: {
                pos++;
                double v = evaluate(it->a.get(), context);
                if (v < 0.0) throw std::runtime_error("Root of negative number");
                return std::sqrt(v);
            }
            case ItemType::Operator:
                throw std::runtime_error("Unexpected operator");
            case ItemType::Equals:
                throw std::runtime_error("Equation needs two lines");
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

struct Linear {
    double x = 0.0;
    double y = 0.0;
    double constant = 0.0;
    bool valid = true;
};

Linear linearizeSide(const Row* row);

Linear addLinear(const Linear& left, const Linear& right, double sign = 1.0) {
    return { left.x + sign * right.x, left.y + sign * right.y,
             left.constant + sign * right.constant, left.valid && right.valid };
}

Linear multiplyLinear(const Linear& left, const Linear& right) {
    if (!left.valid || !right.valid) return { 0, 0, 0, false };
    bool leftVariable = std::fabs(left.x) > 1e-12 || std::fabs(left.y) > 1e-12;
    bool rightVariable = std::fabs(right.x) > 1e-12 || std::fabs(right.y) > 1e-12;
    if (leftVariable && rightVariable) return { 0, 0, 0, false };
    if (!rightVariable) return { left.x * right.constant, left.y * right.constant,
                                 left.constant * right.constant, true };
    return { right.x * left.constant, right.y * left.constant,
             right.constant * left.constant, true };
}

struct LinearParser {
    const std::vector<std::unique_ptr<Item>>& items;
    size_t pos;
    size_t end;

    LinearParser(const Row* row, size_t begin, size_t finish)
        : items(row->items), pos(begin), end(finish) {}

    bool atEnd() const { return pos >= end; }
    const Item* peek() const { return atEnd() ? nullptr : items[pos].get(); }
    bool isOperator(char op) const {
        const Item* item = peek();
        return item && item->type == ItemType::Operator && item->opChar == op;
    }

    Linear parseAtom() {
        if (atEnd()) return { 0, 0, 0, false };
        const Item* item = items[pos++].get();
        switch (item->type) {
            case ItemType::Number:
                try { return { 0, 0, std::stod(item->numText), true }; }
                catch (...) { return { 0, 0, 0, false }; }
            case ItemType::Variable:
                return item->variableName == 'x' ? Linear{ 1, 0, 0, true } : Linear{ 0, 1, 0, true };
            case ItemType::Paren:
                return linearizeSide(item->a.get());
            case ItemType::Fraction: {
                Linear numerator = linearizeSide(item->a.get());
                Linear denominator = linearizeSide(item->b.get());
                if (!denominator.valid || std::fabs(denominator.x) > 1e-12 || std::fabs(denominator.y) > 1e-12 ||
                    std::fabs(denominator.constant) < 1e-12)
                    return { 0, 0, 0, false };
                return { numerator.x / denominator.constant, numerator.y / denominator.constant,
                         numerator.constant / denominator.constant, numerator.valid };
            }
            case ItemType::Power: {
                Linear base = linearizeSide(item->a.get());
                Linear exponent = linearizeSide(item->b.get());
                if (!base.valid || !exponent.valid || std::fabs(exponent.x) > 1e-12 || std::fabs(exponent.y) > 1e-12)
                    return { 0, 0, 0, false };
                if (std::fabs(exponent.constant - 1.0) < 1e-12) return base;
                if (std::fabs(base.x) > 1e-12 || std::fabs(base.y) > 1e-12)
                    return { 0, 0, 0, false };
                return { 0, 0, std::pow(base.constant, exponent.constant), true };
            }
            case ItemType::Sqrt: {
                Linear value = linearizeSide(item->a.get());
                if (!value.valid || std::fabs(value.x) > 1e-12 || std::fabs(value.y) > 1e-12 || value.constant < 0)
                    return { 0, 0, 0, false };
                return { 0, 0, std::sqrt(value.constant), true };
            }
            case ItemType::Operator:
            case ItemType::Equals:
                return { 0, 0, 0, false };
        }
        return { 0, 0, 0, false };
    }

    Linear parseFactor() {
        bool negative = false;
        while (isOperator('-') || isOperator('+')) {
            if (isOperator('-')) negative = !negative;
            ++pos;
        }
        Linear value = parseAtom();
        if (negative) value = { -value.x, -value.y, -value.constant, value.valid };
        return value;
    }

    Linear parseTerm() {
        Linear value = parseFactor();
        while (!atEnd()) {
            if (isOperator('*')) {
                ++pos;
                value = multiplyLinear(value, parseFactor());
            } else if (peek()->type != ItemType::Operator) {
                value = multiplyLinear(value, parseFactor());
            } else {
                break;
            }
        }
        return value;
    }

    Linear parseRow() {
        if (atEnd()) return { 0, 0, 0, false };
        Linear value = parseTerm();
        while (!atEnd()) {
            if (isOperator('+')) { ++pos; value = addLinear(value, parseTerm()); }
            else if (isOperator('-')) { ++pos; value = addLinear(value, parseTerm(), -1.0); }
            else return { 0, 0, 0, false };
        }
        return value;
    }
};

Linear linearizeSide(const Row* row) {
    if (!row) return { 0, 0, 0, false };
    LinearParser parser(row, 0, row->items.size());
    return parser.parseRow();
}

} // namespace

double evaluate(const Row* root, const EvaluationContext& context) {
    if (!root) return 0.0;
    RowParser p(root, context);
    return p.parseRow();
}

double evaluate(const Row* root) {
    return evaluate(root, EvaluationContext{});
}

std::string evaluateToString(const Row* root, const EvaluationContext& context) {
    try {
        double v = evaluate(root, context);
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

std::string evaluateToString(const Row* root) {
    return evaluateToString(root, EvaluationContext{});
}

QuadraticResult solveQuadratic(double a, double b, double c) {
    QuadraticResult result;
    constexpr double epsilon = 1e-12;
    if (std::fabs(a) < epsilon) {
        if (std::fabs(b) < epsilon) {
            result.valid = false;
            result.message = std::fabs(c) < epsilon ? "Every value is a solution" : "No solution";
            return result;
        }
        result.rootCount = 1;
        result.first = -c / b;
        return result;
    }

    double discriminant = b * b - 4.0 * a * c;
    if (discriminant < -epsilon) {
        result.rootCount = 0;
        result.message = "No real solutions";
        return result;
    }
    if (std::fabs(discriminant) <= epsilon) {
        result.rootCount = 1;
        result.first = -b / (2.0 * a);
        return result;
    }
    double root = std::sqrt(discriminant);
    result.rootCount = 2;
    result.first = (-b + root) / (2.0 * a);
    result.second = (-b - root) / (2.0 * a);
    return result;
}

bool solveTwoVariableSystem(const Row* first, const Row* second,
                            double& x, double& y, std::string& message) {
    auto equation = [](const Row* row, Linear& left, Linear& right) {
        if (!row) return false;
        size_t equals = row->items.size();
        for (size_t i = 0; i < row->items.size(); ++i) {
            if (row->items[i]->type == ItemType::Equals) {
                if (equals != row->items.size()) return false;
                equals = i;
            }
        }
        if (equals == 0 || equals + 1 >= row->items.size()) return false;
        LinearParser leftParser(row, 0, equals);
        LinearParser rightParser(row, equals + 1, row->items.size());
        left = leftParser.parseRow();
        right = rightParser.parseRow();
        return left.valid && right.valid;
    };

    Linear left1, right1, left2, right2;
    if (!equation(first, left1, right1) || !equation(second, left2, right2)) {
        message = "Use two linear equations with one '=' each";
        return false;
    }
    double a1 = left1.x - right1.x;
    double b1 = left1.y - right1.y;
    double c1 = right1.constant - left1.constant;
    double a2 = left2.x - right2.x;
    double b2 = left2.y - right2.y;
    double c2 = right2.constant - left2.constant;
    double determinant = a1 * b2 - a2 * b1;
    if (std::fabs(determinant) < 1e-12) {
        message = "No unique solution";
        return false;
    }
    x = (c1 * b2 - c2 * b1) / determinant;
    y = (a1 * c2 - a2 * c1) / determinant;
    message.clear();
    return true;
}
