#!/bin/bash
# Starts Passport Mic in the menu bar. The Terminal window can be closed
# afterwards; quit from the menu-bar icon.
cd -- "$(dirname -- "$0")/.." || exit 1
if [[ ! -x .venv/bin/python || ! -x build/mac/chatterfly-key ]]; then
    echo '请先执行 companion/build-mac.sh 完成安装。'
    exit 1
fi
if pgrep -f 'companion/menubar.py' >/dev/null || pgrep -f 'companion/passport_mic.py' >/dev/null; then
    echo 'Passport Mic 已经在运行，请看屏幕右上角的菜单栏图标。'
    exit 0
fi
build/mac/chatterfly-key --request-access || exit 1
nohup .venv/bin/python companion/menubar.py >/dev/null 2>&1 &
disown
echo 'Passport Mic 已在菜单栏启动，可以关闭这个窗口。日志：~/Library/Logs/PassportMic/bridge.log'
