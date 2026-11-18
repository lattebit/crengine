/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：与布局分离的受限 EPUB 源缓存；缓存淘汰不得关闭在途读取。
 * English: Bounded EPUB source cache independent of layout; eviction cannot close in-flight readers.
 */
#pragma once
#include "cre_mcu_document.h"
#include "lvstream.h"
#include <memory>
class CreSourceCache;
std::shared_ptr<CreSourceCache> cre_source_cache_create(LVContainerRef archive, LVContainerRef &wrapped);
void cre_source_cache_bind(CreSourceCache &cache, const cre_source_cache &callbacks);
/// 中文：空闲预算内预读，不淘汰热源或打开未缓存的大资源。/ English: Prefetch within spare budget without evicting hot sources or opening uncached large resources.
void cre_source_cache_prefetch(CreSourceCache &cache, const lChar32 *name);
void cre_source_cache_checkpoint(CreSourceCache &cache);
