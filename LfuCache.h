#pragma once

#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <climits>
#include <vector>

#include "CacheStrategy.h"

// 最近使用频率高的数据很大概率将会再次被使用, 而最近使用频率低的数据, 将来大概率不会再使用
/**
 * 1.每个节点都有一个访问次数, 新插入的节点访问次数为1
 * 2.相同访问次数的依据时间排序, 后插入的在前面
 * 3.当需要淘汰数据时, 会从尾部, 也就是访问次数从小到大, 开始淘汰
*/

/**
 * 基础的 LFU 还有很多待优化点：
 * 1. 频率爆炸问题: 对于长期驻留在缓存中的热数据, 频率计数可能会无限增长, 占用额外的存储空间或导致计数溢出
 * 2. 过时热点数据占用缓存: 一些数据可能已经不再是热点数据, 但因访问频率过高, 难以被替换
 * 3. 冷启动问题: 刚加入缓存的项可能因为频率为 1 而很快被淘汰, 即便这些项是近期访问的热门数据
 * 4. 不适合短期热点: LFU对长期热点数据表现较好, 但对短期热点数据响应较慢, 可能导致短期热点数据无法及时缓存
 * 5. 缺乏动态适应性: 固定的LFU策略难以适应不同的应用场景或工作负载
 * 6. 锁的粒度大, 多线程高并发访问下锁的同步等待时间过长
 * 针对上述问题, 优化LFU算法
 * 1. 加上最大平均访问次数限制
 * 2. HashLfuCache
*/

// 最大平均访问次数限制
/**
 * 引入访问次数平均值概念
 * 当平均值大于最大平均值限制时将所有结点的访问次数减去最大平均值限制的一半或者一个固定值
 * 相当于热点数据“老化”了, 这样可以避免频次计数溢出, 也可以缓解缓存污染
*/

// LFU算法中不同频率有不同频率的列表: 频率为1有维护频率为1的列表, 频率为2有维护频率为2的链表 
namespace Cache {
// 使用前声明
template<typename Key, typename Value> class LfuCache;

template<typename Key, typename Value>
class FreqList {
private:
    struct Node {
        int freq;   // 访问频次, 一个列表维护一个频次
        Key key;
        Value value;
        std::shared_ptr<Node> prev;     // 前一个节点
        std::shared_ptr<Node> next;     // 后一个节点

        Node(): freq(1), prev(nullptr), next(nullptr) {}
        Node(Key key, Value value): key(key), value(value), freq(1), prev(nullptr), next(nullptr) {}
    };

    using NodePtr = std::shared_ptr<Node>;
    int freq_;      // 访问频率
    NodePtr head_;  // 虚拟头节点
    NodePtr tail_;  // 虚拟尾节点

public:
    explicit FreqList(int n): freq_(n) {
        head_ = std::make_shared<Node>();
        tail_ = std::make_shared<Node>();
        head_->next = tail_;
        tail_->prev = head_; 
    }

    bool isEmpty() const {
        return head_->next == tail_;
    }

    // 添加节点到尾部的方法, 于是head_->next的节点是最不常访问的节点
    void addNode(NodePtr node) {
        if (!node || !head_ || !tail_) return;
        node->prev = tail_->prev;
        tail_->prev = node;
        node->prev->next = node;
        node->next = tail_;
    }

    // 删除表中节点的方法
    void removeNode(NodePtr node) {
        if (!node || !head_ || !tail_) return;
        if (!node->next || !node->prev) return;

        node->prev->next = node->next;
        node->next->prev = node->prev;

        node->prev = nullptr;
        node->next = nullptr;
    }

    NodePtr getFirstNode() const {
        return head_->next;
    }

    friend class LfuCache<Key, Value>;
};

template<typename Key, typename Value>
class LfuCache: public CacheStrategy<Key, Value> {
public:
    using Node = typename FreqList<Key, Value>::Node;
    using NodePtr = std::shared_ptr<Node>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    LfuCache(int capacity, int maxAverageNum = 10)
        : capacity_(capacity), minFreq_(INT_MAX), maxAverageNum_(maxAverageNum)
        , curAverageNum_(0), curTotalNum_(0)
    {}

