#include "includes/mon_fs.h"
#include "includes/mon_utils.h"
#include <signal.h>

struct fs_event_manager *MGR = NULL; 
char BASE_DIR[] = "/tmp/inotify_test";


static void cleanup(int sig){
    LOGDEBUG("\nCaught signal '%d', cleaning up and exiting\n", sig);
    destroy_event_monitor(MGR);
    exit(0);
}


int main( )
{
    set_local_debug_enabled(1);
    if (!mon_dir_exists(BASE_DIR)){
        LOGERROR("Note:Base dir does not exist, yet. %s'\n", BASE_DIR);
    }  
    signal(SIGINT, cleanup);
    /*creating the INOTIFY instance*/
     
    /*checking for error*/
    MGR = create_event_monitor(BASE_DIR, 0 /*mask*/, 1 /*recursive*/, example_event_handler, 0 /*use default size*/); 
    if (!MGR){
        LOGERROR("Error creating event mon, bailing...!\n");
        exit(1);
    }
    if (monitor_init(MGR)){
        LOGERROR("Error during monitor init!\n");
        MGR = destroy_event_monitor(MGR);
        exit(1);
    }
    start_monitor_loop_example(MGR);
    LOGDEBUG("monitor loop has ended, cleaning up\n"); 
    cleanup(0);
    return 0;
}

