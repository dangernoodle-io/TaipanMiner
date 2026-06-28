"""Offline unit tests for fleetlib.expr — safe field-expression evaluator."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from fleetlib.expr import compile_expr, ExprError


class TestBasicComparisons(unittest.TestCase):
    def test_gt_true(self):
        p = compile_expr("internal.free > 1000")
        self.assertTrue(p.eval({"internal": {"free": 2000}}))

    def test_gt_false(self):
        p = compile_expr("internal.free > 1000")
        self.assertFalse(p.eval({"internal": {"free": 500}}))

    def test_lt(self):
        self.assertTrue(compile_expr("x < 10").eval({"x": 5}))
        self.assertFalse(compile_expr("x < 10").eval({"x": 15}))

    def test_lte(self):
        self.assertTrue(compile_expr("x <= 10").eval({"x": 10}))
        self.assertFalse(compile_expr("x <= 10").eval({"x": 11}))

    def test_gte(self):
        self.assertTrue(compile_expr("x >= 10").eval({"x": 10}))

    def test_eq(self):
        self.assertTrue(compile_expr("x == 5").eval({"x": 5}))
        self.assertFalse(compile_expr("x == 5").eval({"x": 4}))

    def test_neq(self):
        self.assertTrue(compile_expr("x != 5").eval({"x": 6}))


class TestBoolOps(unittest.TestCase):
    def test_and_true(self):
        p = compile_expr("a > 1 and b < 10")
        self.assertTrue(p.eval({"a": 5, "b": 3}))

    def test_and_false_left(self):
        p = compile_expr("a > 1 and b < 10")
        self.assertFalse(p.eval({"a": 0, "b": 3}))

    def test_and_false_right(self):
        p = compile_expr("a > 1 and b < 10")
        self.assertFalse(p.eval({"a": 5, "b": 20}))

    def test_or_true(self):
        p = compile_expr("a > 100 or b < 10")
        self.assertTrue(p.eval({"a": 0, "b": 5}))

    def test_or_false(self):
        p = compile_expr("a > 100 or b < 10")
        self.assertFalse(p.eval({"a": 0, "b": 50}))


class TestUnaryNot(unittest.TestCase):
    def test_not_false_to_true(self):
        p = compile_expr("not (a == 1)")
        self.assertTrue(p.eval({"a": 2}))

    def test_not_true_to_false(self):
        p = compile_expr("not (a == 1)")
        self.assertFalse(p.eval({"a": 1}))


class TestParens(unittest.TestCase):
    def test_parens_grouping(self):
        # (a > 0 and b > 0) or c > 0
        p = compile_expr("(a > 0 and b > 0) or c > 0")
        self.assertTrue(p.eval({"a": 1, "b": 1, "c": 0}))
        self.assertTrue(p.eval({"a": 0, "b": 0, "c": 1}))
        self.assertFalse(p.eval({"a": 0, "b": 1, "c": 0}))


class TestMissingField(unittest.TestCase):
    def test_missing_field_is_false(self):
        p = compile_expr("missing.field > 0")
        self.assertFalse(p.eval({}))

    def test_partially_missing(self):
        p = compile_expr("a.b.c > 0")
        self.assertFalse(p.eval({"a": {"b": {}}}))


class TestMalformed(unittest.TestCase):
    def test_incomplete_expr_raises(self):
        with self.assertRaises(ExprError):
            compile_expr("a > ")

    def test_empty_raises(self):
        with self.assertRaises(ExprError):
            compile_expr("")

    def test_just_name_raises(self):
        # a bare Name is not a valid boolean expr (Compare required)
        with self.assertRaises(ExprError):
            compile_expr("os")


class TestInjectionRejection(unittest.TestCase):
    def test_dunder_import_rejected(self):
        with self.assertRaises(ExprError):
            compile_expr("__import__('os').system('id')")

    def test_open_call_rejected(self):
        with self.assertRaises(ExprError):
            compile_expr("open('/etc/passwd').read()")

    def test_call_node_rejected(self):
        with self.assertRaises(ExprError):
            compile_expr("len(x) > 0")

    def test_any_call_rejected(self):
        with self.assertRaises(ExprError):
            compile_expr("x > abs(-1)")

    def test_dunder_attr_rejected(self):
        with self.assertRaises(ExprError):
            compile_expr("x.__class__ == 1")


class TestStringLiterals(unittest.TestCase):
    def test_string_eq_true(self):
        p = compile_expr("status == 'ok'")
        self.assertTrue(p.eval({"status": "ok"}))

    def test_string_eq_false(self):
        p = compile_expr("status == 'ok'")
        self.assertFalse(p.eval({"status": "error"}))


class TestBoolNullLiterals(unittest.TestCase):
    def test_true_literal(self):
        p = compile_expr("enabled == true")
        self.assertTrue(p.eval({"enabled": True}))

    def test_false_literal(self):
        p = compile_expr("enabled == false")
        self.assertTrue(p.eval({"enabled": False}))

    def test_null_literal(self):
        p = compile_expr("val == null")
        self.assertTrue(p.eval({"val": None}))

    def test_null_not_equal(self):
        p = compile_expr("val != null")
        self.assertTrue(p.eval({"val": 1}))


class TestFloatLiteral(unittest.TestCase):
    def test_float_gt_true(self):
        p = compile_expr("x > 1.5")
        self.assertTrue(p.eval({"x": 2.0}))

    def test_float_gt_false(self):
        p = compile_expr("x > 1.5")
        self.assertFalse(p.eval({"x": 1.0}))


if __name__ == "__main__":
    unittest.main()
