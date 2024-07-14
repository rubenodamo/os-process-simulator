#include <sys/types.h> 
#include <unistd.h> 
#include <stdio.h> 
#include <stdlib.h>

#include "linkedlist.h"
#include "os_process.h"

int main() {
    pid_t pid = 0;
    Process *p = generateProcess(pid);

    /*
    Output InitialBurstTime and RemaingBurstTime

    ***FORMULAE***
    burst time = execution time = last execution - first execution
    */
    printf("GENERATOR - CREATED: [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n",
           p->iPID, p->iPriority, p->iBurstTime, p->iRemainingBurstTime);

    // simulation in round robin fashion
    while (p->iState != TERMINATED) {
        runPreemptiveProcess(p, true);
        printf("SIMULATOR - CPU 0: RR [PID = %d, Priority = %d, InitialBurstTime = %d, RemainingBurstTime = %d]\n",
               p->iPID, p->iPriority, p->iBurstTime, p->iRemainingBurstTime);
    }

    /*
    Output ResponseTime and TurnAroundTime

    ***FORMULAE***
    response time = first execution - arrival time [i.e. one process, therefore arrival time = creation time (no queue)]
    turn around time =  completion time - arrival time [i.e. last execution - creation time]
    */
    printf("TERMINATOR - TERMINATED: [PID = %d, ResponseTime = %ld, TurnAroundTime = %d]\n", 
        p->iPID, getDifferenceInMilliSeconds(p->oTimeCreated, p->oFirstTimeRunning), getDifferenceInMilliSeconds(p->oTimeCreated, p->oLastTimeRunning));

    // free memory
    destroyProcess(p);
    return 0;
}
