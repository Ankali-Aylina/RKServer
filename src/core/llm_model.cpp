/**
 * @file llm_model.cpp
 * @brief LLM 模型管理类实现（异步切换版本）
 */

#include "llm_model.h"

#include <malloc.h>  // for malloc_trim

#include <ctime>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "config_manager.h"
#include "logger.h"

using json = nlohmann::json;

// ---- 单例 ----
LLMModel& LLMModel::getInstance() {
    static LLMModel instance;
    return instance;
}

// ---- 构造/析构 ----
LLMModel::LLMModel() : m_handle(nullptr), m_initialized(false), m_max_new_tokens(512) {
    m_state.store(ModelState::IDLE);
}

LLMModel::~LLMModel() {
    cleanup();
}

// ---- 状态字符串 ----
std::string LLMModel::getStateString() const {
    switch (m_state.load()) {
    case ModelState::IDLE:
        return "idle";
    case ModelState::READY:
        return "ready";
    case ModelState::SWITCHING:
        return "switching";
    case ModelState::ERROR:
        return "error";
    default:
        return "unknown";
    }
}

// ---- 加载模型（内部，不加锁） ----
bool LLMModel::loadModelInternal(const ModelConfig& config) {
    // 销毁旧句柄（如果存在）
    if (m_handle) {
        rkllm_destroy(m_handle);
        m_handle = nullptr;
    }

    RKLLMParam param = rkllm_createDefaultParam();
    param.model_path = config.model_path.c_str();
    param.max_context_len = config.max_context_length; 
    param.max_new_tokens = config.max_new_tokens;
    param.top_k = 40;
    param.top_p = 0.95;
    param.temperature = 0.7;
    param.repeat_penalty = 1.1;
    param.is_async = false;

    int ret = rkllm_init(&m_handle, &param, rkllm_callback);
    if (ret != 0) {
        LOG_ERROR("rkllm_init failed for " + config.model_path + ", code: " + std::to_string(ret));
        m_handle = nullptr;
        return false;
    }


    LOG_INFO("RKLLM loaded: " + config.model_path +
             ", max_new_tokens: " + std::to_string(config.max_new_tokens));
    return true;
}

// ---- 从配置管理器加载所有模型配置 ----
bool LLMModel::loadConfigFromManager() {
    auto& config_manager = ConfigManager::getInstance();
    if (!config_manager.isLoaded()) {
        LOG_ERROR("Config manager not loaded");
        return false;
    }

    auto model_names = config_manager.getAvailableLLMModels();
    if (model_names.empty()) {
        LOG_ERROR("No LLM models found in config");
        return false;
    }

    for (const auto& name : model_names) {
        auto cfg = config_manager.getLLMModelConfig(name);
        ModelConfig model_cfg;
        model_cfg.model_path = cfg.model_path;
        model_cfg.max_new_tokens = cfg.max_new_tokens;
        model_cfg.max_context_length = cfg.max_context_length; 
        m_model_configs[name] = model_cfg;
        LOG_DEBUG("Loaded LLM model config: " + name + " -> " + model_cfg.model_path);
    }

    m_current_model_name = config_manager.getActiveLLMModel();
    if (m_model_configs.find(m_current_model_name) == m_model_configs.end()) {
        LOG_WARNING("Active model '" + m_current_model_name + "' not found, using first available");
        m_current_model_name = m_model_configs.begin()->first;
    }

    LOG_INFO("LLM model configs loaded, available: " + std::to_string(m_model_configs.size()) +
             ", active: " + m_current_model_name);
    return true;
}

// ---- 初始化（同步，启动时调用） ----
bool LLMModel::initializeWithConfig() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        LOG_WARNING("Model already initialized");
        return true;
    }

    if (!loadConfigFromManager()) {
        m_state.store(ModelState::ERROR);
        return false;
    }

    auto it = m_model_configs.find(m_current_model_name);
    if (it == m_model_configs.end()) {
        LOG_ERROR("No valid model found to load");
        m_state.store(ModelState::ERROR);
        return false;
    }

    if (!loadModelInternal(it->second)) {
        LOG_ERROR("Failed to load active model: " + m_current_model_name);
        m_state.store(ModelState::ERROR);
        return false;
    }

    // 注册工具
    registerBuiltinToolsLocked();
    m_initialized = true;
    m_state.store(ModelState::READY);
    LOG_INFO("LLM model initialized successfully: " + m_current_model_name);
    return true;
}

