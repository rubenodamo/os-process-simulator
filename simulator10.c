#include <sys/types.h> 
#include <unistd.h> 
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>

#include "linkedlist.h"
#include "os_process.h"

#define LAST_PROCESSES_NUM 20

// function declaration
void * generator();
void * simulator();
void * terminator();
void * booster();
void * ioSimulator();
void * loadBalancer();

int getProcessesNum(int cpuNum);

float getRollingResponseTime(int cpuNum);
float getRollingTurnAroundTime(int cpuNum);

int getSize(LinkedList *pList); 

void printsf(const char *format, ...);


// semaphores to control the flow of threads
sem_t generatorSemaphore, simulatorSemaphore, terminatorSemaphore;

// linked lists for the ready, terminated, I/O queues 
// (ready = array of linked lists for each priority; I/O = array of linked lists for each device)
LinkedList readyQueue[NUMBER_OF_PRIORITY_LEVELS][NUMBER_OF_QUEUE_SETS];
LinkedList terminatedQueue = LINKED_LIST_INITIALIZER;
LinkedList ioQueue[NUMBER_OF_IO_DEVICES];

// counters and statistics variables
int terminatedProcessCount = 0;
int indexPid = 0;
float totalResponseTime = 0.0;
float totalturnAroundTime = 0.0;

int pidArray[SIZE_OF_PROCESS_TABLE]; // array of pids
int terminatedProcessArray[NUMBER_OF_CPUS]; // number of terminated processes for each CPU
float responseTimes[LAST_PROCESSES_NUM][NUMBER_OF_CPUS]; // response times for each CPU
float turnAroundTimes[LAST_PROCESSES_NUM][NUMBER_OF_CPUS]; // turn around times for each CPU

// process table
Process * processTbl[SIZE_OF_PROCESS_TABLE];

// mutexes for thread synchronisation
pthread_mutex_t printLock, rQueueLock, tQueueLock, arrayLock, tblLock, ioQueueLock, counterLock, rTimeLock;

int main(int argc, char **argv) {
    // thread handles
    pthread_t generatorThread, terminatorThread, boosterThread, ioThread, loadBThread;
    pthread_t simulatorThreads[NUMBER_OF_CPUS];
    
    int cpuArray[NUMBER_OF_CPUS];

    // initialise each linked list and add it to the ready queue array
    for(int n = 0; n < NUMBER_OF_CPUS; n++) {
        for(int i = 0; i < NUMBER_OF_PRIORITY_LEVELS; i++) {
            LinkedList list = LINKED_LIST_INITIALIZER;
            readyQueue[i][n] = list;
        }

        terminatedProcessArray[n] = 0;
    }

    // initialise each linked list and add it to the io queue array
    for(int i = 0; i < NUMBER_OF_IO_DEVICES; i++) {
        LinkedList list = LINKED_LIST_INITIALIZER;
        ioQueue[i] = list;
    }

    // initialise mutexes - pthread_mutex_init(pthread_mutex_t *mutex, attribute NONRECURSIVE);
    pthread_mutex_init(&printLock, NULL);
    pthread_mutex_init(&rQueueLock, NULL);
    pthread_mutex_init(&tQueueLock, NULL);
    pthread_mutex_init(&arrayLock, NULL);
    pthread_mutex_init(&tblLock, NULL);
    pthread_mutex_init(&ioQueueLock, NULL);
    pthread_mutex_init(&counterLock, NULL);
    pthread_mutex_init(&rTimeLock, NULL);

    // initialise the "pool" of PIDs
    for(int i = 0; i < SIZE_OF_PROCESS_TABLE; i++) {
        pidArray[i] = i;
    }

    // initialise semaphores - sem_init(sem_t *sem, int pshared, unsigned int value);
    sem_init(&terminatorSemaphore, 0, 0);
    sem_init(&simulatorSemaphore, 0, 0);
    sem_init(&generatorSemaphore, 0, MAX_CONCURRENT_PROCESSES);

    // create threads - pthread_create(thread_id, attributes, function, function arg)
    pthread_create(&generatorThread, NULL, generator, NULL);
    for(int i = 0; i < NUMBER_OF_CPUS; i++) {
        cpuArray[i] = i;
        pthread_create(&simulatorThreads[i], NULL, simulator, (void *)&cpuArray[i]);
    }
    pthread_create(&terminatorThread, NULL, terminator, NULL);
    pthread_create(&boosterThread, NULL, booster, NULL);
    pthread_create(&ioThread, NULL, ioSimulator, NULL);
    pthread_create(&loadBThread, NULL, loadBalancer, NULL);
    
    // waits for the thread to finish - pthread_join(thread_id, exit status)
    pthread_join(generatorThread, NULL);
    for(int i = 0; i < NUMBER_OF_CPUS; i++) {
        pthread_join(simulatorThreads[i], NULL);
    }
    pthread_join(terminatorThread, NULL);
    pthread_join(loadBThread, NULL);
    pthread_join(ioThread, NULL);
    pthread_join(boosterThread, NULL);

    // destroys mutexes
    pthread_mutex_destroy(&printLock);
    pthread_mutex_destroy(&rQueueLock);
    pthread_mutex_destroy(&tQueueLock);
    pthread_mutex_destroy(&arrayLock);
    pthread_mutex_destroy(&tblLock);
    pthread_mutex_destroy(&ioQueueLock);
    pthread_mutex_destroy(&counterLock);
    pthread_mutex_destroy(&rTimeLock);

    // destroys semaphores
    sem_destroy(&generatorSemaphore);
    sem_destroy(&simulatorSemaphore);
    sem_destroy(&terminatorSemaphore);

    return 0;
}

