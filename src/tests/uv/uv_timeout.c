#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <uv.h>

#include <uv.h>
/*
uv_loop_t *loop;
uv_timer_t gc_req;
uv_timer_t fake_job_req;
*/

int stop  = 0;

void signal_handler(uv_signal_t *handle, int signum)
{
    printf("Signal received: %d\n", signum);
    uv_signal_stop(handle);
}

uv_loop_t* create_loop()
{
    uv_loop_t *loop = malloc(sizeof(uv_loop_t));
    if (loop) {
      uv_loop_init(loop);
    }  
    return loop;
}

void gc(uv_timer_t *handle) {
    int  *data = (int *) handle->data; 
    fprintf(stderr, "GC callback fired. data:'%d'\n", data  ? *data : 0 );
    if (*data > 10){
        printf("Stopping timeout now\n");
        stop = 1;
        uv_timer_stop(handle);
    }
    (*data)++;
    
}

void fake_job(uv_timer_t *handle) {
    char *data = (char *) handle->data; 
    fprintf(stderr, "Fake job cb fired. data:'%s'\n", data ?: "" );
    if (stop){
        printf("Stopping fake job timeout now\n");
        uv_timer_stop(handle);
    }
}


// two signal handlers in one loop
void thread1_worker(void *userp)
{
    uv_loop_t *loop1 = create_loop();
    //uv_timer_t fake_job_timer;
    printf("Creating worker thread. Has userp:'%s'\n", userp ? "Y":"N");
    uv_signal_t sig1a;
    uv_signal_init(loop1, &sig1a);
    uv_signal_start(&sig1a, signal_handler, SIGINT);
    //uv_signal_start(&sig1a, signal_handler, SIGTERM);
    
    //uv_timer_init(loop1, &fake_job_timer);
    //int i = 1; 
    //gc_req.data = &i;
    //int sched = 1000;
    //uv_timer_start(&fake_job_timer, fake_job, sched, 1000);
    uv_run(loop1, UV_RUN_DEFAULT);
    printf("Thread all done -------------\n");
}

int main() {
    uv_loop_t *loop;
    uv_timer_t fake_job_req; 
    uv_timer_t gc_req; 
    loop = uv_default_loop();

    uv_timer_init(loop, &gc_req);
    uv_unref((uv_handle_t*) &gc_req);
    int i = 1; 
    gc_req.data = &i;
    fake_job_req.data = "This is my data";
    // could actually be a TCP download or something
    uv_timer_init(loop, &fake_job_req);
    uv_timer_start(&gc_req, gc, 0, 1000);
    int sched = 1000;
    uv_timer_start(&fake_job_req, fake_job, sched, 1000);
    /*
    while ( i < 10){
        sched = i * 1000;
        printf("Loop done, sched is now:'%d?\n",sched);
        uv_timer_start(&fake_job_req, fake_job, sched, 0);
        uv_run(loop, UV_RUN_DEFAULT);
    }*/
    uv_thread_t thread1;
    uv_thread_create(&thread1, thread1_worker, 0);
    uv_run(loop, UV_RUN_DEFAULT);
    printf("script all done\n");
    uv_thread_join(&thread1);
    return 0;
}
