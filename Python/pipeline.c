#include "Python.h"
#include "pycore_ast.h"           // _PyAST_GetDocString()
#include "pycore_format.h"        // F_LJUST
#include "pycore_long.h"          // _PyLong
#include "pycore_pystate.h"       // _PyThreadState_GET()
#include "pycore_setobject.h"     // _PySet_NextEntry()

typedef struct _PyASTOptimizeState _PyASTOptimizeState;

int astfold_expr(expr_ty node_, PyArena *ctx_, _PyASTOptimizeState *state);

#define EXTRAS(x) (x)->lineno, (x)->col_offset, (x)->end_lineno, (x)->end_col_offset

static expr_ty leftmost_call(expr_ty e, expr_ty c) {
    switch (e->kind) {
    case Attribute_kind:
        return leftmost_call(e->v.Attribute.value, c);
    case Call_kind:
        return leftmost_call(e->v.Call.func, e);
    case Subscript_kind:
        return leftmost_call(e->v.Subscript.value, c);
    default:
        break;
    }
    return c;
}

#define WALK(x) \
    if ((x) != NULL && !walk((x), callback, userdata, include_store)) { \
        return false; \
    }

#define WALK_SEQ(x) \
    for (int i = 0; i < asdl_seq_LEN(x); i++) { \
        WALK(asdl_seq_GET((x), i)); \
    }

#define WALK_ARG_SEQ(x) \
    for (int i = 0; i < asdl_seq_LEN(x); i++) { \
        struct _expr tmp; \
        tmp.kind = Name_kind; \
        tmp.v.Name.ctx = Store; \
        tmp.v.Name.id = asdl_seq_GET((x), i)->arg; \
        if (include_store) \
            WALK(&tmp); \
    }

#define WALK_ARG(x) \
    { \
        struct _expr tmp; \
        tmp.kind = Name_kind; \
        tmp.v.Name.ctx = Store; \
        tmp.v.Name.id = x->arg; \
        if (include_store) \
            WALK(&tmp); \
    }

#define WALK_ARGS(x) { \
    WALK_ARG_SEQ(x->posonlyargs); \
    WALK_ARG_SEQ(x->args); \
    WALK_ARG(x->vararg); \
    WALK_ARG_SEQ(x->kwonlyargs); \
    WALK_SEQ(x->kw_defaults); \
    WALK_ARG(x->kwarg); \
    WALK_SEQ(x->defaults); \
}

#define WALK_COMPR_SEQ(x) \
    for (int i = 0; i < asdl_seq_LEN(x); i++) { \
        comprehension_ty c = asdl_seq_GET((x), i); \
        WALK(c->target); \
        WALK(c->iter); \
        WALK_SEQ(c->ifs); \
    }

#define WALK_KEYWORD_SEQ(x) \
    for (int i = 0; i < asdl_seq_LEN(x); i++) { \
        keyword_ty kwd = asdl_seq_GET((x), i); \
        WALK_ARG(kwd); \
        WALK(kwd->value); \
    }

#define WALK_IDENTIFIER(x, y) \
    { \
        struct _expr tmp; \
        tmp.kind = Name_kind; \
        tmp.v.Name.ctx = (y); \
        tmp.v.Name.id = (x); \
        if ((y) != Store || include_store) \
            WALK(&tmp); \
    }

static bool walk(expr_ty node, bool(*callback)(expr_ty, void*), void *userdata, bool include_store) {
    if (node == NULL) {
        return true;
    }
    switch (node->kind) {
        case BoolOp_kind:
            WALK_SEQ(node->v.BoolOp.values);
            break;
        case NamedExpr_kind:
            if (include_store)
                WALK(node->v.NamedExpr.target);
            WALK(node->v.NamedExpr.value);
            break;
        case BinOp_kind:
            WALK(node->v.BinOp.left);
            WALK(node->v.BinOp.right);
            break;
        case UnaryOp_kind:
            WALK(node->v.UnaryOp.operand);
            break;
        case Lambda_kind:
            if (include_store)
                WALK_ARGS(node->v.Lambda.args);
            WALK(node->v.Lambda.body);
            break;
        case IfExp_kind:
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
            break;
    }
    if (callback(node, userdata)) {
        return true;
    } else {
        return false;
    }
}

