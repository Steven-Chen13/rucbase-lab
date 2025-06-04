/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "lru_replacer.h"

LRUReplacer::LRUReplacer(size_t num_pages) { max_size_ = num_pages; }

LRUReplacer::~LRUReplacer() = default;  

/**
 * @description: 使用LRU策略删除一个victim frame，并返回该frame的id
 * @param {frame_id_t*} frame_id 被移除的frame的id，如果没有frame被移除返回nullptr
 * @return {bool} 如果成功淘汰了一个页面则返回true，否则返回false
 */
bool LRUReplacer::victim(frame_id_t* frame_id) {
    // C++17 std::scoped_lock
    // 它能够避免死锁发生，其构造函数能够自动进行上锁操作，析构函数会对互斥量进行解锁操作，保证线程安全。
    std::scoped_lock lock{latch_};  //  如果编译报错可以替换成其他lock

    // 如果LRU列表为空，没有可以淘汰的页面
    if (LRUlist_.empty()) {
        return false;
    }

    // 获取最近最少使用的页面（列表前部）
    frame_id_t victim_frame = LRUlist_.front();
    *frame_id = victim_frame;

    // 从列表和哈希表中移除该页面
    LRUlist_.pop_front();
    LRUhash_.erase(victim_frame);

    return true;
}

/**
 * @description: 固定指定的frame，即该页面无法被淘汰
 * @param {frame_id_t} 需要固定的frame的id
 */
void LRUReplacer::pin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};
    
    // 查找frame_id是否在哈希表中
    auto it = LRUhash_.find(frame_id);
    if (it != LRUhash_.end()) {
        // 从列表中移除该frame
        LRUlist_.erase(it->second);
        // 从哈希表中移除该frame
        LRUhash_.erase(it);
    }
}

/**
 * @description: 取消固定一个frame，代表该页面可以被淘汰
 * @param {frame_id_t} frame_id 取消固定的frame的id
 */
void LRUReplacer::unpin(frame_id_t frame_id) {
    std::scoped_lock lock{latch_};
    
    // 如果frame_id已经在replacer中，不做任何操作
    auto it = LRUhash_.find(frame_id);
    if (it != LRUhash_.end()) {
        return;
    }
    
    // 检查是否已达到最大容量
    if (LRUlist_.size() >= max_size_) {
        return;
    }
    
    // 将frame_id添加到列表尾部（作为最老的元素）
    LRUlist_.push_back(frame_id);
    // 在哈希表中存储指向列表中位置的迭代器
    LRUhash_[frame_id] = std::prev(LRUlist_.end());
}

/**
 * @description: 获取当前replacer中可以被淘汰的页面数量
 */
size_t LRUReplacer::Size() { return LRUlist_.size(); }
