#!/usr/bin/env bash

set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
vortex_root="$project_root/third_party/vortex"
vortex_build="$vortex_root/build"
toolchain_dir="$project_root/.vortex-tools"
ccache_dir="$project_root/.cache/ccache"
pocl_cache_dir="$project_root/.cache/pocl"
toolchain_rev=$(awk -F= '$1 == "TOOLCHAIN_REV" {print $2}' "$vortex_root/VERSION")
toolchain_stamp="$toolchain_dir/.installed-$toolchain_rev"

git -C "$project_root" submodule update --init --recursive

mkdir -p "$vortex_build"
mkdir -p "$ccache_dir"
mkdir -p "$pocl_cache_dir"
cd "$vortex_build"
../configure --xlen=32 --tooldir="$toolchain_dir"
if [[ ! -f "$toolchain_stamp" || ! -x "$toolchain_dir/llvm-vortex/bin/clang" ]]; then
    ./ci/toolchain_install.sh
    touch "$toolchain_stamp"
fi
env -u DEBUG CCACHE_DIR="$ccache_dir" make -s -C ../third_party softfloat ramulator
env -u DEBUG CCACHE_DIR="$ccache_dir" make -s -C sw/kernel
env -u DEBUG CCACHE_DIR="$ccache_dir" make -s -C sw/runtime/stub
env -u DEBUG CCACHE_DIR="$ccache_dir" make -s -C sw/runtime/simx
env -u DEBUG CCACHE_DIR="$ccache_dir" make install

mkdir -p "$vortex_build/tests/regression/quantum-gate"

echo "Vortex is ready. Run the tuned 15-qubit configuration with:"
echo "  make vortex-simx"

