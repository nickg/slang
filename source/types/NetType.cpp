//------------------------------------------------------------------------------
// NetType.cpp
// Type class for nettypes
//
// File is under the MIT license; see LICENSE for details
//------------------------------------------------------------------------------
#include "slang/types/NetType.h"

#include "slang/binding/BindContext.h"
#include "slang/compilation/Compilation.h"
#include "slang/diagnostics/LookupDiags.h"
#include "slang/diagnostics/TypesDiags.h"
#include "slang/symbols/ASTSerializer.h"
#include "slang/symbols/Scope.h"
#include "slang/symbols/SubroutineSymbols.h"
#include "slang/symbols/VariableSymbols.h"
#include "slang/syntax/AllSyntax.h"
#include "slang/types/Type.h"

namespace slang {

NetType::NetType(NetKind netKind, string_view name, const Type& dataType) :
    Symbol(SymbolKind::NetType, name, SourceLocation()), declaredType(*this), netKind(netKind) {
    declaredType.setType(dataType);
}

NetType::NetType(string_view name, SourceLocation location) :
    Symbol(SymbolKind::NetType, name, location),
    declaredType(*this, DeclaredTypeFlags::UserDefinedNetType), netKind(UserDefined) {
}

static void validateResolver(const NetType& netType, const SubroutineSymbol& resolver,
                             SourceRange range, const BindContext& context) {
    auto& netTypeType = netType.declaredType.getType();
    if (netType.isError() || netTypeType.isError())
        return;

    auto reportError = [&](DiagCode code) -> Diagnostic& {
        auto& diag = context.addDiag(code, range);
        diag << netType.name;
        diag.addNote(diag::NoteDeclarationHere, resolver.location);
        return diag;
    };

    auto args = resolver.getArguments();
    if (args.size() != 1) {
        reportError(diag::NTResolveSingleArg) << netTypeType;
        return;
    }

    auto& retType = resolver.getReturnType();
    auto& argType = args[0]->getType().getCanonicalType();
    if (retType.isError() || argType.isError())
        return;

    if (resolver.subroutineKind != SubroutineKind::Function) {
        reportError(diag::NTResolveTask);
        return;
    }

    auto resolverParent = resolver.getParentScope();
    ASSERT(resolverParent);
    if (!resolver.flags.has(MethodFlags::Static) &&
        resolverParent->asSymbol().kind == SymbolKind::ClassType) {
        reportError(diag::NTResolveClass);
        return;
    }

    if (resolver.flags.has(MethodFlags::InterfaceImport | MethodFlags::DPIImport |
                           MethodFlags::NotConst | MethodFlags::Randomize)) {
        reportError(diag::NTResolveUserDef);
        return;
    }

    if (!netTypeType.isMatching(retType)) {
        reportError(diag::NTResolveReturn) << netTypeType;
        return;
    }

    if (argType.kind != SymbolKind::DynamicArrayType ||
        args[0]->direction != ArgumentDirection::In ||
        !argType.getArrayElementType()->isMatching(netTypeType)) {
        reportError(diag::NTResolveSingleArg) << netTypeType;
        return;
    }

    resolver.getBody();
    if (auto driver = args[0]->getFirstDriver()) {
        auto& diag = context.addDiag(diag::NTResolveArgModify, driver->sourceRange);
        diag << netType.name << args[0]->name;
        diag.addNote(diag::NoteReferencedHere, range);
    }
}

const SubroutineSymbol* NetType::getResolutionFunction() const {
    if (resolver)
        return *resolver;

    auto syntax = getSyntax();
    auto scope = getParentScope();
    ASSERT(syntax && scope);

    auto& declSyntax = syntax->as<NetTypeDeclarationSyntax>();
    if (declSyntax.withFunction) {
        BindContext context(*scope, LookupLocation::after(*this));
        LookupResult result;
        Lookup::name(*declSyntax.withFunction->name, context,
                     LookupFlags::ForceHierarchical | LookupFlags::NoSelectors, result);
        result.reportDiags(context);

        auto symbol = result.found;
        if (symbol) {
            SourceRange range = declSyntax.withFunction->name->sourceRange();
            if (symbol->kind != SymbolKind::Subroutine) {
                auto& diag = scope->addDiag(diag::NotASubroutine, range);
                diag << symbol->name;
                diag.addNote(diag::NoteDeclarationHere, symbol->location);
            }
            else {
                resolver = &symbol->as<SubroutineSymbol>();
                validateResolver(*this, *resolver.value(), range, context);
                return *resolver;
            }
        }
    }

    resolver = nullptr;
    return *resolver;
}

void NetType::serializeTo(ASTSerializer& serializer) const {
    serializer.write("type", getDataType());
}

NetType& NetType::fromSyntax(const Scope& scope, const NetTypeDeclarationSyntax& syntax) {
    auto& comp = scope.getCompilation();
    auto result = comp.emplace<NetType>(syntax.name.valueText(), syntax.name.location());
    result->setSyntax(syntax);
    result->setAttributes(scope, syntax.attributes);
    result->declaredType.setTypeSyntax(*syntax.type);

    return *result;
}

const NetType& NetType::getSimulatedNetType(const NetType& internal, const NetType& external,
                                            bool& shouldWarn) {
    shouldWarn = false;
    switch (internal.netKind) {
        case Unknown:
        case UserDefined:
            return internal;
        case Wire:
        case Tri:
        case Interconnect:
            return external;
        case WAnd:
        case TriAnd:
            switch (external.netKind) {
                case Wire:
                case Tri:
                case Interconnect:
                    return internal;
                case WOr:
                case TriOr:
                case TriReg:
                case Tri0:
                case Tri1:
                case UWire:
                    shouldWarn = true;
                    break;
                default:
                    break;
            }
            return external;
        case WOr:
        case TriOr:
            switch (external.netKind) {
                case Wire:
                case Tri:
                case Interconnect:
                    return internal;
                case WAnd:
                case TriAnd:
                case TriReg:
                case Tri0:
                case Tri1:
                case UWire:
                    shouldWarn = true;
                    break;
                default:
                    break;
            }
            return external;
        case TriReg:
            switch (external.netKind) {
                case Wire:
                case Tri:
                case Interconnect:
                    return internal;
                case WAnd:
                case TriAnd:
                case WOr:
                case TriOr:
                case UWire:
                    shouldWarn = true;
                    break;
                default:
                    break;
            }
            return external;
        case Tri0:
            switch (external.netKind) {
                case Wire:
                case Tri:
                case TriReg:
                case Interconnect:
                    return internal;
                case WAnd:
                case TriAnd:
                case WOr:
                case TriOr:
                case UWire:
                case Tri1:
                    shouldWarn = true;
                    break;
                default:
                    break;
            }
            return external;
        case Tri1:
            switch (external.netKind) {
                case Wire:
                case Tri:
                case TriReg:
                case Interconnect:
                    return internal;
                case WAnd:
                case TriAnd:
                case WOr:
                case TriOr:
                case UWire:
                case Tri0:
                    shouldWarn = true;
                    break;
                default:
                    break;
            }
            return external;
        case UWire:
            switch (external.netKind) {
                case UWire:
                case Supply0:
                case Supply1:
                    return external;
                case WAnd:
                case TriAnd:
                case WOr:
                case TriOr:
                case TriReg:
                case Tri0:
                case Tri1:
                    shouldWarn = true;
                    break;
                default:
                    break;
            }
            return internal;
        case Supply0:
            switch (external.netKind) {
                case Supply0:
                    return external;
                case Supply1:
                    shouldWarn = true;
                    return external;
                default:
                    break;
            }
            return internal;
        case Supply1:
            switch (external.netKind) {
                case Supply0:
                    shouldWarn = true;
                    return external;
                case Supply1:
                    return external;
                default:
                    break;
            }
            return internal;
        default:
            THROW_UNREACHABLE;
    }
}

} // namespace slang
