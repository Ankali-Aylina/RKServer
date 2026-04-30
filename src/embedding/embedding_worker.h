#ifndef EMBEDDING_WORKER_H
#define EMBEDDING_WORKER_H

/**
 * @file embedding_worker.h
 * @brief RKNN Embedding 推理工作器（头文件内联实现）
 *
 * 封装 RKNN（Rockchip Neural Network）API 调用，提供 Embedding 模型的
 * 初始化、推理、资源释放等操作。
 *
 * 推理流程：
 * 1. 设置输入（input_ids + attention_mask）
 * 2. 调用 rknn_run 执行推理
 * 3. 获取输出向量
 * 4. L2 归一化
 * 5. 返回归一化后的向量
 *
 * @namespace embedding
 */

#include <vector>
#include <memory>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "rknn_api.h"
#include "logger.h"

namespace embedding
{

    /**
     * @brief Embedding 工作器配置
     */
    struct WorkerConfig
    {
        int max_seq_len = 256;
        int embedding_dim = 384;
        int input_ids_index = -1;
        int attention_mask_index = -1;
        int output_index = 0;
        int n_outputs = 1;
    };

    /**
     * @brief RKNN Embedding 工作器
     */
    class EmbeddingWorker
    {
    public:
        EmbeddingWorker() : m_ctx(0) {}

        ~EmbeddingWorker()
        {
            destroy();
        }

        EmbeddingWorker(const EmbeddingWorker &) = delete;
        EmbeddingWorker &operator=(const EmbeddingWorker &) = delete;

        /**
         * @brief 从模型数据初始化 RKNN 上下文
         * @param model_data 模型二进制数据
         * @param config 输出参数，填充查询到的模型配置信息
         * @return 初始化成功返回 true
         */
        bool initialize(const std::vector<char> &model_data, WorkerConfig &config)
        {
            if (m_ctx != 0)
            {
                LOG_WARNING("Worker already initialized");
                return true;
            }

            int ret = rknn_init(&m_ctx, const_cast<char *>(model_data.data()),
                                model_data.size(), 0, nullptr);
            if (ret < 0)
            {
                LOG_ERROR("rknn_init failed: " + std::to_string(ret));
                return false;
            }

            if (!queryIO(config))
            {
                LOG_ERROR("Failed to query IO");
                destroy();
                return false;
            }

            LOG_INFO("Embedding worker initialized successfully");
            return true;
        }

        /**
         * @brief 执行推理获取嵌入向量
         * @param input_ids 输入 token ID 序列
         * @param attention_mask 注意力掩码
         * @param config 工作器配置
         * @return 归一化后的嵌入向量
         */
        std::vector<float> infer(const std::vector<int64_t> &input_ids,
                                 const std::vector<int64_t> &attention_mask,
                                 const WorkerConfig &config)
        {
            if (m_ctx == 0)
            {
                LOG_ERROR("Worker not initialized");
                return {};
            }

            rknn_input inputs[2];
            std::memset(inputs, 0, sizeof(inputs));

            inputs[0].index = config.input_ids_index;
            inputs[0].buf = const_cast<int64_t *>(input_ids.data());
            inputs[0].size = config.max_seq_len * sizeof(int64_t);
            inputs[0].type = RKNN_TENSOR_INT64;
            inputs[0].fmt = RKNN_TENSOR_NCHW;

            inputs[1].index = config.attention_mask_index;
            inputs[1].buf = const_cast<int64_t *>(attention_mask.data());
            inputs[1].size = config.max_seq_len * sizeof(int64_t);
            inputs[1].type = RKNN_TENSOR_INT64;
            inputs[1].fmt = RKNN_TENSOR_NCHW;

            int ret = rknn_inputs_set(m_ctx, 2, inputs);
            if (ret < 0)
            {
                LOG_ERROR("rknn_inputs_set failed: " + std::to_string(ret));
                return {};
            }

            ret = rknn_run(m_ctx, nullptr);
            if (ret < 0)
            {
                LOG_ERROR("rknn_run failed: " + std::to_string(ret));
                return {};
            }

            std::vector<rknn_output> outputs(config.n_outputs);
            std::memset(outputs.data(), 0, config.n_outputs * sizeof(rknn_output));
            for (uint32_t i = 0; i < static_cast<uint32_t>(config.n_outputs); ++i)
            {
                outputs[i].want_float = 1;
                outputs[i].index = i;
                outputs[i].is_prealloc = 0;
            }

            ret = rknn_outputs_get(m_ctx, config.n_outputs, outputs.data(), nullptr);
            if (ret < 0)
            {
                LOG_ERROR("rknn_outputs_get failed: " + std::to_string(ret));
                return {};
            }

            std::vector<float> result(config.embedding_dim);
            uint32_t out_idx = static_cast<uint32_t>(config.output_index);
            if (out_idx < static_cast<uint32_t>(config.n_outputs) && outputs[out_idx].size > 0)
            {
                size_t copy_size = std::min(
                    static_cast<size_t>(outputs[out_idx].size),
                    static_cast<size_t>(config.embedding_dim * sizeof(float)));
                std::memcpy(result.data(), outputs[out_idx].buf, copy_size);
            }

            rknn_outputs_release(m_ctx, config.n_outputs, outputs.data());

            normalize(result);

            return result;
        }

