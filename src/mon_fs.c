/* This is the sample program to notify us for the file creation and file deletion takes place in “/tmp” directory
 * From: https://www.thegeekstuff.com/2010/04/inotify-c-program-example/  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h> 
#include <dirent.h>
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
        fprintf(stderr, "WATCH_LIST[%d] = PATH:'%s', WD:'%d'\n", cnt,  ptr->path, ptr->wd);
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
        fprintf(stderr, "Error parsing config:'%s'. Error:'%s'\n", path, error ? (error->text ?: "") : "");
    }
    return json;
}

//Create/allocate new event monitor. To be free'd by caller
struct event_mon *create_event_monitor(char *base_path, 
                                       char *config_path,
                                       uint32_t mask, 
                                       loopctl_func *loopctl,
                                       event_handler *handler, 
                                       size_t event_buf_len){
    struct event_mon *mon = NULL;
    int ifd = -1;
    if (!base_path || !strlen(base_path)){
        fprintf(stderr, "Empty basepath provided to create event monitor\n");
        return NULL;
    }
    size_t buflen = event_buf_len;
    if (event_buf_len >= INOT_EVENT_SIZE + 16 ){
        buflen = (event_buf_len * ( INOT_EVENT_SIZE + 16 ));
    }else{
        buflen = (size_t) INOT_DEFAULT_EVENT_BUF_LEN; 
    }
    // Create the inotify watch instance
    ifd = inotify_init();
    /*checking for error*/
    if (ifd < 0 ) {
        fpritnf(stderr, "inotify_init error for path:'%s'\n", base_path);
        return NULL; 
    }
    mon = calloc(1, sizeof(struct event_mon) + buflen);
    if (!mon){
        fprintf(stderr, "Error allocating new event monitor!\n");
        close(ifd); 
        return NULL;
    }
    mon->ifd = ifd;
    mon->mask;
    mon->buf_len = event_buf_len;
    mon->handler = handler;
    mon->loopctl = loopctl;
    mon->config_path = config_path;
    if (do_mon_config(mon)){
        fprintf(stderr, "Error applying event monitor config. Dir:'%s', config:'%s'\n", base_path, config_path ?: "");
        close(ifd);
        free(mon);
        return NULL;
    }
    // Add the base dir to the monitor, this initializes the watch_list with this w_dir 
    struct w_dir *wdir = monitor_watch_dir(base_path, mon);
    if (!wdir){
        fpritnf(stderr, "Error adding base dir to event monitor:'%s'\n", base_path);
        close(ifd);
        free(mon);
        exit (1);
    }
    mon->base_wd = wdir-wd;
    if (!mon->watch_list){
        mon->watch_list = wdir;
    }
    watch_base = wdir->wd;
    return mon;
}

// returns null to allow assignment by caller. 
struct event_mon *destroy_event_monitor(event_mon *mon){
    // Close our inotify event fd
    if (mon->ifd >= 0){
        close(mon->ifd);
    }
    // decrement ref to monitor's json obj(s)
    if (mon->jconfig){
        json_decref(mon->jconfig);
    }
   
    // Remove and free all the watch dirs 
    struct w_dir *ptr = mon->watch_list;
    struct w_dir *cur = mon->watch_list;
    if (ptr){
        while (ptr != NULL){
            cur = ptr;
            ptr = ptr->next;
            remove_watch_dir(cur, mon->watch_list, 1);
        }
    }
    free(mon);
    return NULL;
}

//Create/allocate new watch dir.  To be free'd by caller
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
        newd->ifd = inotify_fd;
        newd->evt_mon = mon;
        newd->next = NULL;
        strcpy(newd->path, dpath);
    }
    return newd;
}

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

/*
struct w_dir *get_dir_by_wd(int wd){
    return _get_dir_by_wd(wd, _WATCH_LIST);
}
*/


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
    struct w_dir *wlist = mon->watch_list;
    struct w_dir *wdir = NULL;
    int wd;
    if (!dpath || !strlen(dpath)){
        fprintf(stderr, "Null dir name provided\n");
        return NULL;
    }
    
    struct w_dir *ptr = NULL;;
    if (!wlist){
        wlist = create_watch_dir(dpath, mon);
        return wlist;
    }else{
        wdir = get_dir_by_path(dpath, wlist);
        if (wdir){
            printf("Dir already in watchlist:'%s', wd:'%d'\n", wdir->path, wdir->wd);
            return wdir;
        }
    }
    //Create a new item and add it to the end of the list...
    wdir = create_watch_dir(dpath, mon);
    if (wdir){
        printf("Inserting  element to _WATCH_LIST: path:'%s', wd:'%d'\n", wdir->path, wdir->wd);
        ptr = wlist;
        while(ptr != NULL) {
            if (!ptr->next){
                ptr->next = wdir;
                break;
            }
            ptr = ptr->next;
        }
    }
    //debug_show_list(wlist);
    return wdir;
}

