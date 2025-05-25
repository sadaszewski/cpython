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
        /* case IfExp_kind:
            WALK(node->v.IfExp.test);
            WALK(node->v.IfExp.body);
            WALK(node->v.IfExp.orelse);
            break;
        case Dict_kind:
            WALK_SEQ(node->v.Dict.keys);
            WALK_SEQ(node->v.Dict.values);
            break;
        case Set_kind:
            WALK_SEQ(node->v.Set.elts);
            break;
        case ListComp_kind:
            WALK(node->v.ListComp.elt);
            WALK_COMPR_SEQ(node->v.ListComp.generators);
            break;
        case SetComp_kind:
            WALK(node->v.SetComp.elt);
            WALK_COMPR_SEQ(node->v.SetComp.generators);
            break;
        case DictComp_kind:
            WALK(node->v.DictComp.key);
            WALK(node->v.DictComp.value);
            WALK_COMPR_SEQ(node->v.DictComp.generators);
            break;
        case GeneratorExp_kind:
            WALK(node->v.GeneratorExp.elt);
            WALK_COMPR_SEQ(node->v.GeneratorExp.generators);
            break;
        case Await_kind:
            WALK(node->v.Await.value);
            break;
        case Yield_kind:
            WALK(node->v.Yield.value);
            break;
        case YieldFrom_kind:
            WALK(node->v.YieldFrom.value);
            break;
        case Compare_kind:
            WALK_SEQ(node->v.Compare.comparators);
            WALK(node->v.Compare.left);
            break;
        case Call_kind :
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