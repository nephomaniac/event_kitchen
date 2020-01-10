#include <sys/inotify.h>
#include <pthread.h>
#include <jansson.h>

#define INOT_EVENT_SIZE  (sizeof (struct inotify_event))
#define INOT_DEFAULT_EVENT_BUF_LEN  (1024 * ( INOT_EVENT_SIZE + 16 ))


struct w_dir;
struct event_mon;

/* Call back to handle detected events. If using the default loop routine, 
 * a return value of anything other than 0 will stop loop 
 */
typedef int (*event_handler)(struct inotify_event *event, void *data);
// Call back to control event loop. Return 0 to continue, else stop the loop
typedef int (*loopctl_func)(struct event_mon *mon);

//Stucture to map inotify watch descriptors to fs paths
struct w_dir {
    int wd; // inotify watch descriptor
    int ifd; // inotify instance fd
    int base_wd; // base watch descriptor
    struct event_mon *evt_mon; // parent event monitor
    struct w_dir *next; // next w_dir in list
    char path[1]; // path of directory being monitored
};

//event_mon env 
struct event_mon {
    int ifd; // inotify instance fd
    int base_wd; // base dir watch descriptor
    int recursive; // Recursively discover and add sub dirs to monitor 
    int needs_destroy; // flag to indicate this mon is in the destroy process
    int config_wd; // config file watch descriptor
    float interval; // inotify monitor select/poll timeout in seconds
    uint32_t mask; // Default watch mask filter for inotify events
    pthread_mutex_t lock; // Monitor Lock
    pthread_t *thread_id; // Event loop thread for optional threaded loop
    char *base_path; // path to base directroy to be monitored
    json_t *jconfig; // config json object 
    loopctl_func loopctl; // call back used when event loop is finished
    event_handler handler; // call back used to handle individual events
    struct w_dir *watch_list; // list mapping watch descriptors to fs paths 
    size_t buf_len; // length of event buffer 
    char event_buffer[1]; // buffer for reading in inotify events 
};

struct event_mon *create_event_monitor(char *base_path, uint32_t mask, int recursive, event_handler handler, size_t event_buf_len);

struct event_mon *destroy_event_monitor(struct event_mon *mon); 

/* Starts the inotify monitor, adds the base dir to be monitored, as well as 
 * any recursively discovered sub dirs. 
 * If monitor_init() returns 0 then mon->ifd can be used to read in inotify events. 
 */
int monitor_init(struct event_mon *mon);
int start_monitor_loop(struct event_mon *mon);

size_t read_events_fd(int events_fd, char *buffer, size_t buflen, event_handler handler, void *data);

struct w_dir *monitor_watch_dir(char *dpath, struct event_mon *mon);

struct w_dir * create_watch_dir(char *dpath, struct event_mon *mon);

struct w_dir *get_dir_by_wd(int wd, struct w_dir *list);

struct w_dir *get_dir_by_path(char *path, struct w_dir *list);

struct w_dir *add_watch_dir_to_monitor(char *dpath, struct event_mon *mon);

int remove_watch_dir(struct w_dir *wdir, struct w_dir *list, int allow_base);

int remove_watch_dir_by_path(char *path, struct w_dir *list, int allow_base);

char *create_wd_full_path(int wd, char *name, struct event_mon *mon);

void debug_show_list(struct w_dir *list);

/* General, Misc, utils */
int mon_dir_exists(char *dpath); // Check to make sure a dir at dpath exists, is accessible on the fs. 
int mon_fd_has_events(int fd, int sec, int usec);
int delete_file(char *fpath);
int event_handler_default(struct inotify_event *event, void *data);


