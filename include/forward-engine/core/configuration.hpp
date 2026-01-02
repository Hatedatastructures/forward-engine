#pragma once

#include <string>
#include <stdexcept>
#include <filesystem>
#include <boost/json.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <abnormal.hpp>

namespace ngx::core
{
    class configuration
    {
    public:
        configuration() = default;

        /**
         * @brief 加载配置文件
         * @param file_path 配置文件路径
         */
        void load(const std::string& file_path)
        {
            if (!std::filesystem::exists(file_path))
            {
                throw abnormal::security("Configuration file not found:{} ",file_path);
            }
            try
            {
                boost::property_tree::read_json(file_path, root_);
            }
            catch (const std::exception& e)
            {
                throw abnormal::security(std::string("Failed to parse configuration: ") + e.what());
            }
        }

        /**
         * @brief 获取根 JSON 对象
         */
        [[nodiscard]] const boost::property_tree::ptree& data() const
        {
            return root_;
        }

    private:
        boost::property_tree::ptree root_;
    };
}