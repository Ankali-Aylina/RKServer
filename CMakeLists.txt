cmake_minimum_required(VERSION 3.11)
project(RKServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# 子模块检查
# 项目依赖两个 Git 子模块：
# - Crow/       : C++ HTTP 微服务框架
# - json/       : nlohmann/json JSON 解析库
# ============================================================================

# 检查并加载 Crow 子模块
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/Crow/CMakeLists.txt)
    message(STATUS "Found Crow submodule, adding to build")
    add_subdirectory(Crow)
else()
    message(FATAL_ERROR "Crow submodule not found. Please run: git submodule update --init")
endif()

# 检查并加载 nlohmann/json 子模块
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/json/CMakeLists.txt)
    message(STATUS "Found nlohmann/json submodule, adding to build")
    add_subdirectory(json)
else()
    message(FATAL_ERROR "nlohmann/json submodule not found. Please run: git submodule update --init")
endif()

# ============================================================================
# 路径配置
# ============================================================================

# RKLLM 和 RKNN API 头文件路径
set(RKLLM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# RKLLM 和 RKNN 运行时库路径
set(RKLLM_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# SentencePiece 静态库路径（用于 LLM 分词）
set(SENTENCEPIECE_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# 包含目录
include_directories(${RKLLM_INCLUDE_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Crow/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/json/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)

# 链接目录
link_directories(${RKLLM_LIB_DIR})

# 查找 Boost 库（Crow 依赖 Boost.Asio）
find_package(Boost REQUIRED COMPONENTS system)
include_directories(${Boost_INCLUDE_DIRS})

# ============================================================================
# 源文件配置
# ============================================================================

# 核心模块：HTTP 服务器、LLM 推理、Embedding 推理、日志系统
set(CORE_SOURCES
    src/core/http_server.cpp
    src/core/llm_model.cpp
    src/core/embedding_model.cpp
    src/core/logger.cpp
)

# 工具模块：配置管理
set(UTILS_SOURCES
    src/utils/config_manager.cpp
)

# 主程序入口
set(MAIN_SOURCES
    src/main.cpp
)

# 所有源文件汇总
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${UTILS_SOURCES}
    ${MAIN_SOURCES}
)

# 包含目录：使各模块可以互相引用头文件
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/embedding
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http/routes
)

# ============================================================================
# 可执行文件
# ============================================================================

add_executable(RKServer
    ${ALL_SOURCES}
)

# ============================================================================
# 链接库
# ============================================================================

target_link_libraries(RKServer
    ${Boost_LIBRARIES}                    # Boost 库（Crow 依赖）
    rkllmrt                               # RKLLM 运行时（LLM 推理）
    rknnrt                                # RKNN 运行时（Embedding 推理）
    ${SENTENCEPIECE_LIB_DIR}/libsentencepiece.a  # SentencePiece 分词器
    pthread                               # POSIX 线程
    dl                                    # 动态链接库加载
)

# ============================================================================
# 编译选项
# ============================================================================

# 启用编译警告
target_compile_options(RKServer PRIVATE -Wall -Wextra)

# 调试模式：包含调试符号，关闭优化
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# 发布模式：开启优化，关闭调试宏
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# ============================================================================
# 安装配置
# ============================================================================

install(TARGETS RKServer DESTINATION bin)
