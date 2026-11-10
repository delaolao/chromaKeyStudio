# PR：让 QtAVPlayer 支持「单流」VP8/VP9-Alpha WebM

## 问题

现代 FFmpeg（7.x / 8.x / 9.x 实测）在封装 VP8/VP9 alpha 时**不再写第二路视频流**，
而是写：

- 一路视频流；
- alpha 数据放在该流每个 Block 的 **BlockAdditional** 里；
- 容器（Matroska/WebM）上打 `AlphaMode=1`，demuxer 把它暴露成流元数据 `alpha_mode=1`。

QtAVPlayer 原本只处理"两路视频流"的布局，因此这类文件走单流路径 →
`convertTo(AV_PIX_FMT_BGRA)` → FFmpeg 自带的 `vp9`/`vp8` 解码器不还原旁路 alpha →
画面全不透明。这就是 `desktop_pet/README.md` 里"不要用 WebM(VP9-alpha)"的由来。

实测确认：

```
$ ffprobe -show_streams pet.webm | grep -E "codec_name|pix_fmt|alpha"
codec_name=vp9
pix_fmt=yuv420p            ← 单流；像素格式看起来"没有 alpha"
TAG:alpha_mode=1           ← 但容器声明了 alpha

$ ffmpeg -c:v libvpx-vp9 -i pet.webm -frames:v 1 -pix_fmt rgba -f rawvideo - | ...
→ alpha 通道里确实有 89.9% 的透明像素

$ ffmpeg -c:v vp9 -i pet.webm -frames:v 1 -pix_fmt rgba -f rawvideo - | ...
→ alpha 全 255（原生 vp9 解码器丢掉了旁路数据）
```

## 改动

`src/QtAVPlayer/qavdemuxer.cpp::setup_video_codec()`，新增 31 行：

- 流是 VP9/VP8，且流元数据 `alpha_mode == "1"` 时，自动改用 `libvpx-vp9` / `libvpx`；
- 同时 `ignoreHW = true`（硬件 VP9 解码同样不携带 alpha）；
- 尊重调用方已用 `setInputVideoCodec()` 显式指定的解码器（不覆盖）；
- FFmpeg 构建缺 libvpx 时只打 warning，行为退回原样，不崩。

补丁：`qtavplayer-alpha-webm.patch`

## 文档修正

两个示例 README 里"FFmpeg 把 alpha 写成第二个视频流"的说法已经不成立，一并改掉：

- `examples/desktop_pet/README.md`
  - "不要用 WebM" 段落 → 改为"MOV 最省事，WebM 也能直接播"，并解释两种布局；
  - 方式三注释"（解出来是双流）"→ 去掉；
  - 排错表里"用 `pix_fmt` 判断有没有 yuva" → 改为 WebM 看 `alpha_mode`，
    并注明 WebM 的 `pix_fmt` 永远是 `yuv420p`，拿它判断会误判。
- `examples/pet_player/README.md`
  - 功能总览里"支持双流 Alpha WebM" → "支持两种布局"；
  - 格式表 WebM 行 → 单流/双流都列出；
  - "关于 WebM Alpha" 段落 → 重写为两种布局的说明 + 如何正确判断；
  - 实现要点表 → 增加"单流 Alpha WebM"一行。

## 验证

用打补丁后的 pet_player 播放一段由 FFmpeg 导出的 1080p VP9-Alpha WebM
（`ffmpeg -c:v libvpx-vp9 -pix_fmt yuva420p -auto-alt-ref 0`），
播放窗口的背景完全透出桌面，与 MOV 单流 RGBA 的表现一致。

回归：ProRes 4444 / MOV+PNG / MOV+qtrle 三种单流 MOV 仍然正常透明，
普通 H.264 视频不受影响（补丁只在 VP8/VP9 + `alpha_mode` 时触发）。

终端可以看到这一行：

```
[Stream 0] single-stream alpha WebM detected, using libvpx-vp9
```

## 未覆盖

VP8 分支走的是同一套逻辑（`libvpx` 解码器），但没有找到 VP8-alpha 的样本做实测；
VP9 已实测。若维护者希望保守，可以先把条件收紧到只判 VP9。

## 为什么不改成"产出两路流"

FFmpeg 已经不再那样输出；要修复的是"能不能读现在最常见的输出"，
而不是让所有编码工具去适配一个旧布局。两路流的解码路径原样保留。
