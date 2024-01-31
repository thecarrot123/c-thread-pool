# Thread Pool Implementation in C

## Overview

This project is a custom implementation of a thread pool in C. A thread pool is a design pattern that allows for efficient management of multiple threads in a concurrent application, reusing a fixed number of threads to execute tasks. This implementation aims to provide a lightweight and efficient alternative to the thread pool functionalities found in large general-purpose libraries like Qt, .NET, and Boost.

## Features

- **Dynamic Thread Management:** Threads are not all started at once; they're created on demand up to a maximum limit.
- **Task Re-pushing:** Tasks that have been joined but not deleted can be re-pushed back into the pool for execution.
- **Status Check and Result Retrieval:** Similar to `pthread_join`, users can check a task's status and wait for its completion to get the result.
- **No Global Variables:** Ensures thread safety and ease of use in various contexts without relying on global state.
- **Memory Leak Prevention:** Designed to prevent memory leaks, supporting rigorous validation with tools like Valgrind or ASAN.

## Getting Started

### Prerequisites

- GCC compiler 11.4.0
- pthreads library (usually comes with GCC on most Unix-like systems)
- GNU Make 4.3

### Building

TODO

## Usage

This section outlines how to integrate and use the thread pool library in your C projects.

### Including the Thread Pool Library

To use the thread pool in your project, you first need to include the header file in your source code:

```c
#include "thread_pool.h"
```

### Creating a Thread Pool

Create a new thread pool by specifying the maximum number of worker threads it should support. This is done using the thread_pool_new function, which returns a pointer to the newly created thread pool.

```c
struct thread_pool *pool = thread_pool_new(max_threads);
```

- max_threads: The maximum number of threads that can be created in the pool.

### Creating and Submitting Tasks

To submit tasks to the thread pool, you need to create a struct thread_task for each task. Here's how you can create and submit a task:

```c
void task_function(void *arg) {
    // Task implementation
}

// Create a new task
struct thread_task *task = thread_task_create(task_function, task_argument);

// Submit the task to the pool
thread_task_submit(pool, task);
```

- **task_function:** A pointer to the function that will be executed by the worker thread.
- **task_argument:** A pointer to the argument that will be passed to the task function.

### Waiting for Tasks to Complete

To wait for a task's completion and retrieve its result, use thread_task_join:

```c
void *result;
if (thread_task_join(task, &result) == 0) {
    // Task completed successfully, process result
}
```

The thread_task_join function behaves similarly to pthread_join, blocking the calling thread until the specified task has completed.

### Detaching Tasks

thread_task_detach allows a task to be executed without the need to join it later. This is useful for fire-and-forget tasks:

```c
thread_task_detach(task);
```

### Cleaning Up

After you are done with the thread pool and tasks, it is important to free allocated resources:

- Use `thread_task_destroy(task)` to free a task if it's no longer needed and hasn't been submitted or after a join.
- Use `thread_pool_destroy(pool)` to cleanly shut down the thread pool and free its resources.
