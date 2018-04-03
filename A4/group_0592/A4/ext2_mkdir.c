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
	if(argc != 3) {
		fprintf(stderr, "Usage: %s <image file name> <absolute path>\n",
				argv[0]);
		exit(1);
	}

	// Open disk
	int fd = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(disk == MAP_FAILED) {
		perror("mmap");
		exit(errno);
	}

	// Check absolute path
	char argv2_copy[strlen(argv[2]) + 1];
	strcpy(argv2_copy, argv[2]);
	char *token_length;
	int pathlength = 0;
	token_length = strtok(argv2_copy, "/");
	while( token_length != NULL ){
		pathlength++;
		token_length = strtok(NULL, "/");
	}

	// When the destination path is root("/"), return EEXIST
	if(pathlength == 0){
		errno = EEXIST;
		perror("EEXIST");
		exit(EEXIST);
	}

	// Check the length of directory name
	char argv2_copy2[strlen(argv[2]) + 1];
	strcpy(argv2_copy2, argv[2]);
	char dir_name[EXT2_NAME_LEN];
	int depth_name = 1;
	char *token_name;
	token_name = strtok(argv2_copy2, "/");
	while( token_name != NULL ){
		if (depth_name == pathlength) {
			if (strlen(token_name) > EXT2_NAME_LEN) {
				errno = ENAMETOOLONG;
	    		perror("ENAMETOOLONG");
	    		exit(ENAMETOOLONG);
			}
			strcpy(dir_name, token_name);
		}
		token_name = strtok(NULL, "/");
		depth_name++;
	}

	struct ext2_super_block *sb = (struct ext2_super_block *)index(disk, 1);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)index(disk, 2);

	// Check there is remaining block
	if (gd->bg_free_blocks_count == 0){
		errno = ENOSPC;
		perror("ENOSPC");
		exit(ENOSPC);
	}

	// Check there is remaining inode
	if (gd->bg_free_inodes_count == 0){
		errno = ENOSPC;
		perror("ENOSPC");
		exit(ENOSPC);
	}

	unsigned char *block_bm = index(disk, gd->bg_block_bitmap);
	unsigned char *inode_bm = index(disk, gd->bg_inode_bitmap);

	// Get the first free inode
	int finishedinode = 0;
	int freeinodeidx = 0;
	for(int i = 0; (i < (sb->s_inodes_count / 8)) && !finishedinode; i++) {
		for(int j = 0; (j < 8) && !finishedinode; j++){
			int bit = *(inode_bm + i) & (1 << j);
			if (!bit){
				finishedinode = 1;
				freeinodeidx = (8 * i) + j;
			}
		}
	}

	// Get the first free block
	int finishedblock = 0;
	int freeblockidx;
	for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
		for(int l = 0; (l < 8) && !finishedblock; l++){
			int bit = *(block_bm + k) & (1 << l);
			if (!bit){
				finishedblock = 1;
				freeblockidx = (8 * k) + l + 1; // block index starts from 1
			}
		}
	}

	struct ext2_inode *inode = (struct ext2_inode *)index(disk, gd->bg_inode_table);
	struct ext2_inode *root = &inode[EXT2_ROOT_INO - 1];

	// Parse the absolute path
	int find_root = 0;
	int find_path = 0;
	int find_inode = 0;
	int depth = 1;
	unsigned short rest_reclen = EXT2_BLOCK_SIZE;
	char *cur_path;
	char *token;
	token = strtok(argv[2], "/");

	while( token != NULL ) {

		cur_path = token;

		// Check every object(file/directory/symlink) in root directory when the destination path is not under root directory
		if(find_root == 0 && pathlength > 1) {
			for(int i = 0; root->i_block[i] != 0; i++) {
				struct ext2_dir_entry *root_dir_entry = (struct ext2_dir_entry*)index(disk, root->i_block[i]);
				rest_reclen = EXT2_BLOCK_SIZE;
				while(rest_reclen > 0) {
					if(strncmp(root_dir_entry->name, cur_path, root_dir_entry->name_len) == 0 && root_dir_entry->name_len == strlen(cur_path) && root_dir_entry->inode != 0) {
						find_root = 1;
						find_inode = root_dir_entry->inode - 1;
						if(root_dir_entry->file_type != EXT2_FT_DIR){
							errno = ENOTDIR;
							perror("ENOTDIR");
							exit(ENOTDIR);
						}
					}
					rest_reclen = rest_reclen - root_dir_entry->rec_len;
					if (rest_reclen > 0) {
						root_dir_entry = (struct ext2_dir_entry *)((void*)root_dir_entry + root_dir_entry->rec_len);
					}
				}
			}
		}

		// When we want to create a directory in the root directory
		if(find_root == 0 && pathlength == 1){
			for(int i = 0; root->i_block[i] != 0; i++) {
				struct ext2_dir_entry *root_dir_entry = (struct ext2_dir_entry*)index(disk, root->i_block[i]);
				rest_reclen = EXT2_BLOCK_SIZE;
				while(rest_reclen > 0) {
					if(strncmp(root_dir_entry->name, cur_path, root_dir_entry->name_len) == 0 && root_dir_entry->name_len == strlen(cur_path) && root_dir_entry->inode != 0) {
						errno = EEXIST; // an object with the same name already exists
						perror("EEXIST");
						exit(EEXIST);
					}
					rest_reclen = rest_reclen - root_dir_entry->rec_len;
					if (rest_reclen > 0) {
						root_dir_entry = (struct ext2_dir_entry *)((void*)root_dir_entry + root_dir_entry->rec_len);
					}
					
				}
			}
			// When destination path not found, then we need to create a directory in the root directory
			if (find_root == 0) {
				find_inode = EXT2_ROOT_INO - 1;
				find_root = 1;
			}
		}

		// Destination path is not under root directory and the desired directory in root directory is not found, we return ENOENT
		if(find_root == 0) {
			errno = ENOENT;
			perror("ENOENT");
			exit(ENOENT);
		}

		// Continue check the absolute path
		if(find_root == 2) {

			struct ext2_inode *path_inode = &inode[find_inode];

			for(int i = 0; path_inode->i_block[i] != 0; i++) {
				struct ext2_dir_entry *path_dir_entry = (struct ext2_dir_entry*)index(disk, path_inode->i_block[i]);
				rest_reclen = EXT2_BLOCK_SIZE;
				while(rest_reclen > 0) {
					if(strncmp(path_dir_entry->name, cur_path, path_dir_entry->name_len) == 0 && path_dir_entry->name_len == strlen(cur_path) && path_dir_entry->inode != 0) {
						find_path = 1;
						find_inode = path_dir_entry->inode - 1;
						if (pathlength == depth) {
							errno = EEXIST;
							perror("EEXIST");
							exit(EEXIST);
						}
						else {
							if(path_dir_entry->file_type != EXT2_FT_DIR) {
								errno = ENOTDIR;
								perror("ENOTDIR");
								exit(ENOTDIR);
							}
						}
					}
					rest_reclen = rest_reclen - path_dir_entry->rec_len;
					if (rest_reclen > 0) {
						path_dir_entry = (struct ext2_dir_entry *)((void*)path_dir_entry + path_dir_entry->rec_len);
					}
				}
			}

			// When we get to the end of the absolute path
			if (pathlength == depth && find_path == 0) {
				find_path = 1;
			}

			// Destination path not found before we get to the end of the absolute path, we return ENOENT
			if(find_path == 0) {
				errno = ENOENT;
				perror("ENOENT");
				exit(ENOENT);
			}
		}

		find_path = 0;
		find_root = 2;
		depth++;

		token = strtok(NULL, "/");
	}

	struct ext2_inode *dest_inode = &inode[find_inode];

	// Update File system
	struct ext2_dir_entry *dest_dir_entry_update;
	int freepointer = 0;
	int need_new_dir_entry = 0;
	int finish_dir_entry = 0;

	// Check whether we need a new block for directory entry
	for(int i = 0; dest_inode->i_block[i] != 0 && !finish_dir_entry; i++) {
		dest_dir_entry_update = (struct ext2_dir_entry*)index(disk, dest_inode->i_block[i]);

		rest_reclen = EXT2_BLOCK_SIZE;
		while(rest_reclen != dest_dir_entry_update->rec_len) {
			rest_reclen = rest_reclen - dest_dir_entry_update->rec_len;
			dest_dir_entry_update = (struct ext2_dir_entry *)((void*)dest_dir_entry_update + dest_dir_entry_update->rec_len);
		}

		int cur_last_reclen = (unsigned short)dest_dir_entry_update->name_len + sizeof(struct ext2_dir_entry);
		if((cur_last_reclen % 4) != 0) {
			cur_last_reclen = ((cur_last_reclen / 4) + 1) * 4;
		}

		int new_reclen = strlen(dir_name) + sizeof(struct ext2_dir_entry);
		if((new_reclen % 4) != 0) {
			new_reclen = ((new_reclen / 4) + 1) * 4;
		}
		if(dest_dir_entry_update->rec_len < (cur_last_reclen + new_reclen) && dest_inode->i_block[i + 1] == 0) {
			need_new_dir_entry = 1;
		}

		if(dest_dir_entry_update->rec_len >= (cur_last_reclen + new_reclen)) {
			finish_dir_entry = 1;
		}

		freepointer = i;
	}

	// Directory entry length not enough for all current directory entries, need a new block for directory entry
	if(need_new_dir_entry == 1) {

		// Check there are enough blocks
		if (gd->bg_free_blocks_count < 2){
			errno = ENOSPC;
			perror("ENOSPC");
			exit(ENOSPC);
		}

		finishedblock = 0;
		for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
			for(int l = 0; (l < 8) && !finishedblock; l++){
				int bit = *(block_bm + k) & (1 << l);
				if (!bit){
					finishedblock = 1;
					freeblockidx = (8 * k) + l + 1; // block index starts from 1
					*(block_bm + k) = *(block_bm + k) | (1 << l);
				}
			}
		}

		// Update parent directory's inode
		freepointer++;
		dest_inode->i_block[freepointer] = freeblockidx;
		dest_inode->i_blocks = dest_inode->i_blocks + 2;
		dest_inode->i_size = dest_inode->i_size + EXT2_BLOCK_SIZE;

		// Clear the memory block before set the new directory entry
		void *clear_mem = (void*)index(disk, dest_inode->i_block[freepointer]);
		memset(clear_mem, 0, EXT2_BLOCK_SIZE);

		// Update directory entry
		dest_dir_entry_update = (struct ext2_dir_entry*)index(disk, dest_inode->i_block[freepointer]);
		dest_dir_entry_update->inode = (freeinodeidx + 1);
		strcpy(dest_dir_entry_update->name, dir_name);
		dest_dir_entry_update->name_len = strlen(dir_name);
		dest_dir_entry_update->rec_len = EXT2_BLOCK_SIZE;
		dest_dir_entry_update->file_type = EXT2_FT_DIR;
		
		// Update super block and block descriptor
		gd->bg_free_blocks_count--; // one block for new directory entry
		sb->s_free_blocks_count--; // one block for new directory entry

	}
	else {
		// Update directory entry
		unsigned short cur_reclen = dest_dir_entry_update->rec_len;
		unsigned short tmp_reclen = (unsigned short)dest_dir_entry_update->name_len + sizeof(struct ext2_dir_entry); // 8
		if((tmp_reclen % 4) != 0){
			tmp_reclen = ((tmp_reclen / 4) + 1) * 4;
		}
		dest_dir_entry_update->rec_len = tmp_reclen;
		dest_dir_entry_update = (struct ext2_dir_entry *)((void*)dest_dir_entry_update + dest_dir_entry_update->rec_len);
		dest_dir_entry_update->rec_len = cur_reclen - tmp_reclen;
		dest_dir_entry_update->inode = (freeinodeidx + 1);
		strcpy(dest_dir_entry_update->name, dir_name);
		dest_dir_entry_update->name_len = strlen(dir_name);
		dest_dir_entry_update->file_type = EXT2_FT_DIR;
	}

	// Update parent's links count
	dest_inode->i_links_count++;

	// Update inode except i_block[0]
	struct ext2_inode *new_dir_inode = &inode[freeinodeidx];
	new_dir_inode->i_mode = EXT2_S_IFDIR;
	new_dir_inode->i_uid = 0;
	new_dir_inode->i_size = EXT2_BLOCK_SIZE;
	new_dir_inode->i_gid = 0;
	new_dir_inode->i_links_count = 2;
	new_dir_inode->osd1 = 0;
	new_dir_inode->i_generation = 0;
	new_dir_inode->i_faddr = 0;
	new_dir_inode->i_file_acl = 0;
	new_dir_inode->i_dir_acl = 0;
	new_dir_inode->i_dtime = 0;
	new_dir_inode->i_blocks = 2;
	for (int i = 1; i < 15; i++) {
		new_dir_inode->i_block[i] = 0;
	}

	// Update data block & block bitmap & i_block[0] in inode
	finishedblock = 0;
	for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
		for(int l = 0; (l < 8) && !finishedblock; l++){
			int bit = *(block_bm + k) & (1 << l);
			if (!bit){
				finishedblock = 1;
				freeblockidx = (8 * k) + l + 1; // block index starts from 1
				*(block_bm + k) = *(block_bm + k) | (1 << l);
			}
		}
	}
	new_dir_inode->i_block[0] = freeblockidx;

	// Clear the memory block before set the new directory entry
	void *clear_mem = (void*)index(disk, new_dir_inode->i_block[0]);
	memset(clear_mem, 0, EXT2_BLOCK_SIZE);

	// Set the first directory entry (for itself)
	struct ext2_dir_entry *new_dir_entry = (struct ext2_dir_entry*)index(disk, new_dir_inode->i_block[0]);
	new_dir_entry->inode = (freeinodeidx + 1);
	char self[2] = ".";
	strcpy(new_dir_entry->name, self);
	new_dir_entry->name_len = strlen(new_dir_entry->name);
	new_dir_entry->file_type = EXT2_FT_DIR;
	new_dir_entry->rec_len = (unsigned short)dest_dir_entry_update->name_len + sizeof(struct ext2_dir_entry);
	if((new_dir_entry->rec_len % 4) != 0){
		new_dir_entry->rec_len = ((new_dir_entry->rec_len / 4) + 1) * 4;
	}

	// Set the second directory entry (for parent diretory)
	unsigned short pre_reclen = new_dir_entry->rec_len;
	new_dir_entry = (struct ext2_dir_entry *)((void*)new_dir_entry + new_dir_entry->rec_len);
	new_dir_entry->inode = (find_inode + 1);
	char parent[3] = "..";
	strcpy(new_dir_entry->name, parent);
	new_dir_entry->name_len = strlen(new_dir_entry->name);
	new_dir_entry->file_type = EXT2_FT_DIR;
	new_dir_entry->rec_len = EXT2_BLOCK_SIZE - pre_reclen;

	// Update group descriptor
	gd->bg_free_inodes_count--;
	gd->bg_free_blocks_count--;
	gd->bg_used_dirs_count++;

	// Update super block
	sb->s_free_inodes_count--;
	sb->s_free_blocks_count--;

	// Update inode bitmap
    finishedinode = 0;
    for(int i = 0; (i < (sb->s_inodes_count / 8)) && !finishedinode; i++) {
    	for(int j = 0; (j < 8) && !finishedinode; j++){
			int bit = *(inode_bm + i) & (1 << j);
			if (!bit){
				finishedinode = 1;
				*(inode_bm + i) = *(inode_bm + i) | (1 << j);
			}
    	}
    }

    int close_error;	
	close_error = close(fd);
	if (close_error != 0) {
		perror("close fd");
		exit(errno);
	}

	return 0;
}
