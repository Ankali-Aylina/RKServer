/**
 * @file chat_routes.h
 * @brief 聊天相关 API 路由处理器
 *
 * 提供 OpenAI 兼容的聊天补全接口和简化聊天接口。
 * 支持工具调用（Function Calling），使用 RKLLM SDK 原生 API。
 *
 * API 接口：
 * - POST /v1/chat/completions  - OpenAI 兼容的聊天补全接口
 * - POST /chat                 - 简化聊天接口
 */

#ifndef CHAT_ROUTES_H
#define CHAT_ROUTES_H

#include <crow.h>

#include <ctime>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>

#include "llm_model.h"
#include "logger.h"

namespace routes {

using json = nlohmann::json;

class ChatRoutes {
public:
    static crow::response chatCompletions(const crow::request& req) {
        if (req.body.empty())
            return crow::response(400, "Empty body");

        json req_json;
        try {
            req_json = json::parse(req.body);
        } catch (const std::exception& e) {
            return crow::response(400, "Invalid JSON");
        }

        std::string user_prompt;
        std::string system_prompt = "You are a helpful assistant.";
        if (!extractMessages(req_json, user_prompt, system_prompt))
            return crow::response(400, "Missing 'messages' or 'prompt' field");
        if (user_prompt.empty())
            return crow::response(400, "No user prompt found");

        auto& model = LLMModel::getInstance();

        // ---- 模型切换处理 ----
        std::string requested_model = req_json.value("model", "");
        if (!requested_model.empty() && requested_model != model.getCurrentModelName()) {
            LOG_INFO("Requested LLM model switch to: " + requested_model);
            if (!model.switchModel(requested_model)) {
                json error_resp;
                error_resp["error"] = {
                    {"message", "Model switch request failed (maybe already switching or invalid)"},
                    {"type", "invalid_request_error"},
                    {"param", "model"},
                    {"code", "switch_failed"}};
                return crow::response(400, error_resp.dump());
            }

            // 等待切换完成（最多 5 秒，可根据模型大小调整）
            if (model.getState() == LLMModel::ModelState::SWITCHING) {
                if (!model.waitForSwitchComplete(5000)) {
                    json error_resp;
                    error_resp["error"] = {
                        {"message", "Model is still switching, please retry later"},
                        {"type", "server_error"},
                        {"param", nullptr},
                        {"code", "model_switching"}};
                    return crow::response(503, error_resp.dump());  // Service Unavailable
                }
            }
        }

        // 现在模型应该就绪，但以防万一再次检查
        if (model.getState() != LLMModel::ModelState::READY) {
            json error_resp;
            error_resp["error"] = {
                {"message", "Model not ready (state: " + model.getStateString() + ")"},
                {"type", "server_error"},
                {"param", nullptr},
                {"code", "model_not_ready"}};
            return crow::response(503, error_resp.dump());
        }

        // ---- 执行推理 ----
        if (req_json.contains("tools") && req_json["tools"].is_array() &&
            !req_json["tools"].empty()) {
            return handleToolCall(model, req_json, user_prompt, system_prompt);
        } else {
            return handleNormalInference(model, user_prompt);
        }
    }

    static crow::response simpleChat(const crow::request& req) {
        json req_json;
        try {
            req_json = json::parse(req.body);
        } catch (const std::exception& e) {
            return crow::response(400, "Invalid JSON");
        }

        std::string prompt = req_json.value("prompt", "");
        if (prompt.empty())
            return crow::response(400, "Missing 'prompt'");

        auto& model = LLMModel::getInstance();

        // 简化的模型切换（与 chatCompletions 逻辑相同，可复用）
        std::string requested_model = req_json.value("model", "");
        if (!requested_model.empty() && requested_model != model.getCurrentModelName()) {
            LOG_INFO("Requested LLM model switch to: " + requested_model);
            if (!model.switchModel(requested_model)) {
                json error_resp;
                error_resp["error"] = {{"message", "Model switch failed"},
                                       {"type", "invalid_request_error"},
                                       {"param", "model"},
                                       {"code", "switch_failed"}};
                return crow::response(400, error_resp.dump());
            }
            if (model.getState() == LLMModel::ModelState::SWITCHING) {
                if (!model.waitForSwitchComplete(5000)) {
                    json error_resp;
                    error_resp["error"] = {{"message", "Model switching, retry later"},
                                           {"type", "server_error"},
                                           {"param", nullptr},
                                           {"code", "model_switching"}};
                    return crow::response(503, error_resp.dump());
                }
            }
        }

        if (model.getState() != LLMModel::ModelState::READY) {
            return crow::response(503, "Model not ready");
        }

        std::string answer = model.infer(prompt);
        json resp = {{"response", answer}};
        return crow::response(resp.dump());
    }

private:
    /**
     * @brief 从请求中提取消息
     */
    static bool extractMessages(const json& req_json, std::string& user_prompt,
                                std::string& system_prompt) {
        if (req_json.contains("messages") && req_json["messages"].is_array()) {
            for (auto& msg : req_json["messages"]) {
                if (msg["role"] == "user") {
                    user_prompt = extractContent(msg["content"]);
                } else if (msg["role"] == "system") {
                    system_prompt = extractContent(msg["content"]);
                }
            }
            return true;
        } else if (req_json.contains("prompt")) {
            user_prompt = req_json["prompt"].get<std::string>();
            return true;
        }
        return false;
    }

