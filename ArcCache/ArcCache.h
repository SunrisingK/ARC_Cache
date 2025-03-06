/**
 * ARC (Adapative Replacement Cache)
 * 核心思想:
 * 1. 当访问的数据趋向于访问最近的内容, 会更多地命中LRU list, 增大LRU的空间
 * 2. 当系统趋向于访问频繁访问的内容时, 会更多地命中LFU list, 增加LFU的空间
 * 应用场景
 * 主要应用于不同的访问模式(比如近期频繁访问和周期性访问交叉的场景), 可以动态调整缓存分区的大小
*/
/**
 * ARC特点
 * 1. 自适应性
 *  · ARC动态调整2个缓存区域的大小: T1 (最近使用的元素) T2 (频繁访问的元素)
 *  · 根据访问模式自动调整策略, 适应不同的工作负载
 * 2. 缓存分区
 *  · LRU-like区域: 处理新进入的访问数据 (类似最近最少使用)
 *  · LFU-like区域: 处理频繁访问的数据
 * 3. 避免缓存污染
 *  · ARC在混合不同访问模式的情况下表现良好, 它可以动态调整缓存分区的大小
 * 4. 命中率高
 *  · 在混合访问模式下, ARC通常能够提供比单一策略更高的缓存命中率
 * ARC优点
 * 1. 自适应强, 适合工作负载模式变化频繁的场景
 * 2. 比单纯的LRU或LFU更能处理缓存污染问题
 * ARC缺点
 * 1. 实现复杂性较高, 维护多种数据结构 (如双链表和哈希表), 可能增加CPU开销
 * 2. 在工作负载高度一致 (如完全频繁访问)情况下优势不明显
*/
/**
 * ARC动态适应工作负载
 * · LRU 和 LFU 在固定策略下, 难以同时适应短期热点和长期热点数据
 * · ARC动态调整 partition 分割指针的位置, 能够同时处理短期热点和长期热点数据
*/

#pragma once

#include "../CacheStrategy.h"
#include "ArcLruPart.h"
#include "ArcLfuPart.h"
#include <memory>

namespace Cache {

template<typename Key, typename Value>
class ArcCache : public CacheStrategy<Key, Value> {
public:
    explicit ArcCache(size_t capacity = 10, size_t transformThreshold = 3) 
        : capacity_(capacity)
        , transformThreshold_(transformThreshold)
        , lruPart_(std::make_unique<ArcLruPart<Key, Value>>(capacity, transformThreshold))
        , lfuPart_(std::make_unique<ArcLfuPart<Key, Value>>(capacity, transformThreshold))
    {}

    ~ArcCache() override = default;

    // void put(Key key, Value value) override {
    //     bool inGhost = checkGhostCaches(key);
    //     if (inGhost == false) {
    //         if (lruPart_->put(key, value)) {
    //             lfuPart_->put(key, value);
    //         }
    //     }
    //     else{
    //         lruPart_->put(key, value);
    //     }
    // }
    
    /* put()不增加LRU节点的访问次数, 增加LFU节点的访问次数 */
    void put(Key key, Value value) override {
        // ghost缓存里没有该key就添加到缓存列表里
        bool inGhost = checkGhostCaches(key);
        if (inGhost == false) {
            if (lfuPart_->inLfuMainCache(key)) {
                // key 在 LFU 缓存里
                lfuPart_->put(key, value);
            }
            else {
                // key 在 LRU 缓存里
                lruPart_->put(key, value);
            }
        }
        else {
            lruPart_->put(key, value);
        }
    }

    // get()增加LRU节点和LFU节点的访问次数
    bool get(Key key, Value& value) override {
        // 节点在ghost缓存中就移出该节点并调整LRU和LFU的大小
        // 不在ghost缓存中什么也不做
        checkGhostCaches(key); 
        bool shouldTransform = false;
        if (lruPart_->get(key, value, shouldTransform)) {
            // 如果节点在LRU部分缓存中 并且访问次数达标, 就把该节点复制到LFU列表中
            if (shouldTransform) lfuPart_->put(key, value); 
            return true;
        }
        // 节点不在LRU部分缓存中, 就去LFU部分缓存中去找
        return lfuPart_->get(key, value);
    }

    Value get(Key key) override {
        Value value{};
        get(key, value);
        return value;
    }

private:
    bool checkGhostCaches(Key key) {
        // 节点在ghost缓存中就移出该节点(移出节点的操作由checkGhost()方法完成)并调整LRU和LFU的大小
        bool inGhost = false;
        if (lruPart_->checkGhost(key)) {
            // 节点在LRU的ghost缓存中
            if (lfuPart_->decreaseCapacity()) {
                // 减少LFU部分的缓存大小并增加LRU部分的缓存大小
                lruPart_->increaseCapacity();
            }
            inGhost = true;
        }
        else if (lfuPart_->checkGhost(key)) {
            // 节点在LFU的ghost缓存中
            if (lruPart_->decreaseCapacity()) {
                // 减少LRU部分的缓存大小并增加LFU部分的缓存大小
                lfuPart_->increaseCapacity();
            }
            inGhost = true;
        }
        return inGhost;
    }


private: 
    size_t capacity_;
    size_t transformThreshold_;
    std::unique_ptr<ArcLruPart<Key, Value>> lruPart_;
    std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart_;
};

} // namespace Cache