// ---- 清理资源 ----
void LLMModel::cleanup() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_handle) {
        rkllm_destroy(m_handle);
        m_handle = nullptr;
#ifdef __linux__
        malloc_trim(0);
#endif
        LOG_INFO("RKLLM destroyed");
    }
    m_initialized = false;
    m_current_model_name.clear();
    m_state.store(ModelState::IDLE);
}

// ---- 推理函数（带状态检查） ----
std::string LLMModel::infer(const std::string& prompt) {
    // 快速检查状态（不加锁）
    if (m_state.load() != ModelState::READY) {
        LOG_ERROR("Model not ready (state: " + getStateString() + ")");
        return "[Error] Model is currently " + getStateString() + ", please try again later.";
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    // 双重检查
    if (m_state.load() != ModelState::READY || !m_handle) {
        return "[Error] Model not ready.";
    }

    std::string generated_text;
    CallbackContext ctx{&generated_text};

    RKLLMInput input;
    input.role = "user";
    input.enable_thinking = false;
    input.input_type = RKLLM_INPUT_PROMPT;
    input.prompt_input = prompt.c_str();

    RKLLMInferParam infer_params;
    infer_params.mode = RKLLM_INFER_GENERATE;
    infer_params.lora_params = nullptr;
    infer_params.prompt_cache_params = nullptr;
    infer_params.keep_history = 0;

    int ret = rkllm_run(m_handle, &input, &infer_params, &ctx);
    if (ret != 0) {
        LOG_ERROR("rkllm_run failed, code: " + std::to_string(ret));
        return "[Error] Inference failed.";
    }

    return generated_text;
}

std::string LLMModel::inferWithSystemPrompt(const std::string& prompt,
                                            const std::string& system_prompt) {
    std::string full_prompt = system_prompt + "\n\n" + prompt;
    return infer(full_prompt);
}

std::string LLMModel::inferWithTools(const std::string& prompt, const std::string& tools_json,
                                     const std::string& system_prompt) {
    // 状态检查
    if (m_state.load() != ModelState::READY) {
        LOG_ERROR("Model not ready (state: " + getStateString() + ")");
        return "[Error] Model is currently " + getStateString() + ", please try again later.";
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state.load() != ModelState::READY || !m_handle) {
        return "[Error] Model not ready.";
    }

    // 设置工具
    if (!tools_json.empty()) {
        int ret = rkllm_set_function_tools(m_handle, system_prompt.c_str(), tools_json.c_str(),
                                           "tool_responses");
        if (ret != 0) {
            LOG_WARNING("rkllm_set_function_tools failed, code: " + std::to_string(ret));
        }
    }

    std::string generated_text;
    CallbackContext ctx{&generated_text};

    RKLLMInput input;
    input.role = "user";
    input.enable_thinking = false;
    input.input_type = RKLLM_INPUT_PROMPT;
    input.prompt_input = prompt.c_str();

    RKLLMInferParam infer_params;
    infer_params.mode = RKLLM_INFER_GENERATE;
    infer_params.lora_params = nullptr;
    infer_params.prompt_cache_params = nullptr;
    infer_params.keep_history = 0;

    int ret = rkllm_run(m_handle, &input, &infer_params, &ctx);
    if (ret != 0) {
        LOG_ERROR("rkllm_run failed, code: " + std::to_string(ret));
        return "[Error] Inference failed.";
    }

    return generated_text;
}

// ---- 工具注册 ----
void LLMModel::registerBuiltinTools() {
    std::lock_guard<std::mutex> lock(m_mutex);
    registerBuiltinToolsLocked();
}

void LLMModel::registerBuiltinToolsLocked() {
    if (!m_handle) {
        LOG_ERROR("Model not initialized, cannot register tools");
        return;
    }

    std::string tools_json = buildBuiltinToolsJson();
    std::string system_prompt =
        "You are a helpful assistant. You can use the following functions when needed.";

    int ret = rkllm_set_function_tools(m_handle, system_prompt.c_str(), tools_json.c_str(),
                                       "tool_responses");
    if (ret != 0) {
        LOG_ERROR("Failed to register builtin tools, code: " + std::to_string(ret));
        return;
    }
    LOG_INFO("Builtin tools registered successfully");
}

std::string LLMModel::buildBuiltinToolsJson() {
    json tools = json::array();
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

// ---- 异步切换 ----
bool LLMModel::switchModel(const std::string& model_name) {
    std::lock_guard<std::mutex> lock(m_switch_mutex);

    if (m_state.load() == ModelState::SWITCHING) {
        LOG_WARNING("Already switching, please wait");
        return false;
    }

    auto it = m_model_configs.find(model_name);
    if (it == m_model_configs.end()) {
        LOG_ERROR("Model not found: " + model_name);
        return false;
    }

    if (model_name == m_current_model_name && m_state.load() == ModelState::READY) {
        LOG_DEBUG("Model already loaded: " + model_name);
        return true;
    }

    LOG_INFO("Starting asynchronous switch to model: " + model_name);

    ModelConfig target_config = it->second;
    std::string target_name = model_name;
    m_state.store(ModelState::SWITCHING);

    m_switch_future = std::async(std::launch::async, [this, target_config, target_name]() -> bool {
        // 1. 销毁旧模型（持有锁保护 m_handle 访问）
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_handle) {
                LOG_INFO("Destroying old model...");
                rkllm_destroy(m_handle);
                m_handle = nullptr;
#ifdef __linux__
                malloc_trim(0);
#endif
                LOG_INFO("Old model destroyed.");
            }
            // 清除初始化状态
            m_initialized = false;
        }

        // 2. 加载新模型（loadModelInternal 内部会修改 m_handle，但我们已持有锁？）
        // 实际上，loadModelInternal 会修改 m_handle，并可能调用 rkllm_init，这可能会阻塞。
        // 为了减少锁持有时间，我们不在这里加锁，因为此时状态已经是 SWITCHING，
        // 推理函数会因状态检查而拒绝访问 m_handle，所以安全。
        // 但为了代码安全，我们也可以在 loadModelInternal 内部加锁，但那样会延长锁持有时间。
        // 权衡：我们选择让 loadModelInternal 不加锁，但在调用前确保状态为 SWITCHING。
        // 这样推理不会访问，cleanup 不会被调用（除非退出），因此安全。
        if (!loadModelInternal(target_config)) {
            LOG_ERROR("Failed to load model: " + target_name);
            m_state.store(ModelState::ERROR);
            return false;
        }

        // 3. 注册工具并更新状态（需要持有锁）
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            registerBuiltinToolsLocked();
            m_current_model_name = target_name;
            m_initialized = true;
        }

        // 4. 状态设为 READY
        m_state.store(ModelState::READY);
        LOG_INFO("Successfully switched to model: " + target_name);
        return true;
    });

    return true;  // 切换任务已启动
}

