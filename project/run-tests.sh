#!/usr/bin/env bash

set -euo pipefail

# Pass any flags (e.g. --update-snapshots) through to run-test.sh
EXTRA_FLAGS=()
for arg in "$@"; do
    if [[ "$arg" == "--update-snapshots" || "$arg" == "-u" ]]; then
        EXTRA_FLAGS+=("$arg")
    fi
done

./build.sh

./run-test.sh ${EXTRA_FLAGS[@]+"${EXTRA_FLAGS[@]}"} "it_should_fail_on_too_long_argument" ./myvmm -v "$(printf '%*s' -4097 '' | tr ' ' 'A')"
