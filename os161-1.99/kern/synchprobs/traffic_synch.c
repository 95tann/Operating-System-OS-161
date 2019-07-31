#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <opt-A1.h>

/*
 * This simple default synchronization mechanism allows only vehicle at a time
 * into the intersection.   The intersectionSem is used as a a lock.
 * We use a semaphore rather than a lock so that this code will work even
 * before locks are implemented.
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
static struct lock* lk_intersection;
static struct cv* cv_intersection;
volatile int cars_in_intersect;
volatile char signal;

typedef struct Car{
    bool going_right;
    Direction destination;
    Direction origin;
} Car;

Car* car_array[10000];

// prototype
bool valid_to_enter(Car* prev, Car* curr);
void shifter(int index);

// implementation
bool valid_to_enter(Car *prev, Car* curr){

  if(prev->origin == curr->origin){
//        kprintf("same origin condition\n");
        return true;
  }

  else if((curr->destination != prev->destination) &&
          (curr->going_right || prev->going_right)){
//        kprintf("atleast 1 car going right\n");
        return true;
  }

  else if((curr->origin == prev->destination) && (curr->destination == prev->origin)){
//        kprintf("cars going opposite direction\n");
        return true;
  }

  else{
        return false;
  }
}

void shifter(int index){

   int new_index = index;
   while(new_index < cars_in_intersect + 1) {
    car_array[new_index] = car_array[new_index+1];
    new_index += 1;
   }

}

/*
 * The simulation driver will call this function once before starting
 * the simulation
 *
 * You can use it to initialize synchronization and other variables.
 *
 */
void
intersection_sync_init(void)
{
  /* replace this default implementation with your own implementation */

  cars_in_intersect = 0;
  cv_intersection = cv_create("my_cv");
  lk_intersection = lock_create("my_lk");
  signal = 'G';

  return;
}

/*
 * The simulation driver will call this function once after
 * the simulation has finished
 *
 * You can use it to clean up any synchronization and other variables.
 *
 */
void
intersection_sync_cleanup(void)
{
  KASSERT(lk_intersection != NULL);
  KASSERT(cv_intersection != NULL);
  /* replace this default implementation with your own implementation */
  cv_destroy(cv_intersection);
  lock_destroy(lk_intersection);
}


/*
 * The simulation driver will call this function each time a vehicle
 * tries to enter the intersection, before it enters.
 * This function should cause the calling simulation thread
 * to block until it is OK for the vehicle to enter the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle is arriving
 *    * destination: the Direction in which the vehicle is trying to go
 *
 * return value: none
 */

void
intersection_before_entry(Direction origin, Direction destination)
{
  /* replace this default implementation with your own implementation */
  KASSERT(cv_intersection != NULL);
  KASSERT(lk_intersection != NULL);

  Car* c = kmalloc(sizeof(struct Car));
  c->destination = destination;
  c->origin = origin;

  c->going_right = false; // default setting
  // check for right turn
  if (c->destination == west && c->origin == north){
    c->going_right = true;
  }
  if (c->destination == east && c->origin == south){
    c->going_right = true;
  }
  if (c->destination == south && c->origin == west){
    c->going_right = true;
  }
  if (c->destination == north && c->origin == east){
    c->going_right = true;
  }
//  kprintf("before acquiring lock\n ----------------- \n");
//  kprintf("new car: orgin = %d, dest = %d\n", c->origin, c->destination);


  lock_acquire(lk_intersection);

  while(true){ 
    signal = 'G';

    int i = 0;
    while (cars_in_intersect >= i+1){
        if (valid_to_enter(car_array[i], c) != true){
    //            kprintf("i = %d\n", i);
    //            kprintf("cars in intersect = %d\n", cars_in_intersect);
    //            kprintf("car array => %d, %d\n", car_array[i]->origin, car_array[i]->destination );
    //            kprintf("cars in intersection, awaiting\n");
            cv_wait(cv_intersection, lk_intersection);
    //            kprintf("done waiting\n");
    //            kprintf("cars in intersect = %d\n", cars_in_intersect);
    //            kprintf("car array => %d, %d\n", car_array[i]->origin, car_array[i]->destination );
            signal = 'R';
            break;
        }
        i = i + 1;
    }
    if (signal != 'R') {
            break;
    }
  }

  car_array[cars_in_intersect] = c;
  cars_in_intersect = cars_in_intersect + 1;
  lock_release(lk_intersection);
}


/*
 * The simulation driver will call this function each time a vehicle
 * leaves the intersection.
 *
 * parameters:
 *    * origin: the Direction from which the vehicle arrived
 *    * destination: the Direction in which the vehicle is going
 *
 * return value: none
 */

void
intersection_after_exit(Direction origin, Direction destination)
{
    KASSERT(cv_intersection != NULL);
    KASSERT(lk_intersection != NULL);

    lock_acquire(lk_intersection);

    int j = 0;
    while(j < cars_in_intersect){
        Car* curr = car_array[j];
        if ((curr->destination == destination) &&
           (curr->origin == origin)){

            kfree(car_array[j]);
            shifter(j);
	        cars_in_intersect = cars_in_intersect - 1;
            cv_broadcast(cv_intersection, lk_intersection);

            break;
        }
//        kprintf("before j = %d\n" , j);
        j = j+1;
//        kprintf("after j = %d\n" , j);

    }
    lock_release(lk_intersection);
}
