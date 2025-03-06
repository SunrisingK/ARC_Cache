/**
 * ARC解决了 LRU 的循环缓存问题
 * · 在LRU中, 如果缓存中有热点数据 (频繁访问的少量数据), 但新的数据不断加入, 
 *   可能会导致热点数据被淘汰, 出现缓存抖动 (该被淘汰的数据又要访问)
 * ARC使用2个队列来分别跟踪最近访问 (类似LRU) 和经常访问 (类似LFU) 的数据, 
 *   并根据访问模式动态调整这两部分缓存的大小, 从而避免热点数据过早被淘汰
 * LRU有一个ghost list淘汰链表, 存储从LRU链表中淘汰的数据
*/

#pragma once

#include "ArcCacheNode.h"
#include <unordered_map>
#include <mutex>

namespace Cache {

template<typename Key, typename Value>
class ArcLruPart {
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    explicit ArcLruPart(size_t capacity, size_t transformThreshold)
        : capacity_(capacity)
        , ghostCapacity_(capacity)
        , transformThreshold_(transformThreshold)
    {    
        initlizeLists();
    }

    // 在ARCLru中, put()方法不会会增加节点的访问次数
    bool put(Key key, Value value) {
        if (capacity_ == 0) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end()) {
            // 在主缓存中
            updateExistingNode(it->second, value);
        }
        else {
            // 不在主缓存中
            addNewNode(key, value);
        }
        return true;
    }

    // 在ARCLru中, get()方法会增加一次节点的访问次数
    bool get(Key key, Value& value, bool& shouldTransform) {
        std::lock_guard<std::mutex> lock(mutex_);
        bool flag = false;
        auto it = mainCache_.find(key);
        if (it != mainCache_.end()) {
            shouldTransform = updateNodeAccess(it->second);
            value = it->second->getValue();
            flag = true;
        }
        return flag;
    }

    bool inLruMainCache(Key key) {
        return mainCache_.find(key) != mainCache_.end();
    }

    bool checkGhost(Key key) {
        auto it = ghostCache_.find(key);
        bool flag = false;
        // 在ghost缓存中就把这个节点移出ghost缓存, 并清空它的映射
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
        if (capacity_ <= 0) return false;
        if (mainCache_.size() == capacity_) {
            evictLeastRecent();
        }
        capacity_--;
        return true;
    }

private:
    void initlizeLists() {
        mainHead_ = std::make_shared<NodeType>();
        mainTail_ = std::make_shared<NodeType>();
        mainHead_->next_ = mainTail_;
        mainTail_->prev_ = mainHead_;

        ghostHead_ = std::make_shared<NodeType>();
        ghostTail_ = std::make_shared<NodeType>();
        ghostHead_->next_ = ghostTail_;
        ghostTail_->prev_ = ghostHead_;
    }

    void updateExistingNode(NodePtr node, const Value value) {
        // 改变value
        node->setValue(value);
        // 移到链表头表示刚刚访问
        moveToFront(node);
    }

    void addNewNode(const Key& key, const Value& value) {
        if (mainCache_.size() >= capacity_) {
            // 主缓存已满则驱逐最近最少访问
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<NodeType>(key, value);
        // 加入到主缓存映射中
        mainCache_[key] = newNode;
        addToFront(newNode);
    }

    bool updateNodeAccess(NodePtr node) {
        moveToFront(node);
        node->increaseAccessCount();
        return node->getAccessCount() >= transformThreshold_;
    }
 
    void moveToFront(NodePtr node) {
        // 先从当前位置移除
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;
        // 添加到头部
        addToFront(node);
    }

    void addToFront(NodePtr node) {
        // 链表头部表示最近访问的节点
        node->next_ = mainHead_->next_;
        mainHead_->next_ = node;
        node->next_->prev_ = node;
        node->prev_ = mainHead_;
    }

    void evictLeastRecent() {
        // 从主缓存链表中驱除最不常访问的节点, 把它加入到ghost列表里
        NodePtr leastRecent = mainTail_->prev_;
        if (leastRecent == mainHead_) return;

        // 从主链表中移除
        removeFromMain(leastRecent);

        // 添加到ghostList, 如果ghost列表满了就淘汰最尾部的节点
        if (ghostCache_.size() >= ghostCapacity_) {
            removeOldestGhost();
        }
        addToGhost(leastRecent);

        // 从主缓存映射中移除
        mainCache_.erase(leastRecent->getKey());
    }

    void removeFromMain(NodePtr node) {
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;
    }

    void removeFromGhost(NodePtr node) {
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;
    }

    void addToGhost(NodePtr node) {
        // 加入到ghost列表需要重置节点的访问计数
        node->accessCount_ = 1;

        // 添加到ghostList的头部
        node->next_ = ghostHead_->next_;
        ghostHead_->next_ = node;
        node->next_->prev_ = node;
        node->prev_ = ghostHead_;

        // 添加到ghost缓存映射
        ghostCache_[node->getKey()] = node;
    }

    void removeOldestGhost() {
        NodePtr oldestGhost = ghostTail_->prev_;

        if (oldestGhost == ghostHead_) return;

        removeFromGhost(oldestGhost);
        ghostCache_.erase(oldestGhost->getKey());
    }

private:
    size_t capacity_;               // 主缓存容量
    size_t ghostCapacity_;          // ghost缓存容量
    size_t transformThreshold_;     // 转换阈值
    std::mutex mutex_;           

    NodeMap mainCache_;
    NodeMap ghostCache_;

    NodePtr mainHead_;
    NodePtr mainTail_;

    NodePtr ghostHead_;
    NodePtr ghostTail_;
};

} // namespace Cache