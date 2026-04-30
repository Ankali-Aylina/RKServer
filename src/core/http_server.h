#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <crow.h>
#include <nlohmann/json.hpp>
#include <string>
#include <atomic>

/**
 * @class HttpServer
 * @brief HTTP 服务器管理类
 *
 * 封装 Crow Web 框架，提供以下 API：
 *
 * ## 聊天接口
 * - POST /v1/chat/completions  - OpenAI 兼容接口
 * - POST /chat                 - 简化接口
 *
 * ## 模型接口
 * - GET  /v1/models            - 获取模型列表
 * - POST /v1/embeddings        - Embedding 接口
 * - POST /v1/models/embedding/switch     - 切换 Embedding 模型
 * - GET  /v1/models/embedding/current    - 获取当前模型
 * - GET  /v1/models/embedding/list       - 模型列表
 *
 * ## 系统接口
 * - GET  /health               - 健康检查
 */
class HttpServer
{
public:
    /**
     * @brief 构造函数
     * @param port 监听端口（默认 8080）
     */
    HttpServer(int port = 8080);

    /**
     * @brief 启动 HTTP 服务器
     *
     * 阻塞调用，直到服务器停止。
     * 使用多线程模式处理并发请求。
     */
    void start();

    /**
     * @brief 停止 HTTP 服务器
     *
     * 通常在信号处理函数中调用。
     */
    void stop();

    /**
     * @brief 设置关机标志
     * @param flag true=关机，false=运行
     */
    void setShutdownFlag(bool flag) { m_shutdown_flag = flag; }

    /**
     * @brief 获取 Crow 应用指针
     * @return crow::SimpleApp 指针
     */
    crow::SimpleApp *getApp() { return &m_app; }

    /**
     * @brief 注册所有路由
     *
     * 将各个 API 处理函数绑定到对应的 URL 路径。
     * 必须在 start() 之前调用。
     */
    void setupRoutes();

private:
    crow::SimpleApp m_app;                    ///< Crow Web 应用实例
    int m_port;                               ///< 监听端口
    std::atomic<bool> m_shutdown_flag{false}; ///< 关机标志
};

/**
 * @brief 设置信号处理函数
 * @param server HttpServer 指针
 *
 * 注册 SIGINT（Ctrl+C）和 SIGTERM 信号处理器。
 */
void setupSignalHandlers(HttpServer *server);

#endif // HTTP_SERVER_H
