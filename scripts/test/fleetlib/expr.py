"""Safe field-expression evaluator for fleet watch --until / --alert.

Uses ast module with a strict node allowlist. No eval/exec.
"""
from __future__ import annotations
import ast
import re
from typing import Any


class ExprError(ValueError):
    """Raised for invalid or disallowed expressions."""


# Python builtins that must never be accessible as names
_BUILTINS = frozenset(dir(__builtins__) if isinstance(__builtins__, dict) else dir(__builtins__))

_ALLOWED_OPS = (
    ast.Lt, ast.LtE, ast.Gt, ast.GtE, ast.Eq, ast.NotEq,
    ast.And, ast.Or, ast.Not,
)


def _field_path(node: ast.expr) -> str:
    """Extract dotted field path from a chain of Name/Attribute nodes.

    Returns the dotted string, e.g. 'internal.free'.
    Raises ExprError for anything that is not a plain name/attribute chain.
    """
    if isinstance(node, ast.Name):
        if node.id.startswith("__"):
            raise ExprError(f"dunder name not allowed: {node.id!r}")
        if node.id in _BUILTINS:
            raise ExprError(f"builtin name not allowed: {node.id!r}")
        return node.id
    if isinstance(node, ast.Attribute):
        if node.attr.startswith("__"):
            raise ExprError(f"dunder attribute not allowed: {node.attr!r}")
        if node.attr in _BUILTINS:
            raise ExprError(f"builtin attribute not allowed: {node.attr!r}")
        return _field_path(node.value) + "." + node.attr
    raise ExprError(f"unsupported node in field path: {type(node).__name__}")


def _check_node(node: ast.AST, _root: bool = False) -> None:
    """Walk the AST and raise ExprError on any disallowed node type."""
    if isinstance(node, ast.Expression):
        body = node.body
        # Root expression body must be a Compare, BoolOp, or UnaryOp — not a bare name/constant
        if not isinstance(body, (ast.Compare, ast.BoolOp, ast.UnaryOp)):
            raise ExprError(
                f"expression must be a comparison or boolean, got {type(body).__name__}"
            )
        _check_node(body)

    elif isinstance(node, ast.BoolOp):
        if not isinstance(node.op, (ast.And, ast.Or)):
            raise ExprError(f"unsupported bool op: {type(node.op).__name__}")
        for v in node.values:
            _check_node(v)

    elif isinstance(node, ast.UnaryOp):
        if not isinstance(node.op, ast.Not):
            raise ExprError(f"unsupported unary op: {type(node.op).__name__}")
        _check_node(node.operand)

    elif isinstance(node, ast.Compare):
        for op in node.ops:
            if not isinstance(op, (ast.Lt, ast.LtE, ast.Gt, ast.GtE, ast.Eq, ast.NotEq)):
                raise ExprError(f"unsupported comparison op: {type(op).__name__}")
        # left must be a field path (Name/Attribute chain)
        _field_path(node.left)
        # comparators must all be Constants
        for comp in node.comparators:
            if not isinstance(comp, ast.Constant):
                raise ExprError(
                    f"comparator must be a literal constant, got {type(comp).__name__}"
                )

    elif isinstance(node, ast.Constant):
        pass  # int, float, str, bool, None — all fine

    elif isinstance(node, ast.Name):
        _field_path(node)  # validates name is not dunder/builtin

    elif isinstance(node, ast.Attribute):
        _field_path(node)  # validates chain

    elif isinstance(node, ast.Call):
        raise ExprError("function calls are not allowed in expressions")

    else:
        raise ExprError(f"disallowed AST node type: {type(node).__name__}")


def _preprocess(s: str) -> str:
    """Replace JSON/JS boolean/null literals with Python equivalents."""
    s = re.sub(r'\btrue\b', 'True', s)
    s = re.sub(r'\bfalse\b', 'False', s)
    s = re.sub(r'\bnull\b', 'None', s)
    return s


def _eval_node(node: ast.expr, sample: dict) -> Any:
    """Recursively evaluate a validated AST node against sample."""
    if isinstance(node, ast.Expression):
        return _eval_node(node.body, sample)

    if isinstance(node, ast.BoolOp):
        if isinstance(node.op, ast.And):
            for v in node.values:
                if not _eval_node(v, sample):
                    return False
            return True
        else:  # Or
            for v in node.values:
                if _eval_node(v, sample):
                    return True
            return False

    if isinstance(node, ast.UnaryOp):  # Not
        return not _eval_node(node.operand, sample)

    if isinstance(node, ast.Compare):
        _SENTINEL = object()
        path = _field_path(node.left)
        # Use sentinel to distinguish "key absent" from "key present with value None"
        def _get(obj: dict, dotpath: str) -> Any:
            parts = dotpath.split(".")
            cur: Any = obj
            for p in parts:
                if not isinstance(cur, dict) or p not in cur:
                    return _SENTINEL
                cur = cur[p]
            return cur
        left = _get(sample, path)
        if left is _SENTINEL:
            return False  # missing field => False
        result = True
        current = left
        for op, comp_node in zip(node.ops, node.comparators):
            right = comp_node.value  # ast.Constant.value
            try:
                if isinstance(op, ast.Lt):
                    result = current < right
                elif isinstance(op, ast.LtE):
                    result = current <= right
                elif isinstance(op, ast.Gt):
                    result = current > right
                elif isinstance(op, ast.GtE):
                    result = current >= right
                elif isinstance(op, ast.Eq):
                    result = current == right
                elif isinstance(op, ast.NotEq):
                    result = current != right
                else:
                    return False
            except TypeError:
                return False
            if not result:
                return False
            current = right
        return result

    if isinstance(node, ast.Constant):
        return node.value

    return False


class Predicate:
    """Compiled expression predicate."""

    def __init__(self, tree: ast.Expression, source: str):
        self._tree = tree
        self._source = source

    def eval(self, sample: dict) -> bool:
        """Evaluate against a flat/nested dict. Missing field => False."""
        try:
            return bool(_eval_node(self._tree, sample))
        except Exception:
            return False

    def __repr__(self) -> str:
        return f"Predicate({self._source!r})"


def compile_expr(s: str) -> Predicate:
    """Parse and compile an expression string.

    Raises ExprError on bad syntax or disallowed AST nodes.
    """
    processed = _preprocess(s)
    try:
        tree = ast.parse(processed, mode="eval")
    except SyntaxError as e:
        raise ExprError(f"syntax error in expression {s!r}: {e}") from e

    try:
        _check_node(tree)
    except ExprError:
        raise
    except Exception as e:
        raise ExprError(f"expression validation failed: {e}") from e

    return Predicate(tree, s)