/* 
 * Generator function: Adds processes to the ready queue, goes to sleep when there are
 * MAX_CONCURRENT_PROCESSES in the system, and is woken up as soon as a new process can
 * be added to the system.
 */
void * generator() {
    pid_t pid = 0;
    
    // loop for creating processes
    for(int i = 0; i < NUMBER_OF_PROCESSES; i++) {
        // wait for the generator semaphore (decrements)
        sem_wait(&generatorSemaphore);
        
        // if indexPid is within the pid pool
        if(indexPid < SIZE_OF_PROCESS_TABLE) {
            int processesNum = 0;

            // gets a random number between 0 and NUMBER_OF_CPUS
            int randProcessor = rand() % NUMBER_OF_CPUS;

            // lock the pidArray
            pthread_mutex_lock(&arrayLock);
            pid = pidArray[indexPid];
            indexPid++;
            // unlock the pidArray
            pthread_mutex_unlock(&arrayLock);

            // generate a process
            Process *p = generateProcess(pid);

            // lock the process table
            pthread_mutex_lock(&tblLock);
            // add process to the table
            processTbl[pid] = p;
            // unlock the process table
            pthread_mutex_unlock(&tblLock);

            // lock the ready queue 
            pthread_mutex_lock(&rQueueLock);
            printsf("GENERATOR - CREATED: [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n", 
                p->iPID, p->iPriority, p->iBurstTime, p->iRemainingBurstTime);
            printsf("GENERATOR - ADDED TO TABLE: [PID = %d, Priority = %d, Initial BurstTime = %d, Remaining BurstTime = %d]\n", 
                p->iPID, p->iPriority, p->iBurstTime, p->iRemainingBurstTime);

            // add the process to the ready queue
            addLast(p, &readyQueue[p->iPriority][randProcessor]);
            printsf("QUEUE - ADDED: [Queue = SET %d, READY %d, Size = %d, PID = %d, Priority = %d]\n",
                randProcessor, p->iPriority, getSize(&readyQueue[p->iPriority][randProcessor]), p->iPID, p->iPriority);

            // for loop to loop through number of CPUs
            for(int n = 0; n < NUMBER_OF_CPUS; n++) {
                // counts the total number of processes in the ready queues 
                for(int j = 0; j < NUMBER_OF_PRIORITY_LEVELS; j++) {
                    processesNum += getSize(&readyQueue[j][n]);
                }
            }

            // if the ready queue is full, wake up the simulator
            if(processesNum == MAX_CONCURRENT_PROCESSES) {
                for(int j = 0; j < NUMBER_OF_CPUS; j++) {
                    // post to the simulator semaphore (increments)
                    sem_post(&simulatorSemaphore);
                }
            }

            printsf("GENERATOR - ADMITTED: [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n",
                p->iPID, p->iPriority, p->iBurstTime, p->iRemainingBurstTime); 

            // unlock the ready queue
            pthread_mutex_unlock(&rQueueLock);
        }
        // else index is not in the pid pool
        else {
            i--; // decrements i
            usleep(10000); // sleep
        }
    }

    printsf("GENERATOR: Finished\n");
}


