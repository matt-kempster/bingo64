#!/usr/bin/env bash
# Redeploy relay.py to the live VM — the active-development loop.
#
# There is no backwards-compatible versioning while the protocol is in
# flux: relay.py and the game bump PROTOCOL_VERSION together and old
# peers are refused ("E version"). So a deploy is just: run the tests,
# push the file, restart the service. Everyone in a race gets dropped —
# deploy between races.
#
# The live VM (verified 2026-08-16): GCP instance "mario-server",
# zone us-east1-c, IPv6-only (free tier — no external IPv4, do NOT
# attach one; Google bills IPv4 now). Admin access is gcloud SSH over
# an IAP tunnel; game traffic goes through playit.gg, untouched here.
# On the VM: /home/kidpixel/relay.py, run by bingo64.service (user
# kidpixel, --port 64064); playit runs as playit.service.
#
# Default transport is gcloud (needs a logged-in gcloud on PATH).
# Override with BINGO64_SSH=user@host for plain ssh/scp if you ever
# have a direct route (e.g. from an IPv6-capable machine).
#
# One-time provisioning of a fresh VM is gcp-setup.sh, not this script.
set -euo pipefail
cd "$(dirname "$0")/.."   # server/

INSTANCE="${BINGO64_INSTANCE:-mario-server}"
ZONE="${BINGO64_ZONE:-us-east1-c}"
REMOTE_PATH=/home/kidpixel/relay.py
PORT="${BINGO64_PORT:-64064}"
SERVICE=bingo64
DEPLOY_CMD="sudo install -o kidpixel -g kidpixel -m 644 /tmp/relay.py ${REMOTE_PATH} \
  && sudo systemctl restart ${SERVICE} \
  && sleep 1 \
  && sudo systemctl --no-pager --lines=3 status ${SERVICE}"
# The relay's "?" occupancy probe (v6+), asked over TCP on the VM
# itself. A pre-v6 relay never answers; that reads as "can't tell".
PROBE_CMD='python3 -c "
import socket
s = socket.create_connection((\"127.0.0.1\", '"${PORT}"'), timeout=3)
s.sendall(b\"?\n\")
s.settimeout(3)
print(s.recv(256).decode().strip())
" 2>/dev/null || echo probe-no-reply'

echo "== tests first: a broken relay never ships =="
python3 test_relay.py 2>&1 | tail -3

remote() {
    if [[ -n "${BINGO64_SSH:-}" ]]; then
        ssh "${BINGO64_SSH}" "$1"
    else
        gcloud compute ssh "${INSTANCE}" --zone="${ZONE}" \
            --tunnel-through-iap --command="$1"
    fi
}

echo "== occupancy check: a restart drops everyone in a race =="
OCC="$(remote "${PROBE_CMD}" || echo probe-failed)"
echo "relay says: ${OCC}"
if [[ "${OCC}" == *"members="* && "${OCC}" != *"members=0"* ]]; then
    if [[ -z "${BINGO64_FORCE:-}" ]]; then
        echo "ABORT: players are in a room. Deploy between races, or"
        echo "BINGO64_FORCE=1 to drop them on purpose."
        exit 1
    fi
    echo "BINGO64_FORCE set: deploying over live players."
elif [[ "${OCC}" != *"members=0"* ]]; then
    echo "WARNING: no occupancy answer (pre-v6 relay or probe failure);"
    echo "cannot tell if rooms are live. Continuing."
fi

if [[ -n "${BINGO64_SSH:-}" ]]; then
    echo "== deploying via ssh to ${BINGO64_SSH} =="
    scp relay.py "${BINGO64_SSH}:/tmp/relay.py"
    ssh "${BINGO64_SSH}" "${DEPLOY_CMD}"
else
    echo "== deploying via gcloud to ${INSTANCE} (${ZONE}, IAP tunnel) =="
    gcloud compute scp relay.py "${INSTANCE}:/tmp/relay.py" \
        --zone="${ZONE}" --tunnel-through-iap
    gcloud compute ssh "${INSTANCE}" --zone="${ZONE}" --tunnel-through-iap \
        --command="${DEPLOY_CMD}"
fi

echo "== done. Status above should show the service running on the new file. =="
