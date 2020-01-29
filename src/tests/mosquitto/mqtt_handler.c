#include <mosquitto.h>
#include <jansson.h>
#include <stdint.h>
#include <string.h>
#include "includes/mon_utils.h"


#include <unistd.h>
#include <signal.h>

/* api docs here: https://mosquitto.org/api/files/mosquitto-h.html */


///#define mqtt_host "172.20.4.117"
#define mqtt_host "localhost"
#define mqtt_port 1883
#define mqtt_tls_port 8883
#define mqtt_user "mqttuser"
#define mqtt_pass "mqttpass"
#define cacrt_path "/etc/mosquitto/conf.d/ca.crt"
 

static int run = 1;
static char sub_topic[] = "/test/mytopic/+";
int starting_message_id = 1000;
/*
  options.qos = 1; 
  char t[128];
  strcat(t, options.topic);
  const char *resp = json_object_to_json_string(jobj);
  mosquitto_subscribe(mosq, NULL, topic, options.qos);
  mosquitto_publish(mosq, 0, topic_a, strlen(resp), resp, 1, false);

*/

void connect_callback(struct mosquitto *mosq, void *obj, int result)
{
	printf("connect callback, mosq:'%s', obj:'%s',  rc=%d, '%s'\n", 
        mosq ? "Y":"N", obj ? "Y":"N", result, mosquitto_connack_string(result));
    if (result == MOSQ_ERR_SUCCESS){
        printf("Since were connected, subscribe to:'%s' \n", sub_topic);
	    mosquitto_subscribe(mosq, NULL, sub_topic, 0);
    }
}


void publish_callback(struct mosquitto *mosq, void *obj, int mid){
    printf("published message  mosq:'%s', obj:'%s', mid:%d, \n", mosq ? "Y":"N", obj ? "Y":"N", mid);

}


void message_callback(struct mosquitto *mosq, void *obj, const struct mosquitto_message *message)
{
	bool match = 0;
    printf("message callback  mosq:'%s', obj:'%s'\n", mosq ? "Y":"N", obj ? "Y":"N");
	printf("got message '%.*s' for topic '%s'\n", message->payloadlen, (char*) message->payload, message->topic);
    //Check whether a topic matches a subscription.
    //int mosquitto_topic_matches_sub(const char *sub, const char *topic, bool *result);

	mosquitto_topic_matches_sub(sub_topic, message->topic, &match);
	if (match) {
		printf("got message for topic:'%s'\n", sub_topic);
	}
}

static void  handle_signal(int sig){
    LOGERROR("Caught signal:%d all done\n", sig);
    run = 0;     
}



//int main(int argc, char *argv[])
int main(void)
{
	//uint8_t reconnect = true;
	char clientid[24];
	struct mosquitto *mosq;
	int rc = 0;
    int tls = 1;
    int port = mqtt_port; 
    set_local_debug_enabled(1);
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
    //char *cacrt_path = NULL;
	mosquitto_lib_init();

	memset(clientid, 0, 24);
	snprintf(clientid, sizeof(clientid), "myclientid_%d", getpid());
	mosq = mosquitto_new(clientid, true, 0);

	if(mosq){
        if (strlen(mqtt_user) && strlen(mqtt_pass)){
            mosquitto_username_pw_set(mosq, mqtt_user, mqtt_pass);
        }
		mosquitto_connect_callback_set(mosq, connect_callback);
		mosquitto_message_callback_set(mosq, message_callback);
		//mosquitto_disconnect_callback_set(mosq, disconnect_callback);
        //mosquitto_publish_callback_set(mosq, publish_callback);
        //mosquitto_subscribe_callback_set(mosq, subscribe_callback);
        //mosquitto_unsubscribe_callback_set(mosq, unsubscribe_callback);
        
        //mosquitto_message_retry_set(mosq, retry_count);
        /* Set the last will on the broker. If we're detected to be offline the broker
         * will publish (on our behalf) to this topic for us to alert any subscribers of this topic that
         * we've gone offline. */
        //mosquitto_will_set(mosq, topic, strlen(report), report, 1, false);

        if (tls){
            if (!cacrt_path || !strlen(cacrt_path)){
                LOGERROR("Must provide cacert path if using tls\n");
                exit(1);
            }
            port = mqtt_tls_port; 
            mosquitto_tls_opts_set(mosq, 1, NULL, NULL);
            mosquitto_tls_set(mosq, cacrt_path, NULL, NULL, NULL, NULL);
        }



	    rc = mosquitto_connect(mosq, mqtt_host, port, 60);
        printf("Connect returned rc:'%s'(%d)\n", mosquitto_strerror(rc), rc);
        if (rc != MOSQ_ERR_SUCCESS){
            LOGERROR("Ruh oh failed to connect, rc:%d\n", rc);

        }


/*
 * Function: mosquitto_loop
 *
 * The main network loop for the client. You must call this frequently in order
 * to keep communications between the client and broker working.
 *
 * This calls select() to monitor the client network socket. If you want to
 * integrate mosquitto client operation with your own select() call, use
 * <mosquitto_socket>, <mosquitto_loop_read>, <mosquitto_loop_write> and
 * <mosquitto_loop_misc>.
 *
 * Parameters:
 *	mosq -    a valid mosquitto instance.
 *	timeout - Maximum number of milliseconds to wait for network activity in
 *            the select() call before timing out. Set to 0 for instant return.
 *            Set negative to use the default of 1000ms.
 * 
 * Returns:
 *	MOSQ_ERR_SUCCESS -   on success.
 * 	MOSQ_ERR_INVAL -     if the input parameters were invalid.
 * 	MOSQ_ERR_NOMEM -     if an out of memory condition occurred.
 * 	MOSQ_ERR_NO_CONN -   if the client isn't connected to a broker.
 *  MOSQ_ERR_CONN_LOST - if the connection to the broker was lost.
 *	MOSQ_ERR_PROTOCOL -  if there is a protocol error communicating with the
 *                       broker.
 */

        char payload[] = "This is my payload"; 

        int pubdelay = 0;
		while(run){
            if (pubdelay <= 0){
                rc = mosquitto_publish(mosq, &starting_message_id, "mytesttopic",strlen(payload), payload, 1, false);  
                printf("Published message id:%d, rc:'%s'(%d)\n", starting_message_id, mosquitto_strerror(rc), rc); 
                starting_message_id++;    
                pubdelay=10; 
            }
            pubdelay--; 
			rc = mosquitto_loop(mosq, -1, 1);
			if(run && rc != MOSQ_ERR_SUCCESS){
                printf("Loop returned rc:'%s'(%d)\n", mosquitto_strerror(rc), rc);
                printf("Run:%d, rc:%d\n", run, rc);
				printf("connection error trying to reconnect...!\n");
				sleep(10);
				mosquitto_reconnect(mosq);
			}
            sleep(1);
		}
		mosquitto_destroy(mosq);
	}

	mosquitto_lib_cleanup();

	return rc;
}
