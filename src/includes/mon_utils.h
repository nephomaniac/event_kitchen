#include <syslog.h>
#include <jansson.h>

int _LOCAL_DEBUG;
int set_local_debug_enabled(int enabled);    
int _local_debug_enabled(void);

#define MONLOG(lev, fmt, args...) do {\
    syslog(lev, "[%s:%d:%s()] " fmt, __FILE__, __LINE__, __func__, ##args);\
    if (_local_debug_enabled() != 0) \
        printf("%s:%s:%d:%s():" fmt, #lev, __FILE__, __LINE__, __func__, ##args);\
} while(0)

#define LOGDEBUG(a, args...) MONLOG(LOG_DEBUG, a, ##args)
#define LOGINFO(a, args...) MONLOG(LOG_INFO, a, ##args)
#define LOGNOTICE(a, args...) MONLOG(LOG_NOTICE, a, ##args)
#define LOGWARNING(a, args...) MONLOG(LOG_WARNING, a, ##args)
#define LOGERR(a, args...) MONLOG(LOG_ERR, a, ##args)
#define LOGERROR(a, args...) MONLOG(LOG_ERR, a, ##args)
#define LOGCRIT(a, args...) MONLOG(LOG_CRIT, a, ##args)
#define LOGALERT(a, args...) MONLOG(LOG_ALERT, a, ##args)
#define LOGEMERG(a, args...) MONLOG(LOG_EMERG, a, ##args)


/* Check to make sure a dir at dpath exists, is accessible on the fs. */
int mon_dir_exists(char *dpath); 
int delete_file(char *fpath);

json_t *json_from_file(char *path);

