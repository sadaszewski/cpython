#include "Python.h"
#include "pycore_ast.h"           // _PyAST_GetDocString()
#include "pycore_format.h"        // F_LJUST
#include "pycore_long.h"          // _PyLong
#include "pycore_pystate.h"       // _PyThreadState_GET()
#include "pycore_setobject.h"     // _PySet_NextEntry()


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

typedef struct _PyASTOptimizeState _PyASTOptimizeState;

int handle_pipeline(expr_ty node_, PyArena *ctx_, _PyASTOptimizeState *state) {
    //_Py_asdl_expr_seq_new(ctx_);
    printf("handle_pipeline()\n");
    printf("leftmost_call: 0x%08llX\n", (unsigned long long) leftmost_call);
    printf("node_: 0x%08llX\n", (unsigned long long) node_);
    printf("node_->v.Pipeline.right: 0x%08llX\n", (unsigned long long) node_->v.Pipeline.right);
    expr_ty rhs_leftmost_call = leftmost_call(node_->v.Pipeline.right, NULL);
    if (rhs_leftmost_call) {
        printf("rhs_leftmost_call->func.kind: %d\n", rhs_leftmost_call->v.Call.func->kind);
        printf("rhs_leftmost_call->args: 0x%08llX\n", (unsigned long long) rhs_leftmost_call->v.Call.args);
        if (rhs_leftmost_call->v.Call.args) {
            printf("rhs_leftmost_call->args->size: %ld\n", rhs_leftmost_call->v.Call.args->size);
        }
    } else {
        printf("No leftmost RHS call!");
    }
    bool placeholder_found = contains_placeholder(node_->v.Pipeline.right);
    printf("placeholder_found: %d\n", (int) placeholder_found);
    if (!rhs_leftmost_call) {
        return 1;
    }
    return 1;
}
