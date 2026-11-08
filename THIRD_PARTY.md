# 第三方组件与素材

| 组成 | 工程中的用途 | 来源与许可记录 |
| --- | --- | --- |
| IXWebSocket | WebSocket 与 TLS | [上游](https://github.com/machinezone/IXWebSocket/tree/bf7ad53c02612643e1cf72e37c8749491798d33d) · [随附许可](third_party/ixwebsocket/LICENSE.txt) |
| nlohmann/json 3.11.3 | 产品资料与协议 JSON | [上游](https://github.com/nlohmann/json/tree/v3.11.3) · [MIT](third_party/nlohmann/LICENSE.MIT) |
| libfvad | WebRTC VAD 的独立实现，用于旧路径和测试 | [上游](https://github.com/dpirch/libfvad/tree/532ab666c20d3cfda38bca63abbb0f152706c369) · [许可证](third_party/libfvad/LICENSE) · [专利声明](third_party/libfvad/PATENTS) |
| sherpa-onnx / ONNX Runtime | 本机模型推理 | [sherpa-onnx 发行页](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v1.13.8) · [上游许可文本](third_party/speech-licenses/) |
| Silero VAD / Zipformer KWS | 语音端点、中文唤醒词 | [Silero](https://github.com/snakers4/silero-vad) · [KWS 模型发布](https://github.com/k2-fsa/sherpa-onnx/releases/tag/kws-models) · [下载清单与摘要](manifest.json) |
| PulseAudio / WebRTC Audio Processing | 目标系统上的回声消除和降噪 | [PulseAudio](https://www.freedesktop.org/wiki/Software/PulseAudio/) · [WebRTC](https://webrtc.googlesource.com/src/+/refs/heads/main/modules/audio_processing/) |
| Opus / Qt | 实时音频编码、触摸屏界面 | [Opus](https://opus-codec.org/) · [Qt](https://www.qt.io/) |

云端通信参考[小智 ESP32 项目的 WebSocket 协议](https://github.com/78/xiaozhi-esp32/blob/main/docs/websocket.md)。本项目的 Linux 客户端以 C++ 实现。

角色贴图由项目根据角色参考图制作，界面截图来自项目演示。Khadas 产品照片与品牌图形的来源见[产品图记录](data/asset-sources.json)与[品牌来源记录](data/brand-logo-source.json)。第三方商标、照片、模型和组件各有其权利归属，来源记录本身不授予再分发许可。
