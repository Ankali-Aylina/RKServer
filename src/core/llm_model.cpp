/**
 * @file llm_model.cpp
 * @brief LLM 模型管理类实现
 *
 * 基于老项目 (rkllmHttpServerold) 的简单实现：
 * - 使用简单的回调函数收集推理结果
 * - 使用互斥锁保护并发访问
 * - 使用 RKLLM SDK 原生 API 实现工具调用
 *
 * 工具调用流程：
 * 1. 调用 rkllm_set_function_tools() 注册工具定义
 * 2. 执行推理，RKLLM SDK 自动处理工具调用逻辑
 * 3. 模型输出可能包含工具调用 JSON
 *
 * @author RKLLM HTTP Server Team
 */

#include "llm_model.h"
#include "logger.h"
#include <iostream>
#include <memory>
#include <sstream>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// 获取单例实例
LLMModel& LLMModel::getInstance() {
    static LLMModel instance;
    return instance;
}

// 构造函数
LLMModel::LLMModel()
    : m_handle(nullptr), m_initialized(false), m_max_new_tokens(512) {
}

// 析构函数
LLMModel::~LLMModel() {
    cleanup();
}

// 初始化模型
bool LLMModel::initialize(const std::string& model_path, int max_new_tokens) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_initialized) {
        LOG_INFO("Model already initialized");
        return true;
    }

    m_max_new_tokens = max_new_tokens;

    RKLLMParam param = rkllm_createDefaultParam();
    param.model_path = model_path.c_str();
    param.max_context_len = 4096;
    param.max_new_tokens = max_new_tokens;
    param.top_k = 40;
    param.top_p = 0.95;
    param.temperature = 0.7;
    param.repeat_penalty = 1.1;
    param.is_async = false; // 使用同步模式

    int ret = rkllm_init(&m_handle, &param, rkllm_callback);
    if (ret != 0) {
        LOG_ERROR("rkllm_init failed with code: " + std::to_string(ret));
        return false;
    }

    m_initialized = true;

    // 从模型路径提取模型名称
    size_t last_slash = model_path.find_last_of("/\\");
    size_t last_dot = model_path.find_last_of('.');
    if (last_slash != std::string::npos && last_dot != std::string::npos && last_dot > last_slash) {
        m_model_name = model_path.substr(last_slash + 1, last_dot - last_slash - 1);
    } else {
        m_model_name = "rkllm-model";
    }

    LOG_INFO("RKLLM initialized successfully from: " + model_path);
    LOG_INFO("Model name: " + m_model_name + ", max_new_tokens: " + std::to_string(max_new_tokens));
    return true;
}

// 执行推理
std::string LLMModel::infer(const std::string& prompt) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized || !m_handle) {
        LOG_ERROR("Model not initialized");
        return "[Error] Model not initialized.";
    }

    std::string generated_text;
    CallbackContext ctx{&generated_text};

    // 构造输入
    RKLLMInput input;
    input.role = "user";
    input.enable_thinking = false;
    input.input_type = RKLLM_INPUT_PROMPT;
    input.prompt_input = prompt.c_str();

    // 推理参数
    RKLLMInferParam infer_params;
    infer_params.mode = RKLLM_INFER_GENERATE;
    infer_params.lora_params = nullptr;
    infer_params.prompt_cache_params = nullptr;
    infer_params.keep_history = 0; // 不保留历史

    // 执行推理
    LOG_DEBUG("Starting inference...");
    int ret = rkllm_run(m_handle, &input, &infer_params, &ctx);
    if (ret != 0) {
        LOG_ERROR("rkllm_run failed, code: " + std::to_string(ret));
        return "[Error] Inference failed.";
    }

    LOG_DEBUG("Inference completed, output length: " + std::to_string(generated_text.length()));
    return generated_text;
}

// 执行推理（带系统提示词）
std::string LLMModel::inferWithSystemPrompt(const std::string& prompt, const std::string& system_prompt) {
    // 将系统提示词和用户提示词拼接
    std::string full_prompt = system_prompt + "\n\n" + prompt;
    return infer(full_prompt);
}

