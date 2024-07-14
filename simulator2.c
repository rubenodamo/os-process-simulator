#include <sys/types.h> 
#include <unistd.h> 
#include <stdio.h> 
#include <stdlib.h>

#include "linkedlist.h"
#include "os_process.h"

int getSize(LinkedList *pList); 

int main() {
    // Initialize two linked lists: readyQueue and terminatedQueue
    LinkedList readyQueue = LINKED_LIST_INITIALIZER;
    LinkedList terminatedQueue = LINKED_LIST_INITIALIZER;

    int i;
    pid_t pid = 0;

    float totalResponseTime = 0;
    float totalturnAroundTime = 0;

    float avResponseTime = 0.0;
    float avTurnAroundTime = 0.0;

    int iTerminated = 0;

    // Generate and add processes to the readyQueue
    for(i = 0; i < NUMBER_OF_PROCESSES; i++) {
        pid = i;

        Process *p = generateProcess(pid);
        printf("GENERATOR - CREATED: [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n", 
            p->iPID, p->iPriority, p->iBurstTime, p->iRemainingBurstTime);

        addLast(p, &readyQueue);
        printf("QUEUE - ADDED: [Queue = READY, Size = %d, PID = %d, Priority = %d]\n",
            getSize(&readyQueue), p->iPID, p->iPriority);

        printf("GENERATOR - ADMITTED: [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n",
            p->iPID, p->iPriority, p->iBurstTime, p->iRemainingBurstTime);
    }
    printf("GENERATOR: Finished\n");

    // Simulate the execution of processes in the readyQueue
    while(getHead(readyQueue) != NULL) {
        Process *cp = removeFirst(&readyQueue);
        printf("QUEUE - REMOVED: [Queue = READY, Size = %d, PID = %d, Priority = %d]\n",
           getSize(&readyQueue), cp->iPID, cp->iPriority);
        runPreemptiveProcess(cp, false);

        printf("SIMULATOR - CPU 0: RR [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n",
            cp->iPID, cp->iPriority, cp->iBurstTime, cp->iRemainingBurstTime);

        // If the process is not terminated, add it back to the readyQueue
        if (cp->iState != TERMINATED) {
            addLast(cp, &readyQueue);
            printf("QUEUE - ADDED: [Queue = READY, Size = %d, PID = %d, Priority = %d]\n",
                getSize(&readyQueue), cp->iPID, cp->iPriority);
            printf("SIMULATOR - CPU 0 - READY: [PID = %d, Priority = %d]\n", cp->iPID, cp->iPriority);
        // If the process is terminated, print information and add it to the terminatedQueue
        } else {
            printf("SIMULATOR - CPU 0 - TERMINATED: [PID = %d, ResponseTime = %d, TurnAroundTime = %d]\n",
                cp->iPID, getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oFirstTimeRunning), getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oLastTimeRunning));
            addLast(cp, &terminatedQueue);

            printf("QUEUE - ADDED: [Queue = TERMINATED, Size = %d, PID = %d, Priority = %d]\n",
                getSize(&terminatedQueue), cp->iPID, cp->iPriority);

            // Update response and turnaround time statistics
            totalResponseTime += getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oFirstTimeRunning);
            totalturnAroundTime += getDifferenceInMilliSeconds(cp->oTimeCreated, cp->oLastTimeRunning);
        }
    }
    printf("SIMULATOR: Finished\n");

    while(getHead(terminatedQueue) != NULL) {
        Process *dp =  removeFirst(&terminatedQueue);
        printf("QUEUE - REMOVED: [Queue = TERMINATED, Size = %d, PID = %d, Priority = %d]\n",
            getSize(&terminatedQueue),dp->iPID, dp->iPriority);
        destroyProcess(dp);

        printf("TERMINATION DAEMON - CLEARED: [#iTerminated = %d, PID = %d, Priority = %d]\n",
            ++iTerminated, dp->iPID, dp->iPriority);
    }

    // Calculates the average response and turn around time
    avResponseTime = totalResponseTime/NUMBER_OF_PROCESSES;
    avTurnAroundTime = totalturnAroundTime/NUMBER_OF_PROCESSES;

    printf("TERMINATION DAEMON: Finished\n");
    printf("TERMINATION DAEMON: [Average Response Time = %lf, Average Turn Around Time = %lf]\n",
        avResponseTime, avTurnAroundTime);
}

// Function to get the size of a linked list
int getSize(LinkedList *pList) {
    int size = 0;
    Element *current = pList->pHead; // Start at the head of the list

    while (current != NULL) {
        size++; // Increment the size for each element
        current = current->pNext; // Move to the next element
    }

    return size;
}
