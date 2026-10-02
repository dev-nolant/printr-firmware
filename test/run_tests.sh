#!/usr/bin/env sh
# Builds and runs the host-side unit tests (needs any C++11 compiler).
#   sh firmware/test/run_tests.sh
# Override the compiler with CXX=..., extra flags with CXXFLAGS=...
# (MinGW installs without a plain `ld` need CXXFLAGS=-fuse-ld=bfd)
set -e
cd "$(dirname "$0")"
CXX="${CXX:-c++}"
"$CXX" -std=c++11 -Wall -Wextra -Werror -O1 -g $CXXFLAGS \
    -o test_textfmt test_textfmt.cpp ../printr/textfmt.cpp
./test_textfmt
