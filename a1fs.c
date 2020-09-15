/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>

#include "a1fs.h"
#include "helper.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"


//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
	// Nothing to initialize if only printing help or version
	if (opts->help || opts->version) return true;

	size_t size;
	void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
	if (!image) return false;

	return fs_ctx_init(fs, image, size, opts);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx*)ctx;
	if (fs->image) {
		if (fs->opts->sync && (msync(fs->image, fs->size, MS_SYNC) < 0)) {
			perror("msync");
		}
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx*)fuse_get_context()->private_data;
}


/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st)
{
	(void)path;// unused
	fs_ctx *fs = get_fs();

	assert(fs != NULL);
	assert(fs->image != NULL);

	void *image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image;
	memset(st, 0, sizeof(*st));

	//TODO: fill in the rest of required fields based on the information stored
	// in the superblock
	st->f_bsize   = A1FS_BLOCK_SIZE; /* File system block size*/
	st->f_frsize  = A1FS_BLOCK_SIZE; /* Fragment size */
	st->f_bfree   = superblock->free_blocks_count;
	st->f_bavail  = superblock->free_blocks_count;
	st->f_files   = superblock->inodes_count;
	st->f_ffree   = superblock->free_inodes_count;
	st->f_favail  = superblock->free_inodes_count;
	st->f_namemax = A1FS_NAME_MAX;

	return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the stat() system call. See "man 2 stat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors).
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */


static int a1fs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= A1FS_PATH_MAX) return -ENAMETOOLONG;
	fs_ctx *fs = get_fs();
    
	//TODO: lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode
    void *image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image;
    a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);

    //initialize all the field of st
	memset(st, 0, sizeof(*st));

	//store the path on heap
	//char *path_heap = strdup(path);
	//if(path_heap == NULL){return -ENOMEM;}

	a1fs_ino_t inode_num_found = 0;
	inode_num_found = find_inode((char *)path,root,image);
	if(inode_num_found == (superblock->inodes_count + 1)){return -ENOTDIR;}
	else if(inode_num_found == (superblock->inodes_count + 2)){return -ENOENT;}
	
    a1fs_inode *curr_inode = &root[inode_num_found];
	
	//if the current inode is a dir
	if(curr_inode->type == 0){
		st->st_mode = S_IFDIR | curr_inode->mode;
	}else if(curr_inode->type == 1){
		//if the current inode is a file
		st->st_mode = S_IFREG | curr_inode->mode;
	}

	st->st_size = curr_inode->size;
	st->st_blocks = curr_inode->size / 512 + (curr_inode->size % 512 > 0 ? 1 : 0);
	st->st_nlink = curr_inode->links;
	st->st_mtime = curr_inode->mtime.tv_sec;


	return 0;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler() for each directory
 * entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls)
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
	(void)offset;// unused
	(void)fi;// unused
	fs_ctx *fs = get_fs(); 


	//NOTE: This is just a placeholder that allows the file system to be mounted
	// without errors. You should remove this from your implementation.
	// if (strcmp(path, "/") == 0) {
	// 	filler(buf, "." , NULL, 0);
	// 	filler(buf, "..", NULL, 0);
	// 	return 0;
	// }

	//TODO: lookup the directory inode for given path and iterate through its
	// directory entries
	void*image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image;
	a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);

	//store the path on heap
	char *path_heap = strdup(path);
	if(path_heap == NULL){return -ENOMEM;}

	a1fs_ino_t inode_num_found = (a1fs_ino_t)find_inode(path_heap,root,image);
	if(inode_num_found == (superblock->inodes_count + 1)){return -ENOTDIR;}
	else if(inode_num_found == (superblock->inodes_count + 2)){return -ENOENT;}

	free(path_heap);
	
    a1fs_inode *target_inode = &root[inode_num_found];
	a1fs_extent *extent_blk = (a1fs_extent *)(image + target_inode->block_no * A1FS_BLOCK_SIZE);
	// how many dentry in this inode
    uint64_t dentry_num = target_inode->size / sizeof(a1fs_dentry);
	//how many blocks the dentry table occupied
    uint64_t dblock_count = target_inode->size / A1FS_BLOCK_SIZE + (target_inode->size % A1FS_BLOCK_SIZE > 0 ? 1 : 0);
    int dentries_inlastblk = (int)dentry_num % (A1FS_BLOCK_SIZE / sizeof(a1fs_dentry));
	//loop over all extents
    for (int extent_count = 0; extent_count < 512 - (int)(target_inode->free_extent_num); extent_count++){
        a1fs_blk_t start_blk_no = (extent_blk + extent_count * sizeof(a1fs_extent))->start;
        size_t *start_blk_ptr = (size_t *)(image + A1FS_BLOCK_SIZE * start_blk_no);
        int blk_in_extent = (int)((extent_blk + extent_count * sizeof(a1fs_extent))->count);
        //loop over all blocks in this extent
        for (int curr = 0; curr < blk_in_extent; curr++){
			dblock_count--;
            a1fs_dentry *start_dentry = (a1fs_dentry *)(start_blk_ptr + curr * A1FS_BLOCK_SIZE);
            int dentry_count = (int)(A1FS_BLOCK_SIZE/sizeof(a1fs_dentry));
            //check whether we are at the last block and the block is not full of dentries
            if (dblock_count == 0 && dentries_inlastblk != 0){
                dentry_count = dentries_inlastblk;
            }
            //loop over all dentries in this block
            for (int count = 0; count < dentry_count; count ++){
                a1fs_dentry *curr_dentry = (a1fs_dentry *)(&start_dentry[count]);
				//check if the dentry is . or ..
				// if (strcmp(curr_dentry->name, ".") == 0 || strcmp(curr_dentry->name, "..") == 0){
				// 	continue;
				// }
				int err_no = filler(buf, curr_dentry->name, NULL, 0);
                if (err_no != 0) return err_no;
            }
        }
    }

	return 0;
}


