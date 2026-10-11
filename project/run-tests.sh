#!/usr/bin/env bash

set -euo pipefail

./build.sh

# It should run example files
./run-test.sh "$@" "it_should_run_assembly_file_vm1" ./myvmm -v assembly_file_vm1
./run-test.sh "$@" "it_should_run_assembly_file_vm2" ./myvmm -v assembly_file_vm2
./run-test.sh "$@" "it_should_run_assembly_file_vm1_and_vm2" ./myvmm -v assembly_file_vm1 -v assembly_file_vm2

# It should be configurable
./run-test.sh "$@" "it_should_print_debug_statements" ./myvmm -d -v tests/debug_assembly_file

# It should handle configuration errors
./run-test.sh "$@" "it_should_exit_on_no_argument" ./myvmm
./run-test.sh "$@" "it_should_fail_on_too_long_argument" ./myvmm -v "$(printf '%*s' -4097 '' | tr ' ' 'A')"
./run-test.sh "$@" "it_should_fail_on_non_existent_assembly_file" ./myvmm -v tests/non_existent_assembly_file
./run-test.sh "$@" "it_should_fail_on_non_existent_succeeding_assembly_file" ./myvmm -v tests/noop_assembly_file -v tests/non_existent_assembly_file
./run-test.sh "$@" "it_should_fail_on_empty_vm_binary" ./myvmm -v tests/empty_vm_binary_assembly_file
./run-test.sh "$@" "it_should_fail_on_non_existent_vm_binary" ./myvmm -v tests/non_existent_vm_binary_assembly_file
./run-test.sh "$@" "it_should_fail_on_non_existent_succeeding_vm_binary" ./myvmm -v tests/noop_assembly_file -v tests/non_existent_vm_binary_assembly_file

# It should support configuration quirks
./run-test.sh "$@" "it_should_parse_spaced_vm_file" ./myvmm -v tests/spaced_assembly_file
./run-test.sh "$@" "it_should_follow_last_of_duplicate_config" ./myvmm -v tests/duplicate_config_assembly_file
./run-test.sh "$@" "it_should_skip_when_no_vm_configured" ./myvmm -d
./run-test.sh "$@" "it_should_parse_absolute_paths" ./myvmm -d -v /home/johnbunag/Documents/remote-cmpe-283-virtual-technologies/project/tests/absolute_path_assembly_file

# It should support instructions
./run-test.sh "$@" "it_should_load_immediate" ./myvmm -v tests/load_immediate_assembly_file
./run-test.sh "$@" "it_should_add" ./myvmm -v tests/add_assembly_file
./run-test.sh "$@" "it_should_subtract" ./myvmm -v tests/subtract_assembly_file
./run-test.sh "$@" "it_should_add_immediate" ./myvmm -v tests/add_immediate_assembly_file
./run-test.sh "$@" "it_should_add_unsigned" ./myvmm -v tests/add_unsigned_assembly_file
./run-test.sh "$@" "it_should_subtract_unsigned" ./myvmm -v tests/subtract_unsigned_assembly_file
./run-test.sh "$@" "it_should_add_immediate_unsigned" ./myvmm -v tests/add_immediate_unsigned_assembly_file
./run-test.sh "$@" "it_should_multiply_without_overflow" ./myvmm -v tests/multiply_without_overflow_assembly_file
./run-test.sh "$@" "it_should_multiply" ./myvmm -v tests/multiply_assembly_file
./run-test.sh "$@" "it_should_divide" ./myvmm -v tests/divide_assembly_file
./run-test.sh "$@" "it_should_and" ./myvmm -v tests/and_assembly_file
./run-test.sh "$@" "it_should_or" ./myvmm -v tests/or_assembly_file
./run-test.sh "$@" "it_should_and_immediate" ./myvmm -v tests/and_immediate_assembly_file
./run-test.sh "$@" "it_should_or_immediate" ./myvmm -v tests/or_immediate_assembly_file
./run-test.sh "$@" "it_should_shift_left_logical" ./myvmm -v tests/shift_left_logical_assembly_file
./run-test.sh "$@" "it_should_shift_right_logical" ./myvmm -v tests/shift_right_logical_assembly_file
./run-test.sh "$@" "it_should_xor" ./myvmm -v tests/xor_assembly_file

# It should handle instruction errors
./run-test.sh "$@" "it_should_ignore_out_of_bounds_register" ./myvmm -v tests/out_of_bounds_register_assembly_file
./run-test.sh "$@" "it_should_ignore_write_to_zero_register" ./myvmm -v tests/zero_register_assembly_file
