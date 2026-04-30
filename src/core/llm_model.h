#ifndef LLM_MODEL_H
#define LLM_MODEL_H

/**
 * @file llm_model.h
 * @brief LLM 模型管理类头文件
 *
 * 基于老项目 (rkllmHttpServerold) 的简单实现，添加工具调用支持。
 * 使用 RKLLM SDK 原生 API (rkllm_set_function_tools) 实现 Function Calling。
 *
 * 主要功能：
 * 1. 加载和管理 RKLLM 模型
 * 2. 执行文本推理（同步模式）
 * 3. 支持工具调用（Function Calling）
 * 4. 内置工具：get_current_datetime（获取当前日期时间）
 *
 * @note 使用单例模式，线程安全
 */

#include <string>
#include <mutex>
#include <functional>
#include <vector>
#include <map>
#include "rkllm.h"

/**
 * @class LLMModel
 * @brief LLM 模型管理类（单例模式）
 *
 * 封装 RKLLM SDK 的初始化、推理、资源清理等操作。
 * 基于老项目的简单回调机制，避免新项目中复杂的 atomic flag 切换逻辑。
 */
class LLMModel {
public:
    /**
     * @brief 获取单例实例
     * @return LLMModel 的引用
     */
    static LLMModel& getInstance();

    /**
     * @brief 初始化模型
     * @param model_path RKLLM 模型文件路径
     * @param max_new_tokens 最大输出 token 数
     * @return 成功返回 true
     */
    bool initialize(const std::string& model_path, int max_new_tokens = 512);

    /**
     * @brief 执行推理
     * @param prompt 输入提示词
     * @return 生成的文本
     */
    std::string infer(const std::string& prompt);

    /**
     * @brief 执行推理（带系统提示词）
     * @param prompt 用户输入
     * @param system_prompt 系统提示词
     * @return 生成的文本
     */
    std::string inferWithSystemPrompt(const std::string& prompt, const std::string& system_prompt);

    /**
     * @brief 执行推理（带工具调用）
     * @param prompt 用户输入
     * @param tools_json 工具定义的 JSON 字符串
     * @param system_prompt 系统提示词
     * @return 生成的文本（可能包含工具调用）
     */
    std::string inferWithTools(const std::string& prompt, const std::string& tools_json, const std::string& system_prompt);

    /**
     * @brief 设置函数工具（调用 RKLLM SDK 原生 API）
     * @param system_prompt 系统提示词
     * @param tools_json 工具定义的 JSON 字符串
     * @param tool_response_str 工具响应标记字符串
     * @return 成功返回 true
     */
    bool setFunctionTools(const std::string& system_prompt, const std::string& tools_json, const std::string& tool_response_str = "tool_responses");

    /**
     * @brief 注册内置工具
     *
     * 注册 get_current_datetime 等内置工具到 RKLLM SDK。
     */
    void registerBuiltinTools();

    /**
     * @brief 获取模型名称
     * @return 模型名称字符串
     */
    std::string getModelName() const { return m_model_name; }

    /**
     * @brief 清理资源
     */
    void cleanup();

    /**
     * @brief 检查是否已初始化
     * @return true=已初始化
     */
    bool isInitialized() const { return m_initialized; }

    // 禁止拷贝和赋值
    LLMModel(const LLMModel&) = delete;
    LLMModel& operator=(const LLMModel&) = delete;

private:
    LLMModel();
    ~LLMModel();

    /**
     * @brief RKLLM 回调函数
     * @param result 推理结果
     * @param userdata 用户数据指针
     * @param state 调用状态
     * @return 0=继续推理，1=暂停推理
     */
    static int rkllm_callback(RKLLMResult* result, void* userdata, LLMCallState state);

    /**
     * @brief 回调上下文
     */
    struct CallbackContext {
        std::string* output;   ///< 输出文本指针
    };

    /**
     * @brief 构建内置工具的 JSON 定义
     * @return 工具定义的 JSON 字符串
     */
    std::string buildBuiltinToolsJson();

    LLMHandle m_handle = nullptr;       ///< RKLLM 模型句柄
    std::mutex m_mutex;                 ///< 互斥锁
    bool m_initialized = false;         ///< 初始化标志
    std::string m_model_name;           ///< 模型名称
    int m_max_new_tokens = 512;         ///< 最大输出 token 数
};

#endif // LLM_MODEL_H
