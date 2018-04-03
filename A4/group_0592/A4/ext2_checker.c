#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include "ext2.h"

unsigned char *disk;
#define index(disk, i) (disk + (i * EXT2_BLOCK_SIZE))

int main(int argc, char **argv) {

	// Check arguments
	if(argc != 2) {
		fprintf(stderr, "Usage: %s <image file name>\n",
				argv[0]);
		exit(1);
	}

	int fd = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(disk == MAP_FAILED) {
		perror("mmap");
		exit(errno);
	}

	int total_fix = 0;

	struct ext2_super_block *sb = (struct ext2_super_block *)index(disk, 1);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)index(disk, 2);

	unsigned char *block_bm = index(disk, gd->bg_block_bitmap);
	unsigned char *inode_bm = index(disk, gd->bg_inode_bitmap);

	struct ext2_inode *inode = (struct ext2_inode *)index(disk, gd->bg_inode_table);
	struct ext2_inode *root = &inode[EXT2_ROOT_INO - 1];

	int dir_inodes[sb->s_inodes_count];
	for(int i = 0; i < sb->s_inodes_count; i++) {
		dir_inodes[i] = 0;
	}
	int dir_inodes_idx = 0;
	int rest_reclen = EXT2_BLOCK_SIZE;

	int finishedinode = 0;
	int finishedblock = 0;

	// Check and fix free inodes count
	int free_inode = 0;
	for(int i = 0; i < (sb->s_inodes_count / 8); i++) {
		for(int j = 0; j < 8; j++){
			int bit = *(inode_bm + i) & (1 << j);
			if (!bit){
				free_inode++;
			}
		}
	}
	if(sb->s_free_inodes_count != free_inode) {
		printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n", abs(free_inode - sb->s_free_inodes_count));
		total_fix = total_fix + abs(free_inode - sb->s_free_inodes_count);
		sb->s_free_inodes_count = free_inode;
	}
	if(gd->bg_free_inodes_count != free_inode) {
		printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap\n", abs(free_inode - gd->bg_free_inodes_count));
		total_fix = total_fix + abs(free_inode - gd->bg_free_inodes_count); // not sure whether should add this twice
		gd->bg_free_inodes_count = free_inode;
	}

	// Check and fix free blocks count
	int free_block = 0;
	for(int i = 0; i < (sb->s_blocks_count / 8); i++) {
		for(int j = 0; j < 8; j++){
			int bit = *(block_bm + i) & (1 << j);
			if (!bit){
				free_block++;
			}
		}
	}
	if(sb->s_free_blocks_count != free_block) {
		printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n", abs(free_block - sb->s_free_blocks_count));
		total_fix = total_fix + abs(free_block - sb->s_free_blocks_count);
		sb->s_free_blocks_count = free_block;
	}
	if(gd->bg_free_blocks_count != free_block) {
		printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap\n", abs(free_block - gd->bg_free_blocks_count));
		total_fix = total_fix + abs(free_block - gd->bg_free_blocks_count); // not sure whether should add this twice
		gd->bg_free_blocks_count = free_block;
	}

	// Check and fix i_dtime for root directory
	if(root->i_dtime !=0) {
		root->i_dtime = 0;
		printf("Fixed: valid inode marked for deletion: [%d]\n", EXT2_ROOT_INO);
		total_fix++;
	}

	// Check and fix inode bitmap for root directory
	finishedinode = 0;
	for(int k = 0; (k < (sb->s_inodes_count / 8)) && !finishedinode; k++) {
		for(int l = 0; (l < 8) && !finishedinode; l++){
			int bit_set = *(inode_bm + k) & (1 << l);
			int bit = (k * 8) + l;
			if((EXT2_ROOT_INO - 1) == bit && !bit_set) {
				*(inode_bm + k) = *(inode_bm + k) | (1 << l);
				printf("Fixed: inode [%d] not marked as in-use\n", EXT2_ROOT_INO);
				total_fix++;
				sb->s_free_inodes_count--;
				gd->bg_free_inodes_count--;
				finishedinode = 1;
			}
			if((EXT2_ROOT_INO - 1) < bit){
				finishedinode = 1;
			}
		}
	}

	// Check and fix block bitmap for root directory
	int block_fix_count = 0;
	for(int i = 0; root->i_block[i] != 0; i++) {
		int block_check = root->i_block[i];
		finishedblock = 0;
		for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
			for(int l = 0; (l < 8) && !finishedblock; l++){
				int bit_set = *(block_bm + k) & (1 << l);
				int bit = (k * 8) + l + 1;
				if(block_check == bit && !bit_set) {
					*(block_bm + k) = *(block_bm + k) | (1 << l);
					block_fix_count++;
					total_fix++;
					sb->s_free_blocks_count--;
					gd->bg_free_blocks_count--;
					finishedblock = 1;
				}
				if(block_check < bit){
					finishedblock = 1;
				}
			}
		}
	}
	if(block_fix_count > 0) {
		printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", block_fix_count, EXT2_ROOT_INO);
	}

	// Check and fix the files/directories/symlinks in root directory
	for(int i = 0; root->i_block[i] != 0; i++) {
		struct ext2_dir_entry *root_dir_entry = (struct ext2_dir_entry*)index(disk, root->i_block[i]);
		rest_reclen = EXT2_BLOCK_SIZE;
		while(rest_reclen > 0) {
			if(root_dir_entry->inode != 0) {
				struct ext2_inode *cur_inode = &inode[(root_dir_entry->inode - 1)];
				// Check and fix i_dtime
				if(cur_inode->i_dtime !=0) {
					cur_inode->i_dtime = 0;
					printf("Fixed: valid inode marked for deletion: [%d]\n", root_dir_entry->inode);
					total_fix++;
				}
				// Check and fix file type
				if((cur_inode->i_mode & 0xF000) == EXT2_S_IFLNK && root_dir_entry->file_type != EXT2_FT_SYMLINK) {
					root_dir_entry->file_type = EXT2_FT_SYMLINK;
					printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", root_dir_entry->inode);
					total_fix++;
				}
				if((cur_inode->i_mode & 0xF000) == EXT2_S_IFREG && root_dir_entry->file_type != EXT2_FT_REG_FILE) {
					root_dir_entry->file_type = EXT2_FT_REG_FILE;
					printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", root_dir_entry->inode);
					total_fix++;
				}
				if((cur_inode->i_mode & 0xF000) == EXT2_S_IFDIR && root_dir_entry->file_type != EXT2_FT_DIR) {
					root_dir_entry->file_type = EXT2_FT_DIR;
					printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", root_dir_entry->inode);
					total_fix++;
				}
				// Check and fix inode bitmap
				finishedinode = 0;
				for(int k = 0; (k < (sb->s_inodes_count / 8)) && !finishedinode; k++) {
					for(int l = 0; (l < 8) && !finishedinode; l++){
						int bit_set = *(inode_bm + k) & (1 << l);
						int bit = (k * 8) + l;
						if((root_dir_entry->inode - 1) == bit && !bit_set) {
							*(inode_bm + k) = *(inode_bm + k) | (1 << l);
							printf("Fixed: inode [%d] not marked as in-use\n", root_dir_entry->inode);
							total_fix++;
							sb->s_free_inodes_count--;
							gd->bg_free_inodes_count--;
							finishedinode = 1;
						}
						if((root_dir_entry->inode - 1) < bit){
							finishedinode = 1;
						}
					}
				}
				// Check and fix block bitmap
				block_fix_count = 0;
				for(int i = 0; cur_inode->i_block[i] != 0; i++) {
					int block_check = cur_inode->i_block[i];
					finishedblock = 0;
					for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
						for(int l = 0; (l < 8) && !finishedblock; l++){
							int bit_set = *(block_bm + k) & (1 << l);
							int bit = (k * 8) + l + 1;
							if(block_check == bit && !bit_set) {
								*(block_bm + k) = *(block_bm + k) | (1 << l);
								block_fix_count++;
								total_fix++;
								sb->s_free_blocks_count--;
								gd->bg_free_blocks_count--;
								finishedblock = 1;
							}
							if(block_check < bit){
								finishedblock = 1;
							}
						}
					}
				}
				if(cur_inode->i_block[12] != 0) {
					int *indirection_pointer = (int*)index(disk, cur_inode->i_block[12]);
					for(int k = 0; indirection_pointer[k] != 0; k++) {
						int block_check = indirection_pointer[k];
						finishedblock = 0;
						for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
							for(int l = 0; (l < 8) && !finishedblock; l++){
								int bit_set = *(block_bm + k) & (1 << l);
								int bit = (k * 8) + l + 1;
								if(block_check == bit && !bit_set) {
									*(block_bm + k) = *(block_bm + k) | (1 << l);
									block_fix_count++;
									total_fix++;
									sb->s_free_blocks_count--;
									gd->bg_free_blocks_count--;
									finishedblock = 1;
								}
								if(block_check < bit){
									finishedblock = 1;
								}
							}
						}
					}
				}
				if(block_fix_count > 0) {
					printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", block_fix_count, root_dir_entry->inode);
				}
				// If current object is a directory, mark it so we can check the files/directories/symlinks in this directory later
				if(root_dir_entry->file_type == EXT2_FT_DIR && strcmp(root_dir_entry->name, ".") != 0 && strcmp(root_dir_entry->name, "..") != 0 && strcmp(root_dir_entry->name, "lost+found") != 0) {
					dir_inodes[dir_inodes_idx] = root_dir_entry->inode - 1; // Store inode index rather than inode number
					dir_inodes_idx++;
				}
			}
			rest_reclen = rest_reclen - root_dir_entry->rec_len;
			if (rest_reclen > 0) {
				root_dir_entry = (struct ext2_dir_entry *)((void*)root_dir_entry + root_dir_entry->rec_len);
			}
		}
	}

	// Check and fix the files/directories/symlinks in all other directories
	for(int x = 0; dir_inodes[x] != 0; x++) {
		struct ext2_inode *parent_inode = &inode[dir_inodes[x]];
		for(int i = 0; parent_inode->i_block[i] != 0; i++) {
			struct ext2_dir_entry *cur_dir_entry = (struct ext2_dir_entry*)index(disk, parent_inode->i_block[i]);
			rest_reclen = EXT2_BLOCK_SIZE;
			while(rest_reclen > 0) {
				if(cur_dir_entry->inode != 0) {
					struct ext2_inode *cur_inode = &inode[(cur_dir_entry->inode - 1)];
					// Check and fix i_dtime
					if(cur_inode->i_dtime !=0) {
						cur_inode->i_dtime = 0;
						printf("Fixed: valid inode marked for deletion: [%d]\n", cur_dir_entry->inode);
						total_fix++;
					}
					// Check and fix file type
					if((cur_inode->i_mode & 0xF000) == EXT2_S_IFLNK && cur_dir_entry->file_type != EXT2_FT_SYMLINK) {
						cur_dir_entry->file_type = EXT2_FT_SYMLINK;
						printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", cur_dir_entry->inode);
						total_fix++;
					}
					if((cur_inode->i_mode & 0xF000) == EXT2_S_IFREG && cur_dir_entry->file_type != EXT2_FT_REG_FILE) {
						cur_dir_entry->file_type = EXT2_FT_REG_FILE;
						printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", cur_dir_entry->inode);
						total_fix++;
					}
					if((cur_inode->i_mode & 0xF000) == EXT2_S_IFDIR && cur_dir_entry->file_type != EXT2_FT_DIR) {
						cur_dir_entry->file_type = EXT2_FT_DIR;
						printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", cur_dir_entry->inode);
						total_fix++;
					}
					// Check and fix inode bitmap
					finishedinode = 0;
					for(int k = 0; (k < (sb->s_inodes_count / 8)) && !finishedinode; k++) {
						for(int l = 0; (l < 8) && !finishedinode; l++){
							int bit_set = *(inode_bm + k) & (1 << l);
							int bit = (k * 8) + l;
							if((cur_dir_entry->inode - 1) == bit && !bit_set) {
								*(inode_bm + k) = *(inode_bm + k) | (1 << l);
								printf("Fixed: inode [%d] not marked as in-use\n", cur_dir_entry->inode);
								total_fix++;
								sb->s_free_inodes_count--;
								gd->bg_free_inodes_count--;
								finishedinode = 1;
							}
							if((cur_dir_entry->inode - 1) < bit){
								finishedinode = 1;
							}
						}
					}
					// Check and fix block bitmap
					block_fix_count = 0;
					for(int i = 0; cur_inode->i_block[i] != 0; i++) {
						int block_check = cur_inode->i_block[i];
						finishedblock = 0;
						for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
							for(int l = 0; (l < 8) && !finishedblock; l++){
								int bit_set = *(block_bm + k) & (1 << l);
								int bit = (k * 8) + l + 1;
								if(block_check == bit && !bit_set) {
									*(block_bm + k) = *(block_bm + k) | (1 << l);
									block_fix_count++;
									total_fix++;
									sb->s_free_blocks_count--;
									gd->bg_free_blocks_count--;
									finishedblock = 1;
								}
								if(block_check < bit){
									finishedblock = 1;
								}
							}
						}
					}
					if(cur_inode->i_block[12] != 0) {
						int *indirection_pointer = (int*)index(disk, cur_inode->i_block[12]);
						for(int k = 0; indirection_pointer[k] != 0; k++) {
							int block_check = indirection_pointer[k];
							finishedblock = 0;
							for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
								for(int l = 0; (l < 8) && !finishedblock; l++){
									int bit_set = *(block_bm + k) & (1 << l);
									int bit = (k * 8) + l + 1;
									if(block_check == bit && !bit_set) {
										*(block_bm + k) = *(block_bm + k) | (1 << l);
										block_fix_count++;
										total_fix++;
										sb->s_free_blocks_count--;
										gd->bg_free_blocks_count--;
										finishedblock = 1;
									}
									if(block_check < bit){
										finishedblock = 1;
									}
								}
							}
						}
					}
					if(block_fix_count > 0) {
						printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", block_fix_count, cur_dir_entry->inode);
					}
					// If current object is a directory, mark it so we can check the files/directories/symlinks in this directory later
					if(cur_dir_entry->file_type == EXT2_FT_DIR && strcmp(cur_dir_entry->name, ".") != 0 && strcmp(cur_dir_entry->name, "..") != 0) {
						dir_inodes[dir_inodes_idx] = cur_dir_entry->inode - 1; // Store inode index rather than inode number
						dir_inodes_idx++;
					}
				}
				rest_reclen = rest_reclen - cur_dir_entry->rec_len;
				if (rest_reclen > 0) {
					cur_dir_entry = (struct ext2_dir_entry *)((void*)cur_dir_entry + cur_dir_entry->rec_len);
				}
			}
		}
	}

	if(total_fix == 0) {
		printf("No file system inconsistencies detected!\n");
	}
	else {
		printf("%d file system inconsistencies repaired!\n", total_fix);
	}

	int close_error;
	close_error = close(fd);
	if (close_error != 0) {
		perror("close fd");
		exit(errno);
	}

	return 0;
}

