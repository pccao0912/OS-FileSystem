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
uint8_t bounce[BLOCK_SIZE];
// helper functs
int block_create(){
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
	return index;
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
	for (int i = 1; i < superblock.fat_amount + 1; ++i) {
		block_write(i, &(FAT[i-1 * BLOCK_SIZE/2]));
	}
	superblock = (const struct superblock){ 0 };
	memset(FAT, 0, sizeof(FAT));
	root_directory = (const struct root_directory){ 0 };
	return 0;
}

int fs_info(void)
{
	fprintf(stdout, "FS Info:\n");
	fprintf(stdout, "total_blk_count=%d\n",		superblock.total_blocks);
	fprintf(stdout, "fat_blk_count=%d\n",		superblock.fat_amount);
	fprintf(stdout, "rdir_blk=%d\n",		superblock.rootdir_blk_index);
	fprintf(stdout, "data_blk=%d\n",		superblock.datablk_start_index);
	fprintf(stdout, "data_blk_count=%d\n",		superblock.datablk_amount);
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
	int first_dblock_start = 0;
	int file_index_directory = 0;
	int remaining_count = count;
	int buffer_written = 0; //tracker how much written to buffer
	int temp_offset_fd = fd_list[fd].offset;//tracker of where to begin
	int block_offset = temp_offset_fd % BLOCK_SIZE;
	//get first dblock index
	for (int i = 0 ; i < FS_FILE_MAX_COUNT ; i++) {
		if (root_directory.entry_array[i].filename == fd_table[fd].entry->filename) {
			first_dblock_start = root_directory.entry_array[i].datablk_start_index;
			file_index_directory = i;
		}
	}
	// create block if file is new
	if (fd_list[fd].entry->datablk_start_index == 0xFFFF) {
		int new_block_index = block_create();
		if (new_block_index == -1) {
			return 0; //no more space for creating a new block
		}
		root_directory.entry_array[file_index_directory].datablk_start_index = new_block_index;
		first_dblock_start = new_block_index;
	}
	// begin writing
	do {
		int write_index = (temp_offset_fd/BLOCK_SIZE) + first_dblock_start + superblock.datablk_start_index;
		block_read(write_index, bounce);
		if (block_offset + remaining_to_write <= BLOCK_SIZE) {
			memcpy(bounce+block_offset, buffer_written+ buf, remaining_count);
			temp_offset_fd += remaining_count;
			buffer_written += remaining_count;
			remaining_count = remaining_count - BLOCK_SIZE;
			block_write(write_index, bounce);
		} else if (block_offset + remaining_to_write > BLOCK_SIZE) {
			int amount_can_fit =  BLOCK_SIZE - block_offset;
			memcpy(bounce+block_offset, buffer_written+ buf, amount_can_fit);
			temp_offset_fd += amount_can_fit;
			buffer_written += amount_can_fit;
			remaining_count = remaining_count - amount_can_fit;
			block_write(write_index, bounce);
			int new_block_index = block_create();
			FAT[write_index] = new_block_index;
			FAT[new_block_index] = 0xFFFF;
		}
	} while (remaining_count >0)
	if (fd_list[fd].entry->file_size < fd_list[fd].offset) {
			fd_list[fd].entry->file_size = fd_list[fd].offset;
	}
	return buffer_written;
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}
