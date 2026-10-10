#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FM1 Move: fetch the JieLi toolchain and the three AC79 SDK files the package needs (sparse checkout, ~100 KB
# instead of the whole SDK). Prints the two variables build.sh and tests/run_tests.sh read.
#   tools/get_deps.sh [DEST]      (default: ~/.fm1move-deps)
set -e
DEST="${1:-$HOME/.fm1move-deps}"
mkdir -p "$DEST"
[ -x "$DEST/jieli/toolchain/pi32v2/bin/clang" ] || sh "$(dirname "$0")/get_toolchain.sh" "$DEST/jieli"
if [ ! -f "$DEST/ac79sdk/cpu/wl82/tools/uboot.boot" ]; then
    git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 --filter=blob:none --sparse \
        https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$DEST/ac79sdk"
    git -C "$DEST/ac79sdk" sparse-checkout set --no-cone /cpu/wl82/tools/uboot.boot /cpu/wl82/tools/cfg_tool.bin \
        /cpu/wl82/tools/cfg/eq_cfg_hw.bin
fi
echo "export JIELI_TOOLCHAIN=$DEST/jieli/toolchain"
echo "export AC79_SDK=$DEST/ac79sdk"
