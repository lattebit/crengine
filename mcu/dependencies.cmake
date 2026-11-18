# 中文：一个固件只链接一份依赖；裁剪配置不修改固定的第三方工作树。
# English: Link one copy of each dependency; trimming never modifies pinned third-party trees.
set(CRE_PORTS "${CMAKE_CURRENT_LIST_DIR}/ports")
# 中文：在构建目录应用单点失败清理修正，保留来源许可和固定 submodule。
# English: Apply the failure-cleanup fix in the build directory, retaining licenses and the pinned submodule.
file(READ "${CRE_DEPS}/freetype/src/truetype/ttgxvar.c" CRE_GX_SOURCE)
set(CRE_GX_ANCHOR "num_axes = blend->mmvar->num_axis;")
string(REGEX MATCHALL "num_axes = blend->mmvar->num_axis" CRE_GX_MATCHES "${CRE_GX_SOURCE}")
list(LENGTH CRE_GX_MATCHES CRE_GX_COUNT)
if(NOT CRE_GX_COUNT EQUAL 1)
    message(FATAL_ERROR "Review FreeType variable-font cleanup before updating its source")
endif()
string(REPLACE "${CRE_GX_ANCHOR}" "num_axes = blend->mmvar ? blend->mmvar->num_axis : 0;" CRE_GX_SOURCE "${CRE_GX_SOURCE}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ttgxvar_mcu.c" CONTENT "${CRE_GX_SOURCE}")
file(READ "${CRE_DEPS}/freetype/src/truetype/truetype.c" CRE_TT_SOURCE)
string(REPLACE "#include \"ttgxvar.c\"" "#include \"${CMAKE_CURRENT_BINARY_DIR}/ttgxvar_mcu.c\"" CRE_TT_SOURCE "${CRE_TT_SOURCE}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/truetype_mcu.c" CONTENT "${CRE_TT_SOURCE}")
add_library(cre_freetype STATIC
    "${CRE_PORTS}/ftbase.c" "${CRE_DEPS}/freetype/src/base/ftsynth.c" "${CRE_DEPS}/freetype/src/base/ftstroke.c" "${CRE_DEPS}/freetype/src/base/ftbbox.c"
    "${CRE_DEPS}/freetype/src/base/ftbitmap.c" "${CRE_DEPS}/freetype/src/base/ftdebug.c"
    "${CRE_DEPS}/freetype/src/base/ftglyph.c" "${CRE_DEPS}/freetype/src/base/ftinit.c"
    "${CRE_DEPS}/freetype/src/base/ftmm.c" "${CRE_DEPS}/freetype/src/base/ftsystem.c"
    "${CRE_DEPS}/freetype/src/sfnt/sfnt.c" "${CMAKE_CURRENT_BINARY_DIR}/truetype_mcu.c"
    "${CRE_DEPS}/freetype/src/smooth/smooth.c" "${CRE_DEPS}/freetype/src/cff/cff.c"
    "${CRE_DEPS}/freetype/src/psaux/psaux.c" "${CRE_DEPS}/freetype/src/pshinter/pshinter.c"
    "${CRE_DEPS}/freetype/src/psnames/psnames.c")
target_include_directories(cre_freetype PUBLIC "${CRE_DEPS}/freetype/include" PRIVATE "${CRE_PORTS}" "${CRE_DEPS}/freetype/src/truetype")
target_compile_definitions(cre_freetype PRIVATE FT2_BUILD_LIBRARY
    FT_CONFIG_OPTIONS_H="cre_ftoptions.h" FT_CONFIG_MODULES_H="cre_ftmodule.h")
# 中文：灰阶栅格器仅在同步调用期间借用栈池；GCC 15 的逃逸提示保留为警告。
# English: The gray rasterizer borrows its stack pool only during a synchronous call; retain GCC 15's escape diagnostic as a warning.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12)
    set_source_files_properties("${CRE_DEPS}/freetype/src/smooth/smooth.c" PROPERTIES COMPILE_OPTIONS "-Wno-error=dangling-pointer")
endif()

# 中文：关闭服务会释放语言池，默认语言句柄不能跨关闭保留。
# English: Service shutdown frees the interned language pool, so the default handle must not survive shutdown.
file(READ "${CRE_DEPS}/harfbuzz/src/hb-common.cc" CRE_HB_COMMON)
set(CRE_HB_ANCHOR "static hb_atomic_ptr_t <hb_language_t> default_language;")
string(REGEX MATCHALL "static hb_atomic_ptr_t <hb_language_t> default_language" CRE_HB_MATCHES "${CRE_HB_COMMON}")
list(LENGTH CRE_HB_MATCHES CRE_HB_COUNT)
if(NOT CRE_HB_COUNT EQUAL 1)
    message(FATAL_ERROR "Review HarfBuzz default-language lifetime before updating its source")
endif()
string(REPLACE "${CRE_HB_ANCHOR}" "hb_atomic_ptr_t <hb_language_t> default_language;" CRE_HB_COMMON "${CRE_HB_COMMON}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/hb-common_mcu.cc" CONTENT "${CRE_HB_COMMON}")
file(READ "${CRE_DEPS}/harfbuzz/src/harfbuzz.cc" CRE_HB_SOURCE)
string(REPLACE "#include \"hb-common.cc\"" "#include \"${CMAKE_CURRENT_BINARY_DIR}/hb-common_mcu.cc\"" CRE_HB_SOURCE "${CRE_HB_SOURCE}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/harfbuzz_mcu.cc" CONTENT "${CRE_HB_SOURCE}")
add_library(cre_harfbuzz STATIC "${CRE_PORTS}/harfbuzz.cc")
target_include_directories(cre_harfbuzz PUBLIC "${CRE_DEPS}/harfbuzz/src" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(cre_harfbuzz PRIVATE HAVE_FREETYPE HB_NO_MT HB_NO_MMAP
    HB_NO_ATEXIT HB_NO_SUBSET HB_NO_PAINT HB_NO_COLOR)
target_compile_features(cre_harfbuzz PRIVATE cxx_std_17)
target_link_libraries(cre_harfbuzz PUBLIC cre_freetype)

# 中文：引擎直接传入 Unicode，移除 FriBidi 的命令行字符集转换和弃用 API。
# English: The engine passes Unicode directly; omit FriBidi command-line charset conversion and deprecated APIs.
set(CRE_FRIBIDI_SOURCES fribidi fribidi-arabic fribidi-bidi fribidi-bidi-types
    fribidi-brackets fribidi-joining fribidi-joining-types
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
# 中文：md_is_link_label 成功路径总会写入起始行；保留 GCC 15 的保守分析提示。
# English: Successful md_is_link_label paths always set the starting line; retain GCC 15's conservative diagnostic.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(cre_md4c PRIVATE -Wno-error=maybe-uninitialized)
endif()
foreach(target cre_freetype cre_harfbuzz cre_fribidi cre_unibreak cre_utf8proc cre_zlib cre_xxhash cre_md4c)
    target_compile_options(${target} PRIVATE -ffunction-sections -fdata-sections)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ${CRE_MCU_PIC})
endforeach()
