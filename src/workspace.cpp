// workspace.cpp
#include "workspace.h"
#include "evaluator.h"
#include <cstdio>

bool Workspace::commitCurrent(const EvaluationContext& context) {
    if (rowIsEmpty(current_->root.get())) return false;

    auto entry = std::make_unique<HistoryEntry>();
    entry->expr = std::move(current_);

    if (hasEquals(entry->expr->root.get())) {
        entry->result = "Equation stored";
        entry->isError = false;
    } else {
        try {
            double v = evaluate(entry->expr->root.get(), context);
            entry->result = evaluateToString(entry->expr->root.get(), context);
            entry->isError = false;
            (void)v;
        } catch (const std::exception& e) {
            entry->result = e.what();
            entry->isError = true;
        }
    }

    history_.push_back(std::move(entry));
    if (history_.size() >= 2 &&
        hasEquals(history_[history_.size() - 2]->expr->root.get()) &&
        hasEquals(history_.back()->expr->root.get())) {
        double x = 0.0, y = 0.0;
        std::string message;
        if (solveTwoVariableSystem(history_[history_.size() - 2]->expr->root.get(),
                                   history_.back()->expr->root.get(), x, y, message)) {
            char result[128];
            std::snprintf(result, sizeof(result), "x = %.10g, y = %.10g", x, y);
            history_.back()->result = result;
            history_.back()->isError = false;
        } else {
            history_.back()->result = message;
            history_.back()->isError = true;
        }
    }
    current_ = std::make_unique<Expression>();
    recallIndex_ = history_.size();
    return true;
}

bool Workspace::recallPrevious() {
    if (history_.empty()) return false;
    if (recallIndex_ > history_.size()) recallIndex_ = history_.size();
    if (recallIndex_ == 0) return false;
    --recallIndex_;
    current_ = cloneExpression(*history_[recallIndex_]->expr);
    return true;
}
