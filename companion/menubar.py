#!/usr/bin/env python3
"""Passport Mic menu-bar app.

Runs the tested bridge (passport_mic.py) as a child process, shows its state
in the menu bar, and keeps a log in ~/Library/Logs/PassportMic. Closing the
Terminal window is no longer needed to keep the bridge alive; quit from the menu.
"""
from collections import deque
from datetime import datetime
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading

import objc
from AppKit import (NSApp, NSApplication, NSApplicationActivationPolicyAccessory, NSImage, NSMenu,
                    NSMenuItem, NSStatusBar, NSVariableStatusItemLength, NSWorkspace)
from Foundation import NSURL, NSObject, NSTimer

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bridge_status as bs  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PYTHON = ROOT / ".venv/bin/python"
BRIDGE = ROOT / "companion/passport_mic.py"
LOG_DIR = Path.home() / "Library/Logs/PassportMic"
LOG = LOG_DIR / "bridge.log"
LOG_LIMIT = 2 * 1024 * 1024
RESTART_DELAY = 3.0


class Bridge:
    """The bridge child process; its output lines feed the status."""

    def __init__(self, on_line):
        self.on_line = on_line
        self.proc = None
        self.lock = threading.Lock()

    def start(self, extra_args=()):
        with self.lock:
            if self.proc and self.proc.poll() is None:
                return
            self.proc = subprocess.Popen(
                [str(PYTHON), "-u", str(BRIDGE), "--transport", "ble", *extra_args],
                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, bufsize=1,
                start_new_session=True)
            for stream, is_err in ((self.proc.stdout, False), (self.proc.stderr, True)):
                threading.Thread(target=self._pump, args=(self.proc, stream, is_err), daemon=True).start()

    def _pump(self, proc, stream, is_err):
        for line in stream:
            self.on_line(line, is_err, proc)

    def stop(self):
        """SIGINT first so the bridge releases dictation, then force after a timeout."""
        with self.lock:
            proc, self.proc = self.proc, None
        if not proc or proc.poll() is not None:
            return
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=4)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    def running(self):
        return self.proc is not None and self.proc.poll() is None


class Log:
    def __init__(self):
        LOG_DIR.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()

    def write(self, text):
        with self.lock:
            if LOG.exists() and LOG.stat().st_size > LOG_LIMIT:
                LOG.replace(LOG.with_suffix(".log.1"))
            with LOG.open("a", encoding="utf-8") as f:
                f.write(f"{datetime.now():%Y-%m-%d %H:%M:%S} {text.rstrip()}\n")


