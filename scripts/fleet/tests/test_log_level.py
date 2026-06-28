"""Tests that --log-level is accepted both before and after the subcommand."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

import fleet


class TestLogLevelPlacement(unittest.TestCase):
    def _parse(self, argv):
        p = fleet._build_parser()
        return p.parse_args(argv)

    def test_log_level_before_subcommand(self):
        args = self._parse(["--log-level", "DEBUG", "discover", "--hosts", "127.0.0.1"])
        self.assertEqual(args.log_level, "DEBUG")

    def test_log_level_after_subcommand(self):
        args = self._parse(["discover", "--log-level", "INFO", "--hosts", "127.0.0.1"])
        self.assertEqual(args.log_level, "INFO")

    def test_log_level_default_is_warning(self):
        args = self._parse(["discover", "--hosts", "127.0.0.1"])
        self.assertEqual(args.log_level, "WARNING")

    def test_log_level_after_call_subcommand(self):
        args = self._parse(["call", "--log-level", "ERROR", "GET", "/api/info",
                            "--hosts", "127.0.0.1"])
        self.assertEqual(args.log_level, "ERROR")


if __name__ == "__main__":
    unittest.main()
