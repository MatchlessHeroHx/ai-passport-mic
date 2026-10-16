#include "mic_lines.h"

/* Copy from docs/firmware-copy.optimized.md. Each line must fit the bubble:
   at most 196 px at 16 px, about 12 CJK characters (checked by the host renderer). */
static const char *const waiting[] = {
    "正在静候连接", "等待设备接入", "等待 Mac 连接", "Passport Mic 已打开吗？", "暂未检测到 Mac",
};
static const char *const hello[] = {
    "已连接好", "嗨，又见面了", "已准备就绪", "今天想聊些什么？",
};
static const char *const idle[] = {
    "按确定，随时在听", "有想法，随时开口", "随时都在", "慢慢想，不着急",
    "随时可以说", "今天还顺心吗？", "随时为你记录", "轻按一下，就能说",
};
static const char *const listen[] = {
    "请说，我在听", "我在听", "继续说，都记着", "不急，慢慢说", "细心听着", "听着呢",
};
static const char *const long_talk[] = {
    "别着急，慢慢讲", "都在完整记录", "长语音记录中…", "说完按一下确定",
};
static const char *const done[] = {
    "都记好了", "记下了", "已同步完成", "这段录好了", "收到", "这段听清了",
};
static const char *const send[] = {
    "已发出", "发送完成", "已发送", "已送达",
};
static const char *const undo[] = {
    "已撤回", "没关系，重来一次", "刚才的不算", "已抹除",
};
static const char *const pair[] = {
    "请在 Mac 确认配对", "请在电脑输入配对码", "正在等待配对确认",
};
static const char *const error[] = {
    "未采集到声音信号", "麦克风启动异常", "请关机后重新开机",
};

static const struct {
    const char *const *lines;
    unsigned count;
} table[MIC_MOOD_COUNT] = {
#define LINES(a) {a, sizeof(a) / sizeof(a[0])}
    [MIC_MOOD_WAITING] = LINES(waiting), [MIC_MOOD_HELLO] = LINES(hello),
    [MIC_MOOD_IDLE] = LINES(idle),       [MIC_MOOD_LISTEN] = LINES(listen),
    [MIC_MOOD_LONG] = LINES(long_talk),  [MIC_MOOD_DONE] = LINES(done),
    [MIC_MOOD_SEND] = LINES(send),       [MIC_MOOD_UNDO] = LINES(undo),
    [MIC_MOOD_PAIR] = LINES(pair),       [MIC_MOOD_ERROR] = LINES(error),
#undef LINES
};

static uint8_t last[MIC_MOOD_COUNT] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

unsigned mic_line_count(mic_mood_t mood) { return mood < MIC_MOOD_COUNT ? table[mood].count : 0; }

const char *mic_line_at(mic_mood_t mood, unsigned index) {
    return index < mic_line_count(mood) ? table[mood].lines[index] : "";
}

const char *mic_line(mic_mood_t mood, uint32_t seed) {
    unsigned n = mic_line_count(mood);
    if (!n) return "";
    /* Integer hash so consecutive seeds still pick unrelated lines. */
    seed ^= seed >> 16; seed *= 0x7feb352dU; seed ^= seed >> 15;
    unsigned i = seed % n;
    if (n > 1 && i == last[mood]) i = (i + 1 + seed / n % (n - 1)) % n;
    last[mood] = (uint8_t)i;
    return table[mood].lines[i];
}
