# 构建与运行

## 从 Windows 连接到实机

1. 在 Windows 上用 VMware Workstation 启动 Ubuntu 20.04.6 x86-64 虚拟机，在虚拟机的 VS Code 中打开仓库。开发板运行 Ubuntu 22.04.4 ARM64，接 7 英寸 MIPI 屏并用以太网接入路由器。
2. 在开发板终端运行 `hostname -I` 查看局域网地址。确保虚拟机能访问该地址。在**虚拟机终端**设置下面两个变量；输入板上实际的普通用户和 IP。

   ```bash
   read -rp '板端用户名: ' BOARD_USER
   read -rp '开发板 IP: ' BOARD_IP
   BOARD="${BOARD_USER}@${BOARD_IP}"
   ssh "$BOARD" 'uname -m'
   ```

   成功登录时开发板输出 `aarch64`。后续在虚拟机运行第 1–3 节的测试和交叉构建。
3. 首次部署先按第 4 节准备模型、运行库和自己的设备身份。在**虚拟机的仓库根目录**，下面的示例把程序和界面文件传到板端用户目录：

   ```bash
   ssh "$BOARD" 'mkdir -p ~/xiaoda-rk3588/bin'
   scp build/rk3588/rkvoice_cloud build/deploy/rkvoice_ui "$BOARD:xiaoda-rk3588/bin/"
   scp -r ui data scripts "$BOARD:xiaoda-rk3588/"
   ```

4. 在开发板已登录的图形桌面里，从 `~/xiaoda-rk3588` 运行 `bash scripts/run-ui.sh --preview` 检查显示与触摸。准备好语音依赖后，按第 4 节启动真实会话并测试唤醒、收音与播放。后续只改 QML 或图片时，用 SCP 同步对应文件，再重新启动界面即可。

**串口与烧录口各有用途。** 网络尚未启动时，Windows 可通过 USB 转串口连接板上 UART 调试口，在 MobaXterm 查看启动日志。需要写入或恢复整个系统镜像时，Windows 使用 RKDevTool 和 USB 数据线连接板上烧录口；镜像与 Loader 操作以迅为对应板型和屏幕的手册为准。


## 1. 在 Ubuntu 上跑本机测试

从仓库根目录执行。需要 CMake 3.16 或以上、支持 C++17 的编译器、Python 3、OpenSSL、PulseAudio 和 Opus 的开发文件。

```bash
sudo apt install build-essential cmake pkg-config python3 libssl-dev libpulse-dev libopus-dev
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel 4
(cd build/host && ctest --output-on-failure)
```

9 项测试覆盖 PCM 边界、Opus/协议、会话状态、产品工具、人声端点、流式 PCM、云端退出处理和音频文件。测试使用合成输入及上游测试样本。已有 Opus 开发头文件的机器也可以传 `-DOPUS_INCLUDE_DIR=/path/to/opus/include/opus` 指向包含 `opus.h` 的目录。

## 2. 交叉编译 ARM64 语音程序

参考环境为 x86-64 Ubuntu 虚拟机、Arm GNU 10.3 工具链、Ubuntu ARM64 目标系统。将 `ARM64_TOOLCHAIN_ROOT` 指向带有 `bin/aarch64-none-linux-gnu-g++` 的工具链。准备脚本固定了原板匹配的软件包版本和摘要，使用其他镜像时需一并核实 ABI 与版本。

```bash
bash scripts/prepare-pulse-sdk.sh
bash scripts/prepare-cloud-sdk.sh
# 先按 manifest.json 下载、校验并解压 sherpa-onnx ARM64 shared CPU SDK
cmake -S . -B build/rk3588 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake \
  -DARM64_TOOLCHAIN_ROOT=/path/to/arm-toolchain \
  -DRKVOICE_SPEECH_SDK=/path/to/unpacked/sherpa-onnx-sdk \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/rk3588 --parallel 4
```

`manifest.json` 列出了 sherpa-onnx、中文 KWS 模型包和 Silero VAD 的上游 URL 与 SHA-256。将下载文件与对应摘要核对，例如 `sha256sum filename`。完整对话服务需要启用 `RKVOICE_SPEECH_SDK`，本机协议测试可以单独运行。

## 3. 构建 Qt 触摸界面

```bash
bash scripts/prepare-qt-sdk.sh
# 在下述位置补齐与目标板匹配的 Qt 动态链接库与图形开发头文件，然后执行
CXX_ARM64=/path/to/arm-toolchain/bin/aarch64-none-linux-gnu-g++ bash scripts/build-ui.sh
```

脚本将 Qt 5 头文件放到 `build/qt-sdk/usr/include`。QtCore、Gui、Qml、Quick、Network 的 `.so.5` 放在 `build/qt-sdk/board-libs/`；按目标镜像配套提供 `GL/` 等开发头文件。输出的界面程序在 `build/deploy/`。

## 4. 在开发板上运行

在板端工作目录按下列布局放文件。运行时依赖还包括 Qt Quick/QML 模块、Mali EGL、PulseAudio、ALSA 工具，以及与镜像匹配的 OpenSSL、Opus 动态库。

```text
bin/rkvoice_cloud                    从 build/rk3588/ 复制
bin/rkvoice_ui                       从 build/deploy/ 复制
models/keywords.txt                  仓库内的小哒唤醒词配置
models/silero_vad.onnx               来自 manifest.json
models/kws/                          KWS 包内的 tokens.txt、int8 encoder、float decoder、int8 joiner
lib/speech/                          sherpa-onnx SDK 的运行库，复制时保留 .so 符号链接
ui/  data/  scripts/                 复制仓库对应目录
```

本机唤醒模型的精确文件名见 `src/speech_engine.hpp`。先用 `ldd -r bin/rkvoice_cloud` 和 `ldd -r bin/rkvoice_ui` 检查库是否齐全，必要时为前者设置 `LD_LIBRARY_PATH="$PWD/lib/speech"`。

登录开发板图形桌面的普通用户可以执行 `bash scripts/run-ui.sh --preview` 查看界面。要开始真实对话，设置 `RKVOICE_IDENTITY_DIR` 指向**自己的已绑定设备**配置目录（内含 `config.json` 和 `efuse.json`），再执行 `bash scripts/run-ui.sh`。服务会通过 HTTPS 获取短期连接参数。设备凭据和运行日志留在设备上。

`scripts/run-board.sh` 和音频设置脚本采用这块 ES8388 板的设备名、混音器和端口。换板时先核实主麦克风、扬声器和 PulseAudio 路由；在 x86 主机上只执行第一节测试。天气和产品工具还依赖云端智能体配置。
