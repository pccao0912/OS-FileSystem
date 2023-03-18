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
	int error_flag = 0;
	if (opendisk == - 1) {
		return -1;	
	}
	error_flag = block_read(0, &superblock);
	if (error_flag != 0) {
		return -1;
	}

	error_flag = block_read(superblock.rootdir_blk_index, &root_directory);
	if (error_flag != 0) {
		return -1;
	}

	for ( int i = 0; i < superblock.fat_amount; ++i) {
		error_flag = block_read(i+1, &(FAT[(i * BLOCK_SIZE/2)]));
		if (error_flag != 0) {
			return -1;
		}
	}
	// check signature == ECS150FS
	if (superblock.signature != 0x5346303531534345) {
		return -1;
	}
	return 0;
}

int fs_umount(void)
{
	// No FS mounted
	if (!superblock.signature || superblock.signature != 0x5346303531534345) {
		return -1;
	}
	int error_flag = 0;
	error_flag = block_write(superblock.rootdir_blk_index, &root_directory);
	if (error_flag != 0) {
		return -1;
	}
	for (int i = 0; i < superblock.fat_amount; ++i) {
		error_flag = block_write(i + 1, &(FAT[i * (BLOCK_SIZE/2)]));
		if (error_flag != 0) {
			return -1;
		}
	}

	for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i) {
		if (fd_table[i].entry != NULL){
			return -1;
		}
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
	// FS not mounted
	if (!superblock.signature || superblock.signature != 0x5346303531534345) {
		return -1;
	}
	// filename invalid or filename is too long
	int filename_length = strlen(filename);
	if (!filename || filename_length >= FS_FILENAME_LEN) {
		return -1;
	}
	int index = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
		if (root_directory.entry_array[i].filename[0] == '\0') {
			index = i;
			break;
		}
		// filename already exist
		if (!strcmp(filename, (char*) root_directory.entry_array[i].filename)) {
			return -1;
		}
	}
	strcpy((char*)root_directory.entry_array[index].filename, filename);
	root_directory.entry_array[index].file_size = 0;
	root_directory.entry_array[index].datablk_start_index = 0xFFFF;  // FAT EOC = 0xFFFF
	return 0;
}

int fs_delete(const char *filename)
{
	// FS not mounted
	if (!superblock.signature || superblock.signature != 0x5346303531534345) {
		return -1;
	}
	// filename is invalid
	if (!filename || filename == NULL) {
		return -1;
	}
	// check if filename is opened
	for (int i = 0; i < FS_OPEN_MAX_COUNT; ++i) {
		if (!strcmp((char*)fd_table[i].entry->filename, filename)) {
			return -1;
		}
	}
	int index = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
		if (strcmp((char*)root_directory.entry_array[i].filename, filename) == 0) {
			index = i;
			break;
		}
	}
	// if no file named filename
	if (index == -1) {
		return -1;
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
	// FS not mounted
	if (!superblock.signature || superblock.signature != 0x5346303531534345) {
		return -1;
	}
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
	// No FS mounted
	if (!superblock.signature || superblock.signature != 0x5346303531534345) {
		return -1;
	}
	// filename invalid
	if (!filename || filename == NULL) {
		return -1;
	}
	int fd_table_index;
	int file_index = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
		if (strcmp((char*)root_directory.entry_array[i].filename, filename) == 0) {
			file_index = i;
			break;
		}
	}
	for (int j = 0; j < FS_OPEN_MAX_COUNT; ++j) {
		if (fd_table[j].entry == NULL) {
			fd_table_index = j;
			break;
		}
		// there are already %FS_OPEN_MAX_COUNT files currently open
		if (j == FS_OPEN_MAX_COUNT) {
			return -1;
		}
	}
	fd_table[fd_table_index].entry = &(root_directory.entry_array[file_index]);
	return fd_table_index;
}

int fs_close(int fd)
{
	// No FS mounted
	if (!superblock.signature || superblock.signature != 0x5346303531534345) {
		return -1;
	}
	// if file descriptor @fd is invalid (out of bounds or not currently open)
	if (fd >= FS_OPEN_MAX_COUNT || !fd_table[fd].entry) {
		return -1;
	}
	fd_table[fd].entry = NULL;
	fd_table[fd].offset = 0;
	return 0;
}

int fs_stat(int fd)
{
	// No FS mounted
	if (!superblock.signature || superblock.signature != 0x5346303531534345) {
		return -1;
	}
	// if file descriptor @fd is invalid (out of bounds or not currently open)
	if (fd >= FS_OPEN_MAX_COUNT || !fd_table[fd].entry) {
		return -1;
	}
	return fd_table[fd].entry -> file_size;
}

