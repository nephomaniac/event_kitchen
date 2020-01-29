#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>


void run_command(uv_fs_event_t *handle, const char *filename, int events, int status);
char *command; 
uv_loop_t *loop; 

void watch_file(uv_loop_t *loop, char *fpath){
        fprintf(stderr, "Adding watch on %s\n", fpath);
        uv_fs_event_t *fs_event_req = malloc(sizeof(uv_fs_event_t));
        uv_fs_event_init(loop, fs_event_req);
        // The recursive flag watches subdirectories too.
        uv_fs_event_start(fs_event_req, run_command, fpath, UV_FS_EVENT_RECURSIVE);
}

void run_command(uv_fs_event_t *handle, const char *filename, int events, int status) {
    char path[1024];
    char fpath[1024];
    char cmd[1024];
    size_t size = 1023;
    // Does not handle error if path is longer than 1023.
    uv_fs_event_getpath(handle, path, &size);
    path[size] = '\0';
    snprintf(fpath, sizeof(fpath), "%s%s", path, filename);
    snprintf(cmd, sizeof(cmd), "%s \"Got this:'%s'\"", command, filename); 
    printf("Got fs event for filename:'%s'\n" 
            "\tpath:'%s',\n"
            "\tevents:'%d',\n"
            "\tstatus:'%d',\n"
            "\tfpath:'%s',\n"
            "\tcmd:'%s'\n", filename, path, events, status,fpath, cmd); 
    fprintf(stderr, "Change detected(%d) in '%s': ", status, path);
    if (events & UV_RENAME){
        fprintf(stderr, "renamed");
    } 
    if (events & UV_CHANGE){
        fprintf(stderr, "changed");
    }
    fprintf(stderr, " %s\n", filename ? filename : "");
    printf("Command:'%s' return value:%d\n", cmd, system(cmd));
    watch_file(loop, fpath);
    free(handle); 
}


int main(int argc, char **argv) {
    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <command> <file1> [file2 ...]\n", argv[0]);
        return 1;
    }
    loop = malloc(sizeof(uv_loop_t));
    //uv_loop_init(loop);
    loop = uv_default_loop();
    command = argv[1];

    while (argc-- > 2) {
        watch_file(loop, argv[argc]); 
    }

    return uv_run(loop, UV_RUN_DEFAULT);
};