/*
 * Simulator function: Removes processes from the ready queues and runs them in a round robin fashion
 * using the runPreemptiveProcess() function.
 */
void * simulator(void *num) {
    int cpuNum = *((int *) num);

    // wait for the generator to signal (decrements)
    sem_wait(&simulatorSemaphore);

    // simulate the execution of processes in the readyQueue
    while(1) {
        Process *cp;

        // if the ready queue is empty...
        if (getProcessesNum(cpuNum) == 0) {
            // lock terminated queue
            pthread_mutex_lock(&counterLock);
            // check if all processes are terminated
            if(terminatedProcessCount == NUMBER_OF_PROCESSES) {
                // unlocks queues (ready and terminated)
                pthread_mutex_unlock(&counterLock);
                break;
            }
            // unlocks queues (ready and terminated)
            pthread_mutex_unlock(&counterLock);
            continue;
        }
        // else ready queue is populated
        else {
            // for loop to remove the first non empty queue
            for(int i = 0; i < NUMBER_OF_PRIORITY_LEVELS; i++) {
                // lock the ready queue
                pthread_mutex_lock(&rQueueLock);
                if(getSize(&readyQueue[i][cpuNum]) > 0) {
                    // remove the current process from the ready queue
                    cp = removeFirst(&readyQueue[i][cpuNum]);
                    printsf("QUEUE - REMOVED: [Queue = SET %d, READY %d, Size = %d, PID = %d, Priority = %d]\n",
                        cpuNum, i, getSize(&readyQueue[i][cpuNum]), cp->iPID, cp->iPriority);
                    // unlock the ready queue
                    pthread_mutex_unlock(&rQueueLock);
                    break;
                }
                // unlock the ready queue
                pthread_mutex_unlock(&rQueueLock);
            }

            // if the current process is a higher priority...
            if(cp->iPriority  < (NUMBER_OF_PRIORITY_LEVELS/2)) {
                // run the process in FCFS fashion
                runNonPreemptiveProcess(cp, true);
                printsf("SIMULATOR - CPU %d: FCFS [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n",
                    cpuNum, cp->iPID, cp->iPriority, cp->iBurstTime, cp->iRemainingBurstTime);
            }
            // else the current process is a lower priority...
            else {
                // run the process in RR fashion
                runPreemptiveProcess(cp, true);
                printsf("SIMULATOR - CPU %d: RR [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n",
                    cpuNum, cp->iPID, cp->iPriority, cp->iBurstTime, cp->iRemainingBurstTime); 
            }
            
            // if the process is blocked, add it to the I/O queue
            if (cp->iState == BLOCKED) {
                printsf("SIMULATOR - CPU %d - I/O BLOCKED: [PID = %d, Priority = %d, Device = %d]\n",
                    cpuNum, cp->iPID, cp->iPriority, cp->iDeviceID);

                // lock the io queue 
                pthread_mutex_lock(&ioQueueLock);

                // add the processes to the io queue
                addLast(cp, &ioQueue[cp->iDeviceID]);
                printsf("QUEUE - ADDED: [Queue = I/O %d, Size = %d, PID = %d, Priority = %d]\n", 
                    cp->iDeviceID, getSize(&ioQueue[cp->iDeviceID]), cp->iPID, cp->iPriority);

                // unlock the io queue
                pthread_mutex_unlock(&ioQueueLock);
            
            } 
            // if the process is ready, add it back to the readyQueue
            else if (cp->iState == READY) {
                // lock the ready queue 
                pthread_mutex_lock(&rQueueLock);

                // add the process back to the ready queue
                addLast(cp, &readyQueue[cp->iPriority][cpuNum]);
                printsf("QUEUE - ADDED: [Queue = SET %d, READY %d, Size = %d, PID = %d, Priority = %d]\n",
                    cpuNum, cp->iPriority, getSize(&readyQueue[cp->iPriority][cpuNum]), cp->iPID, cp->iPriority);

                printsf("SIMULATOR - CPU %d - READY: [PID = %d, Priority = %d]\n", cpuNum, cp->iPID, cp->iPriority);

                // unlock the ready queue
                pthread_mutex_unlock(&rQueueLock);
            
            } 
            // if the process is terminated, print information and add it to the terminatedQueue
            else if (cp->iState == TERMINATED) {
                // lock the counter
                pthread_mutex_lock(&counterLock);
                // increment the terminated processes variables
                terminatedProcessCount++;
                terminatedProcessArray[cpuNum]++; 
                // unlock the counter
                pthread_mutex_unlock(&counterLock);

                printsf("SIMULATOR - TERMINATED: [PID = %d, ResponseTime = %d, TurnAroundTime = %d]\n",
                    cp->iPID, getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oFirstTimeRunning), 
                    getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oLastTimeRunning));

                // lock the response time
                pthread_mutex_lock(&rTimeLock);

                // update response and turnaround time statistics
                totalResponseTime += getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oFirstTimeRunning);
                totalturnAroundTime += getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oLastTimeRunning);

                // for loop to remove the last time from the array, and shift each value to the right
                for(int i = LAST_PROCESSES_NUM - 1; i >= 0; i--) {
                    responseTimes[i][cpuNum] = responseTimes[i-1][cpuNum];
                    turnAroundTimes[i][cpuNum] = turnAroundTimes[i-1][cpuNum];
                }
                // update response and turnaround time array for the current process (put at index 0)
                responseTimes[0][cpuNum] = getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oFirstTimeRunning);
                turnAroundTimes[0][cpuNum] = getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oFirstTimeRunning);

                printsf("SIMULATOR - CPU %d: rolling average response time = %f, rolling average turnaround time = %f\n", 
                            cpuNum, getRollingResponseTime(cpuNum), getRollingTurnAroundTime(cpuNum));
                // unlock the response time
                pthread_mutex_unlock(&rTimeLock);

                // lock the terminated queue
                pthread_mutex_lock(&tQueueLock);
                // add the process to terminated queue
                addLast(cp, &terminatedQueue);
                printsf("QUEUE - ADDED: [Queue = TERMINATED, Size = %d, PID = %d, Priority = %d]\n",
                    getSize(&terminatedQueue), cp->iPID, cp->iPriority);

                // lock the terminated queue
                pthread_mutex_unlock(&tQueueLock);

                // signal the terminator
                sem_post(&terminatorSemaphore);
            }
        }
    }

    while(1) {
        // lock the counter
        pthread_mutex_lock(&counterLock);
        // check if all processes are terminated
        if (terminatedProcessCount == NUMBER_OF_PROCESSES) {
            // unlock the counter
            pthread_mutex_unlock(&counterLock);
            break;
        }
        // unlock the counter
        pthread_mutex_unlock(&counterLock);
    }

    // sleep
    usleep(1000);
    printsf("SIMULATOR: Finished\n");
}

