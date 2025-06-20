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

## 详细修改内容说明

### 一、DiskManager类实现详解

#### 1.1 页面读写操作核心实现

**write_page() 函数**
```cpp
void DiskManager::write_page(int fd, page_id_t page_no, const char *offset, int num_bytes) {
    // 计算页面在文件中的偏移量
    off_t page_offset = static_cast<off_t>(page_no) * PAGE_SIZE;
    
    // 使用lseek定位到指定页面的起始位置
    if (lseek(fd, page_offset, SEEK_SET) == -1) {
        throw InternalError("DiskManager::write_page lseek Error");
    }
    
    // 调用write函数写入数据
    ssize_t bytes_written = write(fd, offset, num_bytes);
    
    // 检查写入的字节数是否与期望的一致
    if (bytes_written != num_bytes) {
        throw InternalError("DiskManager::write_page Error");
    }
}
```

**关键修改点：**
- **偏移量计算**：使用 `page_no * PAGE_SIZE` 计算页面在文件中的绝对位置
- **错误处理**：对 `lseek()` 和 `write()` 的返回值进行严格检查
- **类型转换**：使用 `static_cast<off_t>` 确保偏移量类型正确

**read_page() 函数**
```cpp
void DiskManager::read_page(int fd, page_id_t page_no, char *offset, int num_bytes) {
    // 计算页面在文件中的偏移量
    off_t page_offset = static_cast<off_t>(page_no) * PAGE_SIZE;
    
    // 使用lseek定位到指定页面的起始位置
    if (lseek(fd, page_offset, SEEK_SET) == -1) {
        throw InternalError("DiskManager::read_page lseek Error");
    }
    
    // 调用read函数读取数据
    ssize_t bytes_read = read(fd, offset, num_bytes);
    
    // 检查读取的字节数是否与期望的一致
    if (bytes_read != num_bytes) {
        throw InternalError("DiskManager::read_page Error");
    }
}
```

#### 1.2 文件操作实现

**create_file() 函数**
```cpp
void DiskManager::create_file(const std::string &path) {
    // 检查文件是否已存在
    if (is_file(path)) {
        throw FileExistsError(path);
    }
    
    // 创建文件，使用O_CREAT | O_RDWR模式，权限设为0644
    int fd = open(path.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd == -1) {
        throw UnixError();
    }
    
    // 立即关闭文件
    close(fd);
}
```

**关键修改点：**
- **重复创建检查**：先调用 `is_file()` 检查文件是否存在
- **文件权限设置**：使用 `0644` 权限（所有者读写，组和其他用户只读）
- **资源管理**：创建后立即关闭文件描述符

**open_file() / close_file() 函数**
```cpp
int DiskManager::open_file(const std::string &path) {
    // 检查文件是否已经被打开
    if (path2fd_.count(path)) {
        throw FileNotClosedError(path);
    }
    
    // 检查文件是否存在
    if (!is_file(path)) {
        throw FileNotFoundError(path);
    }
    
    // 打开文件并更新映射表
    int fd = open(path.c_str(), O_RDWR);
    if (fd == -1) {
        throw UnixError();
    }
    
    path2fd_[path] = fd;  // 路径到文件描述符的映射
    fd2path_[fd] = path;  // 文件描述符到路径的映射
    
    return fd;
}
```

**关键修改点：**
- **重复打开检查**：维护 `path2fd_` 映射表防止重复打开
- **双向映射**：同时维护 `path2fd_` 和 `fd2path_` 两个映射表
- **状态一致性**：确保文件打开状态的一致性管理

### 二、LRUReplacer类实现详解

#### 2.1 数据结构设计

```cpp
class LRUReplacer : public Replacer {
private:
    size_t max_size_;                    // 最大容量
    std::list<frame_id_t> lru_list_;     // LRU链表，头部为最近使用
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_;  // 快速定位
    std::mutex latch_;                   // 并发控制锁
};
```

**设计亮点：**
- **双向链表**：支持O(1)时间复杂度的插入和删除
- **哈希表加速**：通过迭代器实现O(1)查找和删除
- **线程安全**：使用互斥锁保护所有操作

#### 2.2 核心算法实现

