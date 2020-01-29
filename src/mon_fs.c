#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <jansson.h>
#include "includes/mon_fs.h"
#include "includes/mon_utils.h"

/* POC to show how inotify events can be used to monitor a directory and dynamically + recursively add/remove triggers
 * on the files and child directories. 
 * Other eventing libraries provide a subset of the inotify events, and some notion of recursive discovery, but
 * they were found to be incomplete and/or buggy during testing. 
 * This is intended to be used with existing event libs, and/or in a byob loop provided by the consumer.  
 */ 



/*
 Following are the available inotify events:
 
 IN_ACCESS – File was accessed
 IN_ATTRIB – Metadata changed (permissions, timestamps, extended attributes, etc.)
 IN_CLOSE_WRITE – File opened for writing was closed
 IN_CLOSE_NOWRITE – File not opened for writing was closed
 IN_CREATE – File/directory created in watched directory
 IN_DELETE – File/directory deleted from watched directory
 IN_DELETE_SELF – Watched file/directory was itself deleted
 IN_MODIFY – File was modified
 IN_MOVE_SELF – Watched file/directory was itself moved
 IN_MOVED_FROM – File moved out of watched directory
 IN_MOVED_TO – File moved into watched directory
 IN_OPEN – File was opened
 */


void debug_show_list(struct w_dir *list){
    struct w_dir *wlist = list;
    int cnt = 0;
    struct w_dir *ptr = wlist;
    //LOGERROR("---- WATCHED DIRS:\n");
    LOGDEBUG("---- WATCHED DIRS:\n");
    if (!wlist){
        //LOGERROR("\tWATCH_LIST EMPTY\n");
        LOGDEBUG("\tWATCH_LIST EMPTY\n");
    }else{
        while(ptr != NULL) {
            //LOGERROR("\tLIST[%d] = PATH:'%s', WD:'%d', mask:'0x%lx', ptr:'%p', next:'%p'\n", 
            //    cnt,  ptr->path, ptr->wd, (unsigned long)ptr->mask, ptr, ptr->next);
            LOGDEBUG("\tLIST[%d] = PATH:'%s', WD:'%d', mask:'0x%lx', ptr:'%p', next:'%p'\n", 
                cnt,  ptr->path, ptr->wd, (unsigned long)ptr->mask, ptr, ptr->next);
            ptr = ptr->next;
            cnt++;
        }
    }
    LOGDEBUG("---- END WATCHED DIRS (%d) -----\n", cnt);
    return;
}


/* Starts the inotify monitor, adds the base dir to be monitored, as well as 
 * any recursively discovered sub dirs. 
 * If monitor_init() returns 0 then mon->ifd can be used to read in inotify events. 
 */
int monitor_init(struct fs_event_manager *mon){
    if (!mon){
        LOGERROR("Null event mon passed to monitor_init\n");
        return -1;
    }
    if (!mon->base_path || !strlen(mon->base_path)){
        LOGERROR("Empty basepath provided to create event monitor\n");
        return -1;
    }
    if (mon->needs_destroy){
        LOGERROR("Monitor marked for destroy, not doing init:'%s'\n", mon->base_path);
        return -1;
    }
    if (mon->ifd >= 0){
        LOGERROR("Monitor inotify instance already assigned for mon:'%s'\n", mon->base_path);
    }else{
        // Create the inotify watch instance
        int ifd = inotify_init();
        /*checking for error*/
        if (ifd < 0 ) {
            LOGERROR("inotify_init error for path:'%s'\n", mon->base_path);
            return -1; 
        }
        mon->ifd = ifd;
    }
    // Add the base dir to the monitor, this initializes the watch_list with this w_dir 
    if (!mon_dir_exists(mon->base_path)){
        // If dir does not exist and this monitor has the restore flag set, we can mkdir here...
        if (mon->restore_base_dir){
            LOGERROR("Base dir not found doing mkdir('%s')\n", mon->base_path);
            mkdir(mon->base_path, mon->base_mode);
        }else{
            LOGERROR("Error. Dir does not exist and restore_dir not set: '%s'\n", mon->base_path);
            return -1;
        }
    }else{
        // Grab the original dir mode if it exists in case it needs to be recreated later
        struct stat st;
        if(stat(mon->base_path, &st) == 0){
            mon->base_mode = st.st_mode;
        }
    }
    struct w_dir *wdir = monitor_dir(mon->base_path, mon);
    if (!wdir){
        LOGERROR("Error adding base dir to event monitor:'%s'\n", mon->base_path);
        close(mon->ifd);
        mon->ifd = -1;
        return -1; 
    }
    // Assign the base watch descriptor so there's a starting reference point, 
    // and we dont accidently delete it later
    mon->base_wd = wdir->wd;
    // our list of watched dirs starts with this  base dir...
    if (!mon->watch_list){
        mon->watch_list = wdir;
    }
    
    return 0;  
}

