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
    if(argc != 4) {
        fprintf(stderr, "Usage: %s <image file name> <path to file> <absolute path>\n",
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

    // Open source file
    int sfd = open(argv[2], O_RDWR);
    if(sfd == -1){
    	errno = ENOENT;
    	perror("Can not open source file");
    	exit(ENOENT);
    }
    struct stat st;
    if(fstat(sfd, &st) == -1){
    	perror("fstat");
    	exit(errno);
    }
    unsigned char *src = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, sfd, 0);
    if(src == MAP_FAILED) {
    	perror("mmap");
    	exit(errno);
    }

    // Check source file name length
    char *length_check;
    length_check = strrchr(argv[2], '/');
    if (length_check == NULL) {
    	if(strlen(argv[2]) > EXT2_NAME_LEN){
	    	errno = ENAMETOOLONG;
	    	perror("File name too long");
	    	exit(ENAMETOOLONG);
    	}
    }
    else {
    	if(strlen(length_check) > (EXT2_NAME_LEN + 1)){
	    	errno = ENAMETOOLONG;
	    	perror("File name too long");
	    	exit(ENAMETOOLONG);
    	}
    }

    // Get source file name
    char filename[EXT2_NAME_LEN];
	char *token_name;
	token_name = strtok(argv[2], "/");
	while( token_name != NULL ){
		strcpy(filename, token_name);
		token_name = strtok(NULL, "/");
	}
    
    struct ext2_super_block *sb = (struct ext2_super_block *)index(disk, 1);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)index(disk, 2);

	// Get the number of blocks required for the source file
	int block_num = 0;
	if(st.st_size % EXT2_BLOCK_SIZE == 0){
		block_num = st.st_size / EXT2_BLOCK_SIZE;
	}
	else {
		block_num = (st.st_size / EXT2_BLOCK_SIZE) + 1;
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
				freeinodeidx = (8 * i) + j; // inode index starts from 0
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

	// Check the absolute path is a valid path
	if(argv[3][0] != '/') {
		errno = ENOENT;
		perror("Not a valid path");
		exit(ENOENT);
	}

	// Check whether the absolute path ends with '/'
	int endslash = 0;
	if(argv[3][strlen(argv[3])] == '/') {
		endslash = 1;
	}

	// Get the length of absolute path
	char argv3_copy[strlen(argv[3]) + 1];
	strcpy(argv3_copy, argv[3]);
	int pathlength = 0;
	char *token_length;
	token_length = strtok(argv[3], "/");
	while( token_length != NULL ) {
		pathlength++;
		token_length = strtok(NULL, "/");
	}

	// Parse the absolute path
	int find_root = 0;
	int find_path = 0;
	int find_inode = 0;
	int depth = 1;
	int dest_file = 0;
	int file_not_exist = 0;
	char *token;
	char *destpath;
	unsigned short rest_reclen = EXT2_BLOCK_SIZE;
	token = strtok(argv3_copy, "/");

	while( token != NULL ) {

		destpath = token;

		// Check every object(file/directory/symlink) in root directory when the destination path is not under root directory
		if(find_root == 0 && pathlength > 1) {
			for(int i = 0; root->i_block[i] != 0; i++) {
				struct ext2_dir_entry *root_dir_entry = (struct ext2_dir_entry*)index(disk, root->i_block[i]);
				rest_reclen = EXT2_BLOCK_SIZE;
				while(rest_reclen > 0) {
					if(strncmp(root_dir_entry->name, destpath, root_dir_entry->name_len) == 0 && root_dir_entry->name_len == strlen(destpath) && root_dir_entry->inode != 0) { // add inode != 0 later
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
		// When the destination path is an object(file/directory/symlink) in the root directory
		if(find_root == 0 && pathlength == 1){
			for(int i = 0; root->i_block[i] != 0; i++) {
				struct ext2_dir_entry *root_dir_entry = (struct ext2_dir_entry*)index(disk, root->i_block[i]);
				rest_reclen = EXT2_BLOCK_SIZE;
				while(rest_reclen > 0) {
					if(strncmp(root_dir_entry->name, destpath, root_dir_entry->name_len) == 0 && root_dir_entry->name_len == strlen(destpath) && root_dir_entry->inode != 0) {
						find_root = 1;
						find_inode = root_dir_entry->inode - 1;
						if(root_dir_entry->file_type != EXT2_FT_DIR){
							if(root_dir_entry->file_type == EXT2_FT_SYMLINK) {
								errno = EEXIST;
								perror("EEXIST");
								exit(EEXIST);
							}
							dest_file = 1;
							strcpy(filename, destpath);
						}
					}
					rest_reclen = rest_reclen - root_dir_entry->rec_len;
					if (rest_reclen > 0) {
						root_dir_entry = (struct ext2_dir_entry *)((void*)root_dir_entry + root_dir_entry->rec_len);
					}
				}
			}
			// When destination path is a file and the file is not found, we need to create a new file with the name given in the absolute path
			if (find_root == 0 && endslash == 0) {
				if(strlen(destpath) > EXT2_NAME_LEN){
					errno = ENAMETOOLONG;
					perror("File name too long");
					exit(ENAMETOOLONG);
				}
				strcpy(filename, destpath);
				find_inode = EXT2_ROOT_INO - 1;
				dest_file = 1;
				find_root = 1;
				file_not_exist = 1;
			}
		}

		// Destination path is not under root directory and the desired directory in root directory is not found, we return ENOENT
		if(find_root == 0) {
			errno = ENOENT;
			perror("Path not found");
			exit(ENOENT);
		}

		// Continue check the absolute path
		if(find_root == 2) {

			struct ext2_inode *path_inode = &inode[find_inode];

			for(int i = 0; path_inode->i_block[i] != 0; i++) {
				struct ext2_dir_entry *path_dir_entry = (struct ext2_dir_entry*)index(disk, path_inode->i_block[i]);
				rest_reclen = EXT2_BLOCK_SIZE;
				while(rest_reclen > 0) {
					if(strncmp(path_dir_entry->name, destpath, path_dir_entry->name_len) == 0 && path_dir_entry->name_len == strlen(destpath) && path_dir_entry->inode != 0) {
						find_path = 1;
						find_inode = path_dir_entry->inode - 1;
						if (pathlength == depth) {
							if(path_dir_entry->file_type != EXT2_FT_DIR) {
								if(path_dir_entry->file_type == EXT2_FT_SYMLINK) {
									errno = EEXIST;
									perror("EEXIST");
									exit(EEXIST);
								}
								dest_file = 1;
								strcpy(filename, destpath);
							}
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
			// When destination path is a file and the file is not found, we need to create a new file with the name given in the absolute path
			if (pathlength == depth && find_path == 0 && endslash == 0) {
				if(strlen(destpath) > EXT2_NAME_LEN){
					errno = ENAMETOOLONG;
					perror("File name too long");
					exit(ENAMETOOLONG);
				}
				strcpy(filename, destpath);
				dest_file = 1;
				find_path = 1;
				file_not_exist = 1;
			}

			// Destination path not found before we get to the end of the absolute path, we return ENOENT
			if(find_path == 0) {
				errno = ENOENT;
				perror("Path not found");
				exit(ENOENT);
			}

		}

		find_path = 0;
		find_root = 2;
		depth++;

		token = strtok(NULL, "/");
	}

	// When the destination path is root("/")
	if(pathlength == 0){
		find_inode = EXT2_ROOT_INO - 1;
	}

	// Destination path is a file (overwrite the original file)
	if (dest_file && !file_not_exist) { // need to change: inode, data block, data bitmap, gd, sb. no need to change: inode bitmap, dir_entry NOTE: block bitmap cannot use FCFS

		struct ext2_inode *dest_inode = &inode[find_inode];

		int origin_file_block_num = dest_inode->i_blocks / 2;
		if (origin_file_block_num > 12) {
			origin_file_block_num--;
		}

		// Check there are enough blocks for the new file
		if(block_num > origin_file_block_num) {
			if(block_num > 12 && origin_file_block_num <= 12) {
				if((block_num - origin_file_block_num) > (gd->bg_free_blocks_count - 1)){
					errno = ENOSPC;
					perror("No enough blocks");
					exit(ENOSPC);
				}
			}
			else {
				if((block_num - origin_file_block_num) > gd->bg_free_blocks_count){
					errno = ENOSPC;
					perror("No enough blocks");
					exit(ENOSPC);
				}
			}
		}

		void *new_file_block_data = (void*)src;

		if (block_num > 12) {
			if (origin_file_block_num > 12) {

				// Overwrite the first 12 blocks
				for (int i = 0; i < 12; i++) {
					int origin_file_block = dest_inode->i_block[i];
					void *origin_file_block_data = (void*)index(disk, origin_file_block);
					memset(origin_file_block_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
					memcpy(origin_file_block_data, new_file_block_data, EXT2_BLOCK_SIZE);
					new_file_block_data = (void*)((void*)new_file_block_data + EXT2_BLOCK_SIZE); // maybe need to restrict the last move
				}

				// Free the indirect pointer of the original file
				int *indirection_pointer = (int*)index(disk, dest_inode->i_block[12]);
				int indirection_idx = 0;
				for (int i = 12; i < origin_file_block_num; i++) {
					int free_block = indirection_pointer[indirection_idx];
					indirection_pointer[indirection_idx] = 0;
					indirection_idx++;
					finishedblock = 0;
					for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
						for(int l = 0; (l < 8) && !finishedblock; l++){
							int bit = (k * 8) + l + 1;
							if(free_block == bit) {
								*(block_bm + k) = *(block_bm + k) & (~(1 << l)); // Set this bit free
								finishedblock = 1;
							}
						}
					}
				}

				// Clear the memory block before set the new indrect pointer
				void *clear_mem = (void*)index(disk, dest_inode->i_block[12]);
				memset(clear_mem, 0, EXT2_BLOCK_SIZE);

				// Allocate the remain blocks of the new file in the indirect pointer
				indirection_idx = 0;
				for (int i = 12; i < block_num; i++) {
					finishedblock = 0;
					for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
						for(int l = 0; (l < 8) && !finishedblock; l++){
							int bit = *(block_bm + k) & (1 << l);
							if (!bit){
								*(block_bm + k) = *(block_bm + k) | (1 << l); // set this bit not free
								freeblockidx = (8 * k) + l + 1; // block index starts from 1
								finishedblock = 1;
							}
						}
					}
					indirection_pointer[indirection_idx] = freeblockidx;
					indirection_idx++;
					void *extra_file_block_data = (void*)index(disk, freeblockidx);
					memset(extra_file_block_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
					memcpy(extra_file_block_data, new_file_block_data, EXT2_BLOCK_SIZE);
					if(i < block_num - 1){
						new_file_block_data = (void*)((void*)new_file_block_data + EXT2_BLOCK_SIZE); // maybe need to restrict the last move
					}
				}

				// Update inode
				dest_inode->i_size = (unsigned int)st.st_size;
				dest_inode->i_blocks = (block_num + 1) * 2;
				// Update group descriptor
				gd->bg_free_blocks_count = gd->bg_free_blocks_count + (origin_file_block_num - block_num);
				// Update super block
				sb->s_free_blocks_count = sb->s_free_blocks_count + (origin_file_block_num - block_num);
				
			}
			else { // origin_file_block_num <= 12

				// Overwrite the blocks of the original file
				for (int i = 0; i < origin_file_block_num; i++) {
					int origin_file_block = dest_inode->i_block[i];
					void *origin_file_block_data = (void*)index(disk, origin_file_block);
					memset(origin_file_block_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
					memcpy(origin_file_block_data, new_file_block_data, EXT2_BLOCK_SIZE);
					new_file_block_data = (void*)((void*)new_file_block_data + EXT2_BLOCK_SIZE); // maybe need to restrict the last move
				}

				// Allocate remaining blocks in direct blocks
				for (int i = origin_file_block_num; i < 12; i++) {
					finishedblock = 0;
					for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
						for(int l = 0; (l < 8) && !finishedblock; l++){
							int bit = *(block_bm + k) & (1 << l);
							if (!bit){
								*(block_bm + k) = *(block_bm + k) | (1 << l); // set this bit not free
								freeblockidx = (8 * k) + l + 1; // block index starts from 1
								finishedblock = 1;
							}
						}
					}
					dest_inode->i_block[i] = freeblockidx;
					void *extra_file_block_data = (void*)index(disk, freeblockidx);
					memset(extra_file_block_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
					memcpy(extra_file_block_data, new_file_block_data, EXT2_BLOCK_SIZE);
					new_file_block_data = (void*)((void*)new_file_block_data + EXT2_BLOCK_SIZE);
				}

				// Set the indirect pointer
				finishedblock = 0;
				for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
					for(int l = 0; (l < 8) && !finishedblock; l++){
						int bit = *(block_bm + k) & (1 << l);
						if (!bit){
							*(block_bm + k) = *(block_bm + k) | (1 << l); // set this bit not free
							freeblockidx = (8 * k) + l + 1; // block index starts from 1
							finishedblock = 1;
						}
					}
				}
				dest_inode->i_block[12] = freeblockidx;

				// Clear the memory block before set the new indrect pointer
				void *clear_mem = (void*)index(disk, dest_inode->i_block[12]);
				memset(clear_mem, 0, EXT2_BLOCK_SIZE);

				// Allocate the remain blocks of the new file in the indirect pointer
				int *indirection_pointer = (int*)index(disk, dest_inode->i_block[12]);
				int indirection_idx = 0;
				for (int i = 12; i < block_num; i++) {
					finishedblock = 0;
					for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
						for(int l = 0; (l < 8) && !finishedblock; l++){
							int bit = *(block_bm + k) & (1 << l);
							if (!bit){
								*(block_bm + k) = *(block_bm + k) | (1 << l); // set this bit not free
								freeblockidx = (8 * k) + l + 1; // block index starts from 1
								finishedblock = 1;
							}
						}
					}

					indirection_pointer[indirection_idx] = freeblockidx;
					indirection_idx++;
					void *extra_file_block_data = (void*)index(disk, freeblockidx);
					memset(extra_file_block_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
					memcpy(extra_file_block_data, new_file_block_data, EXT2_BLOCK_SIZE);
					if(i < block_num - 1){
						new_file_block_data = (void*)((void*)new_file_block_data + EXT2_BLOCK_SIZE); // maybe need to restrict the last move
					}
				}

				// Update inode
				dest_inode->i_size = (unsigned int)st.st_size;
				dest_inode->i_blocks = (block_num + 1) * 2;
				// Update group descriptor
				gd->bg_free_blocks_count = gd->bg_free_blocks_count + (origin_file_block_num - block_num - 1);
				// Update super block
				sb->s_free_blocks_count = sb->s_free_blocks_count + (origin_file_block_num - block_num - 1);

			}
		}
		else { // block_num <= 12
			if (origin_file_block_num > 12) {

				// Overwrite the original file in direct blocks
				for (int i = 0; i < block_num; i++) {
					int origin_file_block = dest_inode->i_block[i];
					void *origin_file_block_data = (void*)index(disk, origin_file_block);
					memset(origin_file_block_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
					memcpy(origin_file_block_data, new_file_block_data, EXT2_BLOCK_SIZE);
					if(i < block_num - 1){
						new_file_block_data = (void*)((void*)new_file_block_data + EXT2_BLOCK_SIZE); // maybe need to restrict the last move
					}
				}

				// Free the original file in direct blocks
				for (int i = block_num; i < 12; i++) {
					int free_block = dest_inode->i_block[i];
					dest_inode->i_block[i] = 0;
					finishedblock = 0;
					for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
						for(int l = 0; (l < 8) && !finishedblock; l++){
							int bit = (k * 8) + l + 1;
							if(free_block == bit) {
								*(block_bm + k) = *(block_bm + k) & (~(1 << l)); // Set this bit free
								finishedblock = 1;
							}
						}
					}
				}

				// Free the original file in indirect blocks
				int indirection_block = dest_inode->i_block[12];
				dest_inode->i_block[12] = 0;
				finishedblock = 0;
				for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
					for(int l = 0; (l < 8) && !finishedblock; l++){
						int bit = (k * 8) + l + 1;
						if(indirection_block == bit) {
							*(block_bm + k) = *(block_bm + k) & (~(1 << l)); // Set this bit free
							finishedblock = 1;
						}
					}
				}

				int *indirection_pointer = (int*)index(disk, indirection_block);
				int indirection_idx = 0;
				for (int i = 12; i < origin_file_block_num; i++) {
					int free_block = indirection_pointer[indirection_idx];
					indirection_idx++;
					finishedblock = 0;
					for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
						for(int l = 0; (l < 8) && !finishedblock; l++){
							int bit = (k * 8) + l + 1;
							if(free_block == bit) {
								*(block_bm + k) = *(block_bm + k) & (~(1 << l)); // Set this bit free
								finishedblock = 1;
							}
						}
					}
				}

				// Update inode
				dest_inode->i_size = (unsigned int)st.st_size;
				dest_inode->i_blocks = block_num * 2;
				// Update group descriptor
				gd->bg_free_blocks_count = gd->bg_free_blocks_count + (origin_file_block_num + 1 - block_num);
				// Update super block
				sb->s_free_blocks_count = sb->s_free_blocks_count + (origin_file_block_num + 1 - block_num);
				
			}
			else { // origin_file_block_num <= 12
				// When the new file is smaller than the original file, need to free some blocks in the block bitmap
				if(block_num < origin_file_block_num){

					// Overwrite data blocks
					for (int i = 0; i < block_num; i++) {
						int origin_file_block = dest_inode->i_block[i];
						void *origin_file_block_data = (void*)index(disk, origin_file_block);
						memset(origin_file_block_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
						memcpy(origin_file_block_data, new_file_block_data, EXT2_BLOCK_SIZE);
						if(i < block_num - 1){
							new_file_block_data = (void*)((void*)new_file_block_data + EXT2_BLOCK_SIZE); // maybe need to restrict the last move
						}
					}

					// Update block bitmap & dest_inode->i_block[], free the remaining blocks in the original file
					for (int i = block_num; i < origin_file_block_num; i++) {
						int free_block = dest_inode->i_block[i];
						dest_inode->i_block[i] = 0;
						finishedblock = 0;
						for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
							for(int l = 0; (l < 8) && !finishedblock; l++){
								int bit = (k * 8) + l + 1;
								if(free_block == bit) {
									*(block_bm + k) = *(block_bm + k) & (~(1 << l)); // Set this bit free
									finishedblock = 1;
								}
							}
						}
					}
				}
				// When the new file is larger than the original file, need to allocate some new data blocks
				else { // block_num => origin_file_block_num

					// Update data block & block bitmap & dest_inode->i_block[], overwrite the original file
					for (int i = 0; i < origin_file_block_num; i++) {
						int origin_file_block = dest_inode->i_block[i];
						void *origin_file_block_data = (void*)index(disk, origin_file_block);
						memset(origin_file_block_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
						memcpy(origin_file_block_data, new_file_block_data, EXT2_BLOCK_SIZE);
						new_file_block_data = (void*)((void*)new_file_block_data + EXT2_BLOCK_SIZE); // maybe need to restrict the last move
					}

					// Allocate extra data blocks for the new file
					for (int i = origin_file_block_num; i < block_num; i++) {
						finishedblock = 0;
						for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
							for(int l = 0; (l < 8) && !finishedblock; l++){
								int bit = *(block_bm + k) & (1 << l);
								if (!bit){
									*(block_bm + k) = *(block_bm + k) | (1 << l); // set this bit not free
									freeblockidx = (8 * k) + l + 1; // block index starts from 1
									finishedblock = 1;
								}
							}
						}
						dest_inode->i_block[i] = freeblockidx;
						void *extra_file_block_data = (void*)index(disk, freeblockidx);
						memset(extra_file_block_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
						memcpy(extra_file_block_data, new_file_block_data, EXT2_BLOCK_SIZE);
						if(i < block_num - 1){
							new_file_block_data = (void*)((void*)new_file_block_data + EXT2_BLOCK_SIZE);
						}
					}
				}

				// Update inode
				dest_inode->i_size = (unsigned int)st.st_size;
				dest_inode->i_blocks = block_num * 2;
				// Update group descriptor
				gd->bg_free_blocks_count = gd->bg_free_blocks_count + (origin_file_block_num - block_num);
				// Update super block
				sb->s_free_blocks_count = sb->s_free_blocks_count + (origin_file_block_num - block_num);
			}
		}
	}
	// Destination path is a directory(create a file in the given path) or Destination path is a file but the file doesn't exist(create a file with the given name(in the path) in the given path(in the parent directory of the given file))
	else {

		// Check there is remaining inode
		if (gd->bg_free_inodes_count == 0){
	    	errno = ENOSPC;
	    	perror("No enough inode");
	    	exit(ENOSPC);
	    }

		struct ext2_inode *dest_inode = &inode[find_inode];

		// Check no object(file or directory or symbolic link) with the same name exist in the destination path
		for(int i = 0; dest_inode->i_block[i] != 0; i++) {

			struct ext2_dir_entry *dest_dir_entry = (struct ext2_dir_entry*)index(disk, dest_inode->i_block[i]);
			rest_reclen = EXT2_BLOCK_SIZE;
			while(rest_reclen > 0) {
				if(strncmp(dest_dir_entry->name, filename, dest_dir_entry->name_len) == 0 && dest_dir_entry->name_len == strlen(filename) && dest_dir_entry->inode != 0) { // A object(file or directory or symbolic link) with the same name already exist in the destination path, return EEXIST (if it is a file, should overwrite it, but EEXIST is accepted now(on piazza))
					errno = EEXIST;
					perror("EEXIST");
					exit(EEXIST);
				}
				rest_reclen = rest_reclen - dest_dir_entry->rec_len;
				if (rest_reclen > 0) {
					dest_dir_entry = (struct ext2_dir_entry *)((void*)dest_dir_entry + dest_dir_entry->rec_len);
				}
			}
		}

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

			int new_reclen = strlen(filename) + sizeof(struct ext2_dir_entry);
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

			// Check there are enough blocks for the file and one more directory entry
			if (block_num > 12) {
				if(block_num > (gd->bg_free_blocks_count - 2)){
					errno = ENOSPC;
					perror("No enough blocks");
					exit(ENOSPC);
				}
			}
			else {
				if(block_num > (gd->bg_free_blocks_count - 1)){
					errno = ENOSPC;
					perror("No enough blocks");
					exit(ENOSPC);
				}
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
			strncpy(dest_dir_entry_update->name, filename, EXT2_NAME_LEN);
			dest_dir_entry_update->name_len = strlen(filename);
			dest_dir_entry_update->rec_len = EXT2_BLOCK_SIZE;
			dest_dir_entry_update->file_type = EXT2_FT_REG_FILE;

			// Update super block and block descriptor
			gd->bg_free_blocks_count--; // one block for new directory entry
			sb->s_free_blocks_count--; // one block for new directory entry

		}
		else {

			// Check there are enough blocks for the file
			if (block_num > 12) {
				if(block_num > (gd->bg_free_blocks_count - 1)){
					errno = ENOSPC;
					perror("No enough blocks");
					exit(ENOSPC);
				}
			}
			else {
				if(block_num > gd->bg_free_blocks_count){
					errno = ENOSPC;
					perror("No enough blocks");
					exit(ENOSPC);
				}
			}

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
			strncpy(dest_dir_entry_update->name, filename, EXT2_NAME_LEN);
			dest_dir_entry_update->name_len = strlen(filename);
			dest_dir_entry_update->file_type = EXT2_FT_REG_FILE;
		}

		// Update inode except i_block[]
		struct ext2_inode *copy_file_inode = &inode[freeinodeidx];
		copy_file_inode->i_mode = (unsigned short)st.st_mode;
		copy_file_inode->i_uid = 0;
		copy_file_inode->i_size = st.st_size;
		copy_file_inode->i_gid = 0;
		copy_file_inode->i_links_count = 1;
		copy_file_inode->osd1 = 0;
		copy_file_inode->i_generation = 0;
		copy_file_inode->i_faddr = 0;
		copy_file_inode->i_file_acl = 0;
		copy_file_inode->i_dir_acl = 0;
		copy_file_inode->i_dtime = 0;
		if(block_num > 12){
			copy_file_inode->i_blocks = 2 * (block_num + 1);
		}
		else{
			copy_file_inode->i_blocks = 2 * block_num;
		}

		// Update data block & block bitmap & i_block[] in inode
		if (block_num > 12) { // When there is one level indirection
			// Set the first 12 blocks
			int block_count = 12;
			void *source_data = (void*)src;
			for(int k = 0; (k < (sb->s_blocks_count / 8)) && block_count > 0; k++) {
				for(int l = 0; (l < 8) && block_count > 0; l++){
					int bit = *(block_bm + k) & (1 << l);
					if (!bit){
						freeblockidx = (8 * k) + l + 1; // block index starts from 1
						*(block_bm + k) = *(block_bm + k) | (1 << l);
						void *copy_file_data = (void*)index(disk, freeblockidx);
						memset(copy_file_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
						memcpy(copy_file_data, source_data, EXT2_BLOCK_SIZE);
						source_data = (void*)((void*)source_data + EXT2_BLOCK_SIZE);
						copy_file_inode->i_block[12 - block_count] = freeblockidx;
						block_count--;
					}
				}
			}
			// Set the indirection pointer
			block_count = 1;
			for(int k = 0; (k < (sb->s_blocks_count / 8)) && block_count > 0; k++) {
				for(int l = 0; (l < 8) && block_count > 0; l++){
					int bit = *(block_bm + k) & (1 << l);
					if (!bit){
						freeblockidx = (8 * k) + l + 1; // block index starts from 1
						*(block_bm + k) = *(block_bm + k) | (1 << l);
						copy_file_inode->i_block[12] = freeblockidx;
						block_count--;
					}
				}
			}

			// Clear the memory block before set the new indrect pointer
			void *clear_mem = (void*)index(disk, freeblockidx);
			memset(clear_mem, 0, EXT2_BLOCK_SIZE);

			int *indirection_pointer = (int*)index(disk, freeblockidx);
			// Set the remaining blocks
			block_count = block_num - 12;
			for(int k = 0; (k < (sb->s_blocks_count / 8)) && block_count > 0; k++) {
				for(int l = 0; (l < 8) && block_count > 0; l++){
					int bit = *(block_bm + k) & (1 << l);
					if (!bit){
						freeblockidx = (8 * k) + l + 1; // block index starts from 1
						*(block_bm + k) = *(block_bm + k) | (1 << l);
						void *copy_file_data = (void*)index(disk, freeblockidx);
						memset(copy_file_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
						memcpy(copy_file_data, source_data, EXT2_BLOCK_SIZE);
						if(block_count > 1){
							source_data = (void*)((void*)source_data + EXT2_BLOCK_SIZE);
						}
						indirection_pointer[(block_num - 12) - block_count] = freeblockidx;
						block_count--;
					}
				}
			}
			copy_file_inode->i_block[13] = 0;
			copy_file_inode->i_block[14] = 0;
		}
		else { // When no indirection
			int block_count = block_num;
			void *source_data = (void*)src;
			for(int k = 0; (k < (sb->s_blocks_count / 8)) && block_count > 0; k++) {
				for(int l = 0; (l < 8) && block_count > 0; l++){
					int bit = *(block_bm + k) & (1 << l);
					if (!bit){
						freeblockidx = (8 * k) + l + 1; // block index starts from 1
						*(block_bm + k) = *(block_bm + k) | (1 << l);
						void *copy_file_data = (void*)index(disk, freeblockidx);
						memset(copy_file_data, 0, EXT2_BLOCK_SIZE); // Clear the memory blocks before copy
						memcpy(copy_file_data, source_data, EXT2_BLOCK_SIZE);
						if(block_count > 1){
							source_data = (void*)((void*)source_data + EXT2_BLOCK_SIZE);
						}
						copy_file_inode->i_block[block_num - block_count] = freeblockidx;
						block_count--;
					}
				}
			}
			for (int j = block_num; j < 15; j++) {
				copy_file_inode->i_block[j] = 0; // Set remaining i_block[] to 0
			}
		}

		// Update group descriptor
		gd->bg_free_inodes_count--;
		gd->bg_free_blocks_count = gd->bg_free_blocks_count - block_num;
		if(block_num > 12){
			gd->bg_free_blocks_count--; // one block for indirection pointer
		}

		// Update super block
		sb->s_free_inodes_count--;
		sb->s_free_blocks_count = sb->s_free_blocks_count - block_num;
		if(block_num > 12){
			sb->s_free_blocks_count--; // one block for indirection pointer
		}

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

	}

	int close_error;	
	close_error = close(fd);
	if (close_error != 0) {
		perror("close fd");
		exit(errno);
	}
	close_error = close(sfd);
	if (close_error != 0) {
		perror("close sfd");
		exit(errno);
	}

	return 0;
}
