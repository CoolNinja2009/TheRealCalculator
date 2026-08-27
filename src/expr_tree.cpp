// expr_tree.cpp
#include "expr_tree.h"
#include <cassert>
#include <algorithm>

// ---------------------------------------------------------------- helpers

bool rowIsEmpty(const Row* r) { return r == nullptr || r->items.empty(); }

bool hasEquals(const Row* r) {
    if (!r) return false;
    for (const auto& item : r->items) {
        if (item->type == ItemType::Equals) return true;
    }
    return false;
}

static void attachRow(std::unique_ptr<Row>& slot, Item* owner, Row* ownerParentRow) {
    if (!slot) slot = std::make_unique<Row>();
    slot->owner = owner;
    slot->ownerParentRow = ownerParentRow;
}

// Find the index of `child` (a row belonging to some structural item) within
// its owner item's parent row, i.e. where the *item* sits. Returns -1 if
// child is the root row (no owner).
static int ownerIndexInParentRow(Row* child) {
    if (!child || !child->owner || !child->ownerParentRow) return -1;
    Row* parent = child->ownerParentRow;
    for (size_t i = 0; i < parent->items.size(); ++i) {
        if (parent->items[i].get() == child->owner) return (int)i;
    }
    return -1;
}

// Is `child` the "a" row (numerator/base/inner/radicand) of its owner?
static bool isARow(Row* child) {
    return child && child->owner && child->owner->a.get() == child;
}
static bool isBRow(Row* child) {
    return child && child->owner && child->owner->b.get() == child;
}

// ------------------------------------------------------------- Expression

Expression::Expression() {
    root = std::make_unique<Row>();
    root->owner = nullptr;
    root->ownerParentRow = nullptr;
    cursor.row = root.get();
    cursor.index = 0;
}

static void serializeRow(const Row* r, std::string& out);

static void serializeItem(const Item* it, std::string& out) {
    switch (it->type) {
        case ItemType::Number:
            out += it->numText;
            break;
        case ItemType::Variable:
            out += it->variableName;
            break;
        case ItemType::Operator:
            out += ' ';
            out += it->opChar;
            out += ' ';
            break;
        case ItemType::Equals:
            out += " = ";
            break;
        case ItemType::CloseParen:
            out += ')';
            break;
        case ItemType::Fraction:
            out += '(';
            serializeRow(it->a.get(), out);
            out += ")/(";
            serializeRow(it->b.get(), out);
            out += ')';
            break;
        case ItemType::Paren:
            out += '(';
            serializeRow(it->a.get(), out);
            out += ')';
            break;
        case ItemType::Power:
            out += '(';
            serializeRow(it->a.get(), out);
            out += ")^(";
            serializeRow(it->b.get(), out);
            out += ')';
            break;
        case ItemType::Sqrt:
            out += "sqrt(";
            serializeRow(it->a.get(), out);
            out += ')';
            break;
    }
}

static void serializeRow(const Row* r, std::string& out) {
    if (!r) return;
    for (auto& it : r->items) serializeItem(it.get(), out);
}

std::string Expression::toPlainString() const {
    std::string out;
    serializeRow(root.get(), out);
    return out;
}

std::string rowRangeToPlainString(const Row* row, int begin, int end) {
    std::string out;
    if (!row) return out;
    begin = std::max(0, begin);
    end = std::min((int)row->items.size(), end);
    for (int i = begin; i < end; ++i) serializeItem(row->items[i].get(), out);
    return out;
}

void deleteRange(Expression& expression, Cursor first, Cursor last) {
    if (!first.row || first.row != last.row) return;
    int begin = std::min(first.index, last.index);
    int end = std::max(first.index, last.index);
    begin = std::max(0, begin);
    end = std::min((int)first.row->items.size(), end);
    if (begin >= end) return;
    first.row->items.erase(first.row->items.begin() + begin,
                           first.row->items.begin() + end);
    expression.cursor.row = first.row;
    expression.cursor.index = begin;
}

static std::unique_ptr<Row> cloneRow(const Row* source, Item* owner, Row* parent) {
    auto copy = std::make_unique<Row>();
    copy->owner = owner;
    copy->ownerParentRow = parent;
    if (!source) return copy;
    for (const auto& sourceItem : source->items) {
        auto item = std::make_unique<Item>(sourceItem->type);
        item->numText = sourceItem->numText;
        item->opChar = sourceItem->opChar;
        item->variableName = sourceItem->variableName;
        Item* itemPtr = item.get();
        if (sourceItem->a) item->a = cloneRow(sourceItem->a.get(), itemPtr, copy.get());
        if (sourceItem->b) item->b = cloneRow(sourceItem->b.get(), itemPtr, copy.get());
        copy->items.push_back(std::move(item));
    }
    return copy;
}

