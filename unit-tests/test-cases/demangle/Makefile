##
# Copyright (c) 2010 Apple Inc. All rights reserved.
#
# @APPLE_LICENSE_HEADER_START@
# 
# This file contains Original Code and/or Modifications of Original Code
# as defined in and that are subject to the Apple Public Source License
# Version 2.0 (the 'License'). You may not use this file except in
# compliance with the License. Please obtain a copy of the License at
# http://www.opensource.apple.com/apsl/ and read it before using this
# file.
# 
# The Original Code and all software distributed under the License are
# distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
# EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
# INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
# Please see the License for the specific language governing rights and
# limitations under the License.
# 
# @APPLE_LICENSE_HEADER_END@
##
TESTROOT = ../..
include ${TESTROOT}/include/common.makefile


SHELL = bash # use bash shell so we can redirect just stderr


#
# Test that linker option -demangle works
#

run: all

all:
	${CXX} ${CXXFLAGS} main.cxx -c
	${FAIL_IF_SUCCESS} ${LD} -arch ${ARCH} -dylib main.o ${LD_SYSROOT} ${LD_NEW_LINKEDIT} -o main 2>main1.fail
	grep __ZN3Foo4doitEv main1.fail | ${FAIL_IF_EMPTY}
	${FAIL_IF_SUCCESS} ${LD} -arch ${ARCH} -dylib main.o ${LD_SYSROOT} ${LD_NEW_LINKEDIT} -demangle -o main 2>main2.fail
	grep 'Foo::doit()' main2.fail | ${PASS_IFF_STDIN}
	

clean:
	rm -f main.o main1.fail main2.fail main
