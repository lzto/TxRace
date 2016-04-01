# Build system for the common Sanitizer runtime support library components.
# These components are shared between AddressSanitizer and ThreadSanitizer.

cmake_minimum_required (VERSION 2.6)


set(CMAKE_CXX_FLAGS "-fPIC -O3 -fPIE -msse3 -Wframe-larger-than=512 -std=c++11 -Wall -Werror -fno-builtin -fno-exceptions -fno-rtti -fomit-frame-pointer")


set(SANITIZER_SOURCES
  sanitizer_allocator.cc
  sanitizer_common.cc
  sanitizer_deadlock_detector1.cc
  sanitizer_deadlock_detector2.cc
  sanitizer_flags.cc
  sanitizer_flag_parser.cc
  sanitizer_libc.cc
  sanitizer_libignore.cc
  sanitizer_linux.cc
  sanitizer_mac.cc
  sanitizer_persistent_allocator.cc
  sanitizer_platform_limits_linux.cc
  sanitizer_platform_limits_posix.cc
  sanitizer_posix.cc
  sanitizer_printf.cc
  sanitizer_procmaps_common.cc
  sanitizer_procmaps_freebsd.cc
  sanitizer_procmaps_linux.cc
  sanitizer_procmaps_mac.cc
  sanitizer_stackdepot.cc
  sanitizer_stacktrace.cc
  sanitizer_stacktrace_printer.cc
  sanitizer_suppressions.cc
  sanitizer_symbolizer.cc
  sanitizer_symbolizer_libbacktrace.cc
  sanitizer_symbolizer_mac.cc
  sanitizer_symbolizer_win.cc
  sanitizer_tls_get_addr.cc
  sanitizer_thread_registry.cc
  sanitizer_win.cc)

set(SANITIZER_LIBCDEP_SOURCES
  sanitizer_common_libcdep.cc
  sanitizer_coverage_libcdep.cc
  sanitizer_coverage_mapping_libcdep.cc
  sanitizer_linux_libcdep.cc
  sanitizer_posix_libcdep.cc
  sanitizer_stacktrace_libcdep.cc
  sanitizer_stoptheworld_linux_libcdep.cc
  sanitizer_symbolizer_libcdep.cc
  sanitizer_symbolizer_posix_libcdep.cc
  sanitizer_symbolizer_process_libcdep.cc
  sanitizer_unwind_posix_libcdep.cc)

# Explicitly list all sanitizer_common headers. Not all of these are
# included in sanitizer_common source files, but we need to depend on
# headers when building our custom unit tests.
set(SANITIZER_HEADERS
  sanitizer_addrhashmap.h
  sanitizer_allocator.h
  sanitizer_allocator_interface.h
  sanitizer_allocator_internal.h
  sanitizer_atomic.h
  sanitizer_atomic_clang.h
  sanitizer_atomic_msvc.h
  sanitizer_bitvector.h
  sanitizer_bvgraph.h
  sanitizer_common.h
  sanitizer_common_interceptors.inc
  sanitizer_common_interceptors_ioctl.inc
  sanitizer_common_interceptors_format.inc
  sanitizer_common_syscalls.inc
  sanitizer_deadlock_detector.h
  sanitizer_deadlock_detector_interface.h
  sanitizer_flag_parser.h
  sanitizer_flags.h
  sanitizer_flags.inc
  sanitizer_interface_internal.h
  sanitizer_internal_defs.h
  sanitizer_lfstack.h
  sanitizer_libc.h
  sanitizer_libignore.h
  sanitizer_linux.h
  sanitizer_list.h
  sanitizer_mac.h
  sanitizer_mutex.h
  sanitizer_persistent_allocator.h
  sanitizer_placement_new.h
  sanitizer_platform.h
  sanitizer_platform_interceptors.h
  sanitizer_platform_limits_posix.h
  sanitizer_posix.h
  sanitizer_procmaps.h
  sanitizer_quarantine.h
  sanitizer_report_decorator.h
  sanitizer_stackdepot.h
  sanitizer_stackdepotbase.h
  sanitizer_stacktrace.h
  sanitizer_stacktrace_printer.h
  sanitizer_stoptheworld.h
  sanitizer_suppressions.h
  sanitizer_symbolizer.h
  sanitizer_symbolizer_internal.h
  sanitizer_symbolizer_libbacktrace.h
  sanitizer_symbolizer_mac.h
  sanitizer_symbolizer_win.h
  sanitizer_syscall_generic.inc
  sanitizer_syscall_linux_x86_64.inc
  sanitizer_thread_registry.h)

add_library(tsan_san STATIC
	${SANITIZER_SOURCES}
	${SANITIZER_LIBCDEP_SOURCES}
)

include_directories(../)


