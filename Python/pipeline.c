#include "Python.h"
#include "pycore_ast.h"           // _PyAST_GetDocString()
#include "pycore_format.h"        // F_LJUST
#include "pycore_long.h"          // _PyLong
#include "pycore_pystate.h"       // _PyThreadState_GET()
#include "pycore_setobject.h"     // _PySet_NextEntry()
#include "pycore_walker.h"
#include "pycore_intrinsics.h"

#define EXTRAS(x) (x)->lineno, (x)->col_offset, (x)->end_lineno, (x)->end_col_offset

typedef struct _search_track {
    int depth;
    bool used;
    bool overwritten;
    int shadowed;
} *search_track_ty;

static void search_track_init(search_track_ty search_track);
static bool placeholder_use_info(expr_ty node, search_track_ty search_track);
static bool leftmost_call_callback(walk_kind_ty, walk_node_ty, expr_context_ty, void*, callback_kind_ty, void**);
static int handle_injection(expr_ty rhs, identifier placeholder_id, PyArena *arena);
static int handle_magic_method(expr_ty rhs, identifier placeholder_id, bool last, PyArena *arena);

static bool leftmost_call_callback(
    walk_kind_ty kind, walk_node_ty node, expr_context_ty ctx,
    void *found, callback_kind_ty cb_kind, void **target
) {
    if (cb_kind == CallbackEarly_kind && kind == WalkExpr_kind && node.Expr->kind == Call_kind) {
        *((expr_ty*) found) = node.Expr;
        walk_node_ty walk_node = { .Expr = node.Expr->v.Call.func };
        ast_walker(WalkExpr_kind, walk_node, NULL, ctx, leftmost_call_callback, found);
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
    ast_walker_expr(e, NULL, Load, leftmost_call_callback, &found);
    return found;
}

static int transform_pipeline(expr_ty node, PyArena *arena);

static bool walk_replace_pipelines_callback(
    walk_kind_ty kind, walk_node_ty node, expr_context_ty ctx,
    void *arena, callback_kind_ty cb_kind, void **target
) {
    if (cb_kind == CallbackEarly_kind && kind == WalkExpr_kind && node.Expr->kind == Pipeline_kind) {
        transform_pipeline(node.Expr, arena);
    }
    return true;
}

int walk_replace_pipelines(mod_ty m, PyArena *arena) {
    ast_walker_mod(m, NULL, Load, walk_replace_pipelines_callback, arena);
    return 1;
}

/*

LHS |> RHS

to:

((_ := LHS), (lambda _: _)(RHS))[1]

*/

static _Py_Identifier PyID__ = { .string = "_", .index = -1 };

#define CHECK_NULL(x) if ((x) == NULL) return 0;

static int transform_pipeline_instance(expr_ty node, expr_ty leftmost, asdl_expr_seq *seq, int count, PyArena *arena);
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
        return transform_pipeline_instance(node, leftmost, seq, count, arena);
    }
}

