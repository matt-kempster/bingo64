#!/usr/bin/env bash
# Redeploy relay.py to the live VM — the active-development loop.
#
# There is no backwards-compatible versioning while the protocol is in
# flux: relay.py and the game bump PROTOCOL_VERSION together and old
# peers are refused ("E version"). So a deploy is just: run the tests,
# push the file, restart the service. Everyone in a race gets dropped —
# deploy between races.
#
# Transport, first one that applies:
#   BINGO64_SSH=user@host  ./update.sh     plain ssh/scp (key already set up)
#   BINGO64_ZONE=<zone>    ./update.sh     gcloud, instance "bingo64-relay"
#
# One-time provisioning of a fresh VM is gcp-setup.sh, not this script.
set -euo pipefail
cd "$(dirname "$0")/.."   # server/

INSTANCE="${BINGO64_INSTANCE:-bingo64-relay}"

echo "== tests first: a broken relay never ships =="
python3 test_relay.py 2>&1 | tail -3

if [[ -n "${BINGO64_SSH:-}" ]]; then
    echo "== deploying via ssh to ${BINGO64_SSH} =="
    scp relay.py "${BINGO64_SSH}:/tmp/relay.py"
    ssh "${BINGO64_SSH}" \
        'sudo install -m 644 /tmp/relay.py /opt/bingo64/relay.py \
         && sudo systemctl restart bingo64-relay \
         && sleep 1 \
         && sudo systemctl --no-pager --lines=3 status bingo64-relay'
elif [[ -n "${BINGO64_ZONE:-}" ]]; then
    echo "== deploying via gcloud to ${INSTANCE} (${BINGO64_ZONE}) =="
    gcloud compute scp relay.py "${INSTANCE}:/tmp/relay.py" --zone="${BINGO64_ZONE}"
    gcloud compute ssh "${INSTANCE}" --zone="${BINGO64_ZONE}" --command\
='sudo install -m 644 /tmp/relay.py /opt/bingo64/relay.py \
  && sudo systemctl restart bingo64-relay \
  && sleep 1 \
  && sudo systemctl --no-pager --lines=3 status bingo64-relay'
else
    echo "Set BINGO64_SSH=user@host (plain ssh) or BINGO64_ZONE=<zone> (gcloud)." >&2
    echo "The tunnel address in sm64config.txt is playit, not the VM." >&2
    exit 1
fi

echo "== done. The journal line above should show the new protocol version. =="
