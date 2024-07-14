#include <sys/types.h> 
#include <unistd.h> 
#include <stdio.h> 
#include <stdlib.h>

#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>

#include "linkedlist.h"
#include "os_process.h"

// function declaration
void * generator();
void * simulator();
void * terminator();
void * booster();
void * ioSimulator();

int getSize(LinkedList *pList); 

void printsf(const char *format, ...);

// semaphores to control the flow of threads
sem_t generatorSemaphore, simulatorSemaphore, terminatorSemaphore;

// linked lists for the ready, terminated, I/O queues 
// (ready = array of linked lists for each priority; I/O = array of linked lists for each device)
LinkedList readyQueue[NUMBER_OF_PRIORITY_LEVELS];
LinkedList terminatedQueue = LINKED_LIST_INITIALIZER;
LinkedList ioQueue[NUMBER_OF_IO_DEVICES];

// counters and statistics variables
int terminatedProcessCount = 0;
int iTerminated = 0;
float totalResponseTime = 0;
float totalturnAroundTime = 0;
float avResponseTime = 0.0;
float avTurnAroundTime = 0.0;

// mutexes for thread synchronisation
pthread_mutex_t printLock, rQueueLock, tQueueLock, arrayLock, tblLock, ioQueueLock;

int pidArray[SIZE_OF_PROCESS_TABLE];
Process * processTbl[SIZE_OF_PROCESS_TABLE];
int indexPid = 0;

