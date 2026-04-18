#!/bin/bash
# Build and run IPC tests under QEMU mps2-an386.
# Usage: ./run_tests.sh [--no-build]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

if [ "$1" != "--no-build" ]; then
    echo "=== Building IPC test ==="
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    ARMGCC_DIR="${ARMGCC_DIR:-/usr}"
    cmake \
        -DCMAKE_TOOLCHAIN_FILE="${SCRIPT_DIR}/../../armgcc.cmake" \
        -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release \
        "${SCRIPT_DIR}"
    make -j4
    cd "${SCRIPT_DIR}"
fi

ELF="${BUILD_DIR}/ipc_test.elf"
if [ ! -f "${ELF}" ]; then
    echo "ERROR: ${ELF} not found. Run without --no-build first."
    exit 1
fi

echo ""
echo "=== Running under QEMU ==="
qemu-system-arm \
    -machine mps2-an386 \
    -nographic \
    -serial stdio \
    -semihosting-config enable=on,target=native \
    -kernel "${ELF}" \
    -m 16M \
    -no-reboot

EXIT_CODE=$?
echo ""
if [ ${EXIT_CODE} -eq 0 ]; then
    echo "=== QEMU: test process exited cleanly ==="
else
    echo "=== QEMU: test process exited with code ${EXIT_CODE} ==="
fi
exit ${EXIT_CODE}
