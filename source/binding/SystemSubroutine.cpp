//------------------------------------------------------------------------------
// SystemSubroutine.cpp
// System-defined subroutine handling
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#include "slang/binding/SystemSubroutine.h"

#include "slang/binding/Expression.h"
#include "slang/compilation/Compilation.h"
#include "slang/diagnostics/ExpressionsDiags.h"
#include "slang/diagnostics/SysFuncsDiags.h"
#include "slang/symbols/ASTVisitor.h"

namespace slang {

bool SystemSubroutine::allowEmptyArgument(size_t) const {
    return false;
}

bool SystemSubroutine::allowClockingArgument(size_t) const {
    return false;
}

const Expression& SystemSubroutine::bindArgument(size_t, const BindContext& context,
                                                 const ExpressionSyntax& syntax,
                                                 const Args&) const {
    return Expression::bind(syntax, context);
}

string_view SystemSubroutine::kindStr() const {
    return kind == SubroutineKind::Task ? "task"sv : "function"sv;
}

bool SystemSubroutine::checkArgCount(const BindContext& context, bool isMethod, const Args& args,
                                     SourceRange callRange, size_t min, size_t max) const {
    size_t provided = args.size();
    if (isMethod) {
        ASSERT(provided);
        provided--;
    }

    if (provided < min) {
        context.addDiag(diag::TooFewArguments, callRange) << name << min << provided;
        return false;
    }

    if (provided > max) {
        context.addDiag(diag::TooManyArguments, args[max]->sourceRange) << name << max << provided;
        return false;
    }

    for (auto arg : args) {
        if (arg->bad())
            return false;
    }

    return true;
}

const Type& SystemSubroutine::badArg(const BindContext& context, const Expression& arg) const {
    context.addDiag(diag::BadSystemSubroutineArg, arg.sourceRange) << *arg.type << kindStr();
    return context.getCompilation().getErrorType();
}

bool SystemSubroutine::notConst(EvalContext& context, SourceRange range) const {
    context.addDiag(diag::SysFuncNotConst, range) << name;
    return false;
}

bool SystemSubroutine::noHierarchical(EvalContext& context, const Expression& expr) const {
    if (expr.hasHierarchicalReference() &&
        !context.compilation.getOptions().allowHierarchicalConst &&
        !context.flags.has(EvalFlags::IsScript)) {
        context.addDiag(diag::SysFuncHierarchicalNotAllowed, expr.sourceRange) << name;
        return false;
    }

    return true;
}

const Expression& SimpleSystemSubroutine::bindArgument(size_t argIndex, const BindContext& context,
                                                       const ExpressionSyntax& syntax,
                                                       const Args& args) const {
    if (isMethod)
        argIndex--;

    if (argIndex >= argTypes.size())
        return SystemSubroutine::bindArgument(argIndex, context, syntax, args);

    return Expression::bindArgument(*argTypes[argIndex], ArgumentDirection::In, syntax, context);
}

const Type& SimpleSystemSubroutine::checkArguments(const BindContext& context, const Args& args,
                                                   SourceRange range, const Expression*) const {
    auto& comp = context.getCompilation();
    if (!checkArgCount(context, isMethod, args, range, requiredArgs, argTypes.size()))
        return comp.getErrorType();

    if (isFirstArgLValue && !args.empty() && !args[0]->requireLValue(context))
        return comp.getErrorType();

    return *returnType;
}

} // namespace slang
