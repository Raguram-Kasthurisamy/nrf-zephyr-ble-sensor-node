#!/usr/bin/env bash
# Author: Raguram Kasthurisamy
# Creates your own MCUboot image-signing key (ECDSA P-256) in keys/. Keep it out of git:
# anyone holding it can build firmware that your bootloader will accept.
set -euo pipefail

WORKSPACE="$(west topdir)"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
KEY="$REPO/keys/signing-key.pem"

mkdir -p "$REPO/keys"
if [[ -e "$KEY" ]]; then
  echo "$KEY already exists; not overwriting it."
else
  python3 "$WORKSPACE/bootloader/mcuboot/scripts/imgtool.py" keygen -k "$KEY" -t ecdsa-p256
  echo "Created $KEY"
fi
echo
echo "Build with your key:"
echo "  west build -b nrf7002dk/nRF5340 app --sysbuild -- -DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=\\\"$KEY\\\""
