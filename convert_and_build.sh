#!/bin/bash

# Copyright (C) 2022 Codeplay Software Limited
# This work is licensed under the terms of the MIT license.
# For a copy, see https://opensource.org/licenses/MIT.

set -e
set -v

SRC_SYCL="src_sycl"
BUILD_DIR="build"

[[ -e $SRC_SYCL ]] && rm -fr $SRC_SYCL
[[ -e $BUILD_DIR ]] && rm -fr $BUILD_DIR

cp -r src $SRC_SYCL

cd $SRC_SYCL && dpct *.cu && rm *.cu
mv dpct_output/* ./ && rmdir dpct_output && cd ..
cp CMakeLists_dpcpp.txt src_sycl/CMakeLists.txt

sed -i 's/simulator.cuh/simulator.dp.hpp/g' src_sycl/*
sed -i 's/simulator.cu/simulator.dp.cpp/g' src_sycl/*

mkdir $BUILD_DIR
cd $BUILD_DIR || exit

CXX=clang++ \
CC=clang \
cmake .. -GNinja \
-DGLEW_LIBRARY=/usr/lib/x86_64-linux-gnu/libGLEW.so \
-DBACKEND=DPCPP -DDPCPP_CUDA_SUPPORT=off || exit

ninja
