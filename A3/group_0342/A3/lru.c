#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

int *lrucountlist;	// an int array to store the timestamp of each frame

int lrucount = 0;	// use int to represent timestamp

/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int lru_evict() {
	
	int min = lrucountlist[0];
	int lruidx = 0;
	int loopvar;
	// this loop find the frame with the earliest(smallest) timestamp
	for(loopvar = 0; loopvar < memsize; loopvar++){

		if(lrucountlist[loopvar] <= min){
			min = lrucountlist[loopvar];
			lruidx = loopvar;
		}
	}

	return lruidx;
}

/* This function is called on each access to a page to update any information
 * needed by the lru algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(pgtbl_entry_t *p) {

	int frameidx = p->frame >> PAGE_SHIFT;
	// increase timestamp
	lrucount++;
	// assign timestamp(an int) to the the frame when it is accessed
	lrucountlist[frameidx] = lrucount;

	return;
}


/* Initialize any data structures needed for this 
 * replacement algorithm 
 */
void lru_init() {

	int loopvar;

	lrucountlist = malloc(memsize*sizeof(int));
	// initialize all timestamps to 0
	for(loopvar = 0; loopvar < memsize; loopvar++){
		lrucountlist[loopvar] = 0;
	}
}