static int _stop_loop_callback(struct fs_event_manager *mon){
    LOGDEBUG("Stopping loop for mon base dir:'%s'\n", mon->base_path ?: "");
    return 1;
}

void stop_monitor_loop(struct fs_event_manager *mon){
    LOGDEBUG("Stopping loop, setting loopctl\n");
    mon->loopctl = _stop_loop_callback; 
}

int start_monitor_loop_example(struct fs_event_manager *mon){
    int cnt = 0;
    if (!mon || !mon->handler){
       LOGERROR("Err starting mon loop. Mon null:'%s', mon->handler null:'%s'\n", 
                mon ? "Y":"N", mon->handler ? "Y":"N"); 
        return -1;
    }
    LOGDEBUG("Start Monitor Loop with following dirs...\n");
    debug_show_list(mon->watch_list);
    for (;;){
        if (mon->needs_destroy){
            LOGDEBUG("monitor marked as needs_destroy ending loop\n");
            break;
        } else if (mon->loopctl){
            if (mon->loopctl(mon) != 0) {
                LOGDEBUG("loopctl exited non-zero, ending loop\n");
                break;
            }
        } else {
            if (mon_fd_has_events(mon->ifd, mon->interval, 0)){
                LOGDEBUG("<<< start loop %d handlers >>>\n", cnt);
                read_events_fd(mon->ifd, mon->event_buffer, mon->buf_len, mon->handler, mon);
                LOGDEBUG("<<< end loop %d handlers >>>\n", cnt);
                cnt++;
            }else{
                //printf("NO EVENTS DETECTED during interval\n");
                //debug_show_list(mon->watch_list);
            }
        }  
    }  
    return 0;
}

/*remove monitors and rebuild from the base dir up */
int reset_monitor(struct fs_event_manager *mon){
    if (!mon){
        LOGERROR("Was passed a null mon, bug.\n");
        return -1;  
    }
    if (mon->needs_destroy){
        LOGERROR("Monitor marked for destroy, not restoring base dir:'%s'\n", mon->base_path ?: "");
        return -1;
    }    
    LOGERROR("RESETING MONITOR for:'%s'\n", mon->base_path ?: "");
    mon->base_wd = -1;
    destroy_wdir_list(mon);
    if (mon->ifd >= 0){
        close(mon->ifd);
        mon->ifd = -1;
    } 
    monitor_init(mon); 
    return 0; 
}

/* Create/allocate new event monitor instance
 * Monitor will need to be started with monitor_init() afterwards 
 * To be free'd by caller
 */ 
