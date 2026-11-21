#!/usr/bin/env bash
set -euo pipefail

DOWNLOAD_DIR="/home/andi/Downloads"
REPO_DIR="/root/FragOS"

# Match both clean filenames and browser-renamed duplicates like foo(1).zip or foo (2).zip
LATEST_ZIP="$(find "${DOWNLOAD_DIR}" -maxdepth 1 -type f -name 'fragOS-utils-v1.7.1*.zip' -printf '%T@ %p
' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2- || true)"

if [[ -z "${LATEST_ZIP}" ]]; then
    LATEST_ZIP="$(find "${DOWNLOAD_DIR}" -maxdepth 1 -type f -name 'fragOS-utils*.zip' -printf '%T@ %p
' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2- || true)"
fi

if [[ -z "${LATEST_ZIP}" ]]; then
    echo "Error: No fragOS-utils*.zip found in ${DOWNLOAD_DIR}" >&2
    exit 1
fi

echo "==> Found archive: ${LATEST_ZIP}"
mkdir -p "${REPO_DIR}"
unzip -o "${LATEST_ZIP}" -d "${REPO_DIR}"

cd "${REPO_DIR}"
chmod 0755 "${REPO_DIR}"

if [[ ! -d .git ]]; then
    git init -b main
    git remote add origin https://github.com/andiallen1979/FragOS.git
fi

git add .
if ! git diff --cached --quiet; then
    MSG="Update FragOS overlay from $(basename "${LATEST_ZIP}")"
    git commit -m "${MSG}"
    git push -u origin main
else
    echo "==> No changes to commit in ${REPO_DIR}"
fi

echo "==> Syncing Gentoo Portage FragOS repository..."
emaint sync -r FragOS

echo "==> Emerging app-misc/fragOS-utils (source build)..."
emerge -av app-misc/fragOS-utils
