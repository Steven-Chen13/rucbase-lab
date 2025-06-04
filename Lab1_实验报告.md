# RucBase Lab 1 存储管理实验报告

## 实验概述

### 实验目标
本实验要求实现数据库存储系统中的缓冲池管理器和记录管理器，主要包括：
- **任务一**：缓冲池管理器（DiskManager、LRU Replacer、BufferPoolManager）
- **任务二**：记录管理器（Record Operations、Record Iterator）

### 实验环境
- 操作系统：Ubuntu 22.04.5 LTS (Dev Container)
- 编译器：GCC/G++
- 构建工具：CMake + Make
- 测试框架：Google Test

## 任务一：缓冲池管理器

### 任务1.1 磁盘存储管理器 (DiskManager)

#### 实现思路
DiskManager负责磁盘文件的底层操作，包括页面读写、文件管理和页面编号分配。主要使用Linux系统调用实现文件操作。

#### 关键实现

**1. 页面读写操作**
```cpp
void DiskManager::write_page(int fd, page_id_t page_no, const char *offset, int num_bytes) {
    // 计算页面在文件中的偏移量
    off_t page_offset = page_no * PAGE_SIZE;
    // 定位到指定位置
    lseek(fd, page_offset, SEEK_SET);
    // 写入数据
    ssize_t bytes_written = write(fd, offset, num_bytes);
    if (bytes_written != num_bytes) {
        throw UnixError();
    }
}

void DiskManager::read_page(int fd, page_id_t page_no, char *offset, int num_bytes) {
    // 计算页面在文件中的偏移量
    off_t page_offset = page_no * PAGE_SIZE;
    // 定位到指定位置
    lseek(fd, page_offset, SEEK_SET);
    // 读取数据
    ssize_t bytes_read = read(fd, offset, num_bytes);
    if (bytes_read != num_bytes) {
        throw UnixError();
    }
}
```

**2. 文件操作**
- 使用`struct stat`检查文件存在性
- 使用`O_CREAT | O_EXCL`模式创建文件，防止重复创建
- 维护文件描述符映射表，防止重复打开/关闭
- 使用`unlink()`删除文件

**3. 页面分配策略**
采用简单的自增分配策略，为每个文件维护独立的页面计数器。

#### 测试结果
- FileOperation 测试通过 (16ms)
- PageOperation 测试通过 (17ms)
- **得分：10/10**

### 任务1.2 缓冲池替换策略 (LRU Replacer)

#### 实现思路
实现LRU (Least Recently Used) 替换算法，使用双向链表+哈希表的经典组合：
- `std::list<frame_id_t>`：维护帧的LRU顺序
- `std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator>`：快速定位帧在链表中的位置

#### 关键实现

**1. 数据结构设计**
```cpp
class LRUReplacer : public Replacer {
private:
    size_t max_size_;
    std::list<frame_id_t> lru_list_;  // 链表头部为最近使用，尾部为最久未使用
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;
    std::mutex latch_;  // 并发控制
};
```

**2. 核心算法**
- `victim()`：选择尾部（最久未使用）的帧进行淘汰
- `pin()`：从LRU列表中移除指定帧
- `unpin()`：将帧插入到链表头部（最近使用位置）

**3. 并发安全**
所有操作都使用`std::lock_guard<std::mutex>`保证线程安全。

#### 测试结果
- SimpleTest 测试通过 (0ms)
- MixTest 测试通过 (26ms)
- ConcurrencyTest 测试通过 (211ms)
- **得分：20/20**

### 任务1.3 缓冲池管理器 (BufferPoolManager)

#### 实现思路
BufferPoolManager是缓冲池的核心，负责协调内存页面和磁盘页面之间的数据移动。主要数据结构包括：
- 页面数组 `pages_`
- 页面表 `page_table_`（PageId到frame_id的映射）
- 空闲帧列表 `free_list_`
- LRU替换器 `replacer_`

#### 关键实现

**1. 辅助函数**
```cpp
bool BufferPoolManager::find_victim_page(frame_id_t *frame_id) {
    // 优先使用空闲帧
    if (!free_list_.empty()) {
        *frame_id = free_list_.front();
        free_list_.pop_front();
        return true;
    }
    // 使用LRU替换策略
    return replacer_->victim(frame_id);
}

void BufferPoolManager::update_page(Page *page, PageId new_page_id, frame_id_t new_frame_id) {
    // 更新页面信息
    page->id_ = new_page_id;
    page->pin_count_ = 1;
    page->is_dirty_ = false;
    // 更新页面表
    page_table_[new_page_id] = new_frame_id;
}
```

**2. 核心功能**
- `new_page()`：分配新页面编号，找到可用帧，初始化页面
- `fetch_page()`：从缓冲池获取页面，必要时从磁盘读取
- `unpin_page()`：减少页面引用计数，更新脏标志
- `delete_page()`：删除页面，更新页面表和空闲列表
- `flush_page()`/`flush_all_pages()`：强制写入磁盘

**3. 脏页处理**
在淘汰脏页时，必须先将其写入磁盘，保证数据一致性。

