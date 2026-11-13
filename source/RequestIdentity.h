#pragma once
#include <array>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace quota::request_detail {
// 常见小写 UUID 可无损压成 16 字节；大写及非 UUID 保持原字节和大小写语义。
inline bool PackUuid(std::string_view text, unsigned char* output) {
    if (text.size() != 36) return false;
    size_t nibble = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
            continue;
        }
        const int hex = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (hex < 0) return false;
        if (nibble % 2) output[nibble / 2] |= static_cast<unsigned char>(hex);
        else output[nibble / 2] = static_cast<unsigned char>(hex << 4);
        ++nibble;
    }
    return true;
}
using TurnIdentity = std::variant<std::array<unsigned char, 16>, std::string>;
using RequestIdentity = std::variant<std::array<unsigned char, 32>, std::string>;
inline TurnIdentity TurnKey(const std::string& turn) {
    std::array<unsigned char, 16> packed;
    if (PackUuid(turn, packed.data())) return packed;
    return turn;
}
inline RequestIdentity RequestKey(const std::string& thread, const std::string& turn) {
    std::array<unsigned char, 32> packed;
    if (PackUuid(thread, packed.data()) && PackUuid(turn, packed.data() + 16)) return packed;
    return thread + '\n' + turn;
}

template<class Key, size_t Limit>
class RecentIdentities {
    std::set<Key> values_;
    // 仅在首次插入时分配淘汰队列；set 节点稳定，队列不复制字符串。
    std::optional<std::deque<typename std::set<Key>::const_iterator>> order_;
public:
    RecentIdentities() = default;
    RecentIdentities(const RecentIdentities&) = delete;
    RecentIdentities& operator=(const RecentIdentities&) = delete;
    bool contains(const Key& key) const { return values_.find(key) != values_.end(); }
    bool insert(Key key) {
        auto [at, added] = values_.insert(std::move(key));
        if (!added) return false;
        try {
            if (!order_) order_.emplace();
            order_->push_back(at);
        }
        catch (...) { values_.erase(at); throw; }
        if (order_->size() > Limit) { values_.erase(order_->front()); order_->pop_front(); }
        return true;
    }
    void clear() { order_.reset(); values_.clear(); }
};
}
