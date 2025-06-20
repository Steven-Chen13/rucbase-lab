#!/bin/bash

# Rucbase项目清理脚本
# 用于清理构建产物和临时文件

echo "开始清理Rucbase项目..."

# 清理构建目录
if [ -d "build" ]; then
    echo "清理构建目录..."
    cd build
    rm -rf CMakeCache.txt CMakeFiles/ test_detail.xml *Test_db/
    cd ..
fi

# 清理客户端构建目录
if [ -d "rucbase_client/build" ]; then
    echo "清理客户端构建目录..."
    rm -rf rucbase_client/build/*
fi

# 清理临时文件
echo "清理临时文件..."
find . -name "*.tmp" -delete
find . -name "*.bak" -delete
find . -name "*~" -delete
find . -name "core.*" -delete

# 清理日志文件
find . -name "*.log" -delete

echo "清理完成！"
echo ""
echo "如需完全重新构建，请运行："
echo "rm -rf build && mkdir build && cd build && cmake .. && make"
