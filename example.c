#include "thread_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

pthread_mutex_t mutex;

/**
 * sleep 1 second then increment arg by 1.
 * this function uses global mutex
 * @param arg pointer to integer
 * 
 * @retval Pointer to arg
*/
static void *
increment(void *arg)
{
    sleep(1);
    int *cnt = (int *)arg;
    pthread_mutex_lock(&mutex);
    *cnt+=1;
    pthread_mutex_unlock(&mutex);
	return arg;
}

int 
main()
{
    const int TASK_NUM=30;
    const int THREAD_NUM=15;
	struct thread_pool *pool;
    struct thread_task *tasks[TASK_NUM];
    struct timespec s_ts, e_ts;
    int arg = 0;

    clock_gettime(CLOCK_MONOTONIC, &s_ts);    
    printf("Creat thread pool with %d threads.\n",THREAD_NUM);
    thread_pool_new(THREAD_NUM, &pool);


    printf("Creat and push %d tasks.\n", TASK_NUM);
    for(int i=0;i<TASK_NUM;i++) {
        thread_task_new(&tasks[i], increment, &arg);
        thread_pool_push_task(pool,tasks[i]);
    }
    void *result;
    int rc;

    puts("Join tasks and print results.");
    for(int i=0;i<TASK_NUM;i++) {
        rc = thread_task_join(tasks[i], &result);
        if(rc == 0) {
            thread_task_delete(tasks[i]);
        } else {
            puts("task join failed");
            exit(1);
        }
    }
    printf("Result: %d\n",*(int *)result);
    clock_gettime(CLOCK_MONOTONIC, &e_ts);

    thread_pool_delete(pool);
    double total_time = e_ts.tv_sec * 1000 + e_ts.tv_nsec / 1000000 - (s_ts.tv_sec * 1000 + s_ts.tv_nsec / 1000000);
    printf("Finished %d tasks using %d threads in %.3lf ms.\n", TASK_NUM, THREAD_NUM, total_time);
	return 0;
}