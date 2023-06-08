#include "jumbo_file_system.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// C does not have a bool type, so I created one that you can use
typedef char bool_t;
#define TRUE 1
#define FALSE 0


static block_num_t current_dir;


// optional helper function you can implement to tell you if a block is a dir node or an inode, return TRUE for dir node, FALSE for inode
static bool_t is_dir(block_num_t block_num) {
    // get the block
    struct block target_block;
    bzero(&target_block, sizeof(struct block));
    int ret_temp = read_block(block_num, &target_block);
    if (ret_temp == -1){
        return ret_temp;
    }
    
    // check if dir node
    return (target_block.is_dir == 0);
}

/* if_exist
 *   helper function to see if a "name" exists 
 *   in a given block ("block_num")
 *
 * name: the name of a dir or file
 * 
 * returns -1 if not exist or error, otherwise the index of the file/dir 
 * in the "entries" (see the "struct block" in jumbo_file_system.h )
 */ 
static int if_exist(const char* target_name) {
    // get current folder block in current_dir
    struct block cur_block;
    bzero(&cur_block, sizeof(struct block));
    int ret_temp = read_block(current_dir, &cur_block);
    if (ret_temp == -1){
        return ret_temp;
    }
    
    // get current number of entries in the current folder
    uint16_t cur_entries = cur_block.contents.dirnode.num_entries;
    // check if name exits
    for (int i = 0; i < cur_entries; i++){
        if (strcmp(cur_block.contents.dirnode.entries[i].name, target_name)== 0){
            return i;
        }
    }
    return -1;
}
/* remove_directory_entry
 *   helper function to remove the specific entry from the given block (folder):
 *   release the deleted block, update the meta data of the given block (folder)
 * cur_block: the block to remove the entry
 * entry_index: index of the entry in the block
 *
 * returns 0 on failure, otherwise returns the block num of the inode or dir block
 *   of the deleted entry
 */
static block_num_t remove_directory_entry(struct block* cur_block, int entry_index){
    
    // release the target block (to be deleted)
    block_num_t target_block_num = 
        (*cur_block).contents.dirnode.entries[entry_index].block_num;
    int ret_temp = release_block(target_block_num);
    if (ret_temp == -1){
        //release failed
        return 0;
    }
    
    // clean up the target block
    // by write a new empty block back
    struct block new_block;
    bzero(&new_block, sizeof(struct block));
    write_block(target_block_num, &new_block); 
    
    //	Remove entry from cur_block
    uint16_t cur_entry_num = (*cur_block).contents.dirnode.num_entries;
    if (entry_index+1 != cur_entry_num){
        // target entry is not the last entry
        // replace the target entry with the last entry
        (*cur_block).contents.dirnode.entries[entry_index].block_num = 
            (*cur_block).contents.dirnode.entries[cur_entry_num-1].block_num;
        strcpy((*cur_block).contents.dirnode.entries[entry_index].name,
           (*cur_block).contents.dirnode.entries[cur_entry_num-1].name);
    }
    // decrease the entry number by one
    (*cur_block).contents.dirnode.num_entries--;
    // write back the current block
    if (write_block(current_dir, (void *)cur_block) == -1){
        return 0;
    }
    
    
    return target_block_num;
}

/* release_data_blocks
 *   helper function to release all the data block from the given inode
 *
 * returns 0 on success, otherwise returns -1 
 */
static int release_data_blocks(struct block* inode_block){
    // store file size
    uint32_t fSize = (*inode_block).contents.inode.file_size;
    
    // get data block amount
    int block_amount = (fSize + BLOCK_SIZE - 1)/BLOCK_SIZE; // ceiling division
    
    // release all the datablocks
    for (int i = 0; i < block_amount; i++){
        block_num_t data_block_num = (*inode_block).contents.inode.data_blocks[i];
        int ret_temp = release_block(data_block_num);
        if (ret_temp == -1){
            //release failed
            return -1;
        }    
    }
    return 0;
    
}

/* release_all
 *   helper function to release all blocks in the block_num_list
 * returns 0 for success, -1 otherwise
 */
