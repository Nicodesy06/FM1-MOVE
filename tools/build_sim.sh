#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FM1 Move in the browser: compile web/sim/fm1sim.c (the firmware's sources, FM1MOVE=1) to WebAssembly with clang's
# wasm32 target (no Emscripten, no libc) into docs/sim/fm1sim.wasm. Needs the generated headers in build/gen
# (any ./build.sh, or the generators below run here when missing).
set -e
cd "$(dirname "$0")/.."
GEN=build/gen
mkdir -p "$GEN" docs/sim
[ -f "$GEN/felucca_tables.h" ] || python3 tools/gen_tables.py "$GEN/felucca_tables.h"
[ -f "$GEN/felucca_samples.h" ] || python3 tools/gen_samples.py "$GEN/felucca_samples.h"
[ -f "$GEN/felucca_drumkits.h" ] || python3 tools/gen_drumkits.py "$GEN/felucca_drumkits.h"
[ -f "$GEN/felucca_fm6.h" ] || python3 tools/gen_fm6_patches.py "$GEN/felucca_fm6.h"
python3 tools/gen_dsfont.py "$GEN/ds_fonts.h" >/dev/null
EXPORTS="sim_init sim_boot sim_new sim_demo sim_audio sim_ui sim_set sim_turn sim_led sim_fb sim_pcm_ptr sim_nor sim_fmp_base sim_fmp_size sim_store_base sim_store_size sim_flash_dirty sim_bpm sim_playing sim_rx_ptr sim_rx sim_tx_ptr sim_tx_len sim_tx_clear"
EX=""
for e in $EXPORTS; do EX="$EX -Wl,--export=$e"; done
# --global-base: the firmware's register addresses (all below 1 MiB) land on unused memory, never on our data
clang --target=wasm32 -O2 -ffreestanding -nostdlib -fno-builtin -w \
    -I"$GEN" -Ifirmware/src -Ifirmware/hal \
    -Wl,--no-entry -Wl,--global-base=1048576 -Wl,-z,stack-size=1048576 -Wl,--initial-memory=33554432 \
    $EX -o docs/sim/fm1sim.wasm web/sim/fm1sim.c
# the page asks for this exact build (a new one is never answered from a browser's cache) and names its version
H=$(sha256sum docs/sim/fm1sim.wasm | cut -c1-12)
V=$(sed -n 's/^#define FELUCCA_VERSION "\(FM1 MOVE [^"]*\)".*/\1/p' firmware/src/ui.c)
sed -i -e "s/fm1sim\.wasm?v=[A-Za-z0-9]*/fm1sim.wasm?v=$H/" -e "s/<span class=\"pill\" id=\"fwver\"\([^>]*\)>[^<]*</<span class=\"pill\" id=\"fwver\"\1>$V</" docs/sim/index.html
ls -l docs/sim/fm1sim.wasm; echo "sim page: fm1sim.wasm?v=$H, $V"
