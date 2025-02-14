//------------------------------------------------------------------------------
//! @file SystemSubroutine.h
//! @brief System-defined subroutine handling
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#pragma once

#include "slang/binding/CallExpression.h"
#include "slang/binding/Expression.h"
#include "slang/symbols/SemanticFacts.h"
#include "slang/types/AllTypes.h"
#include "slang/util/SmallVector.h"
#include "slang/util/Util.h"

namespace slang {

namespace mir {
class Procedure;
}

class SystemSubroutine {
public:
    virtual ~SystemSubroutine() = default;

    using Args = span<const Expression* const>;

    std::string name;
    SubroutineKind kind;
    bool hasOutputArgs = false;

    enum class WithClauseMode { None, Iterator, Randomize };
    WithClauseMode withClauseMode = WithClauseMode::None;

    virtual bool allowEmptyArgument(size_t argIndex) const;
    virtual bool allowClockingArgument(size_t argIndex) const;
    virtual const Expression& bindArgument(size_t argIndex, const BindContext& context,
                                           const ExpressionSyntax& syntax,
                                           const Args& previousArgs) const;
    virtual const Type& checkArguments(const BindContext& context, const Args& args,
                                       SourceRange range, const Expression* iterOrThis) const = 0;
    virtual ConstantValue eval(EvalContext& context, const Args& args, SourceRange range,
                               const CallExpression::SystemCallInfo& callInfo) const = 0;

    virtual void lower(mir::Procedure&, const Args&) const {}

protected:
    SystemSubroutine(const std::string& name, SubroutineKind kind) : name(name), kind(kind) {}

    string_view kindStr() const;
    const Type& badArg(const BindContext& context, const Expression& arg) const;

    bool notConst(EvalContext& context, SourceRange range) const;
    bool noHierarchical(EvalContext& context, const Expression& expr) const;

    bool checkArgCount(const BindContext& context, bool isMethod, const Args& args,
                       SourceRange callRange, size_t min, size_t max) const;
};

/// An implementation of the SystemSubroutine interface that has
/// basic argument types and a well-defined return type.
class SimpleSystemSubroutine : public SystemSubroutine {
public:
    const Expression& bindArgument(size_t argIndex, const BindContext& context,
                                   const ExpressionSyntax& syntax,
                                   const Args& previousArgs) const final;
    const Type& checkArguments(const BindContext& context, const Args& args, SourceRange range,
                               const Expression* iterExpr) const final;

protected:
    SimpleSystemSubroutine(const std::string& name, SubroutineKind kind, size_t requiredArgs,
                           const std::vector<const Type*>& argTypes, const Type& returnType,
                           bool isMethod, bool isFirstArgLValue = false) :
        SystemSubroutine(name, kind),
        argTypes(argTypes), returnType(&returnType), requiredArgs(requiredArgs), isMethod(isMethod),
        isFirstArgLValue(isFirstArgLValue) {
        ASSERT(requiredArgs <= argTypes.size());
    }

private:
    std::vector<const Type*> argTypes;
    const Type* returnType;
    size_t requiredArgs;
    bool isMethod;
    bool isFirstArgLValue;
};

class NonConstantFunction : public SimpleSystemSubroutine {
public:
    NonConstantFunction(const std::string& name, const Type& returnType, size_t requiredArgs = 0,
                        const std::vector<const Type*>& argTypes = {}, bool isMethod = false) :
        SimpleSystemSubroutine(name, SubroutineKind::Function, requiredArgs, argTypes, returnType,
                               isMethod) {}

    ConstantValue eval(EvalContext& context, const Args&, SourceRange range,
                       const CallExpression::SystemCallInfo&) const final {
        notConst(context, range);
        return nullptr;
    }
};

} // namespace slang
