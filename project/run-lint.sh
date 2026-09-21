./build.sh

echo "----- Performing SAST with flawfinder -----"
flawfinder myvmm.c

echo ""
echo "------ Performing DAST with Valgrind ------"
valgrind ./myvmm -v noop_assembly_file -v noop_assembly_file
