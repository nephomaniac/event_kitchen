#include <syslog.h>
int local_debug = 1; 

#define MONLOG(lev, fmt, args...) do {\
    syslog(lev, "[%s:%d:%s()] " fmt, __FILE__, __LINE__, __func__, ##args);\
    if (local_debug) \
        printf("%s:%s:%d:%s():" fmt, #lev, __FILE__, __LINE__, __func__, ##args);\
} while(0)

#define LOGDEBUG(a, args...) MONLOG(LOG_DEBUG, a, ##args)
#define LOGINFO(a, args...) MONLOG(LOG_INFO, a, ##args)
#define LOGNOTICE(a, args...) MONLOG(LOG_NOTICE, a, ##args)
#define LOGWARNING(a, args...) MONLOG(LOG_WARNING, a, ##args)
#define LOGERROR(a, args...) MONLOG(LOG_ERR, a, ##args)
#define LOGCRIT(a, args...) MONLOG(LOG_CRIT, a, ##args)
#define LOGALERT(a, args...) MONLOG(LOG_ALERT, a, ##args)
#define LOGEMERG(a, args...) MONLOG(LOG_EMERG, a, ##args)