/*
 * Terminator function: Woken up when a process is returned in the TERMINATED state and
 * so is added to the terminated queue.
 */
void * terminator() {
    float avResponseTime, avTurnAroundTime;
    int iTerminated;
    
    // lock the terminated queue
    pthread_mutex_lock(&tQueueLock);
    // sets iTerminated value
    iTerminated = getSize(&terminatedQueue);
    // unlock the terminated queue
    pthread_mutex_unlock(&tQueueLock);

    while(1) {
        Process *dp;

        // wait for the simulator to signal (decrements)
        sem_wait(&terminatorSemaphore);

        // lock the terminated queue
        pthread_mutex_lock(&tQueueLock);

        // remove the process from the terminated queue
        dp =  removeFirst(&terminatedQueue);

        printsf("QUEUE - REMOVED: [Queue = TERMINATED, Size = %d, PID = %d, Priority = %d]\n",
            getSize(&terminatedQueue),dp->iPID, dp->iPriority);
        printsf("TERMINATION DAEMON - CLEARED: [#iTerminated = %d, PID = %d, Priority = %d]\n",
            ++iTerminated, dp->iPID, dp->iPriority);

        // unlock the terminated queue
        pthread_mutex_unlock(&tQueueLock);

        // lock the process table
        pthread_mutex_lock(&tblLock);
        // removes the process from the process table
        processTbl[dp->iPID] = NULL;
        // unlock the process table
        pthread_mutex_unlock(&tblLock);

        // lock the pid array
        pthread_mutex_lock(&arrayLock);
        // add the PID back to the pool
        pidArray[--indexPid] = dp->iPID;
        // unlock the pid array
        pthread_mutex_unlock(&arrayLock);

        // signal the generator
        sem_post(&generatorSemaphore);

        // if dp exists...
        if(dp) {
            // destroy the process
            destroyProcess(dp);
        }
        
        // lock the counter 
        pthread_mutex_lock(&counterLock);
        // check if all processes are terminated
        if (terminatedProcessCount == NUMBER_OF_PROCESSES) {
            // unlock the counter
            pthread_mutex_unlock(&counterLock);
            break;
        }
        // unlock the counter
        pthread_mutex_unlock(&counterLock);
    }

    // lock the response time
    pthread_mutex_lock(&rTimeLock);

    // calculate the average response and turnaround time
    avResponseTime = totalResponseTime/NUMBER_OF_PROCESSES;
    avTurnAroundTime = totalturnAroundTime/NUMBER_OF_PROCESSES;

    // lock the response time
    pthread_mutex_unlock(&rTimeLock);

    printsf("TERMINATION DAEMON: Finished\n");
    printsf("TERMINATION DAEMON: [Average Response Time = %lf, Average Turn Around Time = %lf]\n",
        avResponseTime, avTurnAroundTime);
    
}