struct fs_event_manager *create_event_monitor(char *base_path, 
                                       uint32_t mask,
                                       int recursive, 
                                       event_handler handler, 
                                       size_t event_buf_len){
    struct fs_event_manager *mon = NULL;
    int ifd = -1;
    char *mon_base_path = NULL;
    if (!base_path || !strlen(base_path)){
        LOGERROR("Empty basepath provided to create event monitor\n");
        return NULL;
    }
    mon_base_path = strdup(base_path);
    if (!mon_base_path){
        LOGERROR("Failed to alloc base_path during create event monitor:'%s'\n", base_path);
        return NULL;
    } 
    // Calc this monitor instance's event buffer size
    size_t buflen = event_buf_len;
    if (buflen >= INOT_EVENT_SIZE + 16 ){
        buflen = (event_buf_len * ( INOT_EVENT_SIZE + 16 ));
    }else{
        buflen = (size_t) INOT_DEFAULT_EVENT_BUF_LEN; 
    }
    // Allocate the monitor instance + it's event buffer...
    mon = calloc(1, sizeof(struct fs_event_manager) + buflen);
    if (!mon){
        LOGERROR("Error allocating new event monitor!\n");
        close(ifd); 
        return NULL;
    }
    // Init the monitors lock just in case this is used in a threaded app some day...
    if (pthread_mutex_init(&mon->lock, NULL) != 0) { 
        LOGERROR("Mutex lock init has failed. Base dir:'%s'\n", base_path);
        mon = destroy_event_monitor(mon);
        return NULL; 
    }
    // Set the event_monitor instance's starting values. See header for more info... 
    mon->buf_len = buflen;;
    mon->ifd = -1;
    mon->base_wd = -1;
    mon->config_wd = -1;
    mon->recursive = 1;
    mon->restore_base_dir = 1;
    mon->base_mode = S_IRWXU | S_IRGRP;
    mon->interval = 1;
    if (mask){
        mon->mask = mask;
    }else{
        //mon->mask = IN_CREATE | IN_DELETE | IN_MODIFY ;
        //mon->mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_DELETE_SELF | IN_MOVE_SELF;
        mon->mask = IN_ALL_EVENTS;
    }
    mon->recursive = recursive;
    mon->handler = handler;
    mon->loopctl = NULL;
    mon->base_path = mon_base_path;
    mon->jconfig = NULL;
    mon->thread_id = NULL;
    mon->watch_list = NULL;
    
    return mon;
}

// Remove and free all the watch dirs 
struct w_dir *destroy_wdir_list(struct fs_event_manager *mon){
    LOGDEBUG("Destroy watch list start\n");
    struct w_dir *ptr = mon->watch_list;
    struct w_dir *cur = mon->watch_list;
    while (ptr != NULL){
        cur = ptr;
        ptr = ptr->next;
        // remove() also frees the w_dir
        remove_watch_dir(cur, mon);
    }
    LOGDEBUG("Done with destroy. List should be empty...\n");
    debug_show_list(mon->watch_list);
    return ptr;
}

/* Destroy and free an event_mon instance. 
 * returns null to allow assignment by caller. 
    (From http://man7.org/linux/man-pages/man7/inotify.7.html)
    When all file descriptors referring to an inotify instance have
    been closed (using close(2)), the underlying object and its
    resources are freed for reuse by the kernel; all associated
    watches are automatically freed.
*/
struct fs_event_manager *destroy_event_monitor(struct fs_event_manager *mon){
    if (!mon){
        LOGERROR("destroy_event_monitor provided a null monitor\n");
        return NULL;
    }
    LOGDEBUG("Destroying mon path:'%s', list:'%p'\n", mon->base_path ?:"", mon->watch_list ?: 0); 
    pthread_mutex_lock(&mon->lock);
    mon->needs_destroy = 1;
    stop_monitor_loop(mon);
    // Close our inotify event fd
    if (mon->ifd >= 0){
        close(mon->ifd);
    }
    // decrement ref to monitor's json obj(s)
    if (mon->jconfig){
        json_decref(mon->jconfig);
    }
    LOGDEBUG("Destroy removing the following watched dirs...\n"); 
    debug_show_list(mon->watch_list);
   
    // Remove and free all the watch dirs 
    mon->watch_list = destroy_wdir_list(mon);
    if (mon->thread_id){
        pthread_join (*mon->thread_id, NULL);
    } 
    pthread_mutex_destroy(&mon->lock);
    if (mon->base_path){
        free(mon->base_path);
        mon->base_path = NULL;
    } 
    free(mon);
    return NULL;
}

/* Create/allocate new watch dir.  
 * To be free'd by caller
 */