int fs_lseek(int fd, size_t offset)
{
	// No FS mounted
	if (!superblock.signature || superblock.signature != 0x5346303531534345) {
		return -1;
	}
	// if file descriptor @fd is invalid (out of bounds or not currently open)
	if (fd >= FS_OPEN_MAX_COUNT || !fd_table[fd].entry) {
		return -1;
	}
	// if @offset is larger than the current file size
	size_t current_file_size = fs_stat(fd);
	if (current_file_size < offset) {
		return -1;
	}
	fd_table[fd].offset = offset;
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	if (fd >= FS_OPEN_MAX_COUNT ) { 
		return -1;
	}
	if (superblock.signature != 0x5346303531534345) {
		return -1;
	}
	if (fd_table[fd].entry == NULL ) {
		return -1;
	}
	if (buf == NULL) {
		return -1;
	}
        if (count == 0){
		return 0;
	}
	uint32_t total_written_count = 0;
	uint16_t offset_in_one_block = fd_table[fd].offset % BLOCK_SIZE;
	uint16_t current_index;
	uint16_t iteration_written_count;
	int finish_flag = 0;
	// checkif exist data block.
	if (fd_table[fd].entry->datablk_start_index == 0xFFFF) {
		current_index = block_create(fd);
	} else {
		current_index = FAT_iterator(fd_table[fd].entry->datablk_start_index, fd_table[fd].offset / BLOCK_SIZE);
	}
	while (finish_flag != 1) {
		if ( count - total_written_count >= (unsigned int)BLOCK_SIZE - offset_in_one_block) {
				iteration_written_count = (unsigned int)BLOCK_SIZE - offset_in_one_block;
		} else {
				iteration_written_count = count - total_written_count;
		}
		//read whole block into bounce
		block_read(current_index +superblock.datablk_start_index, &bounce );
		//copy the aimed area of data into bounce correct position
		memcpy(&bounce[offset_in_one_block], buf+total_written_count, iteration_written_count);
		//write back bounce into datablock

		block_write(current_index +superblock.datablk_start_index, &bounce );
		//iterate through FAT[] or create new FAT entry
		if (FAT[current_index] == 0xFFFF) {
			int free_index;
			for (int i = 1; i < superblock.fat_amount * (BLOCK_SIZE/2); i++) {
				if (FAT[i] == 0){
					free_index = i;
					break;
				}
			}
			FAT[current_index] = free_index;
			FAT[free_index] = 0xFFFF;
			current_index = free_index;
		} else {
			current_index = FAT_iterator(current_index, 1);
		}
		total_written_count += iteration_written_count;
		//update file offset to the end of the current position
		fd_table[fd].offset += iteration_written_count;
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
	if (fd >= FS_OPEN_MAX_COUNT ) { 
		return -1;
	}
	if (superblock.signature != 0x5346303531534345) {
		return -1;
	}
	if (fd_table[fd].entry == NULL ) {
		return -1;
	}
	if (buf == NULL) {
		return -1;
	}
        if (count == 0){
		return 0;
	}
	uint32_t total_read_count = 0;
	uint16_t offset_in_one_block = fd_table[fd].offset % BLOCK_SIZE;
	uint16_t current_index;
	uint16_t iteration_read_count;
	uint32_t file_size = fd_table[fd].entry->file_size - fd_table[fd].offset;
	int finish_flag = 0;
	if (fd_table[fd].entry->file_size == 0) {
		finish_flag = 1;
	}
	current_index = FAT_iterator(fd_table[fd].entry->datablk_start_index, fd_table[fd].offset / BLOCK_SIZE);
	while(finish_flag != 1) {
		//In this way the amount of data each iteration will be restricted according to its size
		if ( count - total_read_count >= (unsigned int)BLOCK_SIZE - offset_in_one_block) {
				iteration_read_count = (unsigned int)BLOCK_SIZE - offset_in_one_block;
		} else {
				iteration_read_count = count - total_read_count;
		}
		// Handler small file if the file size is smaller than the iteration read count
		if (iteration_read_count > file_size) {
			iteration_read_count = file_size;
		}
		//read block into bounce buffer
		block_read(current_index + superblock.datablk_start_index, &bounce);
		//copy aimed area memory into buffer size : iteration__read_count position: offset_in_one_block
		memcpy(buf + total_read_count, &bounce[offset_in_one_block], iteration_read_count);
		total_read_count += iteration_read_count;
		fd_table[fd].offset += iteration_read_count;
		//for the following the offset in one block should be 0
		offset_in_one_block = 0;
		if (count - total_read_count == 0 ){
			finish_flag = 1;
		}
		if (FAT[current_index] == 0xFFFF) {
			break;
		} else {
			current_index = FAT_iterator(current_index,1);
		}
	}
	return total_read_count;
}
