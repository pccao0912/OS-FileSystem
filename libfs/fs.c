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

int fs_mount(const char *diskname)
{
	/* TODO: Phase 1 */
	int opendisk = block_disk_open(diskname);
	if (opendisk == - 1) {
		return -1;	
	}
}

int fs_umount(void)
{
	/* TODO: Phase 1 */
}

int fs_info(void)
{
	/* TODO: Phase 1 */
}

int fs_create(const char *filename)
{
	/* TODO: Phase 2 */
}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
}

int fs_ls(void)
{
	/* TODO: Phase 2 */
}

int fs_open(const char *filename)
{
	/* TODO: Phase 3 */
}

int fs_close(int fd)
{
	/* TODO: Phase 3 */
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

