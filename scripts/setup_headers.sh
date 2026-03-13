#!/bin/bash
# Downloads and sets up AOSP headers required to build Stratum
# Run this once before building

set -e

HEADERS_DIR=$(dirname "$0")/../include

if [ -d "$HEADERS_DIR/v34" ]; then
    echo "[*] Headers already set up, skipping."
    exit 0
fi

echo "[!] Stratum requires AOSP vendor headers to build."
echo "[!] These are large and device-specific."
echo "[!] Options:"
echo "    1) Copy from an existing vendor dump (recommended)"
echo "    2) Pull from a running Android 14 device via adb"
echo ""
echo "Manual setup:"
echo "  Place your v34/arm64 headers in: $HEADERS_DIR/v34"
echo "  Place your logging headers in:   $HEADERS_DIR/logging"
echo "  Place your native headers in:    $HEADERS_DIR/native"
echo ""
echo "See README.md for full instructions."
