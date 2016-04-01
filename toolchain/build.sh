#!/bin/bash
# March 2015 Tong Zhang<ztong@vt.edu>
# April 2015 Tong Zhang<ztong@vt.edu>
#
# toolchain build script for spec-2.6 benchmarks
# This toolchain includes
# binutils-2.25
# gcc-4.8.4
# python-2.7.9
# llvm-3.4.2
# clang-3.4.2
#
# ONLY FOR IA32
#
# the destination of toolchain is /opt/spec/tools-root
# 
# NOTE: -std=gnu89 need to be specified when using the toolchain
# 

die() { echo "$@" 1>&2 ; exit 1; }

MAKE_JOBS="12"

export PATH=/opt/spec/tools-root/bin:$PATH
export SPEC_TOOLS_ROOT="/opt/spec/tools-root"

#---------------------------------------------------#
#TOOL CHAIN
:<<'COMMENTOUT'
#binutils
wget -c "http://ftp.gnu.org/gnu/binutils/binutils-2.25.tar.bz2"

BINUTILS_SRC="binutils-2.25.tar.bz2"

rm -rf ./`echo $BINUTILS_SRC|sed -e s/.tar.bz2//`
tar xf $BINUTILS_SRC

pushd `echo $BINUTILS_SRC|sed -e s/.tar.bz2//`

./configure \
	--prefix=${SPEC_TOOLS_ROOT} \
	--disable-nls || die "binutils config failed"

make -j"${MAKE_JOBS}" || die "binutils make failed"
make install || die "binutils install failed"

popd

#gcc

wget -c "https://ftp.gnu.org/gnu/gcc/gcc-4.8.4/gcc-4.8.4.tar.bz2"
wget -c "http://www.mpfr.org/mpfr-3.1.2/mpfr-3.1.2.tar.xz"
wget -c "http://ftp.gnu.org/gnu//gmp/gmp-6.0.0a.tar.xz"
wget -c "http://www.multiprecision.org/mpc/download/mpc-1.0.2.tar.gz"
wget -c "http://ftp.gnu.org/gnu/libiconv/libiconv-1.14.tar.gz"

GCC_SRC="gcc-4.8.4.tar.bz2"
MPFR_SRC="mpfr-3.1.2.tar.xz"
GMP_SRC="gmp-6.0.0a.tar.xz"
MPC_SRC="mpc-1.0.2.tar.gz"
LIBICONV_SRC="libiconv-1.14.tar.gz"

rm -rf ./`echo $GCC_SRC|sed -e s/.tar.bz2//`

tar xf $GCC_SRC || die "decompress gcc failed"
pushd `echo $GCC_SRC|sed -e s/.tar.bz2//`

xz -d -c ../${MPFR_SRC} | tar xv || die "decompress mpfr failed"
xz -d -c ../${GMP_SRC} | tar xv || die "decompress gmp failed"
tar xf ../${MPC_SRC} || die "decompress mpc failed"
tar xf ../${LIBICONV_SRC} || die "decompress libiconv failed"

echo mv `echo ${MPFR_SRC}|sed -e s/.tar.xz//` mpfr
mv `echo ${MPFR_SRC}|sed -e s/.tar.xz//` mpfr
echo mv `echo ${GMP_SRC}|sed -e s/a.tar.xz//` gmp
mv `echo ${GMP_SRC}|sed -e s/a.tar.xz//` gmp
echo mv `echo ${MPC_SRC}|sed -e s/.tar.gz//` mpc
mv `echo ${MPC_SRC}|sed -e s/.tar.gz//` mpc
echo mv `echo ${LIBICONV_SRC}|sed -e s/.tar.gz//` libiconv
mv `echo ${LIBICONV_SRC}|sed -e s/.tar.gz//` libiconv

./configure \
	--prefix=${SPEC_TOOLS_ROOT} \
	--disable-nls \
	--enable-languages=c,c++ || die "gcc config failed"

make -j"${MAKE_JOBS}"|| die "gcc make failed"

make install || die "gcc install failed"

popd

#---------------------------------------------------#
#python

wget -c "https://www.python.org/ftp/python/2.7.9/Python-2.7.9.tar.xz"

PYTHON_SRC="Python-2.7.9.tar.xz"

rm -rf ./`echo $PYTHON_SRC | sed -e s/.tar.xz//`

xz -d -c $PYTHON_SRC |tar xv
pushd `echo $PYTHON_SRC | sed -e s/.tar.xz//`

./configure --prefix=${SPEC_TOOLS_ROOT}
make -j"${MAKE_JOBS}"
make install 
popd
COMMENTOUT
#----------------------------------------------------#
#LLVM and CLANG
wget -c "http://llvm.org/releases/3.4.2/llvm-3.4.2.src.tar.gz"
wget -c "http://llvm.org/releases/3.4.2/cfe-3.4.2.src.tar.gz"

LLVM_SRC="llvm-3.4.2.src.tar.gz"
CLANG_SRC="cfe-3.4.2.src.tar.gz"

mkdir -p /opt/spec/tools-root/

rm -rf ./`echo $LLVM_SRC | sed -e s/.tar.gz//`

tar xf $LLVM_SRC
pushd `echo $LLVM_SRC | sed -e s/.tar.gz//`/tools
tar xf ../../$CLANG_SRC
mv `echo $CLANG_SRC | sed -e s/.tar.gz//` clang
cd ../

./configure \
	--prefix=${SPEC_TOOLS_ROOT} \
	--enable-optimized \
	--enable-targets="x86"

make -j"${MAKE_JOBS}"

make install

popd


