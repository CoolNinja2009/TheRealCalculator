// workspace.h
//
// The scrollable multiline workspace: a list of completed (expression,
// result) entries, each keeping its own fully-structured expression tree
// so previous calculations still render as proper math (not flattened
// text), plus one live editable Expression at the bottom.

#pragma once
#include "expr_tree.h"
#include <vector>
#include <memory>
#include <string>

struct HistoryEntry {
    std::unique_ptr<Expression> expr; // frozen, non-editable
    std::string result;               // formatted result or error text
    bool isError = false;
};

class Workspace {
public:
    Workspace() { current_ = std::make_unique<Expression>(); }

    Expression& current() { return *current_; }
    const std::vector<std::unique_ptr<HistoryEntry>>& history() const { return history_; }

    // Evaluate the current expression, push it (with its result) onto the
    // history list, and start a fresh empty expression. Returns false (and
    // does nothing) if the current expression is entirely empty.
    bool commitCurrent();

    void clearAll() {
        history_.clear();
        current_ = std::make_unique<Expression>();
    }

private:
    std::vector<std::unique_ptr<HistoryEntry>> history_;
    std::unique_ptr<Expression> current_;
};