static int release_all(block_num_t* block_num_list, int list_size){
    int result = 0;
    for (int i =0; i < list_size; i++){
        if (release_block(block_num_list[i]) != 0){
            result = -1;
        }
    }
    return result;
}

/* write_data_blocks
 *   helper function to write the data from buf to the end of the data blocks
 *   associated with the inode block provided. In this process, data blocks would 
 *   be updated or created. And the inode block would also be updated accordingly.
 *
 * inode_block: representing a file
 * buf: where data coming from
 * count: how many bytes to get copy from buf
 *
 * returns 0 on success, -2 for disk full (allocate fail), otherwise returns -1 
 */
static int write_data_blocks(struct block* inode_block, const void* buf, unsigned short count){
    // record the next index of buf to be copied
    int copied = 0;
    
    uint32_t cur_fSize = (*inode_block).contents.inode.file_size;
    int cur_block_amount = 
        (cur_fSize + BLOCK_SIZE - 1)/BLOCK_SIZE; // ceiling division
        
    // the bytes that all current data blocks can hold
    uint32_t cur_block_vol = cur_block_amount * BLOCK_SIZE;
    
    uint32_t new_size = cur_fSize + count;
    int new_block_amount = 
        (new_size + BLOCK_SIZE - 1)/BLOCK_SIZE; // ceiling division
        
    // block amount that need to be added
    int block_amount_diff = new_block_amount - cur_block_amount;
    
    // data structure to store new data blocks and their block_num
    struct block new_blocks[block_amount_diff];
    bzero(new_blocks, sizeof(new_blocks));
    int new_blocks_counter = 0;
    
    block_num_t new_block_nums[block_amount_diff];
    bzero(new_block_nums, sizeof(new_block_nums));
    int new_block_nums_counter = 0;
    
    // create a new block for the last data block in the current inode
    struct block last_block;
    bzero(&last_block, sizeof(struct block));
    
    // get the block_num for the last data block
    block_num_t last_block_num = 
            (*inode_block).contents.inode.data_blocks[cur_block_amount - 1];
    
    // allocate all needed data blocks
    for (int i = 0; i < block_amount_diff; i++){
        block_num_t new_block_num = allocate_block();        
        if (new_block_num == 0){
            release_all(new_block_nums, new_block_nums_counter);
            return -2;
        }
        new_block_nums[new_block_nums_counter++] = new_block_num;
    }
    
    // create data blocks
    if (cur_fSize < cur_block_vol){
        // last data block is not full
        // get the last data block        
        int ret_temp = read_block(last_block_num, &last_block);
        if (ret_temp == -1){
            return ret_temp;
        }
        
        // calculate the start point to write new data in the last block
        //start index (zero based)        
        int from_here = BLOCK_SIZE - (cur_block_vol - cur_fSize);
        int bytes_left = cur_block_vol - cur_fSize;
            
        // append data to the last block
        char* last_ptr = (char*)(&last_block);
        char* buf_ptr = (char*)buf;
        if (bytes_left > count){
            memcpy(&(last_ptr[from_here]), &(buf_ptr[copied]), count);
            copied = copied + count;
            count = 0;
        } else {
            memcpy(&(last_ptr[from_here]), &(buf_ptr[copied]), bytes_left);
            copied = copied + bytes_left;
            count = count - bytes_left;
        }
    }
    // fill in new data blocks
    for (int i = 0; i < block_amount_diff; i++){
        char* buf_ptr = (char*)buf;
        if (count <= BLOCK_SIZE){
            memcpy(&(new_blocks[new_blocks_counter++]), &(buf_ptr[copied]), count);
            copied = copied + count;
            count = 0;
        } else{
            memcpy(&(new_blocks[new_blocks_counter++]), &(buf_ptr[copied]), BLOCK_SIZE);
            copied = copied + BLOCK_SIZE;
            count = count - BLOCK_SIZE;
        }          
    }
    
    // update inode block
    (*inode_block).contents.inode.file_size = new_size;
    for (int i =0; i < new_block_nums_counter; i++){
        (*inode_block).contents.inode.data_blocks[cur_block_amount + i] = 
            new_block_nums[i];
    }
    
    // write data blocks
    if (cur_fSize < cur_block_vol){
        // no new data block
        // write the last block
        int ret_temp = write_block(last_block_num, (void *)&last_block);
        if (ret_temp == -1){
            release_all(new_block_nums, new_block_nums_counter);
            return -1;
        }
    }
    
    // write new blocks
    for (int i = 0; i < block_amount_diff; i++){
        int ret_temp = write_block(new_block_nums[i], (void *)&(new_blocks[i]));
        if (ret_temp == -1){
            release_all(new_block_nums, new_block_nums_counter);
            return -1;
        }
    }
    return 0;
}


