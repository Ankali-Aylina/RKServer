/**
 * @file embedding_model.cpp
 * @brief Embedding（文本嵌入）模型管理类实现
 *
 * 从新项目 (rkllmHttpServer) 完整复制，功能正常。
 *
 * Embedding 模型使用 RKNN（Rockchip Neural Network）SDK 进行推理，
 * 与 LLM 模型使用的 RKLLM SDK 不同。
 *
 * 工作流程：
 * 1. 加载配置 → 2. 加载词表（BertTokenizer）→ 3. 加载 RKNN 模型
 * 4. 接收文本 → 5. 分词 → 6. RKNN 推理 → 7. L2 归一化 → 8. 返回向量
 */

#include "embedding_model.h"
#include "logger.h"
#include "config_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstring>
#include <numeric>
#include <algorithm>

// ============================================================================
// EmbeddingModel 配置相关实现
// ============================================================================

bool EmbeddingModel::loadConfigFromManager()
{
    auto &config_manager = ConfigManager::getInstance();

    if (!config_manager.isLoaded())
    {
        LOG_ERROR("Config manager not loaded, cannot initialize embedding model");
        return false;
    }

    // 获取所有可用的 embedding 模型配置
    auto available_models = config_manager.getAvailableEmbeddingModels();

    for (const auto &model_name : available_models)
    {
        auto model_config = config_manager.getEmbeddingFileConfig(model_name);

        ModelConfig config;
        config.model_path = model_config.model_path;
        config.tokenizer_path = model_config.tokenizer_json;
        config.vocab_path = model_config.vocab_txt;
        config.max_length = model_config.max_length;
        config.embedding_dim = model_config.embedding_dim;
        config.description = model_config.description;

        m_config.models[model_name] = config;
        LOG_INFO("Loaded model config: " + model_name + " -> " + config.model_path +
                 " (max_length=" + std::to_string(config.max_length) +
                 ", embedding_dim=" + std::to_string(config.embedding_dim) + ")");
    }

    // 设置活跃模型
    m_config.active_model = config_manager.getActiveEmbeddingModel();

    return !m_config.models.empty();
}

std::vector<std::string> EmbeddingModel::getAvailableModels() const
{
    std::vector<std::string> models;
    for (const auto &pair : m_config.models)
    {
        models.push_back(pair.first);
    }
    return models;
}

bool EmbeddingModel::loadModelInternal(const ModelConfig &config)
{
    // 1. 加载分词器词表
    LOG_INFO("Loading embedding model: " + config.model_path +
             " (max_length=" + std::to_string(config.max_length) +
             ", embedding_dim=" + std::to_string(config.embedding_dim) + ")");

    if (!m_tokenizer.loadVocab(config.vocab_path))
    {
        LOG_ERROR("Failed to load vocab from: " + config.vocab_path);
        return false;
    }
    LOG_INFO("BertTokenizer loaded successfully from: " + config.vocab_path);

    // 2. 读取 RKNN 模型文件到内存
    std::ifstream ifs(config.model_path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open())
    {
        LOG_ERROR("Failed to open model file: " + config.model_path);
        return false;
    }
    auto size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<char> model_data(size);
    ifs.read(model_data.data(), size);
    ifs.close();

    // 3. 创建 EmbeddingWorker 并初始化 RKNN 上下文
    m_worker = std::make_unique<EmbeddingWorker>();
    embedding::WorkerConfig worker_config;
    worker_config.max_seq_len = config.max_length;
    worker_config.embedding_dim = config.embedding_dim;

    if (!m_worker->initialize(model_data, worker_config))
    {
        LOG_ERROR("Failed to initialize embedding worker");
        m_worker.reset();
        return false;
    }

    m_worker_config = worker_config;
    // 使用模型实际查询到的 max_seq_len（可能比配置值大）
    max_seq_len_ = worker_config.max_seq_len;
    m_initialized = true;
    LOG_INFO("Embedding model initialized: " + config.model_path +
             " (max_seq_len=" + std::to_string(max_seq_len_) +
             ", config_max_length=" + std::to_string(config.max_length) + ")");
    return true;
}