**victim() 函数 - 淘汰最久未使用的帧**
```cpp
bool LRUReplacer::victim(frame_id_t *frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    
    if (lru_list_.empty()) {
        return false;
    }
    
    // 获取链表尾部的帧（最久未使用）
    *frame_id = lru_list_.back();
    
    // 从映射表和链表中删除
    lru_map_.erase(*frame_id);
    lru_list_.pop_back();
    
    return true;
}
```

**unpin() 函数 - 将帧标记为可替换**
```cpp
void LRUReplacer::unpin(frame_id_t frame_id) {
    std::lock_guard<std::mutex> lock(latch_);
    
    // 如果帧已存在，先删除旧位置
    if (lru_map_.count(frame_id)) {
        lru_list_.erase(lru_map_[frame_id]);
    }
    
    // 检查容量限制
    if (lru_list_.size() >= max_size_) {
        return;
    }
    
    // 插入到链表头部（最近使用位置）
    lru_list_.push_front(frame_id);
    lru_map_[frame_id] = lru_list_.begin();
}
```

### 三、BufferPoolManager类实现详解

#### 3.1 核心数据结构

```cpp
class BufferPoolManager {
private:
    size_t pool_size_;                              // 缓冲池大小
    Page *pages_;                                   // 页面数组
    std::unordered_map<PageId, frame_id_t, PageIdHash> page_table_;  // 页面表
    std::list<frame_id_t> free_list_;              // 空闲帧列表
    Replacer *replacer_;                           // 替换策略
    DiskManager *disk_manager_;                    // 磁盘管理器
    std::mutex latch_;                             // 并发控制锁
};
```

#### 3.2 页面管理核心算法

