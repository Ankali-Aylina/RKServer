#ifndef BERT_TOKENIZER_H
#define BERT_TOKENIZER_H

/**
 * @file bert_tokenizer.h
 * @brief BERT 分词器（头文件内联实现）
 *
 * 实现 BERT 模型使用的 WordPiece 分词算法的简化版本。
 * 从词表文件（vocab.txt）加载词汇，将文本转换为 token ID 序列。
 *
 * 特殊 Token：
 * - [CLS] (ID: 101): 分类 token，放在序列开头
 * - [SEP] (ID: 102): 分隔 token，放在序列末尾
 * - [PAD] (ID: 0): 填充 token，用于对齐序列长度
 * - [UNK] (ID: 100): 未知 token，用于未登录词
 *
 * @namespace embedding
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include "logger.h"

namespace embedding
{

    /**
     * @brief BERT 分词器
     *
     * 实现简化的 WordPiece 分词算法。
     * 使用空格分词 + 字符级回退。
     */
    class BertTokenizer
    {
    public:
        BertTokenizer()
            : unk_id_(100), cls_id_(101), sep_id_(102), pad_id_(0) {}

        /**
         * @brief 从词表文件加载词汇
         * @param vocab_path 词表文件路径（通常为 vocab.txt）
         * @return 加载成功返回 true
         */
        bool loadVocab(const std::string &vocab_path)
        {
            std::ifstream file(vocab_path);
            if (!file.is_open())
            {
                LOG_ERROR("Failed to open vocab file: " + vocab_path);
                return false;
            }

            std::string line;
            int id = 0;
            while (std::getline(file, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                word_to_id_[line] = id++;
            }

            updateSpecialTokenIds();

            LOG_INFO("Vocabulary loaded from: " + vocab_path + ", size: " + std::to_string(word_to_id_.size()));
            return true;
        }

        /**
         * @brief 对文本进行分词
         * @param text 输入文本
         * @param max_len 最大序列长度（包含特殊 token）
         * @return token ID 序列，长度为 max_len
         */
        std::vector<int> tokenize(const std::string &text, int max_len)
        {
            std::vector<int> tokens;
            tokens.reserve(max_len);

            tokens.push_back(cls_id_);

            std::istringstream iss(text);
            std::string word;

            while (iss >> word)
            {
                if (tokens.size() >= static_cast<size_t>(max_len - 1))
                {
                    break;
                }

                auto it = word_to_id_.find(word);
                if (it != word_to_id_.end())
                {
                    tokens.push_back(it->second);
                }
                else
                {
                    tokenizeUnknownWord(word, tokens, max_len);
                }
            }

            while (tokens.size() < static_cast<size_t>(max_len))
            {
                tokens.push_back(pad_id_);
            }

            if (tokens.size() > static_cast<size_t>(max_len))
            {
                tokens.resize(max_len - 1);
            }
            tokens.back() = sep_id_;

            return tokens;
        }

        size_t getVocabSize() const
        {
            return word_to_id_.size();
        }

        int getUnkId() const { return unk_id_; }
        int getClsId() const { return cls_id_; }
        int getSepId() const { return sep_id_; }
        int getPadId() const { return pad_id_; }

    private:
        std::unordered_map<std::string, int> word_to_id_;
        int unk_id_;
        int cls_id_;
        int sep_id_;
        int pad_id_;

        void updateSpecialTokenIds()
        {
            if (word_to_id_.count("[UNK]"))
                unk_id_ = word_to_id_["[UNK]"];
            if (word_to_id_.count("[CLS]"))
                cls_id_ = word_to_id_["[CLS]"];
            if (word_to_id_.count("[SEP]"))
                sep_id_ = word_to_id_["[SEP]"];
            if (word_to_id_.count("[PAD]"))
                pad_id_ = word_to_id_["[PAD]"];
        }

        void tokenizeUnknownWord(const std::string &word, std::vector<int> &tokens, int max_len)
        {
            for (char c : word)
            {
                if (tokens.size() >= static_cast<size_t>(max_len - 1))
                {
                    break;
                }
                std::string s(1, c);
                auto cit = word_to_id_.find(s);
                tokens.push_back(cit != word_to_id_.end() ? cit->second : unk_id_);
            }
        }
    };

} // namespace embedding

#endif // BERT_TOKENIZER_H
