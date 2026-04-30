/**
 * @file config_manager.cpp
 * @brief 配置管理器实现
 *
 * 从新项目 (rkllmHttpServer) 完整复制，功能正常。
 * 负责加载和解析 config.json 配置文件。
 */

#include "config_manager.h"
#include "../core/logger.h"
#include <fstream>
#include <iostream>

bool ConfigManager::load(const std::string &config_path)
{
    LOG_INFO("Loading configuration from: " + config_path);

    std::ifstream file(config_path);
    if (!file.is_open())
    {
        LOG_ERROR("Config file not found: " + config_path);
        return false;
    }

    try
    {
        file >> m_config;
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to parse config JSON: " + std::string(e.what()));
        return false;
    }
    file.close();

    m_config_path = config_path;

    parseLLMConfig();
    parseEmbeddingConfig();
    parseServerConfig();

    m_loaded = true;
    LOG_INFO("Configuration loaded successfully");

    LOG_INFO("=== Configuration Summary ===");
    LOG_INFO("LLM Model: " + std::string(m_llm_enabled ? "enabled" : "disabled") +
             ", active: " + m_llm_active_model);
    LOG_INFO("Embedding: " + std::string(m_embedding_enabled ? "enabled" : "disabled") +
             ", active: " + m_embedding_active_model);
    LOG_INFO("Server: " + m_server_config.host + ":" + std::to_string(m_server_config.port));
    LOG_INFO("Performance mode: " + m_server_config.performance_mode);

    return true;
}

void ConfigManager::parseLLMConfig()
{
    if (!m_config.contains("models") ||
        !m_config["models"].contains("llm"))
    {
        LOG_WARNING("LLM model configuration not found, using defaults");
        return;
    }

    const auto &llm_config = m_config["models"]["llm"];

    m_llm_enabled = llm_config.value("enabled", true);
    m_llm_active_model = llm_config.value("active_model", "qwen2-7b");

    if (llm_config.contains("models") && llm_config["models"].is_object())
    {
        for (auto &[name, config] : llm_config["models"].items())
        {
            LLMModelConfig model_config;
            model_config.model_path = config.value("model_path", "");
            model_config.max_context_length = config.value("max_context_length", 4096);
            model_config.max_new_tokens = config.value("max_new_tokens", 512);
            model_config.description = config.value("description", "");

            m_llm_models[name] = model_config;
            LOG_DEBUG("Loaded LLM model: " + name + " -> " + model_config.model_path +
                      ", max_new_tokens: " + std::to_string(model_config.max_new_tokens));
        }
    }

    if (m_llm_models.find(m_llm_active_model) == m_llm_models.end())
    {
        LOG_WARNING("Active LLM model '" + m_llm_active_model + "' not found");
        if (!m_llm_models.empty())
        {
            m_llm_active_model = m_llm_models.begin()->first;
            LOG_INFO("Using first available model: " + m_llm_active_model);
        }
    }
}

void ConfigManager::parseEmbeddingConfig()
{
    if (!m_config.contains("models") ||
        !m_config["models"].contains("embedding"))
    {
        LOG_WARNING("Embedding model configuration not found, using defaults");
        return;
    }

    const auto &emb_config = m_config["models"]["embedding"];

    m_embedding_enabled = emb_config.value("enabled", true);
    m_embedding_active_model = emb_config.value("active_model", "bge");

    if (emb_config.contains("models") && emb_config["models"].is_object())
    {
        for (auto &[name, config] : emb_config["models"].items())
        {
            EmbeddingFileConfig model_config;
            model_config.model_path = config.value("model_path", "");
            model_config.tokenizer_json = config.value("tokenizer_json", "");
            model_config.vocab_txt = config.value("vocab_path", "");
            model_config.max_length = config.value("max_length", 256);
            model_config.embedding_dim = config.value("embedding_dim", 384);
            model_config.description = config.value("description", "");

            m_embedding_models[name] = model_config;
            LOG_DEBUG("Loaded Embedding model: " + name + " -> " + model_config.model_path);
        }
    }

    if (m_embedding_models.find(m_embedding_active_model) == m_embedding_models.end())
    {
        LOG_WARNING("Active Embedding model '" + m_embedding_active_model + "' not found");
        if (!m_embedding_models.empty())
        {
            m_embedding_active_model = m_embedding_models.begin()->first;
            LOG_INFO("Using first available model: " + m_embedding_active_model);
        }
    }
}

void ConfigManager::parseServerConfig()
{
    if (!m_config.contains("server"))
    {
        LOG_DEBUG("Server configuration not found, using defaults");
        return;
    }

    const auto &server_config = m_config["server"];

    m_server_config.host = server_config.value("host", "0.0.0.0");
    m_server_config.port = server_config.value("port", 8080);
    m_server_config.log_file = server_config.value("log_file", "server.log");
    m_server_config.log_level = server_config.value("log_level", "info");
    m_server_config.log_max_size = server_config.value("log_max_size", 10);
    m_server_config.log_max_backups = server_config.value("log_max_backups", 5);
    m_server_config.performance_mode = server_config.value("performance_mode", "balanced");
}

LLMModelConfig ConfigManager::getLLMModelConfig(const std::string &model_name) const
{
    auto it = m_llm_models.find(model_name);
    if (it != m_llm_models.end())
    {
        return it->second;
    }

    LOG_WARNING("LLM model '" + model_name + "' not found, using default config");
    LLMModelConfig default_config;
    default_config.model_path = "model/Qwen2___5-7B-Instruct_W8A8_RK3588.rkllm";
    default_config.max_context_length = 4096;
    default_config.max_new_tokens = 512;
    return default_config;
}

std::vector<std::string> ConfigManager::getAvailableLLMModels() const
{
    std::vector<std::string> models;
    for (const auto &[name, config] : m_llm_models)
    {
        models.push_back(name);
    }
    return models;
}

EmbeddingFileConfig ConfigManager::getEmbeddingFileConfig(const std::string &model_name) const
{
    auto it = m_embedding_models.find(model_name);
    if (it != m_embedding_models.end())
    {
        return it->second;
    }

    LOG_WARNING("Embedding model '" + model_name + "' not found, using default config");
    EmbeddingFileConfig default_config;
    default_config.model_path = "model/all-MiniLM-L6-v2-no-norm-fp16.rknn";
    default_config.vocab_txt = "tokenizer/vocab.txt";
    default_config.max_length = 256;
    default_config.embedding_dim = 384;
    return default_config;
}

std::vector<std::string> ConfigManager::getAvailableEmbeddingModels() const
{
    std::vector<std::string> models;
    for (const auto &[name, config] : m_embedding_models)
    {
        models.push_back(name);
    }
    return models;
}
