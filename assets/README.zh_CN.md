<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。


麦克风界面使用 `fonts/mic_subset.otf` 与 `fonts/mic_font_16.c` /
`fonts/mic_font_20.c`：Noto Sans CJK SC Regular 子集，共 176 字符，16/20 px，
4 bpp，无压缩。来源：[Noto CJK](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf)，
许可：[SIL OFL](fonts/OFL.txt)。使用 fonttools 与 PATH 中的 lv_font_conv 1.5.3，
运行 `python tools/generate_mic_font.py <upstream-otf>` 重新生成。
脚本从 `main/mic_ui.c` 提取字符。`main/CMakeLists.txt` 编译字体，中文控件显式选择
对应字体；主机渲染检查字形覆盖与文字边界。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |
| [`images/mic-character.png`](images/mic-character.png) | 1199 × 1312，PNG RGB | 麦克风界面伙伴的原始形象，由项目负责人提供；生成视频时用作首帧。 |
| [`images/mic-character-1x1.png`](images/mic-character-1x1.png) | 1199 × 1199，PNG RGB | 原图顶部对齐的正方形裁切，用于 1:1 图生视频。 |
| [`images/mic_clips.bin`](images/mic_clips.bin) | 240 × 240 RGB565 图块，约 4.9 MB | 伙伴动画包，由 `tools/generate_mic_clips.py` 从负责人生成的 `VIDEO/` 视频（待机、倾听、点头）构建，由 `main/CMakeLists.txt` 嵌入。`images/mic-clips-preview.png` 是预览图。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
