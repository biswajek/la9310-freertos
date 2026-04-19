#!/bin/sh
#SPDX-License-Identifier: GPLv2 BSD-3-Clause
#Copyright 2022 NXP

boot_mode=pcie
extra_cmake_flags=""

if [ "$#" -lt "1" ]; then
	echo "Invoke command as show below (boot_mode i2c,pcie)"
	echo "./build_release.sh -boot_mode=i2c"
	echo  "          OR                   "
	echo "./build_release.sh -boot_mode=pci"
	echo  "          OR (with optional extra cmake flags)   "
	echo "./build_release.sh -boot_mode=pci -DTEST_L1C_TASKS=ON"
	exit 1
fi

file="include/la9310_boot_mode.h"
unlink $file 2>/dev/null || true

i2c_boot=`echo "$1" | grep -i "boot_mode=I2c"`
[ -n "$i2c_boot" ] && {
	boot_mode=i2c
}

# Collect any extra cmake -D flags passed after the boot_mode argument
shift
extra_cmake_flags="$*"

if [ "$boot_mode" = "pcie" ];then
	echo "Build NLM for PCIe Host Mode.."
	cmake -DCMAKE_TOOLCHAIN_FILE="./armgcc.cmake" -G "Unix Makefiles" -DTURN_ON_STANDALONE_MODE=OFF  -DCMAKE_BUILD_TYPE=Release  $extra_cmake_flags .
	echo "#define TURN_ON_HOST_MODE  1" > $file
else
	echo "Build NLM for I2C Standalone Mode.."
	cmake -DCMAKE_TOOLCHAIN_FILE="./armgcc.cmake" -G "Unix Makefiles" -DTURN_ON_STANDALONE_MODE=ON  -DCMAKE_BUILD_TYPE=Release  $extra_cmake_flags .
	echo "#define TURN_ON_STANDALONE_MODE  1" > $file
fi
make -j4
unlink $file
