// turbines.cpp : Defines the entry point for the console application.
//
#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>  // this gets done in my time_functions.h 
// old ptghreads.h doersn't know that the time.h it includes already defined time_spec
#define HAVE_STRUCT_TIMESPEC
#include "pthread.h"
#include "sched.h"
#include "semaphore.h"

// macros, structs and typedefs to prevent forward reference errors
#define pause scanf("%c", &junk);
#define RSIZE 3;
#define CSIZE 5;

typedef float power;  // real type then pseudo_type
FILE* fp;
typedef struct threadargsx // 4th parameter to pthread_create
{
	pthread_t thread_ID;
	int m; // row# of current thread in grid
	int n; // col# of current thread in grid
} threadargs;

//  function prototypes for overall program execution
void array_test();
void get_primary_inputs();
void get_grid_values();		//Gets both max and current values
void create_threads(int,int);
void* mainloop(void*);			// get input, start next cycle, output results
void* turbine_n(void* /*mn_pairs_grid*/ );
void add_completed_thread(); // incremented by each thread when a cycle finishes and waits to be restarted


// initialization functions
pthread_t* create_thread_ID_grid(int r, int c);	// creates -> thread_ID_array
// -> pairs of coordinates for threads to self-locate
power* create_cvalue_grid(int m, int n);			// creates -> cvalue_grid array for current values
power* create_maxvalue_grid(int m, int n);			// creates -> cvalue_grid array for current values
threadargs* create_arg_grid();			// each thread has to know its own m,n position
power compute_grid_avg();
void display_grid_and_avg();

void get_MN(threadargs *blob, int * const &i, int * const &j); // p1 -> a single struct within the array
void set_MN(int m, int n); // values to INSERT into array struct
float get_power_val(power* gridptr, int r, int c); // ptr points to the array we want to use
//void set_power_val(power& gridptr, int a, int b, float val);

// data
int M, N; float T; int row_width_int,row_width_args;



pthread_t* thread_ID_grid_ptr;
threadargs* thread_arg_grid_ptr;			// ptr to grid of thread arguments (one cell for each thread)
//mn_pairs_grid* mn_pairs_grid_ptr;
power* cvalue_grid_ptr;
power* maxvalue_grid_ptr;
power* current_avg;
char input_file_comment[80];
power cycle_target;
power grid_avg;
char junk; // for discardable input for pausing output
int activeThreads = 0;   /* number of threads created. set in create_threads */
int waitingThreads = 0;  /* number of threads waiting on the condition */
int readyFlag = 0;       /* flag to tell the threads to proceed when signaled */
pthread_cond_t  cond;    /* condition to wait on / signal */
pthread_mutex_t mtx;     /* mutex for the above */
pthread_cond_t  condWaiting; /* EDIT: additional condition variable to signal
							 * when each thread starts waiting */



pthread_cond_t cycle_starting= PTHREAD_COND_INITIALIZER; // initialized before broadcast. Used by threads
//pthread_cond_t cycle_started= PTHREAD_COND_INITIALIZER; // used by completion counter
//sem_t latch; // set by completion counter
//pthread_mutex_t tlock=PTHREAD_MUTEX_INITIALIZER; // used internally by threads

/* ------------------------------------------------------
remaining problems:
1. make sure main waits for all threads to finish computing before next cycle 
	(must be sure to "fake it" (i.e.; make it true) for 1st time)
2. make sure each thread waits for the broadcast at start of cycle
---------------------------------------------------------*/

