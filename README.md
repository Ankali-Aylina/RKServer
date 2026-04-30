# RKServer

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/Platform-RK3588-red)](https://www.rock-chips.com/)

**RKServer** 是一个基于 Rockchip NPU（RK3588 系列）的高性能 LLM + Embedding 推理 HTTP 服务。提供 OpenAI 兼容的 Chat Completions API 和 Embeddings API，可无缝替代 OpenAI API，作为 AstrBot、NextChat 等应用的本地推理后端。

---

## 特性

- **OpenAI 兼容 API** — 提供 `/v1/chat/completions` 和 `/v1/embeddings` 接口，可直接替换 OpenAI API
- **LLM 文本生成** — 基于 RKLLM SDK，支持 Qwen2、Qwen2.5、Qwen3 等主流模型
- **Embedding 文本向量化** — 基于 RKNN SDK，支持 MiniLM、BGE 等 Embedding 模型，运行时动态切换
- **Function Calling** — 使用 RKLLM SDK 原生 API 实现工具调用，内置 `get_current_datetime` 工具
- **多模型管理** — 通过 `config.json` 配置多个 LLM 和 Embedding 模型，支持运行时切换
- **高性能** — 利用 Rockchip NPU 进行硬件加速推理，多线程处理并发请求
- **日志系统** — 异步日志写入，支持日志轮转和自动清理
- **systemd 集成** — 提供一键安装脚本，可作为系统服务运行

## API 接口

| 方法   | 路径                           | 描述                        |
| ------ | ------------------------------ | --------------------------- |
| `GET`  | `/health`                      | 健康检查                    |
| `GET`  | `/v1/models`                   | 获取可用模型列表            |
| `POST` | `/v1/chat/completions`         | OpenAI 兼容的聊天补全       |
| `POST` | `/chat`                        | 简化聊天接口                |
| `POST` | `/v1/embeddings`               | OpenAI 兼容的文本嵌入       |
| `POST` | `/v1/models/embedding/switch`  | 切换 Embedding 模型         |
| `GET`  | `/v1/models/embedding/current` | 获取当前 Embedding 模型     |
| `GET`  | `/v1/models/embedding/list`    | 获取可用 Embedding 模型列表 |

### Chat Completions

```bash
curl -X POST http://localhost:8080/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{
    "model": "qwen2-1.5b",
    "messages": [
      {"role": "system", "content": "You are a helpful assistant."},
      {"role": "user", "content": "Hello!"}
    ],
    "temperature": 0.7,
    "max_tokens": 512
  }'
```

### Embeddings

```bash
curl -X POST http://localhost:8080/v1/embeddings \
  -H "Content-Type: application/json" \
  -d '{
    "input": "Hello, world!",
    "model": "bge"
  }'
```

## 快速开始

### 环境要求

- **硬件**: Rockchip RK3588 / RK3588S 开发板（如 Orange Pi 5、Rock 5B 等）
- **系统**: Linux (ARM64) 或 Buildroot
- **依赖**: CMake ≥ 3.11, Boost (system), GCC ≥ 8

### 1. 克隆并初始化子模块

```bash
git clone https://github.com/Ankali-Aylina/RKServer
cd RKServer
git submodule update --init --recursive
```

### 2. 准备模型文件

将 RKLLM 模型文件（`.rkllm`）放入 `model/` 目录，将 RKNN Embedding 模型文件（`.rknn`）放入 `model/` 目录，将分词器词表文件（`vocab.txt`）放入 `tokenizer/` 目录。

编辑 [`config.json`](config.json) 配置模型路径和参数。

### 3. 编译

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### 4. 运行

```bash
# 直接运行
./build/RKServer

# 或安装为 systemd 服务
sudo ./install-systemd-service.sh
```

## 配置

参考 [`config.json`](config.json)：

```json
{
  "models": {
    "llm": {
      "enabled": true,
      "active_model": "qwen2-1.5b",
      "models": {
        "qwen2-1.5b": {
          "model_path": "model/Qwen2-1___5B-Instruct_W8A8_RK3588.rkllm",
          "max_context_length": 4096,
          "max_new_tokens": 512
        }
      }
    },
    "embedding": {
      "enabled": true,
      "active_model": "bge",
      "models": {
        "bge": {
          "model_path": "model/bge-small-zh-v1.5-512.rknn",
          "vocab_path": "tokenizer/bge-small-zh-v1___5/vocab.txt",
          "max_length": 256,
          "embedding_dim": 512
        }
      }
    }
  },
  "server": {
    "host": "0.0.0.0",
    "port": 8080,
    "log_file": "server.log",
    "log_level": "info",
    "performance_mode": "balanced"
  }
}
```

## 项目结构

```
RKServer/
├── CMakeLists.txt              # CMake 构建配置
├── config.json                 # 服务配置文件
├── install-systemd-service.sh  # systemd 服务安装脚本
├── Crow/                       # Crow C++ HTTP 框架（Git 子模块）
├── json/                       # nlohmann/json 库（Git 子模块）
├── include/                    # RKLLM / RKNN SDK 头文件
│   ├── rkllm.h
│   ├── rknn_api.h
│   └── ...
├── lib/                        # RKLLM / RKNN 运行时库
│   ├── librkllmrt.so
│   ├── librknnrt.so
│   └── libsentencepiece.a
├── model/                      # 模型文件目录（需自行放置）
├── tokenizer/                  # 分词器词表目录
└── src/
    ├── main.cpp                # 主程序入口
    ├── core/
    │   ├── http_server.h/cpp   # HTTP 服务器（封装 Crow）
    │   ├── llm_model.h/cpp     # LLM 推理引擎（RKLLM SDK）
    │   ├── embedding_model.h/cpp # Embedding 推理引擎（RKNN SDK）
    │   └── logger.h/cpp        # 异步日志系统
    ├── embedding/
    │   ├── bert_tokenizer.h    # BERT 分词器
    │   └── embedding_worker.h  # RKNN Embedding 工作器
    ├── http/routes/
    │   ├── chat_routes.h       # 聊天 API 路由
    │   ├── embedding_routes.h  # Embedding API 路由
    │   └── model_routes.h      # 模型管理路由
    └── utils/
        └── config_manager.h/cpp # 配置管理器
```

## 架构

```
┌─────────────────────────────────────────────────────────┐
│                     HTTP Server (Crow)                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │ Chat Routes  │  │ Embed Routes │  │ Model Routes │  │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  │
│         │                 │                 │           │
│  ┌──────▼───────┐  ┌──────▼───────┐                     │
│  │  LLMModel    │  │ EmbeddingModel│                     │
│  │ (RKLLM SDK)  │  │  (RKNN SDK)  │                     │
│  └──────────────┘  └──────────────┘                     │
│         │                 │                              │
│  ┌──────▼───────┐  ┌──────▼───────┐                     │
│  │  RKLLM NPU   │  │  RKNN NPU   │                     │
│  └──────────────┘  └──────────────┘                     │
│                                                         │
│  ┌──────────────────────────────────────────────────┐   │
│  │              ConfigManager + Logger               │   │
│  └──────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

## 集成示例

### 与 AstrBot 集成

在 AstrBot WebUI 中配置 Embedding Provider：

| 字段         | 值                             |
| ------------ | ------------------------------ |
| 类型         | `openai_embedding`             |
| API Base URL | `http://<RKServer-ip>:8080/v1` |
| API Key      | 留空或任意值                   |
| Model        | `bge`（或 `minilm`）           |
| Dimensions   | 与模型维度一致（如 `512`）     |

### 与 NextChat / ChatGPT-Next-Web 集成

在自定义 API 端点中填写：

- API 地址：`http://<RKServer-ip>:8080/v1`
- API Key：任意值（rkserver 不验证）

## 许可证

本项目基于 [MIT License](LICENSE) 开源。

## 致谢

- [RKNNLLM](https://github.com/airockchip/rknn-llm) — Rockchip NPU LLM 推理 SDK
- [RKNN Toolkit](https://github.com/airockchip/rknn-toolkit2) — Rockchip NPU 模型转换工具
- [Crow](https://github.com/CrowCpp/Crow) — C++ HTTP 微服务框架
- [nlohmann/json](https://github.com/nlohmann/json) — JSON 解析库
