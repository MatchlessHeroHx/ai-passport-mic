"""Turn the bridge's console lines into a menu-bar state.

Pure functions, no AppKit, so tests/test_mic_menubar.py can run anywhere.
"""
from dataclasses import dataclass

OFFLINE, CONNECTING, CONNECTED, LISTENING, ERROR = "offline", "connecting", "connected", "listening", "error"

LABELS = {
    OFFLINE: "未连接",
    CONNECTING: "正在连接…",
    CONNECTED: "蓝牙已连接",
    LISTENING: "正在听",
    ERROR: "出错了",
}

# SF Symbols for the menu-bar icon, drawn as template images so they follow the
# menu bar's light/dark appearance.
SYMBOLS = {
    OFFLINE: "mic.slash",
    CONNECTING: "mic.badge.plus",
    CONNECTED: "mic",
    LISTENING: "mic.fill",
    ERROR: "exclamationmark.triangle",
}

# Errors that reconnecting cannot fix; the menu waits for the user instead.
USER_FIX_MARKERS = ("配对信息", "Peer removed pairing", "权限", "BlackHole", "辅助功能", "多台 Passport Mic")


@dataclass
class Status:
    state: str = OFFLINE
    detail: str = "正在启动…"
    lost_frames: int = 0      # audio frames filled with silence in the current recording

    @property
    def label(self):
        return LABELS[self.state]


def update(status, line, stderr=False):
    """Return the status after one bridge output line."""
    text = line.strip()
    if not text:
        return status
    if stderr or text.startswith("Passport Mic:"):
        message = text[len("Passport Mic:"):].strip() if text.startswith("Passport Mic:") else text
        return Status(ERROR, message, status.lost_frames)
    if "已补静音" in text:
        # Keep the state; count how much audio the radio dropped this recording.
        digits = "".join(ch if ch.isdigit() else " " for ch in text).split()
        lost = status.lost_frames + (int(digits[0]) if digits else 0)
        return Status(status.state, status.detail, lost)
    if "正在听写" in text:
        return Status(LISTENING, "说完按设备上的确定键", 0)
    if "已结束" in text:
        detail = "这段已交给 Mac"
        if status.lost_frames:
            detail += f"（信号不稳，补了 {status.lost_frames * 20} ms 静音）"
        return Status(CONNECTED, detail, status.lost_frames)
    if "已发送回车" in text:
        return Status(CONNECTED, "已发送回车", status.lost_frames)
    if "已发送撤销" in text:
        return Status(CONNECTED, "已发送撤销", status.lost_frames)
    if "蓝牙已连接" in text or text.startswith("已连接"):
        return Status(CONNECTED, "按设备上的确定键开始说话", 0)
    if "首次连接" in text:
        return Status(CONNECTING, "首次配对时，在 Mac 上输入设备屏幕上的六位码", 0)
    if "正在重连" in text:
        reason = text.split("，")[0] if "，" in text else "蓝牙已断开"
        return Status(OFFLINE, f"{reason or '蓝牙已断开'}，正在重连…", 0)
    if "等待设备开机" in text:
        return Status(OFFLINE, "没找到设备：请打开设备，或靠近 Mac", 0)
    if "正在查找" in text:
        detail = status.detail if status.state == OFFLINE else "正在查找 Passport Mic…"
        return Status(OFFLINE, detail, 0)
    return status


def needs_user(message):
    return any(marker in message for marker in USER_FIX_MARKERS)


def fix_hint(message):
    """One short instruction for an error the user must fix."""
    if "配对信息" in message or "Peer removed pairing" in message:
        return "在 Mac 蓝牙设置里忽略 Passport Mic，再点“重新连接”"
    if "BlackHole" in message:
        return "安装 BlackHole 2ch 并重启 Mac"
    if "辅助功能" in message:
        return "在隐私与安全性 → 辅助功能里允许 chatterfly-key"
    if "多台" in message:
        return "只保留一台 Passport Mic 开机"
    if "权限" in message:
        return "在隐私与安全性 → 蓝牙里允许 Passport Mic"
    return "点“重新连接”再试一次"
