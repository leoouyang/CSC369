CC=gcc
CFLAGS=-Wall -std=gnu99 -g


all : ext2_cp ext2_mkdir ext2_checker ext2_ln ext2_rm ext2_restore ext2_rm_bonus ext2_restore_bonus

ext2_cp : ext2_cp.o
	$(CC) $(CFLAGS) -o $@ $^
	
ext2_mkdir : ext2_mkdir.o
	$(CC) $(CFLAGS) -o $@ $^
	
ext2_checker : ext2_checker.o
	$(CC) $(CFLAGS) -o $@ $^

ext2_ln : ext2_ln.o functions.o
	$(CC) $(CFLAGS) -o $@ $^
	
ext2_rm : ext2_rm.o functions.o
	$(CC) $(CFLAGS) -o $@ $^

ext2_restore : ext2_restore.o functions.o
	$(CC) $(CFLAGS) -o $@ $^
	
ext2_rm_bonus : ext2_rm_bonus.o functions.o
	$(CC) $(CFLAGS) -o $@ $^

ext2_restore_bonus : ext2_restore_bonus.o functions.o
	$(CC) $(CFLAGS) -o $@ $^

%.o : %.c ext2.h functions.h
	$(CC) $(CFLAGS) -c -o $@ $<
 
clean : 
	rm -f *.o ext2_cp ext2_mkdir ext2_checker ext2_ln ext2_rm ext2_restore ext2_rm_bonus ext2_restore_bonus *~
