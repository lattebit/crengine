#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# 中文：迁移旧阅读器的资源表与阅读顺序边界，不依赖旧解析库。
# English: Migrate manifest and spine boundaries without the old parsing library.
from pathlib import Path
import sys
import zipfile

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for kind in ['manifest', 'spine']:
    for count in [32768, 32769]:
        with zipfile.ZipFile(out / f'{kind}-{count}.epub', 'w', compression=zipfile.ZIP_DEFLATED) as book:
            book.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
            book.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
            items = ''.join(f'<item id="c{i}" href="body.xhtml" media-type="application/xhtml+xml"/>' for i in range(count if kind == 'manifest' else 1))
            spine = '<itemref idref="c0"/>' * (count if kind == 'spine' else 1)
            book.writestr('book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="3.0"><manifest>' + items + '</manifest><spine>' + spine + '</spine></package>')
            book.writestr('body.xhtml', '<html><body><p>Boundary text.</p></body></html>')
