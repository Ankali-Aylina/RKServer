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
#include <nlohmann/json.hpp>
#include <string>
#include <ctime>
#include <sstream>
#include "llm_model.h"
#include "logger.h"

namespace routes
{

    using json = nlohmann::json;

    class ChatRoutes
    {
    public:
        /**
         * @brief OpenAI 兼容的聊天补全接口
         * POST /v1/chat/completions
         *
         * 处理流程：
         * 1. 解析请求 JSON
         * 2. 提取用户消息和系统提示
         * 3. 检查是否包含工具定义
         * 4. 如果有工具定义，调用 inferWithTools
         * 5. 否则调用普通 infer
         * 6. 返回 OpenAI 兼容格式的响应
         */
        static crow::response chatCompletions(const crow::request &req)
        {
            if (req.body.empty())
            {
                return crow::response(400, "Empty body");
            }

            json req_json;
            try
            {
                req_json = json::parse(req.body);
            }
            catch (const std::exception &e)
            {
                return crow::response(400, "Invalid JSON");
            }

            // 提取用户消息和系统提示
            std::string user_prompt;
            std::string system_prompt = "You are a helpful assistant.";

            if (!extractMessages(req_json, user_prompt, system_prompt))
            {
                return crow::response(400, "Missing 'messages' or 'prompt' field");
            }

            if (user_prompt.empty())
            {
                return crow::response(400, "No user prompt found");
            }

            // 获取模型实例
            auto &model = LLMModel::getInstance();

            // 检查是否包含工具定义
            if (req_json.contains("tools") && req_json["tools"].is_array() && !req_json["tools"].empty())
            {
                return handleToolCall(model, req_json, user_prompt, system_prompt);
            }
            else
            {
                return handleNormalInference(model, user_prompt);
            }
        }

        /**
         * @brief 简化聊天接口
         * POST /chat
         */
        static crow::response simpleChat(const crow::request &req)
        {
            json req_json;
            try
            {
                req_json = json::parse(req.body);
            }
            catch (const std::exception &e)
            {
                return crow::response(400, "Invalid JSON");
            }

            std::string prompt = req_json.value("prompt", "");
            if (prompt.empty())
            {
                return crow::response(400, "Missing 'prompt'");
            }

            auto &model = LLMModel::getInstance();
            std::string answer = model.infer(prompt);

            json resp = {{"response", answer}};
            return crow::response(resp.dump());
        }

    private:
        /**
         * @brief 从请求中提取消息
         */
        static bool extractMessages(const json &req_json, std::string &user_prompt, std::string &system_prompt)
        {
            if (req_json.contains("messages") && req_json["messages"].is_array())
            {
                for (auto &msg : req_json["messages"])
                {
                    if (msg["role"] == "user")
                    {
                        user_prompt = extractContent(msg["content"]);
                    }
                    else if (msg["role"] == "system")
                    {
                        system_prompt = extractContent(msg["content"]);
                    }
                }
                return true;
            }
            else if (req_json.contains("prompt"))
            {
                user_prompt = req_json["prompt"].get<std::string>();
                return true;
            }
            return false;
        }

        /**
         * @brief 提取消息内容（支持纯文本和多模态格式）
         */
        static std::string extractContent(const json &content)
        {
            if (content.is_string())
            {
                return content.get<std::string>();
            }
            else if (content.is_array())
            {
                std::string text;
                for (auto &item : content)
                {
                    if (item.is_object() && item.contains("type") &&
                        item["type"] == "text" && item.contains("text"))
                    {
                        text += item["text"].get<std::string>();
                    }
                    else if (item.is_string())
                    {
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
        static crow::response handleToolCall(LLMModel &model, const json &req_json,
                                             const std::string &user_prompt,
                                             const std::string &system_prompt)
        {
            json tools_json = req_json["tools"];
            std::string answer = model.inferWithTools(user_prompt, tools_json.dump(), system_prompt);

            // 检查 answer 是否包含工具调用的 JSON 格式
            try
            {
                json answer_json = json::parse(answer);
                if (answer_json.contains("role") && answer_json.contains("tool_calls"))
                {
                    return buildToolCallResponse(model, answer_json);
                }
            }
            catch (...)
            {
                // 不是 JSON 格式，作为普通文本响应
            }

            return buildNormalResponse(model, answer);
        }

        /**
         * @brief 处理普通推理（无工具调用）
         */
        static crow::response handleNormalInference(LLMModel &model, const std::string &user_prompt)
        {
            std::string answer = model.infer(user_prompt);
            return buildNormalResponse(model, answer);
        }

        /**
         * @brief 构建工具调用响应（OpenAI 兼容格式）
         */
        static crow::response buildToolCallResponse(LLMModel &model, const json &message)
        {
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
            response["usage"] = {
                {"prompt_tokens", 0},
                {"completion_tokens", 0},
                {"total_tokens", 0}};

            return crow::response(response.dump());
        }

        /**
         * @brief 构建普通文本响应（OpenAI 兼容格式）
         */
        static crow::response buildNormalResponse(LLMModel &model, const std::string &answer)
        {
            json resp;
            resp["id"] = "chatcmpl-" + std::to_string(std::time(nullptr));
            resp["object"] = "chat.completion";
            resp["created"] = std::time(nullptr);
            resp["model"] = model.getModelName();

            resp["choices"] = json::array();
            resp["choices"][0]["message"]["role"] = "assistant";
            resp["choices"][0]["message"]["content"] = answer;
            resp["choices"][0]["finish_reason"] = "stop";

            resp["usage"] = {
                {"prompt_tokens", 0},
                {"completion_tokens", 0},
                {"total_tokens", 0}};

            return crow::response(resp.dump());
        }
    };

} // namespace routes

#endif // CHAT_ROUTES_H
