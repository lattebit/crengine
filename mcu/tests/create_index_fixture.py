#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# 中文：迁移旧引擎的大目录场景，构造三万独立资源和可直接定位的标题。
# English: Migrate the old engine's large-index scenario with thirty thousand separate resources and navigable titles.
from pathlib import Path
import sys
import zipfile

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
count = 30000
with zipfile.ZipFile(out / 'large-index.epub', 'w', compression=zipfile.ZIP_DEFLATED) as book:
    book.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
    book.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
    items = ''.join(f'<item id="c{i}" href="text/{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(count))
    spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(count))
    book.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="3.0"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Large index</dc:title></metadata><manifest>' + items + '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest><spine>' + spine + '</spine></package>')
    book.writestr('nav.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol>' + ''.join(f'<li><a href="text/{i}.xhtml#heading">Chapter {i}</a></li>' for i in range(count)) + '</ol></nav></body></html>')
    for i in range(count):
        book.writestr(f'text/{i}.xhtml', f'<html><body><p id="heading">Body chapter {i}.</p></body></html>')
