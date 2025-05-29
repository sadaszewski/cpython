#include "Python.h"
#include "pycore_ast.h"           // _PyAST_GetDocString()
#include "pycore_format.h"        // F_LJUST
#include "pycore_long.h"          // _PyLong
#include "pycore_pystate.h"       // _PyThreadState_GET()
#include "pycore_setobject.h"     // _PySet_NextEntry()

#include "pycore_walker.h"

#define EXTRAS expr_context_ty ctx, AST_WALKER_CALLBACK callback, void *userdata
#define EXTRA_3 ctx, callback, userdata
#define EXTRA_2 callback, userdata

#define CHECK_NULL(x) if ((x) == NULL) { return true; }

bool ast_walker_expr(expr_ty node, EXTRAS);

static bool ast_walker_expr_seq(asdl_expr_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_expr(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_identifier(identifier id, EXTRAS) {
    CHECK_NULL(id);
    walk_node_ty walk_node = { .Identifier = id };
    return callback(WalkIdentifier_kind, walk_node, ctx, userdata, CallbackSingle_kind);
}

static bool ast_walker_arg(arg_ty arg, EXTRAS) {
    CHECK_NULL(arg);
    return (
        ast_walker_identifier(arg->arg, EXTRA_3) &&
        ast_walker_expr(arg->annotation, EXTRA_3)
    );
}

static bool ast_walker_arg_seq(asdl_arg_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_arg(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_arguments(arguments_ty args, EXTRAS) {
    CHECK_NULL(args);
    return (
        ast_walker_arg_seq(args->posonlyargs, EXTRA_3) &&
        ast_walker_arg_seq(args->args, EXTRA_3) &&
        ast_walker_arg(args->vararg, EXTRA_3) &&
        ast_walker_arg_seq(args->kwonlyargs, EXTRA_3) &&
        ast_walker_expr_seq(args->kw_defaults, EXTRA_3) &&
        ast_walker_arg(args->kwarg, EXTRA_3) &&
        ast_walker_expr_seq(args->defaults, EXTRA_3) 
    );
}

static bool ast_walker_compr(comprehension_ty compr, EXTRAS) {
    CHECK_NULL(compr);
    return (
        ast_walker_expr(compr->target, EXTRA_3) &&
        ast_walker_expr(compr->iter, EXTRA_3) &&
        ast_walker_expr_seq(compr->ifs, EXTRA_3)
    );
}

static bool ast_walker_compr_seq(asdl_comprehension_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        comprehension_ty item = asdl_seq_GET(seq, i);
        if (!ast_walker_compr(item, EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_keyword(keyword_ty kw, EXTRAS) {
    CHECK_NULL(kw);
    return (
        ast_walker_identifier(kw->arg, EXTRA_3) &&
        ast_walker_expr(kw->value, EXTRA_3)
    );
}

static bool ast_walker_keyword_seq(asdl_keyword_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_keyword(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

bool ast_walker_expr(expr_ty node, EXTRAS) {
    CHECK_NULL(node);

    walk_node_ty walk_node = { .Expr = node };
    if (!callback(WalkExpr_kind, walk_node, ctx, userdata, CallbackEarly_kind)) {
        return false;
    }

    bool res = true;
    switch (node->kind) {
        case BoolOp_kind:
            res = ast_walker_expr_seq(node->v.BoolOp.values, EXTRA_3);
            break;
        case NamedExpr_kind:
            res = (
                ast_walker_expr(node->v.NamedExpr.target, Store, EXTRA_2) &&
                ast_walker_expr(node->v.NamedExpr.value, EXTRA_3)
            );
            break;
        case BinOp_kind:
            res = (
                ast_walker_expr(node->v.BinOp.left, EXTRA_3) &&
                ast_walker_expr(node->v.BinOp.right, EXTRA_3)
            );
            break;
        case UnaryOp_kind:
            res = ast_walker_expr(node->v.UnaryOp.operand, EXTRA_3);
            break;
        case Lambda_kind:
            res = (
                ast_walker_arguments(node->v.Lambda.args, EXTRA_3) &&
                ast_walker_expr(node->v.Lambda.body, EXTRA_3)
            );
            break;
        case IfExp_kind:
            res = (
                ast_walker_expr(node->v.IfExp.test, EXTRA_3) &&
                ast_walker_expr(node->v.IfExp.body, EXTRA_3) &&
                ast_walker_expr(node->v.IfExp.orelse, EXTRA_3)
            );
            break;
        case Dict_kind:
            res = (
                ast_walker_expr_seq(node->v.Dict.keys, EXTRA_3) &&
                ast_walker_expr_seq(node->v.Dict.values, EXTRA_3)
            );
            break;
        case Set_kind:
            res = ast_walker_expr_seq(node->v.Set.elts, EXTRA_3);
            break;
        case ListComp_kind:
            res = (
                ast_walker_expr(node->v.ListComp.elt, EXTRA_3) &&
                ast_walker_compr_seq(node->v.ListComp.generators, EXTRA_3)
            );
            break;
        case SetComp_kind:
            res =(
                ast_walker_expr(node->v.SetComp.elt, EXTRA_3) &&
                ast_walker_compr_seq(node->v.SetComp.generators, EXTRA_3)
            );
            break;
        case DictComp_kind:
            res = (
                ast_walker_expr(node->v.DictComp.key, EXTRA_3) &&
                ast_walker_expr(node->v.DictComp.value, EXTRA_3) &&
                ast_walker_compr_seq(node->v.DictComp.generators, EXTRA_3)
            );
            break;
        case GeneratorExp_kind:
            res = (
                ast_walker_expr(node->v.GeneratorExp.elt, EXTRA_3) &&
                ast_walker_compr_seq(node->v.GeneratorExp.generators, EXTRA_3)
            );
            break;
        case Await_kind:
            res = ast_walker_expr(node->v.Await.value, EXTRA_3);
            break;
        case Yield_kind:
            res = ast_walker_expr(node->v.Yield.value, EXTRA_3);
            break;
        case YieldFrom_kind:
            res = ast_walker_expr(node->v.YieldFrom.value, EXTRA_3);
            break;
        case Compare_kind:
            res = (
                ast_walker_expr(node->v.Compare.left, EXTRA_3) &&
                ast_walker_expr_seq(node->v.Compare.comparators, EXTRA_3)
            );
            break;
        case Call_kind:
            res = (
                ast_walker_expr(node->v.Call.func, EXTRA_3) &&
                ast_walker_expr_seq(node->v.Call.args, EXTRA_3) &&
                ast_walker_keyword_seq(node->v.Call.keywords, EXTRA_3)
            );
            break;
        case FormattedValue_kind:
            res =(
                ast_walker_expr(node->v.FormattedValue.format_spec, EXTRA_3) &&
                ast_walker_expr(node->v.FormattedValue.value, EXTRA_3)
            );
            break;
        case JoinedStr_kind:
            res = ast_walker_expr_seq(node->v.JoinedStr.values, EXTRA_3);
            break;
        case Constant_kind:
            break;
        case Attribute_kind:
            res = (
                ast_walker_expr(node->v.Attribute.value, node->v.Attribute.ctx, EXTRA_2) &&
                ast_walker_identifier(node->v.Attribute.attr, node->v.Attribute.ctx, EXTRA_2)
            );
            break;
        case Subscript_kind:
            res = (
                ast_walker_expr(node->v.Subscript.value, node->v.Subscript.ctx, EXTRA_2) &&
                ast_walker_expr(node->v.Subscript.slice, node->v.Subscript.ctx, EXTRA_2)
            );
            break;
        case Starred_kind:
            res = ast_walker_expr(node->v.Starred.value, node->v.Starred.ctx, EXTRA_2);
            break;
        case Name_kind:
            res = ast_walker_identifier(node->v.Name.id, node->v.Name.ctx, EXTRA_2);
            break;
        case List_kind:
            res = ast_walker_expr_seq(node->v.List.elts, node->v.List.ctx, EXTRA_2);
            break;
        case Tuple_kind:
            res = ast_walker_expr_seq(node->v.Tuple.elts, node->v.Tuple.ctx, EXTRA_2);
            break;
        case Slice_kind:
            res = (
                ast_walker_expr(node->v.Slice.lower, EXTRA_3) &&
                ast_walker_expr(node->v.Slice.step, EXTRA_3) &&
                ast_walker_expr(node->v.Slice.upper, EXTRA_3)
            );
            break;
        case Pipeline_kind:
            res = (
                ast_walker_expr(node->v.Pipeline.left, EXTRA_3) &&
                ast_walker_expr(node->v.Pipeline.right, EXTRA_3)
            );
            break;
    }

    if (!res) {
        return false;
    }

    return callback(WalkExpr_kind, walk_node, ctx, userdata, CallbackLate_kind);
}

bool ast_walker_stmt(stmt_ty node, EXTRAS);

static bool ast_walker_stmt_seq(asdl_stmt_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_stmt(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_type_param(type_param_ty node, EXTRAS) {
    CHECK_NULL(node);
    switch(node->kind) {
        case TypeVar_kind:
            return (
                ast_walker_identifier(node->v.TypeVar.name, EXTRA_3) &&
                ast_walker_expr(node->v.TypeVar.bound, EXTRA_3) &&
                ast_walker_expr(node->v.TypeVar.default_value, EXTRA_3)
            );
        case ParamSpec_kind:
            return (
                ast_walker_identifier(node->v.ParamSpec.name, EXTRA_3) &&
                ast_walker_expr(node->v.ParamSpec.default_value, EXTRA_3)
            );
        case TypeVarTuple_kind:
            return (
                ast_walker_identifier(node->v.TypeVarTuple.name, EXTRA_3) &&
                ast_walker_expr(node->v.TypeVarTuple.default_value, EXTRA_3)
            );
    }
    return true;
}

static bool ast_walker_type_param_seq(asdl_type_param_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_type_param(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_withitem(withitem_ty node, EXTRAS) {
    CHECK_NULL(node);
    return (
        ast_walker_expr(node->context_expr, EXTRA_3) &&
        ast_walker_expr(node->optional_vars, EXTRA_3)
    );
}

static bool ast_walker_withitem_seq(asdl_withitem_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_withitem(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_pattern(pattern_ty node, EXTRAS);

static bool ast_walker_pattern_seq(asdl_pattern_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_pattern(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_identifier_seq(asdl_identifier_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_identifier(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_pattern(pattern_ty node, EXTRAS) {
    CHECK_NULL(node);
    switch(node->kind) {
        case MatchValue_kind:
            return (
                ast_walker_expr(node->v.MatchValue.value, EXTRA_3)
            );
        case MatchSingleton_kind:
            return true;
        case MatchSequence_kind:
            return (
                ast_walker_pattern_seq(node->v.MatchSequence.patterns, EXTRA_3)
            );
        case MatchMapping_kind:
            return (
                ast_walker_expr_seq(node->v.MatchMapping.keys, EXTRA_3) &&
                ast_walker_pattern_seq(node->v.MatchMapping.patterns, EXTRA_3) &&
                ast_walker_identifier(node->v.MatchMapping.rest, EXTRA_3)
            );
        case MatchClass_kind:
            return (
                ast_walker_expr(node->v.MatchClass.cls, EXTRA_3) &&
                ast_walker_pattern_seq(node->v.MatchClass.patterns, EXTRA_3) &&
                ast_walker_identifier_seq(node->v.MatchClass.kwd_attrs, EXTRA_3) &&
                ast_walker_pattern_seq(node->v.MatchClass.kwd_patterns, EXTRA_3)
            );
        case MatchStar_kind:
            return (
                ast_walker_identifier(node->v.MatchStar.name, EXTRA_3)
            );
        case MatchAs_kind:
            return (
                ast_walker_pattern(node->v.MatchAs.pattern, EXTRA_3) &&
                ast_walker_identifier(node->v.MatchAs.name, EXTRA_3)
            );
        case MatchOr_kind:
            return (
                ast_walker_pattern_seq(node->v.MatchOr.patterns, EXTRA_3)
            );
    }
    return true;
}

static bool ast_walker_matchcase(match_case_ty node, EXTRAS) {
    CHECK_NULL(node);
    return (
        ast_walker_pattern(node->pattern, EXTRA_3) &&
        ast_walker_expr(node->guard, EXTRA_3) &&
        ast_walker_stmt_seq(node->body, EXTRA_3)
    );
}

static bool ast_walker_matchcase_seq(asdl_match_case_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_matchcase(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_exc_handler(excepthandler_ty node, EXTRAS) {
    CHECK_NULL(node);
    switch(node->kind) {
        case ExceptHandler_kind:
            return (
                ast_walker_expr(node->v.ExceptHandler.type, EXTRA_3) &&
                ast_walker_identifier(node->v.ExceptHandler.name, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.ExceptHandler.body, EXTRA_3)
            );
    }
    return true;
}

static bool ast_walker_exc_handler_seq(asdl_excepthandler_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_exc_handler(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_alias(alias_ty node, EXTRAS) {
    CHECK_NULL(node);
    return (
        ast_walker_identifier(node->name, EXTRA_3) &&
        ast_walker_identifier(node->asname, EXTRA_3)
    );
}

static bool ast_walker_alias_seq(asdl_alias_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_alias(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

bool ast_walker_stmt(stmt_ty node, EXTRAS) {
    CHECK_NULL(node);

    walk_node_ty walk_node = { .Stmt = node };
    if (!callback(WalkStmt_kind, walk_node, ctx, userdata, CallbackEarly_kind)) {
        return false;
    }

    bool res = true;
    switch (node->kind) {
        case FunctionDef_kind:
            res = (
                ast_walker_expr_seq(node->v.FunctionDef.decorator_list, EXTRA_3) &&
                ast_walker_identifier(node->v.FunctionDef.name, EXTRA_3) &&
                ast_walker_type_param_seq(node->v.FunctionDef.type_params, EXTRA_3) &&
                ast_walker_arguments(node->v.FunctionDef.args, EXTRA_3) &&
                ast_walker_expr(node->v.FunctionDef.returns, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.FunctionDef.body, EXTRA_3)
            );
            break;
        case AsyncFunctionDef_kind:
            res = (
                ast_walker_expr_seq(node->v.AsyncFunctionDef.decorator_list, EXTRA_3) &&
                ast_walker_identifier(node->v.AsyncFunctionDef.name, EXTRA_3) &&
                ast_walker_type_param_seq(node->v.AsyncFunctionDef.type_params, EXTRA_3) &&
                ast_walker_arguments(node->v.AsyncFunctionDef.args, EXTRA_3) &&
                ast_walker_expr(node->v.AsyncFunctionDef.returns, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.AsyncFunctionDef.body, EXTRA_3)
            );
            break;
        case ClassDef_kind:
            res = (
                ast_walker_expr_seq(node->v.ClassDef.decorator_list, EXTRA_3) &&
                ast_walker_identifier(node->v.ClassDef.name, EXTRA_3) &&
                ast_walker_type_param_seq(node->v.ClassDef.type_params, EXTRA_3) &&
                ast_walker_expr_seq(node->v.ClassDef.bases, EXTRA_3) &&
                ast_walker_keyword_seq(node->v.ClassDef.keywords, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.ClassDef.body, EXTRA_3)
            );
            break;
        case Return_kind:
            res = ast_walker_expr(node->v.Return.value, EXTRA_3);
            break;
        case Delete_kind:
            res = ast_walker_expr_seq(node->v.Delete.targets, EXTRA_3);
            break;
        case Assign_kind:
            res = (
                ast_walker_expr_seq(node->v.Assign.targets, EXTRA_3) &&
                ast_walker_expr(node->v.Assign.value, EXTRA_3)
            );
            break;
        case TypeAlias_kind:
            res = (
                ast_walker_expr(node->v.TypeAlias.name, EXTRA_3) &&
                ast_walker_type_param_seq(node->v.TypeAlias.type_params, EXTRA_3) &&
                ast_walker_expr(node->v.TypeAlias.value, EXTRA_3)
            );
            break;
        case AugAssign_kind:
            res = (
                ast_walker_expr(node->v.AugAssign.target, EXTRA_3) &&
                ast_walker_expr(node->v.AugAssign.value, EXTRA_3)
            );
            break;
        case AnnAssign_kind:
            res = (
                ast_walker_expr(node->v.AnnAssign.target, EXTRA_3) &&
                ast_walker_expr(node->v.AnnAssign.annotation, EXTRA_3) &&
                ast_walker_expr(node->v.AnnAssign.value, EXTRA_3)
            );
            break;
        case For_kind:
            res = (
                ast_walker_expr(node->v.For.target, EXTRA_3) &&
                ast_walker_expr(node->v.For.iter, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.For.body, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.For.orelse, EXTRA_3)
            );
            break;
        case AsyncFor_kind:
            res = (
                ast_walker_expr(node->v.AsyncFor.target, EXTRA_3) &&
                ast_walker_expr(node->v.AsyncFor.iter, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.AsyncFor.body, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.AsyncFor.orelse, EXTRA_3)
            );
            break;
        case While_kind:
            res = (
                ast_walker_expr(node->v.While.test, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.While.body, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.While.orelse, EXTRA_3)
            );
            break;
        case If_kind:
            res = (
                ast_walker_expr(node->v.If.test, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.If.body, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.If.orelse, EXTRA_3)
            );
            break;
        case With_kind:
            res = (
                ast_walker_withitem_seq(node->v.With.items, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.With.body, EXTRA_3)
            );
            break;
        case AsyncWith_kind:
            res = (
                ast_walker_withitem_seq(node->v.AsyncWith.items, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.AsyncWith.body, EXTRA_3)
            );
            break;
        case Match_kind:
            res = (
                ast_walker_expr(node->v.Match.subject, EXTRA_3) &&
                ast_walker_matchcase_seq(node->v.Match.cases, EXTRA_3)
            );
            break;
        case Raise_kind:
            res = (
                ast_walker_expr(node->v.Raise.exc, EXTRA_3) &&
                ast_walker_expr(node->v.Raise.cause, EXTRA_3)
            );
            break;
        case Try_kind:
            res = (
                ast_walker_stmt_seq(node->v.Try.body, EXTRA_3) &&
                ast_walker_exc_handler_seq(node->v.Try.handlers, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.Try.orelse, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.Try.finalbody, EXTRA_3)
            );
            break;
        case TryStar_kind:
            res = (
                ast_walker_stmt_seq(node->v.TryStar.body, EXTRA_3) &&
                ast_walker_exc_handler_seq(node->v.TryStar.handlers, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.TryStar.orelse, EXTRA_3) &&
                ast_walker_stmt_seq(node->v.TryStar.finalbody, EXTRA_3)
            );
            break;
        case Assert_kind:
            res = (
                ast_walker_expr(node->v.Assert.test, EXTRA_3) &&
                ast_walker_expr(node->v.Assert.msg, EXTRA_3)
            );
            break;
        case Import_kind:
            res = ast_walker_alias_seq(node->v.Import.names, EXTRA_3);
            break;
        case ImportFrom_kind:
            res = (
                ast_walker_identifier(node->v.ImportFrom.module, EXTRA_3) &&
                ast_walker_alias_seq(node->v.ImportFrom.names, EXTRA_3)
            );
            break;
        case Global_kind:
            res = ast_walker_identifier_seq(node->v.Global.names, EXTRA_3);
            break;
        case Nonlocal_kind:
            res = ast_walker_identifier_seq(node->v.Nonlocal.names, EXTRA_3);
            break;
        case Expr_kind:
            res = (
                ast_walker_expr(node->v.Expr.value, EXTRA_3)
            );
            break;
        case Pass_kind:
            break;
        case Break_kind:
            break;
        case Continue_kind:
            break;
    }

    if (!res) {
        return false;
    }

    return callback(WalkStmt_kind, walk_node, ctx, userdata, CallbackLate_kind);
}

bool ast_walker_mod(mod_ty mod, EXTRAS) {
    CHECK_NULL(mod);

    walk_node_ty walk_node = { .Mod = mod };
    if (!callback(WalkMod_kind, walk_node, ctx, userdata, CallbackEarly_kind)) {
        return false;
    }

    bool res = true;
    switch (mod->kind) {
        case Module_kind:
            res = ast_walker_stmt_seq(mod->v.Module.body, EXTRA_3);
            break;
        case Interactive_kind:
            res = ast_walker_stmt_seq(mod->v.Interactive.body, EXTRA_3);
            break;
        case Expression_kind:
            res = ast_walker_expr(mod->v.Expression.body, EXTRA_3);
            break;
        case FunctionType_kind:
            res = (
                ast_walker_expr_seq(mod->v.FunctionType.argtypes, EXTRA_3) &&
                ast_walker_expr(mod->v.FunctionType.returns, EXTRA_3)
            );
            break;
    }

    if (!res) {
        return false;
    }

    return callback(WalkMod_kind, walk_node, ctx, userdata, CallbackLate_kind);
}

bool ast_walker(walk_kind_ty kind, walk_node_ty node, EXTRAS) {
    switch(kind) {
    case WalkExpr_kind:
        CHECK_NULL(node.Expr);
        return ast_walker_expr(node.Expr, EXTRA_3);
    case WalkStmt_kind:
        CHECK_NULL(node.Stmt);
        return ast_walker_stmt(node.Stmt, EXTRA_3);
    case WalkMod_kind:
        CHECK_NULL(node.Mod);
        return ast_walker_mod(node.Mod, EXTRA_3);
    case WalkIdentifier_kind:
        CHECK_NULL(node.Identifier);
        return ast_walker_identifier(node.Identifier, EXTRA_3);
    }
    return true;
}
