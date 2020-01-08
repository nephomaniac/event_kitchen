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


#define EVENT_SIZE  (sizeof (struct inotify_event))
#define EVENT_BUF_LEN  (1024 * ( EVENT_SIZE + 16 ))
#define BASE_DIR "/tmp/inotify_test"

//Stucture to map inotify watch descriptors to fs paths
struct w_dir {
    int wd; // inotify watch descriptor
    int ifd; // inotify instance fd
    struct w_dir* list; // w_dir parent list
    struct w_dir* next; // next w_dir in list
    char path[1]; // path of directory being monitored
};

void read_events_fd(int fd);
struct w_dir *monitor_watch_dir(char *dpath);
struct w_dir *create_watch_dir(char *dpath, int wd);
static struct w_dir *_get_dir_by_wd(int wd, struct w_dir *list);
struct w_dir *get_dir_by_wd(int wd);
static struct w_dir *_get_dir_by_path(char *path, struct w_dir *list);
struct w_dir *get_dir_by_path(char *path);
static struct w_dir *_add_watch_dir_to_list(char *dpath, struct w_dir *list);
struct w_dir *add_watch_dir_to_list(char *dpath);
static int _remove_watch_dir(struct w_dir *wdir, struct w_dir *list);
int remove_watch_dir(struct w_dir *wdir);
static int _remove_watch_dir_by_path(char *path, struct w_dir *list);
int remove_watch_dir_by_path(char *path);
char *create_wd_full_path(int wd, char *name);
void debug_show_list(struct w_dir *list);


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

struct w_dir * create_watch_dir(char *dpath, int inotify_fd){
    if (!dpath || !strlen(dpath)){
        fprintf(stderr, "Null dir name provided\n");
        return NULL;
    }
    if (inotify_fd < 0){
        fprintf(stderr, "Bad inotify instance fd provided:'%d'\n", inotify_fd);
        return NULL;
    }
    int wd = inotify_add_watch( inotify_fd, dpath, IN_CREATE | IN_DELETE | IN_MODIFY);
    if (wd < 0){
        fprintf(stderr, "Could not add watcher for path:'%s', instance fd:'%d'\n", dpath ?: "", inotify_fd);
        return NULL;
    }
    struct w_dir *newd = calloc(1, sizeof(struct w_dir) + strlen(dpath));
    if (newd != NULL){
        newd->wd = wd;
        newd->ifd = inotify_fd;
        newd->list = NULL;
        newd->next = NULL;
        strcpy(newd->path, dpath);
    }
    return newd;
}

