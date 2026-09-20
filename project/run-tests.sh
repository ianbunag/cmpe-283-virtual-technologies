#!/usr/bin/env bash

set -euo pipefail

./build.sh

./run-test.sh "it_should_exit_on_no_argument" ./myvmm
./run-test.sh "it_should_fail_on_too_long_argument" ./myvmm -v "$(printf '%*s' -4097 '' | tr ' ' 'A')"
./run-test.sh "it_should_fail_on_non_existent_assembly_file" ./myvmm -v tests/non_existent_assembly_file_vm
./run-test.sh "it_should_fail_on_non_existent_succeeding_assembly_file" -v noop_assembly_file -v tests/non_existent_assembly_file_vm
./run-test.sh "it_should_fail_on_non_existent_vm_binary" ./myvmm -v tests/non_existent_vm_binary_assembly_file
./run-test.sh "it_should_fail_on_non_existent_succeeding_vm_binary" ./myvmm -v noop_assembly_file -v non_existent_succeeding_vm_binary_assembly_file

# noop with print