#### 测试结果
- SimpleTest 测试通过 (184ms)
- LargeScaleTest 测试通过 (513ms)
- MultipleFilesTest 测试通过 (1431ms)
- ConcurrencyTest 测试通过 (2636ms)
- **得分：40/40**

## 任务二：记录管理器

### 任务2.1 记录操作 (RmFileHandle)

#### 实现思路
RmFileHandle负责文件级别的记录操作，每个文件对应一个文件头和多个数据页面。使用位图（bitmap）跟踪每个slot的使用状态。

#### 关键实现

**1. 辅助函数**
```cpp
RmPageHandle RmFileHandle::create_new_page_handle() {
    // 创建新页面
    PageId new_page_id = {fd_, INVALID_PAGE_ID};
    Page *page = buffer_pool_manager_->new_page(&new_page_id);
    
    // 初始化页面头
    auto page_handle = RmPageHandle(&file_hdr_, page);
    auto page_hdr = page_handle.page_hdr;
    page_hdr->next_free_page_no = RM_NO_PAGE;
    page_hdr->num_records = 0;
    
    // 更新文件头
    file_hdr_.first_free_page_no = new_page_id.page_no;
    file_hdr_.num_pages++;
    
    return page_handle;
}
```

**2. 记录操作**
- `get_record()`：根据RID获取记录，进行边界检查和空值验证
- `insert_record()`：找到空闲slot，更新bitmap，处理页面满的情况
- `delete_record()`：清除bitmap对应位，处理页面从满变为非满的状态转换
- `update_record()`：直接更新指定位置的记录数据

**3. 页面管理**
- 维护空闲页面链表
- 动态分配新页面
- 正确处理页面状态转换

#### 关键挑战与解决方案

**内存管理**：确保所有页面操作后正确调用`unpin_page()`，防止内存泄漏。

**边界检查**：在所有记录操作中添加完整的边界检查和异常处理。

### 任务2.2 记录迭代器 (RmScan)

#### 实现思路
RmScan提供顺序遍历文件中所有记录的功能，需要正确处理跨页面的迭代和各种边界情况。

#### 关键实现

**1. 构造函数**
```cpp
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
    // 初始化到第一条记录
    rid_ = {file_handle_->get_fd(), RM_FIRST_RECORD_PAGE, -1};
    next();  // 找到第一条有效记录
}
```

**2. 迭代逻辑**
```cpp
void RmScan::next() {
    // 处理空文件情况
    if (file_handle_->get_file_hdr().num_pages <= 1) {
        rid_ = {file_handle_->get_fd(), RM_NO_PAGE, -1};
        return;
    }
    
    // 在当前页面查找下一个有效slot
    // 如果当前页面没有更多记录，移动到下一页面
    // 重复直到找到有效记录或到达文件末尾
}
```

**3. 边界处理**
- 空文件：直接设置为结束状态
- 页面边界：正确跳转到下一个页面
- 文件结束：设置特殊的结束标志

#### 关键挑战与解决方案

**空文件处理**：原始实现在处理空文件时会进入无限循环，通过添加页面数量检查解决。

**跨页面迭代**：确保在页面边界正确跳转，不遗漏任何记录。

#### 测试结果
- SimpleTest 测试通过 (1463ms，包含452次插入、284次删除、264次更新)
- MultipleFilesTest 测试通过 (567ms)
- **得分：30/30**

## 实验总结

### 完成情况
- ✅ 任务1.1 磁盘存储管理器 (10/10分)
- ✅ 任务1.2 缓冲池替换策略 (20/20分)  
- ✅ 任务1.3 缓冲池管理器 (40/40分)
- ✅ 任务2.1 记录操作 (30/30分)
- ✅ 任务2.2 记录迭代器 (包含在任务2.1中)

**总分：100/100**

### 技术亮点

1. **并发安全设计**：所有关键组件都实现了线程安全，通过并发测试验证
2. **内存管理**：正确处理页面的pin/unpin操作，避免内存泄漏
3. **错误处理**：完善的边界检查和异常处理机制
4. **性能优化**：LRU算法采用O(1)时间复杂度的实现
5. **代码健壮性**：处理各种边界情况，包括空文件、空页面等

### 遇到的挑战与解决方案

1. **并发控制**：使用细粒度锁保证线程安全的同时避免死锁
2. **内存管理**：仔细跟踪每个页面的生命周期，确保正确的pin/unpin配对
3. **边界情况**：特别是空文件的处理，需要特殊的逻辑来避免无限循环
4. **状态一致性**：确保缓冲池、页面表、空闲列表等数据结构的一致性

### 学习收获

1. **深入理解数据库存储系统**：从底层文件操作到上层记录管理的完整实现
2. **系统编程能力提升**：熟练使用Linux系统调用和并发编程
3. **算法设计与优化**：LRU算法的高效实现和内存管理策略
4. **软件工程实践**：模块化设计、错误处理、测试驱动开发

### 代码质量

- 代码结构清晰，模块化程度高
- 注释完整，便于理解和维护
- 错误处理机制完善
- 通过所有单元测试，包括压力测试和并发测试

本实验成功实现了一个功能完整、性能良好、线程安全的数据库存储管理系统，为后续的索引管理和查询执行实验奠定了坚实的基础。
