/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：验证扩容失败不先释放仍由调用者持有的内存。
 * English: Verify failed growth preserves storage still owned by the caller.
 */
#include "lvtypes.h"
#include <cassert>
#include <cstdlib>
#include <limits>
#include <initializer_list>
static bool fail_resize = false;
static unsigned releases = 0, resizes = 0;
static void *resize_test(void *p, size_t size) { ++resizes; return fail_resize ? nullptr : std::realloc(p, size); }
static void release_test(void *p) { ++releases; std::free(p); }
#define realloc resize_test
#define free release_test
#include "lvmemman.h"
#undef free
#undef realloc
#include "cre_mcu_internal.h"
int main() {
    int *values = cr_realloc<int>(nullptr, 2); assert(values); values[0] = 17; values[1] = 31;
    fail_resize = true;
    for (size_t count : {size_t(4), std::numeric_limits<size_t>::max()}) {
        bool failed = false; unsigned previous = resizes;
        try { values = cr_realloc(values, count); }
        catch (const CreMcuFailure &error) { assert(error.code == CRE_MEMORY); failed = true; }
        assert(failed && !releases && values[0] == 17 && values[1] == 31);
        assert(resizes == previous + (count == 4 ? 1 : 0));
    }
    fail_resize = false; values = cr_realloc(values, 4); assert(values[0] == 17 && values[1] == 31);
    values = cr_realloc(values, 0); assert(!values && releases == 1);
}
