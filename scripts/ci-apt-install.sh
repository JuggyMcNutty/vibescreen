#!/usr/bin/env bash
#
# Install apt packages on a GitHub runner without hanging the job.
#
# The runner ships a mirrorlist at /etc/apt/apt-mirrors.txt with
# azure.archive.ubuntu.com at priority:1 and the canonical archives behind it.
# apt only fails over to the next mirror on a hard error, never on a slow one,
# so when the Azure mirror degrades a download trickles instead of failing.
# apt's own Acquire::http::Timeout is an inactivity timeout, and a server
# dribbling a few bytes at a time never trips it, so the fetch blocks forever.
# With no job timeout that is a six hour wedge rather than a build failure.
#
# So bound each attempt on wall clock time rather than on inactivity, and drop
# the Azure mirror before retrying so the canonical archive takes over. The
# first attempt keeps Azure, which is normally by far the fastest from a runner.
#
# Usage: scripts/ci-apt-install.sh <package>...

set -euo pipefail

if [ "$#" -eq 0 ]; then
    echo "usage: $0 <package>..." >&2
    exit 2
fi

MIRRORS=/etc/apt/apt-mirrors.txt
ATTEMPTS=3

# Long enough for a healthy mirror to be nowhere near it: a good run fetches
# these in one or two seconds, and even the degraded 57 kB/s case above would
# finish 25 MB inside the install budget.
UPDATE_TIMEOUT=240
INSTALL_TIMEOUT=300

for attempt in $(seq 1 "$ATTEMPTS"); do
    if [ "$attempt" -gt 1 ] && [ -f "$MIRRORS" ] \
       && grep -q azure.archive.ubuntu.com "$MIRRORS"; then
        echo "dropping azure.archive.ubuntu.com from the mirrorlist"
        sudo sed -i '/azure\.archive\.ubuntu\.com/d' "$MIRRORS"
        cat "$MIRRORS"
    fi

    if timeout "$UPDATE_TIMEOUT" sudo apt-get update -o Acquire::Retries=3 \
       && timeout "$INSTALL_TIMEOUT" sudo apt-get install -y \
            --no-install-recommends \
            -o Acquire::Retries=3 \
            -o Acquire::http::Timeout=30 \
            "$@"; then
        exit 0
    fi

    echo "::warning::apt attempt $attempt of $ATTEMPTS stalled or failed"
    sleep 10
done

echo "::error::could not install after $ATTEMPTS attempts: $*"
exit 1