/* jfs_mount
 *   prepares the DISK file on the _real_ file system to have file system
 *   blocks read and written to it.  The application _must_ call this function
 *   exactly once before calling any other jfs_* functions.  If your code
 *   requires any additional one-time initialization before any other jfs_*
 *   functions are called, you can add it here.
 * filename - the name of the DISK file on the _real_ file system
 * returns 0 on success or -1 on error; errors should only occur due to
 *   errors in the underlying disk syscalls.
 */
int jfs_mount(const char* filename) {
  int ret = bfs_mount(filename);
  current_dir = 1;
  return ret;
}


/* jfs_mkdir
 *   creates a new subdirectory in the current directory
 * directory_name - name of the new subdirectory
 * returns 0 on success or one of the following error codes on failure:
 *   E_EXISTS, E_MAX_NAME_LENGTH, E_MAX_DIR_ENTRIES, E_DISK_FULL
 */
int jfs_mkdir(const char* directory_name) {
    /***** chekc errors (except E_DISK_FULL)******/
    // to store results of calling other functions
    int ret_temp;
    
    // check if directory_name length too long
    if (strlen(directory_name) > MAX_NAME_LENGTH){
        return E_MAX_NAME_LENGTH;
    }
    
    // get current folder block in current_dir
    struct block cur_block;
    bzero(&cur_block, sizeof(struct block));
    ret_temp = read_block(current_dir, &cur_block);
    if (ret_temp == -1){
        return ret_temp;
    }
    
    // get current number of entries in the current folder
    uint16_t cur_entries = cur_block.contents.dirnode.num_entries;
    
    // check number of entries in the current_dir    
    if (cur_entries >= MAX_DIR_ENTRIES){
        return E_MAX_DIR_ENTRIES;
    }
    
    // check if name already exits
    for (int i = 0; i < cur_entries; i++){
        if (strcmp(cur_block.contents.dirnode.entries[i].name, directory_name)== 0){
            return E_EXISTS;
        }
    }
    
    /***** prepare and write the new directory + E_DISK_FULL******/
    // allocate a block for the new directory
    block_num_t new_block_num = allocate_block();
    if (new_block_num == 0){
        // block full see allocate_block function
        return E_DISK_FULL;
    }
    
    // create a local copy of the new block
    struct block new_block;
    bzero(&new_block, sizeof(struct block));
    
    // clear the allocated block by writing the empty block to the disk
    write_block(new_block_num, &new_block);    
    
    // fill in the meta data for the new directory (local)
    new_block.is_dir = 0;
    new_block.contents.dirnode.num_entries = 0;    
    
    // write the new directory to the allocated block
    if (write_block(new_block_num, (void *)&new_block) == -1){
        return E_UNKNOWN;
    }
    
    /***** update the meta data of the current directory ******/
    // update local copy: cur_block
    uint16_t cur_num_entries = ++(cur_block.contents.dirnode.num_entries);
    cur_block.contents.dirnode.entries[cur_num_entries-1].block_num = new_block_num;
    strcpy(cur_block.contents.dirnode.entries[cur_num_entries-1].name,
           directory_name);
           
    // write the current block
    if (write_block(current_dir, (void *)&cur_block) == -1){
        return E_UNKNOWN;
    }
    
  return E_SUCCESS;
}


