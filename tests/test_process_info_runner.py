"""Guard against silent selftest misplacement when proc.c changes."""
import unittest

from run_process_info import ROOT, instrument_proc


class InstrumentationTests(unittest.TestCase):
    def setUp(self):
        self.source = (ROOT / "kernel/proc.c").read_text()

    def test_calls_are_inside_forkret_and_after_initialization(self):
        result = instrument_proc(self.source)
        start = result.index("\nforkret(void)")
        end = result.index("\n}", start)
        body = result[start:end]
        self.assertLess(body.index("process_info_test_worker();"),
                        body.index("release(&myproc()->lock);"))
        self.assertLess(body.index("first = 0;"), body.index("process_info_selftest();"))
        self.assertLess(body.index("__sync_synchronize();"),
                        body.index("process_info_selftest();"))
        self.assertEqual(result.count("    process_info_selftest();"), 1)

    def test_comments_and_unrelated_barriers_do_not_select_hooks(self):
        source = self.source.replace("Still holding p->lock from scheduler.",
                                     "Changed explanatory comment.")
        source += "\nvoid unrelated(void) { __sync_synchronize(); }\n"
        result = instrument_proc(source)
        self.assertIn("Changed explanatory comment.", result)
        self.assertEqual(result.count("    process_info_selftest();"), 1)

    def test_missing_barrier_reports_source_change(self):
        with self.assertRaisesRegex(RuntimeError, "one barrier in forkret"):
            instrument_proc(self.source.replace("__sync_synchronize();", ""))

    def test_duplicate_definition_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "exactly one"):
            instrument_proc(self.source + "\nvoid\nforkret(void)\n{\n}\n")


if __name__ == "__main__":
    unittest.main()
