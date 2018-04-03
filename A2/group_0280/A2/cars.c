#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "traffic.h"

extern struct intersection isection;

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with 
 * its in_direction
 * 
 * Note: this also updates 'inc' on each of the lanes
 */
void parse_schedule(char *file_name) {
    int id;
    struct car *cur_car;
    struct lane *cur_lane;
    enum direction in_dir, out_dir;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {

        /* construct car */
        cur_car = malloc(sizeof(struct car));
        cur_car->id = id;
        cur_car->in_dir = in_dir;
        cur_car->out_dir = out_dir;

        /* append new car to head of corresponding list */
        cur_lane = &isection.lanes[in_dir];
        cur_car->next = cur_lane->in_cars;
        cur_lane->in_cars = cur_car;
        cur_lane->inc++;
    }

    fclose(f);
}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 * 
 */
void init_intersection() {
	int i;

	for(i = 0; i < 4; i++){
		pthread_mutex_init(&isection.quad[i], NULL);
	}
	for(i = 0; i < 4; i++){
		pthread_mutex_init(&isection.lanes[i].lock, NULL);
		pthread_cond_init(&isection.lanes[i].consumer_cv, NULL);
		pthread_cond_init(&isection.lanes[i].producer_cv,NULL);
		isection.lanes[i].in_cars = NULL;
		isection.lanes[i].out_cars = NULL;
		isection.lanes[i].inc = 0;
		isection.lanes[i].passed = 0;
		isection.lanes[i].buffer = malloc(sizeof(struct car *) * LANE_LENGTH);
		isection.lanes[i].head = 0;
		isection.lanes[i].tail = 0;
		isection.lanes[i].capacity = LANE_LENGTH;
		isection.lanes[i].in_buf = 0;
		//initialize all attributes of each lane
	}
}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 * 
 */
void *car_arrive(void *arg) {
    struct lane *l = arg;

    /* avoid compiler warning */
    while(1){
		pthread_mutex_lock(&l->lock);
		if(l->in_cars == NULL){
			//check whether all cars had enter the lane
			pthread_mutex_unlock(&l->lock);
    		return NULL;
		}
		while (l->in_buf >= l->capacity){ //waiting for space in buffer
			pthread_cond_wait(&l->producer_cv, &l->lock);
		}

		//add the newcomer to the end of circular buffer
		l->in_buf++;
		l->buffer[l->tail] = l->in_cars;
		l->tail = (l->tail + 1) % LANE_LENGTH;
		l->in_cars = l->in_cars->next;

		//notify car_cross the arriving car
		pthread_cond_signal(&l->consumer_cv);
		pthread_mutex_unlock(&l->lock);
    }
    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list of the lane that corresponds to the car's
 * out_dir. Do not free the cars!
 *
 * 
 * Note: For testing purposes, each car which gets to cross the 
 * intersection should print the following three numbers on a 
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 * 
 * You may add other print statements, but in the end, please 
 * make sure to clear any prints other than the one specified above, 
 * before submitting your final code. 
 */
void *car_cross(void *arg) {
    struct lane *l = arg;
    int *path;
    struct car *cur_car;
    int i;
    struct lane *dest;

    while(1){
		pthread_mutex_lock(&l->lock);
		if(l->in_buf == 0 && l->in_cars == NULL){
			//exit if all car had crossed through this lane
			pthread_mutex_unlock(&l->lock);
			return NULL;
		}
		//waiting for cars coming into lane
		while (l->in_buf == 0){
			pthread_cond_wait(&l->consumer_cv, &l->lock);
		}

		//moving the first car in lane
		cur_car = l->buffer[l->head];
		path = compute_path(cur_car->in_dir, cur_car->out_dir);
		//acquiring the locks for quadrants needed
		for(i = 0; i < 3; i++){
			if(path[i] >= 0){
			//we check whether current element is a valid one before locking
				pthread_mutex_lock(&isection.quad[path[i]]);
			}
		}
		l->in_buf--;
		l->head = (l->head + 1) % LANE_LENGTH;
		pthread_cond_signal(&l->producer_cv);
		pthread_mutex_unlock(&l->lock);

		//move the car to the out_cars at destination
		/*I didn't acquire the destination's lock to avoid deadlock
		Also, the quadrant lock protects this part because to exit from
		a specific lane, there is a correspondent quadrant it must pass.
		*/
		dest = &isection.lanes[cur_car->out_dir];
		cur_car->next = dest->out_cars;
		dest->out_cars = cur_car;

		for(i = 0; i < 3; i++){
			if(path[i] >= 0){
				pthread_mutex_unlock(&isection.quad[path[i]]);
				//release the quadrants
			}
		}

		printf("%d %d %d\n", cur_car->in_dir, cur_car->out_dir, cur_car->id);
		free(path);
    }

    return NULL;
}

void sort(int *input){
	//the common bubblesort algorithm. Time complexity doesn't matter since we only have 3 elements.
	int i, j, temp;
	for(i = 0; i < 2; i++){
		for(j = 0; j < 2 - i; j++){
			if(input[j] > input[j+1]){
				temp = input[j];
				input[j] = input[j+1];
				input[j+1] = temp;
			}
		}
	}
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted 
 * list of the quadrants the car will pass through.
 * 
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {
	int * result;
	int i;

	/*The idea is that we have a array of size 3(the max num of quadrant we need)
	all element of the array is set to -1 at beginning, so if it is not -1 when
	returns, we know that it is valid quadrant number*/
	result = malloc(sizeof(int) * 3);
	for(i = 0; i < 3; i++){
		result[i] = -1;
	}
	/*the idea behind is that each entrance direction and exit direction
	 * corresponds to a specific quadrant it has to use
	 */
	//result[0] stores the entrance quadrant before sorting
	if (in_dir == NORTH){
		result[0] = 1;
		//if the car's in_dir is North, it must use Q2, which is
		//index 1 in intersection's lock list
	}else if(in_dir == SOUTH){
		result[0] = 3;
	}else if(in_dir == WEST){
		result[0] = 2;
	}else if(in_dir == EAST){
		result[0] = 0;
	}

	//result[1] stores exit quadrant before sorting
	if (out_dir == NORTH){
		if (result[0] != 0) //compare exit quadrant with entrance quadrant
			result[1] = 0;
	}else if(out_dir == SOUTH){
		if (result[0] != 2)
			result[1] = 2;
	}else if(out_dir == WEST){
		if (result[0] != 1)
			result[1] = 1;
	}else if(out_dir == EAST){
		if (result[0] != 3)
			result[1] = 3;
	}
	//the third quadrant is only needed when a car takes a left turn, when
	//the difference between exit and entrance is 2 (mod 4)
	if(result[1] != -1 && (result[1] - result[0]) % 4 == 2){
		result[2] = (result [0] + 1) % 4;
	}
	sort(result); //sort the result to increasing order for lock priority
    return result;
}
