#!/usr/bin/env bash

set -euo pipefail

# ==============================================================================
# Configuration Variables
# ==============================================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SNAPSHOT_DIR="${SNAPSHOT_DIR:-$SCRIPT_DIR/tests/snapshots}"

# ==============================================================================
# Argument Parsing
# ==============================================================================
UPDATE_SNAPSHOTS=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --update-snapshots|-u)
            UPDATE_SNAPSHOTS=1
            shift
            ;;
        *)
            break
            ;;
    esac
done

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 [--update-snapshots|-u] <test_name> <command...>" >&2
    exit 2
fi

TEST_NAME="$1"
shift

# Ensure the snapshot directory exists
mkdir -p "$SNAPSHOT_DIR"

SNAPSHOT_FILE="$SNAPSHOT_DIR/${TEST_NAME}.snapshot"
TEMP_OUTPUT="$(mktemp)"
trap 'rm -f "$TEMP_OUTPUT"' EXIT

# Execute command chain and capture combined stdout and stderr
set +e
"$@" > "$TEMP_OUTPUT" 2>&1
COMMAND_EXIT_CODE=$?
set -e

# If snapshot doesn't exist or update requested, write snapshot
if [[ ! -f "$SNAPSHOT_FILE" || "$UPDATE_SNAPSHOTS" -eq 1 ]]; then
    cp "$TEMP_OUTPUT" "$SNAPSHOT_FILE"
    echo "⚠️ Snapshot saved for ${TEST_NAME}: ${SNAPSHOT_FILE}"

    echo "--- Snapshot start ---"
    cat $SNAPSHOT_FILE
    echo "---- Snapshot end ----"
    echo ""
    exit 0
fi

# Compare run output against saved snapshot
if cmp -s "$TEMP_OUTPUT" "$SNAPSHOT_FILE"; then
    echo "✅ Test for ${TEST_NAME} passed"
    exit 0
else
    echo "❌ Test for ${TEST_NAME} failed"
    echo "--- Side-by-side Diff (Left: Snapshot | Right: Current Run) ---"
    diff -y "$SNAPSHOT_FILE" "$TEMP_OUTPUT" || true
    echo ""
    exit 0
fi
