#include "thread_pool.h"
#include <stdio.h>

static void *
task_incr_f(void *arg)
{
	__atomic_add_fetch((int *)arg, 1, __ATOMIC_RELAXED);
	return arg;
}

int main()
{
	struct thread_pool *p;
    thread_pool_new(10, &p);
    struct thread_task *t;
    int arg = 0;
    void *result;
    int rc;
    for(int i=0;i<100;i++) {
        thread_task_new(&t, task_incr_f, &arg);
        thread_pool_push_task(p,t);
        rc = thread_task_join(t, &result);
        if(rc == 0)
            thread_task_delete(t);
    }
    thread_pool_delete(p);
    puts("done.");
	return 0;
}