#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <iomanip>
#include <random>
#include <algorithm>
#include <array>

#include "CacheStrategy.h"
#include "LruCache.h"
#include "LfuCache.h"
#include "ArcCache/ArcCache.h"

using namespace std;


class Timer {
public:
    Timer(): start_(chrono::high_resolution_clock::now()) {}

    double elapsed() {
        auto now = chrono::high_resolution_clock::now();
        return chrono::duration_cast<chrono::milliseconds>(now - start_).count();
    }

private:
    chrono::time_point<chrono::high_resolution_clock> start_;
};


// 辅助函数打印结果
void printResults(const string& testName, int capacity, const vector<int>& get_operations, const vector<int>& hits) {
    cout << "缓存大小: " << capacity << endl;
    cout << "LRU - 命中率: " << fixed << setprecision(2) << (100.0 * hits[0] / get_operations[0]) << "%" << endl;
    cout << "LFU - 命中率: " << fixed << setprecision(2) << (100.0 * hits[1] / get_operations[1]) << "%" << endl;
    cout << "ARC - 命中率: " << fixed << setprecision(2) << (100.0 * hits[2] / get_operations[2]) << "%" << endl;
}


void testHotDataAccess() {
    cout << "=== 测试场景1: 热点数据访问测试 ===" << endl;
    const int CAPACITY = 50;                    // 缓存容量
    const int OPERATIONS = 500000;              // 操作次数
    const int HOT_KEYS = 20;                    // 热点数据数量
    const int COLD_KEYS = 5000;

    Cache::LruCache<int, string> lru(CAPACITY);
    Cache::LfuCache<int, string> lfu(CAPACITY);
    Cache::ArcCache<int, string> arc(CAPACITY);

    random_device rd;
    mt19937 gen(rd());

    array<Cache::CacheStrategy<int, string>*, 3> caches = {&lru, &lfu, &arc};
    vector<int> hits(3, 0);
    vector<int> get_operations(3, 0);

    // 先进行一系列put操作
    for (int i = 0; i < caches.size(); i++) {
        for (int op = 0; op < OPERATIONS; op++) {
            int key;
            if (op % 100 < 70) {
                // 70% 热点数据
                key = gen() % HOT_KEYS;
            }
            else {
                // 30% 冷数据
                key = HOT_KEYS + (gen() % COLD_KEYS);
            }
            string value = "value" + to_string(key);
            caches[i]->put(key, value);
        }

        // 再进行一系列get操作
        for (int get_op = 0; get_op < OPERATIONS; get_op++) {
            int key;
            if (get_op % 100 < 70) {
                key = gen() % HOT_KEYS;
            }
            else {
                key = HOT_KEYS + (gen() % COLD_KEYS);
            }

            string result;
            get_operations[i]++;
            if (caches[i]->get(key, result)) {
                hits[i]++;
            }
        }    
    }
    printResults("热点数据访问测试", CAPACITY, get_operations, hits);
}


void testLoopPattern() {
    cout << "\n=== 测试场景2: 循环数据访问测试 ===" << endl;
    const int CAPACITY = 50;            // 缓存容量
    const int LOOP_SIZE = 500;
    const int OPERATIONS = 200000;     

    Cache::LruCache<int, string> lru(CAPACITY);
    Cache::LfuCache<int, string> lfu(CAPACITY);
    Cache::ArcCache<int, string> arc(CAPACITY);

    random_device rd;
    mt19937 gen(rd());

    array<Cache::CacheStrategy<int, string>*, 3> caches = {&lru, &lfu, &arc};
    vector<int> hits(3, 0);
    vector<int> get_operations(3, 0);

    // 先填充数据
    for (int i = 0; i < caches.size(); i++) {
        for (int key = 0; key < LOOP_SIZE; key++) {
            // 只填充 LOOP_SIZE 的数据
            string value = "loop" + to_string(key);
            caches[i]->put(key, value);
        }

        // 访问测试
        int current_pos = 0;
        for (int op = 0; op < OPERATIONS; op++) {
            int key;
            if (op % 100 < 60) {
                // 60% 顺序扫描
                key = current_pos;
                current_pos = (current_pos + 1) % LOOP_SIZE;
            }
            else if (op % 100 < 90) {
                // 30% 随机跳跃
                key = gen() % LOOP_SIZE;
            }
            else {
                // 10% 访问范围外数据
                key = LOOP_SIZE + (gen() % LOOP_SIZE);
            }
            string result;
            get_operations[i]++;
            if (caches[i]->get(key, result)) {
                hits[i]++;
            }
        }
    }
    printResults("循环扫描测试", CAPACITY, get_operations, hits);
}


void testWorkloadShift() {
    cout << "\n=== 测试场景3: 工作负载剧烈变化测试 ===" << endl;
    const int CAPACITY = 4;            // 缓存容量
    const int OPERATIONS = 80000;
    const int PHASE_LENGTH = OPERATIONS / 5;

    Cache::LruCache<int, string> lru(CAPACITY);
    Cache::LfuCache<int, string> lfu(CAPACITY);
    Cache::ArcCache<int, string> arc(CAPACITY);

    random_device rd;
    mt19937 gen(rd());

    array<Cache::CacheStrategy<int, string>*, 3> caches = {&lru, &lfu, &arc};
    vector<int> hits(3, 0);
    vector<int> get_operations(3, 0);

    // 填充一些初始数据
    for (int i = 0; i < caches.size(); i++) {
        for (int key = 0; key < 1000; key++) {
            string value = "init" + to_string(key);
            caches[i]->put(key, value);
        }
        // 多阶段测试
        for (int op = 0; op < OPERATIONS; op++) {
            int key;
            // 根据不同阶段选择不同的访问模式
            if (op < PHASE_LENGTH) {
                // 热点访问
                key = gen() % 5;
            }
            else if (op < PHASE_LENGTH * 2) {
                // 大范围随机
                key = gen() % 1000;
            }
            else if (op < PHASE_LENGTH * 3) {
                // 顺序扫描
                key = (op - PHASE_LENGTH * 2) % 100;
            }
            else if (op < PHASE_LENGTH * 4) {
                //局部随机访问
                int locality = (op / 1000) % 10;
                key = locality * 2- + (gen() % 20);
            }
            else {
                // 混合访问
                int r = gen() % 100;
                if (r < 30) {
                    key = gen() % 5;
                }
                else if (r < 60) {
                    key = 5 + (gen() % 95);
                }
                else {
                    key = 100 + (gen() % 900);
                }
            }

            string result;
            get_operations[i]++;
            if (caches[i]->get(key, result)) {
                hits[i]++;
            }

            // 随机进行put操作, 更新缓存内容
            if (gen() % 100 < 30) {
                // 30% 概率进行put
                string value = "new" + to_string(key);
                caches[i]->put(key, value);
            }
        }
    }
    printResults("工作负载剧烈变化测试", CAPACITY, get_operations, hits);
}


int main(int argc, char const* argv[]) {
    testHotDataAccess();
    testLoopPattern();
    testWorkloadShift();
    cout << endl << "按任意键结束...";
    getchar();
    return 0;
}