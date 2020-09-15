# Extent-based File System

### Introduction
An extent is a contiguous set of blocks allocated to a file, and is defined by the starting block number and the number of blocks in the extent. Each file or directory in our file system could have at most 512 extents.

### Functionalities
- formatting the disk image (mkfs)
- creating and deleting directories (mkdir, rmdir)
- creating and deleting files (creat, unlink)
- writing data to files and reading data from files (read, write)
- displaying metadata about a file or directory (stat)

### Potential Problems
We have not test complicate cases yet,  so there may be errors when executing commands in complicate cases. We are especially uncertain about the read/write operations.

### Testing
There is a shell script called ```runit.sh``` that demonstrates some functionality of this file system.

### Other

Thank you for your time to read our assignment, we know it's quite heavy work and we've try our best to write meaningful comments. We also decide to retain the print statements in create_inode, which we initially used to debug, but it may be convenient for you to test our work. Wish you a merry semester : )
