#include "http_server.h"
#include "llm_model.h"
#include "embedding_model.h"
#include "logger.h"
#include "../http/routes/chat_routes.h"
#include "../http/routes/embedding_routes.h"
#include "../http/routes/model_routes.h"
#include <iostream>
#include <csignal>
#include <atomic>

using json = nlohmann::json;

// 全局变量用于信号处理
static std::atomic<HttpServer *> g_server_ptr{nullptr};

/**
 * @brief 信号处理函数
 * @param sig 信号编号（SIGINT 或 SIGTERM）
 */
void signal_handler(int sig)
{
    if (sig == SIGINT || sig == SIGTERM)
    {
        std::cout << "\nReceived shutdown signal. Stopping server..." << std::endl;
        auto *server = g_server_ptr.load();
        if (server)
        {
            server->setShutdownFlag(true);
            server->stop();
        }
    }
}

/**
 * @brief 设置信号处理器
 * @param server HttpServer 指针
 */
void setupSignalHandlers(HttpServer *server)
{
    g_server_ptr.store(server);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

/**
 * @brief 构造函数
 * @param port 监听端口（默认 8080）
 */
HttpServer::HttpServer(int port) : m_port(port)
{
    m_app.loglevel(crow::LogLevel::Warning);
}

/**
 * @brief 启动 HTTP 服务器
 *
 * 阻塞调用，直到服务器停止。
 * 使用多线程模式处理并发请求。
 */
void HttpServer::start()
{
    std::cout << "Starting HTTP server on http://0.0.0.0:" << m_port << std::endl;
    LOG_INFO("Starting HTTP server on http://0.0.0.0:" + std::to_string(m_port));
    m_app.port(m_port).multithreaded().run();
    std::cout << "Server stopped." << std::endl;
}

/**
 * @brief 停止 HTTP 服务器
 */
void HttpServer::stop()
{
    m_app.stop();
}

/**
 * @brief 注册所有 API 路由
 *
 * 将各个 API 处理函数绑定到对应的 URL 路径。
 * 每个路由使用 lambda 表达式将请求转发到对应的 Route 类静态方法。
 */
void HttpServer::setupRoutes()
{
    using namespace routes;

    // ==================== 健康检查 ====================
    CROW_ROUTE(m_app, "/health")
    ([]()
     { return ModelRoutes::healthCheck(); });

    // ==================== 模型接口 ====================
    CROW_ROUTE(m_app, "/v1/models")
        .methods("GET"_method)([](const crow::request &req)
                               { return ModelRoutes::listModels(req); });

    // ==================== 聊天接口 ====================
    CROW_ROUTE(m_app, "/v1/chat/completions")
        .methods("POST"_method)([](const crow::request &req)
                                { return ChatRoutes::chatCompletions(req); });

    CROW_ROUTE(m_app, "/chat")
        .methods("POST"_method)([](const crow::request &req)
                                { return ChatRoutes::simpleChat(req); });

    // ==================== Embedding 接口 ====================
    CROW_ROUTE(m_app, "/v1/embeddings")
        .methods("POST"_method)([](const crow::request &req)
                                { return EmbeddingRoutes::embeddings(req); });

    CROW_ROUTE(m_app, "/v1/models/embedding/switch")
        .methods("POST"_method)([](const crow::request &req)
                                { return EmbeddingRoutes::switchEmbeddingModel(req); });

    CROW_ROUTE(m_app, "/v1/models/embedding/current")
        .methods("GET"_method)([](const crow::request &req)
                               { return EmbeddingRoutes::getCurrentEmbeddingModel(req); });

    CROW_ROUTE(m_app, "/v1/models/embedding/list")
        .methods("GET"_method)([](const crow::request &req)
                               { return EmbeddingRoutes::listEmbeddingModels(req); });
}
