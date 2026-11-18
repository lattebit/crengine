/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：实际 C 端口检查资源表和阅读顺序上限，以及失败后可继续打开文档。
 * English: Exercise package limits through the real C port and reopen documents after rejection.
 */
#include "platform.h"
#include "cre_mcu_document.h"
#include <string>
#include <cstring>

int main(int argc, char **argv) {
    assert(argc == 2); Platform platform; auto cfg = config(platform); assert(cre_mcu_initialize(&cfg) == CRE_OK);
    cre_layout layout{320, 480, 24, 400, 8, 100, 0, 0, 0, "unused.ttf", 8};
    for (const char *kind : {"manifest", "spine"}) {
        for (unsigned count : {32769u, 32768u}) {
            auto path = std::string(argv[1]) + '/' + kind + '-' + std::to_string(count) + ".epub";
            cre_document *document = nullptr; int rc = cre_document_open(path.c_str(), &layout, &document);
            std::fprintf(stderr, "%s result=%d\n", path.c_str(), rc);
            if (count == 32769) assert(rc == CRE_CONTENT && !document);
            else {
                assert(rc == CRE_OK && document);
                cre_metadata metadata{}; assert(cre_document_metadata(document, &metadata) == CRE_OK);
                assert(metadata.chapters == (std::strcmp(kind, "spine") ? 1u : count));
                cre_document_close(document);
            }
            assert(!platform.files);
        }
    }
    cre_mcu_shutdown(); assert(!platform.live && !platform.files);
    std::puts("Manifest/spine limits, early rejection and subsequent document opens passed");
}
