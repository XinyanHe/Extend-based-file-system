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
 * CSC369 Assignment 1 - a1fs formatting tool.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "a1fs.h"
#include "map.h"
#include "util.h"
#include "helper.h"


/** Command line options. */
typedef struct mkfs_opts {
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Sync memory-mapped image file contents to disk. */
	bool sync;
	/** Verbose output. If false, the program must only print errors. */
	bool verbose;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -s      sync image file contents to disk\n\
    -v      verbose output\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfsvz")) != -1) {
		switch (o) {
			case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

			case 'h': opts->help    = true; return true;// skip other arguments
			case 'f': opts->force   = true; break;
			case 's': opts->sync    = true; break;
			case 'v': opts->verbose = true; break;
			case 'z': opts->zero    = true; break;

			case '?': return false;
			default : assert(false);
		}
	}

	if (optind >= argc) {
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (opts->n_inodes == 0) {
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
	//TODO: check if the image already contains a valid a1fs superblock
    assert(image != NULL);

    a1fs_superblock *superblock = (a1fs_superblock *)image;
    if (A1FS_MAGIC == superblock->magic) {
        return true;
    }
    
	return false;
}


/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	//TODO: initialize the superblock and create an empty root directory
    is_aligned(size,A1FS_BLOCK_SIZE);
    
    if(opts->n_inodes <= 1){
           return false;
       }
    
    //calculate how many inodes can be stored in a block
    uint64_t inodes_in_block = A1FS_BLOCK_SIZE/A1FS_INODE_SIZE;
    
    //calculate the number of blocks needed to store inodes
    uint64_t num_blocks_inodes = opts->n_inodes/inodes_in_block;
    
    if(opts->n_inodes % inodes_in_block > 0){
        num_blocks_inodes++;
    }
    
    //calculate the number of blocks needed to store the inode bitmap
    uint64_t inode_bitmap_count = opts->n_inodes / (A1FS_BLOCK_SIZE * 8) + (opts->n_inodes % (A1FS_BLOCK_SIZE * 8) > 0 ? 1 : 0);
    
    //calculate the number of blocks needed to store the block bitmap
    uint64_t num_blocks = size / A1FS_BLOCK_SIZE;
    uint64_t block_bitmap_count = num_blocks / (A1FS_BLOCK_SIZE * 8) + (num_blocks % (A1FS_BLOCK_SIZE * 8) > 0 ? 1 : 0);
    
    //invalid size check
    size_t minimum_size = (2 + inode_bitmap_count + block_bitmap_count + num_blocks_inodes) * A1FS_BLOCK_SIZE;
   
    if (size <= minimum_size) {
        return false;
    }
    
    memset(image,0,size);
    
	//set all info in superblock
    a1fs_superblock *superblock = (a1fs_superblock *)image;
    
    superblock->inode_bitmap_start = 1;
    superblock->block_bitmap_start = 1 + inode_bitmap_count;
    superblock->inode_table_start = 1 + inode_bitmap_count + block_bitmap_count;
    superblock->data_start = 1 + inode_bitmap_count + block_bitmap_count + num_blocks_inodes;
    
    superblock->magic = A1FS_MAGIC;
    superblock->size = size;
    superblock->inodes_count = opts->n_inodes;
    superblock->free_inodes_count = opts->n_inodes;
    superblock->blocks_count = size / A1FS_BLOCK_SIZE;
    superblock->free_blocks_count = superblock->blocks_count;
    superblock->ino_bitmap_bytes = (superblock->inodes_count / 8) + (superblock->inodes_count % 8 > 0 ? 1 : 0);
    superblock->blk_bitmap_bytes = (superblock->blocks_count / 8) + (superblock->blocks_count % 8 > 0 ? 1 : 0);
	// set all blocks occupied to 1
	unsigned char * block_bitmap = (unsigned char * )((unsigned char * )image + (superblock->block_bitmap_start) * A1FS_BLOCK_SIZE);
	for(int i = 0;i < (int)superblock->data_start;i++){
		set_bit(block_bitmap,1,i,1,superblock);
	}

	a1fs_ino_t root = create_inode((mode_t)S_IFDIR, 0, image, 0);
	assert(root == 0);
    
	return true;
}


int main(int argc, char *argv[])
{
	mkfs_opts opts = {0};// defaults are all 0
	if (!parse_args(argc, argv, &opts)) {
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	if (opts.help) {
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map image file into memory
	size_t size;
	void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
	if (image == NULL) return 1;

	// Check if overwriting existing file system
	int ret = 1;
	if (!opts.force && a1fs_is_present(image)) {
		fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero) memset(image, 0, size);
	if (!mkfs(image, size, &opts)) {
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}

	// Sync to disk if requested
	if (opts.sync && (msync(image, size, MS_SYNC) < 0)) {
		perror("msync");
		goto end;
	}

	ret = 0;
end:
	munmap(image, size);
	return ret;
}