/*
struct w_dir *add_watch_dir_to_monitor(char *dpath){
    if (!_WATCH_LIST){
        printf("watch list was null so adding new dir '%s'\n", dpath);
        _WATCH_LIST = create_watch_dir(dpath, mon_fd);
        if (!_WATCH_LIST){
            printf("wtf watchlist is still null?\n");
        }
        return _WATCH_LIST;
    }
    return _add_watch_dir_to_monitor(dpath, _WATCH_LIST);
}
*/

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
    printf("Attempting to remove wdir->path:'%s', ptr:'%p'\n", wdir->path ?: "", wdir);
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
/*
int remove_watch_dir(struct w_dir *wdir){
    return _remove_watch_dir(wdir, _WATCH_LIST);
}
*/

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

/*
int remove_watch_dir_by_path(char *path){
    return _remove_watch_dir_by_path(path, _WATCH_LIST);
}
*/



// Must be free'd by caller
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

struct w_dir *monitor_watch_dir(char *dpath, struct event_mon *mon){
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
                if (!monitor_watch_dir(subdir, mon)){
                    fprintf(stderr, "Error adding dir to watchlist:'%s'\n", entry->d_name);
                };
            }
        }
        entry=readdir(folder);
    }
    closedir(folder);
    return(wdir);
}

int dir_exists(char *dpath){
    
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

static int has_changed(int fd, int sec, int usec){
    struct timeval time;
    fd_set rfds;
    int ret;
    
    /* timeout after five seconds */
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




int start_event_loop(char *base_dir, float interval_sec, char *config_path){
    



} 



static int delete_file(char *fname){
    int ret = -1;
    if (!fname || !strlen(fname)){
        perror("empty filename provided to delete_file()\n");
        return -1;
    }
    snprintf(fpath,sizeof(fpath),"%s/%s", BASE_DIR, fname);
    if (remove(fpath) == 0){
        printf("Automatically deleted file:'%s'\n", fpath);
        ret = 0;
    } else {
        perror("Failed, delete_file()\n");
    }
    return ret;
}

static void handle_event(struct inotify_event *event, void *data)
    {
    if (!data){
        fprintf(stderr, "Null data passed to handle data\n");
        return;
    }
    int new_watch = 0;
    char *fname = NULL;
    struct w_dir *wdir = NULL;
    struct event_mon *mon = data;
    struct w_dir *wlist = mon->watch_list;
    
    if (event){
        if (event->len && event->name){
            //fname = event->name;
            fname = create_wd_full_path(event->wd, event->name, wlist);
        }
        if ( event->mask & IN_CREATE ) {
            if ( event->mask & IN_ISDIR ) {
                printf( "MONITOR: New directory '%s' created.\n", fname ?: "");
                //snprintf(fpath, sizeof(fpath), "%s/%s", BASE_DIR, fname);
                //new_watch = inotify_add_watch( mon_fd, fpath, IN_CREATE | IN_DELETE | IN_MODIFY);
                //Need to store mapping of file/path to watch wd so we can delete it if the dir is detects deleted?
                monitor_watch_dir(fname, mon);
            } else {
                if ( event->mask & IN_MODIFY){
                    printf( "MONITOR: File '%s' was created and modified\n", fname ?: "");
                    if (fname){
                        delete_file(fname);
                        free(fname);
                    }
                    return;
                }
                if (event->cookie){
                    printf( "MONITOR: New file '%s' created, use follow up event instead.\n", fname ?: "" );
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
                    wdir = get_dir_by_wd(event->wd, list);
                    if (wdir && wdir->path && strlen(wdir->path)){
                        snprintf(fpath, sizeof(fpath), "%s/%s", wdir->path, event->name);
                        remove_watch_dir_by_path(fpath, list);
                    }
                }
            } else {
                printf( "MONITOR: File '%s' deleted.\n", fname ?: "" );
            }
        }else if ( event->mask & IN_MODIFY){
            printf( "MONITOR: File '%s' was modified\n", fname ?: "");
            delete_file(fname);
        }
    }
    if (fname){
        free(fname);
    }
    return;
}


/* Reads events from inotify fd and calls the provided handler func to process them. 
   ! This read blocks until the change event occurs, use select of poll to make sure
 * the fd is read ready*/
void read_events_fd(int events_fd, event_handler handler, void *data){
    int length, i = 0;
    int cookie = 1;
    int x = 0;
    char buffer[INOT_DEFAULT_EVENT_BUF_LEN];
    struct inotify_event *event = NULL;
    
    length = read(fd, buffer, INOT_DEFAULT_EVENT_BUF_LEN);
    
    /*checking for error*/
    if ( length < 0 ) {
        perror( "read" );
    }
    /*actually read return the list of change events happens. Here, read the change event one by one and process it accordingly.*/
    while ( i < length ) {
        event = ( struct inotify_event * ) &buffer[ i ];
        printf("Got event name:%s, cookie:'%d', wd:'%d', len:%lu\n",
               event->name ?: "", event->cookie, event->wd,  (unsigned long)event->len);
        handler(event, data);
        i += INOT_EVENT_SIZE + event->len;
    }
    
}
