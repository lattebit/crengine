/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：在设备大小的线程栈上恢复压缩布局，不能用主机默认大栈掩盖问题。
 * English: Restore compressed layouts on a device-sized thread stack, without hiding faults behind host defaults.
 */
#include "platform.h"
#include "cache_records.h"
#include "cre_mcu_document.h"
#include <pthread.h>
#include <string>
static Records records;
struct Inputs { const char *font; const char *fixtures; };
static void *run(void *opaque) {
    auto &inputs = *static_cast<Inputs *>(opaque);
    Platform platform;
    auto cfg = config(platform);
    cfg.read_layout = [](void *, uint64_t key, void *out, size_t capacity, size_t *actual) -> int {
        return read(&records, key, out, capacity, actual);
    };
    cfg.write_layout = [](void *, uint64_t key, const void *data, size_t bytes) -> int {
        return write(&records, key, data, bytes);
    };
    cfg.remove_layout = [](void *, uint64_t key) { remove(&records, key); };
    assert(cre_mcu_initialize(&cfg) == CRE_OK);
    cre_layout layout{480, 640, 24, 400, 12, 100, 2, 0, 0, {}, 12};
    std::strcpy(layout.font, inputs.font);
    std::vector<uint8_t> pixels(layout.width * layout.height / 2), expected;
    std::string path = std::string(inputs.fixtures) + "/fixture.html";
    for (unsigned pass = 0; pass < 3; ++pass) {
        cre_document *document = nullptr;
        assert(cre_document_open(path.c_str(), &layout, &document) == CRE_OK);
        cre_anchor anchor{};
        cre_page_state state{};
        assert(cre_document_prepare(document, &anchor, 0, pixels.data(), pixels.size(), &state) == CRE_OK);
        if (!pass) expected = pixels;
        else assert(pixels == expected && records.reads);
        assert(cre_document_checkpoint(document) == CRE_OK && records.writes);
        cre_document_close(document);
    }
    cre_mcu_shutdown();
    assert(!platform.files && !platform.live);
    return nullptr;
}
int main(int argc, char **argv) {
    assert(argc == 3);
    Inputs inputs{argv[1], argv[2]};
    pthread_attr_t attr;
    pthread_t worker;
    assert(!pthread_attr_init(&attr));
    assert(!pthread_attr_setstacksize(&attr, 128 * 1024));
    assert(!pthread_create(&worker, &attr, run, &inputs));
    assert(!pthread_attr_destroy(&attr));
    assert(!pthread_join(worker, nullptr));
    std::puts("PASS compressed layout restore on a 128 KiB worker stack");
}
