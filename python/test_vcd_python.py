"""Unit tests of the Python module `vcdwriter` (run by ctest with PYTHONPATH set to the module)."""
import os
import unittest

import vcdwriter

COMPARE_VCD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "compare.vcd")
DATE = "2024-05-21 22:16:16"
HEADER = "$timescale 1 ns $end\n$date " + DATE + " $end\n"


class WriterTest(unittest.TestCase):
    def setUp(self):
        # every test writes its own file
        self.filename = self.id().rsplit(".", 2)[-2] + "_" + self._testMethodName + ".py.vcd"

    def writer(self, **kwargs):
        kwargs.setdefault("timescale", "1 ns")
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
            counter = w.register_var("a.b.c", "counter", "integer", 8)
            var = w.register_var("a.b", "var", "integer", 8)
            for t in range(5):
                w.change(counter, t, format(10 + t * 2, "08b"))
                w.change(var, t, 11 + t * 2)
        with open(COMPARE_VCD, "rb") as f:
            self.assertEqual(self.output(), f.read().decode())

    def test_header(self):
        with self.writer(timescale="10us", comment="c", version="v") as w:
            w.register_var("top", "a", "wire", 1)
        self.assertTrue(self.output().startswith(
            "$timescale 10 us $end\n$date " + DATE + " $end\n$comment c $end\n$version v $end\n"))

    def test_no_date(self):
        with self.writer(date=""):
            pass
        self.assertNotIn("$date", self.output())

    def test_invalid_header(self):
        for timescale in ("2 ns", "1 xs", "ns", "266 ns", ""):
            with self.assertRaises(vcdwriter.VCDTypeError, msg=timescale):
                self.writer(timescale=timescale)
        with self.assertRaises(vcdwriter.VCDTypeError):
            self.writer(date="yesterday")

    def test_variable(self):
        with self.writer() as w:
            v = w.register_var("top", "data", "wire", 8)
            self.assertEqual((v.name, v.type, v.size, v.ident), ("data", "wire", 8, 0))
            self.assertEqual(w.register_var("top", "i", "integer").size, 64)
            self.assertIs(type(w.var("top", "data")), vcdwriter.Variable)

    def test_value_types(self):
        with self.writer() as w:
            s = w.register_var("top", "s", "integer", 1)  # a scalar
            v = w.register_var("top", "v", "wire", 8)
            r = w.register_var("top", "r", "real")
            t = w.register_var("top", "t", "string")
            e = w.register_var("top", "e", "event")
            w.change(s, 1, True)
            w.change(v, 1, 5)
            w.change(r, 1, 1.5)
            w.change(t, 1, "hello")
            w.change(e, 1, True)
            w.change(s, 2, 0)
            w.change(v, 2, None)
            w.change(r, 2, 2)
            w.change(s, 3, "z")
            w.change(v, 3, "x1")
        self.assertEqual(self.body(),
                         "#0\n$dumpvars\nx0\nbxxxxxxxx 1\nr0 2\nsx 3\n$end\n"
                         "#1\n10\nb00000101 1\nr1.5 2\nshello 3\n14\n"
                         "#2\n00\nbzzzzzzzz 1\nr2 2\n"
                         "#3\nz0\nbxxxxxxx1 1\n")

    def test_negative_and_wide_ints(self):
        with self.writer() as w:
            v = w.register_var("top", "v", "wire", 4)
            wide = w.register_var("top", "w", "wire", 70)
            w.change(v, 1, -1)
            w.change(wide, 1, 1 << 69)
            w.change(v, 2, -8)
            w.change(wide, 2, -1)
            with self.assertRaises(vcdwriter.VCDTypeError):
                w.change(v, 3, -9)
            with self.assertRaises(vcdwriter.VCDTypeError):
                w.change(v, 3, 16)
        body = self.body()
        self.assertIn("#1\nb1111 0\nb1" + "0" * 69 + " 1\n", body)
        self.assertIn("#2\nb1000 0\nb" + "1" * 70 + " 1\n", body)
        self.assertNotIn("#3", body)

    def test_errors_are_value_errors(self):
        with self.writer() as w:
            v = w.register_var("top", "v", "wire", 2)
            with self.assertRaises(ValueError):
                w.change(v, 0, "102")
            with self.assertRaises(vcdwriter.VCDTypeError):
                w.change(v, 0, 1.5)
            with self.assertRaises(vcdwriter.VCDTypeError):
                w.register_var("top", "bad", "logic", 1)
            with self.assertRaises(vcdwriter.VCDTypeError):
                w.register_var("top", "v", "wire", 2)
            with self.assertRaises(vcdwriter.VCDTypeError):
                w.register_var("top", "wire_without_size", "wire")
            self.assertTrue(issubclass(vcdwriter.VCDTypeError, vcdwriter.VCDError))

    def test_phase_errors(self):
        w = self.writer()
        v = w.register_var("top", "v", "wire", 1)
        w.change(v, 5, 1)
        with self.assertRaises(vcdwriter.VCDPhaseError):
            w.change(v, 4, 0)
        with self.assertRaises(vcdwriter.VCDPhaseError):
            w.register_var("top", "late", "wire", 1)
        w.close()
        w.close()  # a repeated call does nothing
        with self.assertRaises(vcdwriter.VCDPhaseError):
            w.change(v, 6, 0)
        self.assertTrue(issubclass(vcdwriter.VCDPhaseError, vcdwriter.VCDError))

    def test_change_by_name_and_dedup(self):
        with self.writer() as w:
            w.register_var("top.cpu", "pc", "reg", 4, init=3)
            self.assertTrue(w.change("top.cpu", "pc", 1, 4))
            self.assertFalse(w.change("top.cpu", "pc", 2, 4))
            with self.assertRaises(vcdwriter.VCDPhaseError):
                w.change("top.cpu", "nope", 3, 1)
        self.assertEqual(self.body(), "#0\n$dumpvars\nb0011 0\n$end\n#1\nb0100 0\n")

    def test_dump_off_on_and_scopes(self):
        with self.writer() as w:
            w.set_scope_sep("/")
            w.set_scope_default_type("task")
            v = w.register_var("top/sub", "v", "integer", 1, init=0)
            w.set_scope_type("top/sub", "fork")
            w.dump_off(1)
            w.change(v, 2, 1)
            w.dump_on(3)
            w.flush(4)
        self.assertEqual(self.output(), HEADER +
                         "$scope task top $end\n$scope fork sub $end\n$var integer 1 0 v $end\n"
                         "$upscope $end\n$upscope $end\n$enddefinitions $end\n"
                         "#0\n$dumpvars\n00\n$end\n#1\n$dumpoff\nx0\n$end\n#3\n$dumpon\n10\n$end\n#4\n")

    def test_close_with_timestamp(self):
        w = self.writer()
        w.register_var("top", "v", "wire", 1)
        w.close(7)
        self.assertTrue(self.output().endswith("#7\n"))

    def test_path_like_file(self):
        import pathlib
        with vcdwriter.Writer(pathlib.Path(self.filename), date=""):
            pass
        self.assertEqual(self.output(), "$timescale 1 us $end\n$enddefinitions $end\n")


if __name__ == "__main__":
    unittest.main()
