PROJ_INSTALL_ROOT=/opt/spec/tools-root/

all:
	mkdir -p build && pushd build && cmake .. && make -j && popd

install: all
	install build/rtl/interception/libtsan_icp.a ${PROJ_INSTALL_ROOT}/lib
	install build/rtl/sanitizer_common/libtsan_san.a ${PROJ_INSTALL_ROOT}/lib
	install build/libtsan_rtl.a ${PROJ_INSTALL_ROOT}/lib

