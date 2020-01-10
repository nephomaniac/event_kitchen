/* This is the sample program to notify us for the file creation and file deletion takes place in “/tmp” directory
 * From: https://www.thegeekstuff.com/2010/04/inotify-c-program-example/  */
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

/*Global vars*/
struct w_dir *_WATCH_LIST = NULL;
int mon_fd = -1;
int watch_base = -1;
char fpath[256]; 

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
    if (!wlist){
        wlist = _WATCH_LIST;
    }
    int cnt = 0;
    struct w_dir *ptr = wlist;
    fprintf(stderr, "---- WATCHED DIRS:\n");
    while(ptr != NULL) {
        fprintf(stderr, "WATCH_LIST[%d] = PATH:'%s', WD:'%d', mask:'0x%lx', ptr:'%p', next:'%p'\n", 
            cnt,  ptr->path, ptr->wd, (unsigned long)ptr->mask, ptr, ptr->next);
        ptr = ptr->next;
        cnt++;
    }
    fprintf(stderr, "---- END WATCHED DIRS -----\n");
    return;
}



json_t *json_from_file(char *path){
    json_t *json = NULL;
    json_error_t error;
    if (!path || !strlen(path)){
        fprintf(stderr, "Empty config path provided to parse_config\n");
        return NULL;
    }
    json = json_load_file(path, 0, &error);
    if(!json) {
        fprintf(stderr, "Error parsing config:'%s'. Error:'%s'\n", path, error.text ?: "");
    }
    return json;
}



/* Starts the inotify monitor, adds the base dir to be monitored, as well as 
 * any recursively discovered sub dirs. 
 * If monitor_init() returns 0 then mon->ifd can be used to read in inotify events. 
 */
