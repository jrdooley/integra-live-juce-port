# Third-party code compiled into libIntegra.
#
#   integra_libpd        - libpd (Pd 0.43.3) + the Pd "extra" externals Integra registers
#   integra_pdexternals  - partconv~, freeverb~, copy, soundfile_info, fsplay~ (+ flext)
#   integra_minizip      - zip/unzip for .integra and .module files
#   integra_tmpfileplus  - portable temporary files

set(_pd ${CMAKE_CURRENT_SOURCE_DIR}/libpd/pure-data)

# ---------------------------------------------------------------- libpd -------
set(INTEGRA_PD_SOURCES
    d_arithmetic.c d_array.c d_ctl.c d_dac.c d_delay.c d_fft.c d_fft_mayer.c
    d_fftroutine.c d_filter.c d_global.c d_math.c d_misc.c d_osc.c d_resample.c
    d_soundfile.c d_ugen.c g_all_guis.c g_array.c g_bang.c g_canvas.c g_editor.c
    g_graph.c g_guiconnect.c g_hdial.c g_hslider.c g_io.c g_mycanvas.c g_numbox.c
    g_readwrite.c g_rtext.c g_scalar.c g_template.c g_text.c g_toggle.c
    g_traversal.c g_vdial.c g_vslider.c g_vumeter.c m_atom.c m_binbuf.c m_class.c
    m_conf.c m_glob.c m_memory.c m_obj.c m_pd.c m_sched.c s_audio.c
    s_audio_dummy.c s_file.c s_inter.c s_loader.c s_main.c s_path.c s_print.c
    s_utf8.c x_acoustics.c x_arithmetic.c x_connective.c x_gui.c x_interface.c
    x_list.c x_midi.c x_misc.c x_net.c x_qlist.c x_time.c)
list(TRANSFORM INTEGRA_PD_SOURCES PREPEND ${_pd}/src/)

add_library(integra_libpd STATIC
    ${INTEGRA_PD_SOURCES}
    libpd/libpd_wrapper/s_libpdmidi.c
    libpd/libpd_wrapper/x_libpdreceive.c
    libpd/libpd_wrapper/z_libpd.c
    libpd/libpd_wrapper/util/ringbuffer.c
    libpd/libpd_wrapper/util/z_hook_util.c
    libpd/libpd_wrapper/util/z_print_util.c
    libpd/libpd_wrapper/util/z_queued.c
    libpd/cpp/PdBase.cpp
    libpd/cpp/PdTypes.cpp
    # Pd "extra" externals registered statically by CDspEngine::register_externals()
    ${_pd}/extra/bonk~/bonk~.c
    ${_pd}/extra/fiddle~/fiddle~.c
    ${_pd}/extra/lrshift~/lrshift~.c
    ${_pd}/extra/expr~/vexp.c
    ${_pd}/extra/expr~/vexp_fun.c
    ${_pd}/extra/expr~/vexp_if.c)

target_include_directories(integra_libpd PUBLIC
    ${_pd}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/libpd/libpd_wrapper
    ${CMAKE_CURRENT_SOURCE_DIR}/libpd/libpd_wrapper/util
    ${CMAKE_CURRENT_SOURCE_DIR}/libpd/cpp)
target_compile_definitions(integra_libpd PUBLIC PD USEAPI_DUMMY
    $<$<NOT:$<PLATFORM_ID:Windows>>:HAVE_UNISTD_H HAVE_LIBDL>)
if(WIN32)
    target_compile_definitions(integra_libpd PUBLIC WINVER=0x502 PD_INTERNAL _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(integra_libpd PUBLIC ws2_32 kernel32)
else()
    target_link_libraries(integra_libpd PUBLIC ${CMAKE_DL_LIBS} m)
endif()
target_link_libraries(integra_libpd PUBLIC Threads::Threads)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Pd 0.43 predates modern warnings; keep the build log readable.
    target_compile_options(integra_libpd PRIVATE $<$<COMPILE_LANGUAGE:C>:-w>)
endif()

# ---------------------------------------------------------------- flext -------
file(GLOB INTEGRA_FLEXT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/flext/source/*.cpp)
# SndObj and STK bindings are optional flext features Integra does not use
list(FILTER INTEGRA_FLEXT_SOURCES EXCLUDE REGEX "/fl(sndobj|stk)\\.cpp$")
add_library(integra_flext STATIC ${INTEGRA_FLEXT_SOURCES})
target_include_directories(integra_flext PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/flext/source)
# static, multi-threaded flext for Pd (equivalent of the old libflext-pd_t.a)
target_compile_definitions(integra_flext PUBLIC FLEXT_SYS=2 FLEXT_THREADS FLEXT_USE_CMEM)
target_link_libraries(integra_flext PUBLIC integra_libpd)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(integra_flext PRIVATE -w)
endif()

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
target_link_libraries(integra_pdexternals PUBLIC integra_flext integra_libpd integra::fftw3f integra::sndfile)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(integra_pdexternals PRIVATE -w)
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
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(integra_minizip PRIVATE -w)
endif()

# ------------------------------------------------------------ tmpfileplus -----
add_library(integra_tmpfileplus STATIC tmpfileplus/tmpfileplus.c)
target_include_directories(integra_tmpfileplus PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tmpfileplus)
