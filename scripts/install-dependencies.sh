#!/usr/bin/env bash

set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
vortex_root="$project_root/third_party/vortex"

cd "$vortex_root"
sudo ./ci/install_dependencies.sh