static int transform_child_nodes(expr_ty leftmost, asdl_expr_seq *seq, int count, PyArena *arena) {
    // only now transform the children
    if (!ast_walker_expr(leftmost->v.Pipeline.left, NULL, Load, walk_replace_pipelines_callback, arena)) {
        return 0;
    }
    for (int i = 0; i < count; i++) {
        if (!ast_walker_expr(asdl_seq_GET(seq, i), NULL, Load, walk_replace_pipelines_callback, arena)) {
            return 0;
        }
    }
    return 1;
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

    //expr_ty placeholder_e = _PyAST_Name(placeholder, Store, EXTRAS(node), arena);
    //CHECK_NULL(placeholder_e);
    asdl_expr_seq *elts = _Py_asdl_expr_seq_new(count, arena);
    CHECK_NULL(elts);
    for (int i = 0; i < count; i++) {
        expr_ty e = asdl_seq_GET(seq, i);
        if (!handle_magic_method(e, placeholder, (i == count - 1), arena)) {
            return 0;
        }
        //e = _PyAST_NamedExpr(placeholder_e, e, EXTRAS(node), arena);
        //CHECK_NULL(e);
        asdl_seq_SET(elts, i, e);
    }
    expr_ty tuple = _PyAST_Tuple(elts, Load, EXTRAS(node), arena);
    CHECK_NULL(tuple);

    PyObject *minus_one = PyLong_FromLong(-1);
    if (_PyArena_AddPyObject(arena, minus_one) < 0) {
        Py_DecRef(minus_one);
        return 0;
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

static int handle_injection(expr_ty rhs, identifier placeholder_id, PyArena *arena) {
    expr_ty rhs_leftmost_call = leftmost_call(rhs, NULL);

    // Handle injection
    struct _search_track search_track;
    search_track_init(&search_track);
    if (rhs_leftmost_call != NULL) {
        placeholder_use_info(rhs_leftmost_call, &search_track);
        /* printf(
            "placeholder_use_info(), depth: %d, overwritten: %d, used: %d, shadowed: %d\n",
            search_track.depth,
            (int) search_track.overwritten,
            (int) search_track.used,
            (int) search_track.shadowed
        ); */
    }

    if (
        !search_track.used
        && !search_track.overwritten
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

static int transform_pipeline_instance(expr_ty node, expr_ty leftmost, asdl_expr_seq *seq, int count, PyArena *arena) {
    identifier placeholder_id = _PyUnicode_FromId(&PyID__);
    CHECK_NULL(placeholder_id);
    expr_ty placeholder = _PyAST_Name(placeholder_id, Store, EXTRAS(node), arena);
    CHECK_NULL(placeholder);

    for (int i = 0; i < count; i++) {
        if (!handle_magic_method(asdl_seq_GET(seq, i), placeholder_id, (i == count - 1), arena)) {
            return 0;
        }
    }

    asdl_expr_seq *elts = _Py_asdl_expr_seq_new(count + 1, arena);
    CHECK_NULL(elts);
    expr_ty assignment = _PyAST_NamedExpr(placeholder, leftmost->v.Pipeline.left, EXTRAS(node), arena);
    CHECK_NULL(assignment);
    asdl_seq_SET(elts, 0, assignment);
    for (int i = 0; i < count; i++) {
        //assignment = _PyAST_NamedExpr(placeholder, asdl_seq_GET(seq, i), EXTRAS(node), arena);
        //CHECK_NULL(assignment);
        asdl_seq_SET(elts, i + 1, asdl_seq_GET(seq, i));
    }
    expr_ty tuple = _PyAST_Tuple(elts, Load, EXTRAS(node), arena);
    CHECK_NULL(tuple);

    PyObject *minus_one = PyLong_FromLong(-1);
    CHECK_NULL(minus_one);
    if (_PyArena_AddPyObject(arena, minus_one) < 0) {
        Py_DecRef(minus_one);
        return 0;
    }
    expr_ty minus_one_e = _PyAST_Constant(minus_one, NULL, EXTRAS(node), arena);
    CHECK_NULL(minus_one_e);

    node->kind = Subscript_kind;
    node->v.Subscript.ctx = Load;
    node->v.Subscript.value = tuple;
    node->v.Subscript.slice = minus_one_e;

    // only now transform the child nodes
    if (!transform_child_nodes(leftmost, seq, count, arena)) {
        return 0;
    }

    return 1;
}

static bool find_placeholder_callback(
    walk_kind_ty kind,
    walk_node_ty node,
    expr_context_ty ctx,
    void *userdata,
    callback_kind_ty cb_kind,
    void **target
) {
    if (
        cb_kind == CallbackSingle_kind &&
        kind == WalkIdentifier_kind &&
        _PyUnicode_EqualToASCIIString(node.Identifier, "_")
    ) {
        return false;
    }
    return true;
}

static bool placeholder_use_info_callback(
    walk_kind_ty kind,
    walk_node_ty node,
    expr_context_ty ctx,
    void *userdata,
    callback_kind_ty cb_kind,
    void **target
) {
    search_track_ty search_track = (search_track_ty) userdata;

    /* printf(
        "placeholder_use_info_callback(), kind: %d, node.Expr->kind: %d, ctx: %d, cb_kind: %d, depth: %d, overwritten: %d, used: %d, shadowed: %d\n",
        (int) kind,
        (int) node.Expr->kind,
        (int) ctx,
        (int) cb_kind,
        (int) search_track->depth,
        (int) search_track->overwritten,
        (int) search_track->used,
        (int) search_track->shadowed
    ); */

    if (
        kind == WalkExpr_kind &&
        node.Expr->kind == Lambda_kind
    ) {
        if (cb_kind == CallbackEarly_kind) {
            search_track->depth++;
            if (!ast_walker_arguments(node.Expr->v.Lambda.args, NULL, Load, find_placeholder_callback, NULL)) {
                search_track->shadowed++;
            }
        } else if (cb_kind == CallbackLate_kind) {
            search_track->depth--;
            if (!ast_walker_arguments(node.Expr->v.Lambda.args, NULL, Load, find_placeholder_callback, NULL)) {
                search_track->shadowed--;
            }
        }
    }

    if (
        kind == WalkExpr_kind &&
        node.Expr->kind == Pipeline_kind
    ) {
        if (cb_kind == CallbackEarly_kind) {
            search_track->depth++;
            search_track->shadowed++;
        } else if (cb_kind == CallbackLate_kind) {
            search_track->depth--;
            search_track->shadowed--;
        }
    }

    if (
        cb_kind == CallbackLate_kind &&
        kind == WalkExpr_kind &&
        node.Expr->kind == Name_kind &&
        node.Expr->v.Name.ctx == Load &&
        _PyUnicode_EqualToASCIIString(node.Expr->v.Name.id, "_")
    ) {
        if (!search_track->overwritten && !search_track->shadowed) {
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
    search_track->shadowed = 0;
}

static bool placeholder_use_info(expr_ty node, search_track_ty search_track) {
    search_track_init(search_track);
    return ast_walker_expr(node, NULL, Load, placeholder_use_info_callback, search_track);
}

static _Py_Identifier PyID_hasattr = { .string = "hasattr", .index = -1 };
static _Py_Identifier PyID___pipe__ = { .string = "__pipe__", .index = -1 };

static int handle_magic_method(expr_ty rhs_orig, identifier placeholder_id, bool last, PyArena *arena) {
    PyObject *unparsed_o = _PyAST_ExprAsUnicode(rhs_orig);
    CHECK_NULL(unparsed_o);
    if (_PyArena_AddPyObject(arena, unparsed_o) < 0) {
        Py_DecRef(unparsed_o);
        return 0;
    }
    expr_ty unparsed_e = _PyAST_Constant(unparsed_o, NULL, EXTRAS(rhs_orig), arena);
    CHECK_NULL(unparsed_e);

    expr_ty rhs = (expr_ty) _PyArena_Malloc(arena, sizeof(*rhs));
    CHECK_NULL(rhs);
    memcpy(rhs, rhs_orig, sizeof(*rhs));

    expr_ty target = NULL;
    expr_ty rhs_named_expr = NULL;
    if (rhs->kind == NamedExpr_kind) {
        rhs_named_expr = rhs;
        target = rhs->v.NamedExpr.target;
        rhs = rhs->v.NamedExpr.value;
    }

    expr_ty rhs_noinject = (expr_ty) _PyArena_Malloc(arena, sizeof(*rhs));
    CHECK_NULL(rhs_noinject);
    memcpy(rhs_noinject, rhs, sizeof(*rhs));

    if (!handle_injection(rhs, placeholder_id, arena)) {
        return 0;
    }

    identifier hasattr_id = _PyUnicode_FromId(&PyID_hasattr);
    CHECK_NULL(hasattr_id);
    identifier magic_id = _PyUnicode_FromId(&PyID___pipe__);
    CHECK_NULL(magic_id);
    
    expr_ty placeholder_load = _PyAST_Name(placeholder_id, Load, EXTRAS(rhs), arena);
    CHECK_NULL(placeholder_load);
    expr_ty magic = _PyAST_Constant(magic_id, NULL, EXTRAS(rhs), arena);
    CHECK_NULL(magic);
    expr_ty check_for_magic_method = _PyAST_Intrinsic2(INTRINSIC_HASATTR, placeholder_load, magic, EXTRAS(rhs), arena);
    CHECK_NULL(check_for_magic_method);
    
    arg_ty a = _PyAST_arg(placeholder_id, NULL, NULL, EXTRAS(rhs), arena);
    CHECK_NULL(a);
    asdl_arg_seq *lambda_args = _Py_asdl_arg_seq_new(1, arena);
    CHECK_NULL(lambda_args);
    asdl_seq_SET(lambda_args, 0, a);
    arguments_ty arguments = _PyAST_arguments(NULL, lambda_args, NULL, NULL, NULL, NULL, NULL, arena);
    CHECK_NULL(arguments);
    expr_ty lambda = _PyAST_Lambda(arguments, rhs, EXTRAS(rhs), arena);
    CHECK_NULL(lambda);
    expr_ty lambda_noinject = _PyAST_Lambda(arguments, rhs_noinject, EXTRAS(rhs), arena);
    CHECK_NULL(lambda_noinject);
    PyObject *last_c = Py_GetConstant(last ? Py_CONSTANT_TRUE : Py_CONSTANT_FALSE);
    CHECK_NULL(last_c);
    expr_ty last_e = _PyAST_Constant(last_c, NULL, EXTRAS(rhs), arena);
    CHECK_NULL(last_e);
    expr_ty none = _PyAST_Constant(Py_GetConstant(Py_CONSTANT_NONE), NULL, EXTRAS(rhs), arena);
    CHECK_NULL(none);
    expr_ty target_name = NULL;
    if (target != NULL) {
        // printf("target->kind: %d\n", target->kind);
        target_name = _PyAST_Constant(target->v.Name.id, NULL, EXTRAS(rhs), arena);
        CHECK_NULL(target_name);
    }
    asdl_expr_seq *magic_args = _Py_asdl_expr_seq_new(5, arena);
    CHECK_NULL(magic_args);
    asdl_seq_SET(magic_args, 0, lambda);
    asdl_seq_SET(magic_args, 1, lambda_noinject);
    asdl_seq_SET(magic_args, 2, last_e);
    asdl_seq_SET(magic_args, 3, target ? target_name : none);
    asdl_seq_SET(magic_args, 4, unparsed_e);
    expr_ty call_magic_method = _PyAST_Attribute(placeholder_load, magic_id, Load, EXTRAS(rhs), arena);
    CHECK_NULL(call_magic_method);
    call_magic_method = _PyAST_Call(call_magic_method, magic_args, NULL, EXTRAS(rhs), arena);
    CHECK_NULL(call_magic_method);
    expr_ty placeholder_store = _PyAST_Name(placeholder_id, Store, EXTRAS(rhs), arena);
    CHECK_NULL(placeholder_store);
    call_magic_method = _PyAST_NamedExpr(placeholder_store, call_magic_method, EXTRAS(rhs), arena);
    // (_1 := (_ := magic())[1], (_ := _[0]))[1]
    expr_ty one = _PyAST_Constant(Py_GetConstant(Py_CONSTANT_ONE), NULL, EXTRAS(rhs), arena);
    CHECK_NULL(one);
    expr_ty zero = _PyAST_Constant(Py_GetConstant(Py_CONSTANT_ZERO), NULL, EXTRAS(rhs), arena);
    CHECK_NULL(zero);
    expr_ty subscript = _PyAST_Subscript(call_magic_method, one, Load, EXTRAS(rhs), arena);
    CHECK_NULL(subscript);
    if (target != NULL) {
        subscript = _PyAST_NamedExpr(target, subscript, EXTRAS(rhs), arena);
        CHECK_NULL(subscript);
        rhs = rhs_named_expr;
    }
    expr_ty subscript2 = _PyAST_Subscript(placeholder_load, last ? one : zero, Load, EXTRAS(rhs), arena);
    CHECK_NULL(subscript2);
    subscript2 = _PyAST_NamedExpr(placeholder_store, subscript2, EXTRAS(rhs), arena);
    CHECK_NULL(subscript2);
    asdl_expr_seq *elts = _Py_asdl_expr_seq_new(2, arena);
    CHECK_NULL(elts);
    asdl_seq_SET(elts, 0, subscript);
    asdl_seq_SET(elts, 1, subscript2);
    expr_ty tuple = _PyAST_Tuple(elts, Load, EXTRAS(rhs), arena);
    CHECK_NULL(tuple);
    tuple = _PyAST_Subscript(tuple, one, Load, EXTRAS(rhs), arena);
    CHECK_NULL(tuple);

    rhs_orig->kind = IfExp_kind;
    rhs_orig->v.IfExp.test = check_for_magic_method;
    rhs_orig->v.IfExp.body = tuple;
    rhs_orig->v.IfExp.orelse = rhs;

    return 1;
}