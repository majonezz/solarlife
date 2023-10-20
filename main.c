#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include "mqtt.h"
#include "client.h"
#include <assert.h>
#include "bluetooth.h"
#include "hci.h"
#include "hci_lib.h"
#include "l2cap.h"
#include "uuid.h"
#include <sys/ioctl.h>
#include "mainloop.h"
#include "util.h"
#include "att.h"
#include "queue.h"
#include "gatt-db.h"
#include "gatt-client.h"
#include "json.h"

#define ATT_CID 4
#define PRLOG(...) \
    printf(__VA_ARGS__); 

#define JSONOUTLEN	4096
#define MQTT_SEND_PERIOD_S 10

static uint16_t mqtt_port = 1883;
static uint16_t send_interval = MQTT_SEND_PERIOD_S;
static bool verbose = false;
static int handler_registered=0;
struct client *cli;
uint8_t rcv_buf[256];
char jsonout[JSONOUTLEN];
struct JsonVal top;
uint8_t rcv_ptr=0, frame_len=0;
uint16_t addr_base=0;
char mqtt_buffer[BUFFER_SIZE_BYTES];
client_t c;
//int is_subscriber = 1;
int keepalive_sec = 4;
int nbytes;

struct client {
    /// socket
    int fd;
    /// pointer to a bt_att structure
    struct bt_att *att;
    /// pointer to a gatt_db structure
    struct gatt_db *db;
    /// pointer to a bt_gatt_client structure
    struct bt_gatt_client *gatt;
    /// session id
    unsigned int reliable_session_id;
};



static void got_connection(client_t* psClnt)
{
  uint8_t buf[128];
  require(psClnt != 0);
  if(!psClnt->sockfd) return;

  printf("MQTT: connected to server.\n");
  nbytes = mqtt_encode_connect_msg2(buf, 0x02, keepalive_sec, (uint8_t*)"Solarlife", 9);

  client_send(&c, (char*)buf, nbytes);
}

static void lost_connection(client_t* psClnt)
{
    require(psClnt != 0);
    printf("MQTT: disconnected from server.\n");
    mainloop_quit();
}

static void got_data(client_t* psClnt, unsigned char* data, int nbytes)
{
  require(psClnt != 0);

  uint8_t ctrl = data[0] >> 4;
  if (ctrl == CTRL_PUBACK)   { 
	printf("MQTT server acknowledged data\n"); 
    }
}




int16_t b2int (uint8_t *ptr) {

    return ptr[1] | ptr[0] << 8;

}


int b2int2 (uint8_t *ptr) {

    return ptr[0] | ptr[1] << 8;

}


uint16_t ModRTU_CRC(uint8_t* buf, int len)
{
  uint16_t crc = 0xFFFF;
  
  for (int pos = 0; pos < len; pos++) {
    crc ^= (uint16_t)buf[pos];          // XOR byte into least sig. byte of crc
  
    for (int i = 8; i != 0; i--) {    // Loop over each bit
      if ((crc & 0x0001) != 0) {      // If the LSB is set
        crc >>= 1;                    // Shift right and XOR 0xA001
        crc ^= 0xA001;
      }
      else                            // Else LSB is not set
        crc >>= 1;                    // Just shift right
    }
  }
  // Note, this number has low and high bytes swapped, so use it accordingly (or swap bytes)
  return crc;  
}


static void write_cb(bool success, uint8_t att_ecode, void *user_data)
{
/*
    if (success) {
	PRLOG("\nWrite successful\n");
    } else {
	PRLOG("\nWrite failed: %s (0x%02x)\n",
		ecode_to_string(att_ecode), att_ecode);
    }
*/
}



int send_modbus_req(uint8_t eq_id, uint8_t fct_code,uint16_t addr, uint16_t len) {

    uint8_t buf[10];


    buf[0]=eq_id;
    buf[1]=fct_code;
    buf[2]=addr>>8;
    buf[3]=addr&0xff;
    buf[4]=len>>8;
    buf[5]=len&0xff;
    uint16_t crc = ModRTU_CRC(buf,6);
    buf[7]=crc>>8;
    buf[6]=crc&0xff;
    addr_base = addr;
    
    rcv_ptr = 0; //reset pointer
    top = jsonCreateObject();

    if (!bt_gatt_client_write_value(cli->gatt, 0x14, buf, 8, write_cb, NULL, NULL)) {
	printf("Failed to initiate write procedure\n");
	return 1;
    }
    
    return 0;

 }


