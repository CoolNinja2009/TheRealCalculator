// workspace.cpp
#include "workspace.h"
#include "evaluator.h"
#include <cstdio>

bool Workspace::commitCurrent(const EvaluationContext& context) {
    if (rowIsEmpty(current_->root.get())) return false;
    solvedValues_ = context;
    hasSolvedValues_ = true;

    auto entry = std::make_unique<HistoryEntry>();
    entry->expr = std::move(current_);

    if (hasEquals(entry->expr->root.get())) {
        QuadraticResult quadratic;
        char variable = 0;
        double value = 0.0;
        std::string message;
        if (solveQuadraticEquation(entry->expr->root.get(), quadratic, message)) {
            if (quadratic.rootCount == 0) entry->result = quadratic.message;
            else if (quadratic.rootCount == 1) {
                char result[96];
                std::snprintf(result, sizeof(result), "x = %.10g", quadratic.first);
                entry->result = result;
            } else {
                char result[128];
                std::snprintf(result, sizeof(result), "x1 = %.10g, x2 = %.10g", quadratic.first, quadratic.second);
                entry->result = result;
            }
            entry->isError = false;
        } else if (solveVariableAssignment(entry->expr->root.get(), context, variable, value, message)) {
            char result[96];
            std::snprintf(result, sizeof(result), "%c = %.10g", variable, value);
            entry->result = result;
            entry->isError = false;
            if (variable == 'x') solvedValues_.x = value;
            else solvedValues_.y = value;
        } else if (solveSingleVariableEquation(entry->expr->root.get(), variable, value, message)) {
            char result[96];
            std::snprintf(result, sizeof(result), "%c = %.10g (the other variable is free)", variable, value);
            entry->result = result;
            entry->isError = false;
            if (variable == 'x') solvedValues_.x = value;
            else solvedValues_.y = value;
        } else {
            entry->result = message;
            entry->isError = false;
        }
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
        isLinearEquation(history_[history_.size() - 2]->expr->root.get()) &&
        isLinearEquation(history_.back()->expr->root.get()) &&
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
            solvedValues_ = { x, y };
            hasSolvedValues_ = true;
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
