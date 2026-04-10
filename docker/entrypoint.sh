#!/usr/bin/env bash
set -euo pipefail

# Virtual framebuffer for GLFW (no physical display in the container).
if [[ -z "${DISPLAY:-}" ]]; then
  export DISPLAY=:99
fi
if ! xdpyinfo -display "${DISPLAY}" >/dev/null 2>&1; then
  Xvfb "${DISPLAY}" -screen 0 1280x720x24 -nolisten tcp &
  XPID=$!
  sleep 0.4
  trap 'kill "${XPID}" 2>/dev/null || true' EXIT
fi

# Prefer Lavapipe (CPU Vulkan) unless the user set VK_ICD_FILENAMES (e.g. NVIDIA image).
if [[ -z "${VK_ICD_FILENAMES:-}" ]]; then
  for icd in /usr/share/vulkan/icd.d/lvp_icd.x86_64.json \
             /usr/share/vulkan/icd.d/lvp_icd.aarch64.json; do
    if [[ -f "${icd}" ]]; then
      export VK_ICD_FILENAMES="${icd}"
      break
    fi
  done
fi

exec /app/build/physisim "$@"
