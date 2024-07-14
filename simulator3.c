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

int getSize(LinkedList *pList); 

void printsf(const char *format, ...);

// semaphores to control the flow of threads
sem_t generatorSemaphore, simulatorSemaphore, terminatorSemaphore;

// linked lists for the ready and terminated queues
LinkedList readyQueue = LINKED_LIST_INITIALIZER;
LinkedList terminatedQueue = LINKED_LIST_INITIALIZER;

// counters and statistics variables
int terminatedProcessCount = 0;
int iTerminated = 0;
float totalResponseTime = 0;
float totalturnAroundTime = 0;
float avResponseTime = 0.0;
float avTurnAroundTime = 0.0;

// mutexes for thread synchronisation
pthread_mutex_t printLock, rQueueLock, tQueueLock;

int main() {
    // thread handles
    pthread_t generatorThread, simulatorThread, terminatorThread;

    // initialise mutexes - pthread_mutex_init(pthread_mutex_t *mutex, attribute NONRECURSIVE);
    pthread_mutex_init(&printLock, NULL);
    pthread_mutex_init(&rQueueLock, NULL);
    pthread_mutex_init(&tQueueLock, NULL);

    // initialise semaphores - sem_init(sem_t *sem, int pshared, unsigned int value);
    sem_init(&generatorSemaphore, 0, MAX_CONCURRENT_PROCESSES);
    sem_init(&simulatorSemaphore, 0, 0);
    sem_init(&terminatorSemaphore, 0, 0);

    // create threads - pthread_create(thread_id, attributes, function, function arg)
    pthread_create(&generatorThread, NULL, generator, NULL);
    pthread_create(&simulatorThread, NULL, simulator, NULL);
    pthread_create(&terminatorThread, NULL, terminator, NULL);
    
    // waits for the thread to finish - pthread_join(thread_id, exit status)
    pthread_join(generatorThread, NULL);
    pthread_join(simulatorThread, NULL);
    pthread_join(terminatorThread, NULL);

    // destroys semaphores
    sem_destroy(&generatorSemaphore);
    sem_destroy(&simulatorSemaphore);
    sem_destroy(&terminatorSemaphore);

    // destroys mutexes
    pthread_mutex_destroy(&printLock);
    pthread_mutex_destroy(&rQueueLock);
    pthread_mutex_destroy(&tQueueLock);

    return 0;
}

/* 
 * Generator function: Adds processes to the ready queue, goes to sleep when there are
 * MAX_CONCURRENT_PROCESSES in the system, and is woken up as soon as a new process can
 * be added to the system.
 */
void * generator() {
    int i;
    pid_t pid = 0;

    // loop for creating processes
    for(i = 0; i < NUMBER_OF_PROCESSES; i++) {
        sem_wait(&generatorSemaphore);
        
        pid = i;

        // generate a process
        Process *p = generateProcess(pid);
        printsf("GENERATOR - CREATED: [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n", 
            p->iPID, p->iPriority, p->iBurstTime, p->iRemainingBurstTime);

        // lock the ready queue
        pthread_mutex_lock(&rQueueLock);

        // add the process to the ready queue
        addLast(p, &readyQueue);
        printsf("QUEUE - ADDED: [Queue = READY, Size = %d, PID = %d, Priority = %d]\n",
            getSize(&readyQueue), p->iPID, p->iPriority);

        // if the ready queue is full, wake up the simulator
        if(getSize(&readyQueue) == MAX_CONCURRENT_PROCESSES) {
            sem_post(&simulatorSemaphore);
        }       

        // unlock the ready queue
        pthread_mutex_unlock(&rQueueLock);

        printsf("GENERATOR - ADMITTED: [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n",
            p->iPID, p->iPriority, p->iBurstTime, p->iRemainingBurstTime); 
    }

    printsf("GENERATOR: Finished\n");
}


/*
 * Simulator function: Removes processes from the ready queues and runs them in a round robin fashion
 * using the runPreemptiveProcess() function.
 */
void * simulator() {
    // wait for the generator to signal
    sem_wait(&simulatorSemaphore);

    // simulate the execution of processes in the readyQueue
    while(1) {
        // lock the ready queue
        pthread_mutex_lock(&rQueueLock);

        // remove the current process from the ready queue
        Process *cp = removeFirst(&readyQueue);
        printsf("QUEUE - REMOVED: [Queue = READY, Size = %d, PID = %d, Priority = %d]\n",
           getSize(&readyQueue), cp->iPID, cp->iPriority);
        
        // unlock the ready queue
        pthread_mutex_unlock(&rQueueLock);

        // run the process in RR fashion
        runPreemptiveProcess(cp, false);

        printsf("SIMULATOR - CPU 0: RR [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n",
            cp->iPID, cp->iPriority, cp->iBurstTime, cp->iRemainingBurstTime);

        // if the process is ready, add it back to the readyQueue
        if (cp->iState == READY) {
            // lock the ready queue 
            pthread_mutex_lock(&rQueueLock);

            // add the process back to the ready queue
            addLast(cp, &readyQueue);
            printsf("QUEUE - ADDED: [Queue = READY, Size = %d, PID = %d, Priority = %d]\n",
                getSize(&readyQueue), cp->iPID, cp->iPriority);

            // unlock the ready queue
            pthread_mutex_unlock(&rQueueLock);

            printsf("SIMULATOR - CPU 0 - READY: [PID = %d, Priority = %d]\n", cp->iPID, cp->iPriority);
        // if the process is terminated, print information and add it to the terminatedQueue
        } else {
            // lock the terminated queue
            pthread_mutex_lock(&tQueueLock);

            terminatedProcessCount++;

            printsf("SIMULATOR - CPU 0 - TERMINATED: [PID = %d, ResponseTime = %d, TurnAroundTime = %d]\n",
                cp->iPID, getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oFirstTimeRunning), 
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

        // check if all processes are terminated
        if(terminatedProcessCount == NUMBER_OF_PROCESSES) {
            break;
        }
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

// function to print with thread-safe formatting
void printsf(const char *format, ...) {
    pthread_mutex_lock(&printLock);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);

    pthread_mutex_unlock(&printLock);
}