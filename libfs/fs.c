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
}

// global
struct superblock superblock;
struct root_directory root_directory;
struct file_descriptor fd_table[FS_OPEN_MAX_COUNT];

int fs_mount(const char *diskname)
{
	int opendisk = block_disk_open(diskname);
	if (opendisk == - 1) {
		return -1;	
	}
	block_read(0, &superblock);
	block_read(superblock.rootdir_blk_index, &root_directory);
	for ( int i = 0; i < superblock.fat_amount; ++i) {
		block_read(i+1, &(FAT[i * BLOCK_SIZE/2)]));
	}
	return 0;
}

int fs_umount(void)
{
	block_write(superblock.rootdir_blk_index, &root_directory);
	for (int i = 1; i < superblock.fat_amount + 1; ++i) {
		block_write(i, &(FAT[i-1 * BLOCK_SIZE/2]))
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
	strcpy(root_directory.entry_array[index].filename, filename);
	root_directory.entry_array[index].file_size = 0;
	root_directory.entry_array[index].datablk_start_index = 0xFFFF;  // FAT EOC = 0xFFFF
	return 0;
}

int fs_delete(const char *filename)
{
	int index = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; ++i) {
		if (root_directory.entry_array[i].filename[0] == filename) {
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
	/* TODO: Phase 3 */
}

int fs_close(int fd)
{
	fd_table[fd].entry = NULL;
	fd_list[fd].offset = 0;
	return 0;
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
}

int fs_lseek(int fd, size_t offset)
{
	int file_size = fs_stat(fd);
	fd_list[fd].offset = offset;

	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

