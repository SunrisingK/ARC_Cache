#pragma once

#include <cstring>
#include <thread>
#include <cmath>
#include <list>
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>

#include "CacheStrategy.h"

namespace Cache {

template<typename Key, typename Value> class LruCache;

template<typename Key, typename Value>
class LruNode {
private:
    Key key_;
    Value value_;
    size_t accessCount_; //访问次数
    std::shared_ptr<LruNode<Key, Value>> prev_;
    std::shared_ptr<LruNode<Key, Value>> next_;

public:
    LruNode(Key key, Value value)
        : key_(key)
        , value_(value)
        , accessCount_(1)
        , prev_(nullptr)
        , next_(nullptr)
    {}

    // 提供必要的访问器
    Key getKey() const { 
        return key_; 
    }
    Value getValue() const { 
        return  value_; 
    }
    void setValue(const Value& value) { 
        value_ = value; 
    }
    size_t getAccessCount() const { 
        return accessCount_; 
    }
    void incrementAccessCount() { 
        accessCount_++; 
    }

    friend class LruCache<Key, Value>;
};


template<typename Key, typename Value>
class LruCache : public CacheStrategy<Key, Value> {
    public:
    using LruNodeType = LruNode<Key, Value>;
    using NodePtr = std::shared_ptr<LruNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    LruCache(int capacity): capacity_(capacity) {
        initializeList();
    }

    ~LruCache() override = default;

    // 在LRU中, put()和get()都会把节点移到到最常访问的位置
    // 添加缓存
    void put(Key key, Value value) override {
        if (capacity_ <= 0) return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end()) {
            // 如果key在当前容器中则更新value, 并调用get()方法, 代表该数据刚被访问
            updateExistingNode(it->second, value);
        }
        else {
            addNewNode(key, value);
        }
        return;
    }
    
    bool get(Key key, Value& value) override {
        std:: lock_guard<std::mutex> lock(mutex_);     
        auto it = nodeMap_.find(key);
        bool flag = false;
        if (it != nodeMap_.end()) {
            moveToMostRecent(it->second);
            value = it->second->getValue();
            flag = true;
        }
        return flag;
    }

    Value get(Key key) override {
        Value value{};
        // memset(& value, 0, sizeof(value)); memset是按字节设置内存的，对于复杂类型（如 string）使用 memset 可能会破坏对象的内部结构
        get(key, value);
        return value;
    }

    // 删除指定元素
    void remove(Key key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end()) {
            removeNode(it->second);
            nodeMap_.erase(it);
        }
    }

private:
    int capacity_;          //缓存容量
    NodeMap nodeMap_;       // key -> node
    std::mutex mutex_;
    NodePtr dummyHead_;     // 虚拟头节点
    NodePtr dummyTail_;

private:
    void initializeList() {
        // 创建首尾虚拟节点
        dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_;
    }

    void updateExistingNode(NodePtr node, const Value& value) {
        node->setValue(value);
        moveToMostRecent(node);
    }

    void addNewNode(const Key& key, const Value& value) {
        if (nodeMap_.size() >= capacity_) {
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        nodeMap_[key] = newNode;
    }

    // 移动节点到最新位置
    void moveToMostRecent(NodePtr node) {
        removeNode(node);
        insertNode(node);
    }

    void removeNode(NodePtr node) {
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;
    }

    // 从尾部插入节点
    void insertNode(NodePtr node) {
        node->next_ = dummyTail_;
        dummyTail_->prev_->next_ = node;
        node->prev_ = dummyTail_->prev_;
        dummyHead_->prev_ = node;
    }

    // 去除最近最少访问节点
    void evictLeastRecent() {
        NodePtr leastRecent = dummyHead_->next_;
        removeNode(leastRecent);
        nodeMap_.erase(leastRecent->getKey());
    }
};


// LRU优化: LRU-K | 通过继承的方式再优化
/**
 * LRU-k 算法是对 LRU 算法的改进
 * 基础的 LRU 算法被访问数据进入缓存队列只需要访问(put, get)一次就行
 * 但是现在需要被访问 k 次才能被放入缓存中，基础的LRU算法可以看成是LRU-1
 * 常见的k=2
 * 访问历史队列中的数据也不是一直保留的, 也是需要按照LRU的规则进行淘汰
*/
template<typename Key, typename Value>
class LruKCache: public CacheStrategy<Key, Value> {
public:
    LruKCache(int capacity, int historyCapacity, int k)
        : LruCache<Key, Value>(capacity) // 调整基类构造
        , historyList_(std::make_unique<LruCache<Key, size_t>>(historyCapacity))
        , k_(k)
    {}

    Value get(Key key) {
        // 获取该数据访问次数
        int historyCount = historyList_->get(key);
        // 如果访问到数据, 则更新历史访问记录节点值 count++
        historyList_->put(key, ++historyCount);

        // 从缓存中获取数据, 不一定能获取到, 因为可能不在缓存中
        return LruCache<Key, Value>::get(key);
    }

    void put(Key key, Value value) {
        // 先判断是否存在与缓存中, 如果存在则直接覆盖, 如果不存在则不直接添加到缓存
        if (LruCache<Key, Value>::get(key) != "") {
            LruCache<Key, Value>::put(key, value);
        }

        // 如果数据历史访问次数达到k次, 则加入到缓存
        int historyCount = historyList_->get(key);
        historyList_->put(key, ++historyCount);

        if (historyCount >= k_) {
            // 移除历史记录
            historyList_->remove(key);
            // 添加到缓存中
            LruCache<Key, Value>::put(key, value);
        }
    }
private:
    int k_;                             // 进入缓存队列的评判标准(>= k_)
    std::unique_ptr<LruCache<Key, size_t>> historyList_; // 访问数据历史记录(value为访问次数)
};


/**
 * 当多个线程同时访问一个LRU/LFU时, 由于锁的粒度大, 会造成长时间的同步等待
 * 如果是多个线程同时访问多个LRU/LFU缓存，同步等待时间将大大减少 
*/
// LRU优化: 对LRU进行分片, 提高高并发使用的性能
template<typename Key, typename Value>
class HashLruCaches {
public:
    HashLruCaches(size_t capacity, int sliceNum)
        : capacity_(capacity)
        , sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency()) 
    {
        size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_));
        // 获取每个分片的大小
        for (int i = 0; i < sliceNum; i++) {
            lruSliceCaches_.emplace_back(new LruCache<Key, Value>(sliceSize));
        }
    }

    void put(Key key, Value value) {
        // 获取key的hash值, 并计算出对应的分片索引
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCaches_[sliceIndex]->put(key, value);
    }

    bool get(Key key, Value& value) {
        // 获取key的hash值, 并计算出对应的分片索引
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(Key key) {
        Value value;
        memset(&value, 0, sizeof(value));
        get(key, value);
        return value;
    }
private:
    size_t capacity_;               // 缓存总容量
    int sliceNum_;                  // 切片数量
    std::vector<std::unique_ptr<LruCache<Key, Value>>> lruSliceCaches_;     // 切片LRU缓存

private:
    // 将key转换为对应的hash值
    size_t Hash(Key key) {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }
};

}