#pragma once

#include <string>
#include <vector>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <memory_resource>

namespace ngx::memory
{
    /**
     * @brief string 类型别名
     * @details 使用 std::pmr::string 替代 std::string
     * @note `monotonic_buffer_resource` 上分配时，字符串内容紧凑排列，缓存极其友好
     */
    using string = std::pmr::string;

    /**
     * @brief vector
     * @details 使用 std::pmr::vector 替代 std::vector
     * @note `monotonic_buffer_resource` 上分配时，数组元素紧凑排列，缓存极其友好
     */
    template <typename Value>
    using vector = std::pmr::vector<Value>;

    /**
     * @brief list
     * @details 使用 std::pmr::list 替代 std::list
     * @note `monotonic_buffer_resource` 上分配时，链表节点紧凑排列，缓存极其友好
     */
    template <typename Value>
    using list = std::pmr::list<Value>;


    /**
     * @brief unordered_map
     * @details 使用 std::pmr::unordered_map 替代 std::unordered_map
     * @note `monotonic_buffer_resource` 上分配时，哈希表元素紧凑排列，缓存极其友好
     */
    template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
    using unordered_map = std::pmr::unordered_map<Key, Value, Hash, KeyEqual>;

    /**
     * @brief unordered_set
     * @details 使用 std::pmr::unordered_set 替代 std::unordered_set
     * @note `monotonic_buffer_resource` 上分配时，哈希集合元素紧凑排列，缓存极其友好
     */
    template <typename Key, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
    using unordered_set = std::pmr::unordered_set<Key, Hash, KeyEqual>;

} // namespace ngx::memory