/*
 * Booster function: Function to boost priority levels of selected processes in the ready queue
 * The booster increases the priority of RR jobs periodically to the highest RR level
 */
void * booster() {
    
    // sleep
    usleep(BOOST_INTERVAL);

    printsf("BOOSTER DAEMON: Created\n");
    while (1) {
        // for loop to loop through number of CPUs 
        for(int n = 0; n < NUMBER_OF_CPUS; n++) {
            // for loop to loop through the priorities starting from mid point + 1
            for(int i = (NUMBER_OF_PRIORITY_LEVELS/2)+1; i < NUMBER_OF_PRIORITY_LEVELS; i++) {
                // lock the ready queue
                pthread_mutex_lock(&rQueueLock);
                // if there are processes in the current priority level
                if (getSize(&readyQueue[i][n]) > 0) {
                    // remove the first non empty queue
                    Process *bp = removeFirst(&readyQueue[i][n]);
                    
                    printsf("QUEUE - REMOVED: [Queue = SET %d, READY %d, Size = %d, PID = %d, Priority = %d]\n", 
                                n, i, getSize(&readyQueue[i][n]), bp->iPID, bp->iPriority);

                    printsf("BOOSTER DAEMON: [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d] => Boosted to Level %d\n",
                                bp->iPID, bp->iPriority, bp->iBurstTime, bp->iRemainingBurstTime, NUMBER_OF_PRIORITY_LEVELS/2);

                    // add the boosted process to the mid-point priority level
                    addLast(bp, &readyQueue[NUMBER_OF_PRIORITY_LEVELS/2][n]);
                    printsf("QUEUE - ADDED: [Queue = SET %d, READY %d, Size = %d, PID = %d, Priority = %d]\n", 
                                n, NUMBER_OF_PRIORITY_LEVELS/2, getSize(&readyQueue[NUMBER_OF_PRIORITY_LEVELS/2][n]), bp->iPID, bp->iPriority);
                }
                // unlock ready queue
                pthread_mutex_unlock(&rQueueLock);
            }
        }

        
        // lock the terminator queue
        pthread_mutex_lock(&counterLock);
        // check if all processes are terminated
        if (terminatedProcessCount == NUMBER_OF_PROCESSES) {
            // unlock the terminator queue
            pthread_mutex_unlock(&counterLock);
            break;
        }
        // unlock the terminator queue
        pthread_mutex_unlock(&counterLock);

        // sleeps
        usleep(BOOST_INTERVAL*1000);
    }

    printsf("BOOSTER DAEMON: Finished\n");
}