/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * NOTE: the mode argument may not have the type specification bits set, i.e.
 * S_ISDIR(mode) can be false. To obtain the correct directory type bits use
 * "mode | S_IFDIR".
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
	fs_ctx *fs = get_fs();

	assert(fs != NULL);
	assert(fs->image != NULL);
	
	//get the image 
	void* image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image; 

	//check if there's enough memory
	if (superblock->free_inodes_count <= 0 && superblock->free_blocks_count <= 0) {return -ENOSPC;}

	a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);
	//the name of the directory we need to create
	char path_cpy[strlen(path)+1];
	strncpy(path_cpy,path,strlen(path)+1);
	// if(path_cpy == NULL) {return -ENOMEM;}
	char *filename = basename((char*)path);
	char *parent_dir = dirname((char*)path_cpy);
	
	printf("filename: %s\n", filename);
	printf("parent directory: %s\n", parent_dir);

	//find the inode number of the parent directory according to the given path
	a1fs_ino_t parent_ino = find_inode(parent_dir, root, image);

	//create the new directory at given path with given mode
	a1fs_ino_t new_ino = create_inode(mode, parent_ino, image, 0);
	write_dentry(filename, new_ino, parent_ino, image);
	//update parent links
	a1fs_inode *parent_inode = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE + parent_ino * A1FS_INODE_SIZE);
	parent_inode->links += 1;

	// (void)path;
	// (void)mode;
	// (void)fs;
	//free(path_cpy);
	return 0;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
	fs_ctx *fs = get_fs();

	//get the image 
	void* image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image; 

	a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);

	//the name of the directory we need to remove
	char *path_cpy = strdup(path);
	if(path_cpy == NULL) {return -ENOMEM;}
	char *filename = basename((char*)path);
	char *parent_dir = dirname(path_cpy);
	
	printf("filename: %s\n", filename);
	printf("parent directory: %s\n", parent_dir);

	//find the inode number of the parent directory according to the given path
	a1fs_ino_t parent_ino = (a1fs_ino_t)find_inode(parent_dir, root, image);
	a1fs_inode *parent_inode = (a1fs_inode *)(image + superblock->inode_table_start * A1FS_BLOCK_SIZE + parent_ino * A1FS_INODE_SIZE);


	//find the dentry and the inode with <filename>
	a1fs_dentry *target_dentry = find_dentry(parent_inode, filename, image);
	a1fs_ino_t target_ino = target_dentry->ino;
	a1fs_inode *target_inode = (a1fs_inode *)(image + superblock->inode_table_start * A1FS_BLOCK_SIZE + target_ino * A1FS_INODE_SIZE);

	//check if the directory is not empty
	if (target_inode->size > 2 * sizeof(a1fs_dentry)) return -ENOTEMPTY;

	//free the inode in the dentry and its extent block and its only data block in this inode
	unsigned char *blk_bitmap = (unsigned char *)(image + superblock->block_bitmap_start * A1FS_BLOCK_SIZE);
	unsigned char *ino_bitmap = (unsigned char *)(image + superblock->inode_bitmap_start * A1FS_BLOCK_SIZE);
	a1fs_extent *last_extent = (a1fs_extent *)(image + target_inode->block_no * A1FS_BLOCK_SIZE);
	a1fs_blk_t last_db = last_extent->start;
	assert(last_extent->count == 1);
	set_bit(blk_bitmap, 1, last_db, 0, superblock); //free the only data block
	set_bit(blk_bitmap, 1, target_inode->block_no, 0, superblock); //free the extent block
	set_bit(ino_bitmap, 0, target_ino, 0, superblock); //free the inode

	//promote the last dentry in parent inode to offset the target_dentry
	promote_last_dentry(parent_inode, target_dentry, image);

	//update parent links
	parent_inode->links -= 1;
	
	free(path_cpy);
	return 0;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;// unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	//get the image 
	void* image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image; 

	//check if there's enough memory
	if (superblock->free_inodes_count <= 0 && superblock->free_blocks_count <= 0) {return -ENOSPC;}

	a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);

	//the name of the file we need to create
	char *path_cpy = strdup(path);
	if(path_cpy == NULL) {return -ENOMEM;}
	char *filename = basename((char*)path);
	char *parent_dir = dirname(path_cpy);
	
	printf("filename: %s\n", filename);
	printf("parent directory: %s\n", parent_dir);

	//find the inode number of the parent directory according to the given path
	a1fs_ino_t parent_ino = (a1fs_ino_t)find_inode(parent_dir, root, image);
	//create the new file at given path with given mode
	a1fs_ino_t new_ino = create_inode(mode, parent_ino, image, 1);

	//test remember to clear
	a1fs_inode *new = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE + new_ino*sizeof(a1fs_inode));
	printf("type: %d\n",new->type);
	write_dentry(filename, new_ino, parent_ino, image);
	
	free(path_cpy);

	return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	//get the image 
	void* image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image; 

	a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);

	//the name of the file we need to remove
	char *path_cpy = strdup(path);
	if(path_cpy == NULL) {return -ENOMEM;}
	char *filename = basename((char*)path);
	char *parent_dir = dirname(path_cpy);
	
	printf("filename: %s\n", filename);
	printf("parent directory: %s\n", parent_dir);

	//find the inode number of the parent directory according to the given path
	a1fs_ino_t parent_ino = find_inode(parent_dir, root, image);
	a1fs_inode *parent_inode = (a1fs_inode *)(image + superblock->inode_table_start * A1FS_BLOCK_SIZE + parent_ino * A1FS_INODE_SIZE);


	//find the dentry and the inode with <filename>
	a1fs_dentry *target_dentry = find_dentry(parent_inode, filename, image);
	a1fs_ino_t target_ino = target_dentry->ino;
	a1fs_inode *target_inode = (a1fs_inode *)(image + superblock->inode_table_start * A1FS_BLOCK_SIZE + target_ino * A1FS_INODE_SIZE);

	//check if the directory is not empty
	if (target_inode->size > 2 * sizeof(a1fs_dentry)) return -ENOTEMPTY;

	//free the inode in the dentry and its extent block and its only data block in this inode
	unsigned char *blk_bitmap = (unsigned char *)(image + superblock->block_bitmap_start * A1FS_BLOCK_SIZE);
	unsigned char *ino_bitmap = (unsigned char *)(image + superblock->inode_bitmap_start * A1FS_BLOCK_SIZE);
	
	//free all data blocks!!!
	free_data(target_inode, blk_bitmap,image);

	set_bit(blk_bitmap, 1, target_inode->block_no, 0, superblock); //free the extent block
	set_bit(ino_bitmap, 0, target_ino, 0, superblock); //free the inode

	//promote the last dentry in parent inode to offset the target_dentry
	promote_last_dentry(parent_inode, target_dentry, image);
	
	free(path_cpy);
	return 0;
}

