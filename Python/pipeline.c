#include "Python.h"
#include "pycore_ast.h"           // _PyAST_GetDocString()
#include "pycore_format.h"        // F_LJUST
#include "pycore_long.h"          // _PyLong
#include "pycore_pystate.h"       // _PyThreadState_GET()
#include "pycore_setobject.h"     // _PySet_NextEntry()
#include "pycore_walker.h"

#define EXTRAS(x) (x)->lineno, (x)->col_offset, (x)->end_lineno, (x)->end_col_offset

typedef struct _search_track {
    int depth;
    bool used;
    bool overwritten;
} *search_track_ty;

static void search_track_init(search_track_ty search_track);
static bool placeholder_use_info(expr_ty node, search_track_ty search_track);
static bool leftmost_call_callback(walk_kind_ty, walk_node_ty, expr_context_ty, void*, callback_kind_ty);

static bool leftmost_call_callback(
    walk_kind_ty kind, walk_node_ty node, expr_context_ty ctx,
    void *found, callback_kind_ty cb_kind
) {
    if (cb_kind == CallbackEarly_kind && kind == WalkExpr_kind && node.Expr->kind == Call_kind) {
        *((expr_ty*) found) = node.Expr;
        walk_node_ty walk_node = { .Expr = node.Expr->v.Call.func };
        ast_walker(WalkExpr_kind, walk_node, ctx, leftmost_call_callback, found);
        return false; // we look to the left manually
    }

    /* if (cb_kind == CallbackLate_kind && kind == WalkExpr_kind && node.Expr->kind == Call_kind) {
        *((expr_ty*) found) = node.Expr;
        return false;
    }*/
    return true;
}

static expr_ty leftmost_call(expr_ty e, expr_ty c) {
    expr_ty found = NULL;
    ast_walker_expr(e, Load, leftmost_call_callback, &found);
    return found;
}

static int transform_pipeline(expr_ty node, PyArena *arena);

static bool walk_replace_pipelines_callback(
    walk_kind_ty kind, walk_node_ty node, expr_context_ty ctx,
    void *arena, callback_kind_ty cb_kind
) {
    if (cb_kind == CallbackLate_kind && kind == WalkExpr_kind && node.Expr->kind == Pipeline_kind) {
        transform_pipeline(node.Expr, arena);
    }
    return true;
}

int walk_replace_pipelines(mod_ty m, PyArena *arena) {
    ast_walker_mod(m, Load, walk_replace_pipelines_callback, arena);
    return 1;
}

/*

LHS |> RHS

to:

((_ := LHS), (lambda _: _)(RHS))[1]

*/

static _Py_Identifier PyID__ = { .string = "_", .index = -1 };

#define CHECK_NULL(x) if ((x) == NULL) { return NULL; }

static expr_ty wrap_in_lambda(expr_ty node, PyArena *arena) {
    arguments_ty arguments = (arguments_ty) _PyArena_Malloc(arena, sizeof(*arguments));
    CHECK_NULL(arguments);
    memset(arguments, 0, sizeof(*arguments));
    arguments[0].args = _Py_asdl_arg_seq_new(1, arena);
    CHECK_NULL(arguments[0].args);

    identifier placeholder = _PyUnicode_FromId(&PyID__);
    CHECK_NULL(placeholder);
    
    arg_ty a = _PyAST_arg(placeholder, NULL, NULL, EXTRAS(node), arena);
    CHECK_NULL(a);
    asdl_seq_SET(arguments[0].args, 0, a);

    expr_ty body = _PyAST_Name(placeholder, Load, EXTRAS(node), arena);
    CHECK_NULL(body);

    expr_ty lambda = _PyAST_Lambda(arguments, body, EXTRAS(node), arena);
    CHECK_NULL(lambda);

    asdl_expr_seq *args = _Py_asdl_expr_seq_new(1, arena);
    CHECK_NULL(args);
    asdl_seq_SET(args, 0, node);

    expr_ty call = _PyAST_Call(lambda, args, NULL, EXTRAS(node), arena);

    return call;
}

#undef CHECK_NULL
#define CHECK_NULL(x) if ((x) == NULL) return 0;

static int transform_pipeline_instance(expr_ty node, PyArena *arena);
static int transform_autolambda(expr_ty node, asdl_expr_seq *seq, int count, PyArena *arena);

static int transform_pipeline(expr_ty node, PyArena *arena) {
    int count = 0;
    expr_ty tmp = node;
    expr_ty leftmost = node;
    while (tmp != NULL && tmp->kind == Pipeline_kind) {
        count++;
        leftmost = tmp;
        tmp = tmp->v.Pipeline.left;
    }
    asdl_expr_seq *seq = _Py_asdl_expr_seq_new(count, arena);
    CHECK_NULL(seq);
    tmp = node;
    for (int i = count - 1; i >= 0; i--) {
        asdl_seq_SET(seq, i, tmp->v.Pipeline.right);
        tmp = tmp->v.Pipeline.left;
    }
    if (leftmost->v.Pipeline.left == NULL) {
        // pipeline
        return transform_autolambda(node, seq, count, arena);
    } else {
        // pipeline instance
        return transform_pipeline_instance(node, arena);
    }
}

