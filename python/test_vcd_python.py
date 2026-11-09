"""Unit tests of the Python module `vcdwriter` (run by ctest with PYTHONPATH set to the module)."""
import os
import pathlib
import unittest

import vcdwriter
from vcdwriter import ScopeType, TimeScale, TimeScaleUnit, VariableType

COMPARE_VCD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "compare.vcd")
DATE = "2024-05-21 22:16:16"
HEADER = "$timescale 1 ns $end\n$date " + DATE + " $end\n"


class WriterTest(unittest.TestCase):
    def setUp(self):
        # every test writes its own file
        self.filename = self.id().rsplit(".", 2)[-2] + "_" + self._testMethodName + ".py.vcd"

    def writer(self, **kwargs):
        kwargs.setdefault("date", DATE)
        return vcdwriter.Writer(self.filename, **kwargs)

    def output(self):
        with open(self.filename, "rb") as f:
            return f.read().decode()

    def body(self):
        return self.output().split("$enddefinitions $end\n", 1)[1]

    def test_main_matches_compare_vcd(self):
        # the same program as test/main.cpp: byte-identical output
        with self.writer(date="2024-01-15 19:16:21") as w:
            counter = w.register_var("a.b.c", "counter", VariableType.integer, 8)
            var = w.register_var("a.b", "var", VariableType.integer, 8)
            for t in range(5):
                w.change(counter, t, format(10 + t * 2, "08b"))
                w.change(var, t, format(11 + t * 2, "b"))
        with open(COMPARE_VCD, "rb") as f:
            self.assertEqual(self.output(), f.read().decode())

    def test_header(self):
        with self.writer(timescale_quan=TimeScale.ten, timescale_unit=TimeScaleUnit.us,
                         comment="c", version="v") as w:
            w.register_var("top", "a", VariableType.wire, 1)
        self.assertTrue(self.output().startswith(
            "$timescale 10 us $end\n$date " + DATE + " $end\n$comment c $end\n$version v $end\n"))

    def test_no_date(self):
        with self.writer(date=""):
            pass
        self.assertNotIn("$date", self.output())

    def test_invalid_header(self):
        with self.assertRaises(vcdwriter.VCDTypeError):
            self.writer(date="yesterday")
        with self.assertRaises(vcdwriter.VCDTypeError):
            self.writer(comment="a $end b")

    def test_variable(self):
        with self.writer() as w:
            v = w.register_var("top", "data", VariableType.wire, 8)
            self.assertEqual((v.name, v.type, v.size, v.ident), ("data", VariableType.wire, 8, 0))
            self.assertEqual(w.register_var("top", "i").size, 64)  # integer by default
            self.assertIs(type(w.var("top", "data")), vcdwriter.Variable)

    def test_value_types(self):
        with self.writer() as w:
            s = w.register_var("top", "s", VariableType.integer, 1)  # a scalar
            v = w.register_var("top", "v", VariableType.wire, 8)
            r = w.register_var("top", "r", VariableType.real)
            t = w.register_var("top", "t", VariableType.string)
            e = w.register_var("top", "e", VariableType.event)
            w.change(s, 1, "1")
            w.change(v, 1, "101")
            w.change(r, 1, "1.5")
            w.change(t, 1, "hello")
            w.change(e, 1, "1")
            w.change(s, 2, "0")
            w.change(v, 2, "z")
            w.change(r, 2, "2")
            w.change(s, 3, "Z")
            w.change(v, 3, "x1")
        self.assertEqual(self.body(),
                         "#0\n$dumpvars\nx0\nbxxxxxxxx 1\nr0 2\nsx 3\n$end\n"
                         "#1\n10\nb00000101 1\nr1.5 2\nshello 3\n14\n"
                         "#2\n00\nbzzzzzzzz 1\nr2 2\n"
                         "#3\nz0\nbxxxxxxx1 1\n")

    def test_errors(self):
        with self.writer() as w:
            v = w.register_var("top", "v", VariableType.wire, 2)
            for value in ("102", "111", "1.5"):
                with self.assertRaises(vcdwriter.VCDTypeError, msg=value):
                    w.change(v, 0, value)
            with self.assertRaises(TypeError):
                w.change(v, 0, 1)  # values are str only
            with self.assertRaises(TypeError):
                w.register_var("top", "bad", "logic", 1)  # types are VariableType only
            with self.assertRaises(vcdwriter.VCDTypeError):
                w.register_var("top", "v", VariableType.wire, 2)
            with self.assertRaises(vcdwriter.VCDTypeError):
                w.register_var("top", "wire_without_size", VariableType.wire)
            self.assertTrue(issubclass(vcdwriter.VCDTypeError, vcdwriter.VCDError))

    def test_phase_errors(self):
        w = self.writer()
        v = w.register_var("top", "v", VariableType.wire, 1)
        w.change(v, 5, "1")
        with self.assertRaises(vcdwriter.VCDPhaseError):
            w.change(v, 4, "0")
        with self.assertRaises(vcdwriter.VCDPhaseError):
            w.register_var("top", "late", VariableType.wire, 1)
        w.close()
        w.close()  # a repeated call does nothing
        with self.assertRaises(vcdwriter.VCDPhaseError):
            w.change(v, 6, "0")
        self.assertTrue(issubclass(vcdwriter.VCDPhaseError, vcdwriter.VCDError))

    def test_change_by_name_and_dedup(self):
        with self.writer() as w:
            w.register_var("top.cpu", "pc", VariableType.reg, 4, init="11")
            self.assertTrue(w.change("top.cpu", "pc", 1, "100"))
            self.assertFalse(w.change("top.cpu", "pc", 2, "100"))
            with self.assertRaises(vcdwriter.VCDPhaseError):
                w.change("top.cpu", "nope", 3, "1")
        self.assertEqual(self.body(), "#0\n$dumpvars\nb0011 0\n$end\n#1\nb0100 0\n")

    def test_dump_off_on_and_scopes(self):
        with self.writer() as w:
            w.set_scope_sep("/")
            w.set_scope_default_type(ScopeType.task)
            v = w.register_var("top/sub", "v", VariableType.integer, 1, init="0")
            w.set_scope_type("top/sub", ScopeType.fork)
            w.dump_off(1)
            w.change(v, 2, "1")
            w.dump_on(3)
            w.flush(4)
        self.assertEqual(self.output(), HEADER +
                         "$scope task top $end\n$scope fork sub $end\n$var integer 1 0 v $end\n"
                         "$upscope $end\n$upscope $end\n$enddefinitions $end\n"
                         "#0\n$dumpvars\n00\n$end\n#1\n$dumpoff\nx0\n$end\n#3\n$dumpon\n10\n$end\n#4\n")

    def test_close_with_timestamp(self):
        w = self.writer()
        w.register_var("top", "v", VariableType.wire, 1)
        w.close(7)
        self.assertTrue(self.output().endswith("#7\n"))

    def test_path_like_file(self):
        with vcdwriter.Writer(pathlib.Path(self.filename), date=""):
            pass
        self.assertEqual(self.output(), "$timescale 1 ns $end\n$enddefinitions $end\n")


if __name__ == "__main__":
    unittest.main()
