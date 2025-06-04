/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_scan.h"
#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
    // 初始化file_handle和rid（指向第一个存放了记录的位置）
    // 如果文件只有文件头页（num_pages = 1），则没有记录页，直接设置为超出范围
    if (file_handle_->file_hdr_.num_pages <= RM_FIRST_RECORD_PAGE) {
        rid_ = Rid{file_handle_->file_hdr_.num_pages, 0}; // 设置为超出范围
    } else {
        rid_ = Rid{RM_FIRST_RECORD_PAGE, -1}; // 从第一个记录页开始，slot_no设为-1
        next(); // 找到第一个有效记录的位置
    }
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    // 找到文件中下一个存放了记录的非空闲位置，用rid_来指向这个位置
    while (true) {
        // 移动到下一个slot
        rid_.slot_no++;
        
        // 检查是否超出当前页面的slot范围
        if (rid_.slot_no >= file_handle_->file_hdr_.num_records_per_page) {
            // 移动到下一个页面
            rid_.page_no++;
            rid_.slot_no = 0;
        }
        
        // 检查是否超出文件范围
        if (rid_.page_no >= file_handle_->file_hdr_.num_pages) {
            return; // 到达文件末尾
        }
        
        // 检查当前位置是否有记录
        if (file_handle_->is_record(rid_)) {
            return; // 找到有效记录
        }
    }
}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
    // 检查是否已经超出文件范围
    return rid_.page_no >= file_handle_->file_hdr_.num_pages;
}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const {
    return rid_;
}