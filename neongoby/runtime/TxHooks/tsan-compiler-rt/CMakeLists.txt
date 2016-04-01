# Build for the ThreadSanitizer runtime support library.

PROJECT(tsan CXX ASM)

cmake_minimum_required (VERSION 2.6)

set(CMAKE_CXX_FLAGS "-fPIC -O3 -fPIE -msse3 -Wframe-larger-than=512 -std=c++11 -Wall -Werror -fno-builtin -fno-exceptions -fno-rtti -fomit-frame-pointer")

set(TSAN_SOURCES
  rtl/tsan_clock.cc
  rtl/tsan_flags.cc
  rtl/tsan_fd.cc
  rtl/tsan_ignoreset.cc
  rtl/tsan_interceptors.cc
  rtl/tsan_interface_ann.cc
  rtl/tsan_interface_atomic.cc
  rtl/tsan_interface.cc
  rtl/tsan_md5.cc
  rtl/tsan_mman.cc
  rtl/tsan_mutex.cc
  rtl/tsan_mutexset.cc
  rtl/tsan_report.cc
  rtl/tsan_rtl.cc
  rtl/tsan_rtl_mutex.cc
  rtl/tsan_rtl_report.cc
  rtl/tsan_rtl_thread.cc
  rtl/tsan_stack_trace.cc
  rtl/tsan_stat.cc
  rtl/tsan_suppressions.cc
  rtl/tsan_symbolize.cc
  rtl/tsan_sync.cc
  rtl/tsan_rtl_amd64.S)

list(APPEND TSAN_SOURCES
    rtl/tsan_platform_linux.cc)

set(TSAN_HEADERS
  rtl/tsan_clock.h
  rtl/tsan_defs.h
  rtl/tsan_dense_alloc.h
  rtl/tsan_fd.h
  rtl/tsan_flags.h
  rtl/tsan_flags.inc
  rtl/tsan_ignoreset.h
  rtl/tsan_interface_ann.h
  rtl/tsan_interface.h
  rtl/tsan_interface_inl.h
  rtl/tsan_mman.h
  rtl/tsan_mutex.h
  rtl/tsan_mutexset.h
  rtl/tsan_platform.h
  rtl/tsan_report.h
  rtl/tsan_rtl.h
  rtl/tsan_stack_trace.h
  rtl/tsan_stat.h
  rtl/tsan_suppressions.h
  rtl/tsan_symbolize.h
  rtl/tsan_sync.h
  rtl/tsan_trace.h
  rtl/tsan_update_shadow_word_inl.h
  rtl/tsan_vector.h)

add_library(tsan_rtl STATIC
	${TSAN_SOURCES}
	)


include_directories(rtl
	rtl/interception
	rtl/sanitizer_common)

add_subdirectory(rtl/interception)
add_subdirectory(rtl/sanitizer_common)

