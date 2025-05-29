#ifndef Py_CORE_WALKER_H
#define Py_CORE_WALKER_H

#include "pycore_ast.h"

#ifdef __cplusplus
extern "C" {
#endif

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

bool ast_walker_expr(expr_ty, expr_context_ty, AST_WALKER_CALLBACK, void*);
bool ast_walker_stmt(stmt_ty, expr_context_ty, AST_WALKER_CALLBACK, void*);
bool ast_walker_mod(mod_ty, expr_context_ty, AST_WALKER_CALLBACK, void*);
bool ast_walker(walk_kind_ty, walk_node_ty, expr_context_ty, AST_WALKER_CALLBACK, void*);

#ifdef __cplusplus
}
#endif
#endif