#define WALK_STMT(x) \
    if ((x) != NULL && !walk_stmt((x), callback, userdata, include_store)) { \
        return false; \
    }

#define WALK_STMT_SEQ(x) \
    for (int i = 0; i < asdl_seq_LEN(x); i++) { \
        WALK_STMT(asdl_seq_GET((x), i)); \
    }

#define WALK_TYPE_PARAM(x) \
    switch((x)->kind) { \
    case TypeVar_kind: \
        WALK((x)->v.TypeVar.bound); \
        WALK((x)->v.TypeVar.default_value); \
        break; \
    case ParamSpec_kind: \
        WALK((x)->v.ParamSpec.default_value); \
        break; \
    case TypeVarTuple_kind: \
        WALK((x)->v.TypeVarTuple.default_value); \
        break; \
    }

#define WALK_TYPE_PARAM_SEQ(x) \
    for (int i = 0; i < asdl_seq_LEN(x); i++) { \
        WALK_TYPE_PARAM(asdl_seq_GET((x), i)); \
    }

#define WALK_WITHITEM(x) { \
        WALK((x)->context_expr); \
        WALK((x)->optional_vars); \
    }

#define WALK_WITHITEM_SEQ(x) \
    for (int i = 0; i < asdl_seq_LEN(x); i++) { \
        WALK_WITHITEM(asdl_seq_GET((x), i)); \
    }

#define WALK_EXC_HANDLER(x) \
    switch ((x)->kind) { \
    case ExceptHandler_kind: \
        WALK((x)->v.ExceptHandler.type); \
        WALK_STMT_SEQ((x)->v.ExceptHandler.body); \
        break; \
    }

#define WALK_EXC_HANDLER_SEQ(x) \
    for (int i = 0; i < asdl_seq_LEN(x); i++) { \
        WALK_EXC_HANDLER(asdl_seq_GET((x), i)); \
    }

