#pragma once

#include <memory>

namespace Cache {

template<typename Key, typename Value>
class ArcNode {
private:
    Key key_;
    Value value_;
    size_t accessCount_;        // 访问次数
    std::shared_ptr<ArcNode> prev_;
    std::shared_ptr<ArcNode> next_;

public:
    ArcNode(): accessCount_(1), prev_(nullptr), next_(nullptr) {}

    ArcNode(Key key, Value value)
        : key_(key)
        , value_(value)
        , accessCount_(1)
        , prev_(nullptr)
        , next_(nullptr)
    {}

    // Get()
    Key getKey() const {
        return key_;
    }
    Value getValue() const {
        return value_;
    }
    size_t getAccessCount() const {
        return accessCount_;
    }

    // Set()
    void setValue(const Value& value) {
        value_ = value;
    }
    void increaseAccessCount() {
        accessCount_++;
    }

    template<typename K, typename V> friend class ArcLruPart;
    template<typename K, typename V> friend class ArcLfuPart;
};

} // namespace Cache