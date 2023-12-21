#include <x86intrin.h>
#include <cpuid.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>

/*
 * processor cache specs: https://www.techpowerup.com/cpu-specs/core-i7-1065g7.c2228
 * this source says the L1 cache size is 80KB. Assuming it is evenly partitioned
 * to instruction-cache and data-cache, the L1 data cache size is 40KB. Assuming
 * the associativity is 8 and the cache line size is 64, we have 80 cache sets.
 */ 
#define S 80 		 // number of sets in L1 cache (computed using cache size)
#define W 8   		 // number of cache lines in a set
#define B 64  		 // number of bytes in cache line (block size)
#define ARRLEN S * W

#define PAGE_SIZE 4096

#define NUM_MEASUREMENTS 1000
#define NUM_OUTLIERS 200


typedef struct cacheline {
    struct cacheline *prev, *next; 	// 8  bytes (total)
    uint64_t time;          		// 8  bytes
	uint8_t padding[48];			// 48 bytes
} cacheline;

int uint64_t_cmp(const void *a, const void *b) { return ((uint64_t *) a) - ((uint64_t *) b); }


cacheline* arr;
cacheline* vic;



static inline void swap_nodes(cacheline *arr, int ind1, int ind2)
{
	cacheline *prev1, *next1, *prev2, *next2;

	prev1 = arr[ind1].prev;
	next1 = arr[ind1].next;
	prev2 = arr[ind2].prev;
	next2 = arr[ind2].next;

	if (arr[ind1].next == &(arr[ind2]))  	// [...] <--> [ind1] <--> [ind2] <--> [...]
	{
		arr[ind1].next = next2;
		next2->prev = &(arr[ind1]);

		arr[ind2].prev = prev1;
		prev1->next = &(arr[ind2]);

		arr[ind1].prev = &(arr[ind2]);
		arr[ind2].next = &(arr[ind1]);
	}
	else
		if (arr[ind2].next == &(arr[ind1]))	// [...] <--> [ind2] <--> [ind1] <--> [...]
		{
			arr[ind2].next = next1;
			next1->prev = &(arr[ind2]);

			arr[ind1].prev = prev2;
			prev2->next = &(arr[ind1]);

			arr[ind2].prev = &(arr[ind1]);
			arr[ind1].next = &(arr[ind2]);
		}
		else
		{
			arr[ind1].prev = prev2;
			arr[ind1].next = next2;

			prev2->next = &(arr[ind1]);
			next2->prev = &(arr[ind1]);

			arr[ind2].prev = prev1;
			arr[ind2].next = next1;

			prev1->next = &(arr[ind2]);
			next1->prev = &(arr[ind2]);
		}
}

static inline void swap_whole_sets(cacheline *arr, int set1, int set2)
{
	cacheline *set1_prev, *set1_next, *set2_prev, *set2_next,
			  *set1_start, *set1_end, *set2_start, *set2_end;

	set1_start = arr;
	for (int i = 0; i < set1; i++)
		for (int j = 0; j < W; j++)
			set1_start = set1_start->next;

	set1_end = set1_start;
	for (int j = 0; j < W-1; j++)
		set1_end = set1_end->next;

	set2_start = arr;
	for (int i = 0; i < set2; i++)
		for (int j = 0; j < W; j++)
			set2_start = set2_start->next;
	
	set2_end = set2_start;
	for (int j = 0; j < W-1; j++)
		set2_end = set2_end->next;

	set1_prev = set1_start->prev;
	set1_next = set1_end->next;
	
	set2_prev = set2_start->prev;
	set2_next = set2_end->next;


	// The 2 cases where the sets are consecutive are handled separately
	if ((set1 + 1) % S == set2)
	{
		set2_start->prev = set1_prev;
		set1_prev->next = set2_start;

		set1_end->next = set2_next;
		set2_next->prev = set1_end;
	
		set2_end->next = set1_start;
		set1_start->prev = set2_end;
	}
	else
		if ((set2 + 1) % S == set1)
		{
			set1_start->prev = set2_prev;
			set2_prev->next = set1_start;

			set2_end->next = set1_next;
			set1_next->prev = set2_end;

			set1_end->next = set2_start;
			set2_start->prev = set1_end;	
		}
		else
		{
			set1_start->prev = set2_prev;
			set2_prev->next = set1_start;

			set1_end->next = set2_next;
			set2_next->prev = set1_end;

			set2_start->prev = set1_prev;
			set1_prev->next = set2_start;

			set2_end->next = set1_next;
			set1_next->prev = set2_end;
		}
}

static inline void shuffle_linked_list(cacheline *arr)
{
	int i, j, r;

	// shuffle sets
	for (i = 0; i < S - 1; i++)
	{
		r = rand() % (S - (i+1)) + i+1;
		swap_whole_sets(arr, i, r);
	}

	// shuffle lines within each set
	for (i = 0; i < S; i++)
		for (j = 0; j < W - 1; j++)
		{
			r = rand() % (W - (j+1)) + j+1;
			swap_nodes(arr, i*W + j, i*W + r);
		}
}



