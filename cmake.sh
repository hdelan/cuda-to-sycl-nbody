#!/bin/bash

# Copyright (C) 2022 Codeplay Software Limited
# This work is licensed under the terms of the MIT license.
# For a copy, see https://opensource.org/licenses/MIT.

sed -i 's/simulator.cuh/simulator.dp.hpp/g' src_sycl/*
sed -i 's/simulator.cu/simulator.dp.cpp/g' src_sycl/*

BUILD_DIR="build"

rm -rf $BUILD_DIR
mkdir $BUILD_DIR
cd $BUILD_DIR || exit

CXX=clang++ \
CC=clang \
cmake .. -GNinja \
-DGLEW_LIBRARY=/usr/lib/x86_64-linux-gnu/libGLEW.so \
-DBACKEND=DPCPP -DDPCPP_CUDA_SUPPORT=off || exit

ninja
