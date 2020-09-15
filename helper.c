#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "a1fs.h"
#include "helper.h"


/* Helper method to set the proper bit in a bitmap, also update the related information in the superblock
 * type 0 is for inode bitmap and type 1 is for block bitmap
 */
void set_bit(unsigned char *bitmap, int type, int bit, int val, a1fs_superblock *superblock) {
    unsigned char *byte = bitmap + (bit / 8);
    if (val == 1) {
        *byte |= 1 << (bit % 8);
        if (type == 0){
            superblock->free_inodes_count--;
        } else{
            superblock->free_blocks_count--;
        }
    } else {
        *byte &= ~(1 << (bit % 8));
        if (type == 0){
            superblock->free_inodes_count++;
        } else{
            superblock->free_blocks_count++;
        }
    }
}


/* Helper method to find a free bit in the bitmap, and return the index of the byte containing this bit with respect
 * of the starting position of the bitmap
 * type 0 is for inode bitmap and type 1 is for block bitmap */
uint32_t find_free_bit(unsigned char *bitmap, size_t size, int type, a1fs_superblock *superblock) {
    if (type == 0) {
        for (int byte = 0; byte < (int)size; byte++){
            for (int bit = 0; bit < 8; bit++){
                if((bitmap[byte] & (1 << bit)) == 0){
                    return (byte * 8 + bit);
                }
            }
        }
    } else {
        // skip the bytes used for metadata blocks
        int byte = (int)superblock->data_start / 8;
        while (byte < (int)size){
            // skip the bits used for inode table
            int bit = 0;
            if (byte == (int)superblock->data_start / 8){
                bit = superblock->data_start % 8;
            }
            while (bit < 8){
                if((bitmap[byte] & (1 << bit)) == 0){
                    return (byte * 8 + bit);
                }
                bit++;
            }
            byte++;
        }
    }
    return -1;
}


/* Get current time and update the mtime in the given inode, also update all its ancestors */
void update_mtime(a1fs_inode *inode, void *image){
    a1fs_superblock *superblock = (a1fs_superblock *)image;
    //update mtime of this inode
    clock_gettime(CLOCK_REALTIME, &(inode->mtime));
    //update mtime for all its ancestors
    if (inode->parent_ino == 0){
        a1fs_inode *root = (a1fs_inode *)(image + superblock->inode_table_start * A1FS_BLOCK_SIZE );
        clock_gettime(CLOCK_REALTIME, &(root->mtime));
    } else{
        a1fs_inode *parent = (a1fs_inode *)(image + superblock->inode_table_start * A1FS_BLOCK_SIZE + sizeof(a1fs_inode) * inode->parent_ino);
        update_mtime(parent,image);
    }
    
}

/* Find and return the pointer to the end of the directory entry table.
 * If the end is the end of the block, then allocate a new data block for the new dentry, and update all related infomation.
 */
a1fs_dentry *find_vacancy(a1fs_inode *inode,void *image){
    a1fs_superblock *superblock = (a1fs_superblock *)image;
    a1fs_extent *extent_blk = (a1fs_extent *)(image + (inode->block_no) * A1FS_BLOCK_SIZE);
    //calculate how much dentries this inode has
    uint64_t dentry_table_size = inode->size / sizeof(a1fs_dentry);

    //then calculate how much block the dentry table has occupied
    uint64_t dblock_count = inode->size / A1FS_BLOCK_SIZE + (inode->size % A1FS_BLOCK_SIZE > 0 ? 1 : 0);

    //if the last existing dentry is the end of this block, we need to get the vacancy in a new block
    if (dentry_table_size % (A1FS_BLOCK_SIZE / A1FS_DENTRY_SIZE) == 0){
        unsigned char *blk_bitmap = (unsigned char *)(image + superblock->block_bitmap_start * A1FS_BLOCK_SIZE);
        a1fs_blk_t new_blk = find_free_bit(blk_bitmap, superblock->blk_bitmap_bytes, 1, superblock);
        assert(new_blk != 0);
        set_bit(blk_bitmap, 1, new_blk, 1, superblock);
        a1fs_dentry *target = (a1fs_dentry *)(image + new_blk * A1FS_BLOCK_SIZE);
        int existing_extents = 512 - (int)(inode->free_extent_num);

        //check if the new block can be add to any existing extent
        for (int extent_count = 0; extent_count < existing_extents; extent_count++){
            a1fs_extent *this_extent = &extent_blk[extent_count];

            //if can be add to a existing extent
            if (new_blk == (this_extent->start + this_extent->count)){
                this_extent->count++;
                
                //if this extent is not the last extent of this inode, we need to exchange this extent with last extent in the extent block, 
                //since we assume the vacancy is always at last block of the last extent in this inode
                if (extent_count != existing_extents - 1){
                    a1fs_extent *last_extent = &extent_blk[existing_extents - 1];
                    //store the last extent in the temps
                    uint32_t temp_start = last_extent->start;
                    uint32_t temp_count = last_extent->count;
                    //put this extent in to the last extent
                    last_extent->start = this_extent->start;
                    last_extent->count = this_extent->count;
                    //update this extent to be the original last extent
                    this_extent->start = temp_start;
                    this_extent->count = temp_count;
                }
                return target;
            }
        }
        // there's no existing extent can put in the new block, so create a new extent
        a1fs_extent *new_extent = &extent_blk[existing_extents];
        new_extent->start = new_blk;
        new_extent->count = 1;
        inode->free_extent_num--;
        return target;
    }

    
    //else, loop over to find the position of last existing dentry
    int count = 0;
    int curr_block = 0;
    for (int extent_count = 0; extent_count < 512 - (int)(inode->free_extent_num); extent_count++){
        int curr = 0;
        int blk_in_extent = (int)((extent_blk + extent_count * sizeof(a1fs_extent))->count);
        while (dblock_count != 0 && curr < blk_in_extent){
            dblock_count--;
            if(dblock_count == 0){
                count = extent_count;
                curr_block = curr;
                break;
            }
            curr++;
        }
    }
    a1fs_blk_t target_blk = (&extent_blk[count])->start + curr_block;
    int existing_entries = dentry_table_size % (A1FS_BLOCK_SIZE / sizeof(a1fs_dentry));
    a1fs_dentry *last_dentry = (a1fs_dentry *)(image + A1FS_BLOCK_SIZE * target_blk + (existing_entries-1) * sizeof(a1fs_dentry));
    a1fs_dentry *vacancy = (a1fs_dentry *)(&last_dentry[1]);
    return vacancy;
}


