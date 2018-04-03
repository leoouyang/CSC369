#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"
#define MAXLINE 256
#define SIMPAGESIZE 16

extern int memsize;

extern int debug;

extern struct frame *coremap;

extern char *tracefile;

extern char *physmem;

// a node in linked list which is used to store the timestamp of vaddr
typedef struct Timestamp{
	int optidx;
	struct Timestamp *next;
} timestamp;
// a node in linked list which is used to store each vaddr
typedef struct Vaddrlist{
	addr_t vaddr;
	timestamp *time;
	struct Vaddrlist *next;
} vaddrlist;

vaddrlist *head;

/* Page to evict is chosen using the optimal (aka MIN) algorithm. 
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int opt_evict() {
	
	int max = 0;
	int loopvar, returnidx;;
	vaddrlist *cur_addr;

	// evict the vaddr with the furthest timestamp
	for(loopvar = 0; loopvar < memsize; loopvar++){
		char *memptr = &physmem[loopvar*SIMPAGESIZE];
		addr_t vaddr = *(addr_t *)(memptr + sizeof(int));

		cur_addr = head;
		while(cur_addr != NULL){
			if(cur_addr->vaddr == vaddr){
				if(cur_addr->time == NULL){
					return loopvar;
				}else{
					if(cur_addr->time->optidx >= max){
						max = cur_addr->time->optidx;
						returnidx = loopvar;
					}
				}
			}
			cur_addr = cur_addr->next;
		}
	}
	return returnidx;
}

/* This function is called on each access to a page to update any information
 * needed by the opt algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void opt_ref(pgtbl_entry_t *p) {
	vaddrlist *cur_addr = head;
	timestamp *prev_time;

	int framenum = p->frame >> PAGE_SHIFT;
	char *memptr = &physmem[framenum*SIMPAGESIZE];
	addr_t vaddr = *(addr_t *)(memptr + sizeof(int));

	// make the timestamp of the referenced vaddr point to its next timestamp
	while(cur_addr->vaddr != vaddr && cur_addr != NULL){
		cur_addr = cur_addr->next;
	}
	if (cur_addr == NULL){
		fprintf(stderr, "Can't find the address in LL. This should not happen!!");
		exit(1);
	}
	prev_time = cur_addr->time;
	cur_addr->time = cur_addr->time->next;
	free(prev_time);

	return;
}

/* Initializes any data structures needed for this
 * replacement algorithm.
 */
void opt_init() {

	vaddrlist *cur_addr, *prev_addr;
	timestamp *cur_time;
	char buf[MAXLINE];
	addr_t vaddr;
	char type;
	int count = 0;

	FILE *tfp;

	if((tfp = fopen(tracefile, "r")) == NULL) {
		perror("Error opening tracefile:");
		exit(1);
	}

	// put each vaddr and its timestamp in linked lists
	while(fgets(buf, MAXLINE, tfp) != NULL) {
		if(buf[0] != '=') {
			sscanf(buf, "%c %lx", &type, &vaddr);
		}

		if (head == NULL){
			head = malloc(sizeof(struct Vaddrlist));
			head->vaddr = vaddr;
			head->time = malloc(sizeof(struct Timestamp));
			head->time->optidx = count;
		}else{
			cur_addr = head;
			while (cur_addr != NULL && cur_addr->vaddr != vaddr){
				prev_addr = cur_addr;
				cur_addr = cur_addr->next;
			}
			if(cur_addr == NULL){
				cur_addr = malloc(sizeof(struct Vaddrlist));
				cur_addr->vaddr = vaddr;
				cur_addr->time = malloc(sizeof(struct Timestamp));
				cur_addr->time->optidx = count;
				prev_addr->next = cur_addr;
			}else{
				cur_time = cur_addr->time;
				while (cur_time->next != NULL){
					cur_time = cur_time->next;
				}
				cur_time->next = malloc(sizeof(struct Timestamp));
				cur_time->next->optidx = count;
			}
		}
		count++;
	}
	if(debug){
		if(head == NULL)
			printf("no node in LL");
		else{
			cur_addr = head;
			while (cur_addr != NULL){
				printf("Address: %lx\n", cur_addr->vaddr);
				cur_time = cur_addr->time;
				while (cur_time != NULL){
					printf("time: %d\n", cur_time->optidx);
					cur_time = cur_time->next;
				}
				cur_addr = cur_addr->next;
			}
		}
	}
}