std::unique_ptr<Expression> cloneExpression(const Expression& source) {
    auto copy = std::make_unique<Expression>();
    copy->root = cloneRow(source.root.get(), nullptr, nullptr);
    copy->cursor.row = copy->root.get();
    copy->cursor.index = (int)copy->root->items.size();
    return copy;
}

// ---------------------------------------------------------------- insert

void insertDigit(Expression& expr, char digit) {
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;

    if (idx > 0 && row->items[idx - 1]->type == ItemType::Number) {
        Item* prev = row->items[idx - 1].get();
        if (digit == '.' && prev->numText.find('.') != std::string::npos)
            return; // only one decimal point per number
        prev->numText += digit;
        return;
    }

    auto item = std::make_unique<Item>(ItemType::Number);
    item->numText = std::string(1, digit);
    row->items.insert(row->items.begin() + idx, std::move(item));
    expr.cursor.index = idx + 1;
}

void insertVariable(Expression& expr, char name) {
    if (name != 'x' && name != 'y') return;
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;
    auto item = std::make_unique<Item>(ItemType::Variable);
    item->variableName = name;
    row->items.insert(row->items.begin() + idx, std::move(item));
    expr.cursor.index = idx + 1;
}

void insertOperator(Expression& expr, char op) {
    if (op != '-' && isBRow(expr.cursor.row) && expr.cursor.row->owner &&
        expr.cursor.row->owner->type == ItemType::Power &&
        expr.cursor.index == (int)expr.cursor.row->items.size()) {
        moveRight(expr);
    }
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;
    auto item = std::make_unique<Item>(ItemType::Operator);
    item->opChar = op;
    row->items.insert(row->items.begin() + idx, std::move(item));
    expr.cursor.index = idx + 1;
}

void insertEquals(Expression& expr) {
    if (isBRow(expr.cursor.row) && expr.cursor.row->owner &&
        expr.cursor.row->owner->type == ItemType::Power &&
        expr.cursor.index == (int)expr.cursor.row->items.size()) {
        moveRight(expr);
    }
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;
    auto item = std::make_unique<Item>(ItemType::Equals);
    row->items.insert(row->items.begin() + idx, std::move(item));
    expr.cursor.index = idx + 1;
}

// Consume the atom immediately to the left of the cursor (a whole Number
// item, or a whole structural item) and return it, adjusting cursor.index
// and the row in the process. Returns nullptr (and leaves cursor alone) if
// there is nothing consumable (start of row, or previous is an Operator).
static std::unique_ptr<Item> consumeLeftAtom(Expression& expr) {
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;
    if (idx == 0) return nullptr;
    Item* prev = row->items[idx - 1].get();
    if (prev->type == ItemType::Operator) return nullptr;

    std::unique_ptr<Item> taken = std::move(row->items[idx - 1]);
    row->items.erase(row->items.begin() + (idx - 1));
    expr.cursor.index = idx - 1;
    return taken;
}

void insertFraction(Expression& expr) {
    Row* row = expr.cursor.row;
    int insertPos = expr.cursor.index;

    auto fracItem = std::make_unique<Item>(ItemType::Fraction);
    Item* fracPtr = fracItem.get();

    std::unique_ptr<Item> consumed = consumeLeftAtom(expr);
    insertPos = expr.cursor.index; // consumeLeftAtom may have shifted it

    attachRow(fracItem->a, fracPtr, row);
    attachRow(fracItem->b, fracPtr, row);
    if (consumed) fracItem->a->items.push_back(std::move(consumed));

    row->items.insert(row->items.begin() + insertPos, std::move(fracItem));

    // cursor -> start of denominator
    expr.cursor.row = fracPtr->b.get();
    expr.cursor.index = 0;
}

void insertPower(Expression& expr) {
    Row* row = expr.cursor.row;
    int insertPos = expr.cursor.index;

    auto powItem = std::make_unique<Item>(ItemType::Power);
    Item* powPtr = powItem.get();

    std::unique_ptr<Item> consumed = consumeLeftAtom(expr);
    insertPos = expr.cursor.index;

    attachRow(powItem->a, powPtr, row);
    attachRow(powItem->b, powPtr, row);
    if (consumed) powItem->a->items.push_back(std::move(consumed));

    row->items.insert(row->items.begin() + insertPos, std::move(powItem));

    // cursor -> start of exponent
    expr.cursor.row = powPtr->b.get();
    expr.cursor.index = 0;
}

