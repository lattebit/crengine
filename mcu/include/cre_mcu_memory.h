/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：内部受管分配接口，不依赖字体或文档类型。
 * English: Internal managed allocation hooks without font or document dependencies.
 */
#pragma once
#include <stddef.h>
void *cre_mcu_allocate(size_t bytes);
void *cre_mcu_allocate_optional(size_t bytes);
void cre_mcu_release(void *data);
/// 最后一个字体引用释放后归还 HarfBuzz 全局缓存。/ Release HarfBuzz global caches after the final font reference.
void cre_mcu_harfbuzz_shutdown();