static int transform_autolambda(expr_ty node, asdl_expr_seq *seq, int count, PyArena *arena) {
    identifier placeholder = _PyUnicode_FromId(&PyID__);
    CHECK_NULL(placeholder);
    arg_ty a = _PyAST_arg(placeholder, NULL, NULL, EXTRAS(node), arena);
    CHECK_NULL(a);
    asdl_arg_seq *args = _Py_asdl_arg_seq_new(1, arena);
    CHECK_NULL(args);
    asdl_seq_SET(args, 0, a);
    arguments_ty arguments = _PyAST_arguments(NULL, args, NULL, NULL, NULL, NULL, NULL, arena);
    CHECK_NULL(arguments);

    expr_ty placeholder_e = _PyAST_Name(placeholder, Store, EXTRAS(node), arena);
    CHECK_NULL(placeholder_e);
    asdl_expr_seq *elts = _Py_asdl_expr_seq_new(count, arena);
    CHECK_NULL(elts);
    for (int i = 0; i < count; i++) {
        expr_ty e = _PyAST_NamedExpr(placeholder_e, asdl_seq_GET(seq, i), EXTRAS(node), arena);
        CHECK_NULL(e);
        asdl_seq_SET(elts, i, e);
    }
    expr_ty tuple = _PyAST_Tuple(elts, Load, EXTRAS(node), arena);
    CHECK_NULL(tuple);

    PyObject *minus_one = PyLong_FromLong(-1);
    if (_PyArena_AddPyObject(arena, minus_one) < 0) {
        Py_DecRef(minus_one);
        return -1;
    }
    expr_ty minus_one_e = _PyAST_Constant(minus_one, NULL, EXTRAS(node), arena);
    CHECK_NULL(minus_one_e);

    expr_ty subscript = _PyAST_Subscript(tuple, minus_one_e, Load, EXTRAS(node), arena);
    CHECK_NULL(subscript);

    node->kind = Lambda_kind;
    node->v.Lambda.args = arguments;
    node->v.Lambda.body = subscript;

    return 1;
}

static int transform_pipeline_instance(expr_ty node, PyArena *arena) {
    expr_ty lhs = node->v.Pipeline.left;
    expr_ty rhs = node->v.Pipeline.right;
    expr_ty rhs_leftmost_call = leftmost_call(rhs, NULL);

    expr_ty rhs_wrapped = wrap_in_lambda(rhs, arena);
    CHECK_NULL(rhs_wrapped);

    asdl_expr_seq *elts = _Py_asdl_expr_seq_new(2, arena);
    CHECK_NULL(elts);
    identifier placeholder_id = _PyUnicode_FromId(&PyID__);
    CHECK_NULL(placeholder_id);
    expr_ty placeholder = _PyAST_Name(placeholder_id, Store, EXTRAS(rhs), arena);
    CHECK_NULL(placeholder);
    expr_ty assignment = _PyAST_NamedExpr(placeholder, lhs, EXTRAS(rhs), arena);
    CHECK_NULL(assignment);
    asdl_seq_SET(elts, 0, assignment);
    asdl_seq_SET(elts, 1, rhs_wrapped);
    expr_ty tuple = _PyAST_Tuple(elts, Load, EXTRAS(rhs), arena);
    CHECK_NULL(tuple);
    expr_ty one = _PyAST_Constant(Py_GetConstant(Py_CONSTANT_ONE), NULL, EXTRAS(rhs), arena);
    CHECK_NULL(one);

    node->kind = Subscript_kind;
    node->v.Subscript.value = tuple;
    node->v.Subscript.slice = one;
    node->v.Subscript.ctx = Load;

    // Handle injection
    struct _search_track search_track;
    search_track_init(&search_track);
    if (rhs_leftmost_call != NULL) {
        placeholder_use_info(rhs_leftmost_call, &search_track);
    }

    if (
        !search_track.used
        && rhs_leftmost_call != NULL
        /* && asdl_seq_LEN(rhs_leftmost_call->v.Call.args) == 0
        && asdl_seq_LEN(rhs_leftmost_call->v.Call.keywords) == 0 */
    ) {
        asdl_expr_seq *old_args = rhs_leftmost_call->v.Call.args;
        int n = asdl_seq_LEN(old_args);
        asdl_expr_seq *injected_args = _Py_asdl_expr_seq_new(n + 1, arena);
        CHECK_NULL(injected_args);
        for (int i = 0; i < n; i++) {
            asdl_seq_SET(injected_args, i, asdl_seq_GET(old_args, i));
        }
        expr_ty placeholder = _PyAST_Name(placeholder_id, Load, EXTRAS(rhs), arena);
        CHECK_NULL(placeholder);
        asdl_seq_SET(injected_args, n, placeholder);
        rhs_leftmost_call->v.Call.args = injected_args;
    }

    return 1;
}

static bool placeholder_use_info_callback(
    walk_kind_ty kind,
    walk_node_ty node,
    expr_context_ty ctx,
    void *userdata,
    callback_kind_ty cb_kind
) {
    search_track_ty search_track = (search_track_ty) userdata;

    if (
        kind == WalkExpr_kind &&
        node.Expr->kind == Lambda_kind
    ) {
        if (cb_kind == CallbackEarly_kind) {
            search_track->depth++;
        } else if (cb_kind == CallbackLate_kind) {
            search_track->depth--;
        }
    }

    if (
        cb_kind == CallbackLate_kind &&
        kind == WalkExpr_kind &&
        node.Expr->kind == Name_kind &&
        node.Expr->v.Name.ctx == Load &&
        _PyUnicode_EqualToASCIIString(node.Expr->v.Name.id, "_")
    ) {
        if (!search_track->overwritten) {
            search_track->used = true;
        }
    }

    if (
        kind == WalkIdentifier_kind &&
        _PyUnicode_EqualToASCIIString(node.Identifier, "_") &&
        ctx == Store &&
        search_track->depth == 0
    ) {
        search_track->overwritten = true;
    }

    return true;
}

static void search_track_init(search_track_ty search_track) {
    search_track->depth = 0;
    search_track->used = false;
    search_track->overwritten = false;
}

static bool placeholder_use_info(expr_ty node, search_track_ty search_track) {
    search_track_init(search_track);
    return ast_walker_expr(node, Load, placeholder_use_info_callback, &search_track);
}