void insertSqrt(Expression& expr) {
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;

    auto sqItem = std::make_unique<Item>(ItemType::Sqrt);
    Item* sqPtr = sqItem.get();
    attachRow(sqItem->a, sqPtr, row);

    row->items.insert(row->items.begin() + idx, std::move(sqItem));
    expr.cursor.row = sqPtr->a.get();
    expr.cursor.index = 0;
}

void insertOpenParen(Expression& expr) {
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;

    auto pItem = std::make_unique<Item>(ItemType::Paren);
    Item* pPtr = pItem.get();
    attachRow(pItem->a, pPtr, row);

    row->items.insert(row->items.begin() + idx, std::move(pItem));
    expr.cursor.row = pPtr->a.get();
    expr.cursor.index = 0;
}

void insertCloseParen(Expression& expr) {
    Row* row = expr.cursor.row;
    if (row->owner && row->owner->type == ItemType::Paren &&
        row->owner->a.get() == row &&
        expr.cursor.index == (int)row->items.size()) {
        int k = ownerIndexInParentRow(row);
        if (k >= 0) {
            expr.cursor.row = row->ownerParentRow;
            expr.cursor.index = k + 1;
            return;
        }
    }
    Row* parent = expr.cursor.row;
    int index = expr.cursor.index;
    auto item = std::make_unique<Item>(ItemType::CloseParen);
    parent->items.insert(parent->items.begin() + index, std::move(item));
    expr.cursor.index = index + 1;
}

// ---------------------------------------------------------------- motion

void moveLeft(Expression& expr) {
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;

    if (idx > 0) {
        Item* prev = row->items[idx - 1].get();
        switch (prev->type) {
            case ItemType::Number:
            case ItemType::Variable:
            case ItemType::Equals:
            case ItemType::CloseParen:
            case ItemType::Operator:
                expr.cursor.index = idx - 1;
                return;
            case ItemType::Fraction:
            case ItemType::Power:
            case ItemType::Paren:
            case ItemType::Sqrt:
                // Enter from the right -> land at the end of its primary
                // (leftmost/top) row.
                expr.cursor.row = prev->a.get();
                expr.cursor.index = (int)prev->a->items.size();
                return;
        }
    }

    // idx == 0: step out of the current structure, if any.
    if (row->owner) {
        int k = ownerIndexInParentRow(row);
        if (k >= 0) {
            expr.cursor.row = row->ownerParentRow;
            expr.cursor.index = k; // land just before the owning item
        }
    }
    // else: already at the very start of the whole expression; no-op.
}

void moveRight(Expression& expr) {
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;

    if (idx < (int)row->items.size()) {
        Item* next = row->items[idx].get();
        switch (next->type) {
            case ItemType::Number:
            case ItemType::Variable:
            case ItemType::Equals:
            case ItemType::CloseParen:
            case ItemType::Operator:
                expr.cursor.index = idx + 1;
                return;
            case ItemType::Fraction:
            case ItemType::Power:
            case ItemType::Paren:
            case ItemType::Sqrt:
                expr.cursor.row = next->a.get();
                expr.cursor.index = 0;
                return;
        }
    }

    // idx == end of row: step out of the current structure, if any.
    if (row->owner) {
        int k = ownerIndexInParentRow(row);
        if (k >= 0) {
            expr.cursor.row = row->ownerParentRow;
            expr.cursor.index = k + 1; // land just after the owning item
        }
    }
    // else: already at the very end of the whole expression; no-op.
}

void moveUp(Expression& expr) {
    Row* row = expr.cursor.row;
    if (isBRow(row)) { // in denominator/exponent -> go to numerator/base
        Item* owner = row->owner;
        Row* target = owner->a.get();
        expr.cursor.row = target;
        expr.cursor.index = (int)target->items.size() < expr.cursor.index
                                 ? (int)target->items.size()
                                 : expr.cursor.index;
        if (expr.cursor.index > (int)target->items.size())
            expr.cursor.index = (int)target->items.size();
    }
    // In a-row, or in a single-row structure (Paren/Sqrt), or in root:
    // no vertical sibling to move to (simplification).
}

void moveDown(Expression& expr) {
    Row* row = expr.cursor.row;
    if (isARow(row) && row->owner &&
        (row->owner->type == ItemType::Fraction || row->owner->type == ItemType::Power)) {
        Item* owner = row->owner;
        Row* target = owner->b.get();
        expr.cursor.row = target;
        if (expr.cursor.index > (int)target->items.size())
            expr.cursor.index = (int)target->items.size();
    }
}

