# Build for the runtime interception helper library.

set(CMAKE_CXX_FLAGS "-fPIC -O3 -fPIE -msse3 -Wframe-larger-than=512 -std=c++11 -Wall -Werror -fno-builtin -fno-exceptions -fno-rtti -fomit-frame-pointer")


set(INTERCEPTION_SOURCES
  interception_linux.cc
  interception_mac.cc
  interception_win.cc
  interception_type_test.cc
  )

add_library(tsan_icp STATIC
	${INTERCEPTION_SOURCES}
)

include_directories(../)


