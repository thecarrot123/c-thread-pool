#define _POSIX_C_SOURCE 200809
#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>

// Task states enumeration
enum task_state {
    TASK_STATE_DETACHED = 1,
    TASK_STATE_CREATED,
    TASK_STATE_PUSHED,
    TASK_STATE_EXECUTING,
    TASK_STATE_COMPLETED,
    TASK_STATE_JOINED
};

struct thread_task {
    thread_task_f work_function;
    void *work_arg;
    void *work_result;

    pthread_mutex_t *task_mutex;
    pthread_cond_t *task_cond;

    enum task_state current_state;
};

struct thread_pool {
    pthread_t *worker_threads;

    int max_thread_limit;
    int current_thread_count;
    int running_thread_count;

    pthread_mutex_t *pool_mutex;
    pthread_cond_t *pool_cond;

    struct thread_task **task_buffer;
    int task_buffer_count;

    bool is_shutting_down;
};

int 
thread_pool_new(int max_thread_count, struct thread_pool **pool) {
    if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    *pool = malloc(sizeof(struct thread_pool));
    if (*pool == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    (*pool)->max_thread_limit = max_thread_count;
    (*pool)->current_thread_count = 0;
    (*pool)->running_thread_count = 0;
    (*pool)->worker_threads = malloc(sizeof(pthread_t) * max_thread_count);
    if ((*pool)->worker_threads == NULL) {
        free(*pool);
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    // Initialize mutex and condition variable for the pool
    (*pool)->pool_mutex = malloc(sizeof(pthread_mutex_t));
    (*pool)->pool_cond = malloc(sizeof(pthread_cond_t));
    if ((*pool)->pool_mutex == NULL || (*pool)->pool_cond == NULL) {
        free((*pool)->worker_threads);
        free((*pool)->pool_mutex);
        free((*pool)->pool_cond);
        free(*pool);
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
    pthread_mutex_init((*pool)->pool_mutex, NULL);
    pthread_cond_init((*pool)->pool_cond, NULL);

    // Initialize task buffer (queue)
    (*pool)->task_buffer_count = 0;
    (*pool)->task_buffer = malloc(sizeof(struct thread_task *) * (TPOOL_MAX_TASKS + max_thread_count));
    if ((*pool)->task_buffer == NULL) {
        free((*pool)->worker_threads);
        free((*pool)->pool_mutex);
        free((*pool)->pool_cond);
        free(*pool);
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    (*pool)->is_shutting_down = false;

    return 0;
}


int 
thread_pool_thread_count(const struct thread_pool *pool) {
    if (pool == NULL) {
        return 0;
    }
	int count;

    pthread_mutex_lock(pool->pool_mutex);
	count = pool->current_thread_count;
	pthread_mutex_unlock(pool->pool_mutex);

	return count;
}

int
thread_pool_delete(struct thread_pool *pool) {
    if (pool == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(pool->pool_mutex);
    
    // Check if there are still tasks in the queue
    if (pool->task_buffer_count > 0 || pool->running_thread_count > 0) {
        pthread_mutex_unlock(pool->pool_mutex);
        return TPOOL_ERR_HAS_TASKS;
    }

    // Indicate that the pool is shutting down
    pool->is_shutting_down = true;

    // Wake up all threads if they are waiting
    pthread_cond_broadcast(pool->pool_cond);

    pthread_mutex_unlock(pool->pool_mutex);

    for (int i = 0; i < pool->current_thread_count; ++i) {
        pthread_join(pool->worker_threads[i], NULL);
    }
    free(pool->worker_threads);

    pthread_mutex_destroy(pool->pool_mutex);
    pthread_cond_destroy(pool->pool_cond);
    free(pool->pool_mutex);
    free(pool->pool_cond);
    free(pool->task_buffer);
    free(pool);

    return 0;
}

void *
worker_thread_function(void *arg) {
    struct thread_pool *pool = (struct thread_pool *)arg;

    while (1) {
        pthread_mutex_lock(pool->pool_mutex);

        while (pool->task_buffer_count == 0) {
            if (pool->is_shutting_down) {
                pthread_mutex_unlock(pool->pool_mutex);
                return NULL;
            }
            pthread_cond_wait(pool->pool_cond, pool->pool_mutex);
        }

		pool->running_thread_count++;
        struct thread_task *task = pool->task_buffer[--pool->task_buffer_count];
		
        pthread_mutex_unlock(pool->pool_mutex);

        pthread_mutex_lock(task->task_mutex);

        if (task->current_state != TASK_STATE_DETACHED) {
			task->current_state = TASK_STATE_EXECUTING;
		}

        thread_task_f fun = task->work_function;
		void *worker_arg = task->work_arg;

        pthread_mutex_unlock(task->task_mutex);

        void *result = fun(worker_arg);

        pthread_mutex_lock(task->task_mutex);

        task->work_result = result;

        if (task->current_state == TASK_STATE_DETACHED) {
            pthread_mutex_unlock(task->task_mutex);
            thread_task_delete(task);
        } else {
            task->current_state = TASK_STATE_COMPLETED;
            pthread_cond_signal(task->task_cond);
            pthread_mutex_unlock(task->task_mutex);
        }
        
        pthread_mutex_lock(pool->pool_mutex);
        pool->running_thread_count--;
        pthread_mutex_unlock(pool->pool_mutex);
    }

    return NULL;
}

int 
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task) {
    if (pool == NULL || task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(pool->pool_mutex);

    if (pool->task_buffer_count >= TPOOL_MAX_TASKS) {
        pthread_mutex_unlock(pool->pool_mutex);
        return TPOOL_ERR_TOO_MANY_TASKS;
    }

	pthread_mutex_lock(task->task_mutex);

    task->current_state = TASK_STATE_PUSHED;
    pool->task_buffer[pool->task_buffer_count++] = task;

	pthread_mutex_unlock(task->task_mutex);

    if (pool->current_thread_count < pool->max_thread_limit &&
		 pool->current_thread_count == pool->running_thread_count) {
        pthread_t new_thread;
        pthread_create(&new_thread, NULL, worker_thread_function, pool);
		pool->worker_threads[pool->current_thread_count++] = new_thread;
    }

    // Signal a worker thread that a new task is available
    pthread_cond_signal(pool->pool_cond);

    pthread_mutex_unlock(pool->pool_mutex);

    return 0;
}

int 
thread_task_new(struct thread_task **task, thread_task_f function, void *arg) {
    if (task == NULL || function == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    *task = malloc(sizeof(struct thread_task));
    if (*task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    (*task)->work_function = function;
    (*task)->work_arg = arg;
    (*task)->work_result = NULL; // Initially, there's no result

    (*task)->task_mutex = malloc(sizeof(pthread_mutex_t));
    (*task)->task_cond = malloc(sizeof(pthread_cond_t));
    if ((*task)->task_mutex == NULL || (*task)->task_cond == NULL) {
        free((*task)->task_mutex);
        free((*task)->task_cond);
        free(*task);
        return TPOOL_ERR_INVALID_ARGUMENT;
    }
    pthread_mutex_init((*task)->task_mutex, NULL);
    pthread_cond_init((*task)->task_cond, NULL);

    (*task)->current_state = TASK_STATE_CREATED;

    return 0;
}


bool
thread_task_is_finished(const struct thread_task *task) {
    if (task == NULL) {
        return false;
    }

    return task->current_state == TASK_STATE_COMPLETED;
}

bool
thread_task_is_running(const struct thread_task *task) {
    if (task == NULL) {
        return false;
    }

    return task->current_state == TASK_STATE_EXECUTING;
}

int
thread_task_join(struct thread_task *task, void **result) {
    if (task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(task->task_mutex);

	if (task->current_state == TASK_STATE_CREATED ||
		task->current_state == TASK_STATE_DETACHED) {
        pthread_mutex_unlock(task->task_mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    while (task->current_state != TASK_STATE_COMPLETED) {
        pthread_cond_wait(task->task_cond, task->task_mutex);
    }

    task->current_state = TASK_STATE_JOINED;

    if (result != NULL) {
        *result = task->work_result;
    }

    pthread_mutex_unlock(task->task_mutex);
	
    return 0;
}

#ifdef NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result) {
    if (task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

	if (timeout <= 0.0) {
		return TPOOL_ERR_TIMEOUT;
	}

    pthread_mutex_lock(task->task_mutex);
	
	if (task->current_state == TASK_STATE_CREATED ||
        task->current_state == TASK_STATE_DETACHED) {
		pthread_mutex_unlock(task->task_mutex);
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}

    struct timespec end, current;
	clock_gettime(CLOCK_MONOTONIC, &end);

	end.tv_sec += (long)timeout;
	end.tv_nsec += (long)((timeout - (long)timeout) * 1e9);

	if (end.tv_nsec >= 1e9) {
		end.tv_sec += 1;
		end.tv_nsec -= 1e9;
	}

    while (task->current_state != TASK_STATE_COMPLETED) {
		clock_gettime(CLOCK_MONOTONIC, &current);
		if (current.tv_sec > end.tv_sec || 
		(current.tv_sec == end.tv_sec && current.tv_nsec >= end.tv_nsec)) {
			pthread_mutex_unlock(task->task_mutex);
			return TPOOL_ERR_TIMEOUT;
		}
        pthread_cond_timedwait(task->task_cond, task->task_mutex, &end);
    }

	*result = task->work_result;
	task->current_state = TASK_STATE_JOINED;
    pthread_mutex_unlock(task->task_mutex);

    return 0;
}

#endif

int
thread_task_delete(struct thread_task *task) {
    if (task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(task->task_mutex);

    if (task->current_state == TASK_STATE_EXECUTING || 
        task->current_state == TASK_STATE_PUSHED || 
        task->current_state == TASK_STATE_COMPLETED) {
        pthread_mutex_unlock(task->task_mutex);
        return TPOOL_ERR_TASK_IN_POOL;
    }

    pthread_mutex_unlock(task->task_mutex);
    pthread_mutex_destroy(task->task_mutex);
    free(task->task_mutex);

    pthread_cond_destroy(task->task_cond);
    free(task->task_cond);
    free(task);

    return 0;
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task) {
    if (task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(task->task_mutex);

    if (task->current_state != TASK_STATE_PUSHED &&
        task->current_state != TASK_STATE_EXECUTING &&
        task->current_state != TASK_STATE_COMPLETED) {
        pthread_mutex_unlock(task->task_mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    if (task->current_state == TASK_STATE_COMPLETED) {
        pthread_mutex_unlock(task->task_mutex);
        thread_task_delete(task);
        return 0;
    }

    task->current_state = TASK_STATE_DETACHED;

    pthread_mutex_unlock(task->task_mutex);

    return 0;
}

#endif
