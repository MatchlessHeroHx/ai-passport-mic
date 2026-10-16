"""Menu-bar status parsing from real bridge output lines; no AppKit needed."""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'companion'))
import bridge_status as bs


def run(lines):
    status = bs.Status()
    for line in lines:
        err = line.startswith('!')
        status = bs.update(status, line.lstrip('!'), err)
    return status


class MenuBarStatusTests(unittest.TestCase):
    def test_connect_record_stop(self):
        s = run(['正在查找 Passport Mic…', '首次连接请在 Mac 输入设备屏幕上的六位配对码。',
                 '蓝牙已连接。保持 Chatterfly 听写关闭，等设备就绪后按确定键。'])
        self.assertEqual(s.state, bs.CONNECTED)
        s = bs.update(s, '正在听写（BLE）…')
        self.assertEqual(s.state, bs.LISTENING)
        s = bs.update(s, '已结束，按确定键继续。')
        self.assertEqual((s.state, s.detail), (bs.CONNECTED, '这段已交给 Mac'))

    def test_lost_frames_are_reported_after_recording(self):
        s = run(['蓝牙已连接。', '正在听写（BLE）…', '蓝牙丢失 3 帧，已补静音。', '蓝牙丢失 12 帧，已补静音。'])
        self.assertEqual((s.state, s.lost_frames), (bs.LISTENING, 15))
        s = bs.update(s, '已结束，按确定键继续。')
        self.assertIn('300 ms', s.detail)
        self.assertEqual(bs.update(s, '正在听写（BLE）…').lost_frames, 0)

    def test_disconnect_reason_and_empty_reason(self):
        s = run(['蓝牙已连接。', '超过 3 秒没收到设备数据，本次听写已停止。正在重连…'])
        self.assertEqual(s.state, bs.OFFLINE)
        self.assertTrue(s.detail.startswith('超过 3 秒没收到设备数据'))
        s = run(['蓝牙已连接。', '，本次听写已停止。正在重连…'])
        self.assertTrue(s.detail.startswith('蓝牙已断开'))

    def test_searching_keeps_out_of_range_hint(self):
        s = run(['等待设备开机或回到蓝牙范围；Ctrl-C 退出。', '正在查找 Passport Mic…'])
        self.assertEqual(s.state, bs.OFFLINE)
        self.assertIn('靠近 Mac', s.detail)

    def test_edit_keys(self):
        self.assertEqual(run(['蓝牙已连接。', '已发送回车。']).detail, '已发送回车')
        self.assertEqual(run(['蓝牙已连接。', '已发送撤销快捷键。']).detail, '已发送撤销')

    def test_pairing_error_needs_user(self):
        line = ('Passport Mic: 蓝牙连接失败：failed to connect: Error Domain=CBErrorDomain Code=14 '
                '"Peer removed pairing information"。检查蓝牙权限和配对信息后重新打开。')
        s = run(['!' + line])
        self.assertEqual(s.state, bs.ERROR)
        self.assertTrue(bs.needs_user(s.detail))
        self.assertIn('忽略 Passport Mic', bs.fix_hint(s.detail))

    def test_transient_error_does_not_need_user(self):
        self.assertFalse(bs.needs_user('桥接程序意外退出'))
        self.assertIn('重新连接', bs.fix_hint('桥接程序意外退出'))

    def test_every_state_has_label_and_icon(self):
        for state in (bs.OFFLINE, bs.CONNECTING, bs.CONNECTED, bs.LISTENING, bs.ERROR):
            self.assertIn(state, bs.LABELS)
            self.assertIn(state, bs.SYMBOLS)


if __name__ == '__main__':
    unittest.main()
