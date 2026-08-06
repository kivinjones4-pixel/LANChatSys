# LANChatSys

一个面向局域网场景的轻量级聊天室，包含 **Node.js/TypeScript TCP 服务端**与 **Qt/C++ 桌面客户端**。用户可以在同一局域网内进行群聊、私聊，并发送图片或其他文件。

## 功能特性

- TCP 长连接通信
- 多用户群聊
- 在线用户列表与上下线状态
- 用户间私聊
- 图片预览与文件传输
- 大文件分块传输与进度显示
- 自动保存接收到的文件
- 服务端管理命令
- Qt 图形界面与 Python 命令行测试客户端
- Docker 部署服务端

## 技术栈

| 模块 | 技术 |
| --- | --- |
| 桌面客户端 | C++17、Qt Widgets、Qt Network |
| 服务端 | Node.js 18+、TypeScript、TCP Socket |
| 测试客户端 | Python 3 标准库 |
| 容器化 | Docker |

## 项目结构

```text
LANChatSys/
├── README.md
├── LICENSE
└── src/
    ├── LANChat-Client/          # Qt/C++ 桌面客户端
    │   ├── LANChat-Client.pro
    │   ├── main.cpp
    │   ├── widget.cpp
    │   ├── widget.h
    │   └── widget.ui
    ├── LANChat-Server/          # Node.js/TypeScript 服务端
    │   ├── src/
    │   ├── package.json
    │   ├── tsconfig.json
    │   └── dockerfile
    └── test_client.py           # Python 命令行测试客户端
```

## 快速开始

### 1. 启动服务端

环境要求：

- Node.js 18 或更高版本
- npm

```bash
cd src/LANChat-Server
npm ci
npm run build
npm start
```

服务端默认监听所有网络接口的 `8888` 端口。看到以下日志即表示启动成功：

```text
🚀 聊天服务器启动，监听端口 8888
```

开发时也可以直接运行 TypeScript 源码：

```bash
npm run dev
```

### 2. 编译桌面客户端

环境要求：

- 支持 C++17 的编译器
- Qt 6（需要 Widgets、Network 和 Core5Compat 模块）
- qmake

使用 Qt Creator 时，打开 `src/LANChat-Client/LANChat-Client.pro`，选择合适的 Kit 后构建并运行。

也可以在命令行中构建：

```bash
cd src/LANChat-Client
qmake LANChat-Client.pro
make -j
./bin/LANChat-Client
```

Windows 可在 Qt 命令行环境中使用 `qmake` 配合 `mingw32-make` 或对应的 MSVC 构建工具。生成的程序默认位于 `src/LANChat-Client/bin/`。

### 3. 连接并聊天

1. 在客户端填写服务器 IP、端口和用户名。
2. 本机测试时，服务器地址填写 `127.0.0.1`，端口填写 `8888`。
3. 局域网内其他设备连接时，填写服务端设备的局域网 IPv4 地址，例如 `192.168.1.10`。
4. 点击连接后即可发送群聊消息。
5. 在用户列表中选择或右键其他用户，可发起私聊。
6. 点击上传按钮可发送图片或文件。

确保服务端防火墙允许 TCP `8888` 端口入站连接，并且客户端与服务端之间网络可达。

## 使用 Python 客户端测试

无需安装第三方 Python 包：

```bash
python3 src/test_client.py <用户名> <服务器IP> <端口>
```

例如：

```bash
python3 src/test_client.py Alice 127.0.0.1 8888
```

输入普通文本即可发送消息，输入 `/quit` 退出。该脚本主要用于验证连接和文本群聊，不包含桌面客户端的私聊及文件传输界面。

## Docker 部署服务端

Dockerfile 运行的是编译后的 `dist/`，因此构建镜像前需要先编译 TypeScript：

```bash
cd src/LANChat-Server
npm ci
npm run build
docker build -f dockerfile -t lanchat-server .
docker run --rm -it -p 8888:8888 lanchat-server
```

