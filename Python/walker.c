#include "Python.h"
#include "pycore_ast.h"           // _PyAST_GetDocString()
#include "pycore_format.h"        // F_LJUST
#include "pycore_long.h"          // _PyLong
#include "pycore_pystate.h"       // _PyThreadState_GET()
#include "pycore_setobject.h"     // _PySet_NextEntry()

typedef enum _walk_kind {
    WalkExpr_kind,
    WalkStmt_kind,
    WalkMod_kind,
    WalkIdentifier_kind
} walk_kind_ty;

typedef union {
    expr_ty Expr;
    stmt_ty Stmt;
    mod_ty Mod;
    identifier Identifier;
} walk_node_ty;

typedef bool (*AST_WALKER_CALLBACK)(walk_kind_ty, walk_node_ty, expr_context_ty, void *userdata);

#define EXTRAS expr_context_ty ctx, AST_WALKER_CALLBACK callback, void *userdata
#define EXTRA_3 ctx, callback, userdata
#define EXTRA_2 callback, userdata

static bool ast_walker_expr(expr_ty node, EXTRAS);

static bool ast_walker_expr_seq(asdl_expr_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        if (!ast_walker_expr(asdl_seq_GET(seq, i), EXTRA_3)) {
            return false;
        }
    }
    return true;
}

static bool ast_walker_identifier(identifier id, EXTRAS) {
    walk_node_ty walk_node = { .Identifier = id };
    return callback(WalkIdentifier_kind, walk_node, ctx, userdata);
}

static bool ast_walker_arg(arg_ty arg, EXTRAS) {
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
    return (
        ast_walker_expr(compr->target, EXTRA_3) &&
        ast_walker_expr(compr->iter, EXTRA_3) &&
        ast_walker_expr_seq(compr->ifs, EXTRA_3)
    );
}

static bool ast_walker_compr_seq(asdl_comprehension_seq *seq, EXTRAS) {
    for (int i = 0; i < asdl_seq_LEN(seq); i++) {
        comprehension_ty item = asdl_seq_GET(seq, i);
        if (!ast_walker_compr(item->target, EXTRA_3)) {
            return false;
        }
    }
}

static bool ast_walker_expr(expr_ty node, EXTRAS) {

    if (node == NULL) {
        return true;
    }
    switch (node->kind) {
        case BoolOp_kind:
            return ast_walker_expr_seq(node->v.BoolOp.values, EXTRA_3);
        case NamedExpr_kind:
            return (
                ast_walker_expr(node->v.NamedExpr.target, Store, EXTRA_2) &&
                ast_walker_expr(node->v.NamedExpr.value, EXTRA_3)
            );
        case BinOp_kind:
            return (
                ast_walker_expr(node->v.BinOp.left, EXTRA_3) &&
                ast_walker_expr(node->v.BinOp.right, EXTRA_3)
            );
        case UnaryOp_kind:
            return ast_walker_expr(node->v.UnaryOp.operand, EXTRA_3);
        case Lambda_kind:
            return (
                ast_walker_arguments(node->v.Lambda.args, EXTRA_3) &&
                ast_walker_expr(node->v.Lambda.body, EXTRA_3)
            ); 
        case IfExp_kind:
            return (
                ast_walker_expr(node->v.IfExp.test, EXTRA_3) &&
                ast_walker_expr(node->v.IfExp.body, EXTRA_3) &&
                ast_walker_expr(node->v.IfExp.orelse, EXTRA_3)
            );
        case Dict_kind:
            return (
                ast_walker_expr_seq(node->v.Dict.keys, EXTRA_3) &&
                ast_walker_expr_seq(node->v.Dict.values, EXTRA_3)
            );
        case Set_kind:
            return ast_walker_expr_seq(node->v.Set.elts, EXTRA_3);
        case ListComp_kind:
            return (
                ast_walker_expr(node->v.ListComp.elt, EXTRA_3) &&
                ast_walker_compr_seq(node->v.ListComp.generators, EXTRA_3)
            );
        case SetComp_kind:
            return(
                ast_walker_expr(node->v.SetComp.elt, EXTRA_3) &&
                ast_walker_compr_seq(node->v.SetComp.generators, EXTRA_3)
            );
        case DictComp_kind:
            return (
                ast_walker_expr(node->v.DictComp.key, EXTRA_3) &&
                ast_walker_expr(node->v.DictComp.value, EXTRA_3) &&
                ast_walker_compr_seq(node->v.DictComp.generators, EXTRA_3)
            );
        case GeneratorExp_kind:
            return (
                ast_walker_expr(node->v.GeneratorExp.elt, EXTRA_3) &&
                ast_walker_compr_seq(node->v.GeneratorExp.generators, EXTRA_3)
            );
        case Await_kind:
            return ast_walker_expr(node->v.Await.value, EXTRA_3);
        case Yield_kind:
            return ast_walker_expr(node->v.Yield.value, EXTRA_3);
        case YieldFrom_kind:
            return ast_walker_expr(node->v.YieldFrom.value, EXTRA_3);
        case Compare_kind:
            return (
                ast_walker_expr(node->v.Compare.left, EXTRA_3) &&
                ast_walker_expr_seq(node->v.Compare.comparators, EXTRA_3)
            );
        /*case Call_kind :
            WALK(node->v.Call.func);
            WALK_SEQ(node->v.Call.args);
            WALK_KEYWORD_SEQ(node->v.Call.keywords);
            break;
        case FormattedValue_kind:
            WALK(node->v.FormattedValue.format_spec);
            WALK(node->v.FormattedValue.value);
            break;
        case JoinedStr_kind:
            WALK_SEQ(node->v.JoinedStr.values);
            break;
        case Constant_kind:
            break;
        case Attribute_kind:
            WALK_IDENTIFIER(node->v.Attribute.attr, node->v.Attribute.ctx);
            WALK(node->v.Attribute.value);
            break;
        case Subscript_kind:
            if (node->v.Subscript.ctx != Store || include_store)
                WALK(node->v.Subscript.slice);
            if (node->v.Subscript.ctx != Store || include_store)
                WALK(node->v.Subscript.value);
            break;
        case Starred_kind:
            if (node->v.Starred.ctx != Store || include_store)
                WALK(node->v.Starred.value);
            break;
        case Name_kind:
            break;
        case List_kind:
            if (node->v.List.ctx != Store || include_store)
                WALK_SEQ(node->v.List.elts);
            break;
        case Tuple_kind:
            if (node->v.Tuple.ctx != Store || include_store)
                WALK_SEQ(node->v.Tuple.elts);
            break;
        case Slice_kind:
            WALK(node->v.Slice.lower);
            WALK(node->v.Slice.upper);
            WALK(node->v.Slice.step);
            break;
        case Pipeline_kind:
            WALK(node->v.Pipeline.left);
            WALK(node->v.Pipeline.right);
            break;*/
    }

    walk_node_ty walk_node = { .Expr = node };
    if (callback(WalkExpr_kind, walk_node, ctx, userdata)) {
        return true;
    } else {
        return false;
    }
}

static bool ast_walker(walk_kind_ty kind, walk_node_ty node, EXTRAS) {
    switch(kind) {
    case WalkExpr_kind:
        if (!ast_walker_expr(node.Expr, ctx, callback, userdata)) {
            return false;
        }
        break;
    case WalkStmt_kind:
        break;
    case WalkMod_kind:
        break;
    case WalkIdentifier_kind:
        break;
    }
    return true;
}