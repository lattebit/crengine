/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：将布局文件映射到不可变缓存块，最后提交根记录；不保存页面像素。
 * English: Map layout files to immutable cache blocks, committing the root last; page pixels are excluded.
 */
#pragma once
#include "lvstream.h"
#include <cstddef>
#include <cstdint>

struct CreLayoutRecords {
    void *context; ///< 所属文档 / Owning document
    bool (*read)(void *, uint64_t, void *, size_t, size_t *); ///< 有界校验读取 / Bounded verified read
    bool (*write)(void *, uint64_t, const void *, size_t); ///< 原子记录写入 / Atomic record write
    void (*remove)(void *, uint64_t); ///< 可选提交后回收 / Optional reclamation after commit
};
/// 每个布局键只有一个活动写入者；新建不会覆盖已提交根。/ One active writer per layout key; creation does not replace the committed root.
LVStreamRef cre_layout_stream(CreLayoutRecords records, uint64_t layout, bool restore);
/// 同步只写块，显式提交才发布根；取消或失败保留旧根。/ Flush writes blocks; explicit commit publishes the root, preserving the old root on cancellation or failure.
bool cre_layout_commit(LVStreamRef stream);
