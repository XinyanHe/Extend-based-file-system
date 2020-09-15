#ifndef helper_h
#define helper_h

#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "a1fs.h"

/* Following are the global varibles for the whole file system
*/
// record the starting point of the file system

void set_bit(unsigned char *bitmap, int type, int bit, int val,a1fs_superblock *superblock);

uint32_t find_free_bit(unsigned char *bitmap, size_t size, int type, a1fs_superblock *superblock);

void update_mtime(a1fs_inode *inode, void *image);

a1fs_dentry *find_vacancy(a1fs_inode *inode,void *image);

void write_dentry(const char *name, a1fs_ino_t inode_num, a1fs_ino_t parent_ino,void *image);

a1fs_ino_t create_inode(mode_t mode, a1fs_ino_t parent_ino, void *image, uint32_t type);

void promote_last_dentry(a1fs_inode *inode, a1fs_dentry *vacancy_ptr, void *image);

a1fs_dentry* find_dentry(a1fs_inode *inode, const char *filename, void *image);

a1fs_dentry* find_in_extent(a1fs_extent *extent, const char *filename, void *image);

a1fs_ino_t find_inode(char *path, a1fs_inode *root, void *image);

void free_data(a1fs_inode *inode, unsigned char *block_bitmap,void *image);

void free_in_extent(a1fs_extent *extent, unsigned char *block_bitmap, void *image);

void shrink_data(size_t size_cut, a1fs_inode *inode, void *image);

void add_to_extent(a1fs_extent *extent_blk, a1fs_inode *inode, a1fs_blk_t new_blk);

void extend_data(size_t size_allocate, a1fs_inode *inode, void *image);

#endif /* helper_h */