/* 
 * IO function: This function simulates the I/O operations by periodically checking the I/O queues for blocked processes.
 * If there are processes in the I/O queues, it unblocks them and moves them back to the ready queue for further
 * processing by the simulator. The function runs in a loop until all processes are terminated.
 */
void * ioSimulator() {
    // sleep
    usleep(IO_DAEMON_INTERVAL);

    while(1) {
        // lock the io queue
        pthread_mutex_lock(&ioQueueLock);

        // for loop to loop through number of CPUs 
        for(int n = 0; n < NUMBER_OF_CPUS; n++) {
            // for loop to loop through the number of devices
            for(int i = 0; i < NUMBER_OF_IO_DEVICES; i++) {    
                // while loop to run through every blocked process and unblock it
                while(getSize(&ioQueue[i]) > 0) {
                    // removes blocked process (unblock) from io queue
                    Process *p = removeFirst(&ioQueue[i]);
                    printsf("I/O DAEMON - UNBLOCKED: [PID = %d, Priority = %d]\n", 
                                p->iPID, p->iPriority);
        
                    // locks ready queue
                    pthread_mutex_lock(&rQueueLock);

                    // adds process to ready queue
                    addFirst(p, &readyQueue[p->iPriority][n]);
                    printsf("QUEUE - ADDED: [Queue = SET %d, READY %d, Size = %d, PID = %d, Priority = %d]\n", 
                                n, p->iPriority, getSize(&readyQueue[p->iPriority][n]), p->iPID, p->iPriority);

                    // unlocks ready queue
                    pthread_mutex_unlock(&rQueueLock);
                }
            }
        }

        // unlocks the io queues
        pthread_mutex_unlock(&ioQueueLock);

        // locks the counter
        pthread_mutex_lock(&counterLock);
        // check if all processes are terminated
        if (terminatedProcessCount == NUMBER_OF_PROCESSES) {
            // unlock the terminator queue
            pthread_mutex_unlock(&counterLock);
            break;
        }
        // unlock the counter
        pthread_mutex_unlock(&counterLock);

        // sleeps
        usleep(IO_DAEMON_INTERVAL*1000);
    }

    printsf("I/O DAEMON: Finished\n");
}

/* 
 * loadBalancer function: this function is responsible for redistributing processes among CPUs
 * based on their rolling response time, aiming to balance the load across the system.
 * If the load (response time) is unbalanced, the load balancer removes a random process form busiest 
 * CPU and adds it to the queues for the least busy CPU.
 */