        /**
         * @brief 销毁工作器，释放 RKNN 上下文
         */
        void destroy()
        {
            if (m_ctx != 0)
            {
                rknn_destroy(m_ctx);
                m_ctx = 0;
                LOG_DEBUG("RKNN context destroyed");
            }
        }

        bool isInitialized() const
        {
            return m_ctx != 0;
        }

    private:
        rknn_context m_ctx;

        bool queryIO(WorkerConfig &config)
        {
            rknn_input_output_num io_num;
            int ret = rknn_query(m_ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
            if (ret < 0)
            {
                LOG_ERROR("Failed to query IO num");
                return false;
            }

            LOG_DEBUG("Input count: " + std::to_string(io_num.n_input) +
                      ", Output count: " + std::to_string(io_num.n_output));

            for (uint32_t i = 0; i < io_num.n_input; ++i)
            {
                rknn_tensor_attr attr;
                attr.index = i;
                ret = rknn_query(m_ctx, RKNN_QUERY_INPUT_ATTR, &attr, sizeof(attr));
                if (ret < 0)
                    continue;

                std::string name(attr.name);
                LOG_DEBUG("Input " + std::to_string(i) + ": " + name +
                          ", type=" + std::to_string(attr.type) +
                          ", dims=" + std::to_string(attr.n_dims) + "D");

                // 打印维度信息
                std::string dims_str;
                for (uint32_t d = 0; d < attr.n_dims; ++d)
                {
                    dims_str += std::to_string(attr.dims[d]) + " ";
                }
                LOG_DEBUG("  dims: " + dims_str);

                if (name.find("input_ids") != std::string::npos)
                {
                    config.input_ids_index = i;
                    // 从模型实际维度获取 max_seq_len
                    // 通常 input_ids 的 shape 为 [1, seq_len] 或 [1, 1, seq_len]
                    if (attr.n_dims >= 2)
                    {
                        int model_seq_len = attr.dims[attr.n_dims - 1];
                        if (model_seq_len > 0 && model_seq_len != config.max_seq_len)
                        {
                            LOG_INFO("Updating max_seq_len from config(" +
                                     std::to_string(config.max_seq_len) + ") to model(" +
                                     std::to_string(model_seq_len) + ")");
                            config.max_seq_len = model_seq_len;
                        }
                    }
                }
                else if (name.find("attention_mask") != std::string::npos)
                {
                    config.attention_mask_index = i;
                }
            }

            for (uint32_t i = 0; i < io_num.n_output; ++i)
            {
                rknn_tensor_attr attr;
                attr.index = i;
                ret = rknn_query(m_ctx, RKNN_QUERY_OUTPUT_ATTR, &attr, sizeof(attr));
                if (ret < 0)
                    continue;

                config.n_outputs = io_num.n_output;
                if (i == 0)
                {
                    config.output_index = i;
                    config.embedding_dim = attr.dims[attr.n_dims - 1];
                    LOG_DEBUG("Output embedding dim: " + std::to_string(config.embedding_dim));
                }
            }

            if (config.input_ids_index < 0 || config.attention_mask_index < 0)
            {
                LOG_ERROR("Failed to find required inputs");
                return false;
            }

            return true;
        }

        void normalize(std::vector<float> &vec)
        {
            float sum = 0.0f;
            for (float v : vec)
            {
                sum += v * v;
            }

            float norm = std::sqrt(sum);
            if (norm > 0)
            {
                for (float &v : vec)
                {
                    v /= norm;
                }
            }
        }
    };

} // namespace embedding

#endif // EMBEDDING_WORKER_H
