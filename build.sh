#!/bin/sh
export AUTOCONF_VERSION=2.69
export AUTOMAKE_VERSION=1.15
test -f configure || ./autogen.sh
./configure --without-local-portaudio \
            --with-wish=/usr/local/bin/wish8.5 \
            CXXFLAGS="-I/usr/local/include" \
	    LIBS="-L/usr/local/lib" \
	    CC=egcc CXX=eg++
gmake