void *loadBalancer() {
    // sleeps for a specified load balancing interval
    usleep(LOAD_BALANCING_INTERVAL);

    while(1) {
        int busiestCpu = 0;
        int leastBusiestCpu = 0;

        Process * hp; // highest priority process 
        int index;

        // for loop to iterate each CPU to find the busiest and least busy CPUs
        for(int i = 0; i < NUMBER_OF_CPUS; i++) {
            // lock the response time
            pthread_mutex_lock(&rTimeLock);
            if (getRollingResponseTime(i) > getRollingResponseTime(busiestCpu)) {
                busiestCpu = i;
            }
            
            if (getRollingResponseTime(i) < getRollingResponseTime(leastBusiestCpu)) {
                leastBusiestCpu = i;
            }
            // unlock the response time
            pthread_mutex_unlock(&rTimeLock);
        }

        // for loop to find the least busy CPU with no terminated processes
        for(int i = 0; i < NUMBER_OF_CPUS; i++) {
            // lock the counter
            pthread_mutex_lock(&counterLock);
            if (terminatedProcessArray[i] == 0 ) {
                leastBusiestCpu = i;
                // unlock the counter
                pthread_mutex_unlock(&counterLock);
                break;
            }
            // unlock the counter
            pthread_mutex_unlock(&counterLock);
        }

        // lock the counter
        pthread_mutex_lock(&counterLock);
        // checks if there are terminated processes
        if (terminatedProcessCount == 0) {
            // unlock the counter
            pthread_mutex_unlock(&counterLock);
            continue;
        }
        // unlock the counter
        pthread_mutex_unlock(&counterLock);

        // lock the response time
        pthread_mutex_lock(&rTimeLock);
        // checks if the busiest and least busy CPUs have the same response time
        if (getRollingResponseTime(busiestCpu) == getRollingResponseTime(leastBusiestCpu)) {
            // unlock the response time
            pthread_mutex_unlock(&rTimeLock);
            continue;
        }
        // unlock the response time
        pthread_mutex_unlock(&rTimeLock);
        
        // checks if the busiest CPU has processes in its ready queue
        if (getProcessesNum(busiestCpu) == 0) {
            // lock the counter
            pthread_mutex_lock(&counterLock);
            // check if all processes are terminated 
            if (terminatedProcessCount == NUMBER_OF_PROCESSES) {
                // unlock the counter 
                pthread_mutex_unlock(&counterLock);
                break;
            }
            // unlock the counter 
            pthread_mutex_unlock(&counterLock);
            continue;
        }
        else {
            // for loop to find the highest priority process in the busiest CPU's ready queue
            for(int i = 0; i < NUMBER_OF_PRIORITY_LEVELS; i++) {
                // lock the ready queue
                pthread_mutex_lock(&rQueueLock);
                if (getSize(&readyQueue[i][busiestCpu]) > 0) {
                    hp = removeFirst(&readyQueue[i][busiestCpu]);
                    // unlock the ready queue
                    pthread_mutex_unlock(&rQueueLock);
                    index = i;
                    break;
                }
                // unlock the ready queue
                pthread_mutex_unlock(&rQueueLock);
            }

            // lock the ready queue
            pthread_mutex_lock(&rQueueLock);
            // add the highest priority process to the least busy CPU's ready queue
            addLast(hp, &readyQueue[index][leastBusiestCpu]);
            // lock the ready queue
            pthread_mutex_unlock(&rQueueLock);
            
            // lock the repsonse time
            pthread_mutex_lock(&rTimeLock);
            printsf("LOAD BALANCER: Moving [PID = %d, Priority = %d] from CPU %d (response time = %f) to CPU %d (response time = %f)\n", 
                        hp->iPID, hp->iPriority, busiestCpu, getRollingResponseTime(busiestCpu), leastBusiestCpu, getRollingResponseTime(leastBusiestCpu));
            // lock the response time
            pthread_mutex_unlock(&rTimeLock);
        }
        
        // sleeps
        usleep(LOAD_BALANCING_INTERVAL*1000);
    }

    while(1) {
        // sleeps
        usleep(LOAD_BALANCING_INTERVAL);
        // locks the counter
        pthread_mutex_lock(&counterLock);
        // check if all processes are terminated
        if (terminatedProcessCount == NUMBER_OF_PROCESSES) {
            // unlocks the counter
            pthread_mutex_unlock(&counterLock);
            break;
        }
        // unlocks the counter
        pthread_mutex_unlock(&counterLock); 

        // sleeps
        usleep(LOAD_BALANCING_INTERVAL*1000);
    } 

    printsf("LOAD BALANCER: Finished\n"); 
}

