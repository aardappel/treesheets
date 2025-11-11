#!/usr/bin/env bash
set -euo pipefail

phase() { echo "::group::$*"; }
endphase() { echo "::endgroup::"; }

phase "Base APT bootstrap"
# Allow archived repos (expired metadata) and explicit insecure repos for archive.debian.org
printf 'Acquire::Check-Valid-Until "false";\nAcquire::AllowInsecureRepositories "true";\n' > /etc/apt/apt.conf.d/99archive
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  ca-certificates wget gnupg debian-archive-keyring
update-ca-certificates || true
endphase

phase "Configure archived Debian bullseye sources (main, updates, security, backports)"
# Use HTTP for archive endpoints to avoid TLS friction in minimal images
cat > /etc/apt/sources.list <<'EOF'
deb http://archive.debian.org/debian bullseye main
deb http://archive.debian.org/debian bullseye-updates main
deb http://archive.debian.org/debian-security bullseye-security main
EOF
echo 'deb http://archive.debian.org/debian bullseye-backports main' \
  > /etc/apt/sources.list.d/backports.list
apt-get update
endphase

phase "Install base build dependencies (bullseye)"
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential git mesa-common-dev libgl1-mesa-dev libgl1 libglx-mesa0 libxext-dev \
  libgtk-3-dev dpkg-dev file ccache ninja-build libjpeg-dev libtiff-dev
endphase

phase "Install newer compiler from bullseye-backports (gcc-11/g++-11)"
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  -t bullseye-backports gcc-11 g++-11
update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-11 110 \
                     --slave /usr/bin/gcc gcc /usr/bin/gcc-11
update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-11 110 \
                     --slave /usr/bin/g++ g++ /usr/bin/g++-11
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

phase "Configure (use gcc-11/g++-11 and static libstdc++)"
# Ensure CMake picks the newer compilers and link libstdc++/libgcc statically for the executable
cmake -S . -B _build \
  -G Ninja \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCPACK_PACKAGING_INSTALL_PREFIX=/usr \
  -DCMAKE_BUILD_TYPE=Release \
  -DGIT_WXWIDGETS_SUBMODULES=ON \
  -DwxUSE_SYS_LIBS=OFF \
  -DCMAKE_C_COMPILER=/usr/bin/gcc-11 \
  -DCMAKE_CXX_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
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
uid="${HOST_UID:-1000}"
gid="${HOST_GID:-1000}"
if [[ -z "${HOST_UID:-}" ]] || [[ -z "${HOST_GID:-}" ]] ; then
  echo "Warning: HOST_UID or HOST_GID not set, using defaults (uid=$uid, gid=$gid)"
fi
chown -R "$uid:$gid" _build || { echo "Warning: Failed to set ownership on _build"; exit 1; }
apt-get clean
rm -rf /var/lib/apt/lists/*
endphase
