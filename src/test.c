static void cleanup(int sig){
    printf("\nCaught signal '%d', cleaning up and exiting\n", sig);
    if (mon_fd < 0){
        return;
    }  
    /*closing the INOTIFY instance*/
    close( mon_fd ); 
    mon_fd = -1;
    /*removing the directories from the watch list.*/
    /**if (watch_base >= 0){
     inotify_rm_watch( mon_fd, watch_base );
     }*/
    struct w_dir *ptr = _WATCH_LIST;
    struct w_dir *cur = _WATCH_LIST;
    if (ptr){
        while (ptr != NULL){
            cur = ptr;
            ptr = ptr->next;
            remove_watch_dir(cur, _WATCH_LIST, 1);
        }  
    }  
    exit(0);
}


int main( )
{
    int ret;
    int cnt=0;
    if (!dir_exists(BASE_DIR)){
        fprintf(stderr, "Base dir does not exist. Try: 'mkdir %s'\n", BASE_DIR);
        exit(1);
    }  
    signal(SIGINT, cleanup);
    /*creating the INOTIFY instance*/
    mon_fd = inotify_init();
    
    /*checking for error*/
    if ( mon_fd < 0 ) {
        perror( "inotify_init" ); 
    }  
    struct event_mon *mon = create_event_monitor(); 
    struct w_dir *wdir= monitor_watch_dir(BASE_DIR, mon);
    if (!wdir){
        perror("watch list errror!");
        exit (1);
    }  
    watch_base = wdir->wd;
    for (;;){
        if (has_changed(mon_fd, .5, 0)){
            printf("-- start loop %d --\n", cnt);
            read_events_fd(mon_fd);
            printf("-- end loop %d --\n", cnt);
            cnt++;
        }
    }
    cleanup(0);
}

