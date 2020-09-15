#Create an image
truncate -s 64K test.img
echo "test.img created"

#Format the image
./mkfs.a1fs -i 16 test.img
echo "test.img is formatted with 16 inodes"

#Mount the image
mkdir mnt
chmod 777 mnt
./a1fs test.img mnt
echo -e "my_fs is the mounted ROOT directory\n"

#Performance
cd mnt

#mkdir
echo "---------- Test creating directory ----------"
echo "--- create directory /a1 ---"
mkdir a1
cd a1
echo "--- create directory /a1/a2 ---"
mkdir a2
cd ..
echo -e "\n"

#stat
echo "---------- Test stat ----------"
echo "--- status of ROOT ---"
stat .
echo "--- status of /a1 ---"
stat ./a1
echo "--- status of /a1/a2 ---"
stat ./a1/a2
echo -e "\n"

#creat files
echo "---------- Test Creating files ----------"
echo "--- create 1.txt ---"
touch 1.txt
echo "--- create 2.txt ---"
touch 2.txt
echo "--- create 3.txt ---"
touch 3.txt
echo -e "\n"

#write and read files
echo "---------- File manipulation: READ & WRITE ----------"
echo "--- write into 1.txt ---"
echo "This is the content in 1.txt" > 1.txt
cat 1.txt
echo "--- append to 1.txt ---"
echo "This is a huge assignment~" >> 1.txt
cat 1.txt
echo "--- write into 2.txt ---"
echo "This is the content in 2.txt" > 2.txt
cat 2.txt
echo "--- overwrite the content in 2.txt"
echo "Overwriting..." > 2.txt
cat 2.txt
echo -e "\n"

#ls
echo "---------- Read directory ----------"
ls -la
echo -e "\n"

#truncate
echo "---------- Test truncate ----------"
echo "--- truncate 1.txt to 100 bytes ---"
truncate -s 100 1.txt
stat 1.txt
cat 1.txt
echo "--- truncate 1.txt to 5 bytes ---"
truncate -s 5 1.txt
stat 1.txt
cat 1.txt
echo -e "\n"

#unlink
echo "---------- Test Deleting file ----------"
echo "--- unlink/remove 1.txt ---"
unlink 1.txt
ls -la
echo -e "\n"

#rmdir
echo "---------- Delete directory ----------"
echo "--- delete /a1/a2"
rm -rf ./a1/a2
echo "--- Here is /a1 ---"
ls -la ./a1
echo -e "\n"

#rename
echo "---------- Test Rename file/directories ----------"
mkdir hi

echo "--- Rename directory from hi to hello---"
mv hi hello

echo "---------- Test Rename file/directories ----------"
touch a.txt

echo "--- Rename file a.txt to b.txt---"
mv a.txt b.txt

#unmount
echo "---------- Unmounting file system ----------"
cd ..
fusermount -u mnt
echo "file system mnt unmounted"
echo -e "\n"

echo "---------- Mount mnt again to test persistency ----------"
#mount again
./a1fs test.img mnt
cd mnt
ls -la