#ifndef EMBEDDING_MODEL_H
#define EMBEDDING_MODEL_H

/**
 * @file embedding_model.h
 * @brief Embedding（文本嵌入）模型管理类头文件
 *
 * 从新项目 (rkllmHttpServer) 完整复制，功能正常。
 * Embedding 是将文本转换为固定长度的向量表示，用于语义搜索等任务。
 *
 * @note 支持多模型配置和运行时切换
 */

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <unordered_map>
#include "rknn_api.h"
#include "embedding/bert_tokenizer.h"
#include "embedding/embedding_worker.h"

// 使用 embedding 命名空间中的类型
using embedding::BertTokenizer;
using embedding::EmbeddingWorker;

// ============================================================================
// 配置数据结构
// ============================================================================

/**
 * @struct ModelConfig
 * @brief 单个 Embedding 模型的配置信息
 */
struct ModelConfig {
    std::string model_path;         ///< RKNN 模型文件路径
    std::string tokenizer_path;     ///< 分词器配置路径（可选）
    std::string vocab_path;         ///< 词表文件路径
    int max_length;                 ///< 最大序列长度（token 数）
    int embedding_dim;              ///< 嵌入向量维度（如 384、768）
    std::string description;        ///< 模型描述信息
};

/**
 * @struct EmbeddingModelConfig
 * @brief Embedding 模型的完整配置
 */
struct EmbeddingModelConfig {
    std::string active_model;                          ///< 当前激活的模型名称
    std::unordered_map<std::string, ModelConfig> models; ///< 所有可用模型配置
};

// ============================================================================
// EmbeddingModel 模型管理类
// ============================================================================

/**
 * @class EmbeddingModel
 * @brief Embedding 模型管理类（单例模式）
 *
 * 主要功能：
 * 1. 加载和管理 Embedding 模型
 * 2. 将文本转换为向量（getEmbedding）
 * 3. 支持多模型配置和动态切换
 * 4. 资源清理
 *
 * @note 线程安全：使用互斥锁保护共享资源
 */
class EmbeddingModel {
public:
    /**
     * @brief 获取单例实例
     * @return EmbeddingModel 的引用
     */
    static EmbeddingModel& getInstance();

    /**
     * @brief 从配置对象初始化模型
     * @param config 模型配置对象
     * @return 成功返回 true
     */
    bool initialize(const EmbeddingModelConfig& config);

    /**
     * @brief 从配置文件初始化模型
     * @param config_file 配置文件路径（JSON 格式）
     * @return 成功返回 true
     */
    bool initializeWithConfig(const std::string& config_file);

    /**
     * @brief 切换到指定的模型
     * @param model_name 模型名称（在配置中定义）
     * @return 成功返回 true
     */
    bool switchModel(const std::string& model_name);

    /**
     * @brief 获取当前模型名称
     * @return 模型名称字符串
     */
    std::string getCurrentModel() const { return m_current_model_name; }

    /**
     * @brief 获取所有可用模型列表
     * @return 模型名称列表
     */
    std::vector<std::string> getAvailableModels() const;

    /**
     * @brief 获取文本的嵌入向量
     * @param text 输入文本
     * @return 归一化后的浮点向量（维度=embedding_dim）
     */
    std::vector<float> getEmbedding(const std::string& text);

    /**
     * @brief 清理模型资源
     */
    void cleanup();

    /**
     * @brief 检查模型是否已初始化
     * @return true=已初始化
     */
    bool isInitialized() const { return m_initialized; }

    // 禁止拷贝和赋值
    EmbeddingModel(const EmbeddingModel&) = delete;
    EmbeddingModel& operator=(const EmbeddingModel&) = delete;

private:
    EmbeddingModel();
    ~EmbeddingModel();

    /**
     * @brief 从 ConfigManager 加载配置
     */
    bool loadConfigFromManager();

    /**
     * @brief 加载单个模型
     */
    bool loadModelInternal(const ModelConfig& config);

    std::unique_ptr<EmbeddingWorker> m_worker;   ///< RKNN 工作器
    embedding::WorkerConfig m_worker_config;      ///< 工作器配置
    BertTokenizer m_tokenizer;                    ///< BERT 分词器
    std::mutex m_inference_mutex;                 ///< 推理互斥锁
    std::mutex m_config_mutex;                    ///< 配置互斥锁
    bool m_initialized;                           ///< 初始化标志
    int max_seq_len_ = 256;                       ///< 最大序列长度

    EmbeddingModelConfig m_config;                ///< 多模型配置
    std::string m_current_model_name;             ///< 当前模型名称
};

#endif // EMBEDDING_MODEL_H