static inline void init()
{
	uint64_t start;
	int dummy;

	// allocate arrays in a way that they would start in the very beginning 
	// of some memory page (this reduces the overhead of memory accesses)
	arr = (cacheline*)__mingw_aligned_malloc(ARRLEN * sizeof(cacheline), PAGE_SIZE);
	vic = (cacheline*)__mingw_aligned_malloc(ARRLEN * sizeof(cacheline), PAGE_SIZE);

	// init linked list
	for (int i = 0; i < ARRLEN; i++)
	{
		if (i > 0)
			arr[i].prev = &(arr[i-1]);
		else
			arr[i].prev = &(arr[ARRLEN-1]);

		if (i+1 < ARRLEN)
			arr[i].next = &(arr[i+1]);
		else
			arr[i].next = &(arr[0]);
	}
	shuffle_linked_list(arr);

	// warm up CPU
	start = __rdtscp(&dummy);
	while ((__rdtscp(&dummy) - start) < 1000000000) ;
}

static inline void soft_init()
{
	for (int i = 0; i < S; i++)
		arr[i*W + W-1].time = 0;
}

static inline void prime()
{
	cacheline *curr = arr;
	cacheline dummy;
	
	// traverse linked list until you reach the starting point
	do {
		curr = curr->next;
	} 
	while (curr != arr) ;
}

static inline void probe()
{
	int a,b,c,d,e, dummy;

	uint64_t start, end;

	cacheline *curr = arr;
	cacheline tmp;

	do {		
		__cpuid(a,b,c,d,e);	// ensure all previous instructions terminate
		start = __rdtsc();

		for (int i = 0; i < W-1; i++)
			curr = curr->prev;
		
		end   = __rdtscp(&dummy);
		__cpuid(a,b,c,d,e);	// ensure all following instructions do not start yet
		
		curr->time = (end - start) / W;
		
		curr = curr->prev;
	} 
	while (curr != arr) ;
}

static inline void victim()
{
	uint64_t tmp;

	for (uint64_t i = 0; i < 1000000; i++)
		// tmp += vic[0*W].time;
		tmp += arr[0*W].time;
}



static inline void measure_once(uint64_t *times)
{
	prime();
	// victim();
	probe();

	for (int i = 0; i < S; i++)
		times[i] = arr[i*W + W-1].time;
}

static inline void calc_mean_var(double *E_arr, double *V_arr , uint64_t all_times[NUM_MEASUREMENTS][S])
{
	uint64_t sorted_times[S][NUM_MEASUREMENTS];

	// sort
	for (int i = 0; i < S; i++)
	{
		for (int j = 0; j < NUM_MEASUREMENTS; j++)
			sorted_times[i][j] = all_times[j][i];
		
		qsort(sorted_times[i], NUM_MEASUREMENTS, sizeof(uint64_t), uint64_t_cmp);
	}	
	
	// calc
	for (int i = 0; i < S; i++)
	{
		E_arr[i] = 0;
		for (int j = 0; j < NUM_MEASUREMENTS - NUM_OUTLIERS; j++)
			E_arr[i] += sorted_times[i][j];
		
		E_arr[i] /= (NUM_MEASUREMENTS - NUM_OUTLIERS);
	}	
	for (int i = 0; i < S; i++)
	{
		V_arr[i] = 0;
		for (int j = 0; j < NUM_MEASUREMENTS - NUM_OUTLIERS; j++)
			V_arr[i] += pow((double)(sorted_times[i][j] - E_arr[i]), 2);
		
		V_arr[i] /= (NUM_MEASUREMENTS - NUM_OUTLIERS);
	}		
}



void main()
{
	int      i;
	double   E_arr[S], V_arr[S];
	uint64_t all_times[NUM_MEASUREMENTS][S];
	
	init();
	
	for (i = 0; i < NUM_MEASUREMENTS; i++)
	{
		// if (i%5 == 0)
		// 	printf("--------------- cycle number %2d --------------- \n", (i+1));

		soft_init();
		measure_once(all_times[i]);
	}

	free(arr);
	free(vic);
	
	printf("\n---------------  stats summary  ----------------\n");
	calc_mean_var(E_arr, V_arr, all_times);
	for (i = 0; i < S; i++)
		printf("Set %2d\tE = %2.0lf\tsigma = %2.0lf\n", i, E_arr[i], sqrt(V_arr[i]));
}

// ---------------------- some previously useful print commands ----------------------------
/* 
 * 
 *
 * if (arr[0*W + W-1].time > 0.1  ||  arr[1*W + W-1].time > 0.1)
 * 	printf("cycle %2d\tset 0 access time: %" PRIu64 "\tset 1 access time: %" PRIu64 "\n", (j+1), arr[0*W + W-1].time, arr[1*W + W-1].time);
 * 
 * 
 */
// -----------------------------------------------------------------------------------------
