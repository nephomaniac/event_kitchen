#include <stdio.h>
#include <stdlib.h>
//#include <unistd.h>
#include <uv.h>

/*
void signal_handler(uv_signal_t *handle, int signum){
    printf("Signal received: %d, tearing things down...\n", signum);
    uv_signal_stop(handle);
    exit(0);
}*/

void cleanup(int signum){
    printf("Signal received: %d, tearing things down...\n", signum);
    exit(0);
}

void uv_timeout_cb(uv_timer_t *handle){
    //char *buf = (char *) uv_handle_get_data(handle);
    char *buf = (char *) handle->data;
    printf("uv_timeout_cb fired. Handle has data:'%s'\n", buf ?: "");
}

int main(){

    char words[] = "This is my data!";

    uv_loop_t *loop = malloc(sizeof(uv_loop_t));

    /* First add the sig handlers - warning seg faults on init need to pull in newer libuv with fix 
    uv_signal_t sig;
    uv_signal_init(loop, &sig);
    uv_signal_start(&sig, signal_handler, SIGINT);
    */
    signal(SIGINT, cleanup);
    // Now add the timers 
    uv_timer_t timeout_watcher;
    //uv_handle_set_data(&timeout_watcher, words);
    timeout_watcher.data = words;
    uv_timer_init(loop, &timeout_watcher);
    
    uv_timer_start(&timeout_watcher, uv_timeout_cb, 50, 50);



}
