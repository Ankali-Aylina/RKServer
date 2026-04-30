/**
 * @file main.cpp
 * @brief RKLLM HTTP Server (rkserver) 主程序入口
 *
 * 本程序启动一个基于 Rockchip NPU 的 LLM 推理 HTTP 服务。
 * 提供 OpenAI 兼容的 Chat Completions API 和 Embeddings API。
 *
 * 启动流程：
 * 1. 加载配置文件 (config.json)
 * 2. 初始化日志系统
 * 3. 初始化 LLM 模型（文本生成）
 * 4. 注册内置工具（日期时间等）
 * 5. 初始化 Embedding 模型（文本向量化）
 * 6. 启动 HTTP 服务器（阻塞，等待请求）
 * 7. 收到退出信号后，优雅清理所有资源
 */

#include <iostream>
#include "logger.h"
#include "llm_model.h"
#include "embedding_model.h"
#include "http_server.h"
#include "config_manager.h"

int main()
{
    // ==================== 步骤 1: 加载配置文件 ====================
    auto &config = ConfigManager::getInstance();
    if (!config.load("config.json"))
    {
        std::cerr << "Failed to load config.json, using defaults" << std::endl;
    }

    // ==================== 步骤 2: 初始化日志系统 ====================
    auto server_config = config.getServerConfig();
    Logger::getInstance().initialize("config.json");

    LOG_INFO("=== RKLLM HTTP Server (rkserve) Starting ===");
    LOG_INFO("Configuration loaded from: config.json");

    // ==================== 步骤 3: 初始化 LLM 模型（如果启用） ====================
    LLMModel *llm_model = nullptr;
    if (config.isLLMEnabled())
    {
        std::string active_model = config.getActiveLLMModel();
        auto model_config = config.getLLMModelConfig(active_model);

        LOG_INFO("LLM model enabled, loading: " + active_model);
        LOG_INFO("Model path: " + model_config.model_path);
        LOG_INFO("max_new_tokens: " + std::to_string(model_config.max_new_tokens));

        llm_model = &LLMModel::getInstance();
        if (!llm_model->initialize(model_config.model_path, model_config.max_new_tokens))
        {
            LOG_ERROR("Failed to initialize LLM model");
            llm_model = nullptr;
        }
        else
        {
            LOG_INFO("LLM initialized successfully: " + llm_model->getModelName());

            // ==================== 步骤 4: 注册内置工具 ====================
            LOG_INFO("Registering builtin tools...");
            llm_model->registerBuiltinTools();
            LOG_INFO("Builtin tools registered successfully");
        }
    }
    else
    {
        LOG_INFO("LLM model is disabled in configuration");
    }

    // ==================== 步骤 5: 初始化 Embedding 模型（如果启用） ====================
    EmbeddingModel *emb_model = nullptr;
    if (config.isEmbeddingEnabled())
    {
        emb_model = &EmbeddingModel::getInstance();

        if (!emb_model->initializeWithConfig(config.getConfigPath()))
        {
            LOG_ERROR("Failed to initialize Embedding model with config");
            emb_model = nullptr;
        }
        else
        {
            LOG_INFO("Embedding initialized successfully: " + emb_model->getCurrentModel());
            auto models = emb_model->getAvailableModels();
            std::string models_str;
            for (const auto &m : models)
                models_str += m + " ";
            LOG_INFO("Available embedding models: " + models_str);
        }
    }
    else
    {
        LOG_INFO("Embedding model is disabled in configuration");
    }

    // ==================== 步骤 6: 创建并启动 HTTP 服务器 ====================
    LOG_INFO("Starting HTTP server on " + server_config.host + ":" + std::to_string(server_config.port));

    HttpServer server(server_config.port);

    // 设置信号处理（捕获 Ctrl+C 和 SIGTERM 实现优雅退出）
    setupSignalHandlers(&server);

    // 注册所有 API 路由
    server.setupRoutes();

    // 启动服务器（阻塞调用，直到收到退出信号）
    server.start();

    // ==================== 步骤 7: 清理资源 ====================
    LOG_INFO("=== Starting graceful shutdown ===");
    LOG_INFO("Cleaning up resources...");

    if (llm_model)
    {
        LOG_INFO("Cleaning up LLM model...");
        llm_model->cleanup();
        LOG_INFO("LLM model cleaned up");
    }

    if (emb_model)
    {
        LOG_INFO("Cleaning up Embedding model...");
        emb_model->cleanup();
        LOG_INFO("Embedding model cleaned up");
    }

    LOG_INFO("Shutting down logger...");
    Logger::getInstance().shutdown();

    LOG_INFO("=== Application exited cleanly ===");
    return 0;
}