/* Create a dentry with the given inode number and name, and write it into the data block of its parent inode. 
 * this function will modify the extent block if necessary.
 */
void write_dentry(const char *name, a1fs_ino_t inode_num, a1fs_ino_t parent_ino,void *image){
    //find a place to put the new dentry and update its fields
    a1fs_superblock *superblock = (a1fs_superblock *)image;

    a1fs_inode *inode_start = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);
    assert(inode_start != NULL);
    a1fs_inode *parent_inode = &inode_start[parent_ino];
    assert(parent_inode != NULL);
    a1fs_dentry *new_dentry = find_vacancy(parent_inode,image);
    assert(new_dentry != NULL);
    new_dentry->ino = inode_num;
    strncpy(new_dentry->name, name, A1FS_NAME_MAX);

    //update all related info
    parent_inode->size += sizeof(a1fs_dentry);
    update_mtime(parent_inode,image);
}


/* Create the inode with the given information and return the inode number of this inode */
a1fs_ino_t create_inode(mode_t mode, a1fs_ino_t parent_ino, void *image, uint32_t type){

    a1fs_superblock *superblock = (a1fs_superblock *)image;
    //allocate an inode and update inode bitmap
    unsigned char * block_bitmap = (unsigned char * )((unsigned char * )image + (superblock->block_bitmap_start) * A1FS_BLOCK_SIZE);
    unsigned char * inode_bitmap = (unsigned char * )((unsigned char * )image + (superblock->inode_bitmap_start) * A1FS_BLOCK_SIZE);

    a1fs_ino_t inode_num = find_free_bit(inode_bitmap, superblock->ino_bitmap_bytes, 0, superblock);
    // assert(inode_num == 0);
    set_bit(inode_bitmap, 0, inode_num, 1, superblock);

    //create inode
    a1fs_inode *inode_table_start = (a1fs_inode *)(image + (superblock->inode_table_start) * A1FS_BLOCK_SIZE);
    a1fs_inode *new_inode = &inode_table_start[inode_num];
    assert(new_inode != NULL);
    new_inode->type = type;
    new_inode->mode = (mode_t)mode;
    new_inode->parent_ino = parent_ino;
    clock_gettime(CLOCK_REALTIME, &(new_inode->mtime));

    //allocate a block for the extent block and update the block bitmap
    //unsigned char * block_bitmap = (unsigned char * )(superblock + (superblock->block_bitmap_start) * A1FS_BLOCK_SIZE);
    a1fs_blk_t extent_blk_num = find_free_bit(block_bitmap, superblock->blk_bitmap_bytes, 1, superblock);
    set_bit(block_bitmap, 1, extent_blk_num, 1,superblock);
    new_inode->block_no = extent_blk_num;
    //!!
    new_inode->free_extent_num = 512;
    new_inode->size = 0;

    if (type == 0) {
        new_inode->links = 2;
        // write . and .. into the directory entry table if the inode is a directory
        write_dentry(".", inode_num, inode_num, superblock);
        write_dentry("..", parent_ino, inode_num, superblock);
    } else {
        new_inode->links = 1;
    }

    return inode_num;
}

