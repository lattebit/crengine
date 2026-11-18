# 中文：一个固件只链接一份依赖；裁剪配置不修改固定的第三方工作树。
# English: Link one copy of each dependency; trimming never modifies pinned third-party trees.
set(CRE_PORTS "${CMAKE_CURRENT_LIST_DIR}/ports")
add_library(cre_freetype STATIC
    "${CRE_PORTS}/ftbase.c" "${CRE_DEPS}/freetype/src/base/ftsynth.c" "${CRE_DEPS}/freetype/src/base/ftstroke.c" "${CRE_DEPS}/freetype/src/base/ftbbox.c"
    "${CRE_DEPS}/freetype/src/base/ftbitmap.c" "${CRE_DEPS}/freetype/src/base/ftdebug.c"
    "${CRE_DEPS}/freetype/src/base/ftglyph.c" "${CRE_DEPS}/freetype/src/base/ftinit.c"
    "${CRE_DEPS}/freetype/src/base/ftmm.c" "${CRE_DEPS}/freetype/src/base/ftsystem.c"
    "${CRE_DEPS}/freetype/src/sfnt/sfnt.c" "${CRE_DEPS}/freetype/src/truetype/truetype.c"
    "${CRE_DEPS}/freetype/src/smooth/smooth.c" "${CRE_DEPS}/freetype/src/cff/cff.c"
    "${CRE_DEPS}/freetype/src/psaux/psaux.c" "${CRE_DEPS}/freetype/src/pshinter/pshinter.c"
    "${CRE_DEPS}/freetype/src/psnames/psnames.c")
target_include_directories(cre_freetype PUBLIC "${CRE_DEPS}/freetype/include" PRIVATE "${CRE_PORTS}")
target_compile_definitions(cre_freetype PRIVATE FT2_BUILD_LIBRARY
    FT_CONFIG_OPTIONS_H="cre_ftoptions.h" FT_CONFIG_MODULES_H="cre_ftmodule.h")

add_library(cre_harfbuzz STATIC "${CRE_DEPS}/harfbuzz/src/harfbuzz.cc")
target_include_directories(cre_harfbuzz PUBLIC "${CRE_DEPS}/harfbuzz/src")
target_compile_definitions(cre_harfbuzz PRIVATE HAVE_FREETYPE HB_NO_MT HB_NO_MMAP
    HB_NO_ATEXIT HB_NO_SUBSET HB_NO_PAINT HB_NO_COLOR)
target_compile_features(cre_harfbuzz PRIVATE cxx_std_17)
target_link_libraries(cre_harfbuzz PUBLIC cre_freetype)

set(CRE_FRIBIDI_SOURCES fribidi fribidi-arabic fribidi-bidi fribidi-bidi-types
    fribidi-brackets fribidi-char-sets fribidi-char-sets-cap-rtl fribidi-char-sets-cp1255
    fribidi-char-sets-cp1256 fribidi-char-sets-iso8859-6 fribidi-char-sets-iso8859-8
    fribidi-char-sets-utf8 fribidi-deprecated fribidi-joining fribidi-joining-types
    fribidi-mirroring fribidi-run fribidi-shape)
list(TRANSFORM CRE_FRIBIDI_SOURCES PREPEND "${CRE_DEPS}/fribidi/lib/")
list(TRANSFORM CRE_FRIBIDI_SOURCES APPEND ".c")
add_library(cre_fribidi STATIC ${CRE_FRIBIDI_SOURCES})
target_include_directories(cre_fribidi PUBLIC "${CRE_DEPS}/fribidi/lib" "${CRE_PORTS}/fribidi")
target_compile_definitions(cre_fribidi PRIVATE FRIBIDI_BUILD HAVE_STDLIB_H=1 HAVE_STRING_H=1 HAVE_STRINGS_H=1 HAVE_STRINGIZE=1 STDC_HEADERS=1)

add_library(cre_unibreak STATIC "${CRE_DEPS}/libunibreak/src/eastasianwidthdef.c" "${CRE_DEPS}/libunibreak/src/linebreak.c"
    "${CRE_DEPS}/libunibreak/src/linebreakdata.c" "${CRE_DEPS}/libunibreak/src/linebreakdef.c"
    "${CRE_DEPS}/libunibreak/src/unibreakbase.c" "${CRE_DEPS}/libunibreak/src/unibreakdef.c"
    "${CRE_DEPS}/libunibreak/src/wordbreak.c" "${CRE_DEPS}/libunibreak/src/graphemebreak.c")
target_include_directories(cre_unibreak PUBLIC "${CRE_DEPS}/libunibreak/src")
add_library(cre_utf8proc STATIC "${CRE_PORTS}/utf8proc.c")
target_include_directories(cre_utf8proc PUBLIC "${CRE_DEPS}/utf8proc")
target_compile_definitions(cre_utf8proc PUBLIC UTF8PROC_STATIC)
add_library(cre_zlib STATIC "${CRE_DEPS}/zlib/adler32.c" "${CRE_DEPS}/zlib/crc32.c"
    "${CRE_DEPS}/zlib/deflate.c" "${CRE_DEPS}/zlib/inflate.c" "${CRE_DEPS}/zlib/inffast.c"
    "${CRE_DEPS}/zlib/inftrees.c" "${CRE_DEPS}/zlib/trees.c" "${CRE_DEPS}/zlib/zutil.c"
    "${CRE_DEPS}/zlib/compress.c" "${CRE_DEPS}/zlib/uncompr.c")
target_include_directories(cre_zlib PUBLIC "${CRE_DEPS}/zlib")
add_library(cre_xxhash STATIC "${CRE_DEPS}/xxhash/xxhash.c")
target_include_directories(cre_xxhash PUBLIC "${CRE_DEPS}/xxhash")
add_library(cre_md4c STATIC "${CRE_DEPS}/md4c/src/md4c.c")
target_include_directories(cre_md4c PUBLIC "${CRE_DEPS}/md4c/src")
foreach(target cre_freetype cre_harfbuzz cre_fribidi cre_unibreak cre_utf8proc cre_zlib cre_xxhash cre_md4c)
    target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()