/* jfs_chdir
 *   changes the current directory to the specified subdirectory, or changes
 *   the current directory to the root directory if the directory_name is NULL
 * directory_name - name of the subdirectory to make the current
 *   directory; if directory_name is NULL then the current directory
 *   should be made the root directory instead
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_NOT_DIR
 */
int jfs_chdir(const char* directory_name) {
    // if null -> root
    if (directory_name == NULL){
        current_dir = 1;
        return E_SUCCESS;
    }
    
    // get current folder block in current_dir
    struct block cur_block;
    bzero(&cur_block, sizeof(struct block));
    int ret_temp = read_block(current_dir, &cur_block);
    if (ret_temp == -1){
        return ret_temp;
    }
        
    // check if name exits    
    int target_index = if_exist(directory_name);
    if (target_index != -1){
        // found the same name
        // check if it is a directory or not
        block_num_t target_dir_num = 
            cur_block.contents.dirnode.entries[target_index].block_num;
        if (is_dir(target_dir_num)){
            current_dir = target_dir_num;
            return E_SUCCESS;
        } else{
            return E_NOT_DIR;
        }
    }
    
    // name not exist in the current directory
    return E_NOT_EXISTS;
}


/* jfs_ls
 *   finds the names of all the files and directories in the current directory
 *   and writes the directory names to the directories argument and the file
 *   names to the files argument
 * directories - array of strings; the function will set the strings in the
 *   array, followed by a NULL pointer after the last valid string; the strings
 *   should be malloced and the caller will free them
 * file - array of strings; the function will set the strings in the
 *   array, followed by a NULL pointer after the last valid string; the strings
 *   should be malloced and the caller will free them
 * returns 0 on success or one of the following error codes on failure:
 *   (this function should always succeed)
 */
int jfs_ls(char* directories[MAX_DIR_ENTRIES+1], char* files[MAX_DIR_ENTRIES+1]) {
    // set the two results to NULL
    for (size_t i = 0; i < MAX_DIR_ENTRIES + 1; i++) { 
        directories[i] = NULL;
        files[i] = NULL;
    }
    
    // get current folder block in current_dir
    struct block cur_block;
    bzero(&cur_block, sizeof(struct block));
    int ret_temp = read_block(current_dir, &cur_block);
    if (ret_temp == -1){
        return ret_temp;
    }
    
    // get current number of entries in the current folder
    uint16_t cur_entries = cur_block.contents.dirnode.num_entries;
    
    // start index of the two result arrays
    int d = 0;
    int f = 0;
    
    // go through each entries to get names for both files and directories
    for (int i = 0; i < cur_entries; i++){
        block_num_t target_block_num = 
            cur_block.contents.dirnode.entries[i].block_num;
            
        // malloc a string and set a name in it
        char *target_name = (char *) malloc((MAX_NAME_LENGTH + 1) * sizeof(char));
        if (target_name == NULL) { 
            // malloc fail
            return E_UNKNOWN;
        }
        bzero(target_name, MAX_NAME_LENGTH + 1);
        strcpy(target_name, cur_block.contents.dirnode.entries[i].name);
        
        // put name to the result arrays
        if (is_dir(target_block_num)){
            // dir
            directories[d++] = target_name;
        } else {
            // file
            files[f++] = target_name;
        }
    }
  return E_SUCCESS;
}


/* jfs_rmdir
 *   removes the specified subdirectory of the current directory
 * directory_name - name of the subdirectory to remove
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_NOT_DIR, E_NOT_EMPTY
 */
int jfs_rmdir(const char* directory_name) {
    // get current folder block in current_dir
    struct block cur_block;
    bzero(&cur_block, sizeof(struct block));
    int ret_temp = read_block(current_dir, &cur_block);
    if (ret_temp == -1){
        return ret_temp;
    }
    
    // check if the name exist in the current directory
    int target_index = if_exist(directory_name);
    if (target_index == -1){
        // name not exist
        return E_NOT_EXISTS;
    }
    
    // check if target is a directory
    block_num_t target_block_num = 
        cur_block.contents.dirnode.entries[target_index].block_num;
    if (is_dir(target_block_num)){
        // dir
        // get the target directory block into target_block
        struct block target_block;
        bzero(&target_block, sizeof(struct block));
        ret_temp = read_block(target_block_num, &target_block);
        if (ret_temp == -1) {
            return ret_temp;
        }
        
        // check if the target directory is empty
        if (target_block.contents.dirnode.num_entries > 0) {
            return E_NOT_EMPTY;
        }
        
        // remove the target directory (the entry in my current directory block)
        // also release the deleted block
        block_num_t deleted_block_num = 
            remove_directory_entry(&cur_block, target_index);
        if (deleted_block_num == 0){
            return E_UNKNOWN;
        }
    } else {
        // file
        return E_NOT_DIR;
    }
    
    
  return E_SUCCESS;
}