// -------------------------------------------------------------- deletion

void backspace(Expression& expr) {
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;

    if (idx == 0) {
        if (!row->owner) return; // start of whole expression

        if (isBRow(row)) {
            // Backspace at the very start of a denominator/exponent steps
            // back into the end of the numerator/base, matching natural
            // calculators (no data is lost).
            Row* a = row->owner->a.get();
            expr.cursor.row = a;
            expr.cursor.index = (int)a->items.size();
            return;
        }

        // At the leftmost position of the item's primary row (numerator/
        // base/inner/radicand): remove the whole structural item. This is
        // always safe/non-corrupting -- either it's empty, or the user is
        // deleting a structure they just opened.
        int k = ownerIndexInParentRow(row);
        if (k >= 0) {
            Row* parent = row->ownerParentRow;
            parent->items.erase(parent->items.begin() + k);
            expr.cursor.row = parent;
            expr.cursor.index = k;
        }
        return;
    }

    Item* target = row->items[idx - 1].get();
    if (target->type == ItemType::Number) {
        if (target->numText.size() > 1) {
            target->numText.pop_back();
        } else {
            row->items.erase(row->items.begin() + (idx - 1));
            expr.cursor.index = idx - 1;
        }
        return;
    }
    if (target->type == ItemType::Variable) {
        row->items.erase(row->items.begin() + (idx - 1));
        expr.cursor.index = idx - 1;
        return;
    }
    if (target->type == ItemType::Operator || target->type == ItemType::CloseParen ||
        target->type == ItemType::Equals) {
        row->items.erase(row->items.begin() + (idx - 1));
        expr.cursor.index = idx - 1;
        return;
    }

    // Structural neighbor.
    bool aEmpty = rowIsEmpty(target->a.get());
    bool bEmpty = (target->type == ItemType::Fraction || target->type == ItemType::Power)
                      ? rowIsEmpty(target->b.get())
                      : true;
    if (aEmpty && bEmpty) {
        row->items.erase(row->items.begin() + (idx - 1));
        expr.cursor.index = idx - 1;
        return;
    }

    // Non-empty structure: first backspace "enters" it (lands at the end
    // of its rightmost row) rather than deleting its contents outright.
    Row* enter = (target->type == ItemType::Fraction || target->type == ItemType::Power)
                     ? target->b.get()
                     : target->a.get();
    expr.cursor.row = enter;
    expr.cursor.index = (int)enter->items.size();
}

void doDelete(Expression& expr) {
    Row* row = expr.cursor.row;
    int idx = expr.cursor.index;

    if (idx == (int)row->items.size()) {
        if (!row->owner) return; // end of whole expression

        if (isARow(row) && row->owner &&
            (row->owner->type == ItemType::Fraction || row->owner->type == ItemType::Power)) {
            // Delete at the very end of numerator/base steps forward into
            // the start of denominator/exponent.
            Row* b = row->owner->b.get();
            expr.cursor.row = b;
            expr.cursor.index = 0;
            return;
        }

        int k = ownerIndexInParentRow(row);
        if (k >= 0) {
            Row* parent = row->ownerParentRow;
            parent->items.erase(parent->items.begin() + k);
            expr.cursor.row = parent;
            expr.cursor.index = k;
        }
        return;
    }

    Item* target = row->items[idx].get();
    if (target->type == ItemType::Number) {
        if (target->numText.size() > 1) {
            target->numText.erase(target->numText.begin());
        } else {
            row->items.erase(row->items.begin() + idx);
        }
        return;
    }
    if (target->type == ItemType::Variable) {
        row->items.erase(row->items.begin() + idx);
        return;
    }
    if (target->type == ItemType::Operator || target->type == ItemType::CloseParen ||
        target->type == ItemType::Equals) {
        row->items.erase(row->items.begin() + idx);
        return;
    }

    bool aEmpty = rowIsEmpty(target->a.get());
    bool bEmpty = (target->type == ItemType::Fraction || target->type == ItemType::Power)
                      ? rowIsEmpty(target->b.get())
                      : true;
    if (aEmpty && bEmpty) {
        row->items.erase(row->items.begin() + idx);
        return;
    }

    // Enter the structure from its primary (leftmost) row instead of
    // deleting its contents outright.
    expr.cursor.row = target->a.get();
    expr.cursor.index = 0;
}
