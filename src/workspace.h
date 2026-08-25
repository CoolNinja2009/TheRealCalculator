// workspace.h
//
// The scrollable multiline workspace: a list of completed (expression,
// result) entries, each keeping its own fully-structured expression tree
// so previous calculations still render as proper math (not flattened
// text), plus one live editable Expression at the bottom.

#pragma once
#include "expr_tree.h"
#include "evaluator.h"
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
    bool commitCurrent(const EvaluationContext& context = EvaluationContext{});

    // Recall the previous committed expression into the editable line.
    bool recallPrevious();

    bool hasSolvedValues() const { return hasSolvedValues_; }
    const EvaluationContext& solvedValues() const { return solvedValues_; }

    void clearVariables() {
        solvedValues_ = EvaluationContext{};
        hasSolvedValues_ = true;
    }

    void clearAll() {
        history_.clear();
        current_ = std::make_unique<Expression>();
        recallIndex_ = 0;
        hasSolvedValues_ = false;
    }

private:
    std::vector<std::unique_ptr<HistoryEntry>> history_;
    std::unique_ptr<Expression> current_;
    size_t recallIndex_ = 0;
    EvaluationContext solvedValues_;
    bool hasSolvedValues_ = false;
};