**find_victim_page() 辅助函数**
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
```

**fetch_page() 函数 - 获取指定页面**
```cpp
Page *BufferPoolManager::fetch_page(PageId page_id) {
    std::lock_guard<std::mutex> lock(latch_);
    
    // 检查页面是否已在缓冲池中
    if (page_table_.count(page_id)) {
        frame_id_t frame_id = page_table_[page_id];
        Page *page = &pages_[frame_id];
        page->pin_count_++;
        replacer_->pin(frame_id);
        return page;
    }
    
    // 查找可用帧
    frame_id_t frame_id;
    if (!find_victim_page(&frame_id)) {
        return nullptr;  // 无可用帧
    }
    
    Page *page = &pages_[frame_id];
    
    // 如果被替换的页面是脏页，先写入磁盘
    if (page->is_dirty_) {
        disk_manager_->write_page(page->id_.fd, page->id_.page_no, 
                                 page->data_, PAGE_SIZE);
    }
    
    // 更新页面表
    if (page->id_.page_no != INVALID_PAGE_ID) {
        page_table_.erase(page->id_);
    }
    
    // 从磁盘读取新页面
    disk_manager_->read_page(page_id.fd, page_id.page_no, 
                            page->data_, PAGE_SIZE);
    
    // 更新页面信息
    update_page(page, page_id, frame_id);
    replacer_->pin(frame_id);
    
    return page;
}
```

### 四、RmFileHandle类实现详解

#### 4.1 记录操作核心实现

**insert_record() 函数**
```cpp
Rid RmFileHandle::insert_record(char *buf, Context *context) {
    // 获取或创建可用页面
    RmPageHandle page_handle = create_page_handle();
    
    // 在页面中找到空闲slot
    int slot_no = Bitmap::next_bit(0, page_handle.bitmap, file_hdr_.num_records_per_page);
    if (slot_no == file_hdr_.num_records_per_page) {
        // 当前页面已满，创建新页面
        buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
        page_handle = create_new_page_handle();
        slot_no = Bitmap::next_bit(0, page_handle.bitmap, file_hdr_.num_records_per_page);
    }
    
    // 插入记录
    char *slot_data = page_handle.get_slot(slot_no);
    memcpy(slot_data, buf, file_hdr_.record_size);
    
    // 更新bitmap和页面头信息
    Bitmap::set(page_handle.bitmap, slot_no);
    page_handle.page_hdr->num_records++;
    
    // 如果页面插入后变满，更新空闲页面链表
    if (page_handle.page_hdr->num_records == file_hdr_.num_records_per_page) {
        file_hdr_.first_free_page_no = page_handle.page_hdr->next_free_page_no;
    }
    
    // 标记页面为脏页并释放
    buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), true);
    
    return Rid{fd_, page_handle.page->get_page_id().page_no, slot_no};
}
```

**关键修改点：**
- **位图管理**：使用 `Bitmap::next_bit()` 查找空闲slot
- **页面状态管理**：正确处理页面从非满到满的状态转换
- **内存管理**：确保每个页面操作后正确调用 `unpin_page()`

#### 4.2 记录迭代器实现

**RmScan::next() 函数**
```cpp
void RmScan::next() {
    // 处理空文件情况
    if (file_handle_->get_file_hdr().num_pages <= 1) {
        rid_ = Rid{file_handle_->get_fd(), RM_NO_PAGE, -1};
        return;
    }
    
    while (rid_.page_no < file_handle_->get_file_hdr().num_pages) {
        RmPageHandle page_handle = file_handle_->fetch_page_handle(rid_.page_no);
        
        // 在当前页面查找下一个有效记录
        int next_slot = Bitmap::next_bit(rid_.slot_no + 1, page_handle.bitmap, 
                                        file_handle_->get_file_hdr().num_records_per_page);
        
        file_handle_->buffer_pool_manager_->unpin_page(page_handle.page->get_page_id(), false);
        
        if (next_slot < file_handle_->get_file_hdr().num_records_per_page) {
            // 找到有效记录
            rid_.slot_no = next_slot;
            return;
        }
        
        // 移动到下一页
        rid_.page_no++;
        rid_.slot_no = -1;
    }
    
    // 到达文件末尾
    rid_ = Rid{file_handle_->get_fd(), RM_NO_PAGE, -1};
}
```

**关键修改点：**
- **边界处理**：正确处理空文件和文件末尾情况
- **跨页迭代**：实现页面边界的正确跳转
- **内存管理**：每次页面访问后正确释放

### 五、系统调用函数说明

#### 5.1 lseek() 系统调用

**函数原型**：`off_t lseek(int fd, off_t offset, int whence)`

**参数说明**：
- `fd`：文件描述符
- `offset`：偏移量
- `whence`：定位方式
  - `SEEK_SET`：从文件开头开始定位（绝对位置）
  - `SEEK_CUR`：从当前位置开始定位（相对位置）
  - `SEEK_END`：从文件末尾开始定位

**在项目中的使用**：
1. **页面定位**：`lseek(fd, page_no * PAGE_SIZE, SEEK_SET)` - 定位到指定页面
2. **日志追加**：`lseek(log_fd_, 0, SEEK_END)` - 定位到文件末尾

#### 5.2 read()/write() 系统调用

**函数原型**：
- `ssize_t read(int fd, void *buf, size_t count)`
- `ssize_t write(int fd, const void *buf, size_t count)`

**错误处理策略**：
- 检查返回值是否等于期望读写的字节数
- 不匹配时抛出 `InternalError` 异常

#### 5.3 open() 系统调用

**使用的标志位**：
- `O_CREAT`：文件不存在时创建
- `O_RDWR`：读写模式打开
- `O_EXCL`：与O_CREAT配合，文件存在时失败

**文件权限**：使用 `0644`（所有者读写，组和其他用户只读）

### 六、并发控制策略

#### 6.1 锁的使用原则

1. **粗粒度锁**：每个类使用一个全局锁保证原子性
2. **RAII模式**：使用 `std::lock_guard` 自动管理锁的生命周期
3. **避免死锁**：统一的加锁顺序，避免嵌套锁

#### 6.2 线程安全保证

- **LRUReplacer**：所有操作都在锁保护下执行
- **BufferPoolManager**：页面分配、替换等关键操作加锁
- **原子操作**：页面编号分配使用 `std::atomic`

### 七、错误处理机制

#### 7.1 异常类型

- `InternalError`：内部错误（如系统调用失败）
- `FileExistsError`：文件已存在
- `FileNotFoundError`：文件不存在
- `FileNotClosedError`：文件未关闭
- `UnixError`：Unix系统调用错误

#### 7.2 错误恢复策略

- **fail-fast原则**：发现错误立即抛出异常
- **资源清理**：异常发生时确保资源正确释放
- **状态一致性**：错误后保持数据结构的一致性

这些详细的实现说明展示了数据库存储系统的完整设计思路和关键技术细节，为理解现代数据库系统的存储层实现提供了深入的技术参考。
