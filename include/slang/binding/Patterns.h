//------------------------------------------------------------------------------
//! @file PatternExpressions.h
//! @brief Definitions for pattern expressions
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#pragma once

#include "slang/binding/Expression.h"
#include "slang/util/Enum.h"
#include "slang/util/StackContainer.h"

namespace slang {

class FieldSymbol;
class PatternVarSymbol;

// clang-format off
#define PATTERN(x) \
    x(Invalid) \
    x(Wildcard) \
    x(Constant) \
    x(Variable) \
    x(Tagged) \
    x(Structure)
ENUM(PatternKind, PATTERN)
#undef PATTERN
// clang-format on

struct PatternSyntax;

/// Base class for "patterns", as used in pattern matching conditional
/// statements and expressions.
class Pattern {
public:
    /// The kind of pattern represented by this instance.
    PatternKind kind;

    /// The syntax node used to create the pattern, if it came from source code.
    const SyntaxNode* syntax = nullptr;

    /// The source range where this pattern occurs, if it came from source code.
    SourceRange sourceRange;

    /// Returns true if the pattern had an error and is therefore invalid.
    bool bad() const { return kind == PatternKind::Invalid; }

    using VarMap = SmallMap<string_view, const PatternVarSymbol*, 4>;

    static Pattern& bind(const PatternSyntax& syntax, const Type& targetType, VarMap& varMap,
                         BindContext& context);

    /// Evaluates the pattern under the given evaluation context. Any errors that occur
    /// will be stored in the evaluation context instead of issued to the compilation.
    ConstantValue eval(EvalContext& context, const ConstantValue& value) const;

    template<typename T>
    T& as() {
        ASSERT(T::isKind(kind));
        return *static_cast<T*>(this);
    }

    template<typename T>
    const T& as() const {
        ASSERT(T::isKind(kind));
        return *static_cast<const T*>(this);
    }

    template<typename TVisitor, typename... Args>
    decltype(auto) visit(TVisitor& visitor, Args&&... args) const;

protected:
    Pattern(PatternKind kind, SourceRange sourceRange) : kind(kind), sourceRange(sourceRange) {}

    static Pattern& badPattern(Compilation& compilation, const Pattern* child);
};

class InvalidPattern : public Pattern {
public:
    const Pattern* child;

    explicit InvalidPattern(const Pattern* child) :
        Pattern(PatternKind::Invalid, SourceRange()), child(child) {}

    static bool isKind(PatternKind kind) { return kind == PatternKind::Invalid; }

    void serializeTo(ASTSerializer& serializer) const;
};

struct WildcardPatternSyntax;

class WildcardPattern : public Pattern {
public:
    explicit WildcardPattern(SourceRange sourceRange) :
        Pattern(PatternKind::Wildcard, sourceRange) {}

    static Pattern& fromSyntax(const WildcardPatternSyntax& syntax, const BindContext& context);

    ConstantValue evalImpl(EvalContext& context, const ConstantValue& value) const;

    static bool isKind(PatternKind kind) { return kind == PatternKind::Wildcard; }

    void serializeTo(ASTSerializer&) const {}
};

struct ExpressionPatternSyntax;

class ConstantPattern : public Pattern {
public:
    const Expression& expr;

    ConstantPattern(const Expression& expr, SourceRange sourceRange) :
        Pattern(PatternKind::Constant, sourceRange), expr(expr) {}

    static Pattern& fromSyntax(const ExpressionPatternSyntax& syntax, const Type& targetType,
                               const BindContext& context);

    ConstantValue evalImpl(EvalContext& context, const ConstantValue& value) const;

    static bool isKind(PatternKind kind) { return kind == PatternKind::Constant; }

    void serializeTo(ASTSerializer& serializer) const;
};

struct VariablePatternSyntax;

class VariablePattern : public Pattern {
public:
    const PatternVarSymbol& variable;

    VariablePattern(const PatternVarSymbol& variable, SourceRange sourceRange) :
        Pattern(PatternKind::Variable, sourceRange), variable(variable) {}

    static Pattern& fromSyntax(const VariablePatternSyntax& syntax, const Type& targetType,
                               VarMap& varMap, BindContext& context);

    ConstantValue evalImpl(EvalContext& context, const ConstantValue& value) const;

    static bool isKind(PatternKind kind) { return kind == PatternKind::Variable; }

    void serializeTo(ASTSerializer& serializer) const;
};

struct TaggedPatternSyntax;

class TaggedPattern : public Pattern {
public:
    const FieldSymbol& member;
    const Pattern* valuePattern;

    TaggedPattern(const FieldSymbol& member, const Pattern* valuePattern, SourceRange sourceRange) :
        Pattern(PatternKind::Tagged, sourceRange), member(member), valuePattern(valuePattern) {}

    static Pattern& fromSyntax(const TaggedPatternSyntax& syntax, const Type& targetType,
                               VarMap& varMap, BindContext& context);

    ConstantValue evalImpl(EvalContext& context, const ConstantValue& value) const;

    static bool isKind(PatternKind kind) { return kind == PatternKind::Tagged; }

    void serializeTo(ASTSerializer& serializer) const;
};

struct StructurePatternSyntax;

class StructurePattern : public Pattern {
public:
    struct FieldPattern {
        not_null<const FieldSymbol*> field;
        not_null<const Pattern*> pattern;
    };

    span<const FieldPattern> patterns;

    StructurePattern(span<const FieldPattern> patterns, SourceRange sourceRange) :
        Pattern(PatternKind::Structure, sourceRange), patterns(patterns) {}

    static Pattern& fromSyntax(const StructurePatternSyntax& syntax, const Type& targetType,
                               VarMap& varMap, BindContext& context);

    ConstantValue evalImpl(EvalContext& context, const ConstantValue& value) const;

    static bool isKind(PatternKind kind) { return kind == PatternKind::Structure; }

    void serializeTo(ASTSerializer& serializer) const;
};

} // namespace slang
