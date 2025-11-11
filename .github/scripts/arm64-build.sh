#!/usr/bin/env bash
set -euo pipefail

# Optional: echo each major phase for easier log scanning
phase() { echo "::group::$*"; }
endphase() { echo "::endgroup::"; }

phase "Base APT bootstrap"
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  ca-certificates wget gnupg
update-ca-certificates || true
endphase

phase "Enable bullseye-backports"
echo 'deb http://deb.debian.org/debian bullseye-backports main' \
  > /etc/apt/sources.list.d/backports.list
apt-get update
endphase

phase "Install build dependencies (excluding CMake)"
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  g++ git mesa-common-dev libgl1-mesa-dev libgl1 libglx-mesa0 libxext-dev \
  libgtk-3-dev dpkg-dev file ccache ninja-build
endphase

phase "Install CMake from bullseye-backports"
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  -t bullseye-backports cmake
endphase

phase "Verify CMake version"
cmake --version
required=3.25.0
inst="$(cmake --version | awk '/cmake version/{print $3}')"
if ! dpkg --compare-versions "$inst" ge "$required"; then
  echo "CMake version $inst is less than required $required"
  exit 1
fi
endphase

phase "Configure"
cmake -S . -B _build \
  -G Ninja \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCPACK_PACKAGING_INSTALL_PREFIX=/usr \
  -DCMAKE_BUILD_TYPE=Release \
  -DGIT_WXWIDGETS_SUBMODULES=ON \
  -DwxUSE_SYS_LIBS=OFF \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
endphase

phase "Build"
cmake --build _build --target all -j"$(nproc)"
endphase

phase "Test"
ctest --test-dir _build --output-on-failure
endphase

phase "Package"
cmake --build _build --target package -j"$(nproc)"
endphase

phase "Sanitize .deb filenames"
shopt -s nullglob
for file in _build/treesheets_*.deb; do
  base="$(basename "$file")"
  sanitized="$(printf '%s' "$base" | sed -E 's/^(treesheets_)[0-9]+:/\1/; s/:/-/g')"
  if [[ "$sanitized" != "$base" ]]; then
    mv -v "$file" "_build/$sanitized"
  fi
done
endphase

phase "Ownership and cleanup"
# HOST_UID / HOST_GID exported via docker run env or fallback.
uid="${HOST_UID:-1000}"
gid="${HOST_GID:-1000}"
if [[ -z "${HOST_UID:-}" ]] || [[ -z "${HOST_GID:-}" ]]; then
  echo "Warning: HOST_UID or HOST_GID not set, using defaults (uid=$uid, gid=$gid)"
fi
chown -R "$uid:$gid" _build || { echo "Warning: Failed to set ownership on _build"; exit 1; }
apt-get clean
rm -rf /var/lib/apt/lists/*
endphase