/* Promote the last dentry in the directory entry table to the vacancy during deletion of dentry.
 * This function is a helper for remove functions
 */
void promote_last_dentry(a1fs_inode *inode, a1fs_dentry *vacancy_ptr, void *image){
    //The address of the block storing extents
    a1fs_superblock *superblock = (a1fs_superblock *)image;
    a1fs_extent *extent_blk = (a1fs_extent *)(image + (inode->block_no) * A1FS_BLOCK_SIZE);

    int max_dentry = (int)(A1FS_BLOCK_SIZE / sizeof(a1fs_dentry));
    int dentry_num = inode->size / sizeof(a1fs_dentry);
    //Number of dentries in last block
    int dentry_in_last = (int)(dentry_num % max_dentry > 0 ? (dentry_num % max_dentry):max_dentry);

    //Number of extents needed
    int total_extent = 512 - (int)(inode->free_extent_num);
    a1fs_extent *last_extent = &(extent_blk[total_extent - 1]);
    a1fs_blk_t start = last_extent->start;
    a1fs_blk_t last_block = (a1fs_blk_t)(start + last_extent->count - 1);
    
    a1fs_dentry *dentry_start = (a1fs_dentry *)(image + last_block * A1FS_BLOCK_SIZE);
    a1fs_dentry *last_dentry = &(dentry_start[dentry_in_last - 1]);
   
   //store all info of the dentry into the vacancy only if the vacancy is not at last dentry
   if (vacancy_ptr != last_dentry){
    vacancy_ptr->ino = last_dentry->ino;
    strncpy(vacancy_ptr->name, last_dentry->name, A1FS_NAME_MAX);
   }

   //clear the last dentry
    last_dentry->ino = 0;
    memset(last_dentry->name, 0, A1FS_NAME_MAX);

    //update the inode information
    inode->size -= sizeof(a1fs_dentry);

    if(dentry_in_last == 1){ //if the last block has only one dentry
        last_extent->count -= 1;

        //update the block bitmap
        unsigned char * block_bitmap = (unsigned char * )(image + (superblock->block_bitmap_start) * A1FS_BLOCK_SIZE);
        set_bit(block_bitmap,1,last_block,0,superblock);

        if(last_extent->count == 0){ //if the last extent has only one block
            last_extent->start = 0;
            inode->free_extent_num += 1;
        }
    }
    update_mtime(inode,image);
}

/* Find the dentry with filename in the current extent.
 *   Errors:
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 * We would return -1 if we didn't find the dentry we want in the current extent.
 */
a1fs_dentry* find_in_extent(a1fs_extent *extent, const char *filename, void *image){
    //a1fs_superblock *superblock = (a1fs_superblock *)image;
    a1fs_blk_t start = extent->start;
    a1fs_blk_t count = extent->count;
    uint32_t max_dentry_num = A1FS_BLOCK_SIZE * count / A1FS_DENTRY_SIZE;

    a1fs_dentry *dentry_list = (a1fs_dentry*)(image + A1FS_BLOCK_SIZE * start);

    for(uint32_t i = 0; i < max_dentry_num; i++){
        a1fs_dentry *curr_dentry = &dentry_list[i];
        if(curr_dentry->ino == 0 && strcmp(curr_dentry->name, "") == 0 ){
            break;
        }
        if (strcmp(curr_dentry->name, filename) == 0){
            return curr_dentry;
        }
    }
    return NULL;
}

/* Find the dentry with filename in the dentry table of the given inode, and return the inode number associate with the filename if exists,
 * otherwise return the errno according to the error type.
 *   Errors:
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 * we will return -1 for ENOENT and -2 for ENOTDIR
 */
a1fs_dentry* find_dentry(a1fs_inode *inode, const char *filename, void *image){
    
    //a1fs_superblock *superblock = (a1fs_superblock *)image;
    a1fs_extent *extent = (a1fs_extent *)(image + (inode->block_no) * A1FS_BLOCK_SIZE);

    //loop over all extents
    for (uint32_t i = 0; i < 512 - (inode->free_extent_num); i++){
        a1fs_extent *target_extent = &extent[i];
        if(find_in_extent(target_extent,filename,image)!= NULL){
            return find_in_extent(target_extent,filename,image);
        }
    }
    return NULL;
}