int monitor_init(struct event_mon *mon){
    if (!mon){
        fprintf(stderr, "Null event mon passed to monitor_init\n");
        return -1;
    }
    if (!mon->base_path || !strlen(mon->base_path)){
        fprintf(stderr, "Empty basepath provided to create event monitor\n");
        return -1;
    }
    if (mon->ifd >= 0){
        fprintf(stderr, "Monitor inotify instance already assigned for mon:'%s'\n", mon->base_path);
        return -1;
    }
    
    // Create the inotify watch instance
    int ifd = inotify_init();
    /*checking for error*/
    if (ifd < 0 ) {
        fprintf(stderr, "inotify_init error for path:'%s'\n", mon->base_path);
        return -1; 
    }
    mon->ifd = ifd;
    // Add the base dir to the monitor, this initializes the watch_list with this w_dir 
    struct w_dir *wdir = monitor_dir(mon->base_path, mon);
    if (!wdir){
        fprintf(stderr, "Error adding base dir to event monitor:'%s'\n", mon->base_path);
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

static int _stop_loop_callback(struct event_mon *mon){
    printf("Stopping loop for mon base dir:'%s'\n", mon->base_path ?: "");
    return 1;
}

void stop_monitor_loop(struct event_mon *mon){
    printf("Stopping loop, setting loopctl\n");
    mon->loopctl = _stop_loop_callback; 
}

int start_monitor_loop(struct event_mon *mon){
    int cnt = 0;
    if (!mon || !mon->handler){
       fprintf(stderr, "Err starting mon loop. Mon null:'%s', mon->handler null:'%s'\n", 
                mon ? "Y":"N", mon->handler ? "Y":"N"); 
        return -1;
    }
    printf("Start Monitor Loop with following dirs...\n");
    debug_show_list(mon->watch_list);
    for (;;){
        if (mon->needs_destroy){
            printf("monitor marked as needs_destroy ending loop\n");
            break;
        } else if (mon->loopctl){
            if (mon->loopctl(mon) != 0) {
                printf("loopctl exited non-zero, ending loop\n");
                break;
            }
        } else {
            if (mon_fd_has_events(mon->ifd, mon->interval, 0)){
                printf("-- start loop %d --\n", cnt);
                read_events_fd(mon->ifd, mon->event_buffer, mon->buf_len, mon->handler, mon);
                printf("-- end loop %d --\n", cnt);
                cnt++;
            }else{
                //printf("NO EVENTS DETECTED during interval\n");
                //debug_show_list(mon->watch_list);
            }
        }  
    }  
    return 0;
}


/* Create/allocate new event monitor instance
 * Monitor will need to be started with monitor_init() afterwards 
 * To be free'd by caller
 */ 
struct event_mon *create_event_monitor(char *base_path, 
                                       uint32_t mask,
                                       int recursive, 
                                       event_handler handler, 
                                       size_t event_buf_len){
    struct event_mon *mon = NULL;
    int ifd = -1;
    char *mon_base_path = NULL;
    if (!base_path || !strlen(base_path)){
        fprintf(stderr, "Empty basepath provided to create event monitor\n");
        return NULL;
    }
    mon_base_path = strdup(base_path);
    if (!mon_base_path){
        fprintf(stderr, "Failed to alloc base_path during create event monitor:'%s'\n", base_path);
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
    mon = calloc(1, sizeof(struct event_mon) + buflen);
    if (!mon){
        fprintf(stderr, "Error allocating new event monitor!\n");
        close(ifd); 
        return NULL;
    }
    // Init the monitors lock just in case this is used in a threaded app some day...
    if (pthread_mutex_init(&mon->lock, NULL) != 0) { 
        fprintf(stderr,"Mutex lock init has failed. Base dir:'%s'\n", base_path);
        mon = destroy_event_monitor(mon);
        return NULL; 
    }
    // Set the event_monitor instance's starting values. See header for more info... 
    mon->buf_len = buflen;;
    mon->ifd = -1;
    mon->base_wd = -1;
    mon->config_wd = -1;
    mon->recursive = 1;
    mon->interval = 1;
    if (mask){
        mon->mask = mask;
    }else{
        mon->mask = IN_CREATE | IN_DELETE | IN_MODIFY;
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

/* Destroy and free an event_mon instance. 
 * returns null to allow assignment by caller. */
struct event_mon *destroy_event_monitor(struct event_mon *mon){
    if (!mon){
        fprintf(stderr, "destroy_event_monitor provided a null monitor\n");
        return NULL;
    }
    printf("Destroying mon path:'%s', list:'%p'\n", mon->base_path ?:"", mon->watch_list ?: 0); 
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
    printf("Destroy removing the following watched dirs...\n"); 
    debug_show_list(mon->watch_list);
   
    // Remove and free all the watch dirs 
    struct w_dir *ptr = mon->watch_list;
    struct w_dir *cur = mon->watch_list;
    while (ptr != NULL){
        cur = ptr;
        ptr = ptr->next;
        // remove() also frees the w_dir
        remove_watch_dir(cur, mon->watch_list, 1);
    }
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
struct w_dir * create_watch_dir(char *dpath, struct event_mon *mon){
    int inotify_fd;
    uint32_t mask;
    if (!dpath || !strlen(dpath)){
        fprintf(stderr, "Null dir name provided\n");
        return NULL;
    }
    if (!mon){
        fprintf(stderr, "Null event monitor provided\n");
        return NULL;
    }
    inotify_fd = mon->ifd;
    mask = mon->mask; //IN_CREATE | IN_DELETE | IN_MODIFY
    if (inotify_fd < 0){
        fprintf(stderr, "Bad inotify instance fd provided:'%d'\n", inotify_fd);
        return NULL;
    }
    // Add dir path to our watcher
    int wd = inotify_add_watch( inotify_fd, dpath, mask);
    if (wd < 0){
        fprintf(stderr, "Could not add watcher for path:'%s', instance fd:'%d'\n", dpath ?: "", inotify_fd);
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
struct w_dir * get_dir_by_wd(int wd, struct w_dir *list){
    struct w_dir *wlist = list;
    if (!wlist){
        return NULL;;
    }
    struct w_dir *ptr = wlist;
    while(ptr != NULL) {
        if (ptr->wd == wd){
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}

/* Fetch w_dir with matching 'path' from provided w_dir list */
struct w_dir * get_dir_by_path(char *path, struct w_dir *list){
    struct w_dir *wlist = list;
    if (!wlist){
        wlist = _WATCH_LIST;
    }
    struct w_dir *ptr = wlist;
    while(ptr != NULL) {
        if (ptr->path && !strcmp(ptr->path, path)){
            return ptr;
        }
        ptr = ptr->next;
    }
    return NULL;
}

/*
struct w_dir * get_dir_by_path(char *path){
    return _get_dir_by_path(path, _WATCH_LIST);
}
*/

// Create mapping for dir to watch descriptor and add to the event_monitor list...
struct w_dir *add_watch_dir_to_monitor(char *dpath, struct event_mon *mon){
    if (!mon){
        fprintf(stderr, "Was provided null event_mon\n");
        return NULL;
    }
    struct w_dir *wdir = NULL;
    if (!dpath || !strlen(dpath)){
        fprintf(stderr, "Null dir name provided\n");
        return NULL;
    }
    
    struct w_dir *ptr = NULL;;
    if (!mon->watch_list){
        //printf("Adding first item in list! ('%s')\n", dpath);
        mon->watch_list = create_watch_dir(dpath, mon);
        //debug_show_list(mon->watch_list);
        return mon->watch_list;
    }else{
        wdir = get_dir_by_path(dpath, mon->watch_list);
        if (wdir){
            printf("Dir already in watchlist:'%s', wd:'%d'\n", wdir->path, wdir->wd);
            return wdir;
        }
    }
    //Create a new item and add it to the end of the list...
    wdir = create_watch_dir(dpath, mon);
    if (!wdir){
        fprintf(stderr, "Failed to creat new wdir for path:'%s'\n", dpath);
    }else{
        printf("Inserting  element to _WATCH_LIST: path:'%s', wd:'%d'\n", wdir->path, wdir->wd);
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
int remove_watch_dir(struct w_dir *wdir, struct w_dir *list, int allow_base){
    struct w_dir *wlist = list;
    if (!wdir){
        fprintf(stderr, "Null dir provided to remove()\n");
        return -1;
    }
    if (wdir->wd == wdir->base_wd && !allow_base){
        fprintf(stderr, "Asked to remove base dir but cant cuz allow_base set to:'%d' \n", allow_base);
        return -1;
    }
    if (!wlist){
        fprintf(stderr, "Empty list provided\n");
        return -1;
    }
    struct w_dir *cur = wlist;
    struct w_dir *last = NULL;
    printf("Attempting to remove wdir->path:'%s', wdir:'%p', next:'%p', list:'%p'\n",
                 wdir->path ?: "", wdir, wdir->next ?: 0, wlist ?: 0);
    int cnt = 0;
    while(cur != NULL) {
        if (cur == wdir){
            printf("Found wdir to remove at position:'%d'\n", cnt);
            if (last){
                last->next = cur->next;
            }
            if (mon_fd >= 0){
                inotify_rm_watch( mon_fd, wdir->wd);
            }
            free(cur);
            cur = NULL;
            printf("Done removing wdir, list after...\n");
            debug_show_list(_WATCH_LIST);
            return 0;
        }
        last = cur;
        cur = cur->next;
        cnt++;
    }
    printf("Done removing wdir, list after...\n");
    debug_show_list(_WATCH_LIST);
    return -1;
}

int remove_watch_dir_by_path(char *path, struct w_dir *list, int allow_base){
    struct w_dir *wlist = list;
    if (!wlist){
        fprintf(stderr, "Empty list provided\n");
        return -1;
    }
    struct w_dir *wdir = get_dir_by_path(path, wlist);
    if (!wdir){
        fprintf(stderr, "Could not find watched dir by path:'%s'\n", path ?: "");
        debug_show_list(wlist);
        return -1;
    }
    return remove_watch_dir(wdir, wlist, allow_base);
}


/* Finds parent directory using w_dir list mappings to build current event's
 * full path. 
 * Returned buffer must be free'd later by caller
 */ 
char *create_wd_full_path(int wd, char *name, struct event_mon *mon){
    char *ret = NULL;
    struct w_dir *wdir = get_dir_by_wd(wd, mon->watch_list);
    if (!wdir){
        fprintf(stderr, "Failed to find wd:'%d' for full path. name:'%s'\n", wd, name ?: "");
        debug_show_list(NULL);
        return NULL;
    }
    size_t total = strlen(wdir->path) + strlen(name) + 2;
    ret = malloc(total);
    if (!ret){
        fprintf(stderr, "Failed to alloc full path for wd\n");
        return NULL;
    }
    snprintf(ret, total, "%s%s%s", wdir->path, name ? "/" : "",  name ?: "");
    return ret;
}


/* Adds the current dir 
 *  if mon->recursive flag is set, then subdirectories will automatically be 
 *  discoverd and added recursively. 
 */
struct w_dir *monitor_dir(char *dpath, struct event_mon *mon){
    DIR *folder;
    char subdir[512];
    struct dirent *entry;
    struct stat filestat;
    struct w_dir *wdir = NULL;
    if (!dpath || !strlen(dpath)){
        fprintf(stderr, "Null dir name provided\n");
        return NULL;
    }
    if (!mon){
        fprintf(stderr, "Null event monitor  provided\n");
        return NULL;
    }
    folder = opendir(dpath);
    if(folder == NULL){
        fprintf(stderr, "Unable to read directory:'%s'\n", dpath);
        return NULL;
    }
    wdir = add_watch_dir_to_monitor(dpath, mon);
    if (!wdir) {
        fprintf(stderr, "Error, adding Dir to watchlist:'%s'\n",dpath);
    }else{
        printf("Added Dir to watchlist:'%s', wd:'%d'\n", wdir->path, wdir->wd);
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
                printf("Found sub dir for path:'%s'\n", entry->d_name);
                if (!monitor_dir(subdir, mon)){
                    fprintf(stderr, "Error adding dir to watchlist:'%s'\n", entry->d_name);
                };
            }
        }
        entry=readdir(folder);
    }
    closedir(folder);
    return(wdir);
}

/* Util func to check if a dir exists and is accesible*/
int mon_dir_exists(char *dpath){
    
    if (!dpath || !strlen(dpath)){
        return 0;
    }
    DIR* dir = opendir(dpath);
    if (dir) {
        /* Directory exists. */
        closedir(dir);
        return 1;
    } else if (ENOENT == errno) {
        return 0;
        /* Directory does not exist. */
    } else {
        return 0;
        /* opendir() failed for some other reason. */
    }
    return 0;
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
        fprintf(stderr, "Warning invalid interval. Sec:'%f' Usec:'%f'. Setting to 1 sec \n", sec, usec);
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





int delete_file(char *fpath){
    int ret = -1;
    if (!fpath || !strlen(fpath)){
        fprintf(stderr, "empty filename provided to delete_file()\n");
        return -1;
    }
    if (remove(fpath) == 0){
        printf("Automatically deleted file:'%s'\n", fpath);
        ret = 0;
    } else {
        fprintf(stderr, "Failed, delete_file()\n");
    }
    return ret;
}

int event_handler_default(struct inotify_event *event, void *data)
    {
    if (!data){
        fprintf(stderr, "Null data passed to handle data\n");
        return -1;
    }
    char *fname = NULL;
    struct w_dir *wdir = NULL;
    struct event_mon *mon = data;
    struct w_dir *wlist = mon->watch_list;
    
    if (event){
        if (event->len && event->name){
            // Check our mappings to derive the full path of this file/dir from the event
            fname = create_wd_full_path(event->wd, event->name, mon);
        }
        if ( event->mask & IN_CREATE ) {
            if ( event->mask & IN_ISDIR ) {
                printf( "MONITOR: New directory '%s' created.\n", fname ?: "");
                // If recursive is set, automatically add this new subdir 
                if (mon->recursive){
                    monitor_dir(fname, mon);
                }
            } else {
                if ( event->mask & IN_MODIFY){
                    printf( "MONITOR: File '%s' was created and modified\n", fname ?: "");
                    return 0;
                }
                if (event->cookie){
                    printf( "MONITOR: New file '%s' created. Cookie set so heads up on paired event:'%lu'\n", fname ?: "", (unsigned long) event->cookie);
                }else{
                    printf( "MONITOR: New file '%s' created.\n", fname ?: "" );
                }
            }
        } else if ( event->mask & IN_DELETE ) {
            if ( event->mask & IN_ISDIR ) {
                printf( "MONITOR: Directory '%s' deleted. Removing wd:'%d' from watchlist\n", fname ?: "", event->wd);
                /* Remove the dir from the watch list...
                 * The dir path will need to be derived from the mapped w_dir in order
                 * to get the wd to remove from inotify mon_fd */
                if (event->name && strlen(event->name)){
                    wdir = get_dir_by_wd(event->wd, wlist);
                    if (wdir && wdir->path && strlen(wdir->path)){
                        snprintf(fpath, sizeof(fpath), "%s/%s", wdir->path, event->name);
                        remove_watch_dir_by_path(fpath, wlist, 0);
                    }
                }
            } else {
                printf( "MONITOR: File '%s' deleted.\n", fname ?: "" );
            }
        }else if ( event->mask & IN_MODIFY){
            printf( "MONITOR: File '%s' was modified\n", fname ?: "");
        }
    }
    if (fname){
        free(fname);
    }
    return 0;
}


/* Reads events from inotify fd and calls the provided handler func to process them. 
   ! This read blocks until the change event occurs, use select of poll to make sure
 * the fd is read ready*/
size_t read_events_fd(int events_fd, char *buffer, size_t buflen, event_handler handler, void *data){
    int length = 0; 
    int i = 0;
    struct inotify_event *event = NULL;
    if (events_fd < 0 ){
        fprintf(stderr, "read_events_fd passed invalid fd:'%d'\n", events_fd);
        return -1;
    }
    if (!buffer || buflen <= 0 ){
        fprintf(stderr, "Passed null buffer:'%s', or invalid length:'%zu'\n", buffer ? "Y" : "N",  buflen);
        return -1;
    } 
    length = read(events_fd, buffer, buflen);
    if ( length < 0 ) {
        fprintf(stderr, "Error reading event fd\n");
        return length; 
    }
    if (!handler){
        return length;
    } 
    /*actually read return the list of change events happens. Here, read the change event one by one and process it accordingly.*/
    while ( i < length ) {
        event = ( struct inotify_event * ) &buffer[ i ];
        print_event(event);
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
    printf("---------------------------------------------------\n");
    if (!event){
        printf("Passed null event to print!\n");
        printf("---------------------------------------------------\n");
        return;
    }
    printf("\tEVENT: name:'%s'\n", event->name ?: "");
    printf("\tEVENT: mask:'0x%lx', cookie:'%d', wd:'%d', len:%lu\n",
               (unsigned long)event->mask, event->cookie, event->wd,  (unsigned long)event->len);
    if (mask == 0){
        printf("\tMASK: NONE\n");   
        printf("---------------------------------------------------\n");
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
    printf("\tMASK: %s\n", buf);   
    printf("---------------------------------------------------\n");
    return;
}