// function to calculate the rolling average response time for a specified CPU
float getRollingResponseTime(int cpuNum) {
    float rResponseTime;
    // lock the counter
    pthread_mutex_lock(&counterLock);

    // check if the number of terminated processes is less than LAST_PROCESSES_NUM
    if (terminatedProcessArray[cpuNum] < LAST_PROCESSES_NUM) {
        float responseSum = 0.0;

        // sum up response times of the last terminated processes on the CPU
        for(int i = 0; i < terminatedProcessArray[cpuNum]; i++) {
            responseSum += responseTimes[i][cpuNum];
        }

        // calculate the rolling processor response and turnaround time statistics
        rResponseTime = responseSum / terminatedProcessArray[cpuNum];
    }
    else {
        float responseSum = 0.0;

        // sum up response times of the last processes (up to LAST_PROCESSES_NUM) on the CPU
        for(int i = 0; i < LAST_PROCESSES_NUM; i++) {
            responseSum += responseTimes[i][cpuNum];
        }

        // calculate the rolling processor response and turnaround time statistics
        rResponseTime = responseSum / LAST_PROCESSES_NUM;
    }
    // unlock the counter
    pthread_mutex_unlock(&counterLock);

    return rResponseTime;
}

// function to calculate the rolling average turn around time for a specified CPU
float getRollingTurnAroundTime(int cpuNum) {
    float rTurnAroundTime;
    // lock the counter
    pthread_mutex_lock(&counterLock);

    // check if the number of terminated processes is less than LAST_PROCESSES_NUM
    if (terminatedProcessArray[cpuNum] < LAST_PROCESSES_NUM) {
        float turnAroundSum = 0.0;
        
        // sum up turnaround times of the last terminated processes on the CPU
        for(int i = 0; i < terminatedProcessArray[cpuNum]; i++) {
            turnAroundSum += turnAroundTimes[i][cpuNum];
        }

        // calculate the rolling processor response and turnaround time statistics
        rTurnAroundTime = turnAroundSum / terminatedProcessArray[cpuNum];
    }
    else {
        float turnAroundSum = 0.0;

        // sum up turnaround times of the last processes (up to LAST_PROCESSES_NUM) on the CPU
        for(int i = 0; i < LAST_PROCESSES_NUM; i++) {
            turnAroundSum += turnAroundTimes[i][cpuNum];
        }

        // calculate the rolling processor response and turnaround time statistics
        rTurnAroundTime = turnAroundSum / LAST_PROCESSES_NUM;
    }
    // unlock the counter
    pthread_mutex_unlock(&counterLock);

    return rTurnAroundTime;
}

// function to get the number of processors for one cpu
int getProcessesNum(int cpuNum) {
    int processesNum = 0;
    // for loop to count the number of processes in the ready queue
    for(int i = 0; i < NUMBER_OF_PRIORITY_LEVELS; i++) {
        // lock the ready queue
        pthread_mutex_lock(&rQueueLock);

        processesNum += getSize(&readyQueue[i][cpuNum]);

        // unlock the ready queue
        pthread_mutex_unlock(&rQueueLock);
    }

    return processesNum;
}

// function to print with thread-safe formatting
void printsf(const char *format, ...) {
    pthread_mutex_lock(&printLock);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    pthread_mutex_unlock(&printLock);
}

// function to get the size of a linked list
int getSize(LinkedList *pList) {
    int size = 0;
    Element *current = pList->pHead; // Start at the head of the list

    // traverse the list
    while (current != NULL) {
        size++; // Increment the size for each element
        current = current->pNext; // Move to the next element
    }

    return size;
}
