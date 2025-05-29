#include "Python.h"
#include "pycore_ast.h"           // _PyAST_GetDocString()
#include "pycore_format.h"        // F_LJUST
#include "pycore_long.h"          // _PyLong
#include "pycore_pystate.h"       // _PyThreadState_GET()
#include "pycore_setobject.h"     // _PySet_NextEntry()
#include "pycore_walker.h"

#define EXTRAS(x) (x)->lineno, (x)->col_offset, (x)->end_lineno, (x)->end_col_offset

static bool leftmost_call_callback(walk_kind_ty kind, walk_node_ty node, expr_context_ty ctx, void *userdata) {
    if (kind == WalkExpr_kind && node.Expr->kind == Call_kind) {
        *((expr_ty*) userdata) = node.Expr;
        return false;
    }
    return true;
}

static expr_ty leftmost_call(expr_ty e, expr_ty c) {
    expr_ty found = NULL;
    ast_walker_expr(e, Load, leftmost_call_callback, &found);
    return found;
}

static bool find_placeholder_callback(walk_kind_ty kind, walk_node_ty node, expr_context_ty ctx, void *userdata) {
    if (kind == WalkIdentifier_kind && _PyUnicode_EqualToASCIIString(node.Identifier, "_")) {
        return false;
    }
    return true;
}

static bool contains_placeholder(expr_ty node) {
    bool res = !ast_walker_expr(node, Load, find_placeholder_callback, NULL);
    return res;
}

static int transform_pipeline(expr_ty node, PyArena *arena);

static bool walk_replace_pipelines_callback(walk_kind_ty kind, walk_node_ty node, expr_context_ty ctx, void *arena) {
    if (kind == WalkExpr_kind && node.Expr->kind == Pipeline_kind) {
        transform_pipeline(node.Expr, arena);
    }
    return true;
}

int walk_replace_pipelines(mod_ty m, PyArena *arena) {
    ast_walker_mod(m, Load, walk_replace_pipelines_callback, arena);
    return 1;
}

static int transform_pipeline(expr_ty node, PyArena *arena) {
    expr_ty lhs = node->v.Pipeline.left;
    expr_ty rhs = node->v.Pipeline.right;
    expr_ty rhs_leftmost_call = leftmost_call(rhs, NULL);
    bool placeholder_found = contains_placeholder(node->v.Pipeline.right);

    arguments_ty arguments = (arguments_ty) _PyArena_Malloc(arena, sizeof(*arguments));
    if (arguments == NULL) {
        return 0;
    }
    memset(arguments, 0, sizeof(*arguments));
    arguments[0].args = _Py_asdl_arg_seq_new(1, arena);
    if (arguments[0].args == NULL) {
        return 0;
    }
    static _Py_Identifier PyID__ = { .string = "_", .index = -1 };
    arg_ty a =  _PyAST_arg(_PyUnicode_FromId(&PyID__), NULL, NULL, EXTRAS(rhs), arena);
    asdl_seq_SET(arguments[0].args, 0, a);

    expr_ty lambda = _PyAST_Lambda(arguments, rhs, EXTRAS(rhs), arena);
    if (lambda == NULL) {
        return 0;
    }

    asdl_expr_seq *args = _Py_asdl_expr_seq_new(1, arena);
    if (args == NULL) {
        return 0;
    }
    asdl_seq_SET(args, 0, lhs);

    node->kind = Call_kind;
    node->v.Call.func = lambda;
    node->v.Call.args = args;
    node->v.Call.keywords = NULL;

    // Handle injection
    if (
        !placeholder_found
        && rhs_leftmost_call != NULL
        /* && asdl_seq_LEN(rhs_leftmost_call->v.Call.args) == 0
        && asdl_seq_LEN(rhs_leftmost_call->v.Call.keywords) == 0 */
    ) {
        asdl_expr_seq *old_args = rhs_leftmost_call->v.Call.args;
        int n = asdl_seq_LEN(old_args);
        asdl_expr_seq *injected_args = _Py_asdl_expr_seq_new(n + 1, arena);
        for (int i = 0; i < n; i++) {
            asdl_seq_SET(injected_args, i, asdl_seq_GET(old_args, i));
        }
        expr_ty placeholder = _PyAST_Name(_PyUnicode_FromId(&PyID__), Load, EXTRAS(rhs), arena);
        asdl_seq_SET(injected_args, n, placeholder);
        rhs_leftmost_call->v.Call.args = injected_args;
    }

    return 1;
}
