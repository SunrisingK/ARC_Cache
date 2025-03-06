/**
 * ARC解决了 LFU 的循环缓存问题
 * · 在 LFU 中, 新加入的缓存项起初频率低, 可能在尚未证明其重要性时就被淘汰
 * · ARC保留了一个专门存储最近访问但被淘汰的数据队列 (ghost list), 帮助识别新数据的价值,
 *   如果某个新数据被多次访问, 可以快速将其提升为频繁访问的数据
 * LFU有一个ghost list淘汰链表, 存储从LLFU中淘汰的数据
*/

#pragma once

#include "ArcCacheNode.h"
#include <unordered_map>
#include <map>
#include <mutex>
#include <list>


namespace Cache {

template<typename Key, typename Value>
class ArcLfuPart {
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;
    using FreqMap = std::map<size_t, std::list<NodePtr>>;   // 频率 -> 频率列表的有序映射

    explicit ArcLfuPart(size_t capacity, size_t transformThreshold)
        : capacity_(capacity)
        , ghostCapacity_(capacity)
        , transformThreshold_(transformThreshold)
        , minFreq_(0)
    {
        initializeLists();
    }

    bool put(Key key, Value value) {
        if (capacity_ == 0) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end()) {
            updateExistingNode(it->second, value);
        }
        else {
            addNewNode(key, value);
        }
        return true;
    }

    bool get(Key key, Value& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool flag = false;
        auto it = mainCache_.find(key);
        if (it != mainCache_.end()) {
            updateNodeFrequency(it->second);
            value = it->second->getValue();
            flag = true;
        }
        return flag;
    }

    bool inLfuMainCache(Key key) {
        return mainCache_.find(key) != mainCache_.end();
    }

    bool checkGhost(Key key) {
        bool flag = false;
        auto it = ghostCache_.find(key);
        if (it != ghostCache_.end()) {
            removeFromGhost(it->second);
            ghostCache_.erase(it);
            flag = true;
        }
        return flag;
    }

    void increaseCapacity() {
        capacity_++;
    }

    bool decreaseCapacity() {
        if (capacity_ <= 0) return true;
        if (mainCache_.size() == capacity_) {
            evictLeastFrequent();
        }
        capacity_--;
        return true;
    }

private:
    // arcLfuPart只有一个ghost列表 
    void initializeLists() {
        ghostHead_ = std::make_shared<NodeType>();
        ghostTail_ = std::make_shared<NodeType>();
        ghostHead_->next_ = ghostTail_;
        ghostTail_->prev_ = ghostHead_;
    }

    void updateExistingNode(NodePtr node, const Value& value) {
        node->setValue(value);
        updateNodeFrequency(node);
    }

    void addNewNode(const Key& key, const Value& value) {
        if (mainCache_.size() >= capacity_) {
            evictLeastFrequent();
        }

        NodePtr newNode = std::make_shared<NodeType>(key, value);
        mainCache_[key] = newNode;
        
        // 将新节点加入到频率为 1 的频率列表里
        if (freqMap_.find(1) == freqMap_.end()) {
            freqMap_[1] = std::list<NodePtr>();
        }
        freqMap_[1].emplace_back(newNode);
        minFreq_ = 1;
    }

    void updateNodeFrequency(NodePtr node) {
        size_t oldFreq = node->getAccessCount();
        node->increaseAccessCount();
        size_t newFreq = node->getAccessCount();

        // 从旧的频率列表中移除
        auto& oldList = freqMap_[oldFreq];
        oldList.remove(node);
        if (oldList.empty()) {
            freqMap_.erase(oldFreq);
            if (oldFreq == minFreq_) {
                minFreq_ = newFreq;
            }
        }

        // 添加到新的频率列表 如果不存在就创建一个新的频率列表
        if (freqMap_.find(newFreq) == freqMap_.end()) {
            freqMap_[newFreq] = std::list<NodePtr>();
        }
        freqMap_[newFreq].emplace_back(node);
    }

    void evictLeastFrequent() {
        if (freqMap_.empty()) return;

        // 获取最小频率列表
        auto& minFreqList = freqMap_[minFreq_];
        if (minFreqList.empty()) return;

        // 移除最少使用的节点
        NodePtr leastNode = minFreqList.front();
        minFreqList.pop_front();

        // 如果该频率列表为空, 则删除该频率映射
        if (minFreqList.empty()) {
            freqMap_.erase(minFreq_);
            // 更新最小频率
            if (!freqMap_.empty()) {
                minFreq_ = freqMap_.begin()->first;
            }
        }

        // 将节点移动到ghostCache
        if (ghostCache_.size() >= ghostCapacity_) {
            removeOldestGhost();
        }
        addToGhost(leastNode);

        // 从主缓存中移除
        mainCache_.erase(leastNode->getKey());
    }

    void removeFromGhost(NodePtr node) {
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;    
    }

    // 尾插, 新的节点加入到尾部
    void addToGhost(NodePtr node) {
        // 重置节点的访问次数
        node->accessCount_ = 1;

        node->next_ = ghostTail_;
        ghostTail_->prev_->next_ = node;
        node->prev_ = ghostTail_->prev_;
        ghostTail_->prev_ = node;
        
        ghostCache_[node->getKey()] = node;
    }

    void removeOldestGhost() {
        NodePtr oldestGhost = ghostHead_->next_;
        if (oldestGhost != ghostTail_) {
            removeFromGhost(oldestGhost);
            ghostCache_.erase(oldestGhost->getKey());
        }
    }

private:
    size_t capacity_;
    size_t ghostCapacity_;
    size_t transformThreshold_;
    size_t minFreq_;
    std::mutex mutex_;

    NodeMap mainCache_;
    NodeMap ghostCache_;
    FreqMap freqMap_;

    NodePtr ghostHead_;
    NodePtr ghostTail_;
};

} // namespace Cache