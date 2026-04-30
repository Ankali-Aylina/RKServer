#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

/**
 * @file config_manager.h
 * @brief 配置管理器
 *
 * 统一管理模型加载配置，支持动态启用/禁用各模块。
 * 从新项目 (rkllmHttpServer) 完整复制，功能正常。
 */

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

/**
 * @brief LLM 模型配置
 */
struct LLMModelConfig
{
    std::string model_path;
    int max_context_length = 4096;
    int max_new_tokens = 512;
    std::string description;
};

/**
 * @brief Embedding 模型配置（用于配置文件）
 */
struct EmbeddingFileConfig
{
    std::string model_path;
    std::string tokenizer_json;
    std::string vocab_txt;
    int max_length = 256;
    int embedding_dim = 384;
    std::string description;
};

/**
 * @brief 服务器配置
 */
struct ServerConfig
{
    std::string host = "0.0.0.0";
    int port = 8080;
    std::string log_file = "server.log";
    std::string log_level = "info";
    int log_max_size = 10;
    int log_max_backups = 5;
    std::string performance_mode = "balanced";
};

/**
 * @brief 配置管理器（单例模式）
 */
class ConfigManager
{
public:
    static ConfigManager &getInstance()
    {
        static ConfigManager instance;
        return instance;
    }

    ConfigManager(const ConfigManager &) = delete;
    ConfigManager &operator=(const ConfigManager &) = delete;

    bool load(const std::string &config_path);

    // LLM 配置访问
    bool isLLMEnabled() const { return m_llm_enabled; }
    std::string getActiveLLMModel() const { return m_llm_active_model; }
    LLMModelConfig getLLMModelConfig(const std::string &model_name) const;
    std::vector<std::string> getAvailableLLMModels() const;

    // Embedding 配置访问
    bool isEmbeddingEnabled() const { return m_embedding_enabled; }
    std::string getActiveEmbeddingModel() const { return m_embedding_active_model; }
    EmbeddingFileConfig getEmbeddingFileConfig(const std::string &model_name) const;
    std::vector<std::string> getAvailableEmbeddingModels() const;

    // 服务器配置访问
    ServerConfig getServerConfig() const { return m_server_config; }
    json getRawJSON() const { return m_config; }
    bool isLoaded() const { return m_loaded; }
    std::string getConfigPath() const { return m_config_path; }

private:
    ConfigManager() = default;
    ~ConfigManager() = default;

    bool m_loaded = false;
    std::string m_config_path;
    json m_config;

    bool m_llm_enabled = true;
    std::string m_llm_active_model;
    std::map<std::string, LLMModelConfig> m_llm_models;

    bool m_embedding_enabled = true;
    std::string m_embedding_active_model;
    std::map<std::string, EmbeddingFileConfig> m_embedding_models;

    ServerConfig m_server_config;

    void parseLLMConfig();
    void parseEmbeddingConfig();
    void parseServerConfig();
};

#endif // CONFIG_MANAGER_H