/* Find the corresponding inode according to the given path. 
    return inode number on success or error.
    errors:
    ENOTDIR: return superblock->inodes_count + 1
    ENOENT: return superblock->inodes_count + 2
*/
a1fs_ino_t find_inode(char *path, a1fs_inode *inode_list, void *image){
    a1fs_superblock *superblock = (a1fs_superblock *)image;
    a1fs_inode *curr_inode = &inode_list[0];

    //if it is the root
    if(strcmp(path,"/") == 0){
        return 0;
    }
    char *temp = strtok(path, "/");
    a1fs_ino_t inode_num = 0;

    while (temp != NULL) {
        if(curr_inode->type != 0){return (superblock->inodes_count + 1);}
        if(find_dentry(curr_inode,temp,image) == NULL){return (superblock->inodes_count + 2);}
        a1fs_ino_t parent_num = ((a1fs_dentry*)find_dentry(curr_inode,"..",image))->ino;
        printf("inode number of .. : %u\n", parent_num);
        inode_num = ((a1fs_dentry*)find_dentry(curr_inode,temp,image))->ino;
        printf("inode_num: %u\n",inode_num);
        //update the current inode number
        curr_inode = &inode_list[inode_num];
        temp = strtok(NULL, "/");      
    } 
    return inode_num;
}


/*
Free all data blocks in this inode (not including the extent block)
*/
void free_data(a1fs_inode *inode, unsigned char *block_bitmap,void *image){
       
    a1fs_extent *extent = (a1fs_extent *)(image + inode->block_no * A1FS_BLOCK_SIZE);
    for (int count = 0; count < 512 - (int)(inode->free_extent_num); count++){
        free_in_extent(&extent[count],block_bitmap,image);
    }  
}

/* Free all data blocks in this extent */
void free_in_extent(a1fs_extent *extent, unsigned char *block_bitmap, void *image){
    
    a1fs_superblock *superblock = (a1fs_superblock *)image;
    a1fs_blk_t start = extent->start;
    a1fs_blk_t count = extent->count;

    for(uint32_t i = start; i < count + start; i++){
        set_bit(block_bitmap,1,start+i,0,superblock);
    }
}

/* Add the new block found to the extent block*/

void add_to_extent(a1fs_extent *extent_blk, a1fs_inode *inode, a1fs_blk_t new_blk){

    int existing_extents = 512 - (int)(inode->free_extent_num);
        //check if the new block can be add to any existing extent
        for (int extent_count = 0; extent_count < existing_extents; extent_count++){
            a1fs_extent *this_extent = &extent_blk[extent_count];
            //if can be add to a existing extent
            if (new_blk == (this_extent->start + this_extent->count)){
                this_extent->count++;
                return;
            }
        }
        //else,we would have a new extent
        a1fs_extent *new_extent = &extent_blk[existing_extents];
        new_extent->start = new_blk;
        new_extent->count = 1;
        inode->free_extent_num--;
}

/*
Extend the file to the target size.
*/
void extend_data(size_t size_allocate, a1fs_inode *inode, void *image){

    a1fs_superblock *superblock = (a1fs_superblock *)image;
    a1fs_extent *extent = (a1fs_extent *)(image + inode->block_no * A1FS_BLOCK_SIZE);

    unsigned char *blk_bitmap = (unsigned char *)(image + superblock->block_bitmap_start * A1FS_BLOCK_SIZE);
    int total_block_used = size_allocate / A1FS_BLOCK_SIZE + ((size_allocate % A1FS_BLOCK_SIZE) > 0 ? 1 : 0);
    for(int i = 0;i < total_block_used;i++){
        a1fs_blk_t new_blk = find_free_bit(blk_bitmap, superblock->blk_bitmap_bytes, 1, superblock);
        set_bit(blk_bitmap,1,new_blk,1,superblock);
        unsigned char *data_start = (unsigned char *)(image + new_blk * A1FS_BLOCK_SIZE);
        memset(data_start, 0, A1FS_BLOCK_SIZE);
        add_to_extent(extent,inode,new_blk);
    }
}

/* 
Shrink the file to the target size.
*/
void shrink_data(size_t size, a1fs_inode *inode, void *image){

    a1fs_superblock *superblock = (a1fs_superblock *)image;
    a1fs_extent *extent = (a1fs_extent *)(image + inode->block_no * A1FS_BLOCK_SIZE);
    unsigned char *blk_bitmap = (unsigned char *)(image + superblock->block_bitmap_start * A1FS_BLOCK_SIZE);

    // The number of blocks we don't need to free
    int start_delete = size / A1FS_BLOCK_SIZE + ((size % A1FS_BLOCK_SIZE) > 0 ? 1 : 0);

    //set the last few blocks
    int existing_extents = 512 - (int)(inode->free_extent_num);

    //check if the new block can be add to any existing extent
    int counter = 0;
        for (int extent_count = 0; extent_count < existing_extents; extent_count++){

            a1fs_extent *curr_extent = &extent[extent_count];
            a1fs_blk_t start = curr_extent->start;
            a1fs_blk_t count = curr_extent->count;

            for(uint32_t i = start; i < count + start; i++){
                if(counter >= start_delete){
                    set_bit(blk_bitmap,1,start + i,0,superblock);
                }
                counter++;
            }
        }
 
}