static bool walk_stmt(stmt_ty node, bool(*callback)(expr_ty, void*), void *userdata, bool include_store) {
    switch (node->kind) {
    case FunctionDef_kind:
        WALK_ARGS(node->v.FunctionDef.args);
        WALK_STMT_SEQ(node->v.FunctionDef.body);
        WALK_SEQ(node->v.FunctionDef.decorator_list);
        WALK(node->v.FunctionDef.returns);
        WALK_TYPE_PARAM_SEQ(node->v.FunctionDef.type_params);
        break;
    case AsyncFunctionDef_kind:
        WALK_ARGS(node->v.AsyncFunctionDef.args);
        WALK_STMT_SEQ(node->v.AsyncFunctionDef.body);
        WALK_SEQ(node->v.AsyncFunctionDef.decorator_list);
        WALK(node->v.AsyncFunctionDef.returns);
        WALK_TYPE_PARAM_SEQ(node->v.AsyncFunctionDef.type_params);
        break;
    case ClassDef_kind:
        WALK_SEQ(node->v.ClassDef.bases);
        WALK_STMT_SEQ(node->v.ClassDef.body);
        WALK_SEQ(node->v.ClassDef.decorator_list);
        WALK_KEYWORD_SEQ(node->v.ClassDef.keywords);
        WALK_TYPE_PARAM_SEQ(node->v.ClassDef.type_params);
        break;
    case Return_kind:
        WALK(node->v.Return.value);
        break;
    case Delete_kind:
        WALK_SEQ(node->v.Delete.targets);
        break;
    case Assign_kind:
        WALK_SEQ(node->v.Assign.targets);
        WALK(node->v.Assign.value);
        break;
    case TypeAlias_kind:
        WALK(node->v.TypeAlias.name);
        WALK_TYPE_PARAM_SEQ(node->v.TypeAlias.type_params);
        WALK(node->v.TypeAlias.value);
        break;
    case AugAssign_kind:
        WALK(node->v.AugAssign.target);
        WALK(node->v.AugAssign.value);
        break;
    case AnnAssign_kind:
        WALK(node->v.AnnAssign.annotation);
        WALK(node->v.AnnAssign.target);
        WALK(node->v.AnnAssign.value);
        break;
    case For_kind:
        WALK_STMT_SEQ(node->v.For.body);
        WALK(node->v.For.iter);
        WALK_STMT_SEQ(node->v.For.orelse);
        WALK(node->v.For.target);
        break;
    case AsyncFor_kind:
        WALK_STMT_SEQ(node->v.AsyncFor.body);
        WALK(node->v.AsyncFor.iter);
        WALK_STMT_SEQ(node->v.AsyncFor.orelse);
        WALK(node->v.AsyncFor.target);
        break;
    case While_kind:
        WALK_STMT_SEQ(node->v.While.body);
        WALK_STMT_SEQ(node->v.While.orelse);
        WALK(node->v.While.test);
        break;
    case If_kind:
        WALK_STMT_SEQ(node->v.If.body);
        WALK_STMT_SEQ(node->v.If.orelse);
        WALK(node->v.If.test);
        break;
    case With_kind:
        WALK_STMT_SEQ(node->v.With.body);
        WALK_WITHITEM_SEQ(node->v.With.items);
        break;
    case AsyncWith_kind:
        WALK_STMT_SEQ(node->v.AsyncWith.body);
        WALK_WITHITEM_SEQ(node->v.AsyncWith.items);
        break;
    case Match_kind:
        WALK(node->v.Match.subject);
        break;
    case Raise_kind:
        WALK(node->v.Raise.cause);
        WALK(node->v.Raise.exc);
        break;
    case Try_kind:
        WALK_STMT_SEQ(node->v.Try.body);
        WALK_STMT_SEQ(node->v.Try.finalbody);
        WALK_EXC_HANDLER_SEQ(node->v.Try.handlers);
        WALK_STMT_SEQ(node->v.Try.orelse);
        break;
    case TryStar_kind:
        WALK_STMT_SEQ(node->v.TryStar.body);
        WALK_STMT_SEQ(node->v.TryStar.finalbody);
        WALK_EXC_HANDLER_SEQ(node->v.TryStar.handlers);
        WALK_STMT_SEQ(node->v.TryStar.orelse);
        break;
    case Assert_kind:
        WALK(node->v.Assert.msg);
        WALK(node->v.Assert.test);
        break;
    case Import_kind:
        break;
    case ImportFrom_kind:
        break;
    case Global_kind:
        break;
    case Nonlocal_kind:
        break;
    case Expr_kind:
        WALK(node->v.Expr.value);
        break;
    case Pass_kind:
        break;
    case Break_kind:
        break;
    case Continue_kind:
        break;
    }

    return true;
}

static bool find_placeholder_callback(expr_ty node, void *placeholder_found) {
    if (node->kind == Name_kind && _PyUnicode_EqualToASCIIString(node->v.Name.id, "_")) {
        *((bool*) placeholder_found) = true;
        return false;
    }
    return true;
}

static bool contains_placeholder(expr_ty node) {
    bool placeholder_found = false;
    walk(node, find_placeholder_callback, &placeholder_found, true);
    return placeholder_found;
}

static int transform_pipeline(expr_ty node, PyArena *arena, _PyASTOptimizeState *state) {
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

    return astfold_expr(lhs, arena, state);
}


int handle_pipeline(expr_ty node, PyArena *arena, _PyASTOptimizeState *state) {
    return transform_pipeline(node, arena, state);
}
