/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：验证压缩资源跨缓冲区跳读、回退及边界，防止图片探测越界。
 * English: Verify compressed-resource seeks, rewinds and boundaries across buffers to prevent image-probe overruns.
 */
#include "platform.h"
#include "lvstream.h"
#include <algorithm>
#include <random>
#include <string>
#include <vector>
int main(int argc, char **argv) {
    assert(argc == 2); Platform platform; auto cfg = config(platform);
    assert(cre_mcu_initialize(&cfg) == CRE_OK);
    {
        auto file = LVOpenFileStream(argv[1], LVOM_READ);
        auto archive = LVOpenArchieve(file); assert(!archive.isNull());
        auto stream = archive->OpenStream(U"bytes.bin", LVOM_READ); assert(!stream.isNull());
        constexpr unsigned size = 300000; assert(stream->GetSize() == size);
        auto check = [&](unsigned at, unsigned count) {
            assert(stream->SetPos(at) == at);
            std::vector<unsigned char> bytes(count); lvsize_t actual = 0;
            assert(stream->Read(bytes.data(), count, &actual) == LVERR_OK);
            assert(actual == std::min(count, size - at));
            for (unsigned i = 0; i < actual; ++i)
                assert(bytes[i] == ((unsigned(at + i) * 37 + (at + i) / 251) & 255));
        };
        check(0, 24); check(60000, 4); check(120000, 17); check(32, 3);
        std::mt19937 random(17);
        for (unsigned i = 0; i < 400; ++i) check(random() % size, 1 + random() % 20000);
        check(size, 16); assert(stream->Eof());
        assert(stream->Seek(-11, LVSEEK_END, nullptr) == LVERR_OK && stream->GetPos() == size - 11);
        assert(stream->Seek(-int(size) - 1, LVSEEK_END, nullptr) == LVERR_FAIL);
        assert(stream->Seek(size + 1, LVSEEK_SET, nullptr) == LVERR_FAIL);
    }
    {
        // 中文：声明长度大于实际解压数据时，读取与跳转必须失败而不死循环。
        // English: An inflated-size mismatch must fail reads and seeks without spinning.
        std::string path(argv[1]); path.replace(path.size() - 4, 4, "-short.zip");
        auto file = LVOpenFileStream(path.c_str(), LVOM_READ);
        auto archive = LVOpenArchieve(file); assert(!archive.isNull());
        auto stream = archive->OpenStream(U"bytes.bin", LVOM_READ); assert(!stream.isNull());
        assert(stream->SetPos(60000) != 60000);
        assert(stream->SetPos(0) == 0);
        unsigned char bytes[16]; lvsize_t actual = 0;
        assert(stream->Read(bytes, sizeof(bytes), &actual) == LVERR_FAIL && actual == 3);
    }
    cre_mcu_shutdown(); assert(!platform.live && !platform.files);
    std::puts("PASS compressed resource multi-block seeks, rewinds, random reads and EOF bounds");
}
