"""Robot Framework library that runs the firmware in QEMU and talks to its UART.

QEMU is started with -nographic, so the guest's UART0 is wired to QEMU's
stdin/stdout. A background thread collects everything the firmware prints;
keywords wait for patterns in that output and send key presses to the
firmware.

Output is read through a cursor: each successful `Wait For Output` moves the
cursor past the match, so later waits only see newer output.
"""

import re
import subprocess
import threading
import time

from robot.api import logger
from robot.utils import timestr_to_secs


class QemuFirmware:
    ROBOT_LIBRARY_SCOPE = "GLOBAL"

    def __init__(self, qemu="qemu-system-aarch64", firmware="build/firmware.elf"):
        self._qemu = qemu
        self._firmware = firmware
        self._process = None
        self._reader = None
        self._output = ""
        self._cursor = 0
        self._lock = threading.Lock()
        self._changed = threading.Condition(self._lock)

    # ---- lifecycle ----------------------------------------------------------

    def start_firmware(self, firmware=None):
        """Boot the firmware in QEMU. Stops any instance already running."""
        self.stop_firmware()
        image = firmware or self._firmware
        command = [
            self._qemu,
            "-M", "virt",
            "-cpu", "cortex-a53",
            "-m", "128M",
            "-nographic",
            "-semihosting",
            "-kernel", image,
        ]
        logger.info("Starting: " + " ".join(command))
        self._output = ""
        self._cursor = 0
        self._process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        self._reader = threading.Thread(target=self._read_output, daemon=True)
        self._reader.start()

    def stop_firmware(self):
        """Kill QEMU if it is still running and log everything it printed."""
        if self._process is None:
            return
        if self._process.poll() is None:
            self._process.kill()
            self._process.wait(timeout=5)
        self._reader.join(timeout=5)
        logger.info("Firmware output:\n" + self._output)
        self._process = None

    def wait_for_exit(self, timeout="5s"):
        """Wait until QEMU exits and return its exit code."""
        try:
            code = self._process.wait(timeout=timestr_to_secs(timeout))
        except subprocess.TimeoutExpired:
            raise AssertionError(f"QEMU did not exit within {timeout}")
        self._reader.join(timeout=5)
        return code

    # ---- interaction --------------------------------------------------------

    def send_key(self, key):
        """Send one key press (or a short string) to the firmware's UART."""
        self._require_running()
        self._process.stdin.write(key.encode())
        self._process.stdin.flush()

    def wait_for_output(self, pattern, timeout="5s"):
        """Wait until the output after the cursor matches the regex `pattern`.

        Returns the matched text and moves the cursor past it. Fails with the
        unmatched output if the pattern doesn't appear within `timeout`.
        """
        regex = re.compile(pattern)
        deadline = time.monotonic() + timestr_to_secs(timeout)
        with self._changed:
            while True:
                match = regex.search(self._output, self._cursor)
                if match:
                    self._cursor = match.end()
                    return match.group(0)
                remaining = deadline - time.monotonic()
                if remaining <= 0 or self._process_finished():
                    raise AssertionError(
                        f"Pattern {pattern!r} not found within {timeout}. "
                        f"Output since last match:\n{self._output[self._cursor:]}"
                    )
                self._changed.wait(min(remaining, 0.1))

    def collect_output(self, duration):
        """Wait for `duration` and return everything printed after the cursor.

        Moves the cursor to the end of the output.
        """
        time.sleep(timestr_to_secs(duration))
        with self._lock:
            text = self._output[self._cursor:]
            self._cursor = len(self._output)
        return text

    def find_all(self, pattern, text):
        """Return every match of the regex `pattern` in `text`.

        Returns the first group when the pattern has one (as a list of strings),
        otherwise the whole matches.
        """
        return re.findall(pattern, text)

    # ---- internals ----------------------------------------------------------

    def _read_output(self):
        stream = self._process.stdout
        while True:
            chunk = stream.read(1024)
            if not chunk:
                break
            text = chunk.decode(errors="replace").replace("\r", "")
            with self._changed:
                self._output += text
                self._changed.notify_all()
        with self._changed:
            self._changed.notify_all()

    def _process_finished(self):
        return self._process is None or self._process.poll() is not None

    def _require_running(self):
        if self._process_finished():
            raise AssertionError("Firmware is not running")