bool EmbeddingModel::switchModel(const std::string &model_name)
{
    auto it = m_config.models.find(model_name);
    if (it == m_config.models.end())
    {
        LOG_ERROR("Model not found: " + model_name);
        std::string available;
        for (const auto &pair : m_config.models)
        {
            available += pair.first + " ";
        }
        LOG_ERROR("Available models: " + available);
        return false;
    }

    // 清理当前模型
    if (m_initialized)
    {
        cleanup();
    }

    // 加载新模型
    LOG_INFO("Switching to model: " + model_name);
    bool success = loadModelInternal(it->second);
    if (success)
    {
        std::lock_guard<std::mutex> lock(m_config_mutex);
        m_current_model_name = model_name;
        LOG_INFO("Successfully switched to model: " + model_name);
    }
    return success;
}

bool EmbeddingModel::initialize(const EmbeddingModelConfig &config)
{
    std::lock_guard<std::mutex> lock(m_config_mutex);

    m_config = config;

    if (m_config.models.empty())
    {
        LOG_ERROR("No models in config");
        return false;
    }

    // 使用 active_model 或第一个可用模型
    std::string model_to_use = m_config.active_model;
    if (m_config.models.find(model_to_use) == m_config.models.end())
    {
        model_to_use = m_config.models.begin()->first;
        LOG_WARNING("Active model '" + m_config.active_model + "' not found, using '" + model_to_use + "'");
    }

    return loadModelInternal(m_config.models[model_to_use]);
}

bool EmbeddingModel::initializeWithConfig(const std::string &config_file)
{
    std::lock_guard<std::mutex> lock(m_config_mutex);

    // 加载配置到 ConfigManager
    auto &config_manager = ConfigManager::getInstance();
    if (!config_manager.load(config_file))
    {
        LOG_ERROR("Failed to load config from: " + config_file);
        return false;
    }

    // 从 ConfigManager 加载配置
    if (!loadConfigFromManager())
    {
        LOG_ERROR("Failed to load config from ConfigManager");
        return false;
    }

    if (m_config.models.empty())
    {
        LOG_ERROR("No models defined in config");
        return false;
    }

    // 使用 active_model
    std::string model_to_use = m_config.active_model;
    if (m_config.models.find(model_to_use) == m_config.models.end())
    {
        model_to_use = m_config.models.begin()->first;
        LOG_WARNING("Active model '" + m_config.active_model + "' not found, using '" + model_to_use + "'");
    }

    m_current_model_name = model_to_use;
    return loadModelInternal(m_config.models[model_to_use]);
}

// ============================================================================
// EmbeddingModel 构造函数/析构函数
// ============================================================================

EmbeddingModel::EmbeddingModel()
    : m_initialized(false)
{
}

// ============================================================================
// EmbeddingModel 单例与核心功能实现
// ============================================================================

EmbeddingModel &EmbeddingModel::getInstance()
{
    static EmbeddingModel instance;
    return instance;
}

std::vector<float> EmbeddingModel::getEmbedding(const std::string &text)
{
    if (!m_initialized || !m_worker)
        return {};

    std::lock_guard<std::mutex> lock(m_inference_mutex);

    // 1. 使用 BertTokenizer 对输入文本进行分词
    std::vector<int> input_ids = m_tokenizer.tokenize(text, max_seq_len_);
    std::vector<int64_t> attention_mask(max_seq_len_, 0);

    // 将 int 转换为 int64_t（RKNN 输入要求）
    std::vector<int64_t> input_ids_int64(input_ids.begin(), input_ids.end());
    for (size_t i = 0; i < input_ids.size(); ++i)
    {
        if (input_ids[i] != 0)
            attention_mask[i] = 1;
    }

    // 2. 通过 EmbeddingWorker 执行 RKNN 推理
    return m_worker->infer(input_ids_int64, attention_mask, m_worker_config);
}

void EmbeddingModel::cleanup()
{
    std::lock_guard<std::mutex> lock(m_inference_mutex);
    if (m_worker)
    {
        m_worker->destroy();
        m_worker.reset();
    }
    m_initialized = false;
    LOG_DEBUG("Embedding model cleanup completed");
}

EmbeddingModel::~EmbeddingModel()
{
    cleanup();
}
