#include "includes/mon_fs.h"
#include <signal.h>

struct event_mon *MON = NULL; 
char BASE_DIR[] = "/tmp/inotify_test";


static void cleanup(int sig){
    printf("\nCaught signal '%d', cleaning up and exiting\n", sig);
    destroy_event_monitor(MON);
    exit(0);
}


int main( )
{
    if (!mon_dir_exists(BASE_DIR)){
        fprintf(stderr, "Base dir does not exist. Try: 'mkdir %s'\n", BASE_DIR);
        exit(1);
    }  
    signal(SIGINT, cleanup);
    /*creating the INOTIFY instance*/
     
    /*checking for error*/
    MON = create_event_monitor(BASE_DIR, 0 /*mask*/, 1 /*recursive*/, event_handler_default, 0 /*use default size*/); 
    if (!MON){
        fprintf(stderr, "Error creating event mon, bailing...!\n");
        exit(1);
    }
    if (monitor_init(MON)){
        fprintf(stderr, "Error during monitor init!\n");
        MON = destroy_event_monitor(MON);
        exit(1);
    }
    start_monitor_loop(MON);
    printf("monitor loop has ended, cleaning up\n"); 
    cleanup(0);
    return 0;
}

