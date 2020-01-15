#include <jansson.h>
//#include <stdio.h>
//#include <stdlib.h>
#include <string.h>
//#include <unistd.h>
#include <errno.h>
#include <dirent.h>
//#include <pthread.h>
//#include <sys/types.h>
//#include <sys/inotify.h>
//#include <sys/stat.h>
#include "includes/mon_utils.h"

int _LOCAL_DEBUG = 0;

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


/* Wrapper with error checks/handling for deleting a file*/
int delete_file(char *fpath){
    int ret = -1;
    if (!fpath || !strlen(fpath)){
        LOGERROR("empty filename provided to delete_file()\n");
        return -1;
    }  
    if (remove(fpath) == 0){
        LOGDEBUG("Automatically deleted file:'%s'\n", fpath);
        ret = 0; 
    } else {
        LOGERROR("Failed, delete_file()\n");
    }  
    return ret;
}


/* Wrapper with error checks/handling for creating a json obj from contents 
 * read in from file at path*/
json_t *json_from_file(char *path){
    json_t *json = NULL;
    json_error_t error;
    if (!path || !strlen(path)){
        LOGERROR("Empty config path provided to parse_config\n");
        return NULL;
    }  
    json = json_load_file(path, 0, &error);
    if(!json) {
        LOGERROR("Error parsing config:'%s'. Error:'%s'\n", path, error.text ?: "");
    }  
    return json;
}


int set_local_debug_enabled(int enabled){
    if (enabled <= 0){
        _LOCAL_DEBUG=0;
    }else{
        _LOCAL_DEBUG = enabled;
    }
    return _LOCAL_DEBUG;;
}

   
int _local_debug_enabled(void){
    return _LOCAL_DEBUG; 
}