/* jfs_creat
 *   creates a new, empty file with the specified name
 * file_name - name to give the new file
 * returns 0 on success or one of the following error codes on failure:
 *   E_EXISTS, E_MAX_NAME_LENGTH, E_MAX_DIR_ENTRIES, E_DISK_FULL
 */
int jfs_creat(const char* file_name) {
    /***** chekc errors (except E_DISK_FULL)******/
    // to store results of calling other functions
    int ret_temp;
    
    // check if file_name length too long
    if (strlen(file_name) > MAX_NAME_LENGTH){
        return E_MAX_NAME_LENGTH;
    }
    
    // get current folder block in current_dir
    struct block cur_block;
    bzero(&cur_block, sizeof(struct block));
    ret_temp = read_block(current_dir, &cur_block);
    if (ret_temp == -1){
        return ret_temp;
    }
    
    // get current number of entries in the current folder
    uint16_t cur_entries = cur_block.contents.dirnode.num_entries;
    
    // check number of entries in the current_dir    
    if (cur_entries >= MAX_DIR_ENTRIES){
        return E_MAX_DIR_ENTRIES;
    }
    
    // check if name already exits
    for (int i = 0; i < cur_entries; i++){
        if (strcmp(cur_block.contents.dirnode.entries[i].name, file_name)== 0){
            return E_EXISTS;
        }
    }
    
    /***** prepare and write the new file + E_DISK_FULL check******/
    // allocate a block for the new file
    block_num_t new_block_num = allocate_block();
    if (new_block_num == 0){
        // block full see allocate_block function
        return E_DISK_FULL;
    }
    
    // create a local copy of the new block
    struct block new_block;
    bzero(&new_block, sizeof(struct block));
    
    // clear the allocated block by writing the empty block to the disk
    write_block(new_block_num, &new_block);    
    
    // fill in the meta data for the new file (local)
    new_block.is_dir = 1;
    new_block.contents.inode.file_size = 0;    
    
    // write the new file to the allocated block
    if (write_block(new_block_num, (void *)&new_block) == -1){
        return E_UNKNOWN;
    }
    
    /***** update the meta data of the current directory ******/
    // update local copy: cur_block
    uint16_t cur_num_entries = ++(cur_block.contents.dirnode.num_entries);
    cur_block.contents.dirnode.entries[cur_num_entries-1].block_num = new_block_num;
    strcpy(cur_block.contents.dirnode.entries[cur_num_entries-1].name,
           file_name);
           
    // write the current block
    if (write_block(current_dir, (void *)&cur_block) == -1){
        return E_UNKNOWN;
    }
    
  return E_SUCCESS;
}


/* jfs_remove
 *   deletes the specified file and all its data (note that this cannot delete
 *   directories; use rmdir instead to remove directories)
 * file_name - name of the file to remove
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_IS_DIR
 */
