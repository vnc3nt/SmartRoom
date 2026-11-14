import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildScriptTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copyfile(ROOT / "build.sh", self.root / "build.sh")
        self.sdk = self.root / "sdk with spaces"
        self.sdk.mkdir()
        (self.sdk / "export.sh").write_text(
            'export PATH="$IDF_PATH/bin:$PATH"\n', encoding="utf-8")
        (self.sdk / "bin").mkdir()
        mock = self.sdk / "bin" / "idf.py"
        mock.write_text(
            '#!/usr/bin/env python3\n'
            'import json, os, sys\n'
            'with open(os.environ["CALL_LOG"], "a") as f: '
            'f.write(json.dumps(sys.argv[1:]) + "\\n")\n'
            'sys.exit(17 if sys.argv[1:] == [os.environ.get("FAIL_COMMAND")] else 0)\n',
            encoding="utf-8")
        mock.chmod(0o755)
        self.log = self.root / "calls.jsonl"
        self.env = dict(os.environ, IDF_PATH=str(self.sdk), CALL_LOG=str(self.log))

    def run_build(self, *args):
        return subprocess.run(["bash", str(self.root / "build.sh"), *args],
                              cwd="/", env=self.env, capture_output=True, text=True)

    def calls(self):
        import json
        return [json.loads(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def test_build_and_explicit_idf_path(self):
        self.assertEqual(self.run_build().returncode, 0)
        self.assertEqual(self.calls(), [["--version"], ["build"], ["size"]])

    def test_flash_and_monitor_use_exact_port(self):
        self.assertEqual(self.run_build("--clean", "--all", "--port", "/dev/cu.usbmodem test").returncode, 0)
        self.assertEqual(self.calls(), [["--version"], ["fullclean"], ["build"], ["size"],
                                      ["-p", "/dev/cu.usbmodem test", "flash"],
                                      ["-p", "/dev/cu.usbmodem test", "monitor"]])

    def test_no_automatic_device_selection(self):
        for option in ("--flash", "--monitor", "--all"):
            with self.subTest(option=option):
                self.assertEqual(self.run_build(option).returncode, 2)
        self.assertEqual(self.calls(), [])

    def test_missing_port_value(self):
        for args in (("--port",), ("--port", "--all"), ("--port", "")):
            with self.subTest(args=args):
                self.assertEqual(self.run_build(*args).returncode, 2)
        self.assertEqual(self.calls(), [])

    def test_unknown_argument(self):
        self.assertEqual(self.run_build("--unknown").returncode, 2)
        self.assertEqual(self.calls(), [])

    def test_failed_build_never_flashes(self):
        self.env["FAIL_COMMAND"] = "build"
        self.assertEqual(self.run_build("--all", "--port", "/dev/ttyACM0").returncode, 17)
        self.assertEqual(self.calls(), [["--version"], ["build"]])

    def test_invalid_idf_path(self):
        self.env["IDF_PATH"] = str(self.root / "missing")
        self.assertEqual(self.run_build().returncode, 2)
        self.assertEqual(self.calls(), [])

    def test_help_does_not_require_sdk(self):
        self.env["IDF_PATH"] = str(self.root / "missing")
        self.assertEqual(self.run_build("--help").returncode, 0)
        self.assertEqual(self.calls(), [])


if __name__ == "__main__":
    unittest.main()
