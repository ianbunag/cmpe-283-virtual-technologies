#!/usr/bin/env bash

set -euo pipefail

./build.sh

#@TODO re-enable
#./run-test.sh "it_should_run_assembly_file_vm1" ./myvmm -v assembly_file_vm1
#./run-test.sh "it_should_run_assembly_file_vm2" ./myvmm -v assembly_file_vm2
#./run-test.sh "it_should_run_assembly_file_vm1_and_vm2" ./myvmm -v assembly_file_vm1 -v assembly_file_vm2
./run-test.sh "it_should_exit_on_no_argument" ./myvmm
./run-test.sh "it_should_fail_on_too_long_argument" ./myvmm -v "$(printf '%*s' -4097 '' | tr ' ' 'A')"
./run-test.sh "it_should_fail_on_non_existent_assembly_file" ./myvmm -v tests/non_existent_assembly_file_vm
./run-test.sh "it_should_fail_on_non_existent_succeeding_assembly_file" ./myvmm -v noop_assembly_file -v tests/non_existent_assembly_file_vm
./run-test.sh "it_should_fail_on_empty_vm_binary" ./myvmm -v tests/empty_vm_binary
./run-test.sh "it_should_fail_on_non_existent_vm_binary" ./myvmm -v tests/non_existent_vm_binary
./run-test.sh "it_should_fail_on_non_existent_succeeding_vm_binary" ./myvmm -v noop_assembly_file -v tests/non_existent_vm_binary
./run-test.sh "it_should_print_debug_statements" ./myvmm -d -v tests/debug_vm_binary

# noop with print
