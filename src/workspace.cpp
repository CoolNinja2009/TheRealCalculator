// workspace.cpp
#include "workspace.h"
#include "evaluator.h"

bool Workspace::commitCurrent() {
    if (rowIsEmpty(current_->root.get())) return false;

    auto entry = std::make_unique<HistoryEntry>();
    entry->expr = std::move(current_);

    try {
        double v = evaluate(entry->expr->root.get());
        entry->result = evaluateToString(entry->expr->root.get());
        entry->isError = false;
        (void)v;
    } catch (const std::exception& e) {
        entry->result = e.what();
        entry->isError = true;
    }

    history_.push_back(std::move(entry));
    current_ = std::make_unique<Expression>();
    return true;
}
