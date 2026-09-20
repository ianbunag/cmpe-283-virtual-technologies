./build.sh

echo "----- Performing SAST with flawfinder -----"
flawfinder myvmm.c

echo ""
echo "------ Performing DAST with Valgrind ------"
valgrind ./myvmm -v assembly_file_vm1