int main()
{
	//array_test();
	//pause
	// read the input file containing all the setup needed.
	get_primary_inputs(); // open the input file and get M, N and T
	// now that we know the grid size, we can build them, then get their values.
	// need malloc'd M*N arrays
	// create the grids and the pointers to them	
	
	// array of turbine thread IDs used for pthread_create
	thread_ID_grid_ptr = create_thread_ID_grid(M, N); 

	cvalue_grid_ptr = create_cvalue_grid(M, N);	// array of power outputs
	maxvalue_grid_ptr=create_cvalue_grid(M, N);	// array of power outputs
	thread_arg_grid_ptr  = create_arg_grid();	// array of structs containing parameters per thread

	get_grid_values(); // continue reading the input to get grid values
	grid_avg = compute_grid_avg(); // This is the current value to be matched against the 1st target

	fscanf(fp, "%s", &input_file_comment); // get header for cycle target values
	printf("cycle hdr=%s\n", input_file_comment);
	
	
	// input criteria:
	//  1. grid size:  M * N   (no requirement for grid to be square)
	//  2. cycle time: T in seconds (i.e.; .3 = 300 ms)
	//  3. initial power: m*n of values per turbine
	//  4. maximum power: m*n of values per turbine
	//  5. sequence of values (average values for the whole grid) to reach by changing turbine settings	
	//                      

	// Before creating threads, we need to:
	// 1. set the pthread_cond_t variable for them to wait on 
	// 2. set up the array of grid coordinate structures, so we can pass the x,y pairs
	//		for each thread at creation time, so it knows where it is in the grid.
	// 3. set the latch (a counting semaphore) for main to wait on before starting the next cycle
	// 4. initialize the latch HERE to 1 so it falls through
	//		after end of each cycle, re-initialize it to M*N

	// MUST initialize before starting the threads
	/*sem_init(&latch, 0, M*N); // initialize the latch counter
	cycle_starting = PTHREAD_COND_INITIALIZER;
	*/
	pthread_mutex_init(&mtx, NULL);
	pthread_cond_init(&cond, NULL);
	pthread_cond_init(&condWaiting, NULL); /* EDIT: if main thread should block
										   * until all threads are waiting */

		// loop creating the threads
	create_threads(M,N);
	// wait 0.5 sec then compute the complete avg
	// and put the result in  global variable
	// meanwhile, each thread waits 0.5 sec then computes its neighbor avg
	// an dcompares it t mster avg and ecides whether to increasse or ecrease utput.
	// result outputis placed in global array for master calc and local calcs.
	printf("threads created and started\n");
	pthread_t mainloop_id;
	pthread_create(&mainloop_id, NULL, &mainloop, NULL);
	printf("--main-- sleeping for testing only\n");
	// mainloop() is now running  // get next cycle value, wakeup threads, output results
	//Sleep(10000);
	pause // wait for console input here so user can see outputs
	// end of for loop here (no more cycles to handle) 
	return 0;
}

void* mainloop(void*)
{
	//  loop through all cycle values
	int eofile = 0; int pauser = 20;
	while (!eofile)
	{
		pauser--;
		eofile = fscanf(fp, "%f", &cycle_target); // get target value for this cycle
		if (eofile = EOF) eofile = 0;
		printf("power req for this cycle is: %f\n", cycle_target); // show we got it right
		printf("latch initialized\n");
		//pthread_cond_broadcast(&cycle_starting);	// now tell all threads to start a new cycle
		printf("broadcast sent. Waiting for latch\n");
		/*sem_wait(&latch); // wait until all threads are done with the previous cycle
		// MUST re-initialize for next cycle
		sem_init(&latch, 0, 0); // now it's OK to reset the latch counter
		pthread_cond_destroy(&cycle_starting);		// MUST destroy before re-init
		pthread_cond_init(&cycle_starting,NULL);			// re-initialize the cond_var for threads
		//cycle_starting = PTHREAD_COND_INITIALIZER;	// re-init the cond var for threads
		pthread_cond_signal(&cycle_started);		//re-init cond var for "latch setting code"
		*/
		printf("waiting=%i active=%i\n", waitingThreads, activeThreads);
		if (pauser=0) pause
		pthread_mutex_lock(&mtx);
		while (waitingThreads < activeThreads) { /* wait on 'condWaiting' until all
												 * active threads are waiting */
			pthread_cond_wait(&condWaiting, &mtx);
		}
		if (waitingThreads != 0) {
			readyFlag = 1;
			pthread_cond_broadcast(&cond);
		}
		pthread_mutex_unlock(&mtx);
		// now we can do anythikng we want here or justr go ack to the while & wait
	}
	return NULL; // when this return occurs, main will be able to wait for user to close the window
}
void get_primary_inputs() 
{
	char filename[25] ="" ; // path eventually to be made portable w/ my macros
	
	fp = fopen("c:\\temp\\coursein\\turbine_setup-3.txt", "r");
		
	if (fp == NULL)
	{
		perror("Error while opening the file. Press any alpha key.\n");
		scanf("%c", &junk);
		exit(EXIT_FAILURE);
	}
	fscanf(fp, "%i %i %f", &M, &N, &T); // get 1st line
	printf("M, N, T= %d %d %f\n", M, N, T); // show we got it right
	fscanf(fp, "%s", &input_file_comment); // get comment1
	printf("comment is: %s\n", input_file_comment); // show we got it right
	printf("end of main inputs (M, N, T)\n");
	row_width_int = N;
	row_width_args = N /**sizeof(threadargs)*/;
}

