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
#include "functions.h"
int pathfinder(unsigned char* disk, char* input){
	int find_inode = -1;
	char *token;
	int pathlength = 0;

	if(input[0] != '/') {
		errno = ENOENT;
		perror("Path need to start with '/'");
		return -ENOENT;
	}
	int length = strlen(input);
	char temp[length+1];
	char path[length+1];
	strcpy(temp, input);
	strcpy(path, input);
	token = strtok(temp, "/");
	while( token != NULL ) {
		pathlength++;
		token = strtok(NULL, "/");
	}
	if(pathlength == 1){
		find_inode = EXT2_ROOT_INO - 1;
	}else{
		int find_dir = 0;
		struct ext2_group_desc *gd = (struct ext2_group_desc *)index(disk, 2);
		struct ext2_inode *inode = (struct ext2_inode *)index(disk, gd->bg_inode_table);
		struct ext2_inode *root = &inode[EXT2_ROOT_INO - 1];
		struct ext2_inode *cur_dir = root;

		struct ext2_dir_entry * temp;
		unsigned short rest_reclen = EXT2_BLOCK_SIZE;
		token = strtok(path, "/");
		for(int i = 0; i < (pathlength - 1); i++){
				find_dir = 0;
				for(int j = 0; cur_dir->i_block[j] != 0; j++) {
					temp = (struct ext2_dir_entry*)index(disk, cur_dir->i_block[j]);
					rest_reclen = EXT2_BLOCK_SIZE;
					while(rest_reclen != 0) {
						if(temp->inode != 0){// skip the node if its inode is 0
							//if we just compare the name and token with strncmp, it may return 0 in case
							//such as name = link, token = link2
							if(strncmp(temp->name, token, temp->name_len) == 0 && temp->name_len == strlen(token)) {
								find_dir = 1;
								find_inode = temp->inode - 1;
							}
						}
						rest_reclen = rest_reclen - temp->rec_len;
						if (rest_reclen != 0){
							temp = (struct ext2_dir_entry *)((void*)temp + temp->rec_len);
						}
					}
				}

				// Destination path not found
				if(find_dir == 0) {
					errno = ENOENT;
					perror("Incorrect file path");
					return -ENOENT;
				}
				cur_dir = &inode[find_inode];
				// Check the path is a directory
				if((cur_dir->i_mode & 0xF000) != EXT2_S_IFDIR) {
					errno = ENOTDIR;
					perror("ENOTDIR");
					return -ENOTDIR;
				}
			token = strtok(NULL, "/");
		}
	}
	return find_inode;
}

int search_dir(unsigned char* disk, int dir_i, char* input){
	int result = -1;
	struct ext2_dir_entry * temp;
	struct ext2_group_desc *gd = (struct ext2_group_desc *)index(disk, 2);
	struct ext2_inode *inode = (struct ext2_inode *)index(disk, gd->bg_inode_table);
	struct ext2_inode *cur_dir = &inode[dir_i];
	unsigned short rest_reclen = EXT2_BLOCK_SIZE;
	//if the input length if greater than maximum, it couldn't be in the directory.
	if(strlen(input) > EXT2_NAME_LEN){
		perror("ENAMETOOLONG");
		return -ENAMETOOLONG;
	}
	for(int j = 0; cur_dir->i_block[j] != 0; j++) {
		temp = (struct ext2_dir_entry*)index(disk, cur_dir->i_block[j]);
		rest_reclen = EXT2_BLOCK_SIZE;
		while(rest_reclen != 0) {
			if(temp->inode != 0){// skip the node if its inode is 0
				if(strncmp(temp->name, input, temp->name_len) == 0 && temp->name_len == strlen(input)) {
					result = temp->inode - 1;
				}
			}
			rest_reclen = rest_reclen - temp->rec_len;
			if(rest_reclen != 0){
				temp = (struct ext2_dir_entry *)((void*)temp + temp->rec_len);
			}
		}
	}
	if(result == -1) {
		return -ENOENT;
	}
	return result;
}