    /**
     * @brief 提取消息内容（支持纯文本和多模态格式）
     */
    static std::string extractContent(const json& content) {
        if (content.is_string()) {
            return content.get<std::string>();
        } else if (content.is_array()) {
            std::string text;
            for (auto& item : content) {
                if (item.is_object() && item.contains("type") && item["type"] == "text" &&
                    item.contains("text")) {
                    text += item["text"].get<std::string>();
                } else if (item.is_string()) {
                    text += item.get<std::string>();
                }
            }
            return text;
        }
        return "";
    }

    /**
     * @brief 处理带工具调用的推理
     *
     * 使用 RKLLM SDK 原生 API (rkllm_set_function_tools) 实现工具调用。
     * 模型输出可能包含工具调用 JSON，需要解析并构建响应。
     */
    static crow::response handleToolCall(LLMModel& model, const json& req_json,
                                         const std::string& user_prompt,
                                         const std::string& system_prompt) {
        json tools_json = req_json["tools"];
        std::string answer = model.inferWithTools(user_prompt, tools_json.dump(), system_prompt);

        // 检查 answer 是否包含工具调用的 JSON 格式
        try {
            json answer_json = json::parse(answer);
            if (answer_json.contains("role") && answer_json.contains("tool_calls")) {
                return buildToolCallResponse(model, answer_json);
            }
        } catch (...) {
            // 不是 JSON 格式，作为普通文本响应
        }

        return buildNormalResponse(model, answer);
    }

    /**
     * @brief 处理普通推理（无工具调用）
     */
    static crow::response handleNormalInference(LLMModel& model, const std::string& user_prompt) {
        std::string answer = model.infer(user_prompt);
        return buildNormalResponse(model, answer);
    }

    /**
     * @brief 构建工具调用响应（OpenAI 兼容格式）
     */
    static crow::response buildToolCallResponse(LLMModel& model, const json& message) {
        json response;
        response["id"] = "chatcmpl-" + std::to_string(std::time(nullptr));
        response["object"] = "chat.completion";
        response["created"] = std::time(nullptr);
        response["model"] = model.getModelName();

        json choice;
        choice["index"] = 0;
        choice["message"] = message;
        choice["finish_reason"] = "tool_calls";

        response["choices"] = json::array({choice});
        response["usage"] = {{"prompt_tokens", 0}, {"completion_tokens", 0}, {"total_tokens", 0}};

        return crow::response(response.dump());
    }

    /**
     * @brief 构建普通文本响应（OpenAI 兼容格式）
     */
    static crow::response buildNormalResponse(LLMModel& model, const std::string& answer) {
        json resp;
        resp["id"] = "chatcmpl-" + std::to_string(std::time(nullptr));
        resp["object"] = "chat.completion";
        resp["created"] = std::time(nullptr);
        resp["model"] = model.getModelName();

        resp["choices"] = json::array();
        resp["choices"][0]["message"]["role"] = "assistant";
        resp["choices"][0]["message"]["content"] = answer;
        resp["choices"][0]["finish_reason"] = "stop";

        resp["usage"] = {{"prompt_tokens", 0}, {"completion_tokens", 0}, {"total_tokens", 0}};

        return crow::response(resp.dump());
    }
};

}  // namespace routes

#endif  // CHAT_ROUTES_H