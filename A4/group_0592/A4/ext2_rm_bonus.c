#include "ext2.h"
#include "functions.h"

int main(int argc, char **argv){
	if(argc != 3 && argc != 4) {
		fprintf(stderr, "Usage: %s <image file name> <flag -s> <absolute path>\n",
				argv[0]);
		exit(1);
	}

	int fp = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fp, 0);
	if(disk == MAP_FAILED) {
		perror("mmap");
		exit(errno);
	}

	int rm_dir, opt;
	rm_dir = 0;
	while ((opt = getopt(argc, argv, "r")) != -1){
		switch(opt){
		case 'r':
			rm_dir = 1;
			break;
		default:
			fprintf(stderr, "Usage: %s <image file name> <flag -s> <absolute path>\n",
							argv[0]);
			exit(1);
		}
	}

	struct ext2_super_block *sb = (struct ext2_super_block *)index(disk, 1);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)index(disk, 2);
	struct ext2_inode *inode = (struct ext2_inode *)index(disk, gd->bg_inode_table);
	unsigned char *block_bm = index(disk, gd->bg_block_bitmap);
	unsigned char *inode_bm = index(disk, gd->bg_inode_bitmap);

	int target_dir_i;
	char* target_name;
	if(rm_dir == 0) {
		target_dir_i = pathfinder(disk, argv[2]);
		if(target_dir_i < 0){ //path is wrong if pathfinder return negative
			exit(-target_dir_i);
		}

		target_name = strrchr(argv[2], '/') + 1;
		if(!strncmp(target_name, ".", 1) || !strncmp(target_name, "..", 2)){
			fprintf(stderr, "Cannot remove directory: %s\n", target_name);
			fprintf(stderr, "Cannot remove directory: %s\n", target_name);
			exit(1);
		}
	}
	else {
		target_dir_i = pathfinder(disk, argv[3]);
		if(target_dir_i < 0){ //path is wrong if pathfinder return negative
			exit(-target_dir_i);
		}

		target_name = strrchr(argv[3], '/') + 1;
		if(!strncmp(target_name, ".", 1) || !strncmp(target_name, "..", 2)){
			fprintf(stderr, "Cannot remove directory: %s\n", target_name);
			fprintf(stderr, "Cannot remove directory: %s\n", target_name);
			exit(1);
		}
	}

	int target_i = search_dir(disk, target_dir_i, target_name);
	if (target_i < 0){ //if search_dir return -1, it means it can't find the file in the directory
		if(target_i == -ENOENT){
			errno = ENOENT;
			perror("Incorrect filename");
		}
		exit(-target_i);
	}

	if(rm_dir == 0) {
		struct ext2_inode *target = &inode[target_i];
		if((target->i_mode & 0xF000) == EXT2_S_IFDIR) {
			errno = EISDIR;
			perror("EISDIR");
			exit(EISDIR);
		}

		int deallocate = -1;
		int finished;
		unsigned short rest_reclen;
		struct ext2_dir_entry* prev;
		struct ext2_dir_entry* temp;
		struct ext2_inode *cur_dir = &inode[target_dir_i];
		for(int j = 0; cur_dir->i_block[j] != 0; j++) {
			temp = (struct ext2_dir_entry*)index(disk, cur_dir->i_block[j]);
			//check the first directory entry, we have checked target is not "." or ".."
			if(temp->inode == (target_i + 1)) {
				temp->inode = 0;
				//flag that we need to deallocate the block
				if (temp->rec_len == EXT2_BLOCK_SIZE){
					deallocate = j;
				}
				break;
			}else{ // if the first entry fulfill, no need to continue checking
				prev = temp;
				temp = (struct ext2_dir_entry *)((void*)temp + temp->rec_len);
				rest_reclen = EXT2_BLOCK_SIZE;
				finished = 0;
				while(rest_reclen != 0 && !finished) {
					if(temp->inode != 0){// skip the node if its inode is 0
						if(temp->inode == (target_i + 1)) {
							prev->rec_len += temp->rec_len;
							finished = 1;
						}
					}
					rest_reclen = rest_reclen - temp->rec_len;
					prev = temp;
					temp = (struct ext2_dir_entry *)((void*)temp + temp->rec_len);
				}
				if(finished){
					break;
				}
			}
		}

		int block_num;
		if (deallocate != -1){
			//change data block bitmap
			block_num = cur_dir->i_block[deallocate];
			int i = (block_num - 1) / 8;
			int j = (block_num - 1) - 8 * i;
			*(block_bm + i) = *(block_bm + i) & (~(1 << j));
			sb->s_free_blocks_count++;
			gd->bg_free_blocks_count++;

			//move the value after deallocated block one position forward to fill the gap
			for(int i = deallocate; i < 11; i++){
				cur_dir->i_block[i] = cur_dir->i_block[i+1];
			}
			cur_dir->i_block[11] = 0;
			cur_dir->i_blocks -= 2;
			cur_dir->i_size -= EXT2_BLOCK_SIZE;
		}

		target->i_links_count--;
		if (target->i_links_count == 0){
			//in piazza, it says the actual value for i_dtime doesn't really matter
			target->i_dtime = 1;
			//free the blocks
			for(int k = 0; k < 12; k++){
				block_num = target->i_block[k];
				if (block_num != 0){
					int i = (block_num - 1) / 8;
					int j = (block_num - 1) - 8 * i;
					*(block_bm + i) = *(block_bm + i) & (~(1 << j));
					sb->s_free_blocks_count++;
					gd->bg_free_blocks_count++;
				}
			}
			//if we had use indirect pointer, free blocks in indirection
			if(target->i_block[12] != 0){
				int *indirection_pointer = (int*)index(disk, target->i_block[12]);
				int k = 0;
				block_num = indirection_pointer[0];
				while(block_num != 0 && k < 128){
					int i = (block_num - 1) / 8;
					int j = (block_num - 1) - 8 * i;
					*(block_bm + i) = *(block_bm + i) & (~(1 << j));
					sb->s_free_blocks_count++;
					gd->bg_free_blocks_count++;
					k++;
					block_num = indirection_pointer[k];
				}
				//free the indirection block itself
				block_num = target->i_block[12];
				int i = (block_num - 1) / 8;
				int j = (block_num - 1) - 8 * i;
				*(block_bm + i) = *(block_bm + i) & (~(1 << j));
				sb->s_free_blocks_count++;
				gd->bg_free_blocks_count++;
			}
			//free the inode
			int i = target_i / 8;
			int j = target_i - 8 * i;
			*(inode_bm + i) = *(inode_bm + i) & (~(1 << j));
			sb->s_free_inodes_count++;
			gd->bg_free_inodes_count++;
		}
	}
	else {
		int dir_inodes[sb->s_blocks_count];
		for(int k = 0; k < sb->s_blocks_count; k++) {
			dir_inodes[k] = 0;
		}
		int dir_inodes_idx = 0;

		struct ext2_inode *target = &inode[target_i];

		int deallocate = -1;
		int finished;
		unsigned short rest_reclen;
		struct ext2_dir_entry* prev;
		struct ext2_dir_entry* temp;
		struct ext2_inode *cur_dir = &inode[target_dir_i];
		if((target->i_mode & 0xF000) == EXT2_S_IFDIR) {
			dir_inodes[dir_inodes_idx] = target_i;
			dir_inodes_idx++;
			cur_dir->i_links_count--;
		}
		for(int j = 0; cur_dir->i_block[j] != 0; j++) {
			temp = (struct ext2_dir_entry*)index(disk, cur_dir->i_block[j]);
			//check the first directory entry, we have checked target is not "." or ".."
			if(temp->inode == (target_i + 1)) {
				temp->inode = 0;
				//flag that we need to deallocate the block
				if (temp->rec_len == EXT2_BLOCK_SIZE){
					deallocate = j;
				}
				break;
			}else{ // if the first entry fulfill, no need to continue checking
				prev = temp;
				temp = (struct ext2_dir_entry *)((void*)temp + temp->rec_len);
				rest_reclen = EXT2_BLOCK_SIZE;
				finished = 0;
				while(rest_reclen != 0 && !finished) {
					if(temp->inode != 0){// skip the node if its inode is 0
						if(temp->inode == (target_i + 1)) {
							prev->rec_len += temp->rec_len;
							finished = 1;
						}
					}
					rest_reclen = rest_reclen - temp->rec_len;
					prev = temp;
					temp = (struct ext2_dir_entry *)((void*)temp + temp->rec_len);
				}
				if(finished){
					break;
				}
			}
		}

		int block_num;
		if (deallocate != -1){
			//change data block bitmap
			block_num = cur_dir->i_block[deallocate];
			int i = (block_num - 1) / 8;
			int j = (block_num - 1) - 8 * i;
			*(block_bm + i) = *(block_bm + i) & (~(1 << j));
			sb->s_free_blocks_count++;
			gd->bg_free_blocks_count++;

			//move the value after deallocated block one position forward to fill the gap
			for(int i = deallocate; i < 11; i++){
				cur_dir->i_block[i] = cur_dir->i_block[i+1];
			}
			cur_dir->i_block[11] = 0;
			cur_dir->i_blocks -= 2;
			cur_dir->i_size -= EXT2_BLOCK_SIZE;
		}

		target->i_links_count--;
		if (target->i_links_count == 0 || (target->i_mode & 0xF000) == EXT2_S_IFDIR){
			//in piazza, it says the actual value for i_dtime doesn't really matter
			target->i_dtime = 1;
			//free the blocks
			for(int k = 0; k < 12; k++){
				block_num = target->i_block[k];
				if (block_num != 0){
					int i = (block_num - 1) / 8;
					int j = (block_num - 1) - 8 * i;
					*(block_bm + i) = *(block_bm + i) & (~(1 << j));
					sb->s_free_blocks_count++;
					gd->bg_free_blocks_count++;
				}
			}
			//if we had use indirect pointer, free blocks in indirection
			if(target->i_block[12] != 0){
				int *indirection_pointer = (int*)index(disk, target->i_block[12]);
				int k = 0;
				block_num = indirection_pointer[0];
				while(block_num != 0 && k < 128){
					int i = (block_num - 1) / 8;
					int j = (block_num - 1) - 8 * i;
					*(block_bm + i) = *(block_bm + i) & (~(1 << j));
					sb->s_free_blocks_count++;
					gd->bg_free_blocks_count++;
					k++;
					block_num = indirection_pointer[k];
				}
				//free the indirection block itself
				block_num = target->i_block[12];
				int i = (block_num - 1) / 8;
				int j = (block_num - 1) - 8 * i;
				*(block_bm + i) = *(block_bm + i) & (~(1 << j));
				sb->s_free_blocks_count++;
				gd->bg_free_blocks_count++;
			}
			//free the inode
			int i = target_i / 8;
			int j = target_i - 8 * i;
			*(inode_bm + i) = *(inode_bm + i) & (~(1 << j));
			sb->s_free_inodes_count++;
			gd->bg_free_inodes_count++;
		}

		for(int z = 0; dir_inodes[z] != 0 && z < sb->s_blocks_count; z++) {
			cur_dir = &inode[dir_inodes[z]];
			for(int x = 0; cur_dir->i_block[x] != 0; x++) {
				struct ext2_dir_entry *cur_dir_entry = (struct ext2_dir_entry*)index(disk, cur_dir->i_block[x]);
				rest_reclen = EXT2_BLOCK_SIZE;
				while(rest_reclen > 0) {
					if(strcmp(cur_dir_entry->name, ".") != 0 && strcmp(cur_dir_entry->name, "..") != 0) {
						if(cur_dir_entry->file_type == EXT2_FT_DIR) {
							dir_inodes[dir_inodes_idx] = (cur_dir_entry->inode - 1);
							dir_inodes_idx++;
						}
						target_i = cur_dir_entry->inode - 1;
						target = &inode[cur_dir_entry->inode - 1];
						target->i_links_count--;
						if (target->i_links_count == 0 || cur_dir_entry->file_type == EXT2_FT_DIR){
							//in piazza, it says the actual value for i_dtime doesn't really matter
							target->i_dtime = 1;
							//free the blocks
							for(int k = 0; k < 12; k++){
								block_num = target->i_block[k];
								if (block_num != 0){
									int i = (block_num - 1) / 8;
									int j = (block_num - 1) - 8 * i;
									*(block_bm + i) = *(block_bm + i) & (~(1 << j));
									sb->s_free_blocks_count++;
									gd->bg_free_blocks_count++;
								}
							}
							//if we had use indirect pointer, free blocks in indirection
							if(target->i_block[12] != 0){
								int *indirection_pointer = (int*)index(disk, target->i_block[12]);
								int k = 0;
								block_num = indirection_pointer[0];
								while(block_num != 0 && k < 128){
									int i = (block_num - 1) / 8;
									int j = (block_num - 1) - 8 * i;
									*(block_bm + i) = *(block_bm + i) & (~(1 << j));
									sb->s_free_blocks_count++;
									gd->bg_free_blocks_count++;
									k++;
									block_num = indirection_pointer[k];
								}
								//free the indirection block itself
								block_num = target->i_block[12];
								int i = (block_num - 1) / 8;
								int j = (block_num - 1) - 8 * i;
								*(block_bm + i) = *(block_bm + i) & (~(1 << j));
								sb->s_free_blocks_count++;
								gd->bg_free_blocks_count++;
							}
							//free the inode
							int i = target_i / 8;
							int j = target_i - 8 * i;
							*(inode_bm + i) = *(inode_bm + i) & (~(1 << j));
							sb->s_free_inodes_count++;
							gd->bg_free_inodes_count++;
						}
					}
					rest_reclen = rest_reclen - cur_dir_entry->rec_len;
					if (rest_reclen > 0) {
						cur_dir_entry = (struct ext2_dir_entry *)((void*)cur_dir_entry + cur_dir_entry->rec_len);
					}
				}
			}
		}
	}

	int close_error;
	close_error = close(fp);
	if (close_error != 0) {
		perror("close fp");
		exit(errno);
	}

	return 0;
}
