#!/bin/sh
# v29 Spire follow the leader with the city threshold, and the checkpoint lives store (regie 2026-10-24, B-E01-52):
# a partner still in the city takes the Ragol warp itself; the lives of a previous run never reach LastLives.

set -e
EXECUTABLE="$1"

check() {
  GOT=$($EXECUTABLE spire-follow-check "$1" "$2" $4 2>/dev/null)
  if [ "$GOT" != "$3" ]; then
    echo "FAIL: partner on floor $1, leader on floor $2: expected $3, got $GOT"
    exit 1
  fi
  echo "ok: partner floor $1, leader floor $2 -> $GOT"
}

check 0 1 skip     # the partner is in the city: it takes the Ragol warp itself (route 1, q920/q929)
check 0 11 skip
check 1 2 warp     # a room transition during the run
check 2 1 warp
check 1 11 warp    # into a boss floor
check 11 12 warp
check 1 1 skip     # already there
check 1 0 skip     # never to the city (B-E01-41)
check 0 0 skip
check 1 2 skip loading  # audit S-12: a partner that is still loading is never warped
check 15 2 skip    # floor 0x0F: no floor reported yet

GOT=$($EXECUTABLE spire-lives-check 2>/dev/null)
[ "$GOT" = ok ] || { echo "FAIL: spire-lives-check: $GOT"; exit 1; }
echo "ok: spire-lives-check"