/**
 * Rename a file or directory.
 *
 * Implements the rename() system call. See "man 2 rename" for details.
 * If the destination file (directory) already exists, it must be replaced with
 * the source file (directory). Existing destination can be replaced if it's a
 * file or an empty directory.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "from" exists.
 *   The parent directory of "to" exists and is a directory.
 *   If "from" is a file and "to" exists, then "to" is also a file.
 *   If "from" is a directory and "to" exists, then "to" is also a directory.
 *
 * Errors:
 *   ENOMEM     not enough memory (e.g. a malloc() call failed).
 *   ENOTEMPTY  destination is a non-empty directory.
 *   ENOSPC     not enough free space in the file system.
 *
 * @param from  original file path.
 * @param to    new file path.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rename(const char *from, const char *to)
{
	fs_ctx *fs = get_fs();

	//TODO: move the inode (file or directory) at given source path to the
	// destination path, according to the description above
	//get the image 
	void* image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image; 

	//check if there's enough memory
	if (superblock->free_inodes_count <= 0 && superblock->free_blocks_count <= 0) {return -ENOSPC;}
	a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);
	unsigned char *blk_bitmap = (unsigned char *)(image + superblock->block_bitmap_start * A1FS_BLOCK_SIZE);
	unsigned char *ino_bitmap = (unsigned char *)(image + superblock->inode_bitmap_start * A1FS_BLOCK_SIZE);

	//get info from the source path

	char *src_base = strdup(from);
	char *src_par = strdup(from);

	if(src_base == NULL || src_par == NULL) {return -ENOMEM;}

	char *src_target = basename(src_base);
	char *src_parent = dirname(src_par);
	//get the parent inode in from
	a1fs_ino_t src_parent_ino = find_inode(src_parent, root, image);
	a1fs_inode *src_parent_inode = (a1fs_inode *)(image + superblock->inode_table_start * A1FS_BLOCK_SIZE + src_parent_ino * A1FS_INODE_SIZE);

	//get info from the destination path
	char *dest_base = strdup(to);
	char *dest_par = strdup(to);

	if(dest_base == NULL || dest_par == NULL) {return -ENOMEM;}
	char *dest_target = basename(dest_base);
	char *dest_parent = dirname(dest_par);
	a1fs_ino_t dest_parent_ino = find_inode(dest_parent, root, image);
	a1fs_inode *dest_parent_inode = (a1fs_inode *)(image + superblock->inode_table_start * A1FS_BLOCK_SIZE + dest_parent_ino * A1FS_INODE_SIZE);
	
	//test
	printf("src_target: %s\n", src_target);
	printf("src parent directory: %s\n", src_parent);

	printf("dest_target: %s\n", dest_target);
	printf("dest_parent directory: %s\n", dest_parent);

	//find the src inode, and set a flag to record its type
	a1fs_ino_t src_ino = find_inode((char*)from, root, image);
	a1fs_inode *src_inode = (a1fs_inode *)(image + superblock->inode_table_start * A1FS_BLOCK_SIZE + src_ino * A1FS_INODE_SIZE);
	int flag = -1;

	flag = src_inode->type;

	//check if the destination exists
	a1fs_ino_t check_dest = find_inode((char*)to, root, image);
	a1fs_ino_t dest_ino;
	a1fs_inode *dest_inode;

	//if the destination does not exist
	if (check_dest == (superblock->inodes_count + 2)){
		// write the inode <src_ino> of src_target to dest_parent with the new filename <dest_target>
		write_dentry((const char *)dest_target, src_ino, dest_parent_ino, image);
		//find the src_inode in its parent and delete the entry
		a1fs_dentry *src_dentry = find_dentry(src_parent_inode, src_target, image);
		promote_last_dentry(src_parent_inode, src_dentry, image);

		free(src_base);
		free(src_par);
		free(dest_base);
		free(dest_par);
		return 0;

	//else the destination exists
	} else{
		dest_ino = (a1fs_ino_t)check_dest;
		dest_inode = (a1fs_inode *)(image + superblock->inode_table_start * A1FS_BLOCK_SIZE + dest_ino * A1FS_INODE_SIZE);

		// check if the destination is not empty directory
		if (flag == 0 && dest_inode->size > 2 * sizeof(a1fs_dentry)) return -ENOTEMPTY;
		
		//else we replace the destination dentry with the source
		a1fs_dentry *dest_dentry = find_dentry(dest_parent_inode, dest_target, image);
		dest_dentry->ino = src_ino;
		strncpy(dest_dentry->name, (const char *)src_target, strlen((const char *)src_target));

		//find the src_inode in its parent and delete the entry
		a1fs_dentry *src_dentry = find_dentry(src_parent_inode, src_target, image);
		promote_last_dentry(src_parent_inode, src_dentry, image);
	}

	//if dest_inode is empty directory, update links of src_parent and dest_parent and free the inode
	if(flag == 0 && check_dest != (superblock->inodes_count + 2)){
		src_parent_inode->links -= 1;
		//update the .. dentry of the src_inode
		a1fs_dentry *parent_dentry = find_dentry(src_inode, "..", image);
		parent_dentry->ino = dest_parent_ino;
		//free the dest_inode and its data
		a1fs_extent *last_extent = (a1fs_extent *)(image + dest_inode->block_no * A1FS_BLOCK_SIZE);
		a1fs_blk_t last_db = last_extent->start;
		assert(last_extent->count == 1);
		set_bit(blk_bitmap, 1, last_db, 0, superblock); //free the only data block
		set_bit(blk_bitmap, 1, dest_inode->block_no, 0, superblock); //free the extent block
		set_bit(ino_bitmap, 0, dest_ino, 0, superblock); //free the inode

	//if dest_inode is file, free the inode and all its data blocks
	} else if (flag == 1 && check_dest != (superblock->inodes_count + 2)){
		free_data(dest_inode, blk_bitmap,image);
		set_bit(blk_bitmap, 1, dest_inode->block_no, 0, superblock); //free the extent block
		set_bit(ino_bitmap, 0, dest_ino, 0, superblock); //free the inode
	}

	//free the src and dest in the heap
	free(src_base);
	free(src_par);
	free(dest_base);
	free(dest_par);
	return 0;
}


/**
 * Change the access and modification times of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only have to implement the setting of modification time (mtime).
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * @param path  path to the file or directory.
 * @param tv    timestamps array. See "man 2 utimensat" for details.
 * @return      0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec tv[2])
{
	fs_ctx *fs = get_fs();
	assert(fs != NULL);
	assert(fs->image != NULL);
	void *image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image;
	a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);

	//TODO: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page
	//find the inode number according to the given path
	a1fs_ino_t inode_number = find_inode((char *)path,root,image);
	a1fs_inode * inode_ptr = &root[inode_number];

	inode_ptr->mtime.tv_sec = tv[1].tv_sec;
	inode_ptr->mtime.tv_nsec = tv[1].tv_nsec;

	// (void)path;
	// (void)tv;
	// (void)fs;
	return 0;
}


/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, future reads from the new uninitialized range must
 * return ranges filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
	static int a1fs_truncate(const char *path, off_t size)
	{
	fs_ctx *fs = get_fs();
	assert(fs != NULL);
	assert(fs->image != NULL);
	void *image = (void*)(fs->image);

	a1fs_superblock *superblock = (a1fs_superblock *)(fs->image);
	a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);
	a1fs_ino_t inode_num = find_inode((char*)path,root,image);
	a1fs_inode *inode = &root[inode_num];

	//TODO: set new file size, possibly "zeroing out" the uninitialized range
	//if it is not a file
	if(inode->type == 0){return EISDIR;}
	
	int block_num = (inode->size) / A1FS_BLOCK_SIZE + (((inode->size) % A1FS_BLOCK_SIZE) > 0 ? 1 : 0);
	//If we use all the current data block
	size_t max_current_size = block_num * A1FS_BLOCK_SIZE;

	//if the size is greater than the size we can maximum store currently
	if(size > (int)max_current_size){
	size_t size_allocate = (size_t)size - max_current_size;
	extend_data(size_allocate,inode,image);
	}

	if(size < (int)inode->size){
	size_t size_cut = inode->size - (size_t)size;
	shrink_data(size_cut,inode,image);
	}

	inode->size = size;
	update_mtime(inode,image);
	return 0;
	}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Should return exactly the number of bytes
 * requested except on EOF (end of file) or error, otherwise the rest of the
 * data will be substituted with zeros. Reads from file ranges that have not
 * been written to must return ranges filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//get the image 
	void* image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image; 

	a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);

	//the name of the file we need to remove
	char *path_cpy = strdup(path);
	if(path_cpy == NULL) {return -ENOMEM;}

	//find the inode number of the target file according to the given path
	a1fs_ino_t target_ino = find_inode(path_cpy, root, image);
	a1fs_inode *inode = &root[target_ino];

	//check if the offset is beyond EOF
	size_t u_offset = (size_t)(offset);
	if (inode->size <= u_offset) return 0;

	//calculate the starting position to write
	uint32_t offset_blk = u_offset / A1FS_BLOCK_SIZE;
	uint64_t offset_byte = u_offset % A1FS_BLOCK_SIZE;

	//calculate num of bytes can be read from offset to EOF
	size_t byte_remain = inode->size - u_offset;

	a1fs_extent *start_extent = (a1fs_extent *)(image + A1FS_BLOCK_SIZE * inode->block_no);
	int extent_num = (int)(512 - inode->free_extent_num);
	uint64_t read_len = 0;

	for (int extent_offset = 0; extent_offset < extent_num; extent_offset++) {
		a1fs_extent *curr_extent = &start_extent[extent_offset];
		if (curr_extent->count <= offset_blk){
			offset_blk -= curr_extent->count;
			continue;
		}

		//find the starting byte in this extent
		a1fs_blk_t start_blk = (&start_extent[extent_offset])->start;
		unsigned char *start_ptr = image + start_blk * A1FS_BLOCK_SIZE;
		uint64_t byte_in_extent = curr_extent->count * A1FS_BLOCK_SIZE;

		//if at the extent containing start point
		if (read_len == 0){
			start_blk = start_blk + offset_blk;
			start_ptr = image + start_blk * A1FS_BLOCK_SIZE + offset_byte * sizeof(char);
			byte_in_extent = byte_in_extent - offset_blk * A1FS_BLOCK_SIZE - offset_byte * sizeof(char);
		}

		//start read from start_ptr
		for (uint64_t i = 0; i < byte_in_extent; i++){
			if (byte_remain != 0){
				memcpy(buf + read_len * sizeof(char), &start_ptr[i], 1);
				read_len++;
				byte_remain--;
			}

			//if finished read <size> bytes
			if (read_len == size){
				free(path_cpy);
				return read_len;
			}

			//if have read all bytes remaining
			if (byte_remain == 0) break;
		}

		//if have read all bytes remaining
		if (byte_remain == 0) break;
	}

	//reach this step only when we need to add 0's to the end
	uint64_t zeros_byte = size - read_len;
	memset(&buf[read_len], 0, zeros_byte);
	
	free(path_cpy);
	return read_len;
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Should return exactly the number of
 * bytes requested except on error. If the offset is beyond EOF (end of file),
 * the file must be extended. If the write creates a "hole" of uninitialized
 * data, future reads from the "hole" must return ranges filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
	(void)fi;// unused
	fs_ctx *fs = get_fs();

	//TODO: write data from the buffer into the file at given offset, possibly
	// "zeroing out" the uninitialized range
	//get the image 
	void* image = (void*)(fs->image);
	a1fs_superblock *superblock = (a1fs_superblock *)image; 

	a1fs_inode *root = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);

	//the name of the file we need to remove
	char *path_cpy = strdup(path);
	if(path_cpy == NULL) {return -ENOMEM;}

	//find the inode number of the target file according to the given path
	a1fs_ino_t target_ino = find_inode(path_cpy, root, image);
	a1fs_inode *inode = &root[target_ino];

	//check if the offset is beyond EOF
	size_t u_offset = (size_t)(offset);
	if (inode->size <= u_offset){
		int ret = a1fs_truncate(path_cpy, size + offset);
		if (ret != 0) {
			free(path_cpy);
			return ret;
		}
	}

	//calculate the starting position to write
	uint32_t offset_blk = u_offset / A1FS_BLOCK_SIZE;
	uint64_t offset_byte = u_offset % A1FS_BLOCK_SIZE;
	

	a1fs_extent *start_extent = (a1fs_extent *)(image + A1FS_BLOCK_SIZE * inode->block_no);
	int extent_num = (int)(512 - inode->free_extent_num);
	uint64_t write_len = 0;

	for (int extent_offset = 0; extent_offset < extent_num; extent_offset++) {
		a1fs_extent *curr_extent = &start_extent[extent_offset];
		if (curr_extent->count <= offset_blk){
			offset_blk -= curr_extent->count;
			continue;
		}

		//find the starting byte
		a1fs_blk_t start_blk = (&start_extent[extent_offset])->start;
		char *start_ptr = image + start_blk * A1FS_BLOCK_SIZE;
		uint64_t byte_in_extent = curr_extent->count * A1FS_BLOCK_SIZE;
		//if at the extent containing start point
		if (write_len == 0){
			start_blk = start_blk + offset_blk;
			start_ptr = image + start_blk * A1FS_BLOCK_SIZE + offset_byte * sizeof(char);
			byte_in_extent = byte_in_extent - offset_blk * A1FS_BLOCK_SIZE - offset_byte * sizeof(char);
		}

		//start write from start_ptr
		for (uint64_t i = 0; i < byte_in_extent; i++){
			strncpy(&start_ptr[i], &buf[write_len], 1);
			write_len++;

			//if finished write
			if (write_len == size){
				free(path_cpy);
				return write_len;
			}
		}
	}

	free(path_cpy);
	return -ENOSYS;
}


static struct fuse_operations a1fs_ops = {
	.destroy  = a1fs_destroy,
	.statfs   = a1fs_statfs,
	.getattr  = a1fs_getattr,
	.readdir  = a1fs_readdir,
	.mkdir    = a1fs_mkdir,
	.rmdir    = a1fs_rmdir,
	.create   = a1fs_create,
	.unlink   = a1fs_unlink,
	.rename   = a1fs_rename,
	.utimens  = a1fs_utimens,
	.truncate = a1fs_truncate,
	.read     = a1fs_read,
	.write    = a1fs_write,
};

int main(int argc, char *argv[])
{
	a1fs_opts opts = {0};// defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!a1fs_opt_parse(&args, &opts)) return 1;

	fs_ctx fs = {0};
	if (!a1fs_init(&fs, &opts)) {
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}