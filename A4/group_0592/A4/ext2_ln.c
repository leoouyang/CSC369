#include "ext2.h"
#include "functions.h"


int main(int argc, char **argv){
	if(argc != 4 && argc != 5) {
		fprintf(stderr, "Usage: %s <image file name> <flag -s> <source> <target>\n",
				argv[0]);
		exit(1);
	}

	int fp = open(argv[1], O_RDWR);
	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fp, 0);
	if(disk == MAP_FAILED) {
		perror("mmap");
		exit(errno);
	}


	int soft, opt;
	soft = 0;
	while ((opt = getopt(argc, argv, "s")) != -1){
		switch(opt){
		case 's':
			soft = 1;
			break;
		default:
			fprintf(stderr, "Usage: %s <image file name> <flag -s> <source> <target>\n",
							argv[0]);
			exit(1);
		}
	}

	struct ext2_super_block *sb = (struct ext2_super_block *)index(disk, 1);
	struct ext2_group_desc *gd = (struct ext2_group_desc *)index(disk, 2);
	struct ext2_inode *inode = (struct ext2_inode *)index(disk, gd->bg_inode_table);

	char *src_path, *link_path;
	if (soft){
		//if it is a symlink, we will need to create a new inode
		if (sb->s_free_inodes_count == 0){
			errno = ENOSPC;
			perror("Inode full");
			exit(ENOSPC);
		}
		src_path = argv[3];
		link_path = argv[4];
	}else{

		src_path = argv[2];
		link_path = argv[3];
	}
	int src_dir_i = pathfinder(disk, src_path);
	if(src_dir_i < -1){ //path is wrong if pathfinder return negative
		exit(-src_dir_i);
	}
	int link_dir_i = pathfinder(disk, link_path);
	if(link_dir_i < 0){
		exit(-link_dir_i);
	}

	char* src_file_name = strrchr(src_path, '/') + 1;
	int src_i = search_dir(disk, src_dir_i, src_file_name);
	if (src_i < 0){ //if search_dir return -1, it means it can't find the file in the directory
		if(src_i == -ENOENT){
			errno = ENOENT;
			perror("Incorrect filename");
		}
		exit(-src_i);
	}
	struct ext2_inode *src = &inode[src_i];
	if((src->i_mode & 0xF000) == EXT2_S_IFDIR) {
		errno = EISDIR;
		perror("EISDIR");
		exit(EISDIR);
	}

	struct ext2_inode *link_dir = &inode[link_dir_i];
	char* link_file_name = strrchr(link_path, '/') + 1;
	if(strlen(link_file_name) > EXT2_NAME_LEN){
		errno = ENAMETOOLONG;
		perror("ENAMETOOLONG");
		exit(ENAMETOOLONG);
	}
	int link_i = search_dir(disk, link_dir_i, link_file_name);
	if (link_i > 0){ //if search_dir return something positive, filename is existed
		errno = EEXIST;
		perror("EEXIST");
		exit(EEXIST);
	}

	struct ext2_dir_entry *cur_dir_entry;
	int freepointer = 0;
	int need_block = 0;
	int cur_last_reclen;
	unsigned short rest_reclen;
	int new_reclen = strlen(link_file_name) + sizeof(struct ext2_dir_entry);
	if((new_reclen % 4) != 0) {
		new_reclen = ((new_reclen / 4) + 1) * 4;
	}
	//for each directories's data block, check whether there is space are the end of block
	for(int i = 0; link_dir->i_block[i] != 0; i++) {
		freepointer = i;
		cur_dir_entry = (struct ext2_dir_entry*)index(disk, link_dir->i_block[i]);
		rest_reclen = EXT2_BLOCK_SIZE;
		while(rest_reclen != cur_dir_entry->rec_len) {
			rest_reclen = rest_reclen - cur_dir_entry->rec_len;
			cur_dir_entry = (struct ext2_dir_entry *)((void*)cur_dir_entry + cur_dir_entry->rec_len);
		}

		// The actual directory length for the last directory object
		cur_last_reclen = (unsigned short)cur_dir_entry->name_len + sizeof(struct ext2_dir_entry);
		if((cur_last_reclen % 4) != 0) {
			cur_last_reclen = ((cur_last_reclen / 4) + 1) * 4;
		}
		if(rest_reclen < cur_last_reclen + new_reclen && link_dir->i_block[i+1] == 0){
			need_block = 1;
		}
		if (rest_reclen >= cur_last_reclen + new_reclen){
			break;
		}
	}
	//we need to allocate a new block for the directory if it is full
	if (need_block){
		// if soft is 1, we will need 1 more block besides this one to store the symlink information
		//so in that case, wee need at least 2 blocks left at this point.
		if(sb->s_free_blocks_count == soft){
			errno = ENOSPC;
			perror("Blocks full");
			exit(ENOSPC);
		}
		unsigned char *block_bm = index(disk, gd->bg_block_bitmap);
		int freeblockidx, finishedblock = 0;
		for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
			for(int l = 0; (l < 8) && !finishedblock; l++){
				int bit = *(block_bm + k) & (1 << l);
				if (!bit){
					finishedblock = 1;
					freeblockidx = (8 * k) + l + 1; // block index starts from 1
					*(block_bm + k) = *(block_bm + k) | (1 << l);
					gd->bg_free_blocks_count--;
					sb->s_free_blocks_count--;
				}
			}
		}
		freepointer++;
		link_dir->i_block[freepointer] = freeblockidx;
		link_dir->i_blocks += 2;
		link_dir->i_size += EXT2_BLOCK_SIZE;
		cur_dir_entry = (struct ext2_dir_entry*)index(disk, freeblockidx);
		memset(cur_dir_entry, 0, EXT2_BLOCK_SIZE);
		cur_dir_entry->rec_len = EXT2_BLOCK_SIZE;
	}else{
		cur_dir_entry->rec_len = cur_last_reclen;
		cur_dir_entry = (struct ext2_dir_entry*)((void*)cur_dir_entry + cur_last_reclen);
		cur_dir_entry->rec_len = rest_reclen - cur_last_reclen;
	}

	cur_dir_entry->name_len = strlen(link_file_name);
	strncpy(cur_dir_entry->name, link_file_name, cur_dir_entry->name_len);

	//different operations depends on whether it is symlink or hard link
	if(!soft){
		cur_dir_entry->inode = src_i + 1;
		cur_dir_entry->file_type = EXT2_FT_REG_FILE;
		src->i_links_count++;
	}else{
		//i put the check for whether there is free inode at beginning to increase efficiency
		unsigned char *inode_bm = index(disk, gd->bg_inode_bitmap);
		int freeinodeidx = 0,finishedinode = 0;
		for(int i = 0; (i < (sb->s_inodes_count / 8)) && !finishedinode; i++) {
			for(int j = 0; (j < 8) && !finishedinode; j++){
				int bit = *(inode_bm + i) & (1 << j);
				if (!bit){
					finishedinode = 1;
					freeinodeidx = (8 * i) + j; // inode index starts from 0
					*(inode_bm + i) = *(inode_bm + i) |(1 << j);
					gd->bg_free_inodes_count--;
					sb->s_free_inodes_count--;
				}
			}
		}

		struct ext2_inode *cur_inode = &inode[freeinodeidx];
		cur_inode->i_mode = EXT2_S_IFLNK;
		cur_inode->i_uid = 0;
		cur_inode->i_size = strlen(src_path);
		cur_inode->i_gid = 0;
		cur_inode->i_links_count = 1;
		cur_inode->i_blocks = 2;
		cur_inode->osd1 = 0;
		cur_inode->i_generation = 0;
		cur_inode->i_faddr = 0;
		cur_inode->i_file_acl = 0;
		cur_inode->i_dir_acl = 0;
		cur_inode->i_dtime = 0;
		for (int i = 0; i < 15; i++){
			cur_inode->i_block[i] = 0;
		}

		if(sb->s_free_blocks_count == 0){
			errno = ENOSPC;
			perror("Blocks full");
			exit(ENOSPC);
		}
		//allocate block to store the path in symlink
		unsigned char *block_bm = index(disk, gd->bg_block_bitmap);
		int freeblockidx = 0, finishedblock = 0;
		for(int k = 0; (k < (sb->s_blocks_count / 8)) && !finishedblock; k++) {
			for(int l = 0; (l < 8) && !finishedblock; l++){
				int bit = *(block_bm + k) & (1 << l);
				if (!bit){
					finishedblock = 1;
					freeblockidx = (8 * k) + l + 1; // block index starts from 1
					*(block_bm + k) = *(block_bm + k) | (1 << l);
					gd->bg_free_blocks_count--;
					sb->s_free_blocks_count--;
				}
			}
		}
		void *datablock = (void*)index(disk, freeblockidx);
		memset(datablock, 0, EXT2_BLOCK_SIZE);
		memcpy(datablock, src_path, strlen(src_path)+1);

		cur_inode->i_block[0] = freeblockidx;
		cur_dir_entry->inode = freeinodeidx + 1;
		cur_dir_entry->file_type = EXT2_FT_SYMLINK;
	}

	int close_error;
	close_error = close(fp);
	if (close_error != 0) {
		perror("close fp");
		exit(errno);
	}

	return 0;
}

