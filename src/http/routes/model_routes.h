#ifndef MODEL_ROUTES_H
#define MODEL_ROUTES_H

#include <crow.h>

#include <ctime>
#include <nlohmann/json.hpp>
#include <string>

#include "llm_model.h"
#include "config_manager.h"

namespace routes {

using json = nlohmann::json;

/**
 * @brief 模型相关路由处理器
 *
 * 提供健康检查接口和 OpenAI 兼容的模型列表接口。
 */
class ModelRoutes {
public:
    /**
     * @brief 健康检查
     * GET /health
     *
     * 简单的健康检查端点，返回 "OK" 表示服务正常运行。
     *
     * @return HTTP 200 响应，内容为 "OK"
     */
    static crow::response healthCheck() {
        return crow::response(200, "OK");
    }

    /**
     * @brief OpenAI 兼容的模型列表接口
     * GET /v1/models
     *
     * 返回当前加载的 LLM 模型信息，格式与 OpenAI API 兼容。
     *
     * @param req HTTP 请求对象
     * @return JSON 格式的模型列表
     */
    static crow::response listModels(const crow::request& req) {
        (void)req;

        json models_response;
        models_response["object"] = "list";
        models_response["data"] = json::array();

        auto& config = ConfigManager::getInstance();
        auto llm_model_names = config.getAvailableLLMModels();  // 获取所有配置键名

        // 如果配置里没有 LLM 模型，回退到当前加载的模型（兼容旧逻辑）
        if (llm_model_names.empty()) {
            auto& model = LLMModel::getInstance();
            if (model.isInitialized()) {
                json model_entry;
                model_entry["id"] = model.getModelName();
                model_entry["object"] = "model";
                model_entry["created"] = static_cast<long>(std::time(nullptr));
                model_entry["owned_by"] = "rkllm";
                model_entry["permission"] = json::array();
                model_entry["root"] = model.getModelName();
                model_entry["parent"] = nullptr;
                models_response["data"].push_back(model_entry);
            }
        } else {
            // 用配置键名作为 id
            for (const auto& model_name : llm_model_names) {
                json model_entry;
                model_entry["id"] = model_name;  // 使用配置键名
                model_entry["object"] = "model";
                model_entry["created"] = static_cast<long>(std::time(nullptr));
                model_entry["owned_by"] = "rkllm";
                model_entry["permission"] = json::array();
                model_entry["root"] = model_name;
                model_entry["parent"] = nullptr;
                // 可额外添加 active 标志（可选）
                // model_entry["active"] = (model_name == config.getActiveLLMModel());
                models_response["data"].push_back(model_entry);
            }
        }

        return crow::response(models_response.dump());
    }

private:
    /**
     * @brief 构建模型条目
     *
     * 创建符合 OpenAI API 格式的模型条目 JSON 对象。
     *
     * @param model_id 模型标识符
     * @param owned_by 模型所有者（默认 "rkllm"）
     * @return JSON 格式的模型条目
     */
    static json buildModelEntry(const std::string& model_id,
                                const std::string& owned_by = "rkllm") {
        json model_entry;
        model_entry["id"] = model_id;
        model_entry["object"] = "model";
        model_entry["created"] = static_cast<long>(std::time(nullptr));
        model_entry["owned_by"] = owned_by;
        model_entry["permission"] = json::array();
        model_entry["root"] = model_id;
        model_entry["parent"] = nullptr;
        return model_entry;
    }
};

}  // namespace routes

#endif  // MODEL_ROUTES_H