// ---- 等待切换完成（可选） ----
bool LLMModel::waitForSwitchComplete(int timeout_ms) {
    if (m_state.load() != ModelState::SWITCHING)
        return true;  // 不是切换中

    if (m_switch_future.valid()) {
        auto status = m_switch_future.wait_for(std::chrono::milliseconds(timeout_ms));
        if (status == std::future_status::ready) {
            return m_switch_future.get();
        } else {
            LOG_WARNING("Switch timeout after " + std::to_string(timeout_ms) + "ms");
            return false;
        }
    }
    return false;
}

// ---- 获取可用模型列表 ----
std::vector<std::string> LLMModel::getAvailableModels() const {
    std::vector<std::string> names;
    for (const auto& pair : m_model_configs) {
        names.push_back(pair.first);
    }
    return names;
}

// ---- 回调函数 ----
int LLMModel::rkllm_callback(RKLLMResult* result, void* userdata, LLMCallState state) {
    if (!result || !result->text)
        return 0;
    auto* ctx = static_cast<CallbackContext*>(userdata);
    if (ctx && ctx->output) {
        ctx->output->append(result->text);
    }
    return 0;
}

// ---- 旧的初始化（保留，但内部使用新逻辑） ----
bool LLMModel::initialize(const std::string& model_path, int max_new_tokens) {
    // 为了兼容，可以调用 initializeWithConfig 或直接加载
    // 但为了简单，直接调用内部逻辑
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_initialized) {
        LOG_INFO("Model already initialized");
        return true;
    }

    m_max_new_tokens = max_new_tokens;
    ModelConfig config;
    config.model_path = model_path;
    config.max_new_tokens = max_new_tokens;
    if (!loadModelInternal(config)) {
        return false;
    }
    registerBuiltinToolsLocked();
    m_initialized = true;
    m_state.store(ModelState::READY);
    return true;
}