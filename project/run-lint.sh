./build.sh

echo "----- Performing SAST with flawfinder -----"
flawfinder myvmm.c

echo -e "\n"
echo "------ Performing DAST with Valgrind ------"
valgrind ./myvmm -v assembly_file_vm1
