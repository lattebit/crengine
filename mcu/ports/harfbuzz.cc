/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：隔离 Picolibc 的 ctype 兼容宏与 HarfBuzz 字符串表宏；第三方源保持原样。
 * English: Isolate Picolibc's ctype compatibility macro from HarfBuzz string tables without changing vendor sources.
 */
#include <locale>
#include "hb.hh"
#pragma push_macro("_S")
#undef _S
#include "../deps/harfbuzz/src/harfbuzz.cc"
#pragma pop_macro("_S")
