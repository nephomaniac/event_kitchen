/*  file main.c
 *  author: Zoltan Gyarmati <mr.zoltan.gyarmati@gmail.com>
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <math.h>
#include <assert.h>
#include <sys/poll.h>

#include <mosquitto.h>
/* compile:
 * cc -o mosquitto-polltest  main.c  -lmosquitto
 *
 * small example code to experiment with poll-based external loop and
 * libmosquitto, here we set up libmosquitto, subscribing to a topic,
 * and with a poll() call we watch both the libmosquitto socket file
 * descriptor and the stdin file descriptor. If a message arrives to the
 * subscribed topic, we simply log it, at if a line comes from the
 * terminal via stdin, we publish it via libmosquitto to the topic
 * 'stdin'
 *
 * send event with:
 * mosquitto_pub -t topic/subtopic -m "ahoi" -q 1
 *
 * listening to the published event (the msg from stdin) with
 * mosquitto_sub -t stdin
 */

/*
 * change this if you need remote broker
 */

#define mqtt_host "localhost"
#define mqtt_port 1883
#define mqtt_tls_port 8883
#define mqtt_user "mqttuser"
#define mqtt_pass "mqttpass"
#define cacrt_path "/etc/mosquitto/conf.d/ca.crt"

const char *mqtt_broker_host = "localhost";
int mqtt_broker_port = 1883;
static char sub_topic[] = "/test/mytopic/+";

// forward declarations
void mqtt_cb_msg(struct mosquitto *mosq, void *userdata,
                  const struct mosquitto_message *msg);
void mqtt_cb_connect(struct mosquitto *mosq, void *userdata, int result);

void mqtt_cb_subscribe(struct mosquitto *mosq, void *userdata, int mid,
                        int qos_count, const int *granted_qos);
void mqtt_cb_disconnect(struct mosquitto *mosq, void *userdat, int rc);

void mqtt_cb_log(struct mosquitto *mosq, void *userdata,
                  int level, const char *str);

int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{

    //s et up mosquitto
    //int keepalive = 3;
    bool clean_session = true;
    struct mosquitto *mosq = NULL;
    int running = 1;
    int tls = 1;
    int rc = 0;
    char clientid[24]; 
    mosquitto_lib_init();
    printf("Creating new mosquitto instance in main\n");
    memset(clientid, 0, 24);
    snprintf(clientid, sizeof(clientid), "myclientid_%d", getpid());
    mosq = mosquitto_new(clientid, clean_session, 0);

    if(!mosq){
        fprintf(stderr, "Error: Out of memory trying to create mosq instance.\n");
        return -1;
    }
    printf("Done creating instance\n");
    if (strlen(mqtt_user) && strlen(mqtt_pass)){
        mosquitto_username_pw_set(mosq, mqtt_user, mqtt_pass);
    }
    mosquitto_connect_callback_set(mosq, mqtt_cb_connect);
    mosquitto_message_callback_set(mosq, mqtt_cb_msg);
    mosquitto_subscribe_callback_set(mosq, mqtt_cb_subscribe);
    mosquitto_disconnect_callback_set(mosq, mqtt_cb_disconnect);
    mosquitto_log_callback_set(mosq, mqtt_cb_log);

    if (tls){
        if (!cacrt_path || !strlen(cacrt_path)){
            fprintf(stderr, "Must provide cacert path if using tls\n");
            exit(1);
        }
        mqtt_broker_port = mqtt_tls_port;
        mosquitto_tls_opts_set(mosq, 1, NULL, NULL);
        mosquitto_tls_set(mosq, cacrt_path, NULL, NULL, NULL, NULL);
    }

    // we try until we succeed, or we killed
    while(running) {
        rc = mosquitto_connect(mosq, mqtt_broker_host, mqtt_broker_port, 60);
        printf("Connect returned rc:'%s'(%d)\n", mosquitto_strerror(rc), rc);
        if (rc != MOSQ_ERR_SUCCESS){
            printf("Unable to connect, host: %s, port: %d\n",
                   mqtt_broker_host, mqtt_broker_port);
            sleep(2);
            continue;
        }else{
            printf("Connected...\n");
            break;
        }
    }
    // pfd[0] is for the mosquitto socket, pfd[1] is for stdin, or any
    // other file descriptor we need to handle
    struct pollfd pfd[2];
    pfd[1].fd = 0;
    pfd[1].events = POLLIN; //these 2 won't change, so enough to set it once
    const int nfds = sizeof(pfd)/sizeof(struct pollfd);

    while (running) {
        rc = mosquitto_loop(mosq, -1, 1);
        printf("Loop returned rc:'%s'(%d)\n", mosquitto_strerror(rc), rc);

        mosquitto_loop_misc(mosq);
        // this might change (when reconnecting for example)
        // so better check it always
        int mosq_fd = mosquitto_socket(mosq);
        //printf("In running loop starting poll on stdin and mosq socket:'%d'...\n", mosq_fd);
        if (mosq_fd < 0) {
            fprintf(stderr, "wtf we lost our socket? mosq_sock:'%d'\n", mosq_fd); 
            exit(1);
        }
        pfd[0].fd = mosq_fd;
        pfd[0].events = POLLIN;
        // we check whether libmosquitto wants to write, and if yes, we
        // also register the POLLOUT event for poll, so it will return
        // when it's possible to write to the socket.
        if (mosquitto_want_write(mosq)){
            printf("Set POLLOUT see if we can write\n");
            pfd[0].events |= POLLOUT;
        }
        // we set the poll()-s timeout here to the half
        // of libmosquitto's keepalive value, to stay on the safe side
        if(poll(pfd, nfds, 100 ) < 0) {
            printf("Poll() failed with <%s>, exiting",strerror(errno));
            return EXIT_FAILURE;
        }
        // first checking the mosquitto socket
        // if we supposed to write:
        if(pfd[0].revents & POLLOUT) {
            printf("In running loop poll write check...\n");
            mosquitto_loop_write(mosq,1);
        }
        // or read:
        if(pfd[0].revents & POLLIN){
            printf("In running loop poll read check...\n");
            int ret = mosquitto_loop_read(mosq, 1);
            if (ret == MOSQ_ERR_CONN_LOST) {
                printf("reconnect...\n");
                mosquitto_reconnect(mosq);
            }
        }
        // we call the misc() funtion in both cases
        // checking if there is data on the stdin, if yes, reading it
        // and publish
        if(pfd[1].revents & POLLIN){
            printf("In running loop poll stdin check...\n");
            char input[64];
            int got = read(0,input,64);
             fprintf(stderr, "--------REad from stdin %d\n --------------", (int) got);
             printf("STDIN: %s",input);
             mosquitto_publish(mosq, NULL, "stdin", strlen(input), input, 0, false);
        }
    }

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    printf("bye\n");

    return EXIT_SUCCESS;
}