static void log_service_event(struct gatt_db_attribute *attr, const char *str)
{
    char uuid_str[MAX_LEN_UUID_STR];
    bt_uuid_t uuid;
    uint16_t start, end;

    gatt_db_attribute_get_service_uuid(attr, &uuid);
    bt_uuid_to_string(&uuid, uuid_str, sizeof(uuid_str));

    gatt_db_attribute_get_service_handles(attr, &start, &end);

    PRLOG("%s - UUID: %s start: 0x%04x end: 0x%04x\n", str, uuid_str,
				start, end);
}




static void register_notify_cb(uint16_t att_ecode, void *user_data)
{
    if (att_ecode) {
	PRLOG("Failed to register notify handler "
		    "- error code: 0x%02x\n", att_ecode);
	return;
    }

    PRLOG("Registered notify handler!\n");
    handler_registered = 1;
}

void parse_val(uint8_t *buf, uint16_t addr) {

    //printf("Parsing value at address %04x\n",addr);
    uint8_t ptr = addr*2+3;
    addr+=addr_base;

    switch (addr) {

	case 0x3000:
	    JsonVal_objectAddNumber(&top, "PV_rated_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x3001:
	    JsonVal_objectAddNumber(&top, "PV_rated_current", (float)b2int(buf+ptr)/100);
	break;
	case 0x3002:
	    JsonVal_objectAddNumber(&top, "PV_rated_power_l", (float)b2int(buf+ptr)/100);
	break;
	case 0x3003:
	    JsonVal_objectAddNumber(&top, "PV_rated_power_h", (float)b2int(buf+ptr)/100);
	break;

	case 0x3004:
	    JsonVal_objectAddNumber(&top, "battery_rated_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x3005:
	    JsonVal_objectAddNumber(&top, "battery_rated_current", (float)b2int(buf+ptr)/100);
	break;
	case 0x3006:
	    JsonVal_objectAddNumber(&top, "battery_rated_power_l", (float)b2int(buf+ptr)/100);
	break;
	case 0x3007:
	    JsonVal_objectAddNumber(&top, "battery_rated_power_h", (float)b2int(buf+ptr)/100);
	break;

	case 0x3008:
	    JsonVal_objectAddNumber(&top, "load_rated_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x3009:
	    JsonVal_objectAddNumber(&top, "load_rated_current", (float)b2int(buf+ptr)/100);
	break;
	case 0x300a:
	    JsonVal_objectAddNumber(&top, "load_rated_power_l", (float)b2int(buf+ptr)/100);
	break;
	case 0x300b:
	    JsonVal_objectAddNumber(&top, "load_rated_power_h", (float)b2int(buf+ptr)/100);
	break;


	case 0x3030:
	    JsonVal_objectAddNumber(&top, "slave_id", (int)b2int(buf+ptr));
	break;
	case 0x3031:
	    JsonVal_objectAddNumber(&top, "running_days", (int)b2int(buf+ptr));
	break;
	case 0x3032:
	    JsonVal_objectAddNumber(&top, "sys_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x3033: 
	    JsonVal_objectAddNumber(&top, "battery_status", b2int(buf+ptr));
	break;
	case 0x3034: 
	    JsonVal_objectAddNumber(&top, "charge_status", b2int(buf+ptr)); 
	break;
	case 0x3035: 
	    JsonVal_objectAddNumber(&top, "discharge_status", b2int(buf+ptr)); 
	break;
	case 0x3036:
	    JsonVal_objectAddNumber(&top, "env_temperature", (float)b2int(buf+ptr)/100);
	break;
	case 0x3037:
	    JsonVal_objectAddNumber(&top, "sys_temperature", (float)b2int(buf+ptr)/100);
	break;
	case 0x3038: 
	    JsonVal_objectAddNumber(&top, "undervoltage_times", b2int(buf+ptr));
	break;
	case 0x3039: 
	    JsonVal_objectAddNumber(&top, "fullycharged_times", b2int(buf+ptr));
	break;
	case 0x303a: 
	    JsonVal_objectAddNumber(&top, "overvoltage_prot_times", b2int(buf+ptr));
	break;
	case 0x303b: 
	    JsonVal_objectAddNumber(&top, "overcurrent_prot_times", b2int(buf+ptr));
	break;
	case 0x303c: 
	    JsonVal_objectAddNumber(&top, "shortcircuit_prot_times", b2int(buf+ptr));
	break;
	case 0x303d: 
	    JsonVal_objectAddNumber(&top, "opencircuit_prot_times", b2int(buf+ptr));
	break;
	case 0x303e: 
	    JsonVal_objectAddNumber(&top, "hw_prot_times", b2int(buf+ptr));
	break;
	case 0x303f: 
	    JsonVal_objectAddNumber(&top, "charge_overtemp_prot_times", b2int(buf+ptr));
	break;
	case 0x3040: 
	    JsonVal_objectAddNumber(&top, "discharge_overtemp_prot_times", b2int(buf+ptr));
	break;
	case 0x3045: 
	    JsonVal_objectAddNumber(&top, "battery_remaining_capacity", b2int(buf+ptr));
	break;
	case 0x3046:
	    JsonVal_objectAddNumber(&top, "battery_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x3047:
	    JsonVal_objectAddNumber(&top, "battery_current", (float)b2int(buf+ptr)/100);
	    //JsonVal_objectAddNumber(&top, "battery_current", b2int(buf+ptr)/100);
	    //printf("current: %d\n",b2int(buf+ptr));

	break;
	case 0x3048:
	    JsonVal_objectAddNumber(&top, "battery_power_lo", (float)b2int(buf+ptr)/100);
	break;
	case 0x3049:
	    JsonVal_objectAddNumber(&top, "battery_power_hi", (float)b2int(buf+ptr)/100);
	break;
	case 0x304a:
	    JsonVal_objectAddNumber(&top, "load_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x304b:
	    JsonVal_objectAddNumber(&top, "load_current", (float)b2int(buf+ptr)/100);
	break;
	case 0x304c:
	    JsonVal_objectAddNumber(&top, "load_power_l", (float)b2int(buf+ptr)/100);
	break;
	case 0x304d:
	    JsonVal_objectAddNumber(&top, "load_power_h", (float)b2int(buf+ptr)/100);
	break;
	case 0x304e:
	    JsonVal_objectAddNumber(&top, "solar_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x304f:
	    JsonVal_objectAddNumber(&top, "solar_current", (float)b2int(buf+ptr)/100);
	break;
	case 0x3050:
	    JsonVal_objectAddNumber(&top, "solar_power_l", (float)b2int(buf+ptr)/100);
	break;
	case 0x3051:
	    JsonVal_objectAddNumber(&top, "solar_power_h", (float)b2int(buf+ptr)/100);
	break;
	case 0x3052:
	    JsonVal_objectAddNumber(&top, "daily_production", (float)b2int(buf+ptr)/100);
	break;
	case 0x3053:
	    JsonVal_objectAddNumber(&top, "total_production_l", (float)b2int(buf+ptr)/100);
	break;
	case 0x3054:
	    JsonVal_objectAddNumber(&top, "total_production_h", (float)b2int(buf+ptr)/100);
	break;
	case 0x3055:
	    JsonVal_objectAddNumber(&top, "daily_consumption", (float)b2int(buf+ptr)/100);
	break;
	case 0x3056:
	    JsonVal_objectAddNumber(&top, "total_consumption_l", (float)b2int(buf+ptr)/100);
	break;
	case 0x3057:
	    JsonVal_objectAddNumber(&top, "total_consumption_h", (float)b2int(buf+ptr)/100);
	break;
	case 0x3058: 
	    JsonVal_objectAddNumber(&top, "lighttime_daily", b2int(buf+ptr));
	break;
	case 0x305d:
	    JsonVal_objectAddNumber(&top, "monthly_production_l", (float)b2int(buf+ptr)/100);
	break;
	case 0x305e:
	    JsonVal_objectAddNumber(&top, "monthly_production_h", (float)b2int(buf+ptr)/100);
	break;
	case 0x305f:
	    JsonVal_objectAddNumber(&top, "yearly_production_l", (float)b2int(buf+ptr)/100);
	break;
	case 0x3060:
	    JsonVal_objectAddNumber(&top, "yearly_production_h", (float)b2int(buf+ptr)/100);
	break;

	case 0x9017: 
	    JsonVal_objectAddNumber(&top, "rtc_second", b2int(buf+ptr));
	break;
	case 0x9018: 
	    JsonVal_objectAddNumber(&top, "rtc_minute", b2int(buf+ptr));
	break;
	case 0x9019: 
	    JsonVal_objectAddNumber(&top, "rtc_hour", b2int(buf+ptr));
	break;
	case 0x901a: 
	    JsonVal_objectAddNumber(&top, "rtc_day", b2int(buf+ptr));
	break;
	case 0x901b: 
	    JsonVal_objectAddNumber(&top, "rtc_month", b2int(buf+ptr));
	break;
	case 0x901c: 
	    JsonVal_objectAddNumber(&top, "rtc_year", b2int(buf+ptr));
	break;
	case 0x901d: 
	    JsonVal_objectAddNumber(&top, "baud_rate", b2int(buf+ptr));
	break;
	case 0x901f: 
	    JsonVal_objectAddNumber(&top, "password", b2int(buf+ptr));
	break;
	case 0x9021: 
	    JsonVal_objectAddNumber(&top, "battery_type", b2int(buf+ptr));
	break;
	case 0x9022:
	    JsonVal_objectAddNumber(&top, "lvp_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x9023:
	    JsonVal_objectAddNumber(&top, "lvr_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x9024:
	    JsonVal_objectAddNumber(&top, "boost_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x9025:
	    JsonVal_objectAddNumber(&top, "equalizing_voltage", (float)b2int(buf+ptr)/100);
	break;
	case 0x9026:
	    JsonVal_objectAddNumber(&top, "floating_voltage", (float)b2int(buf+ptr)/100);
	break;

	default:
	break;
    }

}

static void notify_cb(uint16_t value_handle, const uint8_t *value,
		    uint16_t length, void *user_data)
{

    memcpy(rcv_buf+rcv_ptr, value, length);
    rcv_ptr+=length;

    if (rcv_ptr==rcv_buf[2]+5) {

	uint16_t crc_r = b2int2(rcv_buf+rcv_ptr-2);
	uint16_t crc = ModRTU_CRC(rcv_buf, rcv_ptr-2);
	if((crc==crc_r) && addr_base) { //CRC check passed
	   for (int ii = 0; ii<(rcv_buf[2]-2)/2; ii++) {
		parse_val(rcv_buf,ii);

	   }
	time_t now;
	time(&now);
	char buf[sizeof "2011-10-08T07:07:09Z"];
	strftime(buf, sizeof buf, "%FT%TZ", gmtime(&now));
	JsonVal_objectAddString(&top, "timestamp", buf);
	JsonVal_writeString(&top, jsonout, JSONOUTLEN);
	puts(jsonout);
	char buf2[8000];
	nbytes = mqtt_encode_publish_msg((uint8_t*)buf2, (uint8_t*)"Solarlife",9 , 1, 10, (uint8_t*)jsonout, strlen(jsonout));
    	if(c.sockfd) {
	    client_send(&c, buf2, nbytes);
	    client_poll(&c, 1000000);
	}
	JsonVal_destroy(&top);
	//if(addr_base<0x9000) {
	//	send_modbus_req(0x01, 0x03, 0x9000, 0x40);
	//	addr_base = 0;
	//}
	    
	}
    }
    

}




static void ready_cb(bool success, uint8_t att_ecode, void *user_data)
{
    struct client *cli = user_data;

    if (!success) {
	PRLOG("GATT discovery procedures failed - error code: 0x%02x\n",
				att_ecode);
	return;
    }

    PRLOG("GATT discovery procedures complete\n");

    unsigned char buf[] = {01,00};
    if (!bt_gatt_client_write_value(cli->gatt, 0x12, buf, sizeof(buf),
				write_cb,
				NULL, NULL))
	printf("Failed to initiate write procedure\n");

    unsigned int id = bt_gatt_client_register_notify(cli->gatt, 0x11,
			    register_notify_cb,
			    notify_cb, NULL, NULL);
    if (!id) {
	printf("Failed to register notify handler\n");
    }

    PRLOG("Registering notify handler with id: %u\n", id);


}



static void service_removed_cb(struct gatt_db_attribute *attr, void *user_data)
{
    log_service_event(attr, "Service Removed");
}


static void service_added_cb(struct gatt_db_attribute *attr, void *user_data)
{
    log_service_event(attr, "Service Added");
}


static void att_disconnect_cb(int err, void *user_data)
{
    printf("Device disconnected: %s\n", strerror(err));
    mainloop_quit();
}

void send_mqtt_ping(void) {

	unsigned char buf[100];
	nbytes = mqtt_encode_ping_msg(buf);
        if ((nbytes != 0) && (c.sockfd))
        {
    	    client_send(&c, (char*)buf, nbytes);
    	    client_poll(&c, 0);
	}


}

static void signal_cb(int signum, void *user_data)
{

    static int sec_counter;
    switch (signum) {
    case SIGINT:
    case SIGTERM:
	mainloop_quit();
	break;
    case SIGALRM:
	{
	//if(addr_base) send_modbus_req(0x01, 0x03, 0x9000, 0x40);
	//else { 
	//    send_modbus_req(0x01, 0x04, 0x3000, 0x7c);
	//    addr_base=0;
	//}
	//uint16_t len = form_modbus_msg(buff,0x01, 0x03, 0x9000, 0x40);
	//send_modbus_req(0x01, 0x04, 0x3000, 0x7c);
	//send_modbus_req(0x01, 0x04, 0x3047, 0x20);
	send_mqtt_ping();
	if(handler_registered) {
	    if (++sec_counter>send_interval) {
		send_modbus_req(0x01, 0x04, 0x3000, 0x7c);
		sec_counter=0;
	    }

	}

	
	alarm(1);
	break;
	}
    default:
	break;
    }
}



static void client_destroy(struct client *cli)
{
    bt_gatt_client_unref(cli->gatt);
    bt_att_unref(cli->att);
    free(cli);
}



static struct client *client_create(int fd, uint16_t mtu)
{
    struct client *cli;

    cli = new0(struct client, 1);
    if (!cli) {
	fprintf(stderr, "Failed to allocate memory for client\n");
	return NULL;
    }

    cli->att = bt_att_new(fd, false);
    if (!cli->att) {
	fprintf(stderr, "Failed to initialze ATT transport layer\n");
	bt_att_unref(cli->att);
	free(cli);
	return NULL;
    }

    if (!bt_att_set_close_on_unref(cli->att, true)) {
	fprintf(stderr, "Failed to set up ATT transport layer\n");
	bt_att_unref(cli->att);
	free(cli);
	return NULL;
    }

    if (!bt_att_register_disconnect(cli->att, att_disconnect_cb, NULL,
				NULL)) {
	fprintf(stderr, "Failed to set ATT disconnect handler\n");
	bt_att_unref(cli->att);
	free(cli);
	return NULL;
    }

    cli->fd = fd;
    cli->db = gatt_db_new();
    if (!cli->db) {
	fprintf(stderr, "Failed to create GATT database\n");
	bt_att_unref(cli->att);
	free(cli);
	return NULL;
    }

    cli->gatt = bt_gatt_client_new(cli->db, cli->att, mtu);
    if (!cli->gatt) {
	fprintf(stderr, "Failed to create GATT client\n");
	gatt_db_unref(cli->db);
	bt_att_unref(cli->att);
	free(cli);
	return NULL;
    }

    gatt_db_register(cli->db, service_added_cb, service_removed_cb,
				NULL, NULL);

    bt_gatt_client_set_ready_handler(cli->gatt, ready_cb, cli, NULL);

    /* bt_gatt_client already holds a reference */
    gatt_db_unref(cli->db);

    return cli;
}


static int l2cap_le_att_connect(bdaddr_t *src, bdaddr_t *dst, uint8_t dst_type,
				    int sec)
{
    int sock;
    struct sockaddr_l2 srcaddr, dstaddr;
    struct bt_security btsec;


    sock = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (sock < 0) {
	perror("Failed to create L2CAP socket");
	return -1;
    }

    /* Set up source address */
    memset(&srcaddr, 0, sizeof(srcaddr));
    srcaddr.l2_family = AF_BLUETOOTH;
    srcaddr.l2_cid = htobs(ATT_CID);
    srcaddr.l2_bdaddr_type = 0;
    bacpy(&srcaddr.l2_bdaddr, src);

    if (bind(sock, (struct sockaddr *)&srcaddr, sizeof(srcaddr)) < 0) {
	perror("Failed to bind L2CAP socket");
	close(sock);
	return -1;
    }

    /* Set the security level */
    memset(&btsec, 0, sizeof(btsec));
    btsec.level = sec;
    if (setsockopt(sock, SOL_BLUETOOTH, BT_SECURITY, &btsec,
			    sizeof(btsec)) != 0) {
	fprintf(stderr, "Failed to set L2CAP security level\n");
	close(sock);
	return -1;
    }

    /* Set up destination address */
    memset(&dstaddr, 0, sizeof(dstaddr));
    dstaddr.l2_family = AF_BLUETOOTH;
    dstaddr.l2_cid = htobs(ATT_CID);
    dstaddr.l2_bdaddr_type = dst_type;
    bacpy(&dstaddr.l2_bdaddr, dst);

    printf("Connecting to BT device...");
    fflush(stdout);

    if (connect(sock, (struct sockaddr *) &dstaddr, sizeof(dstaddr)) < 0) {
	perror(" Failed to connect to BT device");
	close(sock);
	return -1;
    }

    printf(" Done\n");

    return sock;
}

void inthandler(int dummy)
{
  (void)dummy;

    uint8_t buf[128];
  int nbytes = mqtt_encode_disconnect_msg(buf);
  if (nbytes != 0)
  {
    client_send(&c, (char*)buf, nbytes);
  }

  client_poll(&c, 10000000);

  exit(0);
}



static void usage(char *argv[])
{
    printf("%s\n",argv[0]);
    printf("Usage:\n\t%s [options]\n",argv[0]);

    printf("Options:\n"
	"\t-i, --index <id>\t\tSpecify adapter index, e.g. hci0\n"
	"\t-d, --dest <addr>\t\tSpecify the destination mac address of the Solarlife device\n"
	"\t-a, --host <ipaddr>\t\tSpecify the MQTT broker address\n"
	"\t-p, --port \t\t\tMQTT broker port, default: %d\n"
	"\t-t, --interval \t\t\tSet data sending interval in seconds, default: %d s\n"
	"\t-v, --verbose\t\t\tEnable extra logging\n"
	"\t-h, --help\t\t\tDisplay help\n",mqtt_port, send_interval);

    printf("Example:\n"
	    "%s -d C4:BE:84:70:29:04 -a test.mosquitto.org -t 10\n",argv[0]);
}



static struct option main_options[] = {
    { "index",		1, 0, 'i' },
    { "dest",		1, 0, 'd' },
    { "host",		1, 0, 'a' },
    { "port",		1, 0, 'p' },
    { "interval",	1, 0, 't' },
    { "verbose",	0, 0, 'v' },
    { "help",		0, 0, 'h' },
    { }
};







int main(int argc, char *argv[])
{

    int opt;
    int sec = BT_SECURITY_LOW;
    uint16_t mtu = 0;
    uint8_t dst_type = BDADDR_LE_PUBLIC;
    bool dst_addr_given = false;
    bdaddr_t src_addr, dst_addr;
    int dev_id = -1;
    int fd;
    sigset_t mask;
    char mqtt_host[100];


    //str2ba("04:7f:0e:54:61:0c", &dst_addr);



    while ((opt = getopt_long(argc, argv, "+hva:t:d:i:p:",
			main_options, NULL)) != -1) {
	switch (opt) {
	case 'h':
	    usage(argv);
	    return EXIT_SUCCESS;
	case 'v':
	    verbose = true;
	    break;
	case 'a':
	    strncpy(mqtt_host,optarg,sizeof(mqtt_host));
	    break;

	case 'p': {
	    int arg;

	    arg = atoi(optarg);
	    if (arg <= 0) {
		fprintf(stderr, "Invalid MQTT port: %d\n", arg);
		return EXIT_FAILURE;
	    }

	    if (arg > UINT16_MAX) {
		fprintf(stderr, "MQTT port too large: %d\n", arg);
		return EXIT_FAILURE;
	    }

	    mqtt_port = (uint16_t)arg;
	    break;
	}


	case 't': {
	    int arg;

	    arg = atoi(optarg);
	    if (arg <= 0) {
		fprintf(stderr, "Invalid interval: %d s\n", arg);
		return EXIT_FAILURE;
	    }

	    if (arg > UINT16_MAX) {
		fprintf(stderr, "Interval too large: %d s\n", arg);
		return EXIT_FAILURE;
	    }
	    if (arg < 1) {
		fprintf(stderr, "Interval too small: %d s\n", arg);
		return EXIT_FAILURE;
	    }

	    send_interval = (uint16_t)arg;
	    break;
	}

	case 'd':
	    if (str2ba(optarg, &dst_addr) < 0) {
		fprintf(stderr, "Invalid Solarlife address: %s\n",
				    optarg);
		return EXIT_FAILURE;
	    }

	    dst_addr_given = true;
	    break;

	case 'i':
	    dev_id = hci_devid(optarg);
	    if (dev_id < 0) {
		perror("Invalid adapter");
		return EXIT_FAILURE;
	    }

	    break;
	default:
	    //fprintf(stderr, "Invalid option: %c\n", opt);
	    return EXIT_FAILURE;
	}
    }

    if (!argc) {
	usage(argv);
	return EXIT_SUCCESS;
    }

    argc -= optind;
    argv += optind;
    optind = 0;

    if (argc) {
	usage(argv);
	return EXIT_SUCCESS;
    }








    if (dev_id == -1)
	bacpy(&src_addr, BDADDR_ANY);
    else if (hci_devba(dev_id, &src_addr) < 0) {
	perror("Adapter not available");
	return EXIT_FAILURE;
    }

    if (!dst_addr_given) {
	fprintf(stderr, "Destination BT mac address required! Use -h option to print usage.\n");
	return EXIT_FAILURE;
    }


    /* create the mainloop resources */


    mainloop_init();

    fd = l2cap_le_att_connect(&src_addr, &dst_addr, dst_type, sec);
    if (fd < 0)
	return EXIT_FAILURE;


    cli = client_create(fd, mtu);
    if (!cli) {
	close(fd);
	return EXIT_FAILURE;
    }

    if(strlen(mqtt_host)) {
	client_init(&c, mqtt_host, mqtt_port, mqtt_buffer, BUFFER_SIZE_BYTES);
	assert(client_set_callback(&c, CB_RECEIVED_DATA, got_data)        == 1);
	assert(client_set_callback(&c, CB_ON_CONNECTION, got_connection)  == 1);
	assert(client_set_callback(&c, CB_ON_DISCONNECT, lost_connection) == 1);
	signal(SIGINT, inthandler);
	client_connect(&c);
    } else {
	printf("MQTT Broker address not given, will not send data.\n");
    }
    alarm(1);



    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGALRM);


    /* add handler for process interrupted (SIGINT) or terminated (SIGTERM)*/
    mainloop_set_signal(&mask, signal_cb, NULL, NULL);

    mainloop_run();

    printf("\n\nShutting down...\n");
    if(c.sockfd) client_disconnect(&c);
    client_destroy(cli);

    return EXIT_SUCCESS;
}