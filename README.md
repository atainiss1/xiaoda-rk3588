# 小哒 · Xiaoda

我叫 Steven.Xia（[@atainiss1](https://github.com/atainiss1)），在 Khadas 担任软件工程师。小哒是我以个人身份开发和维护的项目。

小哒是我做的第一个嵌入式 AI 项目：一台能听、能说、能用手势回应人的 Linux 桌面伙伴。它运行在带触摸屏的 RK3588 开发板上。你可以叫一声「你好小哒」，聊聊今天的天气，也可以让它拉出产品牌，用中文或英文介绍一块开发板。

<p align="center">
  <img src="docs/images/idle.png" width="32%" alt="小哒在触摸屏上待机">
  &nbsp;&nbsp;
  <img src="docs/images/compare.png" width="32%" alt="小哒在产品牌旁比较两款图形扩展设备">
</p>

## 和小哒聊一轮

| 你做什么 | 屏幕上和扬声器里会发生什么 |
| --- | --- |
| 叫「你好小哒」或点击开始 | 进入语音对话。小哒回答时，再叫唤醒词可以打断播放。 |
| 说「用英文介绍 Edge2」 | 小哒调出产品牌，进入讲解姿势，并按当前语言约定回答。也可要求中英双语。 |
| 说「收起来」 | 云端识别意图并请求收牌，板端收起界面，语音会话继续。 |
| 轻触人物或停下来休息 | 触摸触发动作。待机约 15 秒后，小哒闭眼休息。 |

天气依赖已配置的云端实时查询能力。产品介绍使用设备上的产品目录，图片来源和整理记录随目录保存在 `data/`。

## 一句话怎样变成回答

```mermaid
flowchart LR
    M["麦克风 · ES8388"] --> A["PulseAudio 回声消除与降噪"]
    A --> P["16 kHz 单声道 PCM"]
    P --> V["人声检测 / 唤醒词检测"]
    V --> U["Opus 编码"]
    U -->|"WSS"| C["云端识别、对话与语音合成"]
    C -->|"Opus 音频"| D["板端解码与播放"]
    D --> S["扬声器"]
    D -.播放参考.-> A
    C -->|"产品展示工具"| Q["Qt Quick 人物与产品牌"]
```

板端使用 sherpa-onnx 运行 Silero VAD 和关键词模型，完成主语音链的人声检测与唤醒。旧版 VAD 和部分诊断测试使用源自 WebRTC 的 libfvad。音频设备按 48 kHz 工作，语音算法处理 16 kHz 单声道。口型、触摸反馈和产品牌都在本机绘制。

## 从哪里开始

- **第一次编译**：先在 Ubuntu 上跑本机测试，无需连接开发板或登录云端。见[构建与运行](docs/BUILD.md)。
- **改人物和产品牌**：从 `ui/XiaodaCharacter.qml`、`ui/ProductBoard.qml` 和 `data/khadas-products.json` 开始。
- **改语音交互**：`src/speech_engine.hpp` 处理检测，`src/cloud_main.cpp` 负责云端连接，`src/product_mcp.hpp` 实现展示工具。
- **查依赖和素材来源**：见[第三方组件与素材](THIRD_PARTY.md)。

本项目针对 iTOP RK3588、ES8388 和竖屏触摸环境开发。云端语音使用小智协议，需要自己的已绑定设备身份。产品照片、品牌标志及第三方组件的来源单独列出。
