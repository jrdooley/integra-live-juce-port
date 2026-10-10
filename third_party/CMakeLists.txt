# Third-party code compiled into libIntegra.
#
#   integra_libpd        - libpd 0.16.1 (Pd 0.56-5) + the Pd "extra" externals
#   integra_pdexternals  - partconv~, freeverb~, copy, soundfile_info, fsplay~ (+ flext)
#   integra_minizip      - zip/unzip for .integra and .module files
#   integra_tmpfileplus  - portable temporary files

set(_pd ${CMAKE_CURRENT_SOURCE_DIR}/libpd/pure-data)

# ---------------------------------------------------------------- libpd -------
set(INTEGRA_PD_SOURCES
    d_arithmetic.c d_array.c d_ctl.c d_dac.c d_delay.c d_fft.c d_fft_fftsg.c d_filter.c
    d_global.c d_math.c d_misc.c d_osc.c d_resample.c d_soundfile.c d_soundfile_aiff.c
    d_soundfile_caf.c d_soundfile_next.c d_soundfile_wave.c d_ugen.c g_all_guis.c
    g_array.c g_bang.c g_canvas.c g_clone.c g_editor.c g_editor_extras.c g_graph.c
    g_guiconnect.c g_io.c g_mycanvas.c g_numbox.c g_radio.c g_readwrite.c g_rtext.c
    g_scalar.c g_slider.c g_template.c g_text.c g_toggle.c g_traversal.c g_undo.c
    g_vumeter.c m_atom.c m_binbuf.c m_class.c m_conf.c m_glob.c m_memory.c m_obj.c
    m_pd.c m_sched.c s_audio.c s_audio_dummy.c s_inter.c s_inter_gui.c s_loader.c
    s_main.c s_net.c s_path.c s_print.c s_utf8.c x_acoustics.c x_arithmetic.c
    x_array.c x_connective.c x_file.c x_gui.c x_interface.c x_list.c x_midi.c
    x_misc.c x_net.c x_scalar.c x_text.c x_time.c x_vexp.c x_vexp_fun.c x_vexp_if.c)
list(TRANSFORM INTEGRA_PD_SOURCES PREPEND ${_pd}/src/)

# the "extra" objects libpd_init() registers when LIBPD_EXTRA is defined
set(INTEGRA_PD_EXTRA_SOURCES
    bob~/bob~.c bonk~/bonk~.c choice/choice.c fiddle~/fiddle~.c loop~/loop~.c
    lrshift~/lrshift~.c pd~/pdsched.c pd~/pd~.c pique/pique.c sigmund~/sigmund~.c
    stdout/stdout.c)
list(TRANSFORM INTEGRA_PD_EXTRA_SOURCES PREPEND ${_pd}/extra/)

# Pd 0.56-5 via libpd 0.16.1: the same files and definitions as libpd's own CMakeLists.txt
add_library(integra_libpd STATIC
    ${INTEGRA_PD_SOURCES}
    ${INTEGRA_PD_EXTRA_SOURCES}
    libpd/libpd_wrapper/s_libpdmidi.c
    libpd/libpd_wrapper/x_libpdreceive.c
    libpd/libpd_wrapper/z_hooks.c
    libpd/libpd_wrapper/z_libpd.c
    libpd/libpd_wrapper/util/z_ringbuffer.c
    libpd/libpd_wrapper/util/z_print_util.c
    libpd/libpd_wrapper/util/z_queued.c)

include(CheckIncludeFile)
check_include_file(alloca.h HAVE_ALLOCA_H)
check_include_file(endian.h HAVE_ENDIAN_H)
check_include_file(machine/endian.h HAVE_MACHINE_ENDIAN_H)

target_include_directories(integra_libpd PUBLIC
    ${_pd}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libpd/libpd_wrapper
    ${CMAKE_CURRENT_SOURCE_DIR}/libpd/libpd_wrapper/util
    ${CMAKE_CURRENT_SOURCE_DIR}/libpd/cpp)
target_compile_definitions(integra_libpd
    PUBLIC PD=1 USEAPI_DUMMY=1 LIBPD_EXTRA=1
           $<$<BOOL:${HAVE_ALLOCA_H}>:HAVE_ALLOCA_H=1>
           $<$<BOOL:${HAVE_ENDIAN_H}>:HAVE_ENDIAN_H=1>
           $<$<BOOL:${HAVE_MACHINE_ENDIAN_H}>:HAVE_MACHINE_ENDIAN_H=1>
           $<$<NOT:$<PLATFORM_ID:Windows>>:HAVE_UNISTD_H=1 HAVE_LIBDL=1>
           $<$<PLATFORM_ID:Darwin>:_DARWIN_C_SOURCE>
    PRIVATE PD_INTERNAL=1)
