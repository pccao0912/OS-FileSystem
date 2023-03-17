#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

struct superblock {
	uint64_t signature;
	uint16_t total_blocks;
	uint16_t rootdir_blk_index;
	uint16_t datablk_start_index;
	uint16_t datablk_amount;
	uint8_t  fat_amount;
	uint8_t  unused[4079];
}__attribute__((packed));

uint16_t FAT[8192];

struct entry {
	uint8_t  filename[FS_FILENAME_LEN];
	uint32_t file_size;
	uint16_t datablk_start_index;
	uint8_t  unused[10];
}__attribute__((packed));

struct root_directory {
	struct entry entry_array[FS_FILE_MAX_COUNT];
}__attribute__((packed));

struct file_descriptor {
	struct entry *entry;
	size_t offset;
};

// global
struct superblock superblock;
struct root_directory root_directory;
struct file_descriptor fd_table[FS_OPEN_MAX_COUNT];

// helper functs

uint8_t bounce[BLOCK_SIZE];
#define error(fmt, ...) \
	fprintf(stderr, "%s: "fmt"\n", __func__, ##__VA_ARGS__)

#define fs_error(...)				\
{							\
	error(__VA_ARGS__);	\
	return -1;					\
}

#define fs_perror(...)				\
{							\
	perror(__VA_ARGS__);	\
	return -1;					\
}
uint16_t FAT_iterator(uint16_t block_index, uint16_t count)
{
	for (uint16_t i = 0; i < count; ++i) {
		block_index = FAT[block_index];
	}
	return block_index;
}


int block_create(int fd){
	int index = 0;
	for (int i = 0; i < superblock.fat_amount * 2048; i++ ) {
		if (FAT[i] == 0) {
			index = i;
			break;
		}
		if (FAT[i] != 0 && i == (superblock.fat_amount*2048) -1) {
			return -1;
		}
	}
    fd_table[fd].entry->datablk_start_index = index;
    FAT[index] = 0xFFFF;
	return index;
}

int rdir_free_blocks() {
	int counter = FS_FILE_MAX_COUNT;
	for (int i = 0; i <FS_FILE_MAX_COUNT; i++) {
		if (root_directory.entry_array[i].filename[0] == '\0') {
			continue;
		}else {
		counter --;
		}
	}
	return counter;
}

int fat_free_blocks() {
	int counter = superblock.datablk_amount;
	for (int i = 0; i <superblock.datablk_amount; i++) {
		if (FAT[i] == '\0') {
			continue;
		}else {
			counter --;
		}
	}
	return counter;
}

int fs_mount(const char *diskname)
{
	int opendisk = block_disk_open(diskname);
	if (opendisk == - 1) {
		return -1;	
	}
	block_read(0, &superblock);
	block_read(superblock.rootdir_blk_index, &root_directory);
	for ( int i = 0; i < superblock.fat_amount; ++i) {
		block_read(i+1, &(FAT[(i * BLOCK_SIZE/2)]));
	}
	return 0;
}

int fs_umount(void)
{

	block_write(superblock.rootdir_blk_index, &root_directory);
	for (int i = 0; i < superblock.fat_amount; ++i) {
		block_write(i + 1, &(FAT[i * (BLOCK_SIZE/2)]));
	}// 错
    for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i) {
		if (fd_table[i].entry != NULL)
			fs_error("There exist open file descriptors");
	}

	superblock = (const struct superblock){ 0 };
	memset(FAT, 0, sizeof(FAT));
	root_directory = (const struct root_directory){ 0 };
    memset(bounce, 0, sizeof(bounce));

	return 0;
}

int fs_info(void)
{
	int rdir_free = rdir_free_blocks();
	int fat_free = fat_free_blocks();
	fprintf(stdout, "FS Info:\n");
	fprintf(stdout, "total_blk_count=%d\n",		superblock.total_blocks);
	fprintf(stdout, "fat_blk_count=%d\n",		superblock.fat_amount);
	fprintf(stdout, "rdir_blk=%d\n",		superblock.rootdir_blk_index);
	fprintf(stdout, "data_blk=%d\n",		superblock.datablk_start_index);
	fprintf(stdout, "data_blk_count=%d\n",		superblock.datablk_amount);
	fprintf(stdout, "fat_free_ratio=%d/%d\n",	fat_free,	superblock.datablk_amount); 
	fprintf(stdout, "rdir_free_ratio=%d/%d\n",	rdir_free,	FS_FILE_MAX_COUNT);
	return 0;
}

int fs_create(const char *filename)
{
	int index = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
		if (root_directory.entry_array[i].filename[0] == '\0') {
			index = i;
			break;
		}
	}
	strcpy((char*)root_directory.entry_array[index].filename, filename);
	root_directory.entry_array[index].file_size = 0;
	root_directory.entry_array[index].datablk_start_index = 0xFFFF;  // FAT EOC = 0xFFFF
	return 0;
}

