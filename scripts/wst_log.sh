#!/bin/bash
# wst_log.sh — Start a timestamped WiFiSerialTap logging session
# Usage: ./wst_log.sh [device-ip]

DEVICE_IP="${1:-192.168.1.184}"
LOG_DIR="$HOME/log"
LOG_FILE="$LOG_DIR/wst_session.log"

# Ensure dependencies
if ! command -v ts &>/dev/null; then
    echo "Installing moreutils (for 'ts' command)..."
    sudo apt install -y moreutils
fi

# Create log directory
mkdir -p "$LOG_DIR"

echo "Connecting to WiFiSerialTap at $DEVICE_IP:23"
echo "Logging to $LOG_FILE"
echo "Press Ctrl+C to stop"
echo "---"

nc "$DEVICE_IP" 23 | ts '[%Y-%m-%d %H:%M:%S]' | tee -a "$LOG_FILE"
