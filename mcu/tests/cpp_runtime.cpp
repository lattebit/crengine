/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：验证链接引擎的宿主仍能捕获系统 C++ 动态库抛出的标准异常。
 * English: Verify engine consumers can catch standard exceptions from the system C++ library.
 */
#include <cassert>
#include <exception>
#include <string>

int main() {
    // 中文：这两个异常来自标准库，不能用本地显式 throw 代替。
    // English: These exceptions originate in the standard library, not a local explicit throw.
    for (const char *input : {"invalid", "999999999999999999999999999999999999999999"}) {
        bool caught = false;
        try { (void)std::stoul(input); }
        catch (const std::exception &error) { caught = error.what()[0] != '\0'; }
        assert(caught);
    }
}