struct w_dir * create_watch_dir(char *dpath, struct fs_event_manager *mon){
    int inotify_fd;
    uint32_t mask;
    if (!dpath || !strlen(dpath)){
        LOGERROR("Null dir name provided\n");
        return NULL;
    }
    if (!mon){
        LOGERROR("Null event monitor provided\n");
        return NULL;
    }
    inotify_fd = mon->ifd;
    mask = mon->mask; //IN_CREATE | IN_DELETE | IN_MODIFY
    if (inotify_fd < 0){
        LOGERROR("Bad inotify instance fd provided:'%d'\n", inotify_fd);
        return NULL;
    }
    // Add dir path to our watcher
    int wd = inotify_add_watch( inotify_fd, dpath, mask);
    if (wd < 0){
        LOGERROR("Could not add watcher for path:'%s', instance fd:'%d'\n", dpath ?: "", inotify_fd);
        return NULL;
    }
    struct w_dir *newd = calloc(1, sizeof(struct w_dir) + strlen(dpath));
    if (newd != NULL){
        newd->wd = wd;
        newd->mask = mask;
        newd->ifd = inotify_fd;
        newd->evt_mon = mon;
        newd->next = NULL;
        strcpy(newd->path, dpath);
    }
    return newd;
}

/* Fetch w_dir with matchng watch descriptor attribute from provided w_dir list */
struct w_dir * get_dir_by_wd(int wd, struct fs_event_manager *mon){
    if (!mon || !mon->watch_list){
        return NULL;;
    }
    struct w_dir *ptr = mon->watch_list;
    while(ptr != NULL) {
        if (ptr->wd == wd){
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}

/* Fetch w_dir with matching 'path' from provided w_dir list */
struct w_dir * get_dir_by_path(char *path, struct fs_event_manager *mon){
    if (!mon || !mon->watch_list){
        return NULL;
    }
    struct w_dir *ptr = mon->watch_list;
    while(ptr != NULL) {
        if (ptr->path && !strcmp(ptr->path, path)){
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}


// Create mapping for dir to watch descriptor and add to the event_monitor list...
struct w_dir *add_watch_dir_to_monitor(char *dpath, struct fs_event_manager *mon){
    if (!mon){
        LOGERROR("Was provided null event_mon\n");
        return NULL;
    }
    struct w_dir *wdir = NULL;
    if (!dpath || !strlen(dpath)){
        LOGERROR("Null dir name provided\n");
        return NULL;
    }
    
    struct w_dir *ptr = NULL;;
    if (!mon->watch_list){
        //printf("Adding first item in list! ('%s')\n", dpath);
        mon->watch_list = create_watch_dir(dpath, mon);
        //debug_show_list(mon->watch_list);
        return mon->watch_list;
    }else{
        wdir = get_dir_by_path(dpath, mon);
        if (wdir){
            LOGDEBUG("Dir already in watchlist:'%s', wd:'%d'\n", wdir->path, wdir->wd);
            return wdir;
        }
    }
    //Create a new item and add it to the end of the list...
    wdir = create_watch_dir(dpath, mon);
    if (!wdir){
        LOGERROR("Failed to creat new wdir for path:'%s'\n", dpath);
    }else{
        LOGDEBUG("Inserting element to watch list: path:'%s', wd:'%d'\n", wdir->path, wdir->wd);
        ptr = mon->watch_list;
        while(ptr != NULL) {
            if (!ptr->next){
                ptr->next = wdir;
                break;
            }
            ptr = ptr->next;
        }
    }
    //debug_show_list(mon->watch_list);
    return wdir;
}

/*'if' found in list, removes w_dir from list, free's w_dir */ 
int remove_watch_dir(struct w_dir *wdir, struct fs_event_manager *mon){
    int base_removed = 0;
    if (!mon){
        LOGERROR("Null monitor provided to remove_remove_watch_dir()\n");
        return -1;
    }
    struct w_dir *wlist = mon->watch_list;
    if (!wdir){
        LOGERROR("Null dir provided to remove_watch_dir()\n");
        return -1;
    }
    if (!wlist){
        LOGERROR("Empty list provided\n");
        return -1;
    }
    struct w_dir *cur = wlist;
    struct w_dir *last = NULL;
    LOGDEBUG("Attempting to remove wdir->path:'%s', wdir:'%p', next:'%p', list:'%p'\n",
                 wdir->path ?: "", wdir, wdir->next ?: 0, wlist ?: 0);
    int cnt = 0;
    while(cur != NULL) {
        if (cur == wdir){
            if ((mon->base_wd) >= 0 && (wdir->wd == mon->base_wd)) {
                base_removed = 1;
            }
            LOGDEBUG("Found wdir to remove at position:'%d'\n", cnt);
            if (last){
                last->next = cur->next;
            }else{
                mon->watch_list = cur->next;
            }
            if (mon->ifd >= 0){
                inotify_rm_watch( mon->ifd, wdir->wd);
            }
            if (cur){
                free(cur);
                cur = NULL;
            }
            break;
        }else{
            last = cur;
            cur = cur->next;
            cnt++;
        }
    }
    if (base_removed){
        LOGERROR("Deleted base dir:'%s'\n", mon->base_path ?: ""); 
        if (!mon->needs_destroy){
            reset_monitor(mon);
        }
    }
    LOGDEBUG("Done removing wdir, list after...\n");
    debug_show_list(mon->watch_list);
    return -1;
}

int remove_watch_dir_by_path(char *path, struct fs_event_manager *mon){
    if (!mon){
        LOGERROR("Empty mon provided\n");
        return -1;
    }
    struct w_dir *wdir = get_dir_by_path(path, mon);
    if (!wdir){
        LOGERROR("Could not find watched dir by path:'%s'\n", path ?: "");
        debug_show_list(mon->watch_list);
        return -1;
    }
    return remove_watch_dir(wdir, mon);
}


/* Finds parent directory using w_dir list mappings to build current event's
 * full path. 
 * Returned buffer must be free'd later by caller
 */ 
char *create_wd_full_path(int wd, char *name, struct fs_event_manager *mon){
    char *ret = NULL;
    struct w_dir *wdir = get_dir_by_wd(wd, mon);
    if (!wdir){
        LOGERROR("Failed to find wd:'%d' for full path. name:'%s'\n", wd, name ?: "");
        debug_show_list(NULL);
        return NULL;
    }
    size_t total = strlen(wdir->path) + strlen(name) + 2;
    ret = malloc(total);
    if (!ret){
        LOGERROR("Failed to alloc full path for wd\n");
        return NULL;
    }
    snprintf(ret, total, "%s%s%s", wdir->path, name ? "/" : "",  name ?: "");
    return ret;
}


/* Adds the current dir 
 *  if mon->recursive flag is set, then subdirectories will automatically be 
 *  discoverd and added recursively. 
 */
struct w_dir *monitor_dir(char *dpath, struct fs_event_manager *mon){
    DIR *folder;
    char subdir[512];
    struct dirent *entry;
    struct stat filestat;
    struct w_dir *wdir = NULL;
    if (!dpath || !strlen(dpath)){
        LOGERROR("Null dir name provided\n");
        return NULL;
    }
    if (!mon){
        LOGERROR("Null event monitor  provided\n");
        return NULL;
    }
    folder = opendir(dpath);
    if(folder == NULL){
        LOGERROR("Unable to read directory:'%s'\n", dpath);
        return NULL;
    }
    wdir = add_watch_dir_to_monitor(dpath, mon);
    if (!wdir) {
        LOGERROR("Error, adding Dir to watchlist:'%s'\n",dpath);
    }else{
        LOGDEBUG("Added Dir to watchlist:'%s', wd:'%d'\n", wdir->path, wdir->wd);
    }
    if (!mon->recursive){
        // No need to recursively discover and add sub dirs, return this w_dir now...
        return(wdir);
    }
    /* Read directory entries */
    entry=readdir(folder);
    while(entry)
    {
        if (!entry->d_name || !strcmp(".", entry->d_name) || !strcmp("..", entry->d_name)){
            //printf("skipping dir:'%s'\n", entry->d_name);
        }else{
            snprintf(subdir, sizeof(subdir), "%s/%s", dpath, entry->d_name);
            stat(subdir, &filestat);
            if( S_ISDIR(filestat.st_mode)) {
                LOGDEBUG("Found sub dir for path:'%s'\n", entry->d_name);
                if (!monitor_dir(subdir, mon)){
                    LOGERROR("Error adding dir to watchlist:'%s'\n", entry->d_name);
                };
            }
        }
        entry=readdir(folder);
    }
    closedir(folder);
    return(wdir);
}


/* Sample function to be used with an external loop to 
 * show how select/poll could be used to wait for the inotify fd 
 * to avoid blocking on read, etc.. 
 */
int mon_fd_has_events(int fd, float sec, float usec){
    struct timeval time;
    fd_set rfds;
    int ret;
    
    /* timeout after five seconds */
    if (sec <= 0 && usec <= 0){
        LOGERROR("Warning invalid interval. Sec:'%f' Usec:'%f'. Setting to 1 sec \n", sec, usec);
        sec = 1;
    }
    time.tv_sec = sec;
    time.tv_usec = usec;
    
    /* zero-out the fd_set */
    FD_ZERO (&rfds);
    
    /*
     * add the inotify fd to the fd_set -- of course,
     * your application will probably want to add
     * other file descriptors here, too
     */
    FD_SET (fd, &rfds);
    
    ret = (select (fd + 1, &rfds, NULL, NULL, &time));
    if (ret < 0){
        perror ("select");
        return 0;
    } else if (!ret) {
        /* timed out! */
        return 0;
    } else if (FD_ISSET (fd, &rfds)){
        return 1;
    }
    
    return 0;
}


int example_event_handler(struct inotify_event *event, void *data){
    if (!data){
        LOGERROR("Null data passed to handle data\n");
        return -1;
    }
    char *fname = NULL;
    struct w_dir *wdir = NULL;
    struct fs_event_manager *mon = data;
    char fpath[256];
    fpath[0] = '\0';
    if (event){
        if (event->len && event->name){
            // Check our mappings to derive the full path of this file/dir from the event
            fname = create_wd_full_path(event->wd, event->name, mon);
        }
        if ( event->mask & IN_CREATE ) {
            if ( event->mask & IN_ISDIR ) {
                LOGDEBUG( "MONITOR: New directory '%s' created.\n", fname ?: "");
                // If recursive is set, automatically add this new subdir 
                if (mon->recursive){
                    monitor_dir(fname, mon);
                }
            } else {
                if ( event->mask & IN_MODIFY){
                    LOGDEBUG( "MONITOR: File '%s' was created and modified\n", fname ?: "");
                    return 0;
                }
                if (event->cookie){
                    LOGDEBUG( "MONITOR: New file '%s' created. Cookie set so heads up on paired event:'%lu'\n", fname ?: "", (unsigned long) event->cookie);
                }else{
                    LOGDEBUG( "MONITOR: New file '%s' created.\n", fname ?: "" );
                }
            }
        } else if ( event->mask & IN_DELETE) {
            if ( event->mask & IN_ISDIR ) {
                LOGDEBUG( "MONITOR: Directory '%s' deleted. Removing wd:'%d' from watchlist\n", fname ?: "", event->wd);
                /* Remove the dir from the watch list...
                 * The dir path will need to be derived from the mapped w_dir in order
                 * to get the wd to remove from inotify mon_fd */
                if (event->name && strlen(event->name)){
                    wdir = get_dir_by_wd(event->wd, mon);
                    if (wdir && wdir->path && strlen(wdir->path)){
                        snprintf(fpath, sizeof(fpath), "%s/%s", wdir->path, event->name);
                        remove_watch_dir_by_path(fpath, mon);
                    }

                }
            } else {
                LOGDEBUG( "MONITOR: File '%s' deleted.\n", fname ?: "" );
            }
        }else if ( event->mask & IN_MODIFY){
            LOGDEBUG( "MONITOR: File '%s' was modified\n", fname ?: "");
        }else if (event->mask & IN_DELETE_SELF){
            wdir = get_dir_by_wd(event->wd, mon);
            LOGDEBUG("MONITOR: Watcher dir was deleted:'%s'\n", wdir->path ?: "");
            if (wdir){ 
                remove_watch_dir(wdir, mon); 
            }
        }
    }
    if (fname){
        free(fname);
    }
    return 0;
}


/* Reads events from inotify fd and calls the provided handler func to process them. 
   ! This read blocks until the change event occurs, use select of poll to make sure
 * the fd is read ready
 * An optional handler can be provided. If the handler returns non-zero this will 
   exit the loop, and the result of the handler is returned. 
   Returns zero on success. 
 */
int read_events_fd(int events_fd, char *buffer, size_t buflen, event_handler handler, void *data){
    int length = 0; 
    int i = 0;
    struct inotify_event *event = NULL;
    if (events_fd < 0 ){
        LOGERROR("read_events_fd passed invalid fd:'%d'\n", events_fd);
        return -1;
    }
    if (!buffer || buflen <= 0 ){
        LOGERROR("Passed null buffer:'%s', or invalid length:'%zu'\n", buffer ? "Y" : "N",  buflen);
        return -1;
    } 
    length = read(events_fd, buffer, buflen);
    if ( length < 0 ) {
        LOGERROR("Error reading event fd\n");
        return length; 
    }
    if (!handler){
        // Since this is just an example loop, drain the fd and return...
        return length;
    }
    size_t remaining = length; 
    /*actually read return the list of change events happens. Here, read the change event one by one and process it accordingly.*/
    while ( i < length ) {
        remaining = length - i;
        // This is just a sample, but this can be refactored to better handle truncation
        if (remaining < INOT_EVENT_SIZE){
            LOGERROR("Truncated event! Remaining:'%lu', sizeof(event):'%lu'\n",
                    (unsigned long)remaining, (unsigned long)INOT_EVENT_SIZE); 
            break;
        }
        event = ( struct inotify_event * ) &buffer[ i ];
        if (remaining < (INOT_EVENT_SIZE + event->len)){
            LOGERROR("Truncated event! Remaining:'%lu', sizeof(event):'%lu', len:'%lu'\n",
                    (unsigned long)remaining, (unsigned long)INOT_EVENT_SIZE, (unsigned long)event->len); 
            break;

        }
        // Show some debug info about the event
        print_event(event);
        // if a handler was provided, call it here...
        if (handler(event, data)){
            break;
        }
        i += INOT_EVENT_SIZE + event->len;
    }
    return length;
    
}


/* print mask attributes to provided buffer. Return length written. */
void print_event(struct inotify_event *event){
    uint32_t mask = event->mask;
    char buf[256];
    char *ptr = buf;
    size_t buflen = sizeof(buf);
    LOGDEBUG("---------------------------------------------------\n");
    if (!event){
        LOGDEBUG("Passed null event to print!\n");
        LOGDEBUG("---------------------------------------------------\n");
        return;
    }
    LOGDEBUG("\tEVENT: name:'%s'\n", event->name ?: "");
    LOGDEBUG("\tEVENT: mask:'0x%lx', cookie:'%d', wd:'%d', len:%lu\n",
               (unsigned long)event->mask, event->cookie, event->wd,  (unsigned long)event->len);
    if (mask == 0){
        LOGDEBUG("\tMASK: NONE\n");   
        LOGDEBUG("---------------------------------------------------\n");
        return; 
    }
    if (mask & IN_ACCESS){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_ACCESS");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_MODIFY){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_MODIFY");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_ATTRIB){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_ATTRIB");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_CLOSE_WRITE){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_CLOSE_WRITE");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_CLOSE_NOWRITE){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_CLOSE_NOWRITE");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_CLOSE){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_CLOSE");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_OPEN){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_OPEN");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_MOVED_FROM){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_MOVED_FROM");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_MOVED_TO){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_MOVED_TO");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_MOVE){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_MOVE");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_CREATE){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_CREATE");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_DELETE){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_DELETE");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_DELETE_SELF){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_DELETE_SELF");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_MOVE_SELF){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_MOVE_SELF");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_UNMOUNT){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_UNMOUNT");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_Q_OVERFLOW){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_Q_OVERFLOW");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_IGNORED){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_IGNORED");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_CLOSE){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_CLOSE");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_MOVE){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_MOVE");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_ONLYDIR){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_ONLYDIR");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_DONT_FOLLOW){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_DONT_FOLLOW");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_MASK_ADD){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_MASK_ADD");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_ISDIR){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_ISDIR");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    if (mask & IN_ONESHOT){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_ONESHOT");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }
    /*if (mask & IN_ALL_EVENTS){
         ptr += snprintf(ptr, (buflen-strlen(buf)), "%s,", "IN_ALL_EVENTS");
         if (strlen(buf)+1 >= buflen){
              return; //strlen(buf);
         }
    }*/
    LOGDEBUG("\tMASK: %s\n", buf);   
    LOGDEBUG("---------------------------------------------------\n");
    return;
}
