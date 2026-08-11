#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVICE_DIR="$HOME/.config/systemd/user"
SERVICE_FILE="$SERVICE_DIR/antigravity-telegram.service"
PYTHON="$REPO_DIR/.venv/bin/python"

if [[ ! -x "$PYTHON" ]]; then
  echo "ERROR: .venv belum ada. Jalankan instalasi Python dari README terlebih dahulu." >&2
  exit 1
fi
if [[ ! -f "$REPO_DIR/.env" ]]; then
  echo "ERROR: .env belum ada. Salin .env.example dan isi konfigurasi." >&2
  exit 1
fi

mkdir -p "$SERVICE_DIR"
cat > "$SERVICE_FILE" <<EOF
[Unit]
Description=Antigravity Telegram Bot
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
WorkingDirectory=$REPO_DIR
ExecStart=$PYTHON $REPO_DIR/bot.py
Restart=on-failure
RestartSec=5
TimeoutStopSec=20
Environment=PYTHONUNBUFFERED=1
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=default.target
EOF

systemctl --user daemon-reload
systemctl --user enable --now antigravity-telegram.service

echo
echo "Service terpasang. Cek dengan:"
echo "  systemctl --user status antigravity-telegram.service"
echo "  journalctl --user -u antigravity-telegram.service -f"
echo
echo "Agar service user otomatis hidup setelah reboot tanpa login SSH, jalankan sekali:"
echo "  sudo loginctl enable-linger $USER"
