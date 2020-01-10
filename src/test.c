#include "includes/mon_fs.h"

struct event_mon *MON = NULL; 
char BASE_DIR[] = "/tmp/inotify_test"


static void cleanup(int sig){
    printf("\nCaught signal '%d', cleaning up and exiting\n", sig);
    
    destroy_event_mon(MON);
    exit(0);
}


int main( )
{
    int ret;
    int cnt=0;
    if (!mon_dir_exists(BASE_DIR)){
        fprintf(stderr, "Base dir does not exist. Try: 'mkdir %s'\n", BASE_DIR);
        exit(1);
    }  
    signal(SIGINT, cleanup);
    /*creating the INOTIFY instance*/
     
    /*checking for error*/
    struct event_mon *mon = create_event_monitor(base_path=BASE_DIR,
                                                 mask=0, //Use defaults ie: (IN_CREATE | IN_DELETE | IN_MODIFY)
                                                 recursive=1,
                                                 handler=event_handler_default,
                                                 event_buf_len=0, //use default size
                                                 ); 
    if (!mon){
        fprintf("Error creating event mon, bailing...!\n");
        exit(1);
    }
    if (start_event_monitor(mon)){
        fprintf("Error starting event monitor!\n");
        destroy_event_mon(mon);
        exit(1);
    }
    start_monitor_loop(mon); 
    cleanup(0);
}

