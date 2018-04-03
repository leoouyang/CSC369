#include "ext2.h"
#include "functions.h"

int main(int argc, char **argv){
	if(argc != 3) {
		fprintf(stderr, "Usage: %s <image file name> <absolute path>\n",
				argv[0]);
		exit(1);
	}

	int fp = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fp, 0);
	if(disk == MAP_FAILED) {
		perror("mmap");
		exit(errno);
	}

	struct ext2_super_block *sb = (struct ext2_super_block *)index(disk, 1);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)index(disk, 2);
	struct ext2_inode *inode = (struct ext2_inode *)index(disk, gd->bg_inode_table);
	unsigned char *block_bm = index(disk, gd->bg_block_bitmap);
	unsigned char *inode_bm = index(disk, gd->bg_inode_bitmap);

	int target_dir_i = pathfinder(disk, argv[2]);
	if(target_dir_i < 0){ //path is wrong if pathfinder return negative
		exit(-target_dir_i);
	}
	//error check for the file
	char* target_name = strrchr(argv[2], '/') + 1;
	int target_i = search_dir(disk, target_dir_i, target_name);
	if(target_i > 0){
		struct ext2_inode *target = &inode[target_i];
		if((target->i_mode & 0xF000) == EXT2_S_IFDIR) {
			errno = EISDIR;
			perror("EISDIR");
			exit(EISDIR);
		}
		errno = EEXIST;
		perror("EEXIST");
		exit(EEXIST);
	}

	struct ext2_inode *cur_dir = &inode[target_dir_i];
	struct ext2_dir_entry* cur_entry;
	struct ext2_dir_entry* cur_sub_entry;
	int finished, noent = 0;
	unsigned short rest_reclen, rest_sub_reclen, cur_reclen, cur_sub_reclen;
	//we will make change to the duplicated bitmaps when checking
	//if we find out we could restore, overwrite the original with temp
	unsigned char temp_blocks[sb->s_blocks_count/8];
	unsigned char temp_inodes[sb->s_inodes_count/8];
	int added_block = 0;
	memcpy(temp_blocks, block_bm, sb->s_blocks_count/8);
	memcpy(temp_inodes, inode_bm, sb->s_inodes_count/8);
	for(int i = 0; cur_dir->i_block[i] != 0; i++) {
		cur_entry = (struct ext2_dir_entry*)index(disk, cur_dir->i_block[i]);
		rest_reclen = EXT2_BLOCK_SIZE;
		finished = 0;
		//loop the actual directory entries in FS
		while(rest_reclen != 0 && !finished) {
			//check whether cur entry contains a deleted entry
			cur_reclen = cur_entry->name_len + sizeof(struct ext2_dir_entry);
			if((cur_reclen % 4) != 0) {
				cur_reclen = ((cur_reclen / 4) + 1) * 4;
			}
			if(cur_entry->rec_len != cur_reclen){
				//loop the deleted directory entries contained in current directory entry
				rest_sub_reclen = cur_entry->rec_len;
				cur_sub_entry = cur_entry;
				while(rest_sub_reclen != 0 && !finished && strlen(cur_sub_entry->name)!=0 && rest_sub_reclen <= EXT2_BLOCK_SIZE){
					//check if the file is what we want to restore
					if(strncmp(cur_sub_entry->name, target_name, cur_sub_entry->name_len) == 0 &&
							cur_sub_entry->name_len == strlen(target_name)){
						//check the availibilty of restore
						if(cur_sub_entry->inode == 0){
							noent = 1;
						}else{
							//check inode bitmap
							int l = (cur_sub_entry->inode-1) / 8;
							int m = (cur_sub_entry->inode-1) - 8 * l;
							int bit = *(inode_bm + l) & (1 << m);
							if (bit){
								noent = 1;
							}else{
								*(temp_inodes + l) = *(temp_inodes + l) | (1 << m);
								struct ext2_inode *target = &inode[cur_sub_entry->inode-1];
								if((target->i_mode & 0xF000) == EXT2_S_IFDIR) {
									errno = EISDIR;
									perror("EISDIR");
									exit(EISDIR);
								}
								int block_num, b_l, b_m, b_bit;
								//check data block bitmap for the direct datablocks
								for(int j = 0; j < 12; j++){
									block_num = target->i_block[j];
									if(block_num != 0){
										b_l = (block_num - 1) / 8;
										b_m = (block_num - 1) - 8 * b_l;
										b_bit = *(block_bm + b_l) & (1 << b_m);
										if (b_bit){
											noent = 1;
										}
										*(temp_blocks + b_l) = *(temp_blocks + b_l) | (1 << b_m);
										added_block++;
									}
								}
								if(target->i_block[12] != 0){
									//check data block bitmap for the indirection block itself
									block_num = target->i_block[12];
									b_l = (block_num - 1) / 8;
									b_m = (block_num - 1) - 8 * b_l;
									b_bit = *(block_bm + b_l) & (1 << b_m);
									if (b_bit){
										noent = 1;
									}else{
										//check data block bitmap for the blocks in indirection
										*(temp_blocks + b_l) = *(temp_blocks + b_l) | (1 << b_m);
										added_block++;
										int *indirection_pointer = (int*)index(disk, target->i_block[12]);
										int k = 0;
										block_num = indirection_pointer[0];
										while(block_num != 0 && k < 128 && !noent){
											b_l = (block_num - 1) / 8;
											b_m = (block_num - 1) - 8 * b_l;
											b_bit = *(block_bm + b_l) & (1 << b_m);
											if(b_bit){
												noent = 1;
											}
											k++;
											block_num = indirection_pointer[k];
											*(temp_blocks + b_l) = *(temp_blocks + b_l) | (1 << b_m);
											added_block++;
										}
									}
								}
								//we have finished the check
								if (noent != 1){
									struct ext2_inode *target = &inode[cur_sub_entry->inode-1];
									target->i_dtime = 0;
									target->i_links_count = 1;
									memcpy(block_bm,temp_blocks, sb->s_blocks_count/8);
									sb->s_free_blocks_count -= added_block;
									gd->bg_free_blocks_count -= added_block;
									memcpy(inode_bm,temp_inodes, sb->s_inodes_count/8);
									sb->s_free_inodes_count--;
									gd->bg_free_inodes_count--;
									cur_entry->rec_len -= rest_sub_reclen;
									cur_sub_entry->rec_len = rest_sub_reclen;
									finished = 1;
								}
							}
						}
					}
					//we need to know the actual reclen of cur entry to access next potential dir entry
					cur_sub_reclen = strlen(cur_sub_entry->name) + sizeof(struct ext2_dir_entry);
					if((cur_sub_reclen % 4) != 0) {
						cur_sub_reclen = ((cur_sub_reclen / 4) + 1) * 4;
					}
					cur_sub_entry = (struct ext2_dir_entry *)((void*)cur_sub_entry + cur_sub_reclen);
					rest_sub_reclen -= cur_sub_reclen;
				}
			}
			rest_reclen -= cur_entry->rec_len;
			cur_entry = (struct ext2_dir_entry *)((void*)cur_entry + cur_entry->rec_len);
		}
		if(finished){
			break;
		}
	}
	if(!finished || noent){
		errno = ENOENT;
		perror("Can not restore file");
		exit(ENOENT);
	}

	int close_error;
	close_error = close(fp);
	if (close_error != 0) {
		perror("close fp");
		exit(errno);
	}

	return 0;
}