if(WIN32)
    # 64-bit Windows: long is 32 bits, Pd needs a pointer-sized integer
    target_compile_definitions(integra_libpd PUBLIC "PD_LONGINTTYPE=long long" WINVER=0x0601 _WIN32_WINNT=0x0601
                                             PRIVATE WIN32 _WIN32 _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    if(MSVC)
        target_compile_definitions(integra_libpd PUBLIC HAVE_STRUCT_TIMESPEC)
        target_compile_options(integra_libpd PRIVATE $<$<COMPILE_LANGUAGE:C>:/experimental:c11atomics>)
    endif()
    find_package(PThreads4W REQUIRED)    # vcpkg "pthreads"
    target_link_libraries(integra_libpd PUBLIC PThreads4W::PThreads4W ws2_32 kernel32)
else()
    target_link_libraries(integra_libpd PUBLIC ${CMAKE_DL_LIBS} m)
endif()
target_link_libraries(integra_libpd PUBLIC Threads::Threads)
set_target_properties(integra_libpd PROPERTIES C_STANDARD 11)
if(INTEGRA_SANITIZE)
    # precise sanitizer stacks inside Pd (no inlining)
    target_compile_options(integra_libpd PRIVATE -O0)
endif()
set(INTEGRA_NO_WARNINGS $<IF:$<OR:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,/w,-w>)
target_compile_options(integra_libpd PRIVATE ${INTEGRA_NO_WARNINGS})

# ---------------------------------------------------------------- flext -------
file(GLOB INTEGRA_FLEXT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/flext/source/*.cpp)
# SndObj and STK bindings are optional flext features Integra does not use
list(FILTER INTEGRA_FLEXT_SOURCES EXCLUDE REGEX "/fl(sndobj|stk)\\.cpp$")
add_library(integra_flext STATIC ${INTEGRA_FLEXT_SOURCES})
target_include_directories(integra_flext PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/flext/source)
# static, multi-threaded flext for Pd (equivalent of the old libflext-pd_t.a)
target_compile_definitions(integra_flext PUBLIC FLEXT_SYS=2 FLEXT_THREADS FLEXT_USE_CMEM)
target_link_libraries(integra_flext PUBLIC integra_libpd)
target_compile_options(integra_flext PRIVATE ${INTEGRA_NO_WARNINGS})

# -------------------------------------------------------- other externals -----
set(_ext ${CMAKE_CURRENT_SOURCE_DIR}/pd-externals)
add_library(integra_pdexternals STATIC
    ${_ext}/bsaylor/partconv~.c
    ${_ext}/freeverb~/freeverb~.c
    ${_ext}/copy/copy.c
    ${_ext}/iemlib/soundfile_info.c
    ${_ext}/fsplay~/main.cpp
    ${_ext}/fsplay~/fsp_libsndfile.cpp)
target_include_directories(integra_pdexternals PRIVATE
    ${_ext}/iemlib ${_ext}/fsplay~ ${CMAKE_CURRENT_SOURCE_DIR}/boost)
# the bundled Boost predates C++17; libc++ (macOS) no longer provides std::auto_ptr
target_compile_definitions(integra_pdexternals PRIVATE BOOST_NO_AUTO_PTR)
target_link_libraries(integra_pdexternals PUBLIC integra_flext integra_libpd integra::fftw3f integra::sndfile)
target_compile_options(integra_pdexternals PRIVATE ${INTEGRA_NO_WARNINGS})
# an undeclared function is an error on every compiler (Apple clang's default): on Linux,
# an external calling Pd's undeclared error() silently linked against glibc's error(),
# which exits the process
if(NOT MSVC)
    target_compile_options(integra_libpd PRIVATE $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)
    target_compile_options(integra_pdexternals PRIVATE $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)
endif()

# ---------------------------------------------------------------- minizip -----
add_library(integra_minizip STATIC
    minizip/ioapi.c minizip/unzip.c minizip/zip.c minizip/mztools.c
    $<$<PLATFORM_ID:Windows>:${CMAKE_CURRENT_SOURCE_DIR}/minizip/iowin32.c>)
target_include_directories(integra_minizip PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/minizip)
target_link_libraries(integra_minizip PUBLIC ZLIB::ZLIB)
if(APPLE OR CMAKE_SYSTEM_NAME MATCHES "BSD")
    # minizip's ioapi.c uses the *64 stdio names, which are plain stdio there
    target_compile_definitions(integra_minizip PRIVATE USE_FILE32API)
endif()
target_compile_options(integra_minizip PRIVATE ${INTEGRA_NO_WARNINGS})

# ------------------------------------------------------------ tmpfileplus -----
add_library(integra_tmpfileplus STATIC tmpfileplus/tmpfileplus.c)
target_include_directories(integra_tmpfileplus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tmpfileplus)