/* Called when a message arrives to the subscribed topic,
 */
void
mqtt_cb_msg(struct mosquitto *mosq __attribute__((unused)), void *userdata __attribute__((unused)),
                  const struct mosquitto_message *msg)
{
    printf("Received msg on topic: %s\n", msg->topic);
    if(msg->payload != NULL){
        printf("Payload: %s\n", (char *) msg->payload);
    }
}

void
mqtt_cb_connect(struct mosquitto *mosq __attribute__((unused)), void *userdata __attribute__((unused)), int result)
{
    printf("----------------CONNECT CALLBACK HAS FIRED!!! result:(%d)'%s' -------\n", result,  mosquitto_strerror(result));
    if(!result){
        char payload[] = "This is my payload";
        char testtopic[] = "mytesttopic";
        int starting_message_id = 69001;
        printf("!!!!!  Connect cb, publish to topic: '%s' \n", testtopic);
        int rc = mosquitto_publish(mosq, &starting_message_id, testtopic, strlen(payload), payload, 0, false);
        printf("Published message id:%d, rc:'%s'(%d)\n", starting_message_id, mosquitto_strerror(rc), rc);
        //mosquitto_subscribe(mosq, NULL, sub_topic, 2);
        printf("!!!!!  Connect cb, subscribe to topic: '%s' \n", sub_topic);
        mosquitto_subscribe(mosq, NULL, sub_topic, 0);
    }
    else {
        printf("MQTT subscribe failed\n");
    }
}

void
mqtt_cb_subscribe(struct mosquitto *mosq __attribute__((unused)), void *userdata __attribute__((unused)), int mid,
                        int qos_count, const int *granted_qos)
{
    printf("Subscribed (mid: %d): %d\n", mid, granted_qos[0]);
    for(int i=1; i<qos_count; i++){
        printf("\t %d", granted_qos[i]);
    }
}

void
mqtt_cb_disconnect(struct mosquitto *mosq __attribute__((unused)), void *userdata __attribute__((unused)), int rc)
{
    printf("MQTT disconnect, error: %d: '%s'\n",rc, mosquitto_strerror(rc));
}

void
mqtt_cb_log(struct mosquitto *mosq __attribute__((unused)), void *userdata __attribute__((unused)),
                  int level, const char *str)
{
    switch(level){
        case MOSQ_LOG_DEBUG:
            printf("###DBG: %s\n",str);
            break;
        case MOSQ_LOG_INFO:
        case MOSQ_LOG_NOTICE:
            printf("###INF: %s\n",str);
            break;
        case MOSQ_LOG_WARNING:
            printf("###WRN: %s\n",str);
            break;
        case MOSQ_LOG_ERR:
            printf("###ERR: %s\n",str);
            break;
        default:
            printf("###Unknown MOSQ loglevel!");
    }
}

