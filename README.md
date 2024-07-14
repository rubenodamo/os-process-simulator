# OS Process Simulator Implementation

This project is an implementation of a process simulator using the POSIX API on Linux. The goal is to simulate various aspects of an operating system, including process creation, scheduling, and termination, utilizing concurrency techniques. The simulator is structured in multiple stages, gradually increasing in complexity and functionality.

## Table of Contents

1. [Overview](#overview)
2. [Components](#components)
3. [Requirements](#requirements)
4. [Compilation and Execution](#compilation-and-execution)
5. [Breakdown of Stages](#breakdown-of-stages)

## Overview

The simulator implements key operating system concepts, including:
- Operating system APIs
- Process tables and queues
- Concurrent/parallel programming
- Critical sections, semaphores, mutexes, and mutual exclusion
- Bounded buffers
- C programming

## Components

The simulator is composed of the following components, all implemented as threads:
1. **Process Generator**: Simulates user process creation and adds processes to relevant data structures.
2. **Process Simulator**: Runs processes on the hardware using provided procedures.
3. **Booster Daemon**: Periodically boosts process priority to prevent resource starvation.
4. **CPU Load Balancer**: Manages load balancing by migrating processes between CPUs.
5. **I/O Daemon**: Simulates I/O operations by moving blocked processes back to the ready state.
6. **Process Terminator**: Cleans up terminated processes and reports statistics.

## Requirements

The following data structures  required:
- Pool of available PIDs
- Process table
- Ready queues (implemented as linked lists)
- I/O queues (one per device, implemented as linked lists)
- Terminated queue (implemented as a linked list)

## Compilation and Execution

To compile your code:
```sh
gcc -std=gnu99 simulatorX.c -lpthread -o simulatorX
```
Replace `simulatorX.c` with the appropriate stage file name.

To run your code:
```sh
./simulatorX
```

## Breakdown of Stages

1. **Simulation of a Single Process** (`simulator1.c`)
2. **Simulation of Multiple Processes** (`simulator2.c`)
3. **Parallelism - Single CPU** (`simulator3.c`)
4. **Process Table** (`simulator4.c`)
5. **Process Priorities** (`simulator5.c`)
6. **Booster Daemon** (`simulator6.c`)
7. **I/O Simulation** (`simulator7.c`)
8. **Parallelism - Multiple CPUs** (`simulator8.c`)
9. **Private Queues** (`simulator9.c`)
10. **Load Balancing** (`simulator10.c`)

Each stage builds on the previous, adding more complexity and functionality to the simulator.
