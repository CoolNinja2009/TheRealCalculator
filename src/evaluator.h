// evaluator.h
//
// Evaluates a Row (see expr_tree.h) with full BODMAS/PEMDAS precedence.
// Note that '/' never appears as a flat operator in the tree -- pressing
// '/' always builds a Fraction structure at edit time (see
// insertFraction() in expr_tree.cpp), so a Fraction node IS the division:
// evaluating it is simply eval(numerator) / eval(denominator). This means
// precedence of "division" is automatically correct: it's as tightly
// bound as the atom the user wrapped, exactly like on paper.
//
// Grammar walked over a Row's items:
//   Row    := Term (('+' | '-') Term)*
//   Term   := Factor (ImplicitOrStar Factor)*      // '*' or bare adjacency
//   Factor := '-'? Atom
//   Atom   := Number | Fraction | Paren | Power | Sqrt
//
// Throws std::runtime_error with a short human-readable message on:
//   - division by zero
//   - square root of a negative number
//   - a malformed/incomplete expression (e.g. empty operand)

#pragma once
#include "expr_tree.h"
#include <string>

// Evaluate the whole expression. Throws std::runtime_error on error.
double evaluate(const Row* root);

// Convenience: evaluate and format to a display string (trims trailing
// zeros, switches to scientific notation for very large/small magnitudes).
// On error, returns the exception's message prefixed with an error glyph.
std::string evaluateToString(const Row* root);
