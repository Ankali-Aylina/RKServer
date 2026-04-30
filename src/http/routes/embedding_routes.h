#ifndef EMBEDDING_ROUTES_H
#define EMBEDDING_ROUTES_H

/**
 * @file embedding_routes.h
 * @brief Embedding（文本向量化）API 路由处理器
 *
 * 提供 OpenAI 兼容的 Embedding API 接口，支持多模型切换。
 *
 * API 路由列表：
 * - POST /v1/embeddings              → OpenAI 兼容的 Embedding 接口
 * - POST /v1/embedding/switch        → 切换 Embedding 模型
 * - GET  /v1/embedding/current       → 获取当前 Embedding 模型信息
 * - GET  /v1/embedding/models        → 获取可用 Embedding 模型列表
 */

#include <crow.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "embedding_model.h"
#include "logger.h"

namespace routes
{

    using json = nlohmann::json;

    class EmbeddingRoutes
    {
    public:
        /**
         * @brief OpenAI 兼容的 Embedding 接口
         * POST /v1/embeddings
         */
        static crow::response embeddings(const crow::request &req)
        {
            try
            {
                if (req.body.empty())
                {
                    json error_resp;
                    error_resp["error"] = {
                        {"message", "Missing input"},
                        {"type", "invalid_request_error"},
                        {"param", "input"},
                        {"code", "missing_input"}};
                    crow::response response(400);
                    response.set_header("Content-Type", "application/json");
                    response.set_header("Access-Control-Allow-Origin", "*");
                    response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                    response.set_header("Access-Control-Allow-Headers", "*");
                    response.body = error_resp.dump();
                    return response;
                }

                json req_json;
                try
                {
                    req_json = json::parse(req.body);
                }
                catch (const std::exception &e)
                {
                    json error_resp;
                    error_resp["error"] = {
                        {"message", "Invalid JSON: " + std::string(e.what())},
                        {"type", "invalid_request_error"},
                        {"param", nullptr},
                        {"code", "invalid_json"}};
                    crow::response response(400);
                    response.set_header("Content-Type", "application/json");
                    response.set_header("Access-Control-Allow-Origin", "*");
                    response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                    response.set_header("Access-Control-Allow-Headers", "*");
                    response.body = error_resp.dump();
                    return response;
                }

                auto &model = EmbeddingModel::getInstance();
                if (!model.isInitialized())
                {
                    json error_resp;
                    error_resp["error"] = {
                        {"message", "The server had an error while processing your request. Refer to the logs for more details."},
                        {"type", "server_error"},
                        {"param", nullptr},
                        {"code", "model_not_initialized"}};
                    crow::response response(503);
                    response.set_header("Content-Type", "application/json");
                    response.set_header("Access-Control-Allow-Origin", "*");
                    response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                    response.set_header("Access-Control-Allow-Headers", "*");
                    response.body = error_resp.dump();
                    return response;
                }

                json resp;
                resp["object"] = "list";
                resp["data"] = json::array();

                std::string model_name = model.getCurrentModel();
                if (req_json.contains("model"))
                {
                    std::string requested_model = req_json["model"].get<std::string>();
                    if (requested_model != model.getCurrentModel())
                    {
                        if (model.switchModel(requested_model))
                        {
                            model_name = requested_model;
                            LOG_INFO("Switched to embedding model: " + requested_model);
                        }
                        else
                        {
                            LOG_WARNING("Requested model '" + requested_model +
                                        "' not found, using current model '" + model_name + "'");
                        }
                    }
                    else
                    {
                        model_name = requested_model;
                    }
                }
                resp["model"] = model_name;

                int requested_dim = -1;
                if (req_json.contains("dimensions"))
                {
                    requested_dim = req_json["dimensions"].get<int>();
                    LOG_DEBUG("Embedding request with dimensions: " + std::to_string(requested_dim));
                }

                LOG_INFO("Embedding request: model=" + model_name +
                         ", input_type=" + (req_json["input"].is_string() ? "string" : "array") +
                         ", dimensions=" + (requested_dim > 0 ? std::to_string(requested_dim) : "default"));

                if (req_json.contains("input"))
                {
                    auto &input = req_json["input"];
                    std::vector<std::string> texts;

                    if (input.is_string())
                    {
                        texts.push_back(input.get<std::string>());
                    }
                    else if (input.is_array())
                    {
                        for (const auto &item : input)
                        {
                            if (item.is_string())
                            {
                                texts.push_back(item.get<std::string>());
                            }
                            else if (item.is_number())
                            {
                                texts.push_back(std::to_string(item.get<double>()));
                            }
                        }
                    }
                    else
                    {
                        json error_resp;
                        error_resp["error"] = {
                            {"message", "Invalid input type. Expected string or array of strings."},
                            {"type", "invalid_request_error"},
                            {"param", "input"},
                            {"code", "invalid_input_type"}};
                        crow::response response(400);
                        response.set_header("Content-Type", "application/json");
                        response.set_header("Access-Control-Allow-Origin", "*");
                        response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                        response.set_header("Access-Control-Allow-Headers", "*");
                        response.body = error_resp.dump();
                        return response;
                    }

                    int index = 0;
                    for (const auto &text : texts)
                    {
                        auto embedding_vec = model.getEmbedding(text);

                        if (requested_dim > 0 && requested_dim < static_cast<int>(embedding_vec.size()))
                        {
                            embedding_vec.resize(requested_dim);
                        }

                        json item;
                        item["object"] = "embedding";
                        item["index"] = index;
                        item["embedding"] = embedding_vec;
                        resp["data"].push_back(item);
                        index++;
                    }
                }
                else
                {
                    json error_resp;
                    error_resp["error"] = {
                        {"message", "Missing input"},
                        {"type", "invalid_request_error"},
                        {"param", "input"},
                        {"code", "missing_input"}};
                    crow::response response(400);
                    response.set_header("Content-Type", "application/json");
                    response.set_header("Access-Control-Allow-Origin", "*");
                    response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                    response.set_header("Access-Control-Allow-Headers", "*");
                    response.body = error_resp.dump();
                    return response;
                }

                resp["usage"] = {
                    {"prompt_tokens", static_cast<int>(resp["data"].size())},
                    {"total_tokens", static_cast<int>(resp["data"].size())},
                    {"completion_tokens", 0}};

                crow::response response(200);
                response.set_header("Content-Type", "application/json");
                response.set_header("Access-Control-Allow-Origin", "*");
                response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                response.set_header("Access-Control-Allow-Headers", "*");
                response.body = resp.dump();
                return response;
            }
            catch (const std::exception &e)
            {
                LOG_ERROR("Embedding endpoint crashed: " + std::string(e.what()));
                json error_resp;
                error_resp["error"] = {
                    {"message", "Internal server error"},
                    {"type", "server_error"},
                    {"param", nullptr},
                    {"code", "internal_server_error"}};
                crow::response response(500);
                response.set_header("Content-Type", "application/json");
                response.set_header("Access-Control-Allow-Origin", "*");
                response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                response.set_header("Access-Control-Allow-Headers", "*");
                response.body = error_resp.dump();
                return response;
            }
            catch (...)
            {
                LOG_ERROR("Embedding endpoint crashed with unknown error");
                json error_resp;
                error_resp["error"] = {
                    {"message", "Internal server error"},
                    {"type", "server_error"},
                    {"param", nullptr},
                    {"code", "internal_server_error"}};
                crow::response response(500);
                response.set_header("Content-Type", "application/json");
                response.set_header("Access-Control-Allow-Origin", "*");
                response.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
                response.set_header("Access-Control-Allow-Headers", "*");
                response.body = error_resp.dump();
                return response;
            }
        }

