/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：引擎私有平台钩子；仅 MCU 构建可见。
 * English: Private engine platform hooks, visible only in MCU builds.
 */
#pragma once
#include "cre_mcu.h"
#include "cre_mcu_memory.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include <stdexcept>
#include "lvstream.h"
struct CreMcuFailure { int code; };
void cre_mcu_checkpoint();
void cre_mcu_reset_error();
int cre_mcu_error();
FT_Error cre_mcu_new_library(FT_Library *out);
FT_Error cre_mcu_new_face(FT_Library library, const char *path, FT_Long index, FT_Face *out);
/// 嵌入字体共用有界读取缓存，不保留整份文件。/ Embedded fonts share bounded reads without retaining the entire file.
LVStreamRef cre_mcu_font_stream(LVStreamRef source);
FT_Error cre_mcu_new_stream_face(FT_Library library, LVStreamRef source, FT_Long index, FT_Face *out);
/// 按需提取字体，返回普通文件路径；无可用磁盘缓存时返回空路径。
/// Extract a font on demand into a regular path; return an empty path when disk caching is unavailable.
lString8 cre_mcu_cache_font(const char *name, LVStreamRef source);
FT_Library cre_mcu_font_library();
LVStreamRef cre_mcu_open_stream(const lChar32 *path, int mode);
LVContainerRef cre_mcu_open_directory(const lChar32 *path);
class LVImageSource;
class ldomDocument;
class ldomNode;
ldomNode *cre_mcu_svg_image_node(ldomNode *node);
LVImageSource *cre_mcu_svg_image_source(ldomNode *node);
LVImageSource *cre_mcu_image_source(LVStreamRef stream, ldomDocument *doc, ldomNode *node);
int cre_mcu_decode_image(const uint8_t *data, size_t bytes, uint32_t width, uint32_t height, cre_mcu_bitmap *out);