class App(NSObject):
    def init(self):
        self = objc.super(App, self).init()
        if self is None:
            return None
        self.log = Log()
        self.status = bs.Status()
        self.recent = deque(maxlen=6)
        self.pending = deque()
        self.pending_lock = threading.Lock()
        self.quitting = False
        self.waiting_for_user = False
        self.restart_at = None
        self.bridge = Bridge(self.bridge_line)
        return self

    # --- setup -------------------------------------------------------------
    def applicationDidFinishLaunching_(self, _note):
        self.item = NSStatusBar.systemStatusBar().statusItemWithLength_(NSVariableStatusItemLength)
        self.menu = NSMenu.alloc().init()
        self.menu.setAutoenablesItems_(False)
        self.state_item = self.add("", None, enabled=False)
        self.detail_item = self.add("", None, enabled=False)
        self.menu.addItem_(NSMenuItem.separatorItem())
        self.recent_header = self.add("最近", None, enabled=False)
        self.recent_items = [self.add("", None, enabled=False) for _ in range(self.recent.maxlen)]
        self.menu.addItem_(NSMenuItem.separatorItem())
        self.reconnect_item = self.add("重新连接", "reconnect:")
        self.add("打开蓝牙设置…", "openBluetooth:")
        self.add("打开日志…", "openLog:")
        self.menu.addItem_(NSMenuItem.separatorItem())
        self.add("退出 Passport Mic", "quit:", key="q")
        self.item.setMenu_(self.menu)
        self.log.write("==== menu bar started ====")
        self.bridge.start()
        self.render()
        # Bridge output arrives on reader threads; apply it on the main thread.
        NSTimer.scheduledTimerWithTimeInterval_target_selector_userInfo_repeats_(
            0.25, self, "tick:", None, True)

    @objc.python_method
    def add(self, title, action, enabled=True, key=""):
        item = NSMenuItem.alloc().initWithTitle_action_keyEquivalent_(title, action, key)
        if action:
            item.setTarget_(self)
        item.setEnabled_(enabled)
        self.menu.addItem_(item)
        return item

    # --- bridge events -----------------------------------------------------
    @objc.python_method
    def bridge_line(self, line, is_err, proc):
        self.log.write(("[stderr] " if is_err else "") + line)
        with self.pending_lock:
            self.pending.append((line, is_err, proc))

    def tick_(self, _timer):
        with self.pending_lock:
            lines, self.pending = list(self.pending), deque()
        changed = False
        for line, is_err, proc in lines:
            if proc is not self.bridge.proc:
                continue  # output from a bridge we already replaced
            before = (self.status.state, self.status.detail)
            self.status = bs.update(self.status, line, is_err)
            if (self.status.state, self.status.detail) != before:
                changed = True
                self.remember(self.status.detail)
        if not self.quitting and not self.bridge.running() and self.restart_at is None and not self.waiting_for_user:
            self.bridgeExited()
            changed = True
        if self.restart_at is not None and datetime.now().timestamp() >= self.restart_at:
            self.restart_at = None
            self.log.write("restarting bridge")
            self.bridge.start()
        if changed:
            self.render()

    @objc.python_method
    def bridgeExited(self):
        message = self.status.detail if self.status.state == bs.ERROR else "桥接程序意外退出"
        if self.status.state != bs.ERROR:
            self.status = bs.Status(bs.ERROR, message)
        self.log.write(f"bridge exited: {message}")
        if bs.needs_user(message):
            # Reconnecting cannot fix this; wait for the user to act, then "重新连接".
            self.waiting_for_user = True
            self.status = bs.Status(bs.ERROR, bs.fix_hint(message))
        else:
            self.status = bs.Status(bs.OFFLINE, f"{message}，{int(RESTART_DELAY)} 秒后重启")
            self.restart_at = datetime.now().timestamp() + RESTART_DELAY
        self.remember(self.status.detail)

    @objc.python_method
    def remember(self, text):
        if text and (not self.recent or self.recent[0][1] != text):
            self.recent.appendleft((datetime.now().strftime("%H:%M"), text))

    # --- drawing -----------------------------------------------------------
    @objc.python_method
    def render(self):
        image = NSImage.imageWithSystemSymbolName_accessibilityDescription_(
            bs.SYMBOLS[self.status.state], self.status.label)
        button = self.item.button()
        if image is not None:
            image.setTemplate_(True)
            button.setImage_(image)
            button.setTitle_("")
        else:
            button.setTitle_("Mic")
        button.setToolTip_(f"Passport Mic：{self.status.label}")
        self.state_item.setTitle_(f"Passport Mic · {self.status.label}")
        self.detail_item.setTitle_(self.status.detail)
        self.recent_header.setHidden_(not self.recent)
        for item, entry in zip(self.recent_items, list(self.recent) + [None] * self.recent.maxlen):
            item.setHidden_(entry is None)
            if entry:
                item.setTitle_(f"{entry[0]}  {entry[1]}")
        self.reconnect_item.setEnabled_(not self.quitting)

    # --- menu actions ------------------------------------------------------
    def reconnect_(self, _sender):
        self.log.write("user: reconnect")
        self.waiting_for_user = False
        self.restart_at = None
        threading.Thread(target=self.restartBridge, daemon=True).start()
        self.status = bs.Status(bs.OFFLINE, "正在重新连接…")
        self.remember(self.status.detail)
        self.render()

    @objc.python_method
    def restartBridge(self):
        self.bridge.stop()
        self.bridge.start()

    def openBluetooth_(self, _sender):
        url = NSURL.URLWithString_("x-apple.systempreferences:com.apple.BluetoothSettings")
        NSWorkspace.sharedWorkspace().openURL_(url)

    def openLog_(self, _sender):
        if not LOG.exists():
            self.log.write("log opened")
        NSWorkspace.sharedWorkspace().openURL_(NSURL.fileURLWithPath_(str(LOG)))

    def quit_(self, _sender):
        self.quitting = True
        self.log.write("user: quit")
        self.bridge.stop()
        NSApp.terminate_(None)

    def applicationWillTerminate_(self, _note):
        self.quitting = True
        self.bridge.stop()


def main():
    if not PYTHON.exists():
        print("请先执行 companion/build-mac.sh 完成安装。", file=sys.stderr)
        return 1
    app = NSApplication.sharedApplication()
    # Menu-bar only: no Dock icon, no main window.
    app.setActivationPolicy_(NSApplicationActivationPolicyAccessory)
    delegate = App.alloc().init()
    app.setDelegate_(delegate)
    # Ctrl-C in the launching Terminal quits cleanly too.
    signal.signal(signal.SIGINT, lambda *_: delegate.quit_(None))
    signal.signal(signal.SIGTERM, lambda *_: delegate.quit_(None))
    os.environ.setdefault("PYTHONUNBUFFERED", "1")
    app.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
