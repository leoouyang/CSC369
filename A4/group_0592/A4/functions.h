
#ifndef FUNCTIONS_H_
#define FUNCTIONS_H_
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#define index(disk, i) (disk + (i * EXT2_BLOCK_SIZE))
unsigned char *disk;
int pathfinder(unsigned char* disk, char* path);
int search_dir(unsigned char* disk, int dir_i, char* input);
#endif /* FUNCTIONS_H_ */