int main() {
    // thread handles
    pthread_t generatorThread, terminatorThread, boosterThread, ioThread;
    pthread_t simulatorThreads[NUMBER_OF_CPUS];
    int i;
    int cpuArray[NUMBER_OF_CPUS];

    // initialise each linked list and add it to the ready queue array
    for(i = 0; i < NUMBER_OF_PRIORITY_LEVELS; i++) {
        LinkedList list = LINKED_LIST_INITIALIZER;
        readyQueue[i] = list;
    }

    // initialise each linked list and add it to the io queue array
    for(i = 0; i < NUMBER_OF_IO_DEVICES; i++) {
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

    // initialise the "pool" of PIDs
    for(i = 0; i < SIZE_OF_PROCESS_TABLE; i++) {
        pidArray[i] = i;
    }

    // initialise semaphores - sem_init(sem_t *sem, int pshared, unsigned int value);
    sem_init(&generatorSemaphore, 0, MAX_CONCURRENT_PROCESSES);
    sem_init(&simulatorSemaphore, 0, 0);
    sem_init(&terminatorSemaphore, 0, 0);

    // create threads - pthread_create(thread_id, attributes, function, function arg)
    pthread_create(&generatorThread, NULL, generator, NULL);
    for(i = 0; i < NUMBER_OF_CPUS; i++) {
        cpuArray[i] = i;
        pthread_create(&simulatorThreads[i], NULL, simulator, &cpuArray[i]);
    }
    pthread_create(&terminatorThread, NULL, terminator, NULL);
    pthread_create(&boosterThread, NULL, booster, NULL);
    pthread_create(&ioThread, NULL, ioSimulator, NULL);
    
    // waits for the thread to finish - pthread_join(thread_id, exit status)
    pthread_join(generatorThread, NULL);
    for(i = 0; i < NUMBER_OF_CPUS; i++) {
        pthread_join(simulatorThreads[i], NULL);
    }
    pthread_join(terminatorThread, NULL);
    pthread_join(ioThread, NULL);
    pthread_join(boosterThread, NULL);

    // destroys semaphores
    sem_destroy(&generatorSemaphore);
    sem_destroy(&simulatorSemaphore);
    sem_destroy(&terminatorSemaphore);

    // destroys mutexes
    pthread_mutex_destroy(&printLock);
    pthread_mutex_destroy(&rQueueLock);
    pthread_mutex_destroy(&tQueueLock);
    pthread_mutex_destroy(&arrayLock);
    pthread_mutex_destroy(&tblLock);
    pthread_mutex_destroy(&ioQueueLock);

    return 0;
}

/* 
 * Generator function: Adds processes to the ready queue, goes to sleep when there are
 * MAX_CONCURRENT_PROCESSES in the system, and is woken up as soon as a new process can
 * be added to the system.
 */
void * generator() {
    int i, j;
    pid_t pid = 0;

    // loop for creating processes
    for(i = 0; i < NUMBER_OF_PROCESSES; i++) {
        sem_wait(&generatorSemaphore);
        
        // if indexPid is within the pid pool
        if(indexPid < SIZE_OF_PROCESS_TABLE) {
            int processesNum = 0;

            pid = pidArray[indexPid];
            indexPid++;

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
            addLast(p, &readyQueue[p->iPriority]);
            printsf("QUEUE - ADDED: [Queue = READY %d, Size = %d, PID = %d, Priority = %d]\n",
                p->iPriority, getSize(&readyQueue[p->iPriority]), p->iPID, p->iPriority);

            // counts the total number of processes in the ready queues 
            for(j = 0; j < NUMBER_OF_PRIORITY_LEVELS; j++) {
                processesNum += getSize(&readyQueue[j]);
            }

            // if the ready queue is full, wake up the simulator
            if(processesNum == MAX_CONCURRENT_PROCESSES) {
                for(j = 0; j < NUMBER_OF_CPUS; j++) {
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
    int i;
    int cpuNum = *((int *) num);
    // wait for the generator to signal
    sem_wait(&simulatorSemaphore);

    // simulate the execution of processes in the readyQueue
    while(1) {
        // lock the ready queue
        pthread_mutex_lock(&rQueueLock);

        Process *cp;

        int processesNum = 0;
        // for loop to count the number of processes in the ready queue
        for(i = 0; i < NUMBER_OF_PRIORITY_LEVELS; i++) {
            processesNum += getSize(&readyQueue[i]);
        }

        // if the ready queue is empty...
        if(processesNum == 0) {
            // lock terminated queue
            pthread_mutex_lock(&tQueueLock);
            
            // check if all processes are terminated
            if(terminatedProcessCount == NUMBER_OF_PROCESSES){
                // unlocks queues (ready and terminated)
                pthread_mutex_unlock(&rQueueLock);
                pthread_mutex_unlock(&tQueueLock);
                break;
            }
            // unlocks queues (ready and terminated)
            pthread_mutex_unlock(&rQueueLock);
            pthread_mutex_unlock(&tQueueLock);
            continue;
        }
        // else ready queue is populated
        else {
            // for loop to remove the first non empty queue
            for(i = 0; i < NUMBER_OF_PRIORITY_LEVELS; i++) {
                if(getSize(&readyQueue[i]) > 0) {
                    // remove the current process from the ready queue
                    cp = removeFirst(&readyQueue[i]);
                    printsf("QUEUE - REMOVED: [Queue = READY %d, Size = %d, PID = %d, Priority = %d]\n",
                        i, getSize(&readyQueue[i]), cp->iPID, cp->iPriority);
                    break;
                }
            }

            // unlock the ready queue
            pthread_mutex_unlock(&rQueueLock);

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

            // if the process is ready, add it back to the readyQueue
            if (cp->iState == READY) {
                // lock the ready queue 
                pthread_mutex_lock(&rQueueLock);

                // add the process back to the ready queue
                addLast(cp, &readyQueue[cp->iPriority]);
                printsf("QUEUE - ADDED: [Queue = READY %d, Size = %d, PID = %d, Priority = %d]\n",
                    cp->iPriority, getSize(&readyQueue[cp->iPriority]), cp->iPID, cp->iPriority);

                // unlock the ready queue
                pthread_mutex_unlock(&rQueueLock);

                printsf("SIMULATOR - CPU %d - READY: [PID = %d, Priority = %d]\n", cpuNum, cp->iPID, cp->iPriority);
            
            } 
            // else if the process is blocked, add it to the I/O queue
            else if (cp->iState == BLOCKED) {
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
            // if the process is terminated, print information and add it to the terminatedQueue
            else {
                // lock the terminated queue
                pthread_mutex_lock(&tQueueLock);

                terminatedProcessCount++;

                printsf("SIMULATOR - CPU %d - TERMINATED: [PID = %d, ResponseTime = %d, TurnAroundTime = %d]\n",
                    cpuNum, cp->iPID, getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oFirstTimeRunning), 
                    getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oLastTimeRunning));
                addLast(cp, &terminatedQueue);

                printsf("QUEUE - ADDED: [Queue = TERMINATED, Size = %d, PID = %d, Priority = %d]\n",
                    getSize(&terminatedQueue), cp->iPID, cp->iPriority);
                
                // unlock the terminated queue
                pthread_mutex_unlock(&tQueueLock);

                // update response and turnaround time statistics
                totalResponseTime += getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oFirstTimeRunning);
                totalturnAroundTime += getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oLastTimeRunning);

                // signal the terminator
                sem_post(&terminatorSemaphore);
            }
        }
    }

    while(1) {
        // lock the terminator queue
        pthread_mutex_lock(&tQueueLock);
        // check if all processes are terminated
        if(terminatedProcessCount == NUMBER_OF_PROCESSES) {
            // unlock the terminator queue
            pthread_mutex_unlock(&tQueueLock);
            break;
        }
        // unlock the terminator queue
        pthread_mutex_unlock(&tQueueLock);
    }

    // sleep
    usleep(100);
    printsf("SIMULATOR: Finished\n");
}

/*
 * Terminator function: Woken up when a process is returned in the TERMINATED state and
 * so is added to the terminated queue.
 */
void * terminator() {
    // counter for terminated processes
    int terminatedCounter = 0;

    while(1) {
        // wait for the simulator to signal
        sem_wait(&terminatorSemaphore);

        // lock the terminated queue
        pthread_mutex_lock(&tQueueLock);

        // remove the process from the terminated queue
        Process *dp =  removeFirst(&terminatedQueue);

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

        // destroy the process
        destroyProcess(dp);
        terminatedCounter++;

        // check if all processes are terminated
        if(terminatedCounter == NUMBER_OF_PROCESSES) {
            break;
        }
    }

    // calculate the average response and turnaround time
    avResponseTime = totalResponseTime/NUMBER_OF_PROCESSES;
    avTurnAroundTime = totalturnAroundTime/NUMBER_OF_PROCESSES;

    printsf("TERMINATION DAEMON: Finished\n");
    printsf("TERMINATION DAEMON: [Average Response Time = %lf, Average Turn Around Time = %lf]\n",
        avResponseTime, avTurnAroundTime);
}

/*
 * Booster function: Function to boost priority levels of selected processes in the ready queue
 * The booster increases the priority of RR jobs periodically to the highest RR level
 */
void * booster() {
    int i;
    // sleep
    usleep(BOOST_INTERVAL);

    printsf("BOOSTER DAEMON: Created\n");
    while (1)
    {
        // lock the ready queue
        pthread_mutex_lock(&rQueueLock);

        // for loop to loop through the priorities starting from mid point + 1
        for(i = (NUMBER_OF_PRIORITY_LEVELS/2)+1; i < NUMBER_OF_PRIORITY_LEVELS; i++) {
            // if there are processes in the current priority level
            if(getSize(&readyQueue[i]) > 0) {
                // remove the first non empty queue
                Process *bp = removeFirst(&readyQueue[i]);
                
                printsf("QUEUE - REMOVED: [Queue = READY %d, Size = %d, PID = %d, Priority = %d]\n", 
                            i, getSize(&readyQueue[i]), bp->iPID, bp->iPriority);

                printsf("BOOSTER DAEMON: [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d] => Boosted to Level %d\n",
                            bp->iPID, bp->iPriority, bp->iBurstTime, bp->iRemainingBurstTime, NUMBER_OF_PRIORITY_LEVELS/2);

                // add the boosted process to the mid-point priority level
                addLast(bp, &readyQueue[NUMBER_OF_PRIORITY_LEVELS/2]);
                printsf("QUEUE - ADDED: [Queue = READY %d, Size = %d, PID = %d, Priority = %d]\n", 
                            NUMBER_OF_PRIORITY_LEVELS/2, getSize(&readyQueue[NUMBER_OF_PRIORITY_LEVELS/2]), bp->iPID, bp->iPriority);
            }

        }

        // unlock ready queue
        pthread_mutex_unlock(&rQueueLock);
        
        // lock the terminator queue
        pthread_mutex_lock(&tQueueLock);
        // check if all processes are terminated
        if(terminatedProcessCount == NUMBER_OF_PROCESSES) {
            // unlock the terminator queue
            pthread_mutex_unlock(&tQueueLock);
            break;
        }
        // unlock the terminator queue
        pthread_mutex_unlock(&tQueueLock);

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
    int i;
    // sleep
    usleep(IO_DAEMON_INTERVAL);

    while(1)
    {
        // lock the io queue
        pthread_mutex_lock(&ioQueueLock);

        // for loop to loop through the number of devices
        for(i = 0; i < NUMBER_OF_IO_DEVICES; i++)
        {    
            // while loop to run through every blocked process and unblock it
            while(getSize(&ioQueue[i]) > 0)
            {
                // removes blocked process (unblock) from io queue
                Process *p = removeFirst(&ioQueue[i]);
                printsf("I/O DAEMON - UNBLOCKED: [PID = %d, Priority = %d]\n", 
                            p->iPID, p->iPriority);
       
                // locks ready queue
                pthread_mutex_lock(&rQueueLock);

                // adds process to ready queue
                addFirst(p, &readyQueue[p->iPriority]);
                printsf("QUEUE - ADDED: [Queue = READY %d, Size = %d, PID = %d, Priority = %d]\n", 
                            p->iPriority, getSize(&readyQueue[p->iPriority]), p->iPID, p->iPriority);

                // unlocks ready queue
                pthread_mutex_unlock(&rQueueLock);
            }
        }

        // unlocks the io queues
        pthread_mutex_unlock(&ioQueueLock);

        // locks terminated queue
        pthread_mutex_lock(&tQueueLock);
        // check if all processes are terminated
        if(terminatedProcessCount == NUMBER_OF_PROCESSES) {
            // unlock the terminator queue
            pthread_mutex_unlock(&tQueueLock);
            break;
        }
        // unlock the terminator queue
        pthread_mutex_unlock(&tQueueLock);

        // sleeps
        usleep(IO_DAEMON_INTERVAL*1000);
    }

    printsf("I/O DAEMON: Finished\n");
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