    ~LfuCache() override = default;

    // 在LFU中, put()和get()都会增加缓存已有节点的频次并将其移动到新的频次列表
    void put(Key key, Value value) override {
        if (capacity_ == 0) return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end()) {
            // 在缓存中更改其value
            it->second->value = value;
            // 用getInternal()增加node的频率并移动到新的频率列表
            getInternal(it->second, value);
        }
        else {
            // 不在缓存中就加入
            putInternal(key, value);
        }
    }

    // value值为传出参数
    bool get(Key key, Value& value) override {
        bool flag = false;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end()) {
            getInternal(it->second, value);
            flag = true;
        }
        return flag;
    }

    Value get(Key key) override {
        Value value;
        get(key, value);
        return value;
    }

    // 清空缓存, 回收资源
    void purge() {
        nodeMap_.clear();
        freqToFreqList_.clear();
    }
          
private:
    void putInternal(Key key, Value value);             // 添加缓存
    void getInternal(NodePtr node, Value& value);       // 获取缓存

    void kickOut();                                     // 移除缓存中的过期数据

    void removeFromFreqList(NodePtr node);              // 从频率列表中移除节点
    void addToFreqList(NodePtr node);                   // 添加到频率列表
    
    void addFreqNum();                                  // 添加平均访问频率
    void decreaseFreqNum(int num);                      // 减少平均访问频率
    void handleOverMaxAverageNum();                     // 处理当前平均访问频率超过上限的情况
    void updateMinFreq();

private:
    int capacity_;                                                      // 缓存容量
    int minFreq_;                                                       // 最小访问频次(用于找到最小访问频次结点)
    int maxAverageNum_;                                                 // 最大平均访问频次
    int curAverageNum_;                                                 // 当前平均访问频次
    int curTotalNum_;                                                   // 当前访问所有缓存次数总数
    std::mutex mutex_;                                                  // 互斥锁
    NodeMap nodeMap_;                                                   // key 到 缓存节点的映射
    std::unordered_map<int, FreqList<Key, Value>*> freqToFreqList_;      // 访问频次搭配频次链表的映射
};

