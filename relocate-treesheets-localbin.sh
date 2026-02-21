#!/usr/bin/env bash
set -euo pipefail

PKG="treesheets"

# Must be run with sudo
if [[ "${EUID}" -ne 0 ]]; then
  echo "ERROR: run with sudo: sudo $0" >&2
  exit 1
fi
if [[ -z "${SUDO_USER:-}" || "${SUDO_USER}" == "users" ]]; then
  echo "ERROR: run via sudo from the target user session (SUDO_USER must be set)." >&2
  exit 1
fi

USER_NAME="${SUDO_USER}"
HOME_DIR="$(getent passwd "${USER_NAME}" | cut -d: -f6)"
if [[ -z "${HOME_DIR}" || ! -d "${HOME_DIR}" ]]; then
  echo "ERROR: could not determine home directory for user '${USER_NAME}'." >&2
  exit 1
fi

DEST="${HOME_DIR}/.local/bin"

# Required end-state groups
GROUP_DIRS_FILES="users"
GROUP_EXE="users"

# Ensure package is installed
if ! rpm -q "${PKG}" >/dev/null 2>&1; then
  echo "ERROR: RPM package '${PKG}' is not installed." >&2
  exit 1
fi

mkdir -p "${DEST}"

# Backup OUTSIDE ~/.local/bin to keep the final tree exact
ts="$(date +%Y%m%d%H%M%S)"
BACKUP_BASE="${HOME_DIR}/treesheets-old-versions/${ts}"
mkdir -p "${BACKUP_BASE}"

# If any of the target top-level items already exist, move them to backup (outside DEST)
need_backup=0
for item in docs examples images lib scripts translations readme.html readme-ko.html readme-zh_CN.html TreeSheets; do
  if [[ -e "${DEST}/${item}" || -L "${DEST}/${item}" ]]; then
    need_backup=1
    break
  fi
done

if [[ "${need_backup}" -eq 1 ]]; then
  for item in docs examples images lib scripts translations readme.html readme-ko.html readme-zh_CN.html TreeSheets; do
    if [[ -e "${DEST}/${item}" || -L "${DEST}/${item}" ]]; then
      mv -f "${DEST}/${item}" "${BACKUP_BASE}/"
    fi
  done
  chown -R rangelma "${BACKUP_BASE}" || true
  echo "NOTE: Existing TreeSheets payload moved to backup: ${BACKUP_BASE}" >&2
fi

echo "==> Copying RPM-owned files from '${PKG}' into: ${DEST} (stripping /usr/ and dereferencing symlinks)"

# Read the RPM file list; preserve spaces safely
mapfile -t FILES < <(rpm -ql "${PKG}" | sort -u)

for src in "${FILES[@]}"; do
  [[ -z "${src}" ]] && continue

  # This package layout is under /usr/*
  [[ "${src}" == /usr/* ]] || continue

  rel="${src#/usr/}"       # e.g., TreeSheets, docs/..., images/..., lib/...
  dst="${DEST}/${rel}"

  if [[ -d "${src}" ]]; then
    mkdir -p "${dst}"
  elif [[ -e "${src}" || -L "${src}" ]]; then
    mkdir -p "$(dirname "${dst}")"
    # -L dereferences symlinks (prevents broken links after RPM removal)
    cp -aL "${src}" "${dst}"
  else
    echo "NOTE: listed by RPM but missing on disk (skipping): ${src}" >&2
  fi
done

# Ensure the expected top-level dirs exist (in case RPM omits empty dirs)
mkdir -p "${DEST}/docs" "${DEST}/examples" "${DEST}/images" "${DEST}/lib" "${DEST}/scripts" "${DEST}/translations"

echo "==> Applying exact ownership + permissions (scoped to the TreeSheets payload only)"

# Directories and their contents: rangelma:users, dirs 0755, files 0644
for d in docs examples images lib scripts translations; do
  if [[ -d "${DEST}/${d}" ]]; then
    chown -R rangelma "${DEST}/${d}"
    find "${DEST}/${d}" -type d -exec chmod 0755 {} +
    find "${DEST}/${d}" -type f -exec chmod 0644 {} +
  fi
done

# Readmes at top-level: rangelma:users 0644
for f in readme.html readme-ko.html readme-zh_CN.html; do
  if [[ -f "${DEST}/${f}" ]]; then
    chown -R rangelma "${DEST}/${f}"
    chmod 0644 "${DEST}/${f}"
  fi
done

# Main executable: rangelma:users 0777 (as per your required listing)
if [[ -e "${DEST}/TreeSheets" ]]; then
  chown -R rangelma "${DEST}/TreeSheets"
  chmod 0777 "${DEST}/TreeSheets"
else
  echo "ERROR: ${DEST}/TreeSheets not created. Check: rpm -ql ${PKG} | grep -i TreeSheets" >&2
  exit 1
fi

# Optional SELinux context restore (keeps the '.' indicator you see in ls -l on SELinux systems)
if command -v selinuxenabled >/dev/null 2>&1 && selinuxenabled; then
  if command -v restorecon >/dev/null 2>&1; then
    restorecon -RF "${DEST}/docs" "${DEST}/examples" "${DEST}/images" "${DEST}/lib" "${DEST}/scripts" "${DEST}/translations" \
      "${DEST}/TreeSheets" "${DEST}/readme.html" "${DEST}/readme-ko.html" "${DEST}/readme-zh_CN.html" 2>/dev/null || true
  fi
fi

echo "==> Removing system RPM '${PKG}' to complete the move (no --clean-deps)"
zypper -n rm "${PKG}"

echo
echo "==> Final tree under ${DEST}:"
ls -l "${DEST}"

echo
echo "Launch:"
echo "  ${DEST}/TreeSheets"