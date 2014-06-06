#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>

/* 
 * This simple default synchronization mechanism allows only creature at a time to
 * eat.   The globalCatMouseSem is used as a a lock.   We use a semaphore
 * rather than a lock so that this code will work even before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */

struct semaphore *cat_sem;
struct semaphore *mouse_sem;
struct cv *catmouse_cv;
struct lock *mutex;
volatile char *sbowls;
volatile int numcatseat;
volatile int nummiceeat;
/* 
 * The CatMouse simulation will call this function once before any cat or
 * mouse tries to each.
 *
 * You can use it to initialize synchronization and other variables.
 * 
 * parameters: the number of bowls
 */
void
catmouse_sync_init(int bowls)
{

	/**********  ABSTRACT **********  
	 * two semaphores to lock critical "after_eating" sections
	 * cat_sem: for cat_after_eating; mouse_sem: for mouse_after eating
	 * catmouse_cv is used to block both cats and mouse if bowl is used
	 * sbowls is a char array; marks bowl if being used
	 * numcatseat, nummiceeat is a counter for the number of cat
	 *    and mice currently eating
	 */

	cat_sem = sem_create("cat_sem", 1);
	if(cat_sem == NULL) {
		panic("could not create cat_sem semaphore");
	}
	mouse_sem = sem_create("mouse_sem", 1);
	if(mouse_sem == NULL) {
		panic("could not create mouse_sem semaphore");
	}
	catmouse_cv = cv_create("catmouse_cv");
	KASSERT(catmouse_cv != NULL);
	mutex = lock_create("mutex");
	KASSERT(mutex != NULL);
	sbowls = kmalloc(bowls*sizeof(char));
	KASSERT(sbowls != NULL);
	for(int i = 0; i < bowls; i++) {
		sbowls[i] = '-';
	}
	numcatseat = nummiceeat = 0;
	return;
}



/* 
 * The CatMouse simulation will call this function once after all cat
 * and mouse simulations are finished.
 *
 * You can use it to clean up any synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_cleanup(int bowls)
{
	(void)bowls;
	KASSERT(cat_sem != NULL);
	sem_destroy(cat_sem);
	KASSERT(mouse_sem != NULL);
	sem_destroy(mouse_sem);
	KASSERT(catmouse_cv != NULL);
	cv_destroy(catmouse_cv);
	KASSERT(mutex != NULL);
	lock_destroy(mutex);
	KASSERT(sbowls != NULL);
	kfree( (void *) sbowls);
}


/*
 * The CatMouse simulation will call this function each time a cat wants
 * to eat, before it eats.
 * This function should cause the calling thread (a cat simulation thread)
 * to block until it is OK for a cat to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the cat is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_before_eating(unsigned int bowl) 
{
	// cat thread blocks if there are mice eating or 
	// if the bowl is currently in use
	// numcatseat increments if above conditions are not true;
	// assigned bowl is free and i assume the cat will
	//		 have the opportunity to eat

	lock_acquire(mutex);
	while(nummiceeat > 0 || sbowls[bowl - 1] != '-') {
		cv_wait(catmouse_cv, mutex);
	}
	sbowls[bowl - 1] = 'c';
	numcatseat++;
	lock_release(mutex);
}

/*
 * The CatMouse simulation will call this function each time a cat finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this cat finished.
 *
 * parameter: the number of the bowl at which the cat is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_after_eating(unsigned int bowl) 
{
	/*whole function is treated as a critical section with
	 *implementation of semaphore from the start
	 *bowl is marked as free of use, numcatseat is decremented
	 */
	P(cat_sem);
	sbowls[bowl - 1] = '-';
	numcatseat--;
	cv_broadcast(catmouse_cv, mutex);
	V(cat_sem);
}

/*
 * The CatMouse simulation will call this function each time a mouse wants
 * to eat, before it eats.
 * This function should cause the calling thread (a mouse simulation thread)
 * to block until it is OK for a mouse to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the mouse is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_before_eating(unsigned int bowl) 
{
	// abstraction is the same as cat_before_eating except for mice
	lock_acquire(mutex);
	while(numcatseat > 0 || sbowls[bowl - 1] != '-') {
		cv_wait(catmouse_cv, mutex);
	}
	sbowls[bowl - 1] = 'm';
	nummiceeat++;
	lock_release(mutex);
}

/*
 * The CatMouse simulation will call this function each time a mouse finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this mouse finished.
 *
 * parameter: the number of the bowl at which the mouse is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_after_eating(unsigned int bowl) 
{
	// abstraction is the same as cat_after_eating except for mice
	P(mouse_sem);
	sbowls[bowl - 1] = '-';
	nummiceeat--;
	cv_broadcast(catmouse_cv, mutex);
	V(mouse_sem);
}