int fs_delete(const char *filename)
{
	int index = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
		if (strcmp((char*)root_directory.entry_array[i].filename, filename) == 0) {
			index = i;
			break;
		}
	}
	root_directory.entry_array[index].filename[0] = '\0';
	root_directory.entry_array[index].file_size = 0;
	if (root_directory.entry_array[index].datablk_start_index != 0xffff) {
		int delete_index = root_directory.entry_array[index].datablk_start_index;
		while (delete_index != 0xffff) {
			int FAT_num = FAT[delete_index];
			FAT[delete_index] = 0x0;
			delete_index = FAT_num;
		}
	}
	root_directory.entry_array[index].datablk_start_index = '\0';
	return 0;
}

int fs_ls(void)
{
	fprintf(stdout, "FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		fprintf(stdout, "file: %s, size: %d, data_blk: %d\n", 
			root_directory.entry_array[i].filename, 
			root_directory.entry_array[i].file_size, 
			root_directory.entry_array[i].datablk_start_index);
	}
	return 0;
}

int fs_open(const char *filename)
{
	int fd_table_index;
	int file_index = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i ++) {
		if (strcmp((char*)root_directory.entry_array[i].filename , filename) == 0) {
			file_index = i;
			break;
		}
	}
	for (int j = 0; j < FS_OPEN_MAX_COUNT; j ++) {
		if (fd_table[j].entry == NULL) {
			fd_table_index = j;
			break;
		}
	}
	fd_table[fd_table_index].entry = &(root_directory.entry_array[file_index]);
	return fd_table_index;
}

int fs_close(int fd)
{
	fd_table[fd].entry = NULL;
	fd_table[fd].offset = 0;
	return 0;
}

int fs_stat(int fd)
{
	return fd_table[fd].entry -> file_size;
}

int fs_lseek(int fd, size_t offset)
{
	fd_table[fd].offset = offset;
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	uint32_t total_written_count = 0;
	uint16_t offset_in_one_block = fd_table[fd].offset % BLOCK_SIZE;
	uint16_t current_block_index;
	uint16_t iteration_written_count;
    int finish_flag = 0;

	/* Error Checking */
	// Check if FS is mounted
	if (superblock.signature != 0x5346303531534345)
		fs_error("Filesystem not mounted");

	// Check if file descriptor is closed or out of bounds
	if (fd_table[fd].entry == NULL || fd >= FS_OPEN_MAX_COUNT)
		fs_error("Invalid file descriptor");

	if (buf == NULL)
		fs_error("buf is NULL");

	if (count == 0)
		return 0;//必须要有

	// checkif exist data block.
	if (fd_table[fd].entry->datablk_start_index == 0xFFFF) {
		current_block_index = block_create(fd);
	} else {
		current_block_index = FAT_iterator(fd_table[fd].entry->datablk_start_index, fd_table[fd].offset / BLOCK_SIZE);
	}
    while (finish_flag != 1) {
		if ( count - total_written_count >= (unsigned int)BLOCK_SIZE - offset_in_one_block) {
				iteration_written_count = (unsigned int)BLOCK_SIZE - offset_in_one_block;
		} else {
				iteration_written_count = count - total_written_count;
		}
        //read whole block into bounce
        block_read(current_block_index +superblock.datablk_start_index, &bounce );
        //copy the aimed area of data into bounce correct position
        memcpy(&bounce[offset_in_one_block], buf+total_written_count, iteration_written_count);
        total_written_count += iteration_written_count;
        //update file offset to the end of the current position
        fd_table[fd].offset += iteration_written_count;
        //write back bounce into datablock
        block_write(current_block_index +superblock.datablk_start_index, &bounce );
        //iterate through FAT[] or create new FAT entry
		if (FAT[current_block_index] == 0xFFFF) {
            int free_index;
            for (int i = 1; i < superblock.fat_amount * (BLOCK_SIZE/2); i++) {
                if (FAT[i] == 0){
                    free_index = i;
                    break;
                }
            }
            FAT[current_block_index] = free_index;
            FAT[free_index] = 0xFFFF;
			current_block_index = free_index;
		} else {
			current_block_index = FAT_iterator(current_block_index, 1);
		}
        //since after 1st dblock, their offset are at the beginning of the block
        offset_in_one_block = 0;
        if(count - total_written_count == 0) {
            finish_flag =1;
        }
    }
	// update file size by using offset(end of the file)
	if (fd_table[fd].entry->file_size < fd_table[fd].offset) {
		fd_table[fd].entry->file_size = fd_table[fd].offset;
    }    

	return total_written_count;
}

int fs_read(int fd, void *buf, size_t count)
{
	uint32_t total_read_count = 0;
	uint16_t offset_in_one_block = fd_table[fd].offset % BLOCK_SIZE;
	uint16_t current_block_index;
	uint16_t iteration_written_count;
    int finish_flag = 0;
}
