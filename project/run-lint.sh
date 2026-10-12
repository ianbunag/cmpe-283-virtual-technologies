./build.sh

echo "----- Performing SAST with flawfinder -----"
flawfinder myvmm.c

echo ""
echo "------ Performing DAST with Valgrind ------"
valgrind --leak-check=full ./myvmm -v assembly_file_vm1 -v assembly_file_vm2 -v assembly_file_vm_snapshot -v assembly_file_vm_snapshot -s snapshot_vm_snapshot
