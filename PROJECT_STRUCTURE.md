# Rucbase-Lab 项目结构说明

## 目录结构

```
rucbase-lab/
├── .clang-format              # 代码格式化配置
├── .gitignore                # Git忽略文件配置
├── .gitmodules               # Git子模块配置
├── CMakeLists.txt            # 主构建配置文件
├── LICENSE                   # 开源许可证
├── README.md                 # 项目说明文档
├── Lab1_实验报告.md           # 实验报告
├── PROJECT_STRUCTURE.md      # 项目结构说明（本文件）
│
├── src/                      # 源代码目录
│   ├── CMakeLists.txt        # 源码构建配置
│   ├── defs.h                # 全局定义
│   ├── errors.h              # 错误处理定义
│   ├── portal.h              # 门户接口
│   ├── record_printer.h      # 记录打印工具
│   ├── rmdb.cpp              # 主程序入口
│   │
│   ├── storage/              # 存储管理模块
│   ├── record/               # 记录管理模块
│   ├── replacer/             # 页面替换策略模块
│   ├── common/               # 通用工具模块
│   ├── analyze/              # 分析模块
│   ├── execution/            # 执行引擎模块
│   ├── index/                # 索引管理模块
│   ├── optimizer/            # 查询优化器模块
│   ├── parser/               # SQL解析器模块
│   ├── recovery/             # 恢复管理模块
│   ├── system/               # 系统管理模块
│   ├── transaction/          # 事务管理模块
│   └── test/                 # 测试代码
│
├── build/                    # 构建输出目录
│   ├── bin/                  # 可执行文件
│   ├── lib/                  # 静态库文件
│   ├── Makefile              # 生成的Makefile
│   └── ...                   # 其他构建产物
│
├── deps/                     # 第三方依赖
│   ├── CMakeLists.txt        # 依赖构建配置
│   └── googletest/           # Google Test框架
│
├── docs/                     # 实验文档
│   ├── Rucbase-Lab1[存储管理实验文档].md
│   ├── Rucbase-Lab2[索引管理实验文档].md
│   ├── Rucbase-Lab3[查询执行实验指导].md
│   ├── Rucbase-Lab3[查询执行实验文档].md
│   ├── Rucbase-Lab4[并发控制实验文档].md
│   └── ...                   # 其他文档
│
├── pics/                     # 图片资源
│   ├── architecture_fixed.jpg
│   ├── B+树的结构.png
│   └── ...                   # 其他图片
│
└── rucbase_client/           # 客户端代码
    ├── CMakeLists.txt
    ├── main.cpp
    └── build/
```

## 核心模块说明

### 存储管理 (storage/)
- **DiskManager**: 磁盘存储管理器，负责页面级别的I/O操作
- **BufferPoolManager**: 缓冲池管理器，管理内存中的页面缓存
- **Page**: 页面抽象，表示内存中的一个页面

### 记录管理 (record/)
- **RmFileHandle**: 记录文件句柄，提供记录级别的操作接口
- **RmPageHandle**: 记录页面句柄，管理页面内的记录
- **RmScan**: 记录迭代器，支持顺序扫描
- **Bitmap**: 位图工具，用于管理slot的占用状态

### 页面替换 (replacer/)
- **LRUReplacer**: LRU替换算法实现
- **Replacer**: 替换策略的抽象接口

### 通用工具 (common/)
- 包含各种通用的数据结构和工具函数

## 构建说明

```bash
# 创建并进入构建目录
mkdir build && cd build

# 配置构建
cmake ..

# 编译项目
make -j$(nproc)

# 运行测试
make test
```

## 清理说明

项目已进行以下清理：
1. 删除了空的测试文件 (`test_record_simple.cpp`, `test_simple.cpp`)
2. 删除了冗余的构建目录 (`build_new/`)
3. 清理了构建缓存和临时文件
4. 修复了实验报告中的本地图片路径
5. 统一了代码结构和命名规范

## 注意事项

- `build/` 目录包含构建产物，可以安全删除并重新构建
- `pics/` 目录包含实验报告中使用的图片资源
- 所有源代码都在 `src/` 目录下，按模块组织
- 测试代码位于 `src/test/` 目录下