static struct w_dir * _get_dir_by_wd(int wd, struct w_dir *list){
    struct w_dir *wlist = list;
    if (!wlist){
        wlist = _WATCH_LIST;
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

struct w_dir *get_dir_by_wd(int wd){
    return _get_dir_by_wd(wd, _WATCH_LIST);
}


static struct w_dir * _get_dir_by_path(char *path, struct w_dir *list){
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

struct w_dir * get_dir_by_path(char *path){
    return _get_dir_by_path(path, _WATCH_LIST);
}



static struct w_dir *_add_watch_dir_to_list(char *dpath, struct w_dir *list){
    struct w_dir *wlist = list;
    int wd;
    if (!dpath || !strlen(dpath)){
        fprintf(stderr, "Null dir name provided\n");
        return NULL;
    }
    if (!wlist){
        wlist = _WATCH_LIST;
        if (!_WATCH_LIST){
            printf("!!!Yo watch list is null!!!\n");
        }
    }
    
    struct w_dir *ptr = NULL;;
    if (wlist == NULL){
        /* Adding the directory into watch list.
         If the IN_ONESHOT value is OR'ed into the event mask at watch addition,
         the watch is atomically removed during generation of the first event.
         Subsequent events will not be generated against the file until the watch is added back.
         ie: watchlist = inotify_add_watch( mon_fd, "/tmp/inotify_test", IN_CREATE | IN_DELETE | IN_MODIFY | IN_ONESHOT);
         */
        wlist = create_watch_dir(dpath, mon_fd);
        return wlist;
    }
    struct w_dir *wdir = _get_dir_by_path(dpath, wlist);
    if (wdir){
        printf("Dir already in watchlist:'%s', wd:'%d'\n", wdir->path, wdir->wd);
        return wdir;
    }
    //Create a new item and add it to the end of the list...
    wdir = create_watch_dir(dpath, mon_fd);
    if (wdir){
        if (!wlist){
            printf("Adding first element to _WATCH_LIST: path:'%s', wd:'%d'\n", wdir->path, wdir->wd);
            wlist = wdir;
        } else {
            printf("Inserting  element to _WATCH_LIST: path:'%s', wd:'%d'\n", wdir->path, wdir->wd);
            ptr = wlist;
            while(ptr != NULL) {
                if (!ptr->next){
                    ptr->next = wdir;
                    wdir->list = wlist;
                    break;
                }
                ptr = ptr->next;
            }
        }
    }
    debug_show_list(wlist);
    return wdir;
}


struct w_dir *add_watch_dir_to_list(char *dpath){
    if (!_WATCH_LIST){
        printf("watch list was null so adding new dir '%s'\n", dpath);
        _WATCH_LIST = create_watch_dir(dpath, mon_fd);
        if (!_WATCH_LIST){
            printf("wtf watchlist is still null?\n");
        }
        return _WATCH_LIST;
    }
    return _add_watch_dir_to_list(dpath, _WATCH_LIST);
}

static int _remove_watch_dir(struct w_dir *wdir, struct w_dir *list){
    struct w_dir *wlist = list;
    if (!wdir){
        fprintf(stderr, "Null dir provided to remove()\n");
        return -1;
    }
    if (!wlist){
        wlist = _WATCH_LIST;
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
int remove_watch_dir(struct w_dir *wdir){
    return _remove_watch_dir(wdir, _WATCH_LIST);
}

static int _remove_watch_dir_by_path(char *path, struct w_dir *list){
    struct w_dir *wlist = list;
    if (!wlist){
        wlist = _WATCH_LIST;
    }
    struct w_dir *wdir = _get_dir_by_path(path, wlist);
    if (!wdir){
        fprintf(stderr, "Could not find watched dir by path:'%s'\n", path ?: "");
        debug_show_list(wlist);
        return -1;
    }
    return _remove_watch_dir(wdir, wlist);
}

int remove_watch_dir_by_path(char *path){
    return _remove_watch_dir_by_path(path, _WATCH_LIST);
}


// Must be free'd by caller
char *create_wd_full_path(int wd, char *name){
    char *ret = NULL;
    struct w_dir *wdir = get_dir_by_wd(wd);
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

struct w_dir *monitor_watch_dir(char *dpath){
    DIR *folder;
    char subdir[512];
    struct dirent *entry;
    struct stat filestat;
    struct w_dir *wdir = NULL;
    if (!dpath || !strlen(dpath)){
        fprintf(stderr, "Null dir name provided\n");
        return NULL;
    }
    folder = opendir(dpath);
    if(folder == NULL){
        fprintf(stderr, "Unable to read directory:'%s'\n", dpath);
        return NULL;
    }
    wdir = add_watch_dir_to_list(dpath);
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
                if (!monitor_watch_dir(subdir)){
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
            _remove_watch_dir(cur, _WATCH_LIST);
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
    
    struct w_dir *wdir= monitor_watch_dir(BASE_DIR);
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

static void handle_event(struct inotify_event *event){
    int new_watch = 0;
    char *fname = NULL;
    struct w_dir *wdir = NULL;
    if (event){
        if (event->len && event->name){
            //fname = event->name;
            fname = create_wd_full_path(event->wd, event->name);
        }
        if ( event->mask & IN_CREATE ) {
            if ( event->mask & IN_ISDIR ) {
                printf( "MONITOR: New directory '%s' created.\n", fname ?: "");
                //snprintf(fpath, sizeof(fpath), "%s/%s", BASE_DIR, fname);
                //new_watch = inotify_add_watch( mon_fd, fpath, IN_CREATE | IN_DELETE | IN_MODIFY);
                //Need to store mapping of file/path to watch wd so we can delete it if the dir is detects deleted?
                monitor_watch_dir(fname);
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
                    wdir = get_dir_by_wd(event->wd);
                    if (wdir && wdir->path && strlen(wdir->path)){
                        snprintf(fpath, sizeof(fpath), "%s/%s", wdir->path, event->name);
                        remove_watch_dir_by_path(fpath);
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


void read_events_fd(int fd){
    int length, i = 0;
    int cookie = 1;
    int x = 0;
    char buffer[EVENT_BUF_LEN];
    struct inotify_event *event = NULL;
    /* read to determine the event change happens on “/tmp” directory. 
        This read blocks until the change event occurs, use select of poll to make sure
        the fd is read ready*/
    
    length = read(fd, buffer, EVENT_BUF_LEN);
    
    /*checking for error*/
    if ( length < 0 ) {
        perror( "read" );
    }
    /*actually read return the list of change events happens. Here, read the change event one by one and process it accordingly.*/
    while ( i < length ) {
        event = ( struct inotify_event * ) &buffer[ i ];
        printf("Got event name:%s, cookie:'%d', wd:'%d', len:%lu\n",
               event->name ?: "", event->cookie, event->wd,  (unsigned long)event->len);
        handle_event(event);
        i += EVENT_SIZE + event->len;
    }
    
}