// 执行推理（带工具调用）
std::string LLMModel::inferWithTools(const std::string& prompt, const std::string& tools_json, const std::string& system_prompt) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_initialized || !m_handle) {
        LOG_ERROR("Model not initialized");
        return "[Error] Model not initialized.";
    }

    // 设置函数工具
    if (!tools_json.empty()) {
        int ret = rkllm_set_function_tools(m_handle, system_prompt.c_str(), tools_json.c_str(), "tool_responses");
        if (ret != 0) {
            LOG_WARNING("rkllm_set_function_tools failed, code: " + std::to_string(ret));
        }
    }

    std::string generated_text;
    CallbackContext ctx{&generated_text};

    // 构造输入
    RKLLMInput input;
    input.role = "user";
    input.enable_thinking = false;
    input.input_type = RKLLM_INPUT_PROMPT;
    input.prompt_input = prompt.c_str();

    // 推理参数
    RKLLMInferParam infer_params;
    infer_params.mode = RKLLM_INFER_GENERATE;
    infer_params.lora_params = nullptr;
    infer_params.prompt_cache_params = nullptr;
    infer_params.keep_history = 0;

    LOG_DEBUG("Starting inference with tools...");
    int ret = rkllm_run(m_handle, &input, &infer_params, &ctx);
    if (ret != 0) {
        LOG_ERROR("rkllm_run failed, code: " + std::to_string(ret));
        return "[Error] Inference failed.";
    }

    LOG_DEBUG("Inference with tools completed, output length: " + std::to_string(generated_text.length()));
    return generated_text;
}

// 设置函数工具
bool LLMModel::setFunctionTools(const std::string& system_prompt, const std::string& tools_json, const std::string& tool_response_str) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_handle) {
        LOG_ERROR("Model not initialized, cannot set function tools");
        return false;
    }

    int ret = rkllm_set_function_tools(m_handle, system_prompt.c_str(), tools_json.c_str(), tool_response_str.c_str());
    if (ret != 0) {
        LOG_ERROR("rkllm_set_function_tools failed, code: " + std::to_string(ret));
        return false;
    }

    LOG_INFO("Function tools set successfully");
    return true;
}

// 注册内置工具
void LLMModel::registerBuiltinTools() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_handle) {
        LOG_ERROR("Model not initialized, cannot register tools");
        return;
    }

    std::string tools_json = buildBuiltinToolsJson();
    std::string system_prompt = "You are a helpful assistant. You can use the following functions when needed.";

    int ret = rkllm_set_function_tools(m_handle, system_prompt.c_str(), tools_json.c_str(), "tool_responses");
    if (ret != 0) {
        LOG_ERROR("Failed to register builtin tools, code: " + std::to_string(ret));
        return;
    }

    LOG_INFO("Builtin tools registered successfully");
}

// 构建内置工具的 JSON 定义
std::string LLMModel::buildBuiltinToolsJson() {
    json tools = json::array();

    // get_current_datetime 工具
    json datetime_tool;
    datetime_tool["type"] = "function";
    datetime_tool["function"]["name"] = "get_current_datetime";
    datetime_tool["function"]["description"] = "获取当前的日期和时间信息";
    datetime_tool["function"]["parameters"]["type"] = "object";
    datetime_tool["function"]["parameters"]["properties"] = json::object();
    datetime_tool["function"]["parameters"]["required"] = json::array();
    tools.push_back(datetime_tool);

    return tools.dump();
}

// 清理资源
void LLMModel::cleanup() {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_handle) {
        rkllm_destroy(m_handle);
        m_handle = nullptr;
        LOG_INFO("RKLLM destroyed");
    }

    m_initialized = false;
}

// RKLLM 回调函数
int LLMModel::rkllm_callback(RKLLMResult* result, void* userdata, LLMCallState state) {
    if (!result || !result->text)
        return 0;

    auto* ctx = static_cast<CallbackContext*>(userdata);
    if (ctx && ctx->output) {
        ctx->output->append(result->text);
    }

    // 返回 0 表示继续正常推理
    return 0;
}
