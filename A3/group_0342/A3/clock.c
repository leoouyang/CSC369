#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

int clockidx;

/* Page to evict is chosen using the clock algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int clock_evict() {
	
	int result = -1;

	// loop until we find a space
	while (result == -1){

		// if the reference bit is 1, set it to 0
		if (coremap[clockidx].pte->frame & PG_REF){
			coremap[clockidx].pte->frame = coremap[clockidx].pte->frame & (~PG_REF);
		}
		else{
			result = clockidx;
		}
		clockidx = (clockidx + 1) % memsize; // clock hand going around the clock
	}

	return result;
}

/* This function is called on each access to a page to update any information
 * needed by the clock algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void clock_ref(pgtbl_entry_t *p) {

	return;
}

/* Initialize any data structures needed for this replacement
 * algorithm. 
 */
void clock_init() {
	// point to the first frame in coremap
	clockidx = 0;
}