        /**
         * @brief 切换 Embedding 模型
         * POST /v1/embedding/switch
         */
        static crow::response switchEmbeddingModel(const crow::request &req)
        {
            json resp;

            if (req.body.empty())
            {
                resp["success"] = false;
                resp["error"] = "Empty body";
                return crow::response(400, resp.dump());
            }

            json req_json;
            try
            {
                req_json = json::parse(req.body);
            }
            catch (const std::exception &e)
            {
                resp["success"] = false;
                resp["error"] = "Invalid JSON: " + std::string(e.what());
                return crow::response(400, resp.dump());
            }

            if (!req_json.contains("model"))
            {
                resp["success"] = false;
                resp["error"] = "Missing 'model' field";
                return crow::response(400, resp.dump());
            }

            std::string model_name = req_json["model"].get<std::string>();
            auto &model = EmbeddingModel::getInstance();

            LOG_INFO("Request to switch embedding model to: " + model_name);

            if (!model.switchModel(model_name))
            {
                resp["success"] = false;
                resp["error"] = "Failed to switch model: " + model_name;
                resp["available_models"] = model.getAvailableModels();
                return crow::response(400, resp.dump());
            }

            resp["success"] = true;
            resp["message"] = "Model switched successfully";
            resp["current_model"] = model.getCurrentModel();
            resp["available_models"] = model.getAvailableModels();

            LOG_INFO("Successfully switched to model: " + model.getCurrentModel());

            return crow::response(200, resp.dump());
        }

        /**
         * @brief 获取当前 Embedding 模型信息
         * GET /v1/embedding/current
         */
        static crow::response getCurrentEmbeddingModel(const crow::request &req)
        {
            (void)req;

            json resp;
            auto &model = EmbeddingModel::getInstance();

            if (!model.isInitialized())
            {
                resp["initialized"] = false;
                resp["current_model"] = nullptr;
                return crow::response(503, resp.dump());
            }

            resp["initialized"] = true;
            resp["current_model"] = model.getCurrentModel();
            resp["available_models"] = model.getAvailableModels();

            return crow::response(200, resp.dump());
        }

        /**
         * @brief 获取可用 Embedding 模型列表
         * GET /v1/embedding/models
         */
        static crow::response listEmbeddingModels(const crow::request &req)
        {
            (void)req;

            json resp;
            auto &model = EmbeddingModel::getInstance();

            resp["object"] = "list";
            resp["data"] = json::array();

            auto models = model.getAvailableModels();
            auto current_model = model.getCurrentModel();

            for (const auto &model_name : models)
            {
                json model_entry;
                model_entry["id"] = model_name;
                model_entry["object"] = "embedding_model";
                model_entry["active"] = (model_name == current_model);
                resp["data"].push_back(model_entry);
            }

            resp["current_model"] = current_model;

            return crow::response(200, resp.dump());
        }

    private:
        static crow::response errorResponse(int code, const std::string &message)
        {
            json resp;
            resp["success"] = false;
            resp["error"] = message;
            return crow::response(code, resp.dump());
        }
    };

} // namespace routes

#endif // EMBEDDING_ROUTES_H