## 服务端控制命令

服务端运行期间可在其终端输入：

| 命令 | 说明 |
| --- | --- |
| `/users` | 查看当前在线用户 |
| `/say <消息>` | 向所有客户端发送服务器公告 |
| `/stop` | 通知客户端并安全关闭服务端 |

## 文件传输

- 桌面客户端单次选择文件的上限为 **50 MB**。
- 文件通过 Base64 编码，并可按 **64 KB** 分块发送。
- 接收到的图片通常保存在系统图片目录的 `LANChat` 子目录。
- 视频、音频、文档会按类型保存到系统对应目录的 `LANChat` 子目录。
- 文件名冲突时，客户端会添加时间戳，避免覆盖已有文件。

Base64 会增加传输体积，当前实现更适合局域网内的小型文件传输。

## 通信协议概览

服务端兼容简单文本命令与换行分隔的 JSON 消息。

文本登录与聊天示例：

```text
LOGIN:Alice
CHAT:Alice:Hello
USERS
```

JSON 群聊示例：

```json
{
  "type": "text",
  "sender": "Alice",
  "content": "Hello",
  "timestamp": "10:30:00"
}
```

常见消息类型包括：

| 类型 | 用途 |
| --- | --- |
| `text` | 群聊文本消息 |
| `private` | 私聊文本消息 |
| `login` | 设置用户名 |
| `user_list` | 在线用户列表 |
| `user_status` | 用户状态变更 |
| `file_base64` | Base64 文件 |
| `image_base64` | Base64 图片 |
| `file_chunk` | 文件分块 |
| `error` | 错误提示 |

## 开发命令

在 `src/LANChat-Server` 目录执行：

```bash
npm run build       # 编译 TypeScript 到 dist/
npm start           # 启动已编译的服务端
npm run dev         # 直接运行 TypeScript 源码
npm run dev:watch   # 监听源码变化并自动重启
```

## 常见问题

### 客户端无法连接

- 确认服务端已启动并监听 `8888` 端口。
- 本机连接使用 `127.0.0.1`；其他设备不要填写 `127.0.0.1`，应使用服务端的局域网 IP。
- 检查系统防火墙、路由器的访客网络隔离和虚拟机网络设置。
- 可先使用 `python3 src/test_client.py Alice <服务器IP> 8888` 排查 Qt 客户端之外的问题。

### Qt 提示找不到 `core5compat`

安装 Qt 6 的 Core5Compat 模块，或者在 Qt Maintenance Tool 中为当前 Kit 补充该组件，并确认 `qmake` 来自同一套 Qt 安装。

### Docker 镜像构建时找不到 `dist`

先在 `src/LANChat-Server` 中执行 `npm run build`，再执行 `docker build`。

### 局域网用户无法看到服务端

确认设备处于允许互访的同一网络，并检查服务端 IP 是否发生变化。部分公共 Wi-Fi 或访客 Wi-Fi 会启用客户端隔离，此时设备之间无法直接通信。

## 已知限制与安全说明

- 当前没有 TLS 加密、身份认证和消息持久化。
- 用户名未做唯一性约束，不建议将其作为可靠身份标识。
- 服务端将文件分块暂存在内存中，大文件或大量并发传输可能增加内存占用。
- TCP 数据可能发生粘包或拆包；当前实现对复杂并发消息流的边界处理仍有改进空间。
- 服务端端口固定为 `8888`，尚未提供命令行或环境变量配置。
- 不建议将服务端端口直接暴露到互联网。

## 参与开发

欢迎提交 Issue 或 Pull Request。提交改动前，请至少确认：

1. TypeScript 服务端可以通过 `npm run build` 编译。
2. Qt 客户端可以使用项目 `.pro` 文件构建。
3. 至少两个客户端能够完成连接、群聊和断开连接测试。
4. 涉及协议的改动同步更新 README 中的协议说明。

## License

本项目基于 [MIT License](LICENSE) 开源。