power* create_cvalue_grid(int m, int n)
{
	power* cvgrid = (power*)malloc(m * n * sizeof(power));
	return (cvgrid);
}

power* create_maxvalue_grid(int m, int n)
{
	power* mvgrid = (power*)malloc(m * n * sizeof(power));
	return (mvgrid);
}

void get_grid_values() // max & current grid values
{
	power avalue; int i, j;
	// now loop through the data to get the max vals for our grid
	fscanf(fp, "%s", &input_file_comment); // get comment1
	printf("max values hdr= %s \n", input_file_comment); // show we got it right
	for (int i = 0; i < M; i++)
		for (int j = 0; j < N; j++)
		{
			fscanf(fp, "%f", &avalue); // get a grid value
			cvalue_grid_ptr[i*row_width_int+j] = avalue;
		}
	// now loop through the data to get the current vals for our grid
	fscanf(fp, "%s", &input_file_comment); // get comment1
	printf("current values hdr= %s \n", input_file_comment); // show we got it right
	for (i = 0; i < M; i++)
		for (j = 0; j < N; j++)
		{
			fscanf(fp, "%f", &avalue); // get a grid value
			maxvalue_grid_ptr[i*row_width_int + j] = avalue;
		}
}

/*
The four parameters to pthread_create are, in order:
1.A pointer to a pthread_t structure, which pthread_create will fill out with information on the thread it creates.
2.A pointer to a pthread_attr_t with parameters for the thread. You can safely just pass NULL most of the time.
3.A function to run in the thread. The function must return void * and take a void * argument, which you may use however you see fit. (For instance, if you're starting multiple threads with the same function, you can use this parameter to distinguish them.)
4.The void * that you want to start up the thread with. Pass NULL if you don't need it.
*/
void create_threads(int m, int n)
{	
	int i, j, errcode;
	// the 4th parm is a struct* containing the x,y coords of the thread being created
	// why can't we just let the thread acces a global pair? (overwriting)
	for (i = 0; i < m; i++)
		for (j = 0; j < n; j++)
		{
			//printf("i=%i, j=%i, threadargs=%i,%i\n",i,j, thread_arg_grid_ptr[i, j].m, thread_arg_grid_ptr[i, j].n);
			errcode = pthread_create(&thread_ID_grid_ptr[i][j].pthread_ID, NULL, &turbine_n,
				(void*)&thread_arg_grid_ptr[i][j]);
			pause
			if (errcode)
			{
				printf("error code %i in pthread_create", errcode);
				if (errcode = EAGAIN) printf("EAGAIN\n");
				if (errcode = EINVAL) printf("EINVAL\n");
				if (errcode = EPERM) printf("EPERM\n");
				pause
				exit (0);
			}
			activeThreads++;
		}
	printf("created %i threads\n", m*n);
}

