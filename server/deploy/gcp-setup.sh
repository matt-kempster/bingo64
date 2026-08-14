#!/usr/bin/env bash
# One-shot setup for the bingo64 relay on a GCP e2-micro (free tier) VM.
# Run on the VM with relay.py and bingo64-relay.service in the same directory:
#   gcloud compute scp server/relay.py server/deploy/gcp-setup.sh \
#       server/deploy/bingo64-relay.service bingo64-relay:~ --zone=<zone>
#   gcloud compute ssh bingo64-relay --zone=<zone> --command='bash gcp-setup.sh'
# (Port 64064 is opened via a VPC firewall rule, not on the VM — GCE images
# ship with no local firewall, unlike Oracle's.)
set -euo pipefail
cd "$(dirname "$0")"

# Unprivileged user + install dir.
sudo useradd --system --shell /usr/sbin/nologin bingo 2>/dev/null || true
sudo mkdir -p /opt/bingo64
sudo cp relay.py /opt/bingo64/relay.py

# Run under systemd: starts on boot, restarts on crash.
sudo cp bingo64-relay.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now bingo64-relay

sleep 1
sudo systemctl --no-pager status bingo64-relay
echo
echo "Relay is up. Public endpoint: $(curl -s ifconfig.me):64064"