int jfs_remove(const char* file_name) {
    
    // get current folder block in current_dir
    struct block cur_block;
    bzero(&cur_block, sizeof(struct block));
    int ret_temp = read_block(current_dir, &cur_block);
    if (ret_temp == -1){
        return ret_temp;
    }
    
    // check if the name exist in the current directory
    int target_index = if_exist(file_name);
    if (target_index == -1){
        // name not exist
        return E_NOT_EXISTS;
    }
    
    // check if target is a file
    block_num_t target_block_num = 
        cur_block.contents.dirnode.entries[target_index].block_num;
    if (is_dir(target_block_num)){
        // dir
        return E_IS_DIR;
    } else {
        // file
        // get the target inode block into target_block
        struct block target_block;
        bzero(&target_block, sizeof(struct block));
        ret_temp = read_block(target_block_num, &target_block);
        if (ret_temp == -1) {
            return ret_temp;
        }
        
        // check if empty file
        uint32_t fSize = target_block.contents.inode.file_size;
        if (fSize != 0){
            // non empty
            // release all data blocks
            ret_temp = release_data_blocks(&target_block);
            if (ret_temp != 0){
                return ret_temp;
            }
        }
        block_num_t deleted_block_num = 
            remove_directory_entry(&cur_block, target_index);
        if (deleted_block_num == 0){
            return E_UNKNOWN;
        }
    }
  return E_SUCCESS;
}


/* jfs_stat
 *   returns the file or directory stats (see struct stat for details)
 * name - name of the file or directory to inspect
 * buf  - pointer to a struct stat (already allocated by the caller) where the
 *   stats will be written
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS
 */
int jfs_stat(const char* name, struct stats* buf) {
    // get current folder block in current_dir
    struct block cur_block;
    bzero(&cur_block, sizeof(struct block));
    int ret_temp = read_block(current_dir, &cur_block);
    if (ret_temp == -1){
        return ret_temp;
    }
    
    // check if the name exist in the current directory
    int target_index = if_exist(name);
    if (target_index == -1){
        // name not exist
        return E_NOT_EXISTS;
    }
    
    // get target block num
    block_num_t target_block_num = 
        cur_block.contents.dirnode.entries[target_index].block_num;
    // get the target inode/dirnode block into target_block
    struct block target_block;
    bzero(&target_block, sizeof(struct block));
    ret_temp = read_block(target_block_num, &target_block);
    if (ret_temp == -1) {
        return ret_temp;
    }
    
    // clear buf
    bzero(buf, sizeof(struct stats));
    // write buf
    (*buf).is_dir = target_block.is_dir;
    strcpy((*buf).name,
           cur_block.contents.dirnode.entries[target_index].name);
    (*buf).block_num = target_block_num;
    if (target_block.is_dir != 0){
        uint32_t fSize = target_block.contents.inode.file_size;
        (*buf).num_data_blocks = 
            (fSize + BLOCK_SIZE - 1)/BLOCK_SIZE; // ceiling division
        (*buf).file_size = fSize;
    }
    
  return E_SUCCESS;
}


/* jfs_write
 *   appends the data in the buffer to the end of the specified file
 * file_name - name of the file to append data to
 * buf - buffer containing the data to be written (note that the data could be
 *   binary, not text, and even if it is text should not be assumed to be null
 *   terminated)
 * count - number of bytes in buf (write exactly this many)
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_IS_DIR, E_MAX_FILE_SIZE, E_DISK_FULL
 */
int jfs_write(const char* file_name, const void* buf, unsigned short count) {
    // get current folder block in current_dir
    struct block cur_block;
    bzero(&cur_block, sizeof(struct block));
    int ret_temp = read_block(current_dir, &cur_block);
    if (ret_temp == -1){
        return ret_temp;
    }
    
    // check if the name exist in the current directory
    int target_index = if_exist(file_name);
    if (target_index == -1){
        // name not exist
        return E_NOT_EXISTS;
    }
    
    // get target block num
    block_num_t target_block_num = 
        cur_block.contents.dirnode.entries[target_index].block_num;
        
    // check if target is a file
    if (is_dir(target_block_num)){
        // dir
        return E_IS_DIR;
    } else {
        // file
        // get the target inode block into target_block
        struct block target_block;
        bzero(&target_block, sizeof(struct block));
        ret_temp = read_block(target_block_num, &target_block);
        if (ret_temp == -1) {
            return ret_temp;
        }
        
        // check max file size error
        // get current file size
        uint32_t cur_fSize = target_block.contents.inode.file_size;
        uint32_t new_size = cur_fSize + count;
        if (new_size > MAX_FILE_SIZE){
            return E_MAX_FILE_SIZE;
        }
        
        // write data blocks
        int write_result = write_data_blocks(&target_block, buf, count);
        if (write_result != 0){
            if (write_result == -2){
                // full error
                return E_DISK_FULL;
            } else {
                // -1
                return E_UNKNOWN;
            }
        }
        
        // write the inode to disk
        write_result = write_block(target_block_num, &target_block);
        if (write_result == -1){
            return E_UNKNOWN;
        }
    }
  return E_SUCCESS;
}


