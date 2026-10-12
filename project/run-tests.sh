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
./run-test.sh "$@" "it_should_skip_blank_lines_between_instructions" ./myvmm -v tests/blank_lines_assembly_file
./run-test.sh "$@" "it_should_follow_last_of_duplicate_config" ./myvmm -v tests/duplicate_config_assembly_file
./run-test.sh "$@" "it_should_skip_when_no_vm_configured" ./myvmm -d
./run-test.sh "$@" "it_should_parse_absolute_paths" ./myvmm -d -v /home/johnbunag/Documents/remote-cmpe-283-virtual-technologies/project/tests/absolute_path_assembly_file
./run-test.sh "$@" "it_should_create_snapshot_with_mixed_paths" ./myvmm -v mixed_paths_assembly_file
./run-test.sh "$@" "it_should_restore_snapshot_with_mixed_paths" ./myvmm -v mixed_paths_assembly_file -s /home/johnbunag/Documents/remote-cmpe-283-virtual-technologies/project/tests/mixed_paths_snapshot
./run-test.sh "$@" "it_should_create_duplicate_snapshots" ./myvmm -v tests/duplicate_snapshot_assembly_file
./run-test.sh "$@" "it_should_follow_last_of_duplicate_snapshot" ./myvmm -v tests/duplicate_snapshot_assembly_file -s tests/duplicate_snapshot_first_snapshot -s tests/duplicate_snapshot_second_snapshot


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

# It should support snapshots
./run-test.sh "$@" "it_should_create_single_snapshot" ./myvmm -d -v tests/single_snapshot_assembly_file
./run-test.sh "$@" "it_should_restore_single_snapshot" ./myvmm -d -v tests/single_snapshot_assembly_file -s tests/single_snapshot_snapshot
./run-test.sh "$@" "it_should_create_snapshot_with_mutations" ./myvmm -d -v tests/snapshot_mutations_assembly_file
./run-test.sh "$@" "it_should_restore_snapshot_with_mutations" ./myvmm -d -v tests/snapshot_mutations_assembly_file -s tests/snapshot_mutations_snapshot
./run-test.sh "$@" "it_should_create_multiple_snapshots" ./myvmm -v tests/multiple_snapshots_assembly_file
./run-test.sh "$@" "it_should_restore_multiple_snapshots_first_snapshot" ./myvmm -v tests/multiple_snapshots_assembly_file -s tests/multiple_snapshots_first_snapshot
./run-test.sh "$@" "it_should_restore_multiple_snapshots_second_snapshot" ./myvmm -v tests/multiple_snapshots_assembly_file -s tests/multiple_snapshots_second_snapshot
./run-test.sh "$@" "it_should_handle_skip_instructions_exceeding_length" ./myvmm -d -v tests/skip_instructions_exceeded_assembly_file -s tests/skip_instructions_exceeded_snapshot
./run-test.sh "$@" "it_should_restore_snapshot_with_blank_lines" ./myvmm -v tests/spaced_snapshot_assembly_file -s tests/spaced_snapshot_snapshot
./run-test.sh "$@" "it_should_create_snapshot_for_vm_snapshot" ./myvmm -v assembly_file_vm_snapshot
./run-test.sh "$@" "it_should_restore_snapshot_for_vm_snapshot" ./myvmm -v assembly_file_vm_snapshot -s snapshot_vm_snapshot
./run-test.sh "$@" "it_should_create_snapshot_with_absolute_path" ./myvmm -v /home/johnbunag/Documents/remote-cmpe-283-virtual-technologies/project/tests/absolute_path_snapshot_assembly_file
./run-test.sh "$@" "it_should_restore_snapshot_with_absolute_path" ./myvmm -v /home/johnbunag/Documents/remote-cmpe-283-virtual-technologies/project/tests/absolute_path_snapshot_assembly_file -s /home/johnbunag/Documents/remote-cmpe-283-virtual-technologies/project/tests/absolute_path_snapshot_snapshot
./run-test.sh "$@" "it_should_create_snapshots_for_multiple_vms" ./myvmm -v tests/multiple_vms_snapshot_vm1_assembly_file -v tests/multiple_vms_snapshot_vm2_assembly_file
./run-test.sh "$@" "it_should_restore_snapshots_for_multiple_vms" ./myvmm -v tests/multiple_vms_snapshot_vm1_assembly_file -s tests/multiple_vms_snapshot_vm1_snapshot -v tests/multiple_vms_snapshot_vm2_assembly_file -s tests/multiple_vms_snapshot_vm2_snapshot
./run-test.sh "$@" "it_should_warn_on_unknown_snapshot_instruction" ./myvmm -v tests/snapshot_unknown_instruction_assembly_file -s tests/snapshot_unknown_instruction_snapshot

# It should handle snapshot errors
./run-test.sh "$@" "it_should_fail_on_non_existent_snapshot" ./myvmm -v tests/noop_assembly_file -s tests/non_existent_snapshot
