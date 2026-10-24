#!/bin/sh
# v27 SpireBlockedCharacters: a real character is blocked by console serial plus name; the test characters on the same
# console, other consoles and non-Spire quests stay allowed (regie 2026-10-24, Wakiru on the Wii).

set -e
EXECUTABLE="$1"
CONFIG=tests/spire-blocked-characters-config.json
trap 'rm -f tests/spire-blocked-characters-config.json' EXIT

sed '1s#^{#{\n  "SpireBlockedCharacters": {"Quests": [912, 920, 921, 925, 929, 931, 955], "Characters": [{"Serial": 3117539659, "Name": "Wakiru"}, {"Serial": 1234, "Name": ""}]},#' tests/config.json > $CONFIG

check() {
  GOT=$($EXECUTABLE spire-blocked-check --config=$CONFIG "$1" "$2" "$3" 2>/dev/null)
  if [ "$GOT" != "$4" ]; then
    echo "FAIL: serial $1 name '$2' q$3: expected $4, got $GOT"
    exit 1
  fi
  echo "ok: serial $1 name '$2' q$3 $GOT"
}

check 3117539659 Wakiru 929 blocked
check 3117539659 Wakiru 920 blocked
check 3117539659 Wakiru 955 blocked
check 3117539659 "	EWakiru" 912 blocked
check 3117539659 wakiru 925 blocked
check 3117539659 Kamako 929 allowed
check 3117539659 BidX 920 allowed
check 3117539666 Wakiru 929 allowed
check 3117539659 Wakiru 58 allowed
check 3117539659 Wakiru 930 allowed
check 1234 Kamako 929 blocked
check 1234 Kamako 58 allowed

# Without the key nothing is blocked
GOT=$($EXECUTABLE spire-blocked-check --config=tests/config.json 3117539659 Wakiru 929 2>/dev/null)
[ "$GOT" = allowed ] || { echo "FAIL: without the key: got $GOT"; exit 1; }
echo "ok: without the key allowed"

# An empty Characters list is a config error, not a silent no-op
sed '1s#^{#{\n  "SpireBlockedCharacters": {"Quests": [929], "Characters": []},#' tests/config.json > $CONFIG
if $EXECUTABLE spire-blocked-check --config=$CONFIG 3117539659 Wakiru 929 >/dev/null 2>&1; then
  echo "FAIL: empty Characters accepted"; exit 1
fi
echo "ok: empty Characters refused"
