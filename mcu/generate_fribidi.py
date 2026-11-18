#!/usr/bin/env python3
"""从固定 FriBidi 数据生成表。/ Generate tables from pinned FriBidi Unicode data."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
source = root / "deps/fribidi"
output = root / "ports/fribidi"
output.mkdir(parents=True, exist_ok=True)
flags = ["-O2", "-UHAVE_CONFIG_H", "-DHAVE_STRINGIZE", "-DDONT_HAVE_FRIBIDI_CONFIG_H",
         "-DHAVE_STDLIB_H", "-DHAVE_STRING_H", "-DHAVE_STRINGS_H", "-DSTDC_HEADERS=1",
         "-I" + str(source / "lib"), "-I" + str(output)]
tables = {
    "bidi-type": ["UnicodeData.txt"],
    "joining-type": ["UnicodeData.txt", "ArabicShaping.txt"],
    "arabic-shaping": ["UnicodeData.txt"],
    "mirroring": ["BidiMirroring.txt"],
    "brackets": ["BidiBrackets.txt", "UnicodeData.txt"],
    "brackets-type": ["BidiBrackets.txt"],
}
with tempfile.TemporaryDirectory(prefix="cre-fribidi-") as temporary:
    def generate(name, files, target, arguments):
        program = str(Path(temporary) / name)
        subprocess.run([os.environ.get("HOST_CC", "cc"), *flags,
                        *(str(source / "gen.tab" / f) for f in files), "-o", program], check=True)
        with (output / target).open("wb") as result:
            subprocess.run([program, *arguments, name], stdout=result, check=True)

    def data(files):
        return [str(source / "gen.tab/unidata" / f) for f in files]

    generate("gen-unicode-version", ["gen-unicode-version.c"], "fribidi-unicode-version.h",
             data(["ReadMe.txt", "BidiMirroring.txt"]))
    for table, inputs in tables.items():
        name = "gen-" + table + "-tab"
        generate(name, [name + ".c", "packtab.c"], table + ".tab.i", ["2", *data(inputs)])
