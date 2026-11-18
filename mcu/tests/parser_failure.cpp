/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * 中文：格式探测的每次读取与取消均须释放临时字符缓冲和文件。
 * English: Every format-detection read failure and cancellation must release temporary character buffers and files.
 */
#include "platform.h"
#include "cre_mcu_internal.h"
#include "lvxml.h"
#include "lvtinydom.h"
#include <memory>
#include <string>

static void detect(Platform &platform, const std::string &path, bool xml, unsigned step, bool cancel) {
    unsigned failed = 0;
    {
        auto stream = cre_mcu_open_stream(Utf8ToUnicode(path.c_str()).c_str(), LVOM_READ);
        assert(!stream.isNull());
        ldomDocument document; ldomDocumentWriter writer(&document);
        std::unique_ptr<LVFileFormatParser> parser(xml
            ? static_cast<LVFileFormatParser *>(new LVXMLParser(stream, &writer))
            : static_cast<LVFileFormatParser *>(new LVTextParser(stream, &writer, false)));
        if (cancel) platform.cancel_at = platform.checkpoints + step;
        else platform.fail_read_at = platform.reads + step;
        try { (void)parser->CheckFormat(); }
        catch (const CreMcuFailure &error) { assert(error.code == (cancel ? CRE_CANCELLED : CRE_IO)); ++failed; }
        platform.cancel_at = platform.fail_read_at = SIZE_MAX;
    }
    assert(failed == 1 && !platform.files);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    Platform platform; auto cfg = config(platform); assert(cre_mcu_initialize(&cfg) == CRE_OK);
    for (bool xml : {false, true}) for (bool cancel : {false, true}) for (unsigned step : {0u, 1u})
        detect(platform, std::string(argv[1]) + (xml ? "/fixture.fb2" : "/fixture.txt"), xml, step, cancel);
    cre_mcu_shutdown(); assert(!platform.live && !platform.files);
}
