#!/bin/bash

export ARCH=arm64
export CROSS_COMPILE=../PLATFORM/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/bin/aarch64-linux-android-
export clang-triple = android/prebuilts/clang/host/linux-x86/clang-4639204/bin/aarch64-linux-gnu-
export ANDROID_MAJOR_VERSION=q

make exynos9610-a50ks_defconfig
make -j64
