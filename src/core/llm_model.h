#ifndef LLM_MODEL_H
#define LLM_MODEL_H

/**
 * @file llm_model.h
 * @brief LLM 模型管理类头文件（异步切换版本）
 * 
 * 支持单模型异步切换，不增加内存峰值。
 * 切换请求立即返回，后台完成加载，期间推理返回 503。
 */

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <atomic>
#include <future>
#include <condition_variable>

#include "rkllm.h"

class LLMModel {
public:
    static LLMModel& getInstance();

    // ---- 模型状态 ----
    enum class ModelState {
        IDLE,       // 未初始化
        READY,      // 模型就绪
        SWITCHING,  // 切换中
        ERROR       // 加载失败
    };
    ModelState getState() const { return m_state.load(); }
    std::string getStateString() const;

    // ---- 生命周期 ----
    bool initialize(const std::string& model_path, int max_new_tokens = 512);
    bool initializeWithConfig();          // 从配置加载默认模型
    void cleanup();

    // ---- 推理接口 ----
    std::string infer(const std::string& prompt);
    std::string inferWithSystemPrompt(const std::string& prompt, const std::string& system_prompt);
    std::string inferWithTools(const std::string& prompt, const std::string& tools_json,
                               const std::string& system_prompt);

    // ---- 工具注册 ----
    bool setFunctionTools(const std::string& system_prompt, const std::string& tools_json,
                          const std::string& tool_response_str = "tool_responses");
    void registerBuiltinTools();          // 公开接口（加锁）
    void registerBuiltinToolsLocked();    // 内部接口（不加锁，需持有锁）

    // ---- 模型切换 ----
    bool switchModel(const std::string& model_name);   // 异步启动切换
    bool waitForSwitchComplete(int timeout_ms = 30000); // 等待切换完成（可选）

    // ---- 信息查询 ----
    std::string getModelName() const { return m_current_model_name; }
    std::string getCurrentModelName() const { return m_current_model_name; }
    std::vector<std::string> getAvailableModels() const;
    bool isInitialized() const { return m_initialized; }

    // 禁止拷贝
    LLMModel(const LLMModel&) = delete;
    LLMModel& operator=(const LLMModel&) = delete;

private:
    LLMModel();
    ~LLMModel();

    // ---- 内部结构 ----
    struct ModelConfig {
        std::string model_path;
        int max_new_tokens = 512;
        int max_context_length = 4096;
    };
    struct CallbackContext {
        std::string* output;
    };

    // ---- 内部方法 ----
    static int rkllm_callback(RKLLMResult* result, void* userdata, LLMCallState state);
    bool loadConfigFromManager();
    bool loadModelInternal(const ModelConfig& config);   // 不加锁，由调用者保证安全
    std::string buildBuiltinToolsJson();

    // ---- 成员变量 ----
    LLMHandle m_handle = nullptr;
    std::mutex m_mutex;                                  // 保护 m_handle、m_model_name、m_current_model_name、m_initialized
    std::map<std::string, ModelConfig> m_model_configs;
    std::string m_current_model_name;
    bool m_initialized = false;
    int m_max_new_tokens = 512;

    // ---- 异步切换状态 ----
    std::atomic<ModelState> m_state{ModelState::IDLE};
    std::future<bool> m_switch_future;
    std::mutex m_switch_mutex;                          // 防止并发切换请求
    std::condition_variable m_switch_cv;                // 用于等待切换完成（可选）
};

#endif // LLM_MODEL_H