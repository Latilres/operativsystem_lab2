/* Tests cetegorical mutual exclusion with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */
#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "lib/random.h" //generate random numbers
#include "devices/timer.h"

#define BUS_CAPACITY 3
#define SENDER 0
#define RECEIVER 1
#define NORMAL 0
#define HIGH 1

/*
 *	initialize task with direction and priority
 *	call o
 * */
typedef struct {
	int direction;
	int priority;
} task_t;

struct lock low_lock;
struct lock high_lock;
struct semaphore waiting_high[2];
struct semaphore waiting_low[2];
struct lock bus_lock;
int bus_users;
int bus_direction;
int bus_waiting_low[2];
int bus_waiting_high[2];

void batchScheduler(unsigned int num_tasks_send, unsigned int num_task_receive,
        unsigned int num_priority_send, unsigned int num_priority_receive);

void senderTask(void *);
void receiverTask(void *);
void senderPriorityTask(void *);
void receiverPriorityTask(void *);
void init_bus(void);

void oneTask(task_t task);/*Task requires to use the bus and executes methods below*/
	void getSlot(task_t task); /* task tries to use slot on the bus */
	void transferData(task_t task); /* task processes data on the bus either sending or receiving based on the direction*/
	void leaveSlot(task_t task); /* task release the slot */



/* initializes semaphores */ 
void init_bus(void){ 
 
    random_init((unsigned int)123456789); 
    
    lock_init(&low_lock);
    lock_init(&high_lock);
    sema_init(&waiting_high[0], 0);
    sema_init(&waiting_high[1], 0);
    sema_init(&waiting_low[0],  0);
    sema_init(&waiting_low[1],  0);
    lock_init(&bus_lock);
    
    bus_waiting_low[SENDER]     = 0;
    bus_waiting_low[RECEIVER]   = 0;
    bus_waiting_high[SENDER]    = 0;
    bus_waiting_high[RECEIVER]  = 0;
    
    bus_users = 0;
    bus_direction = SENDER;
}

/*
 *  Creates a memory bus sub-system  with num_tasks_send + num_priority_send
 *  sending data to the accelerator and num_task_receive + num_priority_receive tasks
 *  reading data/results from the accelerator.
 *
 *  Every task is represented by its own thread. 
 *  Task requires and gets slot on bus system (1)
 *  process data and the bus (2)
 *  Leave the bus (3).
 */

void batchScheduler(unsigned int num_tasks_send, unsigned int num_task_receive,
        unsigned int num_priority_send, unsigned int num_priority_receive)
{
    unsigned int i;
    for (i = 0; i < num_tasks_send; i++) {
        char name[50];
        snprintf(name, 50, "lowsendtask%d", i);
        thread_create(name, PRI_DEFAULT, senderTask, NULL);
    }
    
    for (i = 0; i < num_task_receive; i++) {
        char name[50];
        snprintf(name, 50, "lowrecvtask%d", i);
        thread_create(name, PRI_DEFAULT, receiverTask, NULL);
    }
    
    for (i = 0; i < num_priority_send; i++) {
        char name[50];
        snprintf(name, 50, "highsendtask%d", i);
        thread_create(name, PRI_MAX, senderPriorityTask, NULL);
    }
    
    for (i = 0; i < num_priority_receive; i++) {
        char name[50];
        snprintf(name, 50, "highrecvtask%d", i);
        thread_create(name, PRI_MAX, receiverPriorityTask, NULL);
    }
}

/* Normal task,  sending data to the accelerator */
void senderTask(void *aux UNUSED){
        task_t task = {SENDER, NORMAL};
        oneTask(task);
}

/* High priority task, sending data to the accelerator */
void senderPriorityTask(void *aux UNUSED){
        task_t task = {SENDER, HIGH};
        oneTask(task);
}

/* Normal task, reading data from the accelerator */
void receiverTask(void *aux UNUSED){
        task_t task = {RECEIVER, NORMAL};
        oneTask(task);
}