/* jfs_read
 *   reads the specified file and copies its contents into the buffer, up to a
 *   maximum of *ptr_count bytes copied (but obviously no more than the file
 *   size, either)
 * file_name - name of the file to read
 * buf - buffer where the file data should be written
 * ptr_count - pointer to a count variable (allocated by the caller) that
 *   contains the size of buf when it's passed in, and will be modified to
 *   contain the number of bytes actually written to buf (e.g., if the file is
 *   smaller than the buffer) if this function is successful
 * returns 0 on success or one of the following error codes on failure:
 *   E_NOT_EXISTS, E_IS_DIR
 */
int jfs_read(const char* file_name, void* buf, unsigned short* ptr_count) {
    // get current folder block in current_dir
    struct block cur_block;
    bzero(&cur_block, sizeof(struct block));
    int ret_temp = read_block(current_dir, &cur_block);
    if (ret_temp == -1){
        return ret_temp;
    }
    
    // check if the name exist in the current directory
    int target_index = if_exist(file_name);
    if (target_index == -1){
        // name not exist
        return E_NOT_EXISTS;
    }
    
    // get target block num
    block_num_t target_block_num = 
        cur_block.contents.dirnode.entries[target_index].block_num;
        
    // check if target is a file
    if (is_dir(target_block_num)){
        // dir
        return E_IS_DIR;
    } else {
        // file
        // get the target inode block into target_block
        struct block target_block;
        bzero(&target_block, sizeof(struct block));
        ret_temp = read_block(target_block_num, &target_block);
        if (ret_temp == -1) {
            return ret_temp;
        }
        
        // get filesize
        uint32_t fSize = target_block.contents.inode.file_size;
        
        // update count
        if ((*ptr_count) > fSize){
            *ptr_count = fSize;
        }
        
        // copy buf
        int block_amount = 
            (fSize + BLOCK_SIZE - 1)/BLOCK_SIZE; // ceiling division
        int left = *ptr_count;
        int cur_block_index = 0; // mark which block we have get to
        int buf_index = 0; // mark where to start to fill in buf
        char* buf_ptr = (char*)buf;
        while (cur_block_index < block_amount && left > 0){
            // get this data block
            block_num_t data_block_num = 
                target_block.contents.inode.data_blocks[cur_block_index];
            struct block data_block;
            bzero(&data_block, sizeof(struct block));
            int ret_temp = read_block(data_block_num, &data_block);
            
            if (ret_temp == -1){
                return ret_temp;
            }
            if (left >= BLOCK_SIZE){
                // copy the whole block
                memcpy(&(buf_ptr[buf_index]), &data_block, BLOCK_SIZE);
                left = left - BLOCK_SIZE;
                buf_index = buf_index + BLOCK_SIZE;
            } else {
                // copy left
                memcpy(&(buf_ptr[buf_index]), &data_block, left);
                left = 0;
                buf_index = buf_index + left;
            }
            cur_block_index++;
        }
            
    }
  return E_SUCCESS;
}


/* jfs_unmount
 *   makes the file system no longer accessible (unless it is mounted again).
 *   This should be called exactly once after all other jfs_* operations are
 *   complete; it is invalid to call any other jfs_* function (except
 *   jfs_mount) after this function complete.  Basically, this closes the DISK
 *   file on the _real_ file system.  If your code requires any clean up after
 *   all other jfs_* functions are done, you may add it here.
 * returns 0 on success or -1 on error; errors should only occur due to
 *   errors in the underlying disk syscalls.
 */
int jfs_unmount() {
  int ret = bfs_unmount();
  return ret;
}