template<typename Key, typename Value>
void LfuCache<Key, Value>::getInternal(NodePtr node, Value& value) {
    // 找到之后需要将其从低访问频次链表移动到 +1 的访问频次链表中
    // 访问频次+1, 然后返回value值
    value = node->value;
    // 从原有访问频次链表中删除节点
    removeFromFreqList(node);
    node->freq++;
    addToFreqList(node);
    // 如果当前node的访问频次等于minFreq + 1, 并且其前驱节点为空
    // 则说明freqToFreqList_[node->freq - 1]的频次链表已经没有节点, 需要更新最小访问频次
    if (node->freq - 1 == minFreq_ && freqToFreqList_[node->freq - 1]->isEmpty()) {
        minFreq_++;
    }

    // 总访问频次和当前平均访问频次也增加
    addFreqNum();
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::putInternal(Key key, Value value) {
    // 如果不在缓存中, 则需要判断缓存是否已满
    if (nodeMap_.size() == capacity_) {
        // 缓存已满, 删除最小频次列表的最不常访问节点, 并更新当前平均访问频次和总访问频次
        kickOut();
    }

    // 创建新节点, 添加新节点, 更新最小访问频次
    NodePtr node = std::make_shared<Node>(key, value);
    // 加入 key -> node 映射
    nodeMap_[key] = node;
    addToFreqList(node);
    addFreqNum();
    minFreq_ = std::min(minFreq_, 1);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::kickOut() {
    // 删掉最小访问频次列表的最不常访问节点
    NodePtr node = freqToFreqList_[minFreq_]->getFirstNode();
    removeFromFreqList(node);
    // 清空这个节点的映射
    nodeMap_.erase(node->key);
    decreaseFreqNum(node->freq);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::removeFromFreqList(NodePtr node) {
    // 检查节点是否为空
    if (node == nullptr) return;
    // 从频次链表中删掉一个节点
    auto freq = node->freq;
    freqToFreqList_[freq]->removeNode(node);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::addToFreqList(NodePtr node) {
    // 检查节点是否为空
    if (node == nullptr) return;
    
    // 添加进入相应的频次链表前需要判断该频次链表是否存在
    auto freq = node->freq;
    if (freqToFreqList_.find(node->freq) == freqToFreqList_.end()) {
        //不存在则创建链表
        freqToFreqList_[node->freq] = new FreqList<Key, Value>(node->freq);
    }

    freqToFreqList_[freq]->addNode(node);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::addFreqNum() {
    // 每次curTotalNum + 1, 表示又有一个节点被访问了
    curTotalNum_++;
    if (nodeMap_.empty()) {
        curAverageNum_ = 0;
    }
    else {
        curAverageNum_ = curTotalNum_ / nodeMap_.size();
    }

    if (curAverageNum_ > maxAverageNum_) {
        handleOverMaxAverageNum();
    }
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::decreaseFreqNum(int num) {
    // 最小频次列表里最不常访问的节点被踢掉了, curTotalNum要减去它的频次
    // 减少平均访问频次和总访问频次
    curTotalNum_ -= num;
    if (nodeMap_.empty()) {
        curAverageNum_ = 0;
    }
    else {
        curAverageNum_ -= curTotalNum_ / nodeMap_.size();
    }
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::handleOverMaxAverageNum() {
    if (nodeMap_.empty()) return;

    // 当前平均访问频次已经超过了最大平均访问频次, 所有节点的访问频次 - (maxAverage_ / 2)
    for (auto it = nodeMap_.begin(); it != nodeMap_.end(); it++) {
        // 检查节点是否为空
        if (it->second == nullptr) continue;

        NodePtr node = it->second;

        // 先从当前频率列表中移除
        removeFromFreqList(node);

        // 减少频率
        node->freq -= maxAverageNum_ / 2;
        if (node->freq < 1) node->freq = 1;

        // 添加到新的频率列表
        addToFreqList(node);
    }
    // 处理完最大访问频次溢出后根据频次列表更新最小访问频次
    updateMinFreq();
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::updateMinFreq() {
    minFreq_ = INT8_MAX;
    // 根据频次列表来更新最小访问频次
    for (const auto& pair: freqToFreqList_) {
        if (pair.second && !pair.second->isEmpty()) {
            minFreq_ = std::min(minFreq_, pair.first);
        }
    }
    if (minFreq_ == INT8_MAX) minFreq_ = 1;
}

/**
 * 设置最大平均访问次数的值解决了什么问题
 * 1. 防止某一个缓存的访问频次无限增加, 而导致的计数溢出
 * 2. 旧的热点缓存, 也就是该数据之前的访问频次很高, 但是现在不再被访问了, 也能够保证他在每次访问缓存平均访问次数大于最大平均访问次数的时候减去一个固定的值, 使这个过去的热点缓存的访问频次逐步降到最低, 然后从内存中淘汰出去
 * 3. 一定程度上是对新加入进来的缓存, 也就是访问频次为1的数据缓存进行了保护, 因为长时间没被访问的旧的数据不再会长期占据缓存空间, 访问频率会逐步被降为小于1最终淘汰
*/


// 对缓存空间切片, 实现hashLFU
template<typename Key, typename Value>
class HashLfuCache {
public:
    HashLfuCache(size_t capacity, int sliceNum, int maxAverageNum = 10) 
        : sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())
        , capacity_(capacity) {
        size_t sliceSize = std::ceil(capacity_ / static_cast<double>(sliceNum_));     // 切片容量
        for (int i = 0; i < sliceNum; i++) {
            lfuSliceCaches_.emplace_back(new LfuCache<Key, Value>(sliceSize, maxAverageNum));
        }
    }

    void put(Key key, Value value) {
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lfuSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(Key key) {
        Value value;
        get(key, value);
        return value;
    }

    // 清除缓存
    void purge() {
        for (auto& lfuSliceCache: lfuSliceCaches_) {
            lfuSliceCache.purge();
        }
    }

private:
    // hash映射
    size_t Hash(Key key) {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }

private:
    size_t capacity_;       // 缓存总容量
    int sliceNum_;          // 切片数量
    std::vector<std::unique_ptr<LfuCache<Key, Value>>> lfuSliceCaches_; //保存切片的容器
};

} // namespace Cache