/* High priority task, reading data from the accelerator */
void receiverPriorityTask(void *aux UNUSED){
        task_t task = {RECEIVER, HIGH};
        oneTask(task);
}

/* abstract task execution*/
void oneTask(task_t task) {
  //printf("Thread: %s tries to acquire slot\n", thread_name());
  getSlot(task);
  //printf("Thread: %s acquired slot\n", thread_name());
  transferData(task);
  leaveSlot(task);
  //printf("Thread: %s released slot\n", thread_name());
}


/* task tries to get slot on the bus subsystem */
void getSlot(task_t task) 
{
    // Step 1: Block if needed
    if (task.priority == NORMAL) {
        // Only one low priority task may acquire a slot at one time
        lock_acquire(&low_lock);
        
        // 
        lock_acquire(&bus_lock);
        
        int has_capacity = bus_users < BUS_CAPACITY;
        int right_direction = bus_direction == task.direction;
        int can_change_direction = bus_users == 0;
        int no_high_waiting = (bus_waiting_high[SENDER] + bus_waiting_high[RECEIVER]) == 0;
        int ready = no_high_waiting && has_capacity && (right_direction || can_change_direction);
        
        if (ready) {
            bus_users++;
            bus_direction = task.direction;
            lock_release(&bus_lock);
        } else {
            bus_waiting_low[task.direction]++;
            lock_release(&bus_lock); // Bus data needs to be free while we block to avoid deadlock
            sema_down(&waiting_low[task.direction]);
        }
        
        lock_release(&low_lock);
    } else if (task.priority == HIGH) {
        // Only one high priority task may acqurie a slot at one time
        lock_acquire(&high_lock);
        
        lock_acquire(&bus_lock);
        
        int has_capacity = bus_users < BUS_CAPACITY;
        int right_direction = bus_direction == task.direction;
        int can_change_direction = bus_users == 0;
        int ready = has_capacity && (right_direction || can_change_direction);
        
        if (ready) {
            bus_users++;
            bus_direction = task.direction;
            lock_release(&bus_lock);
        } else {
            bus_waiting_high[task.direction]++;
            lock_release(&bus_lock); // Bus data needs to be free while we block to avoid deadlock
            sema_down(&waiting_high[task.direction]);
        }
        
        lock_release(&high_lock);
    }
}

/* task processes data on the bus send/receive */
void transferData(task_t task) 
{
    // 0.1 to 5s sleep time
    unsigned long sleep_time = random_ulong() % 200 + 100;
    //printf("Task starts at %ld with priority: %d and direction %d sleeps for %d\n", (long int) timer_ticks(), task.priority, task.direction, 
    //      (unsigned int) sleep_time);
    timer_msleep(sleep_time);
    //printf("Task starts at %ld with priority: %d and direction %d slept for %d\n", (long int) timer_ticks(), task.priority, task.direction, 
    //      (unsigned int) sleep_time);
}

/* task releases the slot */
void leaveSlot(task_t task) 
{
    lock_acquire(&bus_lock);
    
    bus_users--;
    
    int opposite_dir = 1 - task.direction;
    
    if (bus_waiting_high[task.direction] > 0) {
        bus_waiting_high[task.direction]--;
        sema_up(&waiting_high[task.direction]);
    } else if (bus_waiting_high[opposite_dir] > 0 && bus_users == 0) {
        bus_waiting_high[opposite_dir]--;
        bus_direction = opposite_dir;
        sema_up(&waiting_high[opposite_dir]);
    } else if (bus_waiting_low[task.direction] > 0 && bus_waiting_high[opposite_dir] == 0) {
        bus_waiting_low[task.direction]--;
        sema_up(&waiting_low[task.direction]);    
    } else if (bus_waiting_low[opposite_dir] > 0 && bus_users == 0) {
        bus_waiting_low[opposite_dir]--;
        bus_direction = opposite_dir;
        sema_up(&waiting_low[opposite_dir]);    
    }
    
    lock_release(&bus_lock);
}