void* turbine_n(/*mn_pairs_grid*/ void* x) // this is the function that becomes a thread (M*N times)
{// m & n are the coordinates of THIS turbine in the grid passed as a ptr to a value_pair, NOT as parameters
	// 1st set up the named ptr to the grid, then get the values for m,n for this thread
 threadargs *myargs = (threadargs*)x; // re-cast to proper type
	int m, n; int counter = 0;
	get_MN(myargs, &m,  &n); // get_MN will change my m and n
	/*m = myargs->m;
	n = myargs->n; */
	printf("thread %i, %i started\n", m, n);
	return NULL;
	while (1)
		// main will signal the condition (cycle_starting) and every thread must test it before it can begin
	{
		counter++; if (counter > 2) return NULL;
		printf("thread %i, %i waiting on condvar\n", m, n);
		// If the mutex is already locked, the calling thread blocks until the mutex becomes available. 
		/*pthread_mutex_lock(&tlock);	// must lock around the test 
		while (!cycle_starting)	//mainloop must have just awakened
			// the wait will unlock the condvar to unblock other threads who can try to start
			pthread_cond_wait(&cycle_starting, &tlock);
		pthread_mutex_unlock(&tlock);	// this releases OWNERSHIP of the mutex, so nother thread can lock it
		*/
		/* When the threads should wait, do this (they wait for 'readyFlag' to be
		* set, but also adjust the waiting thread count so the main thread can
		* determine whether to broadcast) */
		pthread_mutex_lock(&mtx);
		if (readyFlag == 0) {
			waitingThreads++;
			do {
				pthread_cond_signal(&condWaiting); /* EDIT: signal the main thread when
												   * a thread begins waiting */
				pthread_cond_wait(&cond, &mtx);
			} while (readyFlag == 0);
			waitingThreads--;
		}
		pthread_mutex_unlock(&mtx);

		// if we get here, the cond var has been signalled and we can go to work
		
		// so NOW we can wait for the broadcast
		
		printf("thread %i, %i working\n", m, n);
		printf("thread %i, %i completed a cycle\n", m, n);
		// now compute our new power value and wait for next signal from mainloop

		// NOW signal the latch to decrement the semaphore. 
		// when latch reaches zero, another cycle can begin
		//add_completed_thread(); // increment (unlock) the semaphore to allow mainloop to run when it gets to M*N
								/* When threads finish a cycle, they decrement the active thread count */
		pthread_mutex_lock(&mtx);
		activeThreads--;
		pthread_cond_signal(&condWaiting); /* EDIT: also signal the main thread
										   * when a thread exits to make main thread
										   * recheck the waiting thread count if
										   * waiting for all threads to wait */
		pthread_mutex_unlock(&mtx);
	}
	
	return (NULL);
	// now we can work on our new value
}

pthread_t* create_thread_ID_grid(int r, int c)
{
	// point to a blob of RAM that will be TREATED as an R*C array
	// entries are accessed as arr[a,b] as if it was a static array
	pthread_t *arr = (pthread_t *)malloc(r * c * sizeof(pthread_t));
	return (arr);
}

threadargs* create_arg_grid()
{ 
	threadargs* thread_arg_grid_ptr=(threadargs *)malloc(M * N * sizeof(threadargs));
	if (thread_arg_grid_ptr = NULL)
	{
		printf("thread_arg_grid_ptr is null");
		pause
			exit (0);
	}
	//printf("threadargs size in bytes=%i\n", sizeof(threadargs));
	//printf("arraysize in bytes=%i\n", M * N * sizeof(threadargs));
		pause
	for (int i = 0; i < M; i++)
		for (int j = 0; j < N; j++)
			/*{
				thread_arg_grid_ptr_lcl[i, j].m = i;
				thread_arg_grid_ptr_lcl[i, j].n = j;
				printf("arg[%i,%i]=%i,%i\n", i, j, thread_arg_grid_ptr_lcl[i, j].m, thread_arg_grid_ptr_lcl[i, j].n);
			}*/
			// put the M,N values into array at M,N
			set_MN(M, N); // puts the current values of M & N in the LINEAR array of structs
	printf("all args inserted");
	for (int i = 0; i < M; i++)
		for (int j = 0; j < N; j++)
			printf("arg[%i,%i]=%i,%i\n", i, j,
				thread_arg_grid_ptr[i * row_width_args+ j].m, thread_arg_grid_ptr[i*row_width_args + j].n);

	return(thread_arg_grid_ptr);
}

