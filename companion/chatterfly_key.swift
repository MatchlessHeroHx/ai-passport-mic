import ApplicationServices
import Foundation

let prompt = CommandLine.arguments.contains("--request-access")
let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: prompt] as CFDictionary
guard AXIsProcessTrustedWithOptions(options) else {
    fputs("请在系统设置 → 隐私与安全性 → 辅助功能中允许 Passport Mic 的 chatterfly-key 程序，然后重新打开。\n", stderr)
    exit(2)
}
if CommandLine.arguments.contains("--check") || prompt { exit(0) }
guard let source = CGEventSource(stateID: .hidSystemState) else { exit(1) }

/// Edit keys go to the session tap: input-method event taps at the HID level
/// (Chatterfly, WeType) swallow synthetic Return and Command-Z posted there.
func tapKey(_ key: CGKeyCode, flags: CGEventFlags = []) {
    guard let down = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: true),
          let up = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: false) else { exit(1) }
    down.flags = flags; up.flags = flags
    down.post(tap: .cgSessionEventTap)
    Thread.sleep(forTimeInterval: 0.04)
    up.post(tap: .cgSessionEventTap)
}

if CommandLine.arguments.contains("--undo") {
    tapKey(6, flags: .maskCommand)
} else if CommandLine.arguments.contains("--confirm") {
    tapKey(36)
} else {
    guard let down = CGEvent(keyboardEventSource: source, virtualKey: 62, keyDown: true),
          let up = CGEvent(keyboardEventSource: source, virtualKey: 62, keyDown: false) else { exit(1) }
    down.type = .flagsChanged
    // NX_DEVICERCTLKEYMASK distinguishes right Control from left Control.
    down.flags = CGEventFlags(rawValue: CGEventFlags.maskControl.rawValue | 0x2000)
    up.type = .flagsChanged
    up.flags = []
    down.post(tap: .cghidEventTap)
    Thread.sleep(forTimeInterval: 0.04)
    up.post(tap: .cghidEventTap)
}