power compute_grid_avg()
{
	power avgvalue; power sum=0;
	int i, j;
	for (i = 0; i < M; i++)
		for (j = 0; j < N; j++)
		{
			sum=sum+cvalue_grid_ptr[i*row_width_int + j];
		}
	avgvalue = sum / (M*N);
	printf("current average is: %f", avgvalue); // get a grid value
	return avgvalue;
}

void display_grid_and_avg()
{
	float current_avg;
	current_avg=compute_grid_avg();
	// now loop through grid to display it
}

// ******************* next 2 functions run with mutual exclusion
// they use a semaphore to protect the counting operation
// and signal ANOTHER semaphore to tell release mainloop for another cycle
// mainloop will use pthread_barrier_wait
//int count_of_completed_threads = 0;
pthread_mutex_t count_lock= PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t threads_are_ready = PTHREAD_COND_INITIALIZER;
int thread_done_count=0;

void add_completed_thread() // gets incremented by each thread when it finishes and waits to be restarted
{/*
	pthread_mutex_lock(&count_lock);	// must lock around the test 
	while (!cycle_started)
		// test below will unlock the condvar to unblock other threads
		pthread_cond_wait(&cycle_started, &count_lock); 
	// get here when the above condition has been met
	thread_done_count++;
	if (thread_done_count >= M * N)
	{
		thread_done_count = 0;	// ready to start the next cycle
	   // now we can let mainloop run again.	
		//pthread_cond_signal(&threads_are_ready);
		sem_post(&latch);
	}
	pthread_mutex_unlock(&count_lock); */
}

int c = sizeof(threadargs);
threadargs *arr;
void array_test() // kept for future use (in other programs)
{
	int i,j;
	int r = 3, c = 4; 
	threadargs *arr = (threadargs *)malloc(r * c * sizeof(threadargs));

	for (i=0;i<r;i++)
		for (j = 0; j < c; j++)
		{
			(*(arr+i*c+j)).m = i;
			(*(arr + i * c + j)).n = j;
		}
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			printf("arr[%i,%i]=%i,%i\n",i,j, (*(arr + i * c + j)).m, (*(arr + i * c + j)).n);
	}


void get_MN(threadargs *blob, int * const &i,  int * const &j) // only used for getting the thread's M,N values
{
	// used by "create_threads" to pass a single pointer to the one element that holds a thread's M,N values
		*i=(*blob).m;
		*j=(*blob).n;
		// above code modifies teh callers variables, because you can't return 2 values
	}

void set_MN(int i, int j) // only used for setting up the grid of thread M,N values
	{// the MN array is linear, so we can simply compute the position of a struct
	 // as rownum+row_width*colnum
		
		//row_width= N;
		thread_arg_grid_ptr[i * row_width_args + j].m = i;
		thread_arg_grid_ptr[i * row_width_args + j].n = j;
	}
float get_power_val(power* arr, int r, int c) // ptr points to the array we want to use

	{
	return (arr [r * row_width_int + c]);
	}
void set_power_val(power (*gridptr)[], int a, int b, float val);
void set_power_val(power (*arr)[], int i, int j, float val) // usable for maxc AND current power arrays
	{
		(*arr)[i * row_width_int + j] = val;
	}