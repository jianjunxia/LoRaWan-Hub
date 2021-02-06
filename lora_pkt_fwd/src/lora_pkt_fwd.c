/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
  (C)2013 Semtech-Cycleo

Description:
    Configure Lora concentrator and forward packets to a server
    Use GPS for packet timestamping.
    Send a becon at a regular interval without server intervention

License: Revised BSD License, see LICENSE.TXT file include in the project
Maintainer: Michael Coracin
*/


/* -------------------------------------------------------------------------- */
/* --- DEPENDANCIES --------------------------------------------------------- */

/* fix an issue between POSIX and C99 */
#if __STDC_VERSION__ >= 199901L
    #define _XOPEN_SOURCE 600
#else
    #define _XOPEN_SOURCE 500
#endif

#include <stdint.h>         /* C99 types */
#include <stdbool.h>        /* bool type */
#include <stdio.h>          /* printf, fprintf, snprintf, fopen, fputs */

#include <string.h>         /* memset */
#include <signal.h>         /* sigaction */
#include <time.h>           /* time, clock_gettime, strftime, gmtime */
#include <sys/time.h>       /* timeval */
#include <unistd.h>         /* getopt, access */
#include <stdlib.h>         /* atoi, exit */
#include <errno.h>          /* error messages */
#include <math.h>           /* modf */
#include <assert.h>

#include <sys/socket.h>     /* socket specific definitions */
#include <netinet/in.h>     /* INET constants and stuff */
#include <arpa/inet.h>      /* IP address conversion stuff */
#include <netdb.h>          /* gai_strerror */

#include <pthread.h>
#include <time.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/uio.h>
#include<sys/errno.h>
#include <semaphore.h>

#include "trace.h"
#include "jitqueue.h"
#include "timersync.h"
#include "parson.h"
#include "base64.h"
#include "loragw_hal.h"
#include "loragw_gps.h"
#include "loragw_aux.h"
#include "loragw_reg.h"


#include "utilities.h"
#include "aes.h"
#include "cmac.h"
#include "LoRaMacCrypto.h"
#include <sqlite3.h>
#include <sys/time.h>

#include "common.h"
#include "lora_pkt_fwd.h"
#include "delay_queue.h"    /*  join延时任务队列        */
#include "task_queue.h"     /*  节点上报数据任务队列     */        
#include "data_queue.h"     /*  服务器应答数据存储队列   */
#include "classA_queue.h"   /*  classA调试数据存储队列  */
#include "region.h"         /*  区域配置文件           */  

/*---------------------------------------------------------LoRaWAN官网自有函数部分-----------------------------------------------------------*/
/* signal handling variables */
struct sigaction sigact; /* SIGQUIT&SIGINT&SIGTERM signal handling */
int exit_sig = 0; /* 1 -> application terminates cleanly (shut down hardware, close open files, etc) */
int quit_sig = 0; /* 1 -> application terminates without shutting down the hardware */
#if 1
/* TX gain LUT table */
static struct lgw_tx_gain_lut_s txgain_lut = 
{
    .size = 5,
    .lut[0] = {
        .dig_gain = 0,
        .pa_gain = 0,
        .dac_gain = 3,
        .mix_gain = 12,
        .rf_power = 0
    },
    .lut[1] = {
        .dig_gain = 0,
        .pa_gain = 1,
        .dac_gain = 3,
        .mix_gain = 12,
        .rf_power = 10
    },
    .lut[2] = {
        .dig_gain = 0,
        .pa_gain = 2,
        .dac_gain = 3,
        .mix_gain = 10,
        .rf_power = 14
    },
    .lut[3] = {
        .dig_gain = 0,
        .pa_gain = 3,
        .dac_gain = 3,
        .mix_gain = 9,
        .rf_power = 20
    },
    .lut[4] = {
        .dig_gain = 0,
        .pa_gain = 3,
        .dac_gain = 3,
        .mix_gain = 14,
        .rf_power = 27
    }
};
#endif

/*----------------------------------------------------------------------------*/
/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

#define ARRAY_SIZE(a)   (sizeof(a) / sizeof((a)[0]))
#define STRINGIFY(x)    #x
#define STR(x)          STRINGIFY(x)

/* -------------------------------------------------------------------------- */
/* --- PRIVATE CONSTANTS ---------------------------------------------------- */

#ifndef VERSION_STRING
  #define VERSION_STRING "undefined"
#endif

#define DEFAULT_SERVER      127.0.0.1   /* hostname also supported */
#define DEFAULT_PORT_UP     1780
#define DEFAULT_PORT_DW     1782
#define DEFAULT_KEEPALIVE   5           /* default time interval for downstream keep-alive packet */
#define DEFAULT_STAT        30          /* default time interval for statistics */
#define PUSH_TIMEOUT_MS     100
#define PULL_TIMEOUT_MS     200
#define GPS_REF_MAX_AGE     30          /* maximum admitted delay in seconds of GPS loss before considering latest GPS sync unusable */
#define FETCH_SLEEP_MS      10          /* nb of ms waited when a fetch return no packets */
#define BEACON_POLL_MS      50          /* time in ms between polling of beacon TX status */

#define PROTOCOL_VERSION    2           /* v1.3 */

#define XERR_INIT_AVG       128         /* nb of measurements the XTAL correction is averaged on as initial value */
#define XERR_FILT_COEF      256         /* coefficient for low-pass XTAL error tracking */

#define PKT_PUSH_DATA   0
#define PKT_PUSH_ACK    1
#define PKT_PULL_DATA   2
#define PKT_PULL_RESP   3
#define PKT_PULL_ACK    4
#define PKT_TX_ACK      5

#define NB_PKT_MAX      8 /* max number of packets per fetch/send cycle */

#define MIN_LORA_PREAMB 6 /* minimum Lora preamble length for this application */
#define STD_LORA_PREAMB 8
#define MIN_FSK_PREAMB  3 /* minimum FSK preamble length for this application */
#define STD_FSK_PREAMB  5

#define STATUS_SIZE     200
#define TX_BUFF_SIZE    ((540 * NB_PKT_MAX) + 30 + STATUS_SIZE)

#define UNIX_GPS_EPOCH_OFFSET 315964800 /* Number of seconds ellapsed between 01.Jan.1970 00:00:00
                                                                          and 06.Jan.1980 00:00:00 */

#define DEFAULT_BEACON_FREQ_HZ      869525000
#define DEFAULT_BEACON_FREQ_NB      1
#define DEFAULT_BEACON_FREQ_STEP    0
#define DEFAULT_BEACON_DATARATE     9
#define DEFAULT_BEACON_BW_HZ        125000
#define DEFAULT_BEACON_POWER        14
#define DEFAULT_BEACON_INFODESC     0


/* -------------------------------------------------------------------------- */
/* --- PRIVATE VARIABLES (GLOBAL) ------------------------------------------- */

/* packets filtering configuration variables */
static bool fwd_valid_pkt = true; /* packets with PAYLOAD CRC OK are forwarded */
static bool fwd_error_pkt = false; /* packets with PAYLOAD CRC ERROR are NOT forwarded */
static bool fwd_nocrc_pkt = false; /* packets with NO PAYLOAD CRC are NOT forwarded */

/* network configuration variables */
static uint64_t lgwm = 0; /* Lora gateway MAC address */
static char serv_addr[64] = STR(DEFAULT_SERVER); /* address of the server (host name or IPv4/IPv6) */
static char serv_port_up[8] = STR(DEFAULT_PORT_UP); /* server port for upstream traffic */
static char serv_port_down[8] = STR(DEFAULT_PORT_DW); /* server port for downstream traffic */
static int keepalive_time = DEFAULT_KEEPALIVE; /* send a PULL_DATA request every X seconds, negative = disabled */

/* statistics collection configuration variables */
static unsigned stat_interval = DEFAULT_STAT; /* time interval (in sec) at which statistics are collected and displayed */

/* gateway <-> MAC protocol variables */
static uint32_t net_mac_h; /* Most Significant Nibble, network order */
static uint32_t net_mac_l; /* Least Significant Nibble, network order */

/* network sockets */
static int sock_up; /* socket for upstream traffic */
static int sock_down; /* socket for downstream traffic */

/* network protocol variables */
static struct timeval push_timeout_half = {0, (PUSH_TIMEOUT_MS * 500)}; /* cut in half, critical for throughput */
static struct timeval pull_timeout = {0, (PULL_TIMEOUT_MS * 1000)}; /* non critical for throughput */

/* hardware access control and correction */
pthread_mutex_t mx_concent = PTHREAD_MUTEX_INITIALIZER; /* control access to the concentrator */
//创建锁
pthread_mutex_t lorawan_send = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t mx_xcorr = PTHREAD_MUTEX_INITIALIZER; /* control access to the XTAL correction */
static bool xtal_correct_ok = false; /* set true when XTAL correction is stable enough */
static double xtal_correct = 1.0;

/* GPS configuration and synchronization */
static char gps_tty_path[64] = "\0"; /* path of the TTY port GPS is connected on */
static int gps_tty_fd = -1; /* file descriptor of the GPS TTY port */
static bool gps_enabled = false; /* is GPS enabled on that gateway ? */

/* GPS time reference */
static pthread_mutex_t mx_timeref = PTHREAD_MUTEX_INITIALIZER; /* control access to GPS time reference */
static bool gps_ref_valid; /* is GPS reference acceptable (ie. not too old) */
static struct tref time_reference_gps; /* time reference used for GPS <-> timestamp conversion */

/* Reference coordinates, for broadcasting (beacon) */
static struct coord_s reference_coord;

/* Enable faking the GPS coordinates of the gateway */
static bool gps_fake_enable; /* enable the feature */

#if 0
/* measurements to establish statistics */
static pthread_mutex_t mx_meas_up = PTHREAD_MUTEX_INITIALIZER; /* control access to the upstream measurements */
static uint32_t meas_nb_rx_rcv = 0; /* count packets received */
static uint32_t meas_nb_rx_ok = 0; /* count packets received with PAYLOAD CRC OK */
static uint32_t meas_nb_rx_bad = 0; /* count packets received with PAYLOAD CRC ERROR */
static uint32_t meas_nb_rx_nocrc = 0; /* count packets received with NO PAYLOAD CRC */
static uint32_t meas_up_pkt_fwd = 0; /* number of radio packet forwarded to the server */
static uint32_t meas_up_network_byte = 0; /* sum of UDP bytes sent for upstream traffic */
static uint32_t meas_up_payload_byte = 0; /* sum of radio payload bytes sent for upstream traffic */
static uint32_t meas_up_dgram_sent = 0; /* number of datagrams sent for upstream traffic */
static uint32_t meas_up_ack_rcv = 0; /* number of datagrams acknowledged for upstream traffic */
#endif

static pthread_mutex_t mx_meas_dw = PTHREAD_MUTEX_INITIALIZER; /* control access to the downstream measurements */
static uint32_t meas_dw_pull_sent = 0; /* number of PULL requests sent for downstream traffic */
static uint32_t meas_dw_ack_rcv = 0; /* number of PULL requests acknowledged for downstream traffic */
static uint32_t meas_dw_dgram_rcv = 0; /* count PULL response packets received for downstream traffic */
static uint32_t meas_dw_network_byte = 0; /* sum of UDP bytes sent for upstream traffic */
static uint32_t meas_dw_payload_byte = 0; /* sum of radio payload bytes sent for upstream traffic */
static uint32_t meas_nb_tx_ok = 0; /* count packets emitted successfully */
static uint32_t meas_nb_tx_fail = 0; /* count packets were TX failed for other reasons */
static uint32_t meas_nb_tx_requested = 0; /* count TX request from server (downlinks) */
static uint32_t meas_nb_tx_rejected_collision_packet = 0; /* count packets were TX request were rejected due to collision with another packet already programmed */
static uint32_t meas_nb_tx_rejected_collision_beacon = 0; /* count packets were TX request were rejected due to collision with a beacon already programmed */
static uint32_t meas_nb_tx_rejected_too_late = 0; /* count packets were TX request were rejected because it is too late to program it */
static uint32_t meas_nb_tx_rejected_too_early = 0; /* count packets were TX request were rejected because timestamp is too much in advance */
static uint32_t meas_nb_beacon_queued = 0; /* count beacon inserted in jit queue */
static uint32_t meas_nb_beacon_sent = 0; /* count beacon actually sent to concentrator */
static uint32_t meas_nb_beacon_rejected = 0; /* count beacon rejected for queuing */

static pthread_mutex_t mx_meas_gps = PTHREAD_MUTEX_INITIALIZER; /* control access to the GPS statistics */
static bool gps_coord_valid; /* could we get valid GPS coordinates ? */
static struct coord_s meas_gps_coord; /* GPS position of the gateway */
static struct coord_s meas_gps_err; /* GPS position of the gateway */

//static pthread_mutex_t mx_stat_rep = PTHREAD_MUTEX_INITIALIZER; /* control access to the status report */
static bool report_ready = false; /* true when there is a new report to send to the server */
static char status_report[STATUS_SIZE]; /* status report as a JSON object */

/* beacon parameters */
static uint32_t beacon_period = 0; /* set beaconing period, must be a sub-multiple of 86400, the nb of sec in a day */
static uint32_t beacon_freq_hz = DEFAULT_BEACON_FREQ_HZ; /* set beacon TX frequency, in Hz */
static uint8_t beacon_freq_nb = DEFAULT_BEACON_FREQ_NB; /* set number of beaconing channels beacon */
static uint32_t beacon_freq_step = DEFAULT_BEACON_FREQ_STEP; /* set frequency step between beacon channels, in Hz */
static uint8_t beacon_datarate = DEFAULT_BEACON_DATARATE; /* set beacon datarate (SF) */
static uint32_t beacon_bw_hz = DEFAULT_BEACON_BW_HZ; /* set beacon bandwidth, in Hz */
static int8_t beacon_power = DEFAULT_BEACON_POWER; /* set beacon TX power, in dBm */
static uint8_t beacon_infodesc = DEFAULT_BEACON_INFODESC; /* set beacon information descriptor */

/* auto-quit function */
static uint32_t autoquit_threshold = 0; /* enable auto-quit after a number of non-acknowledged PULL_DATA (0 = disabled)*/

/* Just In Time TX scheduling */
static struct jit_queue_s jit_queue;

/* Gateway specificities */
static int8_t antenna_gain = 0;

/* TX capabilities */
static struct lgw_tx_gain_lut_s txlut; /* TX gain table */
static uint32_t tx_freq_min[LGW_RF_CHAIN_NB]; /* lowest frequency supported by TX chain */
static uint32_t tx_freq_max[LGW_RF_CHAIN_NB]; /* highest frequency supported by TX chain */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE FUNCTIONS DECLARATION ---------------------------------------- */

static void sig_handler(int sigio);

static int parse_SX1301_configuration(const char * conf_file);

static int parse_gateway_configuration(const char * conf_file);

static uint16_t crc16(const uint8_t * data, unsigned size);

static double difftimespec(struct timespec end, struct timespec beginning);

static void gps_process_sync(void);

static void gps_process_coords(void);

/* threads */
void thread_up(void);
void thread_down(void);
void thread_gps(void);
void thread_valid(void);
void thread_jit(void);
void thread_timersync(void);

/* -------------------------------------------------------------------------- */
/* --- PRIVATE FUNCTIONS DEFINITION ----------------------------------------- */

#if 0
static void sig_handler(int sigio) 
{
    if (sigio == SIGQUIT) 
	{
        quit_sig = true;
    } 
	else if ((sigio == SIGINT) || (sigio == SIGTERM)) 
	{
        exit_sig = true;
    }
    return;
}
#endif

static int parse_SX1301_configuration(const char * conf_file) 
{
    int i;
    char param_name[32]; /* used to generate variable parameter names */
    const char *str; /* used to store string value from JSON object */
    const char conf_obj_name[] = "SX1301_conf";
    JSON_Value *root_val = NULL;
    JSON_Object *conf_obj = NULL;
    JSON_Object *conf_lbt_obj = NULL;
    JSON_Object *conf_lbtchan_obj = NULL;
    JSON_Value *val = NULL;
    JSON_Array *conf_array = NULL;
    struct lgw_conf_board_s boardconf;
    struct lgw_conf_lbt_s lbtconf;
    struct lgw_conf_rxrf_s rfconf;
    struct lgw_conf_rxif_s ifconf;
    uint32_t sf, bw, fdev;

    /* try to parse JSON */
    root_val = json_parse_file_with_comments(conf_file);
    if (root_val == NULL) 
	{
        MSG("ERROR: %s is not a valid JSON file\n", conf_file);
        exit(EXIT_FAILURE);
    }

    /* point to the gateway configuration object */
    conf_obj = json_object_get_object(json_value_get_object(root_val), conf_obj_name);
    if (conf_obj == NULL) 
	{
        MSG("INFO: %s does not contain a JSON object named %s\n", conf_file, conf_obj_name);
        return -1;
    } 
	else 
	{
        MSG("INFO: %s does contain a JSON object named %s, parsing SX1301 parameters\n", conf_file, conf_obj_name);
    }

    /* set board configuration */
    memset(&boardconf, 0, sizeof boardconf); /* initialize configuration structure */
    val = json_object_get_value(conf_obj, "lorawan_public"); /* fetch value (if possible) */
    if (json_value_get_type(val) == JSONBoolean) 
	{
        boardconf.lorawan_public = (bool)json_value_get_boolean(val);
    } 
	else 
	{
        MSG("WARNING: Data type for lorawan_public seems wrong, please check\n");
        boardconf.lorawan_public = false;
    }
    val = json_object_get_value(conf_obj, "clksrc"); /* fetch value (if possible) */
    if (json_value_get_type(val) == JSONNumber) 
	{
        boardconf.clksrc = (uint8_t)json_value_get_number(val);
    } 
	else 
	{
        MSG("WARNING: Data type for clksrc seems wrong, please check\n");
        boardconf.clksrc = 0;
    }
    MSG("INFO: lorawan_public %d, clksrc %d\n", boardconf.lorawan_public, boardconf.clksrc);
    /* all parameters parsed, submitting configuration to the HAL */
    if (lgw_board_setconf(boardconf) != LGW_HAL_SUCCESS) 
	{
        MSG("ERROR: Failed to configure board\n");
        return -1;
    }

    /* set LBT configuration */
    memset(&lbtconf, 0, sizeof lbtconf); /* initialize configuration structure */
    conf_lbt_obj = json_object_get_object(conf_obj, "lbt_cfg"); /* fetch value (if possible) */
    if (conf_lbt_obj == NULL) 
	{
        MSG("INFO: no configuration for LBT\n");
    } 
	else 
	{
        val = json_object_get_value(conf_lbt_obj, "enable"); /* fetch value (if possible) */
        if (json_value_get_type(val) == JSONBoolean) 
		{
            lbtconf.enable = (bool)json_value_get_boolean(val);
        } 
		else 
		{
            MSG("WARNING: Data type for lbt_cfg.enable seems wrong, please check\n");
            lbtconf.enable = false;
        }
        if (lbtconf.enable == true) 
		{
            val = json_object_get_value(conf_lbt_obj, "rssi_target"); /* fetch value (if possible) */
            if (json_value_get_type(val) == JSONNumber) 
			{
                lbtconf.rssi_target = (int8_t)json_value_get_number(val);
            } 
			else 
			{
                MSG("WARNING: Data type for lbt_cfg.rssi_target seems wrong, please check\n");
                lbtconf.rssi_target = 0;
            }
            val = json_object_get_value(conf_lbt_obj, "sx127x_rssi_offset"); /* fetch value (if possible) */
            if (json_value_get_type(val) == JSONNumber) 
			{
                lbtconf.rssi_offset = (int8_t)json_value_get_number(val);
            } 
			else 
			{
                MSG("WARNING: Data type for lbt_cfg.sx127x_rssi_offset seems wrong, please check\n");
                lbtconf.rssi_offset = 0;
            }
            /* set LBT channels configuration */
            conf_array = json_object_get_array(conf_lbt_obj, "chan_cfg");
            if (conf_array != NULL) 
			{
                lbtconf.nb_channel = json_array_get_count( conf_array );
                MSG("INFO: %u LBT channels configured\n", lbtconf.nb_channel);
            }
            for (i = 0; i < (int)lbtconf.nb_channel; i++) 
			{
                /* Sanity check */
                if (i >= LBT_CHANNEL_FREQ_NB)
                {
                    MSG("ERROR: LBT channel %d not supported, skip it\n", i );
                    break;
                }
                /* Get LBT channel configuration object from array */
                conf_lbtchan_obj = json_array_get_object(conf_array, i);

                /* Channel frequency */
                val = json_object_dotget_value(conf_lbtchan_obj, "freq_hz"); /* fetch value (if possible) */
                if (json_value_get_type(val) == JSONNumber) 
				{
                    lbtconf.channels[i].freq_hz = (uint32_t)json_value_get_number(val);
                } 
				else 
				{
                    MSG("WARNING: Data type for lbt_cfg.channels[%d].freq_hz seems wrong, please check\n", i);
                    lbtconf.channels[i].freq_hz = 0;
                }

                /* Channel scan time */
                val = json_object_dotget_value(conf_lbtchan_obj, "scan_time_us"); /* fetch value (if possible) */
                if (json_value_get_type(val) == JSONNumber) 
				{
                    lbtconf.channels[i].scan_time_us = (uint16_t)json_value_get_number(val);
                } 
				else 
				{
                    MSG("WARNING: Data type for lbt_cfg.channels[%d].scan_time_us seems wrong, please check\n", i);
                    lbtconf.channels[i].scan_time_us = 0;
                }
            }

            /* all parameters parsed, submitting configuration to the HAL */
            if (lgw_lbt_setconf(lbtconf) != LGW_HAL_SUCCESS) 
			{
                MSG("ERROR: Failed to configure LBT\n");
                return -1;
            }
        } 
		else 
		{
            MSG("INFO: LBT is disabled\n");
        }
    }

    /* set antenna gain configuration */
    val = json_object_get_value(conf_obj, "antenna_gain"); /* fetch value (if possible) */
    if (val != NULL) 
	{
        if (json_value_get_type(val) == JSONNumber) 
		{
            antenna_gain = (int8_t)json_value_get_number(val);
        } 
		else 
		{
            MSG("WARNING: Data type for antenna_gain seems wrong, please check\n");
            antenna_gain = 0;
        }
    }
    MSG("INFO: antenna_gain %d dBi\n", antenna_gain);

    /* set configuration for tx gains */
    memset(&txlut, 0, sizeof txlut); /* initialize configuration structure */
    for (i = 0; i < TX_GAIN_LUT_SIZE_MAX; i++) 
	{
        snprintf(param_name, sizeof param_name, "tx_lut_%i", i); /* compose parameter path inside JSON structure */
        val = json_object_get_value(conf_obj, param_name); /* fetch value (if possible) */
        if (json_value_get_type(val) != JSONObject) 
        {
            MSG("INFO: no configuration for tx gain lut %i\n", i);
            continue;
        }
        txlut.size++; /* update TX LUT size based on JSON object found in configuration file */
        /* there is an object to configure that TX gain index, let's parse it */
        snprintf(param_name, sizeof param_name, "tx_lut_%i.pa_gain", i);
        val = json_object_dotget_value(conf_obj, param_name);
        if (json_value_get_type(val) == JSONNumber) 
        {
            txlut.lut[i].pa_gain = (uint8_t)json_value_get_number(val);
        } 
        else 
        {
            MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
            txlut.lut[i].pa_gain = 0;
        }
        snprintf(param_name, sizeof param_name, "tx_lut_%i.dac_gain", i);
        val = json_object_dotget_value(conf_obj, param_name);
        if (json_value_get_type(val) == JSONNumber) 
        {
            txlut.lut[i].dac_gain = (uint8_t)json_value_get_number(val);
        } 
        else 
        {
            txlut.lut[i].dac_gain = 3; /* This is the only dac_gain supported for now */
        }
        snprintf(param_name, sizeof param_name, "tx_lut_%i.dig_gain", i);
        val = json_object_dotget_value(conf_obj, param_name);
        if (json_value_get_type(val) == JSONNumber) 
        {
            txlut.lut[i].dig_gain = (uint8_t)json_value_get_number(val);
        } 
        else 
        {
            MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
            txlut.lut[i].dig_gain = 0;
        }
        snprintf(param_name, sizeof param_name, "tx_lut_%i.mix_gain", i);
        val = json_object_dotget_value(conf_obj, param_name);
        if (json_value_get_type(val) == JSONNumber) 
        {
            txlut.lut[i].mix_gain = (uint8_t)json_value_get_number(val);
        } 
        else 
        {
            MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
            txlut.lut[i].mix_gain = 0;
        }
        snprintf(param_name, sizeof param_name, "tx_lut_%i.rf_power", i);
        val = json_object_dotget_value(conf_obj, param_name);
        if (json_value_get_type(val) == JSONNumber) 
        {
            txlut.lut[i].rf_power = (int8_t)json_value_get_number(val);
        } 
        else 
        {
            MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
            txlut.lut[i].rf_power = 0;
        }
    }
    /* all parameters parsed, submitting configuration to the HAL */
    if (txlut.size > 0) 
    {
        MSG("INFO: Configuring TX LUT with %u indexes\n", txlut.size);
        if (lgw_txgain_setconf(&txlut) != LGW_HAL_SUCCESS) 
        {
            MSG("ERROR: Failed to configure concentrator TX Gain LUT\n");
            return -1;
        }
    } 
    else 
    {
        MSG("WARNING: No TX gain LUT defined\n");
    }

    /* set configuration for RF chains */
    for (i = 0; i < LGW_RF_CHAIN_NB; ++i) 
    {
        memset(&rfconf, 0, sizeof rfconf); /* initialize configuration structure */
        snprintf(param_name, sizeof param_name, "radio_%i", i); /* compose parameter path inside JSON structure */
        val = json_object_get_value(conf_obj, param_name); /* fetch value (if possible) */
        if (json_value_get_type(val) != JSONObject) 
        {
            MSG("INFO: no configuration for radio %i\n", i);
            continue;
        }
        /* there is an object to configure that radio, let's parse it */
        snprintf(param_name, sizeof param_name, "radio_%i.enable", i);
        val = json_object_dotget_value(conf_obj, param_name);
        if (json_value_get_type(val) == JSONBoolean) 
        {
            rfconf.enable = (bool)json_value_get_boolean(val);
        } 
        else 
        {
            rfconf.enable = false;
        }
        if (rfconf.enable == false) 
        { /* radio disabled, nothing else to parse */
            MSG("INFO: radio %i disabled\n", i);
        } 
        else  
        { /* radio enabled, will parse the other parameters */
            snprintf(param_name, sizeof param_name, "radio_%i.freq", i);
            rfconf.freq_hz = (uint32_t)json_object_dotget_number(conf_obj, param_name);
            snprintf(param_name, sizeof param_name, "radio_%i.rssi_offset", i);
            rfconf.rssi_offset = (float)json_object_dotget_number(conf_obj, param_name);
            snprintf(param_name, sizeof param_name, "radio_%i.type", i);
            str = json_object_dotget_string(conf_obj, param_name);
            if (!strncmp(str, "SX1255", 6)) 
            {
                rfconf.type = LGW_RADIO_TYPE_SX1255;
            } 
            else if (!strncmp(str, "SX1257", 6)) 
            {
                rfconf.type = LGW_RADIO_TYPE_SX1257;
            } 
            else 
            {
                MSG("WARNING: invalid radio type: %s (should be SX1255 or SX1257)\n", str);
            }
            snprintf(param_name, sizeof param_name, "radio_%i.tx_enable", i);
            val = json_object_dotget_value(conf_obj, param_name);
            if (json_value_get_type(val) == JSONBoolean) 
            {
                rfconf.tx_enable = (bool)json_value_get_boolean(val);
                if (rfconf.tx_enable == true) 
                {
                    /* tx is enabled on this rf chain, we need its frequency range */
                    snprintf(param_name, sizeof param_name, "radio_%i.tx_freq_min", i);
                    tx_freq_min[i] = (uint32_t)json_object_dotget_number(conf_obj, param_name);
                    snprintf(param_name, sizeof param_name, "radio_%i.tx_freq_max", i);
                    tx_freq_max[i] = (uint32_t)json_object_dotget_number(conf_obj, param_name);
                    if ((tx_freq_min[i] == 0) || (tx_freq_max[i] == 0)) 
                    {
                        MSG("WARNING: no frequency range specified for TX rf chain %d\n", i);
                    }
                    /* ... and the notch filter frequency to be set */
                    snprintf(param_name, sizeof param_name, "radio_%i.tx_notch_freq", i);
                    rfconf.tx_notch_freq = (uint32_t)json_object_dotget_number(conf_obj, param_name);
                }
            } 
            else 
            {
                rfconf.tx_enable = false;
            }
            MSG("INFO: radio %i enabled (type %s), center frequency %u, RSSI offset %f, tx enabled %d, tx_notch_freq %u\n", i, str, rfconf.freq_hz, rfconf.rssi_offset, rfconf.tx_enable, rfconf.tx_notch_freq);
        }
        /* all parameters parsed, submitting configuration to the HAL */
        if (lgw_rxrf_setconf(i, rfconf) != LGW_HAL_SUCCESS) 
		{
            MSG("ERROR: invalid configuration for radio %i\n", i);
            return -1;
        }
    }

    /* set configuration for Lora multi-SF channels (bandwidth cannot be set) */
    for (i = 0; i < LGW_MULTI_NB; ++i) 
    {
        memset(&ifconf, 0, sizeof ifconf); /* initialize configuration structure */
        snprintf(param_name, sizeof param_name, "chan_multiSF_%i", i); /* compose parameter path inside JSON structure */
        val = json_object_get_value(conf_obj, param_name); /* fetch value (if possible) */
        if (json_value_get_type(val) != JSONObject) {
            MSG("INFO: no configuration for Lora multi-SF channel %i\n", i);
            continue;
        }
        /* there is an object to configure that Lora multi-SF channel, let's parse it */
        snprintf(param_name, sizeof param_name, "chan_multiSF_%i.enable", i);
        val = json_object_dotget_value(conf_obj, param_name);
        if (json_value_get_type(val) == JSONBoolean) {
            ifconf.enable = (bool)json_value_get_boolean(val);
        } else {
            ifconf.enable = false;
        }
        if (ifconf.enable == false) { /* Lora multi-SF channel disabled, nothing else to parse */
            MSG("INFO: Lora multi-SF channel %i disabled\n", i);
        } else  { /* Lora multi-SF channel enabled, will parse the other parameters */
            snprintf(param_name, sizeof param_name, "chan_multiSF_%i.radio", i);
            ifconf.rf_chain = (uint32_t)json_object_dotget_number(conf_obj, param_name);
            snprintf(param_name, sizeof param_name, "chan_multiSF_%i.if", i);
            ifconf.freq_hz = (int32_t)json_object_dotget_number(conf_obj, param_name);
            // TODO: handle individual SF enabling and disabling (spread_factor)
            MSG("INFO: Lora multi-SF channel %i>  radio %i, IF %i Hz, 125 kHz bw, SF 7 to 12\n", i, ifconf.rf_chain, ifconf.freq_hz);
        }
        /* all parameters parsed, submitting configuration to the HAL */
        if (lgw_rxif_setconf(i, ifconf) != LGW_HAL_SUCCESS) {
            MSG("ERROR: invalid configuration for Lora multi-SF channel %i\n", i);
            return -1;
        }
    }

    /* set configuration for Lora standard channel */
    memset(&ifconf, 0, sizeof ifconf); /* initialize configuration structure */
    val = json_object_get_value(conf_obj, "chan_Lora_std"); /* fetch value (if possible) */
    if (json_value_get_type(val) != JSONObject) {
        MSG("INFO: no configuration for Lora standard channel\n");
    } else {
        val = json_object_dotget_value(conf_obj, "chan_Lora_std.enable");
        if (json_value_get_type(val) == JSONBoolean) {
            ifconf.enable = (bool)json_value_get_boolean(val);
        } else {
            ifconf.enable = false;
        }
        if (ifconf.enable == false) {
            MSG("INFO: relayed by Lora standard channel %i disabled\n", i);
        } else  {
            ifconf.rf_chain = (uint32_t)json_object_dotget_number(conf_obj, "chan_Lora_std.radio");
            ifconf.freq_hz = (int32_t)json_object_dotget_number(conf_obj, "chan_Lora_std.if");
            bw = (uint32_t)json_object_dotget_number(conf_obj, "chan_Lora_std.bandwidth");
            switch(bw) {
                case 500000: ifconf.bandwidth = BW_500KHZ; break;
                case 250000: ifconf.bandwidth = BW_250KHZ; break;
                case 125000: ifconf.bandwidth = BW_125KHZ; break;
                default: ifconf.bandwidth = BW_UNDEFINED;
            }
            sf = (uint32_t)json_object_dotget_number(conf_obj, "chan_Lora_std.spread_factor");
            switch(sf) {
                case  7: ifconf.datarate = DR_LORA_SF7;  break;
                case  8: ifconf.datarate = DR_LORA_SF8;  break;
                case  9: ifconf.datarate = DR_LORA_SF9;  break;
                case 10: ifconf.datarate = DR_LORA_SF10; break;
                case 11: ifconf.datarate = DR_LORA_SF11; break;
                case 12: ifconf.datarate = DR_LORA_SF12; break;
                default: ifconf.datarate = DR_UNDEFINED;
            }
            MSG("INFO: Lora std channel> radio %i, IF %i Hz, %u Hz bw, SF %u\n", ifconf.rf_chain, ifconf.freq_hz, bw, sf);
        }
        if (lgw_rxif_setconf(8, ifconf) != LGW_HAL_SUCCESS) {
            MSG("ERROR: invalid configuration for Lora standard channel\n");
            return -1;
        }
    }

    /* set configuration for FSK channel */
    memset(&ifconf, 0, sizeof ifconf); /* initialize configuration structure */
    val = json_object_get_value(conf_obj, "chan_FSK"); /* fetch value (if possible) */
    if (json_value_get_type(val) != JSONObject) {
        MSG("INFO: no configuration for FSK channel\n");
    } else {
        val = json_object_dotget_value(conf_obj, "chan_FSK.enable");
        if (json_value_get_type(val) == JSONBoolean) {
            ifconf.enable = (bool)json_value_get_boolean(val);
        } else {
            ifconf.enable = false;
        }
        if (ifconf.enable == false) {
            MSG("INFO: FSK channel %i disabled\n", i);
        } else  {
            ifconf.rf_chain = (uint32_t)json_object_dotget_number(conf_obj, "chan_FSK.radio");
            ifconf.freq_hz = (int32_t)json_object_dotget_number(conf_obj, "chan_FSK.if");
            bw = (uint32_t)json_object_dotget_number(conf_obj, "chan_FSK.bandwidth");
            fdev = (uint32_t)json_object_dotget_number(conf_obj, "chan_FSK.freq_deviation");
            ifconf.datarate = (uint32_t)json_object_dotget_number(conf_obj, "chan_FSK.datarate");

            /* if chan_FSK.bandwidth is set, it has priority over chan_FSK.freq_deviation */
            if ((bw == 0) && (fdev != 0)) {
                bw = 2 * fdev + ifconf.datarate;
            }
            if      (bw == 0)      ifconf.bandwidth = BW_UNDEFINED;
            else if (bw <= 7800)   ifconf.bandwidth = BW_7K8HZ;
            else if (bw <= 15600)  ifconf.bandwidth = BW_15K6HZ;
            else if (bw <= 31200)  ifconf.bandwidth = BW_31K2HZ;
            else if (bw <= 62500)  ifconf.bandwidth = BW_62K5HZ;
            else if (bw <= 125000) ifconf.bandwidth = BW_125KHZ;
            else if (bw <= 250000) ifconf.bandwidth = BW_250KHZ;
            else if (bw <= 500000) ifconf.bandwidth = BW_500KHZ;
            else ifconf.bandwidth = BW_UNDEFINED;

            MSG("INFO: FSK channel> radio %i, IF %i Hz, %u Hz bw, %u bps datarate\n", ifconf.rf_chain, ifconf.freq_hz, bw, ifconf.datarate);
        }
        if (lgw_rxif_setconf(9, ifconf) != LGW_HAL_SUCCESS) {
            MSG("ERROR: invalid configuration for FSK channel\n");
            return -1;
        }
    }
    json_serialize_to_file(root_val,conf_file); 
    json_value_free(root_val);

    return 0;
}

static int parse_gateway_configuration(const char * conf_file) 
{
    const char conf_obj_name[] = "gateway_conf";
    JSON_Value *root_val;
    JSON_Object *conf_obj = NULL;
    JSON_Value *val = NULL; /* needed to detect the absence of some fields */
    const char *str; /* pointer to sub-strings in the JSON data */
    unsigned long long ull = 0;

    /* try to parse JSON */
    root_val = json_parse_file_with_comments(conf_file);
    if (root_val == NULL) 
	{
        MSG("ERROR: %s is not a valid JSON file\n", conf_file);
        exit(EXIT_FAILURE);
    }

    /* point to the gateway configuration object */
    conf_obj = json_object_get_object(json_value_get_object(root_val), conf_obj_name);
    if (conf_obj == NULL) 
	{
        MSG("INFO: %s does not contain a JSON object named %s\n", conf_file, conf_obj_name);
        return -1;
    } 
	else 
	{
        MSG("INFO: %s does contain a JSON object named %s, parsing gateway parameters\n", conf_file, conf_obj_name);
    }

    /* gateway unique identifier (aka MAC address) (optional) */
    str = json_object_get_string(conf_obj, "gateway_ID");
    if (str != NULL) 
	{
        sscanf(str, "%llx", &ull);
        lgwm = ull;
        MSG("INFO: gateway MAC address is configured to %016llX\n", ull);
    }

    /* server hostname or IP address (optional) */
    str = json_object_get_string(conf_obj, "server_address");
    if (str != NULL) 
	{
        strncpy(serv_addr, str, sizeof serv_addr);
        MSG("INFO: server hostname or IP address is configured to \"%s\"\n", serv_addr);
    }

    /* get up and down ports (optional) */
    val = json_object_get_value(conf_obj, "serv_port_up");
    if (val != NULL) 
	{
        snprintf(serv_port_up, sizeof serv_port_up, "%u", (uint16_t)json_value_get_number(val));
        MSG("INFO: upstream port is configured to \"%s\"\n", serv_port_up);
    }
    val = json_object_get_value(conf_obj, "serv_port_down");
    if (val != NULL) {
        snprintf(serv_port_down, sizeof serv_port_down, "%u", (uint16_t)json_value_get_number(val));
        MSG("INFO: downstream port is configured to \"%s\"\n", serv_port_down);
    }

    /* get keep-alive interval (in seconds) for downstream (optional) */
    val = json_object_get_value(conf_obj, "keepalive_interval");
    if (val != NULL) {
        keepalive_time = (int)json_value_get_number(val);
        MSG("INFO: downstream keep-alive interval is configured to %u seconds\n", keepalive_time);
    }

    /* get interval (in seconds) for statistics display (optional) */
    val = json_object_get_value(conf_obj, "stat_interval");
    if (val != NULL) {
        stat_interval = (unsigned)json_value_get_number(val);
        MSG("INFO: statistics display interval is configured to %u seconds\n", stat_interval);
    }

    /* get time-out value (in ms) for upstream datagrams (optional) */
    val = json_object_get_value(conf_obj, "push_timeout_ms");
    if (val != NULL) {
        push_timeout_half.tv_usec = 500 * (long int)json_value_get_number(val);
        MSG("INFO: upstream PUSH_DATA time-out is configured to %u ms\n", (unsigned)(push_timeout_half.tv_usec / 500));
    }

    /* packet filtering parameters */
    val = json_object_get_value(conf_obj, "forward_crc_valid");
    if (json_value_get_type(val) == JSONBoolean) {
        fwd_valid_pkt = (bool)json_value_get_boolean(val);
    }
    MSG("INFO: packets received with a valid CRC will%s be forwarded\n", (fwd_valid_pkt ? "" : " NOT"));
    val = json_object_get_value(conf_obj, "forward_crc_error");
    if (json_value_get_type(val) == JSONBoolean) {
        fwd_error_pkt = (bool)json_value_get_boolean(val);
    }
    MSG("INFO: packets received with a CRC error will%s be forwarded\n", (fwd_error_pkt ? "" : " NOT"));
    val = json_object_get_value(conf_obj, "forward_crc_disabled");
    if (json_value_get_type(val) == JSONBoolean) {
        fwd_nocrc_pkt = (bool)json_value_get_boolean(val);
    }
    MSG("INFO: packets received with no CRC will%s be forwarded\n", (fwd_nocrc_pkt ? "" : " NOT"));

    /* GPS module TTY path (optional) */
    str = json_object_get_string(conf_obj, "gps_tty_path");
    if (str != NULL) {
        strncpy(gps_tty_path, str, sizeof gps_tty_path);
        MSG("INFO: GPS serial port path is configured to \"%s\"\n", gps_tty_path);
    }

    /* get reference coordinates */
    val = json_object_get_value(conf_obj, "ref_latitude");
    if (val != NULL) {
        reference_coord.lat = (double)json_value_get_number(val);
        MSG("INFO: Reference latitude is configured to %f deg\n", reference_coord.lat);
    }
    val = json_object_get_value(conf_obj, "ref_longitude");
    if (val != NULL) {
        reference_coord.lon = (double)json_value_get_number(val);
        MSG("INFO: Reference longitude is configured to %f deg\n", reference_coord.lon);
    }
    val = json_object_get_value(conf_obj, "ref_altitude");
    if (val != NULL) {
        reference_coord.alt = (short)json_value_get_number(val);
        MSG("INFO: Reference altitude is configured to %i meters\n", reference_coord.alt);
    }

    /* Gateway GPS coordinates hardcoding (aka. faking) option */
    val = json_object_get_value(conf_obj, "fake_gps");
    if (json_value_get_type(val) == JSONBoolean) {
        gps_fake_enable = (bool)json_value_get_boolean(val);
        if (gps_fake_enable == true) {
            MSG("INFO: fake GPS is enabled\n");
        } else {
            MSG("INFO: fake GPS is disabled\n");
        }
    }

    /* Beacon signal period (optional) */
    val = json_object_get_value(conf_obj, "beacon_period");
    if (val != NULL) {
        beacon_period = (uint32_t)json_value_get_number(val);
        if ((beacon_period > 0) && (beacon_period < 6)) {
            MSG("ERROR: invalid configuration for Beacon period, must be >= 6s\n");
            return -1;
        } else {
            MSG("INFO: Beaconing period is configured to %u seconds\n", beacon_period);
        }
    }

    /* Beacon TX frequency (optional) */
    val = json_object_get_value(conf_obj, "beacon_freq_hz");
    if (val != NULL) {
        beacon_freq_hz = (uint32_t)json_value_get_number(val);
        MSG("INFO: Beaconing signal will be emitted at %u Hz\n", beacon_freq_hz);
    }

    /* Number of beacon channels (optional) */
    val = json_object_get_value(conf_obj, "beacon_freq_nb");
    if (val != NULL) {
        beacon_freq_nb = (uint8_t)json_value_get_number(val);
        MSG("INFO: Beaconing channel number is set to %u\n", beacon_freq_nb);
    }

    /* Frequency step between beacon channels (optional) */
    val = json_object_get_value(conf_obj, "beacon_freq_step");
    if (val != NULL) {
        beacon_freq_step = (uint32_t)json_value_get_number(val);
        MSG("INFO: Beaconing channel frequency step is set to %uHz\n", beacon_freq_step);
    }

    /* Beacon datarate (optional) */
    val = json_object_get_value(conf_obj, "beacon_datarate");
    if (val != NULL) {
        beacon_datarate = (uint8_t)json_value_get_number(val);
        MSG("INFO: Beaconing datarate is set to SF%d\n", beacon_datarate);
    }

    /* Beacon modulation bandwidth (optional) */
    val = json_object_get_value(conf_obj, "beacon_bw_hz");
    if (val != NULL) {
        beacon_bw_hz = (uint32_t)json_value_get_number(val);
        MSG("INFO: Beaconing modulation bandwidth is set to %dHz\n", beacon_bw_hz);
    }

    /* Beacon TX power (optional) */
    val = json_object_get_value(conf_obj, "beacon_power");
    if (val != NULL) {
        beacon_power = (int8_t)json_value_get_number(val);
        MSG("INFO: Beaconing TX power is set to %ddBm\n", beacon_power);
    }

    /* Beacon information descriptor (optional) */
    val = json_object_get_value(conf_obj, "beacon_infodesc");
    if (val != NULL) {
        beacon_infodesc = (uint8_t)json_value_get_number(val);
        MSG("INFO: Beaconing information descriptor is set to %u\n", beacon_infodesc);
    }

    /* Auto-quit threshold (optional) */
    val = json_object_get_value(conf_obj, "autoquit_threshold");
    if (val != NULL) {
        autoquit_threshold = (uint32_t)json_value_get_number(val);
        MSG("INFO: Auto-quit after %u non-acknowledged PULL_DATA\n", autoquit_threshold);
    }

    /* free JSON parsing data structure */
    json_serialize_to_file(root_val,conf_file); 
    json_value_free(root_val);
    return 0;
}

static uint16_t crc16(const uint8_t * data, unsigned size) 
{
    const uint16_t crc_poly = 0x1021;
    const uint16_t init_val = 0x0000;
    uint16_t x = init_val;
    unsigned i, j;

    if (data == NULL)  
	{
        return 0;
    }

    for (i=0; i<size; ++i) 
	{
        x ^= (uint16_t)data[i] << 8;
        for (j=0; j<8; ++j) 
		{
            x = (x & 0x8000) ? (x<<1) ^ crc_poly : (x<<1);
        }
    }

    return x;
}

static double difftimespec(struct timespec end, struct timespec beginning) 
{
    double x;

    x = 1E-9 * (double)(end.tv_nsec - beginning.tv_nsec);
    x += (double)(end.tv_sec - beginning.tv_sec);

    return x;
}

static int send_tx_ack(uint8_t token_h, uint8_t token_l, enum jit_error_e error) 
{
    uint8_t buff_ack[64]; /* buffer to give feedback to server */
    int buff_index;

    /* reset buffer */
    memset(&buff_ack, 0, sizeof buff_ack);

    /* Prepare downlink feedback to be sent to server */
    buff_ack[0] = PROTOCOL_VERSION;
    buff_ack[1] = token_h;
    buff_ack[2] = token_l;
    buff_ack[3] = PKT_TX_ACK;
    *(uint32_t *)(buff_ack + 4) = net_mac_h;
    *(uint32_t *)(buff_ack + 8) = net_mac_l;
    buff_index = 12; /* 12-byte header */

    /* Put no JSON string if there is nothing to report */
    if (error != JIT_ERROR_OK) 
	{
        /* start of JSON structure */
        memcpy((void *)(buff_ack + buff_index), (void *)"{\"txpk_ack\":{", 13);
        buff_index += 13;
        /* set downlink error status in JSON structure */
        memcpy((void *)(buff_ack + buff_index), (void *)"\"error\":", 8);
        buff_index += 8;
        switch (error) 
		{
            case JIT_ERROR_FULL:
            case JIT_ERROR_COLLISION_PACKET:
                memcpy((void *)(buff_ack + buff_index), (void *)"\"COLLISION_PACKET\"", 18);
                buff_index += 18;
                /* update stats */
                pthread_mutex_lock(&mx_meas_dw);
                meas_nb_tx_rejected_collision_packet += 1;
                pthread_mutex_unlock(&mx_meas_dw);
                break;
            case JIT_ERROR_TOO_LATE:
                memcpy((void *)(buff_ack + buff_index), (void *)"\"TOO_LATE\"", 10);
                buff_index += 10;
                /* update stats */
                pthread_mutex_lock(&mx_meas_dw);
                meas_nb_tx_rejected_too_late += 1;
                pthread_mutex_unlock(&mx_meas_dw);
                break;
            case JIT_ERROR_TOO_EARLY:
                memcpy((void *)(buff_ack + buff_index), (void *)"\"TOO_EARLY\"", 11);
                buff_index += 11;
                /* update stats */
                pthread_mutex_lock(&mx_meas_dw);
                meas_nb_tx_rejected_too_early += 1;
                pthread_mutex_unlock(&mx_meas_dw);
                break;
            case JIT_ERROR_COLLISION_BEACON:
                memcpy((void *)(buff_ack + buff_index), (void *)"\"COLLISION_BEACON\"", 18);
                buff_index += 18;
                /* update stats */
                pthread_mutex_lock(&mx_meas_dw);
                meas_nb_tx_rejected_collision_beacon += 1;
                pthread_mutex_unlock(&mx_meas_dw);
                break;
            case JIT_ERROR_TX_FREQ:
                memcpy((void *)(buff_ack + buff_index), (void *)"\"TX_FREQ\"", 9);
                buff_index += 9;
                break;
            case JIT_ERROR_TX_POWER:
                memcpy((void *)(buff_ack + buff_index), (void *)"\"TX_POWER\"", 10);
                buff_index += 10;
                break;
            case JIT_ERROR_GPS_UNLOCKED:
                memcpy((void *)(buff_ack + buff_index), (void *)"\"GPS_UNLOCKED\"", 14);
                buff_index += 14;
                break;
            default:
                memcpy((void *)(buff_ack + buff_index), (void *)"\"UNKNOWN\"", 9);
                buff_index += 9;
                break;
        }
        /* end of JSON structure */
        memcpy((void *)(buff_ack + buff_index), (void *)"}}", 2);
        buff_index += 2;
    }

    buff_ack[buff_index] = 0; /* add string terminator, for safety */

    /* send datagram to server */
    return send(sock_down, (void *)buff_ack, buff_index, 0);
}

/*----------------------------------------------------------------------------------------------------------------------------------------------------------------*/


/*--------------------------------------------------------------LORAWAN-HUB固件开发部分-----------------------------------------------------------------------------*/
/* Last Update:2019.3.14 */

/* sqlite3数据库表        */
/* 多线程访问，设成全局变量 */
sqlite3 *db;
char    *zErrMsg =NULL;
int     rc;
char    *sql = NULL;
const   char *data= "Callback function called";

/* 创建访问数据库表数据的锁 */
static pthread_mutex_t lorawan_table_parse      = PTHREAD_MUTEX_INITIALIZER;

/* 创建队列锁  */
pthread_mutex_t add_flag_task_mux               = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t subtr_flag_task_mux             = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t read_flag_task_mux              = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t queue_flag_task_mux             = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t rxbuff_temp_mux                 = PTHREAD_MUTEX_INITIALIZER;

/* 用于保护class a deveui调试信息的写入 读写锁 */
static  pthread_rwlock_t rw_class_a_deveui_mux  = PTHREAD_RWLOCK_INITIALIZER;

/* 创建队列 全局变量 */

/* join request 延时队列 */
Queue           line;

/* 节点上报数据 延时队列 */
Task_Queue      task_line; 

/* 服务器回复应答数据队列 */
Data_Queue      data_line;

/* class a 调试数据队列 */
class_a_Queue   class_a_line;

/* 信号同步变量 */
/* join 的信号量 */
sem_t Join_SemFlag;

/* 节点上报数据同步的信号量 */
sem_t Task_SemFlag;

/* 生产者信号量 */ 
sem_t Producer_SemFlag;

/* 消费者信号量 */
sem_t Customer_SemFlag;

/* 将解析成功的LoRaWan数据同步到服务器的信号量 */
sem_t Send_Parse_Data_To_Server_SemFlag;

/* 将解析成功的LoRaWan数据同步到上位机的信号量 */
sem_t Send_Parse_Data_To_GatewaySet_SemFlag;

/* join rewquest 数据入队信号量  */
sem_t Join_Data_Queue_SemFlag;

/* 发送 join request 信息到服务器的信号量*/
sem_t Send_Join_Data_Server_SemFlag;

/* 节点上报有效数据的信号量 */
sem_t Node_Data_queue_SemFlag;    

/* join时创建的线程标志 */
static unsigned int join_pthread_flag = 1;

/* 节点上报数据时创建的线程标志 */
static unsigned int node_data_pthread_flag = 1;

/* class c线程创建标志 */
static unsigned int class_c_pthread_flag = 1;

/* 节点向lora_pkt_conf.c上报解析后的数据 */
static unsigned int decode_data_pthread_flag = 1;

/* 定义 lgw_send()函数的发送状态  */
static  char *lgw_send_status[] = {"TX_STATUS_UNKNOWN", "TX_OFF", "TX_FREE", "TX_SCHEDULED","TX_EMITTING"};

/* 用于class a 调试信息的deveui的缓存 */
static  uint8_t ClassA_Deveui[8];

/* 判断是否成功获取devaddr的标志位 */
uint8_t Fetch_Address_Successful = 0;

/*  接收LoRaWan数据缓存区域 */
struct lgw_pkt_rx_s rxpkt[NB_PKT_MAX]; /* array containing inbound packets + metadata */

/* 序列化数据结构体 */
Serialization_data_type     struct_serialization_data;

/* 成功解析LoRaWan数据结构体 */
Decode_data                 struct_decode_data;

/* 创建下发入网数据包的线程 */
pthread_t   thread_send_queue;

/* 创建发送join request到lora_pkt_server.c的线程 */
pthread_t   thread_send_join;

/* 创建发送解密后的LoRaWan数据到lora_pkt_server.c的线程 */
pthread_t   thread_server_data;

/* 创建取节点上包数据的线程 */
pthread_t   thread_fetch_report_data;

/* 创建任务处理线程 */
pthread_t   thread_handle_task;

/* 创建数据存储任务线程 */
pthread_t   thread_data_store_task;


/* 创建class a调试数据存储线程 */
pthread_t   thread_classA_data_store_task;


/* 创建处理class c数据类型的线程 */
pthread_t   thread_class_c_task;

/* 创建上报lora_pkt_conf.c解析后的data */
pthread_t   thread_send_pkt_conf_decode_data;


/* 线程：join request 数据队列存储函数 */
void *Thread_Join_Queue(void *rxbuff);

/* 线程：join accept 数据包下发函数 */
void *Thread_Send_Queue_Task(void);

/* 线程：向服务器上报 join request 数据 */
void *Thread_Send_Join(void *rxbuff);

/* 线程：向服务器上报解密后LoRaWan 数据 */
void *Thread_Send_Server_Data(void* send_buff);

/* 线程：节点上报有效数据入队列操作 */
void *Thread_Task_Queue(void *databuff);

/* 线程：取节点上报入队列后的数据 */
void *Thread_Fetch_Report_Data();

/* 线程：下发应答LoRaWan数据 任务处理函数 */
void *Thread_Handle_Data_Task();

/* 线程：服务器应答数据存储 */
void *Thread_Data_Store_Task();

/* 线程：class a 调试数据存储线程 */
void *Thread_classA_Data_Store_Task();

/* 线程：处理class c 数据类型的线程 */
void *Thread_Handle_Class_C_Task();

/* 线程：向lora_pkt_conf.c上报解析后的数据 */
void *Thread_SendDecodeData(void* send_buff);

int 
main(int argc,char*argv[])
{        
    
    int i; /* loop variable and temporary variable for return value */
	int x;
    int pkt_data = 0;
	int  repeat = -1; /* by default, repeat until stopped */
    uint16_t cycle_count = 0;
    int err; 

    /* 配置sx1301的文件名 */
    char *global_cfg_path= "/lorawan/lorawan_hub/global_conf.json"; 

    /*  初始化接收数据缓存区*/
    memset(&rxpkt,0,sizeof(rxpkt));

    /* 打开数据库 */
    rc = sqlite3_open("/lorawan/lorawan_hub/hub.db",&db);
    if(rc != SQLITE_OK){

            fprintf(stderr,"Can't open database: %s\n",sqlite3_errmsg(db));
             exit(EXIT_FAILURE);

    } else {
            
            fprintf(stderr,"Open database successfully\n");            
    }

    /* 读入global_conf.json,SX1301本地配置文件 */
     if (access(global_cfg_path, R_OK) == 0) {

            MSG("INFO: found global configuration file %s, parsing it\n", global_cfg_path);

            /* 解析sx1301配置 */    
            x = parse_SX1301_configuration(global_cfg_path);
            if ( x != 0 ) {
                    
                    exit(EXIT_FAILURE);
            }

    } else {
        
                MSG("ERROR: [main] failed to find any configuration file named %s.\n", global_cfg_path);
                exit(EXIT_FAILURE);
    }
	
    /* 启动集中器 */
    i = lgw_start();
    if ( i == LGW_HAL_SUCCESS ) {
            
            MSG("INFO: [main] concentrator started, packet can now be received\n");
    
    } else {
        
                MSG("ERROR: [main] failed to start the concentrator\n");
                exit(EXIT_FAILURE);
    }         

    /* 初始化信号量 */							
	cycle_count = 0;
    sem_init(&Join_SemFlag,0,0);
    sem_init(&Task_SemFlag,0,0);
    sem_init(&Producer_SemFlag,0,0);
    sem_init(&Customer_SemFlag,0,0);
    sem_init(&Join_Data_Queue_SemFlag,0,0);
    sem_init(&Send_Join_Data_Server_SemFlag,0,0);
    sem_init(&Node_Data_queue_SemFlag,0,0);   
    sem_init(&Send_Parse_Data_To_Server_SemFlag,0,0);
    sem_init(&Send_Parse_Data_To_GatewaySet_SemFlag,0,0);

    /* main loop */  
	while ((repeat == -1) || (cycle_count < repeat))
	{
	 	++cycle_count;
		
        /* class c数据的处理线程 */
        if (1 == class_c_pthread_flag) 
        {

                err = pthread_create(&thread_class_c_task,NULL,Thread_Handle_Class_C_Task,NULL);
            
                if(err != 0){

                        DEBUG_CONF("can't create handle class c thread ");
                } else {
                
                        DEBUG_CONF("create class c pthread successfully\n");
                }

            /* 该线程只创建一次 */    
            class_c_pthread_flag=2;
        }

        /* class a调试数据页面 */
        if (2 == class_c_pthread_flag) 
        {

                err = pthread_create(&thread_classA_data_store_task, NULL,Thread_classA_Data_Store_Task, NULL);
                if(err != 0){

                        DEBUG_CONF("can't create task thread ");
                }else{
                        DEBUG_CONF("classA  data store pthread create successfully\n");
                }

                /* 该线程只创建一次 */    
                class_c_pthread_flag=3;
        }

        /* 从sx1301 buffer中取出LoRaWan数据包 */       	
        pthread_mutex_lock(&mx_concent);
		pkt_data = lgw_receive(8,rxpkt);
		pthread_mutex_unlock(&mx_concent); 

        if (pkt_data == LGW_HAL_ERROR) {

			    DEBUG_CONF("ERROR: [up] failed packet fetch, exiting\n");

                /* 修改读取错误策略，防止线程挂掉 */  
                lgw_abort_tx();      
                wait_ms(FETCH_SLEEP_MS);
			    continue;
		} 
        if (pkt_data == 0) {

			    wait_ms(FETCH_SLEEP_MS);
			    continue;
		}

        /*  lorawan 协议处理   */ 
        /*  fixed bug: 传递取出的数据包个数 pkt_data,不能传入最大包数：8 */
        pthread_mutex_lock(&lorawan_send);
		LoRaWAN_Parse(rxpkt,pkt_data,db,zErrMsg);
      	pthread_mutex_unlock(&lorawan_send);

		/* exit loop on user signals */
		if ((quit_sig == 1) || (exit_sig == 1)) 
		{
			    /* stop the hardware */
                i = lgw_stop();
                if (i == LGW_HAL_SUCCESS) {

                        DEBUG_CONF ("INFO: concentrator stopped successfully\n");

                } else {

                        DEBUG_CONF ("WARNING: failed to stop concentrator successfully\n");
                }
		}       
	}

    /* 预留给线程执行完成的时间*/    
    pthread_join(thread_send_queue,                     NULL);
    pthread_join(thread_send_join,                      NULL);
    pthread_join(thread_handle_task,                    NULL);
    pthread_join(thread_fetch_report_data,              NULL);
    pthread_join(thread_data_store_task,                NULL);
    pthread_join(thread_server_data,                    NULL);
    pthread_join(thread_class_c_task,                   NULL);
    pthread_join(thread_send_pkt_conf_decode_data,      NULL);
    pthread_join(thread_classA_data_store_task,         NULL);
    
    exit(EXIT_SUCCESS);

}

/*-------------------------------------------------------------------------------------------------------------------------------------------*/

/**\brief  LoRaWan协议解析函数
 *
 * 
 * \param 传入lgw_pkt_rx_s的结构体数组  nb_pkt为循环的次数
 * \param
 * \return  NULL
 *
 */

void LoRaWAN_Parse(struct lgw_pkt_rx_s *rxbuff,int nb_pkt,sqlite3 *db,char *zErrMsg)
{		

    /* loop 变量 */
	uint8_t i;
    int err;
    struct  lgw_pkt_tx_s  txbuff; 
	memset(&txbuff, 0, sizeof(txbuff));

	/* 定义发送至下行数据的接收数组 */
    uint8_t Down_buff[LORAMAC_PHY_MAXPAYLOAD];
	memset(Down_buff,0,sizeof(Down_buff));
	
    /* 数据包长度 */
	uint8_t  pktHeaderLen = 0;

	/* 记录载荷开始的地方 */
	uint8_t  appPayloadStartIndex = 0;
	uint16_t size = 0;

	/* mhdr */
	LoRaMacHeader_t	macHdr[nb_pkt];
	/* dveaddr */
	uint32_t address_node_up = 0;
	/* Fctrl */
	LoRaMacFrameCtrl_t fCtrl[nb_pkt];
	/* FPort */
	uint8_t  FPort = 0XFF;

	uint8_t  frameLen = 0;	
    uint8_t  address_buff[4];
    memset(address_buff,0,sizeof(address_buff));

    uint8_t server_deveui[8];
    memset(server_deveui,0,8);
    uint8_t server_appeui[8];
    memset(server_appeui,0,8);

    /* 序列化数据,发送数据的变量 */
    uint8_t   server_send_buff[300];

    /* 向服务器发送解析成功数据标志 */
	int        SEND_FLAG =0;

    /* 记录每次向server发送解密数据的大小 */
    uint16_t  server_send_len =0;

    /* 验证join mic所使用的变量 */
    uint8_t     join_deveui[8];
    uint8_t     join_appkey[16];
    uint32_t    micRx;
    uint32_t    join_mic;
    uint16_t    join_size;
    memset(join_deveui,0,8);
    memset(join_appkey,0,16);
    
    /* 验证节点上报数据所使用的变量 */
    uint32_t upLinkCounter = 0;
    uint16_t sequenceCounter = 0;
    uint16_t sequenceCounterPrev = 0;
    uint16_t sequenceCounterDiff = 0;
    bool isMicOk = false;
    uint32_t node_mic;
    
    /* 向lora_pkt_conf.c发送解析后的数据 */
    int SendDecodeDataFlag = 0;
    uint8_t  DecodeDataBuff[1000];
    uint16_t DecodeDataLen;
    memset(DecodeDataBuff,0,1000);

    /* 增加时间戳比较的阈值变量 */
    static uint32_t last_recvtimestamp;
    uint32_t diff_time;
    static   uint8_t first_recv_flag =1;  

    /* 指向 rx_pkt 数据包的指针 */
    struct lgw_pkt_rx_s *p = NULL;

    /* 创建存储 join request 数据入队线程的 线程id的数组变量 */
    pthread_t   thread_join_queue[8];
    
    /* 创建存储 join request 数据入队线程的 线程id的数组变量 */
    pthread_t   thread_report_data_queue[8];
    
    /* init */
    memset(thread_join_queue,        0, 8);
    memset(thread_report_data_queue, 0, 8);

	for (i=0;i < nb_pkt; i++)
	{
           /* 指针变量赋值 */
           p = &rxbuff[i];

           /* 接收 mType */
		   macHdr[i].Value = p->payload[0];
	   
		   switch(macHdr[i].Bits.MType)
		   {
          		case FRAME_TYPE_JOIN_REQ:
				{	
       
                    /** update:2019.3.6
                     *  增加对节点时间戳差值的筛选
                     *  测试lgw_send函数时常为6ms 
                     *  阈值设置为20ms = 6ms + 2ms + (裕度)
                     *  20 ms = 20000 us 
                     */

                    /* 如果第一次上电 则不进行比较 */
                    if ( first_recv_flag ) {
                            
                            last_recvtimestamp = p->count_us;
                            first_recv_flag = 0;

                            /*初始差值大于20000*/
                            diff_time = 21000; 

                    } else { /* 进行时间戳差值比较   */

                            /* 时间戳为 unsigned 4bytes: 反转情况   */
                            if ( p->count_us < last_recvtimestamp ) {

                                    diff_time = (0xffffffff - last_recvtimestamp) + ( p->count_us - 0) + 1;
                            } else {

                                    diff_time = p->count_us - last_recvtimestamp;
                            }

                            /* 将最新的时间戳赋值给 last_recvtimestamp变量 */
                            last_recvtimestamp = p->count_us;                                                       
                    }

                    /* 阈值判断 */
                    if ( diff_time <= 20000 ) {

                            DEBUG_CONF("\nThe Domain Value is less 20ms, Invalid packet!\n");
                            continue;
                    }

                    /* jion request 数据处理区域 */
        
                    /* 校验 mic 是否正确，保证数据传输，取数据时，数据的完整性 */                   
                    mymemcpy(join_deveui,p->payload+9,8);
                    join_size = p->size;

                    /* 根据上报的 deveui 取出 appkey,可同时判断该节点是否存在 */
                    if ( 1 != Fetch_Appkey_Table(db,join_deveui,join_appkey ) ){
      
                            DEBUG_CONF("no this deveui!\n");
                            continue;
                    } 
             
                    /* 计算join request 的 mic */
                    LoRaMacJoinComputeMic(p->payload, join_size-LORAMAC_MFR_LEN, join_appkey, &join_mic);
                
                    micRx  = (  uint32_t )p->payload[join_size -LORAMAC_MFR_LEN];
                    micRx |= (( uint32_t )p->payload[join_size -LORAMAC_MFR_LEN + 1] << 8 );
                    micRx |= (( uint32_t )p->payload[join_size -LORAMAC_MFR_LEN + 2] << 16 );
                    micRx |= (( uint32_t )p->payload[join_size -LORAMAC_MFR_LEN + 3] << 24 );   

                    /*  mic判断 */
                    if( micRx == join_mic ){


                            /* join log 信息 */    
                            #if 0
                                    DEBUG_CONF("\n/*-----------------------------------------------Join Data ---------------------------------------------------*\\n");
                                    DEBUG_CONF("mType	  :	 0x%02x\n",macHdr[i].Bits.MType);
                                    DEBUG_CONF("ifChain   :   0x%02x\n",p->if_chain);
                                    DEBUG_CONF("rfChain   :   0x%02x\n",p->rf_chain);
                                    DEBUG_CONF("freq      :       %d\n",p->freq_hz);
                                    DEBUG_CONF("datarate  :       %d\n",p->datarate);
                                    DEBUG_CONF("bandwidth :       %d\n",p->bandwidth);
                                    DEBUG_CONF("coderate  :       %d\n",p->coderate);
                                    DEBUG_CONF("rssi      :       %f\n",p->rssi);
                                    DEBUG_CONF("snr       :       %f\n",p->snr);
                                    DEBUG_CONF("\n/*-----------------------------------------------Log Info ---------------------------------------------------*\\n");
                            #endif

                            /* 每次join前清除该deveui的 fcnt_up 和 fcnt_down */
                            if ( 0 == Clear_GWinfo_FcntUp(db,zErrMsg,join_deveui) ){

                                    DEBUG_CONF("clear fcnt_up value error!\n");
                                    continue;
                            }

                            if ( 0 == Clear_GWinfo_FcntDown(db,zErrMsg,join_deveui) ){

                                    DEBUG_CONF("clear fcnt_up value error!\n");
                                    continue;
                            }

                             /* 线程创建区域 */           

                            /* 创建下发join accept 的线程  */
                            if ( 1 == join_pthread_flag ){

                                    err = pthread_create ( &thread_send_queue,NULL,Thread_Send_Queue_Task,NULL);
                                    
                                    if(err != 0){

                                            DEBUG_CONF("can't create task thread ");
                                    
                                    }else{

                                            DEBUG_CONF("send node join data create successfully\n");
                                    }
                                    
                                    pthread_mutex_lock(&queue_flag_task_mux);   
                                    join_pthread_flag=2;
                                    pthread_mutex_unlock(&queue_flag_task_mux); 
              
                            }

                          #if 1
                            /* 创建向lora_pkt_server.c发送数据的任务 */
                            if ( 2 == join_pthread_flag ){
                            
                                    err = pthread_create(&thread_send_join,NULL,Thread_Send_Join,(void*)rxpkt);
                                    
                                    if(err != 0){

                                        DEBUG_CONF("can't create send join info pthread!\n ");
                                    
                                    }else{

                                        DEBUG_CONF("send join info to server pthread create successfully\n");
                                    }
                                    
                                    pthread_mutex_lock(&queue_flag_task_mux);   
                                    join_pthread_flag=3;
                                    pthread_mutex_unlock(&queue_flag_task_mux); 
                            }
                         #endif

                            /** 
                             *  创建join request 数据入队线程 
                             *  因需动态传入rx地址，所以每次需重新创建线程
                             * 
                             */

                           
                            err = pthread_create(&thread_join_queue[i],NULL,Thread_Join_Queue,(void*) p);        
                            if ( err != 0 ){
                                
                                    DEBUG_CONF("can't create task thread ");
                            
                            } else{

                                    DEBUG_CONF("join data into queue create successfully\n");
                            }
                     
                            /* 通知join入队线程将join信息进行入队处理 */
                            sem_post(&Join_Data_Queue_SemFlag);

                            /* 通知发送join request数据到服务器的线程 */
                            sem_post(&Send_Join_Data_Server_SemFlag);

                            /* 等待 Thread_Join_Queue 线程执行结束 */
                            pthread_join(thread_join_queue[i], NULL);

                    }else{
                            
                            DEBUG_CONF("Join mic is error!\n");
                            continue;
                    } 
				} break;

                /* lorawan协议解析部分 */

                /* mType: 0x04 */
				case FRAME_TYPE_DATA_CONFIRMED_UP:	
				/* mType: 0x02 */
                case FRAME_TYPE_DATA_UNCONFIRMED_UP:

                #if 1
				{    

                        
                        /* 数组下标从1开始，0为mType */
                        pktHeaderLen = 1; 

                        /* devAddr 4 bytes */
                        address_node_up  =   p->payload[pktHeaderLen++];
                        address_node_up |= ( p->payload[pktHeaderLen++] << 8 );
                        address_node_up |= ( p->payload[pktHeaderLen++] << 16 );
                        address_node_up |= ( p->payload[pktHeaderLen++] << 24 );
                    
                        /* fCtrl 1 byte */
                        fCtrl[i].Value = p->payload[pktHeaderLen++];

                        /* Fcnt 2 bytes */
                        sequenceCounter	 =   (uint16_t) p->payload[pktHeaderLen++];
                        sequenceCounter	|= ( (uint16_t) p->payload[pktHeaderLen++] << 8 );
                        
                        /*  FRMayload 起始地址 1 + 4 + 1 + 2+fCtrl.Bits.FOptslen */
                        /*  后面再处理 port */
                        appPayloadStartIndex = 8 + fCtrl[i].Bits.FOptsLen;
                    
                        /* 载荷的大小 */
                        size = p->size;
                 
                        /* mic 4 bytes */
                        micRx  =  ( uint32_t)p->payload[size - LORAMAC_MFR_LEN];
                        micRx |=( (uint32_t) p->payload[size - LORAMAC_MFR_LEN+1] << 8 );
                        micRx |=( (uint32_t) p->payload[size - LORAMAC_MFR_LEN+2] << 16);
                        micRx |=( (uint32_t) p->payload[size - LORAMAC_MFR_LEN+3] << 24);
                    
                        /* 取出gwinfo对应的上次的fcnt_up的值 */
                        if ( 0 == Fetch_GWinfo_FcntUp(db,&address_node_up,&upLinkCounter ) ){

                                DEBUG_CONF("fetch fcntup error!\n");    
                                continue;
                        }

                        /** 
                         * 查数据表，取出最新的nwkskey和appskey
                         * 由上报的节点取出数据 nwkskey 和 appskey 用于后面解析数据
                         *                   deveui  和 appeui  用于上报给服务器
                         */
                        pthread_mutex_lock(&lorawan_table_parse);
                        Parse_GwInfo_Table(db,&address_node_up,LoRaMacNwkSKey,LoRaMacAppSKey,server_deveui,server_appeui);
                        pthread_mutex_unlock(&lorawan_table_parse);

                        if (0 == Fetch_Address_Successful){

                                DEBUG_CONF("fetch nwkskey,appskey,deveui,appeui is error!\n");    
                                continue;
                        } 

                        /* fcnt 计数值 */
                        sequenceCounterPrev = ( uint16_t )upLinkCounter;
                        sequenceCounterDiff = ( sequenceCounter - sequenceCounterPrev );
                        /* fcnt 32位计数模式 */
                        if ( sequenceCounterDiff < (1 << 15) ){
                            
                                upLinkCounter += sequenceCounterDiff;

                                /* 计算mic */
                                LoRaMacComputeMic(p->payload, size - LORAMAC_MFR_LEN, 
                                                            LoRaMacNwkSKey, 
                                                            address_node_up, 
                                                            UP_LINK, 
                                                            upLinkCounter, 
                                                            &node_mic );
                                if ( micRx == node_mic ){
                                        isMicOk = true;
                                }

                        } else {
                                    
                                    /* 检查序列翻转 */
                                    uint32_t  upLinkCounterTmp = upLinkCounter + 0x10000 + ( int16_t )sequenceCounterDiff;
                                    LoRaMacComputeMic(p->payload, size - LORAMAC_MFR_LEN, 
                                                                LoRaMacNwkSKey, 
                                                                address_node_up, 
                                                                UP_LINK, 
                                                                upLinkCounterTmp, 
                                                                &node_mic );
                                    if( micRx == node_mic ){

                                            isMicOk = true;
                                            upLinkCounter = upLinkCounterTmp;
                                    }  
                        }

                        /* mic校验成功 */
                        if ( isMicOk == true ){


                            #if 1
                                    DEBUG_CONF("\n/*-----------------------------------------------Join Data ---------------------------------------------------*\\n");
                                    DEBUG_CONF("mType	  :	 0x%02x\n",macHdr[i].Bits.MType);
                                    DEBUG_CONF("ifChain   :   0x%02x\n",p->if_chain);
                                    DEBUG_CONF("rfChain   :   0x%02x\n",p->rf_chain);
                                    DEBUG_CONF("freq      :       %d\n",p->freq_hz);
                                    DEBUG_CONF("datarate  :       %d\n",p->datarate);
                                    DEBUG_CONF("bandwidth :       %d\n",p->bandwidth);
                                    DEBUG_CONF("coderate  :       %d\n",p->coderate);
                                    DEBUG_CONF("rssi      :       %f\n",p->rssi);
                                    DEBUG_CONF("snr       :       %f\n",p->snr);
                                    DEBUG_CONF("\n/*-----------------------------------------------Log Info ---------------------------------------------------*\\n");
                            #endif

                                /* mic校验成功后，将节点的deveui信息存入到class_a deveui 缓存中，便于在class_a线程中处理 */
                                pthread_rwlock_wrlock(&rw_class_a_deveui_mux);
                                mymemcpy ( ClassA_Deveui, server_deveui, 8);
                                pthread_rwlock_unlock(&rw_class_a_deveui_mux);

                                /* 将计算出的uplinkcounter更新到数据库中 */
                                if ( 0 == Update_GWinfo_FcntUp (db,zErrMsg,&address_node_up,&upLinkCounter ) ){

                                        /* 更新失败跳出本次处理循环 */
                                        DEBUG_CONF("update fcntup error!\n");
                                        continue;     
                                }
                                
                                /* 创建线程函数    */
                                /* 下发任务处理线程 */
                                if ( 1 == node_data_pthread_flag ){
                                                                                    
                                        err = pthread_create(&thread_handle_task,NULL,Thread_Handle_Data_Task,NULL);

                                        if(err != 0){
                                            
                                                DEBUG_CONF("can't create  handle task thread ");
                                        } else{

                                                DEBUG_CONF("handle task create successfully\n");
                                        }
                                        
                                        pthread_mutex_lock(&queue_flag_task_mux);   
                                        node_data_pthread_flag=2;
                                        pthread_mutex_unlock(&queue_flag_task_mux);  
                                        DEBUG_CONF("\nnode_data_pthread_flag:%d\n",node_data_pthread_flag);  
                                
                                }
                                
                                /* 取出节点上报数据的线程 */
                                if ( 2 == node_data_pthread_flag ){   
                                    
                                        err = pthread_create(&thread_fetch_report_data,NULL,Thread_Fetch_Report_Data,NULL);
                                        
                                        if(err != 0){

                                                DEBUG_CONF("can't create task thread ");

                                        } else{
                                                
                                                DEBUG_CONF("fetch node data pthread create successfully\n");
                                        }

                                        pthread_mutex_lock(&queue_flag_task_mux);   
                                        node_data_pthread_flag=3;
                                        pthread_mutex_unlock(&queue_flag_task_mux); 
                                }
                            
                                /* 服务器应答数据存储 */
                                if ( 3 == node_data_pthread_flag) {
                                   

                                        err = pthread_create(&thread_data_store_task,NULL,Thread_Data_Store_Task,NULL);
                                        if(err != 0){

                                                DEBUG_CONF("can't create task thread ");
                                        }else{
                                                DEBUG_CONF("server ack data store pthread create successfully\n");
                                        }
                                        pthread_mutex_lock(&queue_flag_task_mux); 
                                        node_data_pthread_flag = 4;
                                        pthread_mutex_unlock(&queue_flag_task_mux);

                                }

                                /** 
                                 *  创建join macpayload 入队线程 
                                 *  因需动态传入rx地址，所以每次需重新创建线程
                                 * 
                                 */                              
                         
                                err = pthread_create(&thread_report_data_queue[i],NULL,Thread_Task_Queue,(void*) p);     
                                
                                if(err != 0){

                                        DEBUG_CONF("can't create  node report data  pthread!\n ");

                                } else{

                                        DEBUG_CONF("node report data pthread create successfully\n");
                                } 

                                /* 通知节点数据入队线程，让节点数据入队 */
                                sem_post(&Node_Data_queue_SemFlag); 

                                /* 以下为lorawan协议解析部分 */
                                DEBUG_CONF("\n //===============================================================================================\\ \n");
                                DEBUG_CONF("\n 			                Recived LoRa MACPayload! \n");				
                                DEBUG_CONF("MType values :%d\n",macHdr[i].Bits.MType);   
                                
                                /* 参考 LoRaWAN V1.0.2 chapter4.3.2 */
                                /* 检查FRMpayload中是否有值 */
                                if ( ( ( size - 4 ) - appPayloadStartIndex ) > 0 ){

                                        FPort    =   p->payload[appPayloadStartIndex++];
                                        frameLen = ( size - 4 ) - appPayloadStartIndex;
                                        
                                        if ( 0 == FPort ){

                                                /** 
                                                 * 允许帧中没有fOpts
                                                 * 判断FOtsLen的数据
                                                 */
                                                if ( 0 == fCtrl[i].Bits.FOptsLen ){

                                                        /** 
                                                         * 解码 FRMpayload数据
                                                         * 注意：因为是解析上行数据
                                                         * 所以此处是：UP_LINK
                                                         */
                                                        LoRaMacPayloadDecrypt( &( p->payload[ appPayloadStartIndex++ ] ),
                                                                                frameLen,
                                                                                LoRaMacNwkSKey,
                                                                                address_node_up,
                                                                                UP_LINK,
                                                                                upLinkCounter,
                                                                                LoRaMacRxPayload );
                    
                                                        // Decode frame payload MAC commands
                                                        // 之后再做处理
                                                        // ProcessMacCommands()

                                                } else { /* 不处理 */
                                                    
                                                            //return -1;
                                                }

                                        } else { /* FPort != 0 */

                                                if(fCtrl[i].Bits.FOptsLen > 0){

                                                        // Decode frame payload MAC commands
                                                        // 之后再做处理
                                                        // ProcessMacCommands()
                                                }
                                                /* 解密载荷 */
                                                LoRaMacPayloadDecrypt( &( p->payload[appPayloadStartIndex++] ),
                                                                          frameLen,
                                                                          LoRaMacAppSKey,
                                                                          address_node_up,
                                                                          UP_LINK,
                                                                          upLinkCounter,
                                                                          LoRaMacRxPayload );
                
                                        }

                                   /* 无载荷 MAC命令 暂时不用考虑处理 */ 
                                } else if ( ( ( size - 4 )- appPayloadStartIndex )<= 0 ){

                                        if ( fCtrl[i].Bits.FOptsLen > 0 ){

                                                // Decode frame payload MAC commands
                                                // 之后再做处理
                                                // ProcessMacCommands()
                                        }
                
                                } else {

                                        DEBUG_CONF("MACPayload error! Please check it!");    
                                }
                        
                            /* 打印LoRaMAC所有的数据 */
                            DEBUG_CONF("\n//==========================================LoRa Pkt Data ==================================================\\ \n");
                            DEBUG_CONF("MType	 :	 0x%02x\n",macHdr[i].Bits.MType);
                            DEBUG_CONF("Devaddr  :	 0x%02x\n",address_node_up);
                            DEBUG_CONF("FCtrl	 :	 ADR:%x 	 ADRACKReq:%x	 ACK:%x    FOptslen:	%x\n",fCtrl[i].Bits.Adr,
                                                                                            fCtrl[i].Bits.AdrAckReq,
                                                                                            fCtrl[i].Bits.Ack,
                                                                                            fCtrl[i].Bits.FOptsLen);
                            DEBUG_CONF("Fcnt	 :	 0x%02x\n",upLinkCounter);
                            DEBUG_CONF("FPort	 :	 0x%02x\n",FPort);
                            DEBUG_CONF("Datarate :       0x%02x\n",rxbuff[i].datarate);
                            DEBUG_CONF("DataSize :       %d\n",frameLen);

                            /* 解码后的载荷 */
                            for(int i= 0; i < frameLen; i++){

                                    DEBUG_CONF("Data[%d]:0x%02x\n",i,LoRaMacRxPayload[i]);
                            }

                            /*
                                 向lora_pkt_conf.c上报deveui信息和解析后的数据信息
                                数据显示在上位机软件接收区
                            */
                            SendDecodeDataFlag = 1;

                            /* 向服务器上报信息 */
                            SEND_FLAG = 1;
                            DEBUG_CONF("MIC 	 :	 0x%x\n",micRx);
                            DEBUG_CONF("//==============================================LoRa Pkt Data =================================================\\\n");
                            
                            /* 解密数据发送到上位机数据接收区 */

                            /* 向lorawan_pkt_conf.c发送 deveui + decode data */
                            if ( 1 == SendDecodeDataFlag ){
                                    
                                    /* 拷贝解析的节点的deveui到临时解码缓存数组 */
                                    mymemcpy(DecodeDataBuff,server_deveui,8);

                                    /* 拷贝解析的数据到临时解码缓存数组 */
                                    mymemccpy(DecodeDataBuff+8,LoRaMacRxPayload,frameLen);
                                    DecodeDataLen = 8 + frameLen;

                                    /* 初始化解码结构体 */
                                    memset(&struct_decode_data,0,sizeof(struct decode_data));

                                     /* 将解码缓存数组中的deveui信息拷贝到解码结构体 */
                                    mymemcpy(struct_decode_data.data,DecodeDataBuff,8);
                                    mymemccpy(struct_decode_data.data+8,DecodeDataBuff+8,frameLen);    
                                    struct_decode_data.length = DecodeDataLen; 

                                   /**
                                    *线程创建：udp内部传输解密后的数据到lora_pkt_conf.c
                                    *        只创建一次       
                                    */ 
                                    if ( 1 == decode_data_pthread_flag){

                                            err = pthread_create(&thread_send_pkt_conf_decode_data,NULL,Thread_SendDecodeData,&struct_decode_data);
                                    
                                    if ( err != 0 ){

                                            DEBUG_CONF("can't create send decode data pthread!\n");

                                    } else {
                                            
                                            DEBUG_CONF("send decode data pthread create successfully\n");
                                    }     

                                    decode_data_pthread_flag=2;
                                    DEBUG_CONF("\n node_data_pthread_flag:%d\n",decode_data_pthread_flag);  
                            
                                }

                                    SendDecodeDataFlag = 0;
                                    memset(DecodeDataBuff,0,1000); 
                                    /* 解析成功，信号量V操作 */
                                    sem_post(&Send_Parse_Data_To_GatewaySet_SemFlag);                           
                            }

                       
                            /*
                                 判断是否是解析正确的LORAWAN数据 
                                 数据本地解析成功，向服务器发送解析成功的数据

                             */

                            #if 1    
                            if ( SEND_FLAG == 1)
                            {				
                                /*
                                    解密数据发送到服务器端
                                    传输字节序    ：     大端传输
                                    数据序列化思想 :
                                                step1: 先序列化到snr 
                                                step2: 插入appeui、deveui
                                                step3: 最后填充Payload    
                                 
                                 */  

                     
                                /* 数据序列化到snr */    
                                server_send_len=LoRaWAN_Data_Serialization(p,server_send_buff);
                
                                /* 插入appeui、deveui */
                                mymemcpy(server_send_buff+server_send_len,server_appeui,8);
                                server_send_len+=8;
                                mymemcpy(server_send_buff+server_send_len,server_deveui,8);
                                server_send_len+=8;

                                /* 拷贝解密后的payload */
                                mymemcpy(server_send_buff+server_send_len,LoRaMacRxPayload,frameLen);
                                server_send_len+=frameLen;

                                /* 添加上报的数据类型传输给lora_pkt_server.c */
                                server_send_buff[server_send_len] = macHdr[i].Bits.MType;
                                server_send_len+=1;
 
                                /* 拷贝数据到序列化结构体，保证线程安全性，避免使用全局变量  */             
                                mymemcpy(struct_serialization_data.data,server_send_buff,server_send_len);
                                struct_serialization_data.length = server_send_len;  
                                
                                /* 线程创建：udp内部传输解密后的数据到lora_pkt_server.c
                                 *
                                 * decode_data_pthread_flag: 线程创建变量，控制线程只创建1次
                                 * 
                                 */
                      
                                if ( 2 == decode_data_pthread_flag ){ 

                                        err = pthread_create(&thread_server_data,NULL,Thread_Send_Server_Data,&struct_serialization_data);
                                        
                                        if ( err != 0 ){

                                                DEBUG_CONF("can't create send server data pthread!\n ");

                                        } else {
                    
                                                 DEBUG_CONF("send server data pthread create successfully\n");
                                        }    
                      
                                        pthread_mutex_lock(&queue_flag_task_mux);   
                                        decode_data_pthread_flag=3;
                                        pthread_mutex_unlock(&queue_flag_task_mux);  
                                
                                } 

                                SEND_FLAG = 0;
                                memset(server_send_buff,0,300);

                                /* 通知 Thread_Send_Server_Data 线程 向服务器发生解析成功的数据 */   
                                sem_post(&Send_Parse_Data_To_Server_SemFlag);                  
                            }
                            #endif

                            /* clear */
                            Fetch_Address_Successful = 0;
                            memset(LoRaMacRxPayload,0,frameLen);
                            pktHeaderLen         = 0;
                            address_node_up 	 = 0;
                            FPort		         = 0;
                            frameLen	         = 0;
                            micRx		         = 0;
                            upLinkCounter        = 0;
                            memset(server_deveui,0,8);
                            memset(server_appeui,0,8);
                            
                            /* 等待 Thread_Join_Queue 线程执行结束 */
                            pthread_join(thread_report_data_queue[i],  NULL);

                    } else {

                                DEBUG_CONF("node data mic is error!\n");
                                continue;
                    }
                    			
			    }break;
	            #endif   
				case FRAME_TYPE_PROPRIETARY:{ }break;
				default:
						   break;
	
		   }
	  
	}
   
}

/** \brief // 将LoRaWAN 入网数据序列化之后通过UDP协议上传至服务器
 *
 * \param    输入解密后的接收结构体地址
 * \param    输入发送缓存数组
 *  return   成功返回需要发送的字节大小  失败返回-1
 *
 *  由LoRaWAN-HUB与服务器通信协议规定，传输方式为：大端传输
 */
int LoRaWAN_Join_Serialization(struct lgw_pkt_rx_s *rxbuff,uint8_t *send_buff)
{
    uint16_t REC_PKT_LEN = 0;
	//存储由float型转换成字节的数据
	//uint8_t rssi_buff[4];
	//uint8_t snr_buff[4];
	//uint8_t snr_min_buff[4];
	//uint8_t snr_max_buff[4];
	
    int16_t rssi = 0;
    int16_t snr  = 0;
    
    if(rxbuff == NULL)
	{
      return -1;
	}
	if(send_buff == NULL)
	{
	  return -1;	
	}
    //进行浮点型数据转换
    //FloatToByte(&rxbuff->rssi,rssi_buff);
	//FloatToByte(&rxbuff->snr,snr_buff);
	//FloatToByte(&rxbuff->snr_min,snr_min_buff);
	//FloatToByte(&rxbuff->snr_max,snr_max_buff);	
    
    //定义格式为大端存储
	//数据序列化
    
    //序列化的顺序参考LoRaWAN-HUB与服务器通信协议
    //if_chain    1byte
    send_buff[REC_PKT_LEN++] = (uint8_t) rxbuff->if_chain;
    //rf_chain    1byte
	send_buff[REC_PKT_LEN++] = (uint8_t) rxbuff->rf_chain;
    //freq_hz     4byte
    send_buff[REC_PKT_LEN++] = (uint8_t)(rxbuff->freq_hz >>24);
    send_buff[REC_PKT_LEN++] = (uint8_t)(rxbuff->freq_hz >>16);
    send_buff[REC_PKT_LEN++] = (uint8_t)(rxbuff->freq_hz >>8); 
	send_buff[REC_PKT_LEN++] = (uint8_t) rxbuff->freq_hz;	
    //datarate   4byte
	send_buff[REC_PKT_LEN++] = (uint8_t) rxbuff->datarate;
    //bandwidth  1byte
	send_buff[REC_PKT_LEN++] = (uint8_t) rxbuff->bandwidth;
    //coderate   1byte
    send_buff[REC_PKT_LEN++] = (uint8_t)(rxbuff->coderate);
    //rssi 精度0.1
    rssi = (rxbuff->rssi * 10);
    send_buff[REC_PKT_LEN++] = (uint8_t)(rssi >> 8);
    send_buff[REC_PKT_LEN++] = (uint8_t) rssi;
    
    //snr  精度0.1
    snr  = (rxbuff->snr  * 10);
    send_buff[REC_PKT_LEN++] = (uint8_t)(snr >> 8);
    send_buff[REC_PKT_LEN++] = (uint8_t) snr;
    
    //appeui
    mymemccpy(send_buff+REC_PKT_LEN,rxbuff->payload+1,8);
    REC_PKT_LEN = REC_PKT_LEN + 8;
    //deveui
    mymemccpy(send_buff+REC_PKT_LEN,rxbuff->payload+9,8);
    REC_PKT_LEN = REC_PKT_LEN + 8;
	return REC_PKT_LEN;
}	

/**  brief   将节点发送的解密后的数据序列化之后通过UDP协议上传至服务器
 *
 *   param    输入解密后的接收结构体地址
 *   param    输入发送缓存数组
 *   return   成功返回需要发送的字节大小  失败返回-1
 *
 *   由LoRaWAN-HUB与服务器通信协议规定，传输方式为：大端传输
 */ 

int LoRaWAN_Data_Serialization(struct lgw_pkt_rx_s *rxbuff,uint8_t *send_buff)
{
    uint16_t REC_PKT_LEN = 0;
    int16_t rssi = 0;
    int16_t snr  = 0;
    
    if(rxbuff == NULL){
            return -1;
	}

	if(send_buff == NULL){
	        return -1;	
	}

    /** 
     * 定义格式为大端存储 
	 * 数据序列化
     */

    /* 序列化的顺序参考LoRaWAN-HUB与服务器通信协议 */
    
    /* if_chain 1byte */
    send_buff[REC_PKT_LEN++] = (uint8_t) rxbuff->if_chain;
    
    /* rf_chain  1byte */
	send_buff[REC_PKT_LEN++] = (uint8_t) rxbuff->rf_chain;
    
    /* freq_hz    4byte */
    send_buff[REC_PKT_LEN++] = (uint8_t)(rxbuff->freq_hz >>24);
    send_buff[REC_PKT_LEN++] = (uint8_t)(rxbuff->freq_hz >>16);
    send_buff[REC_PKT_LEN++] = (uint8_t)(rxbuff->freq_hz >>8); 
	send_buff[REC_PKT_LEN++] = (uint8_t) rxbuff->freq_hz;	
    
    /* datarate   1byte */
	send_buff[REC_PKT_LEN++] = (uint8_t) rxbuff->datarate;
    
    /* bandwidth  1byte */
	send_buff[REC_PKT_LEN++] = (uint8_t) rxbuff->bandwidth;
    
    /* coderate   1byte */
    send_buff[REC_PKT_LEN++] = (uint8_t)(rxbuff->coderate);
    
    /* rssi 精度0.1 */
    rssi = (rxbuff->rssi * 10);
    send_buff[REC_PKT_LEN++] = (uint8_t)(rssi >> 8);
    send_buff[REC_PKT_LEN++] = (uint8_t) rssi;
    
    /* snr  精度0.1 */
    snr  = (rxbuff->snr  * 10);
    send_buff[REC_PKT_LEN++] = (uint8_t)(snr >> 8);
    send_buff[REC_PKT_LEN++] = (uint8_t) snr;

	return REC_PKT_LEN;
}

/* 回调函数写法 */
int callback(void *NotUsed,int argc,char**argv,char**azColName)
{
    int i;
    for(i=0; i <argc;i++)
    {
        printf("%s = %s\n",azColName[i],argv[i]?argv[i]:"NULL");

    }
    printf("\n");
    return 0;
}

//模拟插入GW的设备信息
void Insert_GwInfo_Table(sqlite3 *db,char *zErrMsg)
{
    //绑定的变量值
    int index1;
    int index2;
    int index3;
    int index4;
    int index5;
    int index6;
    int ID = 0;
    //lorawan数据库模型使用的变量
    //前期模拟插入的固定数据
    //后期该数据可专门由服务器配置
    //这里先配置三组a,b,c
    //group a data
    char *Appkey_a   ="00112233445566778899aabbccddeeff";
    char *Deveui_a   ="004a7700660033b4";
    char *Devaddr_a  ="00112233";
    char *Nwkskey_a  ="0123456789abcedf0123456789abcdef";
    char *APPskey_a  ="0123456789abcedf0123456789abcdef";
    //group b data  
    char *Appkey_b   ="00112233445566778899aabbccddeeff";
    char *Deveui_b   ="004a7700660033c9";
    char *Devaddr_b  ="00112233";
    char *Nwkskey_b  ="0123456789abcedf0123456789abcdef";
    char *APPskey_b  ="0123456789abcedf0123456789abcdef";
    //group c data
    char *Appkey_c   ="00112233445566778899aabbccddeeff";
    char *Deveui_c   ="004a77006600136d"; 
    char *Devaddr_c  ="00112233";
    char *Nwkskey_c  ="0123456789abcedf0123456789abcdef";
    char *APPskey_c  ="0123456789abcedf0123456789abcdef";

    sqlite3_stmt *stmt = NULL;
    //语句并不执行。生成二进制sql语句
    // 准备语句并不执行，生成二进制sql语句
     if(sqlite3_prepare_v2
                         (
                             db,
                             "INSERT INTO GWINFO VALUES(:x,:y,:z,:l,:m,:k);",
                             -1,
                             &stmt,
                             0
                         )
                         != SQLITE_OK
       )
     {
  
        printf("\nCould not prepare statement.\n");
        sqlite3_free(zErrMsg);
        sqlite3_close(db);        
     }
     index1 = sqlite3_bind_parameter_index(stmt,":x");
     index2 = sqlite3_bind_parameter_index(stmt,":y");
     index3 = sqlite3_bind_parameter_index(stmt,":z");
     index4 = sqlite3_bind_parameter_index(stmt,":l");
     index5 = sqlite3_bind_parameter_index(stmt,":m");
     index6 = sqlite3_bind_parameter_index(stmt,":k");
     printf("\n The statement has %d wildcards\n",sqlite3_bind_parameter_count(stmt));
     //开BEGIN TRANSACTION功能，可以大大提高插入表格的效率
     sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
     for (ID=1;ID < MAX_ID; ID++)
    {
        switch(ID)
        {
            case 1:
            {
                if(sqlite3_bind_int(stmt,index1,ID)!= SQLITE_OK)
                {
                     printf("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                } 
                if(sqlite3_bind_text(stmt,index2,Appkey_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                     printf("\nCould not bind int.\n");
                     sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index3,Deveui_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index4,Devaddr_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index5,Nwkskey_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index6,APPskey_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if (sqlite3_step(stmt) != SQLITE_DONE)
                {
                    printf("\nCould not step (execute)stmt.\n");
                    sqlite3_free(zErrMsg);
                    sqlite3_close(db);  
                }
                    sqlite3_reset(stmt);   
            }break;

            case 2:
            {
                if(sqlite3_bind_int(stmt,index1,ID)!= SQLITE_OK)
                {
                     printf("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                } 
                if(sqlite3_bind_text(stmt,index2,Appkey_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                     printf("\nCould not bind int.\n");
                     sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index3,Deveui_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index4,Devaddr_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index5,Nwkskey_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index6,APPskey_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if (sqlite3_step(stmt) != SQLITE_DONE)
               {
                    printf("\nCould not step (execute)stmt.\n");
                    sqlite3_free(zErrMsg);
                    sqlite3_close(db);  
                }
                    sqlite3_reset(stmt);   
            }break;
            
            case 3:
            {
                if(sqlite3_bind_int(stmt,index1,ID)!= SQLITE_OK)
                {
                     printf("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                } 
                if(sqlite3_bind_text(stmt,index2,Appkey_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                     printf("\nCould not bind int.\n");
                     sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index3,Deveui_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index4,Devaddr_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index5,Nwkskey_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index6,APPskey_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    printf("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if (sqlite3_step(stmt) != SQLITE_DONE)
                {
                    printf("\nCould not step (execute)stmt.\n");
                    sqlite3_free(zErrMsg);
                    sqlite3_close(db);  
                }
                    sqlite3_reset(stmt);   
            }break;
        }
                          
    }
    //清除所有绑定
    sqlite3_clear_bindings(stmt);
    //销毁sqlite3_prepare_v2创建的对象
    //防止内存泄漏
    sqlite3_finalize(stmt);
    //关闭： COMMIT TRANSACTION
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg);

    //endtime1 = timecacul();
    //resulttime1 = endtime1-starttime1;
    //打印插入表格的时间
    //printf("\nOFF BEGINTRANSACTION INSERT TIME: %lums\n",resulttime1);
}

/**
 * brief: 查询GW的设备信息DEVEUI 
 *        如果表中存在这个节点,则把该节点原来的devaddr, nwkskey, appskey, appeui进行覆盖
 * 
 * update: 
 *        2018.10.11在数据库中增加了appeui
 * 
 */
void Update_GwInfo_Table(sqlite3 *db,char *zErrMsg,uint8_t *deveui,uint8_t *nwkskey,uint8_t *appskey,uint8_t *devaddr,uint8_t *appeui)
{   
    sqlite3_stmt *stmt = NULL;
    int rc; 
    uint8_t deveui_buff [8];
    uint8_t devaddr_buff[4];
    uint8_t appeui_buff[8];

    char *p_deveui  = NULL;
    char *p_nwkskey = NULL;
    char *p_appskey = NULL;
    char *p_devaddr = NULL;
    char *p_appeui  = NULL;  
    
    /**
     *  一个地址只能存储一个字符串，因为1个16进制数表示为2个单个字符串
     *  所以需要设备地址(deveui)2倍的存储空间
     */ 

    char *new_deveui_str  = (char*)malloc(2*DEVEUI_LEN);
    char *new_nwkskey_str = (char*)malloc(2*NWKSKEY_LEN);
    char *new_appskey_str = (char*)malloc(2*APPSKEY_LEN);
    char *new_devaddr_str = (char*)malloc(2*DEVADDR_LEN);
    char *new_appeui_str  = (char*)malloc(2*APPEUI_LEN);

    char *sql_deveui          = (char*)malloc(MALLOC_SIZE);
    char *sql_nwkskey         = (char*)malloc(MALLOC_SIZE);
    char *sql_appskey         = (char*)malloc(MALLOC_SIZE);
    char *sql_devaddr         = (char*)malloc(MALLOC_SIZE);
    char *sql_appeui          = (char*)malloc(MALLOC_SIZE);

    memset(deveui_buff, 0,sizeof(deveui_buff));
    memset(devaddr_buff,0,sizeof(devaddr_buff));
    memset(appeui_buff, 0,sizeof(appeui_buff));

    /* 倒序拷贝 小端存储地址 */
    mymemccpy(deveui_buff, deveui,DEVEUI_LEN);
    mymemccpy(devaddr_buff,devaddr,DEVADDR_LEN);
    mymemccpy(appeui_buff, appeui, APPEUI_LEN);

    /* 数据格式转换 */
    p_deveui  = ChArray_To_String(new_deveui_str,deveui_buff,DEVEUI_LEN);
    
    p_nwkskey = ChArray_To_String(new_nwkskey_str,nwkskey,NWKSKEY_LEN);

    p_appskey = ChArray_To_String(new_appskey_str,appskey,APPSKEY_LEN);

    p_devaddr = ChArray_To_String(new_devaddr_str,devaddr_buff,DEVADDR_LEN);

    p_appeui  = ChArray_To_String(new_appeui_str,appeui_buff,APPEUI_LEN);
  
    /* 存储到sql_deveui中 */
    sprintf(sql_deveui,"SELECT* FROM GWINFO WHERE DEVEui ='%s';",p_deveui);

    rc = sqlite3_prepare_v2(db,sql_deveui,strlen(sql_deveui),&stmt,NULL);
    if (SQLITE_OK !=rc || NULL == stmt){

            printf("\n\n update prepare error!\n\n");
            sqlite3_close(db); 
    } 
 
    /* execute */
    rc = sqlite3_step(stmt);
    if(SQLITE_ROW == rc){

            /* 数据更新 */
            sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);

            sprintf(sql_nwkskey,"UPDATE GWINFO SET Nwkskey = '%s' WHERE DEVEui ='%s';",p_nwkskey,p_deveui);
            rc = sqlite3_exec(db,sql_nwkskey,0,0,&zErrMsg);
            CHECK_RC(rc,zErrMsg,db);
            
            sprintf(sql_appskey,"UPDATE GWINFO SET APPskey = '%s' WHERE DEVEui ='%s';",p_appskey,p_deveui);
            rc = sqlite3_exec(db,sql_appskey,0,0,&zErrMsg);
            CHECK_RC(rc,zErrMsg,db);
            
            sprintf(sql_devaddr,"UPDATE GWINFO SET Devaddr = '%s' WHERE DEVEui = '%s';",p_devaddr,p_deveui);
            rc = sqlite3_exec(db,sql_devaddr,0,0,&zErrMsg);
            CHECK_RC(rc,zErrMsg,db);

            sprintf(sql_devaddr,"UPDATE GWINFO SET APPEui = '%s' WHERE DEVEui = '%s';",p_appeui,p_deveui);
            rc = sqlite3_exec(db,sql_devaddr,0,0,&zErrMsg);
            CHECK_RC(rc,zErrMsg,db);
            
            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);    

    }else{
            /* 不处理，不让节点入网 */
            printf("sorry ,this gwinfo table no serach this deveui.\n");

    }
    
    /* 销毁sqlite3_prepare_v2创建的对象 */
    sqlite3_finalize(stmt);
    //释放申请的动态内存空间
    free(p_deveui);
    free(p_nwkskey);
    free(p_appskey);
    free(p_devaddr);
    free(p_appeui);
    free(sql_deveui);
    free(sql_nwkskey);
    free(sql_appskey);
    free(sql_devaddr);
    free(sql_appeui);
}
/*
    解析数据时使用
    从表中查找是否有对应的deveui的值    
    从表中读取devaddr对应的nwkskey和appskey用于解析lorawan数据
    从表中读取devaddr对应的deveui,appeui 用于向服务器上报数据

*/

void Parse_GwInfo_Table(sqlite3 *db,uint32_t *address, uint8_t parse_new_nwkskey[],uint8_t parse_new_appskey[],uint8_t data_deveui[],uint8_t data_appeui[])
{
    sqlite3_stmt *stmt_data=NULL;
    int rc;
    uint8_t address_buff[4];
    uint8_t endian_address_buff[4];  

    int     len = 0;
    
    /* 每次进入时，清零 */
    Fetch_Address_Successful = 0;

    const char   *parse_nwkskey =   NULL;
    const char   *parse_appskey =   NULL;
    const char   *parse_deveui  =   NULL;
    const char   *parse_appeui  =   NULL;
    char *new_address      =  NULL;
    char *new_address_buff = (char*)malloc(2*DEVADDR_LEN);
    char *sql_address_buff = (char*)malloc(MALLOC_SIZE);

    memset(address_buff,       0,sizeof(address_buff));
    memset(endian_address_buff,0,sizeof(endian_address_buff));  
    
    /* 移位操作，获取解析后的address */
    address_buff[len++] = (uint8_t)(*address);
    address_buff[len++] = (uint8_t)(*address>>8);
    address_buff[len++] = (uint8_t)(*address>>16);
    address_buff[len++] = (uint8_t)(*address>>24);    
    
    /*
      address在数据库中表统一规定为小端存储
      倒序拷贝
    */
    mymemccpy(endian_address_buff,address_buff,DEVADDR_LEN);  

    new_address = ChArray_To_String(new_address_buff,endian_address_buff,DEVADDR_LEN); 

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);   
    
    /* sql 语句 */
    sprintf(sql_address_buff,"SELECT* FROM GWINFO WHERE Devaddr ='%s';",new_address);
    DEBUG_CONF("sql_address_buff:%s\n",sql_address_buff);
    
    /* parpare */
    rc = sqlite3_prepare_v2(db,sql_address_buff,strlen(sql_address_buff),&stmt_data,NULL); 
    if (SQLITE_OK !=rc || NULL == stmt_data)
    {
          printf("\n parse prepare error! \n");
          sqlite3_close(db);
    } 

    /* execute */
    while(SQLITE_ROW == sqlite3_step(stmt_data))
    {         
          /* 提取最新的nwkskey和appskey用于解析lorawan数据 */
          parse_nwkskey = sqlite3_column_text(stmt_data,4);
          parse_appskey = sqlite3_column_text(stmt_data,5);
          parse_deveui  = sqlite3_column_text(stmt_data,2);
          parse_appeui  = sqlite3_column_text(stmt_data,6);

          DEBUG_CONF("parse_nwkskey:%s\n",parse_nwkskey);
          DEBUG_CONF("parse_appskey:%s\n",parse_appskey);
          DEBUG_CONF("parse_deveui:%s\n",parse_deveui);
          DEBUG_CONF("parse_appeui:%s\n",parse_appeui);

         if ( parse_nwkskey != NULL  ) 
         {
                String_To_ChArray(parse_new_nwkskey,parse_nwkskey,NWKSKEY_LEN);
         }
         else
         {
                DEBUG_CONF("parse_nwkskey is full!\n");
         
         }
         
         if (  parse_appskey != NULL ) 
         {
                 String_To_ChArray(parse_new_appskey,parse_appskey,APPSKEY_LEN); 
         
         }
         else
         {
                  DEBUG_CONF("parse_appskey is full!\n");
         
         }
         
         if ( parse_deveui != NULL ) 
         {
                    String_To_ChArray(data_deveui,parse_deveui,DEVEUI_LEN);

         }
         else
         {
   
                    DEBUG_CONF("parse_deveui is full!\n");
         }
         
         if ( parse_appeui != NULL ) 
         {
                    String_To_ChArray(data_appeui,parse_appeui,APPEUI_LEN);    
         }
        else
        {
      
                    DEBUG_CONF("parse_appeui is full!\n");
        }
        
          Fetch_Address_Successful = 1;

    }

    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    
    /* 销毁sqlite3_prepare_v2创建的对象 */

    if ( stmt_data != NULL ) {

        sqlite3_finalize(stmt_data);   
    }

    if ( new_address != NULL ) {

          free(new_address);
    }

    if ( sql_address_buff != NULL ) {
          
          free(sql_address_buff);
    }

}
//根据上报的地址取对应的fcntup
//取数据成功返回1
//失败返回0
int Fetch_GWinfo_FcntUp(sqlite3 *db,uint32_t *address,uint32_t *fcntup)
{
    sqlite3_stmt *stmt_data=NULL;
    int rc;
    uint8_t address_buff[4];
    uint8_t endian_address_buff[4];
    int     len = 0; 
    char *new_address      =  NULL;
    char *new_address_buff = (char*)malloc(2*DEVADDR_LEN);
    char *sql_address_buff = (char*)malloc(MALLOC_SIZE);  

    memset(address_buff,       0,sizeof(address_buff));
    memset(endian_address_buff,0,sizeof(endian_address_buff));  

    //移位操作，获取解析后的address
    address_buff[len++] = (uint8_t)(*address);
    address_buff[len++] = (uint8_t)(*address>>8);
    address_buff[len++] = (uint8_t)(*address>>16);
    address_buff[len++] = (uint8_t)(*address>>24);
    
    mymemccpy(endian_address_buff,address_buff,4);  
    new_address = ChArray_To_String(new_address_buff,endian_address_buff,DEVADDR_LEN);
    DEBUG_CONF("new_address: %s\n",new_address); 
    //开BEGIN TRANSACTION功能，可以大大提高插入表格的效率
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);   
    //存储到sql_deveui中
    sprintf(sql_address_buff,"SELECT* FROM GWINFO WHERE Devaddr ='%s';",new_address);
    //parpare
    rc = sqlite3_prepare_v2(db,sql_address_buff,strlen(sql_address_buff),&stmt_data,NULL); 
    if (SQLITE_OK !=rc || NULL == stmt_data)
    {
          printf("\nfetch fcnt_up values prepare error!\n");
          sqlite3_close(db);
    }
    //execute
    while(SQLITE_ROW == sqlite3_step(stmt_data))
    {         
        //提取最新的fcnt用于解析lorawan数据
        *fcntup = sqlite3_column_int(stmt_data,7);
         DEBUG_CONF("fcnt_up is :%u",*fcntup);
        //关闭： COMMIT TRANSACTION
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
        sqlite3_finalize(stmt_data);  
        free(new_address);
        free(sql_address_buff);
        return 1;
    }
    //关闭： COMMIT TRANSACTION
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    //销毁sqlite3_prepare_v2创建的对象
    sqlite3_finalize(stmt_data);  
    free(new_address);
    free(sql_address_buff);
    return 0;
}

//更新新的fcnt_up到数据库中
//更新数据成功返回1
//更新失败返回0
int Update_GWinfo_FcntUp(sqlite3 *db,char *zErrMsg,uint32_t *address,uint32_t *fcntup)
{   
    sqlite3_stmt *stmt_data=NULL;
    int rc;
    uint8_t address_buff[4];
    uint8_t endian_address_buff[4];
    int     len = 0; 
    char *new_address      =  NULL;
    char *new_address_buff = (char*)malloc(2*DEVADDR_LEN);
    char *sql_address_buff = (char*)malloc(MALLOC_SIZE); 
    char *update_sql       = (char*)malloc(MALLOC_SIZE); 

    memset(address_buff,       0,sizeof(address_buff));
    memset(endian_address_buff,0,sizeof(endian_address_buff));  

    //移位操作，获取解析后的address
    address_buff[len++] = (uint8_t)(*address);
    address_buff[len++] = (uint8_t)(*address>>8);
    address_buff[len++] = (uint8_t)(*address>>16);
    address_buff[len++] = (uint8_t)(*address>>24);

    mymemccpy(endian_address_buff,address_buff,4);  
    new_address = ChArray_To_String(new_address_buff,endian_address_buff,DEVADDR_LEN); 
    sqlite3_exec(db,"BEGIN EXCLUSIVE;",NULL,NULL,NULL);  
    //存储到sql_deveui中
    sprintf(sql_address_buff,"SELECT *FROM GWINFO WHERE Devaddr ='%s';",new_address);
    //parpare
    rc = sqlite3_prepare_v2(db,sql_address_buff,strlen(sql_address_buff),&stmt_data,NULL); 
    if (SQLITE_OK !=rc || NULL == stmt_data)
    {
          printf("\n update fcnt_up values prepare error!\n");
          sqlite3_close(db);
    }
    rc = sqlite3_step(stmt_data);
    if(SQLITE_ROW == rc)
    {
        DEBUG_CONF("no update fcntup is: %u\n",sqlite3_column_int(stmt_data,7));
        //数据更新
        sprintf(update_sql,"UPDATE GWINFO SET FcntUp = %u WHERE Devaddr ='%s';",*fcntup, new_address);
        rc = sqlite3_exec(db,update_sql,0,0,&zErrMsg);
        CHECK_RC(rc,zErrMsg,db);
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);  
        free(new_address);
        free(sql_address_buff);
        free(update_sql);
        sqlite3_reset(stmt_data); 
        sqlite3_finalize(stmt_data);  
        return 1;
    }
    else
    {
        DEBUG_CONF("sorry, no search  this devaddr!\n");
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);  
        free(new_address);
        free(sql_address_buff);
        free(update_sql);
        sqlite3_reset(stmt_data); 
        sqlite3_finalize(stmt_data);  
        return 0;
    }
}
//每次join前清除deveui对应的fcnt_up的值
//返回值1：清除成功
//返回值0：清除失败
int Clear_GWinfo_FcntUp(sqlite3 *db,char *zErrMsg,uint8_t deveui[])
{
    sqlite3_stmt *stmt_data=NULL;
    int rc;
    uint8_t deveui_buff[8];
    char *deveui_str      =  NULL;
    char *new_deveui_buff = (char*)malloc(2*DEVEUI_LEN);
    char *deveui_sql      = (char*)malloc(MALLOC_SIZE); 
    char *update_sql      = (char*)malloc(MALLOC_SIZE); 

    int clear_value = 0;
    //clear
    memset(deveui_buff,0,8);
    //倒序拷贝
    mymemccpy(deveui_buff,deveui,DEVEUI_LEN); 
    deveui_str = ChArray_To_String(new_deveui_buff,deveui_buff,DEVEUI_LEN); 
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
    sprintf(deveui_sql,"SELECT *FROM GWINFO WHERE DEVEui ='%s';",deveui_str);
    //parpare
    rc = sqlite3_prepare_v2(db,deveui_sql,strlen(deveui_sql),&stmt_data,NULL); 
    if (SQLITE_OK !=rc || NULL == stmt_data)
    {
          printf("\n clear fcnt values prepare error!\n");
          sqlite3_close(db);
    }
    rc = sqlite3_step(stmt_data);
    if(SQLITE_ROW == rc)
    {
        //数据更新
        sprintf(update_sql,"UPDATE GWINFO SET FcntUp = %d WHERE DEVEui ='%s';",clear_value, deveui_str);
        rc = sqlite3_exec(db,update_sql,0,0,&zErrMsg);
        CHECK_RC(rc,zErrMsg,db);
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);  
        free(deveui_str);
        free(deveui_sql);
        free(update_sql);
        sqlite3_reset(stmt_data); 
        sqlite3_finalize(stmt_data);  
        return 1;
    }
    else
    {
        DEBUG_CONF("sorry, no search this deveui!\n");
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);  
        free(deveui_str);
        free(deveui_sql);
        free(update_sql);
        sqlite3_reset(stmt_data); 
        sqlite3_finalize(stmt_data);  
        return 0;
    }
}

//根据上报的地址取对应的fcntdown
//取数据成功返回1
//取数据失败返回0
int Fetch_GWinfo_FcntDown(sqlite3 *db,uint32_t *address,uint32_t *fcntdown)
{
    sqlite3_stmt *stmt_data=NULL;
    int rc;
    uint8_t address_buff[4];
    uint8_t endian_address_buff[4];
    int     len = 0; 
    char *new_address      =  NULL;
    char *new_address_buff = (char*)malloc(2*DEVADDR_LEN);
    char *sql_address_buff = (char*)malloc(MALLOC_SIZE);  

    memset(address_buff,       0,sizeof(address_buff));
    memset(endian_address_buff,0,sizeof(endian_address_buff));  

    //移位操作，获取解析后的address
    address_buff[len++] = (uint8_t)(*address);
    address_buff[len++] = (uint8_t)(*address>>8);
    address_buff[len++] = (uint8_t)(*address>>16);
    address_buff[len++] = (uint8_t)(*address>>24);  
    mymemccpy(endian_address_buff,address_buff,4);  
    new_address = ChArray_To_String(new_address_buff,endian_address_buff,DEVADDR_LEN);
   
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
    //存储到sql_deveui中
    sprintf(sql_address_buff,"SELECT* FROM GWINFO WHERE Devaddr ='%s';",new_address);
    //parpare
    rc = sqlite3_prepare_v2(db,sql_address_buff,strlen(sql_address_buff),&stmt_data,NULL); 
    if (SQLITE_OK !=rc || NULL == stmt_data)
    {
          printf("\nfetch fcnt_up values prepare error!\n");
          sqlite3_close(db);
    }
    //execute
    //如果在设备表中能够找到该deveui
    while(SQLITE_ROW == sqlite3_step(stmt_data))
    {         
        //提取最新的fcnt用于解析lorawan数据
        *fcntdown = sqlite3_column_int(stmt_data,8);
        //关闭： COMMIT TRANSACTION
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
        sqlite3_finalize(stmt_data);  
        free(new_address);
        free(sql_address_buff);
         return 1;
    }
    //关闭： COMMIT TRANSACTION
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    //销毁sqlite3_prepare_v2创建的对象
    sqlite3_finalize(stmt_data);  
    free(new_address);
    free(sql_address_buff);
    return 0;
}

//更新新的fcnt_down到数据库中
//更新数据成功返回1
//更新失败返回0
int Update_GWinfo_FcntDown(sqlite3 *db,char *zErrMsg,uint32_t *address,uint32_t *fcntdown)
{
    sqlite3_stmt *stmt_data=NULL;
    int rc;
    uint8_t address_buff[4];
    uint8_t endian_address_buff[4];
    int     len = 0; 
    char *new_address      =  NULL;
    char *new_address_buff = (char*)malloc(2*DEVADDR_LEN);
    char *sql_address_buff = (char*)malloc(MALLOC_SIZE);
    char *update_sql       = (char*)malloc(MALLOC_SIZE);

    memset(address_buff,       0,sizeof(address_buff));
    memset(endian_address_buff,0,sizeof(endian_address_buff));  
    //移位操作，获取解析后的address
    address_buff[len++] = (uint8_t)(*address);
    address_buff[len++] = (uint8_t)(*address>>8);
    address_buff[len++] = (uint8_t)(*address>>16);
    address_buff[len++] = (uint8_t)(*address>>24);  
    mymemccpy(endian_address_buff,address_buff,4);  
    new_address = ChArray_To_String(new_address_buff,endian_address_buff,DEVADDR_LEN); 

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
    sprintf(sql_address_buff,"SELECT *FROM GWINFO WHERE Devaddr ='%s';",new_address);
    //parpare
    rc = sqlite3_prepare_v2(db,sql_address_buff,strlen(sql_address_buff),&stmt_data,NULL); 
    if (SQLITE_OK !=rc || NULL == stmt_data)
    {
          printf("\nupdate fcnt_up values prepare error!\n");
          sqlite3_close(db);
    }
    rc = sqlite3_step(stmt_data);    
    if(SQLITE_ROW == rc)
    {
        DEBUG_CONF("no update fcntdown is: %u\n",sqlite3_column_int(stmt_data,8)); 
        //数据更新
        sprintf(update_sql,"UPDATE GWINFO SET FcntDown = %u WHERE Devaddr ='%s';",*fcntdown, new_address);
        rc = sqlite3_exec(db,update_sql,0,0,&zErrMsg);
        CHECK_RC(rc,zErrMsg,db);
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);  
        free(update_sql);     
        free(new_address);
        free(sql_address_buff);
        sqlite3_reset(stmt_data); 
        sqlite3_finalize(stmt_data);  
        return 1;
    }
    else
    {
        DEBUG_CONF("sorry, no search  this devaddr!\n");
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
        free(update_sql);  
        free(new_address);
        free(sql_address_buff);
        sqlite3_reset(stmt_data); 
        sqlite3_finalize(stmt_data); 
        return 0;
    }
}

//每次join前清除deveui对应的fcnt_down的值
//返回值1：清除成功
//返回值0：清除失败
int Clear_GWinfo_FcntDown(sqlite3 *db,char *zErrMsg,uint8_t deveui[])
{
    sqlite3_stmt *stmt_data=NULL;
    int rc;
    uint8_t deveui_buff[8];
    char *deveui_str      =  NULL;
    char *new_deveui_buff = (char*)malloc(2*DEVEUI_LEN);
    char *deveui_sql      = (char*)malloc(MALLOC_SIZE);
    char *update_sql      = (char*)malloc(MALLOC_SIZE);  
    int clear_value = 0;
    //clear
    memset(deveui_buff,0,8);
    //倒序拷贝
    mymemccpy(deveui_buff,deveui,DEVEUI_LEN); 

    deveui_str = ChArray_To_String(new_deveui_buff,deveui_buff,DEVEUI_LEN); 
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
    sprintf(deveui_sql,"SELECT *FROM GWINFO WHERE DEVEui ='%s';",deveui_str);
    //parpare
    rc = sqlite3_prepare_v2(db,deveui_sql,strlen(deveui_sql),&stmt_data,NULL); 
    if (SQLITE_OK !=rc || NULL == stmt_data)
    {
          printf("\n clear fcnt values prepare error!\n");
          sqlite3_close(db);
    }
    rc = sqlite3_step(stmt_data);
    
    if(SQLITE_ROW == rc)
    {       
        //数据更新
        sprintf(update_sql,"UPDATE GWINFO SET FcntDown = %d WHERE DEVEui ='%s';",clear_value, deveui_str);
        rc = sqlite3_exec(db,update_sql,0,0,&zErrMsg);
        CHECK_RC(rc,zErrMsg,db);
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
        free(deveui_str);
        free(deveui_sql);
        free(update_sql);
        sqlite3_reset(stmt_data); 
        sqlite3_finalize(stmt_data);  
        return 1;
    }
    else
    {
        DEBUG_CONF("sorry, no search  this deveui!\n");
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
        free(deveui_str);
        free(deveui_sql);
        free(update_sql);
        sqlite3_reset(stmt_data); 
        sqlite3_finalize(stmt_data);  
        return 0;
    }
}

/***
 * 每次从上报的DEVEUI中读取APPKEY
 * 便于后面生成appskey 和nwkskey使用
 *  
 */ 
int Fetch_Appkey_Table(sqlite3 *db,uint8_t *deveui,uint8_t Appkey[])
{
    sqlite3_stmt *stmt_deveui = NULL;
    int rc;
    char *deveui_string        = (char*)malloc(2*DEVEUI_LEN);
    char *sql_deveui_buff      = (char*)malloc(MALLOC_SIZE);
    const char *fetch_buff     = NULL; 
    char *deveui_p             = NULL;
    uint8_t address_buff[8];
    memset(address_buff,0,8);

    /* address在数据库中表统一规定为小端存储 */
    /* 倒序拷贝 */
    mymemccpy(address_buff,deveui,DEVEUI_LEN);
    deveui_p = ChArray_To_String(deveui_string,address_buff,DEVEUI_LEN);

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);     
    sprintf(sql_deveui_buff,"SELECT* FROM GWINFO WHERE DEVEui ='%s';",deveui_p);
    DEBUG_CONF("deveui_buff: %s\n",sql_deveui_buff);

    /* parpare */
    rc = sqlite3_prepare_v2(db,sql_deveui_buff,strlen(sql_deveui_buff),&stmt_deveui,NULL);

    if (SQLITE_OK !=rc || NULL == stmt_deveui){
            
            DEBUG_CONF("\n fetch prepare error!\n");
            sqlite3_close(db);
    }
    /* execute */
    while(SQLITE_ROW == sqlite3_step(stmt_deveui)){

            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);    
            fetch_buff = sqlite3_column_text(stmt_deveui, 1);
            
            if ( fetch_buff != NULL ) {
                    
                    String_To_ChArray(Appkey,fetch_buff,APPKEY_LEN); 
            }
            
            if ( stmt_deveui != NULL ) {
                    sqlite3_finalize(stmt_deveui); 
            }

            if ( deveui_string != NULL ) {
                    free(deveui_string);
            }
            
            if ( sql_deveui_buff != NULL ) {
                    free(sql_deveui_buff);
            }
            
            return 1;
    }

    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);  
      
    if ( stmt_deveui != NULL ) {
            sqlite3_finalize(stmt_deveui); 
    }

    if ( deveui_string != NULL ) {
            free(deveui_string);
    }
    
    if ( sql_deveui_buff != NULL ) {
            free(sql_deveui_buff);
    }
            
    return 0;
    
}

/*
  使用地址取nwkskey和appskey
  便于后面生成appskey 和nwkskey使用
*/
int Fetch_Nwkskey_Appskey_Table(sqlite3 *db,uint32_t *address,uint8_t *nwkskey,uint8_t *appskey)
{
    sqlite3_stmt *stmt_data=NULL;
    int rc;
    uint8_t address_buff[4];
    uint8_t endian_address_buff[4];

    /* debug */
    char *sql = NULL; 

    int     len = 0;
    /* 每次进入时，清零 */
    Fetch_Address_Successful = 0;

    const char  *parse_nwkskey = NULL;
    const char  *parse_appskey = NULL;

    char *new_address      =  NULL;
    char *new_address_buff = (char*)malloc(2*DEVADDR_LEN);
    char *sql_address_buff = (char*)malloc(MALLOC_SIZE);

    memset(address_buff,       0,sizeof(address_buff));
    memset(endian_address_buff,0,sizeof(endian_address_buff));  
    
    /* 移位操作，获取解析后的address */
    address_buff[len++] = (uint8_t)(*address);
    address_buff[len++] = (uint8_t)(*address>>8);
    address_buff[len++] = (uint8_t)(*address>>16);
    address_buff[len++] = (uint8_t)(*address>>24); 

    /* address在数据库中表统一规定为小端存储 */
    /* 倒序拷贝 */
    mymemccpy(endian_address_buff,address_buff,DEVADDR_LEN);  

    new_address = ChArray_To_String(new_address_buff,endian_address_buff,DEVADDR_LEN); 

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);   
    /* sql */
    sprintf(sql_address_buff,"SELECT* FROM GWINFO WHERE Devaddr ='%s';",new_address);
    
    /* parpare */
    rc = sqlite3_prepare_v2(db,sql_address_buff,strlen(sql_address_buff),&stmt_data,NULL); 
    if (SQLITE_OK !=rc || NULL == stmt_data)
    {
          printf("\n parse prepare error! \n");
          sqlite3_close(db);
    } 

    /* execute */
    while(SQLITE_ROW == sqlite3_step(stmt_data))
    {         
          /* 提取最新的nwkskey和appskey用于解析lorawan数据 */
          parse_nwkskey = sqlite3_column_text(stmt_data,4);
          parse_appskey = sqlite3_column_text(stmt_data,5);

          if ( parse_nwkskey != NULL ) 
          {
            
                String_To_ChArray(nwkskey,parse_nwkskey,NWKSKEY_LEN);
          }
          else
          {
                DEBUG_CONF("parse_nwkskey is full!\n");
          }

          if ( parse_appskey != NULL ) 
          {
        

                String_To_ChArray(appskey,parse_appskey,APPSKEY_LEN);
          }
          else
          {
            
               DEBUG_CONF("parse_appskey is full!\n");
          }
          
          sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);


        if ( stmt_data != NULL ) {
            sqlite3_finalize(stmt_data);  
        }
    
        if ( new_address != NULL ) {
          free(new_address);
        }
    
        if ( sql_address_buff != NULL ) {
       
            free(sql_address_buff);
        }
          return 1;                     
    
    }

    /* 关闭： COMMIT TRANSACTION */
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    
    if ( stmt_data != NULL ) {
            sqlite3_finalize(stmt_data);  
    }
    
    if ( new_address != NULL ) {
          free(new_address);
    }
    
    if ( sql_address_buff != NULL ) {
       
            free(sql_address_buff);
    }

    return 0;
}

//根据服务器下发的class c数据的deveui取出对应的rx2_freq
//传入参数：数据库地址，设备地址，待取出的class c下发频率
//操作成功返回1
//操作失败返回0
int Fetch_Rx2_Freq(sqlite3 *db, const uint8_t *deveui,uint32_t *rx2_freq,uint32_t *devaddr)
{
    sqlite3_stmt *stmt = NULL;
    int  rc;
    char *deveui_string      = (char*)malloc(2*8);
    char *deveui_buff_sql    = (char*)malloc(100);
    char *deveui_p           =  NULL;
    const char  *devadd_p    =  NULL;
    uint8_t  devaddr_buff[4];
    memset(devaddr_buff,0,4);

    uint8_t deveui_temp[8];
    memset(deveui_temp,0,8);
    //倒序copy
    mymemcpy(deveui_temp,deveui,8);
    //hex transform string
    deveui_p = ChArray_To_String(deveui_string,deveui_temp,8);

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);
    sprintf(deveui_buff_sql,"SELECT* FROM GWINFO WHERE DEVEui ='%s';\n",deveui_p);
    DEBUG_CONF("deveui_buff_sql: %s",deveui_buff_sql);
    //parpare
    rc = sqlite3_prepare_v2(db,deveui_buff_sql,strlen(deveui_buff_sql),&stmt,NULL);
    if (SQLITE_OK !=rc || NULL == stmt)
    {
          DEBUG_CONF("\n fetch prepare error!\n");
          sqlite3_close(db);
    }
    //execute
    while(SQLITE_ROW == sqlite3_step(stmt))
    {
        *rx2_freq = sqlite3_column_int(stmt,9);
        devadd_p  = sqlite3_column_text(stmt,3);
        String_To_ChArray(devaddr_buff,devadd_p,4);
        
        for(int a=0;a<4;a++)
        {
            DEBUG_CONF("devaddr_buff[%d]:0x%02x\n",a,devaddr_buff[a]);
        }
        *devaddr  =  devaddr_buff[3];
        *devaddr |= (devaddr_buff[2]<<8 );
        *devaddr |= (devaddr_buff[1]<<16);
        *devaddr |= (devaddr_buff[0]<<24);
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL); 
        sqlite3_reset(stmt);
        free(deveui_buff_sql);
        free(deveui_p);
        return 1;
    }
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL); 
    sqlite3_reset(stmt);
    free(deveui_buff_sql);
    free(deveui_p);
    return 0;
}

//join数据上报时检查数据库中是否有deveui
int Check_Deveui(sqlite3 *db,uint8_t *deveui)
{
    sqlite3_stmt *stmt_deveui = NULL;
    int rc;
    char *deveui_string     = (char*)malloc(2*DEVEUI_LEN);
    char *sql_deveui_buff   = (char*)malloc(MALLOC_SIZE);
    char *deveui_p          =  NULL;
    uint8_t address_buff[8];
    memset(address_buff,0,8);

    //address在数据库中表统一规定为小端存储
    //倒序拷贝
    mymemccpy(address_buff,deveui,DEVEUI_LEN);
    deveui_p = ChArray_To_String(deveui_string,address_buff,DEVEUI_LEN);

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL); 
    sprintf(sql_deveui_buff,"SELECT* FROM GWINFO WHERE DEVEui ='%s';",deveui_p);

    //parpare
    rc = sqlite3_prepare_v2(db,sql_deveui_buff,strlen(sql_deveui_buff),&stmt_deveui,NULL);
    
    if (SQLITE_OK !=rc || NULL == stmt_deveui)
    {
          printf("\n\n fetch prepare error!\n\n");
          sqlite3_close(db);
    }
    //execute
    while(SQLITE_ROW == sqlite3_step(stmt_deveui))
    {
        sqlite3_reset(stmt_deveui);
        free(deveui_string);
        free(sql_deveui_buff);
        return 1;
    }
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);    
    sqlite3_finalize(stmt_deveui); 
    free(deveui_string);
    free(sql_deveui_buff);
    return 0;    
}

//节点有效数据上报时检查数据库中是否含有该地址
int Check_Devaddr(sqlite3 *db,uint32_t *address)
{
    sqlite3_stmt *stmt_data=NULL;
    int rc;
    uint8_t address_buff[4];
    uint8_t endian_address_buff[4];
    int len = 0;
    memset(address_buff,       0,sizeof(address_buff));
    memset(endian_address_buff,0,sizeof(endian_address_buff)); 
    //移位操作，获取解析后的address
    address_buff[len++] = (uint8_t)(*address);
    address_buff[len++] = (uint8_t)(*address>>8);
    address_buff[len++] = (uint8_t)(*address>>16);
    address_buff[len++] = (uint8_t)(*address>>24);

    char *new_address      =  NULL;
    char *new_address_buff = (char*)malloc(2*DEVADDR_LEN);
    char *sql_address_buff = (char*)malloc(MALLOC_SIZE);

    //address在数据库中表统一规定为小端存储
    //倒序拷贝
    mymemccpy(endian_address_buff,address_buff,DEVADDR_LEN);  
    new_address = ChArray_To_String(new_address_buff,endian_address_buff,DEVADDR_LEN);
    DEBUG_CONF("check address: %s\n",new_address);    
    //开BEGIN TRANSACTION功能，可以大大提高插入表格的效率
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);   
    //存储到sql_deveui中
    sprintf(sql_address_buff,"SELECT* FROM GWINFO WHERE Devaddr ='%s';",new_address);

    //parpare
    rc = sqlite3_prepare_v2(db,sql_address_buff,strlen(sql_address_buff),&stmt_data,NULL); 
    if (SQLITE_OK !=rc || NULL == stmt_data)
    {
          printf("\n\n parse prepare error!\n\n");
          sqlite3_close(db);
    }
    //execute
    //如果在设备表中能够找到该deveui
    while(SQLITE_ROW == sqlite3_step(stmt_data))
    {  
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
        sqlite3_reset(stmt_data);
        sqlite3_finalize(stmt_data);  
        free(new_address);
        free(sql_address_buff);
        return 1;
    }
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    //销毁sqlite3_prepare_v2创建的对象
    sqlite3_finalize(stmt_data);  
    free(new_address);
    free(sql_address_buff);
    return 0;
}

/**
 * 防止devaddr冲突
 * 如果表中已经存在相同的地址则函数返回1，否则返回0
 * 
 */ 
int Prevent_Addr_Identical(sqlite3 *db,uint32_t *address)
{
    sqlite3_stmt *stmt_data;
    int rc;

    uint8_t address_buff[4];
    uint8_t endian_address_buff[4];

    char *new_address      =  NULL;
    char *new_address_buff = (char*)malloc(2*DEVADDR_LEN);
    char *sql_address_buff = (char*)malloc(MALLOC_SIZE); 
    
    memset(address_buff,       0,sizeof(address_buff));
    memset(endian_address_buff,0,sizeof(endian_address_buff));   

    /* 移位操作，获取解析后的address */
    for(size_t i = 0,move_bit=0; i < 4; i++){
        
            address_buff[i] = (uint8_t)(*address >> move_bit);
            move_bit +=8;
    }

    /* 倒序拷贝 */
    mymemccpy(endian_address_buff,address_buff,DEVADDR_LEN);  

    new_address = ChArray_To_String(new_address_buff,endian_address_buff,DEVADDR_LEN); 

    sprintf(sql_address_buff,"SELECT* FROM GWINFO WHERE Devaddr ='%s';",new_address);

    /* parpare */
    rc = sqlite3_prepare_v2(db,sql_address_buff,strlen(sql_address_buff),&stmt_data,NULL); 

    if (SQLITE_OK !=rc || NULL == stmt_data){

            printf("\n\n parse prepare error!\n\n");
            sqlite3_close(db);
          
    } 

    /* execute */
    /* 如果在设备表中能够找到该deveui 则返回1 */
    if(SQLITE_ROW == sqlite3_step(stmt_data)){

            /* 销毁sqlite3_prepare_v2创建的对象 */
            sqlite3_finalize(stmt_data);  
            free(new_address);
            free(sql_address_buff);
            DEBUG_CONF("has exit this address");
            return  ADDRESS_IDENTICAL;    

    }else{  
            /* 销毁sqlite3_prepare_v2创建的对象 */
            sqlite3_finalize(stmt_data);  
            free(new_address);
            free(sql_address_buff);
            return ADDRESS_DIFFERENT;
        }  
      
}

//===============================线程函数实现===================================================//

/**
 *brief: 线程函数 
 *       进行join节点数据的入队，排序操作
 */
void *Thread_Join_Queue(void *rxbuff)
{             

    uint16_t cycle_count;
    int repeat = -1;
    struct lgw_pkt_rx_s *arg_thread = NULL;
    
    Item   node; 
    memset(&node, 0,sizeof(Item));

    cycle_count = 0;

    /* debug */
    int queue_count;

    /* toa 时间 */
    uint32_t toa = 0;

    while ((repeat == -1) || (cycle_count < repeat)){

            cycle_count++;

            /* 等待join信息到来 */         
            sem_wait(&Join_Data_Queue_SemFlag); 

            /* rxpkt 地址赋值 */
            arg_thread = (struct lgw_pkt_rx_s *) rxbuff;

            /* 获取当前时间 */
            struct timeval Time;
            
            /* join request 延时队列 */
            memcpy(node.deveui, arg_thread->payload+9,8);
            memcpy(node.appeui, arg_thread->payload+1,8);
            node.devnonce  = (uint16_t) arg_thread->payload[17];
            node.devnonce |= (uint16_t) arg_thread->payload[18]<<8;
            
            node.freq_hz   =  arg_thread->freq_hz;
            node.coderate  =  arg_thread->coderate;
            node.datarate  =  arg_thread->datarate;
            
            /* 获取当前时间戳,单位us */
            gettimeofday(&Time, NULL);
            node.T1 = Time.tv_sec*1000000+Time.tv_usec;
            /* 节点延时属性,单位us */
        
            /* 出队时间延时 */
            node.delay = 4800000;

            /* 出队列的时间 t1+ qut_queue_delay */
            node.Qut_Queue_Delay = ((node.T1) + (node.delay));
        
            /* 赋值接收到节点的时间戳 */
            node.count_us    = arg_thread->count_us;

            /* 设定真实的延时时间 */
            node.real_delay  = 5000000;

            /*  设定真实的下发时间 */
            /*  tmst + defalut delay -toa */
            toa = rx_lgw_time_on_air(arg_thread);
            node.tx_count_us = arg_thread->count_us + node.real_delay - toa;  
        
            /* 将节点入队 */
            EnQueue(node,&line);

            /* 排序 */
            Insertion_Sort(&line);  

            /* debug */
            queue_count = QueueItemCount(&line);
            DEBUG_CONF("\n queue count: %d \n",queue_count);   

            /* 信号量+1 */
            sem_post(&Join_SemFlag);
        
            /* 线程结束 */
            pthread_exit(0);

            if ((quit_sig == 1) || (exit_sig == 1)) {

                pthread_exit(0);
                break;
            }         
    }
}

/** 
 *  brief: 下发 join accept 线程
 *  取出队列中线程
 *  比较时间
 *  join accept数据打包
 * 
 */ 
void *Thread_Send_Queue_Task( void )
{   
    
    /* 测试运行时间的变量 */
    struct timeval start;
    struct timeval end;
    struct timeval nowtime;
    unsigned long timer = 0;

    /* 控制while循环体变量 */
    uint16_t cycle_count;
    int repeat = -1;

    /* 取 join request 队列项的变量 */
    unsigned long quit_time;
    uint8_t  DevEui[8]; 
    uint8_t  AppEui[8];
    uint16_t DevNonce;
    uint32_t tx_count_us;
    uint32_t freq_hz;
    uint8_t  coderate;
    uint32_t datarate;

    /* 定义一个devaddr,采用自增算法处理，保证devaddr的唯一性 */
    static unsigned int join_devaddr = 0; 
    uint8_t      join_devaddr_buff[4];   

    /* 前导码长度 */
    int  preamb = 8;
    /* 发送功率 */
	int  pow    =20;
	bool invert = true;

    /* 网关生成的 mic 4bytes */
	uint32_t  MICgateway = 0;
    /* 加密载荷 */
    uint8_t Tx_Encrypt_Payload [Join_Accept_Len];
	
    int k;
    /* lgw_send 状态检测变量 */	
    uint8_t status_var;

    /* join request 数据队列中临时的项 */
    Item temp;
    unsigned long diff_time=0;
    unsigned long nowtime_buff = 0;

    /* devaddr文件变量 */
    int fd_devaddr;
    int size_devaddr;
    uint8_t  devaddr_counter[4];
    const uint8_t *devaddr_counter_string = (uint8_t*)malloc(8);
    uint8_t *write_devaddr_string         = (uint8_t*)malloc(8);

    /* 频段配置变量、rx1droffset变量 */
    uint32_t up_frequency;
    uint32_t down_frequency;
    uint8_t  rx1droffset;
    uint8_t  region;

    /* lgw_send()返回状态 */
    int      result = LGW_HAL_SUCCESS;
    uint8_t  tx_status;

    /* join accept 缓存区 */
    uint8_t Tx_JoinAccept_buff [Join_Accept_Len];
    
    /* join accept 数据包变量 */
    Join_Accept	Joinaccept;

    /*  发生数据包变量 */
    struct  lgw_pkt_tx_s Tx_Join_accept;

    /* init */
    memset(DevEui,0,8);
    memset(AppEui,0,8);
    memset(devaddr_counter,0,4);
	memset(&Joinaccept,0,sizeof(Join_Accept));
    memset(join_devaddr_buff,0,sizeof(join_devaddr_buff));
	memset(&Tx_Join_accept,0,sizeof(Tx_Join_accept));
	memset(Tx_JoinAccept_buff,0,Join_Accept_Len);
    memset(Tx_Encrypt_Payload,0,Join_Accept_Len);

    while ( (repeat == -1) || (cycle_count < repeat ) ) {
            
            cycle_count++;

            /* 等待join request 数据队列出队信号 */
            sem_wait(&Join_SemFlag);      
            DEBUG_CONF(" wait Join_SemFlag: %d\n",Join_SemFlag); 

            /* 取join request 数据包 */
        if ( 1 == GetFront_Queue(&line,&quit_time,
                                DevEui,
                                &DevNonce,
                                AppEui,
                                &tx_count_us,
                                &freq_hz,
                                &coderate,
                                &datarate) )
            {


                    DEBUG_CONF("join request queue is empty!\n");
                    continue;    


            }           

            /* 计算当前最新时间 */
            gettimeofday(&nowtime, NULL);

            /* 计算离队时间和最新本地时间差值 */
            diff_time = quit_time - (nowtime.tv_sec*1000000+nowtime.tv_usec);
        
            /* 缓存本地时间 */
            nowtime_buff = nowtime.tv_sec*1000000+nowtime.tv_usec;

            /* 处理离队超时数据，丢弃超时数据包 */
            if ( nowtime_buff >= quit_time ){

                    /* 删除队列首个元素 */
                    if ( QueueIsEmpty(&line) ){

                            DEBUG_CONF("Nothing to list");
                    }else{

                            DeQueue(&temp,&line);
                            DEBUG_CONF("detele this queue\n");
                    }  
                    continue; /* 跳出这次执行,减少时间 */
            }

            /* 根据上报的deveui 取出 appkey */
            if ( 1 != Fetch_Appkey_Table(db,DevEui,AppKey ) ){
 
                    DEBUG_CONF("no this deveui!\n");
        
                    /* 删除队列首个元素 */
                    if ( QueueIsEmpty(&line)){

                            DEBUG_CONF("Nothing to list");
                    }else{

                            DeQueue(&temp,&line);
                            DEBUG_CONF("detele this queue\n");
                    }  
                    continue;//跳出这次执行,减少时间
            }
            /* 645 */
    
            
            /* 休眠差值时间 */
            usleep(diff_time); 
            DEBUG_CONF("\nsleep_time = %u us\n",diff_time); 

            /* rand */
            srand((int)time(0));	
        
            Joinaccept.MHDR.Bits.MType = FRAME_TYPE_JOIN_ACCEPT;
            Joinaccept.MHDR.Bits.Major = 0;
            Joinaccept.MHDR.Bits.RFU   = 0;

            /* AppNonce */
            Joinaccept.AppNonce1	    = (uint8_t)rand();
            Joinaccept.AppNonce2		= (uint8_t)rand();
            Joinaccept.AppNonce3		= (uint8_t)rand();
        
            /* NetID */
            Joinaccept.NetID1 		    = (uint8_t)rand();
            Joinaccept.NetID2 		    = (uint8_t)rand();
            Joinaccept.NetID3 		    = (uint8_t)rand();

            /* 读取本地文件中的 devaddr 信息 */
            fd_devaddr = open("/lorawan/lorawan_hub/devaddrinfo",O_RDWR);

            if(-1 == fd_devaddr){

                    DEBUG_CONF("sorry,There is no devaddrinfo file in this directory,Please check it!\n");            
                    close(fd_devaddr);

                    /* 删除队列首个元素 */
                    if ( QueueIsEmpty(&line)){

                            DEBUG_CONF("Nothing to list");
                    }else{

                            DeQueue(&temp,&line);
                            DEBUG_CONF("detele this queue\n");
                    }  

                    continue;    
            }
        
            size_devaddr = read(fd_devaddr,devaddr_counter_string,8);
            /* string to hex */
            String_To_ChArray(devaddr_counter,devaddr_counter_string,4);

            join_devaddr  =   devaddr_counter[3];
            join_devaddr |=  (devaddr_counter[2]<<8);
            join_devaddr |=  (devaddr_counter[1]<<16);
            join_devaddr |=  (devaddr_counter[0]<<24);

            /* 当devaddr达到最大时，从0开始重新自增 */
            if(0xffffffff == join_devaddr){

                    join_devaddr = 0x00;
            }
    
            /* 每次有一包join request 数据上来的时候 devaddr+1 */
            join_devaddr++;
            
            /* 将自增后的地址写回到本地记录devaddr的文件中 */
            devaddr_counter[3] = (uint8_t) join_devaddr;
            devaddr_counter[2] = (uint8_t)(join_devaddr>>8);
            devaddr_counter[1] = (uint8_t)(join_devaddr>>16);
            devaddr_counter[0] = (uint8_t)(join_devaddr>>24);

            /* hex to string */
            ChArray_To_String(write_devaddr_string,devaddr_counter,4);
            
            /* 文件指针移动到开头 */
            lseek(fd_devaddr,0,SEEK_SET);
            /* devaddr 写回到文件 */
            size_devaddr = write(fd_devaddr,write_devaddr_string,8);
            close(fd_devaddr);  
            /* 247us */
                    
            /* 查数据库中是否有冲突的devaddr */
            if ( ADDRESS_IDENTICAL == Prevent_Addr_Identical(db,&join_devaddr) ){

                    DEBUG_CONF("GW table has same address!\n"); 
                    
                    /* 删除队列首个元素 */
                    if ( QueueIsEmpty(&line)){

                            DEBUG_CONF("Queue is empty nothing to list");
                    }else{

                            DeQueue(&temp,&line);
                            DEBUG_CONF("Detele this queue\n");
                    }  
                    continue;          
            }
            /* 900us */

            Joinaccept.Devaddr		      = join_devaddr;
            /* 缓存devaddr，用于后面查数据库表使用 */
            join_devaddr_buff[0]          = (uint8_t) Joinaccept.Devaddr;
            join_devaddr_buff[1]          = (uint8_t)(Joinaccept.Devaddr>>8) &0XFF;
            join_devaddr_buff[2]          = (uint8_t)(Joinaccept.Devaddr>>16)&0XFF;
            join_devaddr_buff[3]          = (uint8_t)(Joinaccept.Devaddr>>24)&0XFF;

            /* 取出rx1droffset */
            if ( 0 == Fetch_RX1DRoffset_Info(db,&rx1droffset) ){

                    DEBUG_CONF("fetch rx1droffset error!\n");

                    /* 删除队列首个元素 */
                    if ( QueueIsEmpty(&line)){

                            DEBUG_CONF("Nothing to list");
                    }else{

                            DeQueue(&temp,&line);
                            DEBUG_CONF("detele this queue\n");
                    } 

                    continue;
            }

            /* DLSettings */ 
            Joinaccept.DLSettings.uDLSettings_t.RX1DRoffset = rx1droffset;  
        
            /* RxDelay */
            Joinaccept.RxDelay = (uint8_t)0x01;

            /* 根据上行频率取出对应的下行频率 */
            up_frequency = freq_hz;
            
            if (0 == Fetch_DownLink_Freq_Info(db,&up_frequency,&down_frequency) ){

                    DEBUG_CONF("fetch down frequency fail!\n");   

                    /* 删除队列首个元素 */
                    if ( QueueIsEmpty(&line)){

                            DEBUG_CONF("Nothing to list");
                    }else{

                            DeQueue(&temp,&line);
                            DEBUG_CONF("detele this queue\n");
                    }  

                    continue;
            }
            /* 700us */

            /* 设置参考lora_gateway -> Util_tx_test.c */
            Tx_Join_accept.freq_hz	  = down_frequency;
            Tx_Join_accept.tx_mode    = TIMESTAMPED;
            Tx_Join_accept.rf_chain	  = TX_RF_CHAIN;
            Tx_Join_accept.rf_power   = pow;
            Tx_Join_accept.modulation = MOD_LORA;
            Tx_Join_accept.bandwidth  = BW_125KHZ;
    
            /* 计算下发datarate */
            Tx_Join_accept.datarate	  = RegionCN470ApplyDrOffset(0,datarate,0);

            Tx_Join_accept.coderate	  = coderate;
            Tx_Join_accept.invert_pol = invert;
            Tx_Join_accept.preamble	  = preamb;
            Tx_Join_accept.size       = 0x11;
            Tx_Join_accept.count_us   = tx_count_us;


            /* 填充join accept 的payload */

            /* mhdr */
            Tx_JoinAccept_buff[0] = Joinaccept.MHDR.Value;

            /* AppNonce */
            Tx_JoinAccept_buff[1] = Joinaccept.AppNonce1;
            Tx_JoinAccept_buff[2] = Joinaccept.AppNonce2;
            Tx_JoinAccept_buff[3] = Joinaccept.AppNonce3;

            /* NetID */
            Tx_JoinAccept_buff[4] = Joinaccept.NetID1;
            Tx_JoinAccept_buff[5] = Joinaccept.NetID2;
            Tx_JoinAccept_buff[6] = Joinaccept.NetID3;
        
            /* Devaddr */
            Tx_JoinAccept_buff[7]  = (uint8_t) Joinaccept.Devaddr;
            Tx_JoinAccept_buff[8]  = (uint8_t)(Joinaccept.Devaddr>>8);
            Tx_JoinAccept_buff[9]  = (uint8_t)(Joinaccept.Devaddr>>16);
            Tx_JoinAccept_buff[10] = (uint8_t)(Joinaccept.Devaddr>>24);
        
            /* DLSettings */
            Tx_JoinAccept_buff[11] = Joinaccept.DLSettings.Value;
            Tx_JoinAccept_buff[12] = Joinaccept.RxDelay;

            /* 计算网关上的 mic 值 */
            LoRaMacJoinComputeMic(Tx_JoinAccept_buff,Com_MIC_Size,AppKey,&MICgateway); 
            /* 32 us*/ 

            /* 将计算出的MIC存入buff中 */
            Tx_JoinAccept_buff[13] =  MICgateway;
            Tx_JoinAccept_buff[14] = (MICgateway>>8);
            Tx_JoinAccept_buff[15] = (MICgateway>>16);
            Tx_JoinAccept_buff[16] = (MICgateway>>24);

            /* 计算网关上的nwkskey和appskey */
            /* 从AppNonce开始 */
            LoRaMacJoinComputeSKeys(AppKey,Tx_JoinAccept_buff+1,
                                            DevNonce,
                                            LoRaMacNwkSKey,
                                            LoRaMacAppSKey);

            /* 载荷加密 */
            LoRaMacJoinEncrypt(Tx_JoinAccept_buff+1,
                                            Join_Accept_Len-1,
                                            AppKey,
                                            Tx_Encrypt_Payload+1);
            /* 注意这里要再进行一次赋值! */
            Tx_Encrypt_Payload[0] = Joinaccept.MHDR.Value;
        
            /**完成网关上的工作：
             * 计算MIC
             * 计算nwkskey和appskey
             * 载荷加密	
             * 将加密后的载荷打包发给Tx_Join_accept.payload
             */
            memcpy(Tx_Join_accept.payload,Tx_Encrypt_Payload,Join_Accept_Len);
            /* 30us */

            /* 更新表中该deveui 的nwkskey和appskey,devaddr,appeui */   
            Update_GwInfo_Table(db,zErrMsg,DevEui,LoRaMacNwkSKey,LoRaMacAppSKey,join_devaddr_buff,AppEui);				       
            /*  35460us */ 
    
            /* 发送join_accept数据包 */
      
            /* 计算发送时间 */
            gettimeofday(&start,NULL);
            result = lgw_send(Tx_Join_accept); /* non-blocking scheduling of TX packet */			
            gettimeofday(&end,NULL);
            timer = (end.tv_sec - start.tv_sec)*1000000 + (end.tv_usec - start.tv_usec);
            DEBUG_CONF("\nwait_timer = %lu us\n",timer); 

            /* 发送结果检查 */
            if ( result == LGW_HAL_ERROR) {

                    DEBUG_CONF("Join data send error\n");    
        
            }else if (result == LGW_LBT_ISSUE ){

                    DEBUG_CONF("Failed: Not allowed (LBT)\n");
            }else {
                    /* wait for packet to finish sending */

                    //int loop = 0;
                    int scheduled_counter = 0;

                    do 
                    {                                                                        
                           //loop+=1;
                            
                            wait_ms(5);
                            lgw_status(TX_STATUS, &status_var); /* get TX status */
                            /* debug */
                            //DEBUG_CONF("\n  loop: %d get lgw_send status: %s\n", loop,lgw_send_status[status_var]);

                            if (status_var == TX_SCHEDULED) {
                                    
                                    scheduled_counter+=1;
                                    /* debug */
                                    //DEBUG_CONF("\nscheduled_counter: %d\n", scheduled_counter );
                            }
                            
                            /***
                             * lgw_send()发生错误处理机制 
                             * 当lgw_send()的状态一直在TX_SCHEDULED状态超时后，则默认发送失败
                             * 流产该次发送，后续数据会在 sx1301发送缓存区覆盖该次数据
                             * 超时时间计算： 默认延时时间 ( 5s ) - 出队时间 ( 4.8s ) = 200ms
                             *  5 * 40 = 200 ms 
                             * 
                             * update:经实际测试：发送一包数据需要经过三个状态：
                             *          TX_SHEDULED ----> TX_EMITTING  ----> TX-FREE
                             *          发送完一包 join request 数据需要 最大需要 55 * 5ms
                             * 
                             *          当 scheduled_counter >55 时 默认该包数据发生失败
                             */

                            /* tx 错误处理机制  */
                            if ( scheduled_counter >= 55) {

                                    /* 中止发生 */      
                                    result = lgw_abort_tx();

                                    DEBUG_CONF("\n===============================Join Accept TX_SCHEDULED is overtime! Abort tx packet!===================================================\n");
                                    if ( result == LGW_HAL_SUCCESS ) {

                                            DEBUG_CONF("\n LGW_HAL_SUCCESS: %d \n", result );

                                    } else {

                                              DEBUG_CONF("\n LGW_HAL_ERROR: %d \n", result );         
                                    }

                                    /* 删除队列首个元素 */
                                    if ( QueueIsEmpty(&line)){

                                            DEBUG_CONF("Nothing to list");
                                    }else{

                                            DeQueue(&temp,&line);
                                            DEBUG_CONF("detele this queue\n");
                                    }  
                                    
                                    continue;              
                            }       
     
                    } 
                    while (status_var != TX_FREE);
                    DEBUG_CONF("\n ============================================== Join Accept Data Send OK!!!=================================================\n");           
        }

        /* 删除队列首个元素 */
        if ( QueueIsEmpty(&line) ){

                DEBUG_CONF("Nothing to list");
        } else {

                DeQueue(&temp,&line);
                DEBUG_CONF("detele this queue\n");
        }

    }
}

/***
 *  线程函数   ：向lora_okt_server.c (服务器端)发送join request 数据
 *  
 *  parameter : rxpkt 结构体指针
 * 
 *  return    : NULL
 * 
 * 
 */ 
void *Thread_Send_Join(void *rxbuff)
{     
    uint16_t cycle_count;
    int repeat = -1;

    /* 创建结构体指针变量 */  
    struct lgw_pkt_rx_s *arg_thread = NULL;
   
    uint8_t deveui[8];
   
    /**
     * 创建与lora_pkt_server.c传输数据变量
     * 
     * 连接方式：udp
     * 
     */  

    int     sock_pkt_server_fd;
    struct  sockaddr_in pkt_server_addr;
    uint8_t send_join[TX_BYTE_LEN];   
    int     joindata_len;
    int     len = 0;

    /* init */
    memset(send_join,0,TX_BYTE_LEN); 
    memset(deveui,0,8);

    /* lora_pkt_fwd <------> lora_pkt_server transport protocol: udp */

    /* creat socket */
    sock_pkt_server_fd = socket(AF_INET,SOCK_DGRAM,0);
    if ( -1 == sock_pkt_server_fd ) {

            DEBUG_CONF(" socket error!\n");
            close(sock_pkt_server_fd);
    }

    /* set sockaddr_in parameter */
    memset( &pkt_server_addr,0,sizeof(struct sockaddr_in) );
    pkt_server_addr.sin_family      = AF_INET;
    pkt_server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");  

    /* 上报join request 的监听端口  */ 
    pkt_server_addr.sin_port        = htons(5555);

    while ((repeat == -1) || (cycle_count < repeat))
    {
        cycle_count++;

        /* 等待join request信息 */
        sem_wait(&Send_Join_Data_Server_SemFlag);

        /* 结构体指针强制转换 */
        arg_thread = (struct lgw_pkt_rx_s *)rxbuff; 

        /* 取出deveui */
        memcpy(deveui, arg_thread[0].payload+9,8);
        
        /* 序列化函数 */
        joindata_len = LoRaWAN_Join_Serialization(arg_thread,send_join);

        /* 数据发送 */
        len = sendto(sock_pkt_server_fd,send_join,
                                        joindata_len,
                                        0,
                                        (struct sockaddr* )&pkt_server_addr,
                                         sizeof(struct sockaddr_in)
                                         ); 
        if(len <= 0){

                DEBUG_CONF("can't send join data\n");
                close(sock_pkt_server_fd);
                continue; 
        }

        /* exit loop on user signals */
        if ((quit_sig == 1) || (exit_sig == 1)) {

                pthread_exit(0);
                break;        
        }

    }
}

/***
 *  线程函数   ：向lora_okt_server.c (服务器端)发送join maypayload (解密后的) 数据
 *  
 *  parameter : serialization_data 结构体指针
 * 
 *  return    : NULL
 * 
 * 
 */ 
void *Thread_Send_Server_Data(void *send_buff)
{
    uint16_t cycle_count;
    int repeat = -1;
    
    struct serialization_data *p_buff = NULL;
    /*udp连接 */
    int socket_fd;
    struct sockaddr_in server_addr;
    int len;
    
    /* creat sockfd */
    socket_fd = socket(AF_INET,SOCK_DGRAM,0);
    if ( -1 == socket_fd ) {

            DEBUG_CONF("socket error!\n");
            close(socket_fd);
    }

    /* set sockaddr_in parameter */
    memset(&server_addr,0,sizeof(struct sockaddr_in));
    server_addr.sin_family       = AF_INET;
    server_addr.sin_addr.s_addr  = inet_addr("127.0.0.1");

    /* 规定: 5555，用于监听join数据包 5556, 用于监听解密后上报的数据包 */
    server_addr.sin_port         = htons(5556);

    cycle_count = 0;
    while ((repeat == -1) || (cycle_count < repeat))
    {   
        cycle_count++;  

        /* 等待解密成功信号量 */
        sem_wait(&Send_Parse_Data_To_Server_SemFlag);
        
        /* 结构体指针强制转换 */
        p_buff = (struct serialization_data*)send_buff;
        
        /* 向服务器发生数据 */
        len = sendto(socket_fd,p_buff->data,p_buff->length,0,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
        
        if ( len <= 0 ){

                DEBUG_CONF("can't send pasre successful data\n");
                close(socket_fd);
        }

        /* clear */
        memset(&struct_serialization_data,0,sizeof(struct serialization_data));
        
        /* exit loop on user signals */
		if ((quit_sig == 1) || (exit_sig == 1)) 
		{   
            pthread_exit(0);
			break;
		}

    }  
}

/***
 *  线程函数   ：向lora_pkt_conf.c(上位机)发送join maypayload (解密后的) 数据
 *  
 *  parameter : serialization_data 结构体指针
 * 
 *  return    : NULL
 * 
 * 
 */ 

void *Thread_SendDecodeData(void* send_buff)
{
    uint16_t cycle_count;
    int repeat = -1;
    
    struct decode_data *p_buff = NULL;


    /* udp连接 */
    int socket_fd;
    struct sockaddr_in server_addr;
    int len;

    /* creat sockfd */
    socket_fd = socket(AF_INET,SOCK_DGRAM,0);
    if ( -1 == socket_fd ){

            DEBUG_CONF("socket error!\n");
            close(socket_fd);
    }

    /* set sockaddr_in parameter */
    memset(&server_addr,0,sizeof(struct sockaddr_in));
    server_addr.sin_family       = AF_INET;
    server_addr.sin_addr.s_addr  = inet_addr("127.0.0.1");

    /* 规定: 7789为向lora_pkt_conf.c(上位机)发送解密后的数据的端口号 */
    server_addr.sin_port         = htons(7789); 

    cycle_count = 0;
        
    while ((repeat == -1) || (cycle_count < repeat))
    {   
        cycle_count++;  

        /* 等待解密成功的信号量 */
        sem_wait(&Send_Parse_Data_To_GatewaySet_SemFlag);

        /* 指针强制转换 */
        p_buff = (struct decode_data*)send_buff;

        len = sendto(socket_fd,p_buff->data,p_buff->length,0,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
        
        if ( len <= 0 ){

                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"socket ip: %s\n",strerror(errno));
                DEBUG_CONF("can't send pasre successful data\n");
                close(socket_fd);
        }

        /* clear */
        memset(& struct_decode_data,0,sizeof(struct decode_data));
        
        /* exit loop on user signals */
		if ((quit_sig == 1) || (exit_sig == 1)) 
		{   
            pthread_exit(0);
			break;
		}
    }  
}

/**
 * 线程函数 
 * 
 * brief     ： 将节点上报的数据进行入队处理 
 * 
 * parameter ： NULL
 * 
 * return    ： NULL
 * 
 */
void *Thread_Task_Queue(void *databuff)
{
    /* 控制循环变量 */
    uint16_t cycle_count;
    int repeat = -1;

    /*rxpkt 结构体指针*/
    struct lgw_pkt_rx_s *p = NULL;
  
    uint32_t devaddr; 
    /* 创建一个节点中的项 */
    Task node;

    /* toa:空中传输时间变量 */
    uint32_t toa;

    /* debug */
    int queue_count = 0;
     
    /* init */
    memset(&node,0,sizeof(Task));

    while ((repeat == -1) || (cycle_count < repeat))
    {   
        /* 等待节点上报的数据来临 */
        sem_wait(&Node_Data_queue_SemFlag);

        /* 指针类型强制转换 */
        p = (struct lgw_pkt_rx_s *)databuff;
        
        devaddr  =   p->payload[1];
        devaddr |= ( p->payload[2] << 8  );
        devaddr |= ( p->payload[3] << 16 );
        devaddr |= ( p->payload[4] << 24 );

        /* 检查网关是否有该节点 */
        if(1 != Check_Devaddr(db,&devaddr)){

                DEBUG_CONF("gwinfo table no this devaddr");
                continue;
        }
       
        /* 获取当前时间 */
        struct timeval Time;
        
        /* 数据延时队列 */
        node.freq_hz   = p->freq_hz;
        node.bandwidth = p->bandwidth;
        node.datarate  = p->datarate;
        node.coderate  = p->coderate;
        node.size      = p->size;
        mymemcpy( node.payload, p->payload, p->size );

        /* 从这里获取当前时间戳,单位ms */
        gettimeofday(&Time, NULL);
        node.T1 = Time.tv_sec*1000000 + Time.tv_usec;

        /* 节点延时属性 */
        node.delay = 800000;

        /* 出队列的时间 t1 + qut_queue_delay */
        node.Qut_Queue_Delay = ( node.T1 + node.delay );
       
        /* 接收到数据的时间戳 */
        node.count_us    = p->count_us;
        
        /* 节点真实的延时时间 */
        node.real_delay  = 1000000;

        /*  节点下发的时间 */
        /*  tmst + defalut delay -toa */
        toa = rx_lgw_time_on_air(p);
        node.tx_count_us = p->count_us +  node.real_delay -toa;

        /* 将节点入队  */
        Task_EnQueue(node,&task_line);

        /* 快速排序    */
        Task_Insertion_Sort(&task_line);

        /* debug */
        queue_count = Task_QueueItemCount(&task_line);
        DEBUG_CONF("join macpayload queue count: %d \n",queue_count);   
        
        /* 信号量v操作 */
        sem_post(&Task_SemFlag);

        /* 线程结束 */
        pthread_exit(0);

        /* exit loop on user signals */
		if ((quit_sig == 1) || (exit_sig == 1)) {

                pthread_exit(0);
			    break;
		}
    }        

}

/**
 * 线程函数 
 * 
 * brief     ： 将节点上报的数据进行入队处理 
 * 
 * parameter ： NULL
 * 
 * return    ： NULL
 * 
 */
void *Thread_Fetch_Report_Data( void )
{ 
    uint16_t cycle_count;
    int repeat = -1;
    
    /* 管理时间的变量 */
    struct timeval nowtime;
    unsigned long quit_time    = 0;
    unsigned long diff_time    = 0;
    unsigned long nowtime_buff = 0;

    /* txpkt下发变量 */
    uint32_t freq;
    uint8_t  bandwidth;
    uint32_t datarate;
    uint8_t  coderate;
    uint16_t size;
    uint32_t tx_count_us;
    uint8_t  payload[500];
  
    /* 队列临时的节点的项 */
    Task temp;

    /* init */
    memset(payload,0,500);
    memset(&temp,0,sizeof(Task));

    while ( ( repeat == -1) || (cycle_count < repeat ) )
    {
        cycle_count++;

        /* 若队列中没有join macplayload数据，则阻塞该线程 */
        sem_wait(&Task_SemFlag);

        /* 取出头队列元素 */
        if ( 1 == Task_GetFront_Queue ( &task_line,&quit_time,
                                         &freq,
                                         &bandwidth,
                                         &datarate,
                                         &coderate,
                                         &size,
                                         payload,
                                         &tx_count_us ) )
        {

                DEBUG_CONF("Task queue is empty!\n");
                continue;

        }

        /* 获得当前时间 */
        gettimeofday(&nowtime, NULL);
        
        /* 出队时间 - linux本地绝对时间 == 差值时间 */
        diff_time    = quit_time - ( nowtime.tv_sec*1000000 + nowtime.tv_usec );
        nowtime_buff = nowtime.tv_sec*1000000 + nowtime.tv_usec;

        /* test debug */
        DEBUG_CONF("quit_time: %u\n",quit_time);
        DEBUG_CONF("nowtime:   %u\n",nowtime_buff);
        DEBUG_CONF("diff_time: %u\n",diff_time);

        /* 处理超时数据包，丢弃数据包 */
        if ( nowtime_buff >= quit_time ){

                /* 删除队列首个元素 */
                if ( Task_QueueIsEmpty( &task_line ) ){
                
                        DEBUG_CONF("queue is empty");

                } else {
                
                        Task_DeQueue(&temp,&task_line);
                        DEBUG_CONF("detele this queue\n");
            }  

            /* 跳出这次执行，减少时间 */
            continue;
        }
    
        /* 延时差异的时间 */
        usleep(diff_time);
        
        /**
         * 通知消费者取数据
         * 生产者信号量v操作
         * 
         */ 
        sem_post(&Producer_SemFlag);

        /* exit loop on user signals */
		if ((quit_sig == 1) || (exit_sig == 1)) {
			
                pthread_exit(0);
                break;
		}
    }          
}
/**
 * 线程函数      任务处理线程
 * 
 * brief     ： 将节点上报的需要应答的数据进行应答处理
 * 
 * parameter ： NULL
 * 
 * return    ： NULL
 * 
 */
void *Thread_Handle_Data_Task()
{
    /* main loop variable */
    uint16_t cycle_count;
    int repeat = -1; 
    
    /* txpkt下发的变量 */
    unsigned long quit_time = 0;
    uint32_t freq;
    uint8_t  bandwidth;
    uint32_t datarate;
    uint8_t  coderate;
    uint16_t size;
    uint32_t tx_count_us;

    /* 节点上报join macpayload 数据缓存区域 */
    uint8_t  node_payload[500];

    /* debug time */
    struct timeval start;
    struct timeval end;
    struct timeval start_send;
    struct timeval end_send;
    unsigned long timer;
    unsigned long timer_send;

    /* txpkt 结构体 */
    struct  lgw_pkt_tx_s  tx_buff;    

    uint8_t payload[1024];
    uint16_t payload_len;

    /* txpkt 参数配置 */
    int  pow    = 20;
    bool invert = true;
    int  preamb = 8;	
    uint8_t status_var;

    /* 下行数据所需临时变量 */
    uint8_t down_buff[500];
 
    LoRaMacHeader_t	        macHdr;
    LoRaMacHeader_t	        macHdr_temp;
    LoRaMacFrameDownCtrl_t  down_fCtrl;
    LoRaMacFrameCtrl_t      fCtrl;
    
    uint32_t address;
    uint8_t nwkskey[16];
    uint8_t appskey[16];

    /* 载荷加密缓存 */
    uint8_t encrypt_buff[500];

    uint32_t mic_gateway;
    int result;

    /* fcnt采用32位计数 */
    uint32_t fcnt_down = 0;    
    
    /* 队列临时的节点的项 */
    Data temp;   
    Task task_temp;

    /* 分包标志码 */
    uint32_t Multiple_Packets_Code;
    /* 分包标志位 */
    bool Multiple_Packets_Flag = false;
    /* 分包总数  */
    uint16_t Packet_Sum_total;
    /* 分包序   */
    uint16_t Packet_Number;
    /* 队列索引 */
    static   int packageid = 1;
    /* 记录队列中的初始最大总数 */
    static   int queue_count;

    /* 区域配置变量 */
    uint32_t down_frequency;
    uint8_t  region;
    uint8_t  rx1droffset;
    uint32_t down_datarate;

    /* class a队列中的项数 */
    int         class_a_count;
    uint16_t    classa_value_len;
    uint8_t     classa_valueBuf[1024];
    uint8_t     classa_deveui[8];
    class_A     node_temp;

    /* init */
    memset(node_payload,0,500);
    memset(&tx_buff, 0, sizeof(tx_buff));
    memset(payload,0,1024);
    memset(down_buff,0,500);
    memset(nwkskey,0,16);
    memset(appskey,0,16);
    memset(encrypt_buff,0,500);
    memset(&macHdr,      0, sizeof( LoRaMacHeader_t ) );
    memset(&macHdr_temp, 0, sizeof( LoRaMacHeader_t ) );
    memset(&down_fCtrl,  0, sizeof( LoRaMacFrameDownCtrl_t ));
    memset(&fCtrl,       0, sizeof( LoRaMacFrameDownCtrl_t ));

    while ((repeat == -1) || (cycle_count < repeat))
    {
        cycle_count++;

        /* 等待节点上行数据 */
        sem_wait( &Producer_SemFlag );
        
        /* 计算该线程处理时间 */
        gettimeofday( &start,NULL ); 

        /* 从节点上报join macpayload 数据队列中取出数据 */
        if ( 1 == Task_GetFront_Queue ( &task_line,&quit_time,
                                         &freq,&bandwidth,
                                         &datarate,
                                         &coderate,
                                         &size,
                                         node_payload,
                                         &tx_count_us ))
        {

                DEBUG_CONF("Task queue is empty!\n");
                continue;

        }


        /* 
            step1: 如果class a 存储队列中有数据，
            则先把class a 存储队列中的数据带下来，需满足deveui相同的情况，然后跳过循环 
            
            step2: 如果class a 存储队列中没有数据，
            则继续检查服务器端队列中是否有应答数据
        
        */
        class_a_count = ClassA_QueueItemCount ( &class_a_line);
        DEBUG_CONF("class_a_count: %d\n",class_a_count);
        if ( class_a_count >= 1)
        {
                
                /* 取出首队列数据 */
                if ( 1 == ClassA_GetFront_Queue ( &class_a_line, &classa_value_len, classa_valueBuf, classa_deveui)) {        
                        
                        DEBUG_CONF ("classa data get error!\n");
                }

                pthread_rwlock_rdlock(&rw_class_a_deveui_mux);

                if ( 0 == ArrayValueCmp (ClassA_Deveui, classa_deveui, 8))
                {   
                        pthread_rwlock_unlock(&rw_class_a_deveui_mux); 

                        /*
                            由生产者线程可知：join macpayload 队列中有数据，且下发没有超时 
                            删除 join macpayload 队列首个元素  
                        */
                        if ( false == Task_DeQueue(&task_temp,&task_line)){

                                    DEBUG_CONF("delete task queue is error!\n");    
                                    continue;
                        }

                        /* 判断队列中节点的总数 */
                        queue_count = class_a_count > queue_count ? class_a_count : queue_count;
                        DEBUG_CONF("queue_count is : %d\n",queue_count);                  

                        for ( ; packageid <= queue_count;  )
                        {
                                               
                                /* 发送class a 调试数据函数 */
                                SendClassADebugData (   db, &freq, &bandwidth, &datarate, &coderate, &tx_count_us,
                                                        node_payload, &queue_count, &packageid,classa_valueBuf,
                                                        &classa_value_len
                                                    );

                                /* 如果packageid 为最后一包，则重新置为1 */                    
                                if ( packageid == queue_count) {

                                        packageid   = 1;
                                        queue_count = 0;

                                } else{
                                   
                                        packageid++;
                                }
                                
                                /* 跳出for循环，等待下一包空包 */
                                break;

                        }
                         /* 发送完class a调试数据后跳过循环,等待下一包空包 */
                        continue;
                          
                }
                else
                {
                    pthread_rwlock_unlock(&rw_class_a_deveui_mux); 
                }
                        
        }
       
        /*
            由生产者线程可知：join macpayload 队列中有数据，且下发没有超时 
            删除 join macpayload 队列首个元素  
        */
        if ( false == Task_DeQueue(&task_temp,&task_line)){

                    DEBUG_CONF("delete task queue is error!\n");    
                    continue;
        }

        /*
            从服务器端队列取出服务器的应答数据
            如果返回 1 则取数据失败
        */
        if ( 1 ==  Data_GetFront_Queue ( &data_line,&payload_len,payload)){

            
                DEBUG_CONF("Data queue is empty!\n");
                continue;
        }

       /*
            定义分包标识码： 0xfbfcfdfe 
            该分包标识码是判断服务器下发的应答数据是否是多包数据
        */
        Multiple_Packets_Code  =  payload[payload_len - 5] ;
        Multiple_Packets_Code |=  payload[payload_len - 6] <<  8;
        Multiple_Packets_Code |=  payload[payload_len - 7] << 16;  
        Multiple_Packets_Code |=  payload[payload_len - 8] << 24;

        DEBUG_CONF("Multiple_Packets_Code:0x%02x\n",Multiple_Packets_Code);   
        DEBUG_CONF("payload_len: %d\n",payload_len);

        /* 由上行频率取下行频率 */
        if ( 0 == Fetch_DownLink_Freq_Info (db,&freq,&down_frequency ) ) {

                DEBUG_CONF("fetch down frequency fail!\n");                
                continue;
        }
        
        /* 取出区域信息 */
        if ( 0 == Fetch_Region_Info (db, &region) ) {

                DEBUG_CONF("fetch region info fail!\n");
                continue;
        }

        /* 取出rx1droffset 信息 */
        if ( 0 == Fetch_RX1DRoffset_Info (db, &rx1droffset) ) {
            
                DEBUG_CONF("fetch rx1droffset info fail!\n");
                continue;
        }

        /* 由region+上行datarate+rx1offset取下行datarate */
        down_datarate = RegionApplyDrOffset (region, 0, datarate, rx1droffset);
        
        /* 需要判断服务器回复的数据是否是多包数据 */
        if( 0xfbfcfdfe == Multiple_Packets_Code ){

                Multiple_Packets_Flag = true;

        } else {

                Multiple_Packets_Flag = false;
        }

        /* 需要做分包处理 */
        if ( Multiple_Packets_Flag )
        {
                Multiple_Packets_Flag = false;
                macHdr.Value       = node_payload[0];
                macHdr_temp.Value  = node_payload[0];
                
                /* 删除服务器应答数据队列首个元素 */
                if (Data_QueueIsEmpty(&data_line)){
                        
                        DEBUG_CONF("queue is empty");

                } else {
                    
                        Data_DeQueue(&temp,&data_line);
                        DEBUG_CONF("detele data queue\n");
                }

                Packet_Sum_total   = payload[payload_len -3];
                Packet_Sum_total  |= payload[payload_len -4] << 8;
                Packet_Number      = payload[payload_len -1];
                Packet_Number     |= payload[payload_len -2] <<8;

                DEBUG_CONF("Packet_Sum_total:0x%02x\n",Packet_Sum_total);
                DEBUG_CONF("Packet_Number:0x%02x\n",Packet_Number);    

                /* txpkt数据包封装 */
                tx_buff.freq_hz    = down_frequency;
                tx_buff.bandwidth  = bandwidth;
                tx_buff.datarate   = down_datarate;
                tx_buff.coderate   = coderate;
           
                tx_buff.tx_mode    = TIMESTAMPED; 
                tx_buff.rf_chain   = TX_RF_CHAIN;
                tx_buff.rf_power   = pow; 
                tx_buff.modulation = MOD_LORA;
                tx_buff.invert_pol = invert;
                tx_buff.preamble   = preamb;
                tx_buff.count_us   = tx_count_us;
                
                /* mType改为下行不应答类型 */
                macHdr.Bits.MType  = FRAME_TYPE_DATA_UNCONFIRMED_DOWN;
                tx_buff.payload[0] = node_payload[0];

                /* 修改下行数据类型 */
                tx_buff.payload[0] = ((uint8_t)((3<<tx_buff.payload[0])>>3) | (uint8_t)(macHdr.Bits.MType <<5)); 

                /* devaddr */
                tx_buff.payload[1] =  node_payload[1];
                tx_buff.payload[2] =  node_payload[2];
                tx_buff.payload[3] =  node_payload[3];
                tx_buff.payload[4] =  node_payload[4];

                /* devaddr temp */
                address            =  node_payload[1];
                address           |= (node_payload[2] << 8);
                address           |= (node_payload[3] << 16);
                address           |= (node_payload[4] << 24);          

                fCtrl.Value        =  node_payload[5];
            
            
                /* 判断应答位是否需要使能 */ 
                if ( macHdr_temp.Bits.MType == 0x04 ){

                        down_fCtrl.DownBits.Ack = 1;

                } else if ( macHdr_temp.Bits.MType == 0x02 ) {

                        down_fCtrl.DownBits.Ack = 0;

                } else {
                    
                        DEBUG_CONF("MType is error!\n");
                }

                down_fCtrl.DownBits.Adr 	 = fCtrl.Bits.Adr;
                down_fCtrl.DownBits.FOptsLen = fCtrl.Bits.FOptsLen;
                down_fCtrl.DownBits.RFU 	 = 0x0;  

                /* 不是最后一包数据前都开启fpending位 */ 
                if(Packet_Sum_total == Packet_Number)
                {
                        down_fCtrl.DownBits.FPending = 0x00;

                } else {
                        
                        down_fCtrl.DownBits.FPending = 0x01;
                }

                tx_buff.payload[5] =  down_fCtrl.Value;     
            
                /* 读取节点对应的fcnt_down */
                if ( 0 == Fetch_GWinfo_FcntDown(db,&address,&fcnt_down)) {
                    
                        DEBUG_CONF("fetch fcntdown error!\n");    
                        continue;
                }

                /* +1操作 */
                fcnt_down = fcnt_down+1;
                
                /* 更新节点对应的fcnt_down  耗时约30ms */
                if ( 0 == Update_GWinfo_FcntDown (db,zErrMsg,&address,&fcnt_down))
                {
                        DEBUG_CONF("update fcntdown error!\n");    
                        continue;
                }

                tx_buff.payload[6] = (uint8_t) fcnt_down;
                tx_buff.payload[7] = (uint8_t)(fcnt_down >> 8 );
                
                /* 区分应用作用，默认0x0a */
                tx_buff.payload[8] = 0x0a;

                /* FRMpayload */
                /* -8 : 4 bytes 分包标志码 + 4 bytes devaddr */
                payload_len = payload_len -8;

                /* 拷贝服务器下发的数据 */
                mymemcpy(tx_buff.payload+9, payload, payload_len); 

                /* 取密钥 */    
                if ( 0 == Fetch_Nwkskey_Appskey_Table(db,&address,nwkskey,appskey) ){

                        DEBUG_CONF("Fetch Nwkskey and Appskey error!\n");    
                        continue;
                }

                /* 载荷加密 */         
                LoRaMacPayloadEncrypt(tx_buff.payload+9,payload_len, appskey, address, DOWN_LINK,fcnt_down, encrypt_buff);
                
                /* 加密后的载荷再拷贝到发送的载荷中 */   
                mymemcpy( tx_buff.payload + 9, encrypt_buff, payload_len );

                /* 计算MIC */
                LoRaMacComputeMic(tx_buff.payload,payload_len+9,nwkskey, address, DOWN_LINK,fcnt_down, &mic_gateway);       
                tx_buff.payload[payload_len+9]  = (uint8_t) mic_gateway;
                tx_buff.payload[payload_len+10] = (uint8_t)(mic_gateway >> 8);
                tx_buff.payload[payload_len+11] = (uint8_t)(mic_gateway >> 16);
                tx_buff.payload[payload_len+12] = (uint8_t)(mic_gateway >> 24);        
                tx_buff.size                    =  payload_len + 13;    
               
                gettimeofday(&start_send,NULL); 

                /* 下发数据 */
                result=lgw_send(tx_buff);

                gettimeofday(&end,NULL);
                gettimeofday(&end_send,NULL);
                
                timer = (end.tv_sec - start.tv_sec)*1000000 + (end.tv_usec - start.tv_usec); 
                timer_send = (end_send.tv_sec - start_send.tv_sec)*1000000 + (end_send.tv_usec - start_send.tv_usec); 
                DEBUG_CONF("\nhandle data task is:= %lu us\n",timer);
                DEBUG_CONF("\nsend   data time is:= %lu us\n",timer_send);

                /* 发送结果检查 */
                if ( result == LGW_HAL_ERROR) {

                        DEBUG_CONF("Join data send error\n");    
            
                }else if (result == LGW_LBT_ISSUE ){

                        DEBUG_CONF("Failed: Not allowed (LBT)\n");
                }else {
                    /* wait for packet to finish sending */
                    //int loop = 0;
                    int scheduled_counter = 0;
                    do 
                    {       //loop+=1;        
                            wait_ms(5);
                            lgw_status(TX_STATUS, &status_var); /* get TX status */
                            /* debug */
                            //DEBUG_CONF("\n  loop: %d get lgw_send status: %s\n", loop,lgw_send_status[status_var]);

                            if (status_var == TX_SCHEDULED) {
                                    
                                    scheduled_counter+=1;
                                    /* debug */
                                    //DEBUG_CONF("\nscheduled_counter: %d\n", scheduled_counter );
                            }
                            
                            /***
                             * lgw_send()发生错误处理机制 
                             * 当lgw_send()的状态一直在TX_SCHEDULED状态超时后，则默认发送失败
                             * 流产该次发送，后续数据会在 sx1301发送缓存区覆盖该次数据
                             * 超时时间计算： 默认延时时间 ( 1s ) - 出队时间 ( 0.8s ) = 200ms
                             *  5 * 40 = 200 ms 
                             * 
                             * 
                             * update:经实际测试：发送一包数据需要经过三个状态：
                             *          
                             *          TX_SHEDULED ----> TX_EMITTING  ----> TX-FREE
                             *      
                             *         
                             *              
                             *         
                             * 
                             */

                            /* tx 错误处理机制  */
                            if ( scheduled_counter >= 55 ) {

                                    /* 中止发生 */      
                                    result = lgw_abort_tx();

                                    DEBUG_CONF("\n=============================== Payload TX_SCHEDULED is overtime! Abort tx packet!===================================================\n");
                                    if ( result == LGW_HAL_SUCCESS ) {

                                            DEBUG_CONF("\n LGW_HAL_SUCCESS: %d \n", result );

                                    } else {

                                              DEBUG_CONF("\n LGW_HAL_ERROR: %d \n", result );         
                                    }

                                    /* 删除队列首个元素 */
                                    if ( QueueIsEmpty(&line)){

                                            DEBUG_CONF("Nothing to list");
                                    }else{

                                            DeQueue(&temp,&line);
                                            DEBUG_CONF("detele this queue\n");
                                    }  
                                    
                                    continue;              
                            }    

                    } while (status_var != TX_FREE);

                    DEBUG_CONF("\n ============================================== Confrim Data Send OK!!!=================================================\n");           
            
            }

                //clear
                Multiple_Packets_Flag = false;
                freq = 0;
                bandwidth = 0;
                datarate = 0;
                coderate = 0;
                size = 0;
                tx_count_us = 0;
                payload_len = 0;
                memset(payload,0,1024);
                memset(down_buff,0,500);
                memset(nwkskey,0,16);
                memset(appskey,0,16);
                memset(encrypt_buff,0,500);

        } 
        else   
        {
                /***
                 * 服务器下发数据不分包：包含下面三种情况
                 *   
                 *                    1：服务器无应答数据，且节点上报类型为 unconfirm 则不处理 
                 * 
                 *                    2：服务器无应答数据，节点上报类型为   confirm 则下发一包空包给节点  
                 * 
                 *                    3：服务器应答了数据，则下发。 
                 */                   
                
                /* type */
                macHdr.Value      = node_payload[0];
                macHdr_temp.Value = node_payload[0];

                /* unconfirmed 类型，服务器没有应答数据的情况，不处理 */  
                if ( ( payload_len == 0x00 ) && ( macHdr.Bits.MType == 0x02 ) ) 
                {
                      
                } 
                else if ( ( payload_len == 0x00 ) && ( macHdr.Bits.MType == 0x04 ) ) 
                {

                    /* confirmed 类型，服务器没有应答数据的情况，给节点发送空包 */
                    tx_buff.freq_hz    = down_frequency;
                    tx_buff.bandwidth  = bandwidth;
                    tx_buff.datarate   = down_datarate;
                    tx_buff.coderate   = coderate;
                    tx_buff.size       = payload_len + 12;
                    tx_buff.tx_mode    = TIMESTAMPED; 
                    tx_buff.rf_chain   = TX_RF_CHAIN;
                    tx_buff.rf_power   = pow; 
                    tx_buff.modulation = MOD_LORA;
                    tx_buff.invert_pol = invert;
                    tx_buff.preamble   = preamb;
                    tx_buff.count_us   = tx_count_us;

                    /* 改为下行不应答类型 */
                    macHdr.Bits.MType  = FRAME_TYPE_DATA_UNCONFIRMED_DOWN;        
                    tx_buff.payload[0] = node_payload[0];

                    /* 修改下行数据类型 */
                    tx_buff.payload[0] = ((uint8_t)((3<<tx_buff.payload[0])>>3) | (uint8_t)(macHdr.Bits.MType <<5)); 
                    
                    /* devaddr */
                    tx_buff.payload[1] =  node_payload[1];
                    tx_buff.payload[2] =  node_payload[2];
                    tx_buff.payload[3] =  node_payload[3];
                    tx_buff.payload[4] =  node_payload[4];
                    
                    address            =  node_payload[1];
                    address           |= (node_payload[2]<<8);
                    address           |= (node_payload[3]<<16);
                    address           |= (node_payload[4]<<24);          
                    
                    /* 使能应答位 */
                    fCtrl.Value = node_payload[5];
                    down_fCtrl.DownBits.Ack      = 1;
                    down_fCtrl.DownBits.Adr 	 = fCtrl.Bits.Adr;
                    down_fCtrl.DownBits.FOptsLen = fCtrl.Bits.FOptsLen;
                    down_fCtrl.DownBits.RFU 	 = 0x0;
                    tx_buff.payload[5] = down_fCtrl.Value;
                    
                    /* 读取节点对应的fcnt_down */
                    if ( 0 == Fetch_GWinfo_FcntDown( db,&address,&fcnt_down ) ){

                            DEBUG_CONF("fetch fcntdown error!\n");    
                            continue;
                    } 
                    /* +1 操作 */
                    fcnt_down = fcnt_down +1;  
                    
                    /* 更新该节点的fcntdown到本地数据库 */
                    if ( 0 == Update_GWinfo_FcntDown( db,zErrMsg,&address,&fcnt_down ) ) {

                            DEBUG_CONF("update fcntdown error!\n");    
                            continue;
                    }

                    tx_buff.payload[6] = (uint8_t)fcnt_down;
                    tx_buff.payload[7] = (uint8_t)(fcnt_down>>8);
                    
                    /* 计算MIC */
                    LoRaMacComputeMic(tx_buff.payload,8,nwkskey, address, DOWN_LINK,fcnt_down, &mic_gateway);
                    tx_buff.payload[payload_len+8]  = (uint8_t) mic_gateway;
                    tx_buff.payload[payload_len+9]  = (uint8_t)(mic_gateway  >> 8  );
                    tx_buff.payload[payload_len+10] = (uint8_t)(mic_gateway  >> 16 );
                    tx_buff.payload[payload_len+11] = (uint8_t)(mic_gateway  >> 24 );

                    result = lgw_send(tx_buff);

                    /* 发送结果检查 */
                    if ( result == LGW_HAL_ERROR) {

                            DEBUG_CONF("Join data send error\n");    
                
                    }else if (result == LGW_LBT_ISSUE ){

                            DEBUG_CONF("Failed: Not allowed (LBT)\n");

                    } else {

                            /* wait for packet to finish sending */
                            //int loop = 0;
                            int scheduled_counter = 0;
                            do 
                            {      //loop+=1;
                                    
                                    wait_ms(5);
                                    lgw_status(TX_STATUS, &status_var); /* get TX status */
                                    
                                    /* debug */
                                    //DEBUG_CONF("\n  loop: %d get lgw_send status: %s\n", loop,lgw_send_status[status_var]);

                                    if (status_var == TX_SCHEDULED) {
                                            
                                            scheduled_counter+=1;
                                            /* debug */
                                            //DEBUG_CONF("\nscheduled_counter: %d\n", scheduled_counter );
                                    }
                                    
                                    /***
                                     * lgw_send()发生错误处理机制 
                                     * 当lgw_send()的状态一直在TX_SCHEDULED状态超时后，则默认发送失败
                                     * 流产该次发送，后续数据会在 sx1301发送缓存区覆盖该次数据
                                     * 超时时间计算： 默认延时时间 ( 5s ) - 出队时间 ( 4.8s ) = 200ms
                                     *  5 * 40 = 200 ms 
                                     * 
                                     * update:经实际测试：发送一包数据需要经过三个状态：
                                     *          TX_SHEDULED ----> TX_EMITTING  ----> TX-FREE
                                     *          
                                     *         
                                     *              
                                     *          
                                     */

                                    /* tx 错误处理机制  */
                                    if ( scheduled_counter >= 55 ) 
                                    {

                                            /* 中止发生 */      
                                            result = lgw_abort_tx();

                                            DEBUG_CONF("\n=============================== Payload TX_SCHEDULED is overtime! Abort tx packet!===================================================\n");
                                            if ( result == LGW_HAL_SUCCESS ) {

                                                    DEBUG_CONF("\n LGW_HAL_SUCCESS: %d \n", result );

                                            } else {

                                                    DEBUG_CONF("\n LGW_HAL_ERROR: %d \n", result );         
                                            }

                                            /* 删除队列首个元素 */
                                            if ( QueueIsEmpty(&line)) {

                                                    DEBUG_CONF("Nothing to list");
                                            } else {

                                                    DeQueue(&temp,&line);
                                                    DEBUG_CONF("detele this queue\n");
                                            }  
                                            
                                            continue;              
                                    }       
                            } while (status_var != TX_FREE);
                                DEBUG_CONF("\n ============================================== Confrim Data Send OK!!!=================================================\n");           
                        }

                        /* clear */
                        freq = 0;
                        bandwidth = 0;
                        datarate = 0;
                        coderate = 0;
                        size = 0;
                        tx_count_us = 0;
                        memset(payload,0,500);
                        memset(down_buff,0,500);
                        memset(nwkskey,0,16);
                        memset(appskey,0,16);
                        memset(encrypt_buff,0,500); 


                
                } 
                else /* 服务器有数据应答，发送数据给节点 */    
                {

                            /* 删除队列首个元素 */
                            if ( Data_QueueIsEmpty( &data_line ) ){

                                    DEBUG_CONF("queue is empty");

                            } else {
                                
                                    Data_DeQueue(&temp,&data_line);
                                    DEBUG_CONF("detele data queue\n");
                            }

                            tx_buff.freq_hz    = down_frequency;
                            tx_buff.bandwidth  = bandwidth;
                            tx_buff.datarate   = down_datarate;
                            tx_buff.coderate   = coderate;
                            tx_buff.size       = payload_len + 13;
                            tx_buff.tx_mode    = TIMESTAMPED; 
                            tx_buff.rf_chain   = TX_RF_CHAIN;
                            tx_buff.rf_power   = pow; 
                            tx_buff.modulation = MOD_LORA;
                            tx_buff.invert_pol = invert;
                            tx_buff.preamble   = preamb;
                            tx_buff.count_us   = tx_count_us;

                            /* 改为下行不应答类型 */
                            macHdr.Bits.MType  = FRAME_TYPE_DATA_UNCONFIRMED_DOWN;        
                            tx_buff.payload[0] = node_payload[0];

                            //修改下行数据类型
                            tx_buff.payload[0] = ((uint8_t)((3<<tx_buff.payload[0])>>3) | (uint8_t)(macHdr.Bits.MType <<5)); 
                    
                            /* devaddr */
                            tx_buff.payload[1] =  node_payload[1];
                            tx_buff.payload[2] =  node_payload[2];
                            tx_buff.payload[3] =  node_payload[3];
                            tx_buff.payload[4] =  node_payload[4];
                            
                            address            =  node_payload[1];
                            address           |= (node_payload[2]<<8);
                            address           |= (node_payload[3]<<16);
                            address           |= (node_payload[4]<<24);          
                    
                            /* 使能应答位 */
                            fCtrl.Value = node_payload[5];

                            if ( macHdr_temp.Bits.MType == 0x04 ) {

                                    down_fCtrl.DownBits.Ack = 1;

                            } else if ( macHdr_temp.Bits.MType == 0x02 ) {

                                    down_fCtrl.DownBits.Ack = 0;

                            } else {

                                    DEBUG_CONF("MType error!\n");
                            }   

                            down_fCtrl.DownBits.Adr 	 =  fCtrl.Bits.Adr;
                            down_fCtrl.DownBits.FOptsLen =  fCtrl.Bits.FOptsLen;
                            down_fCtrl.DownBits.RFU 	 =  0x0;
                            down_fCtrl.DownBits.FPending =  0x0;

                            tx_buff.payload[5]           =  down_fCtrl.Value;

                            /* 读取节点对应的fcnt_down */
                            
                            if ( 0 == Fetch_GWinfo_FcntDown ( db, &address, &fcnt_down ) ){

                                    DEBUG_CONF("fetch fcntdown error!\n");    
                                    continue;
                            } 

                            /*  fcnt +1 */
                            fcnt_down = fcnt_down +1;  
                            
                            /* 更新该节点的fcntdown到本地数据库 */
                            if ( 0 == Update_GWinfo_FcntDown( db,zErrMsg,&address,&fcnt_down ) ){

                                    DEBUG_CONF("update fcntdown error!\n");    
                                    continue;
                            }  
            
                            tx_buff.payload[6] = (uint8_t)fcnt_down;
                            tx_buff.payload[7] = (uint8_t)(fcnt_down>>8);
                            tx_buff.payload[8] = node_payload[8];
                            
                            /* FRMpayload */
                            /* 拷贝服务器下发的数据 */
                            mymemcpy(tx_buff.payload+9,payload,payload_len); 
                    
                            if ( 0 == Fetch_Nwkskey_Appskey_Table (db,&address,nwkskey,appskey ) ){

                                    DEBUG_CONF("Fetch Nwkskey and Appskey error!\n");    
                                    continue;
                            }

                            /* 载荷加密 */         
                            LoRaMacPayloadEncrypt(tx_buff.payload+9,payload_len, appskey, address, DOWN_LINK,fcnt_down, encrypt_buff);
                            
                            /* 加密后的载荷再拷贝到发送的载荷中 */   
                            mymemcpy(tx_buff.payload+9,encrypt_buff,payload_len);
                            
                            /* 计算MIC */
                            LoRaMacComputeMic(tx_buff.payload,payload_len+9,nwkskey, address, DOWN_LINK,fcnt_down, &mic_gateway);
                            tx_buff.payload[payload_len+9]  = (uint8_t)mic_gateway;
                            tx_buff.payload[payload_len+10] = (uint8_t)(mic_gateway >>8);
                            tx_buff.payload[payload_len+11] = (uint8_t)(mic_gateway >>16);
                            tx_buff.payload[payload_len+12] = (uint8_t)(mic_gateway >>24);        
                            
                            gettimeofday(&start_send,NULL); 
                            //下发数据
                            result=lgw_send(tx_buff); /* non-blocking scheduling of TX packet */
                            gettimeofday(&end,NULL);
                            gettimeofday(&end_send,NULL);
                            timer = (end.tv_sec - start.tv_sec)*1000000 + (end.tv_usec - start.tv_usec); 
                            timer_send = (end_send.tv_sec - start_send.tv_sec)*1000000 + (end_send.tv_usec - start_send.tv_usec); 
                            DEBUG_CONF("\nhandle data task is:= %lu us\n",timer);
                            DEBUG_CONF("\nsend   data time is:= %lu us\n",timer_send);

                            /* 发送结果检查 */
                            if ( result == LGW_HAL_ERROR) {

                                    DEBUG_CONF("Join data send error\n");    
                        
                            }else if (result == LGW_LBT_ISSUE ){

                                    DEBUG_CONF("Failed: Not allowed (LBT)\n");

                            } else {

                                     
                                    
                                    /* wait for packet to finish sending */
                                    int loop = 0;
                                    int scheduled_counter = 0;
                                    do 
                                    {      loop+=1;
                                            
                                            wait_ms(5);
                                            lgw_status(TX_STATUS, &status_var); /* get TX status */
                                           

                                            /* debug */
                                            //DEBUG_CONF("\n  loop: %d get lgw_send status: %s\n", loop,lgw_send_status[status_var]);

                                            if (status_var == TX_SCHEDULED) {
                                                    
                                                    scheduled_counter+=1;
                                                    /* debug */
                                                   //DEBUG_CONF("\nscheduled_counter: %d\n", scheduled_counter );
                                            }
                                            
                                            /***
                                             * lgw_send()发生错误处理机制 
                                             * 当lgw_send()的状态一直在TX_SCHEDULED状态超时后，则默认发送失败
                                             * 流产该次发送，后续数据会在 sx1301发送缓存区覆盖该次数据
                                             * 超时时间计算： 默认延时时间 ( 5s ) - 出队时间 ( 4.8s ) = 200ms
                                             *  5 * 40 = 200 ms 
                                             * 
                                             *  update:经实际测试：发送一包数据需要经过三个状态：
                                             *          TX_SHEDULED ----> TX_EMITTING  ----> TX-FREE
                                             * 
                                             *         
                                             *              
                                             *         
                                             *         
                                             * 
                                             */

                                            /* tx 错误处理机制  */
                                            if ( scheduled_counter >= 55) 
                                            {

                                                    /* 中止发生 */      
                                                    result = lgw_abort_tx();
                                                    DEBUG_CONF("\n=============================== Payload TX_SCHEDULED is overtime! Abort tx packet!===================================================\n");
                                                    if ( result == LGW_HAL_SUCCESS ) {

                                                            DEBUG_CONF("\n LGW_HAL_SUCCESS: %d \n", result );

                                                    } else {

                                                            DEBUG_CONF("\n LGW_HAL_ERROR: %d \n", result );         
                                                    }

                                                    /* 删除队列首个元素 */
                                                    if ( QueueIsEmpty(&line)) {

                                                            DEBUG_CONF("Nothing to list");

                                                    } else {

                                                            DeQueue(&temp,&line);
                                                            DEBUG_CONF("detele this queue\n");
                                                    }  
                                                    
                                                    continue;              
                                            }       
                                    } while (status_var != TX_FREE);
                                         
                                    DEBUG_CONF("\n ============================================== Confrim Data Send OK!!!=================================================\n");           


                                }
                        
                            /* clear */
                            freq = 0;
                            bandwidth = 0;
                            datarate = 0;
                            coderate = 0;
                            size = 0;
                            tx_count_us = 0;
                            memset(payload,0,500);
                            memset(down_buff,0,500);
                            memset(nwkskey,0,16);
                            memset(appskey,0,16);
                            memset(encrypt_buff,0,500); 

                }
        }

         /* exit loop on user signals */
		if ((quit_sig == 1) || (exit_sig == 1)) 
		{
			pthread_exit(0);
            break;
		}
    }    
}

/**
 * 线程函数      将服务下发的数据进行存储到队列中
 * 
 * brief     ： 将节点上报的需要应答的数据进行应答处理
 * 
 * parameter ： NULL
 * 
 * return    ： NULL
 * 
 */
void *Thread_Data_Store_Task()
{
   int sock_fd;
   struct sockaddr_in server_addr;
   struct sockaddr_in client_addr;
   int ret;
   socklen_t addr_len;
   int recv_len;
   uint8_t recv_buff[1000];
   memset(recv_buff,0,1000);
   Data data;
   memset(&data,0,sizeof(Data));
   int Reusraddr    = 1;

    /**
     * creat socket
     * 
     * AF_INET:    ipv4
     * 
     * SOCK_DGRAM: udp
     * 
     */  

   sock_fd = socket(AF_INET, SOCK_DGRAM, 0);//
   
   if ( -1 == sock_fd ){

            DEBUG_CONF("creat socket error:%s\n\a", strerror(errno));
            close(sock_fd);
    }

   /* set sockaddr_in parameter*/
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family      =  AF_INET;

    /* INADDR_ANY:This machine all IP */
    server_addr.sin_addr.s_addr =  inet_addr("127.0.0.1");

    /* 5557用于监听服务器应答数据的下发 */
    server_addr.sin_port        = htons(5557);  

    /* set REUSEADDR properties address reuse */
    ret = setsockopt(sock_fd,SOL_SOCKET,SO_REUSEADDR,&Reusraddr,sizeof(int));
    if ( ret != 0 ) {

            DEBUG_CONF("setsocketopt reuseaddr fail,ret:%d,error:%d\n",ret,errno);
            close(sock_fd);
    
    } 
    /* bind */
    ret = bind(sock_fd, (struct sockaddr *)(&server_addr), sizeof(struct sockaddr));
    
    if ( -1 == ret ){

            DEBUG_CONF("bind socket error:%s\n\a", strerror(errno));
            close(sock_fd);
    }

    while(1)
    {
        /* recvfrom */
        addr_len = sizeof(struct sockaddr);
        recv_len = recvfrom(sock_fd, recv_buff, 1000, 0, (struct sockaddr *)&client_addr, &addr_len);
        
        if (recv_len <= 0) {

                DEBUG_CONF("recvfrom error:%s\n\a", strerror(errno));
                close(sock_fd);    
                continue;

        } else { 

                /** 
                 * 接收到数据后将数据提取出来
                 * 
                 * 进行入队操作
                 *
                 */
            
                data.size = recv_len;
                mymemcpy(data.payload,recv_buff,data.size);
                Data_EnQueue(data,&data_line);
                memset(&data,0,sizeof(Data));
                memset(recv_buff,0,1000);

        }   
    }                
}


/**
 * 线程函数      class a 调试数据存储线程 
 * 
 * brief     ： 将class a 的调试数据进行存储到队列中
 * 
 * parameter ： NULL
 * 
 * return    ： NULL
 * 
 */
void *Thread_classA_Data_Store_Task()
{

            int sock_fd;
            struct sockaddr_in server_addr;
            struct sockaddr_in client_addr;
            int ret;
            socklen_t addr_len;
            int recv_len;
            uint8_t recv_buff[1024];
            memset(recv_buff,0,1024);
            int Reusraddr    = 1;
            class_A     class_a_data;
            memset(&class_a_data,0,sizeof(class_a_data));

            /**
             * creat socket
             * 
             * AF_INET:    ipv4
             * 
             * SOCK_DGRAM: udp
             * 
             */  

            sock_fd = socket(AF_INET, SOCK_DGRAM, 0);//
            
            if ( -1 == sock_fd ){

                        DEBUG_CONF("creat socket error:%s\n\a", strerror(errno));
                        close(sock_fd);
            }

            /* set sockaddr_in parameter*/
            memset(&server_addr, 0, sizeof(struct sockaddr_in));
            server_addr.sin_family      =  AF_INET;

            /* INADDR_ANY:This machine all IP */
            server_addr.sin_addr.s_addr =  inet_addr("127.0.0.1");

            /* 5559用于监听class a调试数据的下发 */
            server_addr.sin_port        = htons(5560);  

            /* set REUSEADDR properties address reuse */
            ret = setsockopt(sock_fd,SOL_SOCKET,SO_REUSEADDR,&Reusraddr,sizeof(int));
            if ( ret != 0 ) {

                    DEBUG_CONF("setsocketopt reuseaddr fail,ret:%d,error:%d\n",ret,errno);
                    close(sock_fd);
            
            } 
            /* bind */
            ret = bind(sock_fd, (struct sockaddr *)(&server_addr), sizeof(struct sockaddr));
            
            if ( -1 == ret ){

                    DEBUG_CONF("bind socket error:%s\n\a", strerror(errno));
                    close(sock_fd);
            }

            while(1)
            {
                    
                    /* recvfrom */
                    addr_len = sizeof(struct sockaddr);
                    recv_len = recvfrom(sock_fd, recv_buff, 1024, 0, (struct sockaddr *)&client_addr, &addr_len);
                    
                    if (recv_len <= 0) {

                            DEBUG_CONF("recvfrom error:%s\n\a", strerror(errno));
                            close(sock_fd);    
                            continue;

                    } else { 

                            /* 
                                接收到数据后将数据提取出来 
                                进行入队操作

                             */
                            DEBUG_CONF("recv_len: %d\n",recv_len);
                            DEBUG_CONF("/*-------------------------test-------------------------------------------*/\n");

                            for ( int i = 0; i < recv_len; i++)
                                    DEBUG_CONF("recv_buff[%d]:0x%02x\n",i,recv_buff[i]);
                     
                            class_a_data.value_len  = recv_len - 8;
                            mymemcpy (class_a_data.deveui,   recv_buff,    8);
                            mymemcpy (class_a_data.valueBuf, recv_buff+8,  class_a_data.value_len);

                            if ( false == ClassA_EnQueue(class_a_data, &class_a_line))
                                    DEBUG_CONF("insert class a data queue is error!\n");
                            
                            memset(&class_a_data,0,sizeof(class_A));
                            memset(recv_buff,0,1024);

                    }   
            
            }    

}


/*
    update:2018.12.21   
    breif:     处理class c数据类型
    parameter: 上行频率
    return:    下行频率
*/
void *Thread_Handle_Class_C_Task(void)
{
    uint16_t cycle_count;
    int repeat = -1; 
    int sock_fd;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    int ret;
    socklen_t addr_len;
    int recv_len;
    uint8_t recv_buff[1024];
    memset(recv_buff,0,1024);
    static uint8_t deveui_buf[8];
    memset(deveui_buf,0,8);
    uint32_t rx2_freq_temp;
    uint32_t devaddr_temp;
    int  pow    = 20;
    bool invert = true;
    int  preamb = 8;
    uint8_t status_var;
    uint32_t mic_gateway;
    int Reusraddr    = 1;

    static uint32_t fcnt_down;
    uint8_t encrypt_buff[1024];
    memset(encrypt_buff,0,1024);
    uint8_t nwkskey[16];
    uint8_t appskey[16];
    memset(nwkskey,0,16);
    memset(appskey,0,16);
    int payload_len;
    int y;
    /* 判断是否超过最大载荷数N值 */
    bool SubpackageIsOk = false;   
    int  packet_sum;
    int  packet_id;
    int  last_packet;
    /* class c 固定为rx2时，datarate固定为sf12 */
    uint8_t max_payload = 51;

    /* 收到class c数据，立即进行下发操作 */
    struct lgw_pkt_tx_s tx_buff;

    uint8_t *p = NULL;
    
    /* init */
    packet_sum  = 0;
    packet_id   = 0;
    last_packet = 0;
    memset(&tx_buff,0,sizeof(tx_buff));
    memset(&server_addr,0,sizeof(struct sockaddr_in));

    LoRaMacHeader_t mHdr;
    memset(&mHdr,0,sizeof(LoRaMacHeader_t));
    
    sock_fd = socket(AF_INET,SOCK_DGRAM,0); /* AF_INET:ipv4;SOCK_DGRAM:udp */
    if ( -1 == sock_fd){

            DEBUG_CONF("socket error:%s\n",strerror(errno));
            close(sock_fd);
    }

    /* set sockaddr_in parameter */
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    /* 规定：5558端口用于接收服务器下发class c 数据下发 */
    server_addr.sin_port   = htons(5558); 
    
    /* set REUSEADDR properties address reuse */
    ret = setsockopt(sock_fd,SOL_SOCKET,SO_REUSEADDR,&Reusraddr,sizeof(int));
    if ( ret != 0){

            DEBUG_CONF("setsocketopt reuseaddr fail,ret:%d,error:%d\n",ret,errno);
            close(sock_fd);
    }

    /* bind */
    ret = bind(sock_fd,(struct sockaddr*)(&server_addr),sizeof(struct sockaddr) );
    if ( -1 == ret)
    {
            DEBUG_CONF("bind error:%s\n",strerror(errno));
            close(sock_fd);
    }
    
    while ( (repeat == -1) || (cycle_count < repeat))
    {
            cycle_count++;

            /* recvfrom */
            addr_len = sizeof(struct sockaddr);
            recv_len = recvfrom(sock_fd,recv_buff,1024,0,(struct sockaddr*)&client_addr,&addr_len);
            
            if ( recv_len <= 0)
            {
                    DEBUG_CONF("recv_from error:%s\n",strerror(errno));
                    close(sock_fd);
            }
            else
            {       
                   /* 9bytes: mtype(1bytes) + deveui(8bytes) */
                    payload_len = recv_len - 9;

                    if ( payload_len > max_payload ) {
                            
                            SubpackageIsOk = true;

                            if ( payload_len % max_payload == 0 ) {
                                
                                        packet_sum  =   payload_len / max_payload;
                                        /* 最后一包字节数*/
                                        last_packet =   max_payload; 

                            }else{
                                        packet_sum  =   (payload_len / max_payload) + 1;       
                                        last_packet =   payload_len - ( (packet_sum-1) * max_payload );
                            }   
                    
                    } else{

                            SubpackageIsOk = false;
                    }
                    
                    /* copy deveui */
                    mymemcpy(deveui_buf,recv_buff+1,8);  
    
                    /* 进行下发数据包的封装 */        
                    if ( 0 == Fetch_Rx2_Freq(db,deveui_buf,&rx2_freq_temp,&devaddr_temp)) {
                            
                            DEBUG_CONF("fetch rx2_freq error!\n");
                            continue;
                    }
                    
                    
                    if ( !SubpackageIsOk) 
                    {
                            tx_buff.freq_hz    = rx2_freq_temp;
                            tx_buff.tx_mode    = IMMEDIATE;
                            tx_buff.rf_chain   = TX_RF_CHAIN;
                            tx_buff.rf_power   = pow; 
                            tx_buff.modulation = MOD_LORA;
                            tx_buff.bandwidth  = 0x03; /* 125khz */
                            /* Rx2 固定freq_hz和datarate 具体参见<<  LoRaWan v1.0.2 RegionParameter >> Page-35 */
                            tx_buff.datarate   = 0x40; 
                            tx_buff.coderate   = 0x01;            
                            tx_buff.invert_pol = invert;
                            tx_buff.preamble   = preamb;
                            tx_buff.no_crc     = true;

                            /* mType */
                            mHdr.Bits.MType    = recv_buff[0];
                            tx_buff.payload[0] = mHdr.Value;
                            /* devaddr */
                            tx_buff.payload[1] = (uint8_t)(devaddr_temp);
                            tx_buff.payload[2] = (uint8_t)(devaddr_temp>>8);
                            tx_buff.payload[3] = (uint8_t)(devaddr_temp>>16);
                            tx_buff.payload[4] = (uint8_t)(devaddr_temp>>24);
                            /* fctrl */
                            tx_buff.payload[5] = 0x80;
                            
                            /* fcnt_down */
                            if ( 0 == Fetch_GWinfo_FcntDown(db,&devaddr_temp,&fcnt_down)) {

                                    DEBUG_CONF("fetch fcntdown error!\n");    
                                    continue;
                            }
                            
                            fcnt_down = fcnt_down +1;
                            tx_buff.payload[6] = (uint8_t)fcnt_down;
                            tx_buff.payload[7] = (uint8_t)(fcnt_down>>8);
                            
                            /* 更新该节点的fcntdown到本地数据库 */
                            if ( 0 == Update_GWinfo_FcntDown(db,zErrMsg,&devaddr_temp,&fcnt_down)){

                                    DEBUG_CONF("update fcntdown error!\n");    
                                    continue;
                            }
                            
                            tx_buff.payload[6] = (uint8_t)fcnt_down;
                            tx_buff.payload[7] = (uint8_t)(fcnt_down>>8);
                            tx_buff.payload[8] = 0x0a; /* 区分应用类型使用，可以固定 */ 
                            
                            /* 
                                frampayload
                                拷贝服务器下发的载荷数据
                            */
                            mymemcpy(tx_buff.payload+9,recv_buff+9,payload_len);
                            
                            if ( 0 == Fetch_Nwkskey_Appskey_Table(db,&devaddr_temp,nwkskey,appskey)){

                                    DEBUG_CONF("Fetch Nwkskey and Appskey error!\n");    
                                    continue;
                            }
                            
                            /* 载荷加密 */    
                            LoRaMacPayloadEncrypt(tx_buff.payload+9,payload_len, appskey, devaddr_temp, DOWN_LINK,fcnt_down, encrypt_buff);
                            /* 加密后的载荷再拷贝到发送的载荷中 */   
                            mymemcpy(tx_buff.payload+9,encrypt_buff,payload_len);            
                            /* 计算mic */
                            LoRaMacComputeMic(tx_buff.payload,payload_len+9,nwkskey, devaddr_temp, DOWN_LINK,fcnt_down, &mic_gateway);
                            
                            tx_buff.payload[payload_len+9]  = (uint8_t)mic_gateway;
                            tx_buff.payload[payload_len+10] = (uint8_t)(mic_gateway >>8);
                            tx_buff.payload[payload_len+11] = (uint8_t)(mic_gateway >>16);
                            tx_buff.payload[payload_len+12] = (uint8_t)(mic_gateway >>24); 
                            tx_buff.size       = payload_len + 13 ;    
                            
                            y=lgw_send(tx_buff); /* non-blocking scheduling of TX packet */
                            if ( y == LGW_HAL_ERROR) 
                            {
                                    DEBUG_CONF("Node data send error!\n");
                            } 	
                            else if ( y == LGW_LBT_ISSUE ) 
                            {
                                    DEBUG_CONF("Failed: Not allowed (LBT)\n");
                            } 
                            else 
                            {
                                    /* wait for packet to finish sending */
                                    do 
                                    {
                                        wait_ms(5);
                                        lgw_status(TX_STATUS, &status_var); /* get TX status */
                                    } 
                                    while (status_var != TX_FREE);
                                    DEBUG_CONF("\nLORAWAN Downlink Packet Send OK!\n");
                            }

                            /* clear */
                            memset(recv_buff,0,1024);
                            recv_len = 0;
                            payload_len = 0;
                            memset(nwkskey,0,16);
                            memset(appskey,0,16);
                            memset(encrypt_buff,0,1024);
                    }
                    else
                    {
                           for ( packet_id = 1; packet_id <= packet_sum; packet_id++)
                           {

                                    tx_buff.freq_hz    = rx2_freq_temp;
                                    tx_buff.tx_mode    = IMMEDIATE;
                                    tx_buff.rf_chain   = TX_RF_CHAIN;
                                    tx_buff.rf_power   = pow; 
                                    tx_buff.modulation = MOD_LORA;
                                    tx_buff.bandwidth  = 0x03; /* 125khz */
                                    /* Rx2 固定freq_hz和datarate 具体参见<<  LoRaWan v1.0.2 RegionParameter >> Page-35 */
                                    tx_buff.datarate   = 0x40; 
                                    tx_buff.coderate   = 0x01;            
                                    tx_buff.invert_pol = invert;
                                    tx_buff.preamble   = preamb;
                                    tx_buff.no_crc     = true;

                                    /* mType */
                                    mHdr.Bits.MType    = recv_buff[0];
                                    tx_buff.payload[0] = mHdr.Value;
                                    /* devaddr */
                                    tx_buff.payload[1] = (uint8_t)(devaddr_temp);
                                    tx_buff.payload[2] = (uint8_t)(devaddr_temp>>8);
                                    tx_buff.payload[3] = (uint8_t)(devaddr_temp>>16);
                                    tx_buff.payload[4] = (uint8_t)(devaddr_temp>>24);
                                    /* fctrl */
                                    tx_buff.payload[5] = 0x80;

                                    /* fcnt_down */
                                    if ( 0 == Fetch_GWinfo_FcntDown(db,&devaddr_temp,&fcnt_down)) {

                                            DEBUG_CONF("fetch fcntdown error!\n");    
                                            continue;
                                    }

                                    fcnt_down = fcnt_down +1;
                                    tx_buff.payload[6] = (uint8_t)fcnt_down;
                                    tx_buff.payload[7] = (uint8_t)(fcnt_down>>8);

                                    /* 更新该节点的fcntdown到本地数据库 */
                                    if ( 0 == Update_GWinfo_FcntDown(db,zErrMsg,&devaddr_temp,&fcnt_down)){

                                            DEBUG_CONF("update fcntdown error!\n");    
                                            continue;
                                    }

                                    tx_buff.payload[6] = (uint8_t)fcnt_down;
                                    tx_buff.payload[7] = (uint8_t)(fcnt_down>>8);
                                    tx_buff.payload[8] = 0x0a; /* 区分应用类型使用，可以固定 */ 

                                    if ( 0 == Fetch_Nwkskey_Appskey_Table(db,&devaddr_temp,nwkskey,appskey)){

                                            DEBUG_CONF("Fetch Nwkskey and Appskey error!\n");    
                                            continue;
                                    }

                                    /* 
                                        frampayload
                                        拷贝服务器下发的载荷数据
                                    */

                                    /* 不是最后一包数据 */    
                                    if ( packet_id != packet_sum) {
                                        
                                            p = recv_buff + 9 + ( (packet_id -1) * max_payload );
                                            mymemcpy(tx_buff.payload+9, p, max_payload);

                                            
                                            /* 载荷加密 */    
                                            LoRaMacPayloadEncrypt(tx_buff.payload+9,max_payload, appskey, devaddr_temp, DOWN_LINK,fcnt_down, encrypt_buff);
                                            /* 加密后的载荷再拷贝到发送的载荷中 */   
                                            mymemcpy(tx_buff.payload+9,encrypt_buff,max_payload);            
                                            /* 计算mic */
                                            LoRaMacComputeMic(tx_buff.payload,max_payload+9,nwkskey, devaddr_temp, DOWN_LINK,fcnt_down, &mic_gateway);

                                            tx_buff.payload[max_payload+9]  = (uint8_t) mic_gateway;
                                            tx_buff.payload[max_payload+10] = (uint8_t)(mic_gateway >>8);
                                            tx_buff.payload[max_payload+11] = (uint8_t)(mic_gateway >>16);
                                            tx_buff.payload[max_payload+12] = (uint8_t)(mic_gateway >>24); 
                                            tx_buff.size       = max_payload + 13 ;  

                                    } else {
                                    
                                            p = recv_buff + 9 + ( (packet_id -1) * max_payload );
                                            mymemcpy(tx_buff.payload+9, p, last_packet);

                                             /* 载荷加密 */    
                                            LoRaMacPayloadEncrypt(tx_buff.payload+9,last_packet, appskey, devaddr_temp, DOWN_LINK,fcnt_down, encrypt_buff);
                                            /* 加密后的载荷再拷贝到发送的载荷中 */   
                                            mymemcpy(tx_buff.payload+9,encrypt_buff,last_packet);            
                                            /* 计算mic */
                                            LoRaMacComputeMic(tx_buff.payload,last_packet+9,nwkskey, devaddr_temp, DOWN_LINK,fcnt_down, &mic_gateway);

                                            tx_buff.payload[last_packet+9]  = (uint8_t) mic_gateway;
                                            tx_buff.payload[last_packet+10] = (uint8_t)(mic_gateway >>8);
                                            tx_buff.payload[last_packet+11] = (uint8_t)(mic_gateway >>16);
                                            tx_buff.payload[last_packet+12] = (uint8_t)(mic_gateway >>24); 
                                            tx_buff.size       = last_packet + 13 ;  

                                    }
                                    
                                    y=lgw_send(tx_buff); /* non-blocking scheduling of TX packet */
                                    
                                    if ( y == LGW_HAL_ERROR) 
                                    {
                                            DEBUG_CONF("Node data send error!\n");
                                    } 	
                                    else if ( y == LGW_LBT_ISSUE ) 
                                    {
                                            DEBUG_CONF("Failed: Not allowed (LBT)\n");
                                    } 
                                    else 
                                    {
                                            /* wait for packet to finish sending */
                                            do 
                                            {
                                                wait_ms(5);
                                                lgw_status(TX_STATUS, &status_var); /* get TX status */
                                            } 
                                            while (status_var != TX_FREE);
                                                    DEBUG_CONF("\nLORAWAN Downlink Packet Send OK!\n");
                                    }
                                    
                           }
                      
                    }
                    
                    /* exit loop on user signals */
                    if ((quit_sig == 1) || (exit_sig == 1)) 
                    {
                        pthread_exit(0);
                        break;
                    }

            }        
    }
           
}
/*
    update:2018.12.21   
    breif:     CN470-510异频下发函数 
    parameter: 上行频率
    return:    下行频率
*/
uint32_t RegionCN470AsynchronyTxFrequency(uint32_t frequency)
{
    uint8_t  Channel;
    uint32_t DownLinkFreq;
    
    //计算上行频率所在的信道
    Channel = ( (frequency - CN470_FIRST_UpLink_Frequency) / CN470_STEPWIDTH_Frequency );

    //计算下行接收频率
    DownLinkFreq = CN470_FIRST_DownLink_Frequency + ( (Channel % 48) *  CN470_STEPWIDTH_Frequency );

    return DownLinkFreq;    
}

#if 0
//异频下发的datarate
uint8_t RegionCN470ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    int8_t datarate = dr - drOffset;

    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    return datarate;
}
#endif


/* update: 2019.3.12 */
/**
 *  增加计算 TOA 时间 
 *  参考官网代码: lora_gateway-master/loragw_hal.c/ lgw_time_on_air()
 *  parme[in]: 接受数据包结构体指针
 *  return:    toa时间 us
 */
uint32_t rx_lgw_time_on_air(struct lgw_pkt_rx_s *packet)
{
        int32_t val;
        uint8_t SF, H, DE;
        uint16_t BW;
        uint32_t payloadSymbNb, Tpacket;
        double Tsym, Tpreamble, Tpayload, Tfsk;

        if (packet == NULL) {
            DEBUG_CONF("ERROR: Failed to compute time on air, wrong parameter\n");
            return 0;
        }

        if (packet->modulation == MOD_LORA) {
            /* Get bandwidth */
            val = lgw_bw_getval(packet->bandwidth);
            if (val != -1) {
                BW = (uint16_t)(val / 1E3);
            } else {
                DEBUG_CONF("ERROR: Cannot compute time on air for this packet, unsupported bandwidth (0x%02X)\n", packet->bandwidth);
                return 0;
            }

            /* Get datarate */
            val = lgw_sf_getval(packet->datarate);
            if (val != -1) {
                SF = (uint8_t)val;
            } else {
                DEBUG_CONF("ERROR: Cannot compute time on air for this packet, unsupported datarate (0x%02X)\n", packet->datarate);
                return 0;
            }

            /* Duration of 1 symbol */
            Tsym = pow(2, SF) / BW;

            /* Duration of preamble */
            /* 接收结构体成员不含前导码长度  hub 固定length为8 */
            Tpreamble = ((double)( 8 ) + 4.25) * Tsym;

            /* Duration of payload */
            // H = (packet->no_header==false) ? 0 : 1; /* header is always enabled, except for beacons */
           
           /*  packet->no_header hub设置为 false */
           
            H = ( 0 ==false) ? 0 : 1;
            
            DE = (SF >= 11) ? 1 : 0; /* Low datarate optimization enabled for SF11 and SF12 */

            payloadSymbNb = 8 + (ceil((double)(8*packet->size - 4*SF + 28 + 16 - 20*H) / (double)(4*(SF - 2*DE))) * (packet->coderate + 4)); /* Explicitely cast to double to keep precision of the division */

            Tpayload = payloadSymbNb * Tsym;

            /* Duration of packet */
            Tpacket = Tpreamble + Tpayload;
        }  else {
            Tpacket = 0;
            DEBUG_CONF("ERROR: Cannot compute time on air for this packet, unsupported modulation (0x%02X)\n", packet->modulation);
        }

        return Tpacket;
}


/*
    @ update    ： 2019.06.14
    @ brief     ： 下发class a 的调试数据
    @ parameter ： sqlite3数据库、上行频率、带宽、速率、下发时间、
                   节点信息载荷、队列项数、第几包，class a调试载荷、
                   class a调试载荷大小

    @comment    :  下发的class a的数据包均为unconfirmed 类型   

*/
void    SendClassADebugData ( 
                                sqlite3  *db, 
                                uint32_t *uplinkfreq, 
                                uint8_t  *bandwidth,
                                uint32_t *datarate, 
                                uint8_t  *coderate, 
                                uint32_t *tx_count_us,
                                uint8_t  *node_payload, 
                                int      *items, 
                                int      *pakageid,
                                uint8_t  *classa_payload,
                                uint16_t *value_len   
                                
                            )

{

        /* 分包标志位 */
        bool Multiple_Packets_Flag = false;
        
        /* 区域配置变量 */
        uint32_t down_frequency;
        uint8_t  region;
        uint8_t  rx1droffset;
        uint32_t down_datarate;

        /* class 队列临时项 */
        class_A     node_temp;

        uint32_t    address;
        uint32_t    fcnt_down;
        uint8_t     nwkskey[16];
        uint8_t     appskey[16];

        /* 载荷加密缓存 */
        uint8_t encrypt_buff[500];

        uint32_t mic_gateway;

        LoRaMacHeader_t	        macHdr;
        LoRaMacHeader_t	        macHdr_temp;
        LoRaMacFrameDownCtrl_t  down_fCtrl;
        LoRaMacFrameCtrl_t      fCtrl;
        
        /* txpkt 结构体 */
        struct  lgw_pkt_tx_s  tx_buff;
        
        /* txpkt 参数配置 */
        int         pow    = 20;
        bool        invert = true;
        int         preamb = 8;	
        uint8_t     status_var;
        int         result;
   
        /* init */
        memset(&macHdr,      0, sizeof( LoRaMacHeader_t ));
        memset(&macHdr_temp, 0, sizeof( LoRaMacHeader_t ) );
        memset(&down_fCtrl,  0, sizeof( LoRaMacFrameDownCtrl_t ));
        memset(&fCtrl,       0, sizeof( LoRaMacFrameDownCtrl_t ));

        /* 由上行频率取出下行频率 */
        if ( 0 == Fetch_DownLink_Freq_Info (db, uplinkfreq, &down_frequency ))
                DEBUG_CONF("fetch down frequency fail!\n");                
      
        /* 取出区域信息 */
        if ( 0 == Fetch_Region_Info (db, &region) ) 
                DEBUG_CONF("fetch region info fail!\n");
        
        /* 取出rx1droffset 信息 */
        if ( 0 == Fetch_RX1DRoffset_Info (db, &rx1droffset) ) {
            
                DEBUG_CONF("fetch rx1droffset info fail!\n");
        }
                
        /* 由region+上行datarate+rx1offset取下行datarate */
        down_datarate = RegionApplyDrOffset (region, 0, *datarate, rx1droffset);
        
        if (  *items > 1) {
        
                Multiple_Packets_Flag = true;

        }else{

                Multiple_Packets_Flag = false;
        }


        if ( Multiple_Packets_Flag) 
        {

                DEBUG_CONF("---------*items:    %d---------\n",    *items);
                DEBUG_CONF("---------*pakageid: %d---------\n", *pakageid);

                Multiple_Packets_Flag = false;
                macHdr.Value          = node_payload[0];
                macHdr_temp.Value     = node_payload[0];

               /* 删除class a队列中首个节点  */                
                if ( ClassA_QueueIsEmpty( &class_a_line)){

                        DEBUG_CONF("queue is empty");

                } else {
                    
                        ClassA_DeQueue(&node_temp, &class_a_line);
                        DEBUG_CONF("detele data queue\n");
                }
                
                tx_buff.freq_hz    =  down_frequency;
                tx_buff.bandwidth  =  *bandwidth;
                tx_buff.datarate   =  down_datarate;
                tx_buff.coderate   =  *coderate;
                tx_buff.size       = (*value_len) + 13;
                tx_buff.tx_mode    = TIMESTAMPED; 
                tx_buff.rf_chain   = TX_RF_CHAIN;
                tx_buff.rf_power   = pow; 
                tx_buff.modulation = MOD_LORA;
                tx_buff.invert_pol = invert;
                tx_buff.preamble   = preamb;
                tx_buff.count_us   = *tx_count_us;

                /* 改为下行不应答类型 */
                macHdr.Bits.MType  = FRAME_TYPE_DATA_UNCONFIRMED_DOWN;        
                tx_buff.payload[0] = node_payload[0];

                /* 修改下行数据类型 */
                tx_buff.payload[0] = ((uint8_t)((3<<tx_buff.payload[0])>>3) | (uint8_t)(macHdr.Bits.MType <<5)); 

                /* devaddr */
                tx_buff.payload[1] =  node_payload[1];
                tx_buff.payload[2] =  node_payload[2];
                tx_buff.payload[3] =  node_payload[3];
                tx_buff.payload[4] =  node_payload[4];

                address            =  node_payload[1];
                address           |= (node_payload[2]<<8);
                address           |= (node_payload[3]<<16);
                address           |= (node_payload[4]<<24);

                fCtrl.Value = node_payload[5];

                /* 判断应答位是否需要使能 */ 
                if ( macHdr_temp.Bits.MType == 0x04 ){

                        down_fCtrl.DownBits.Ack = 1;

                } else if ( macHdr_temp.Bits.MType == 0x02 ) {

                        down_fCtrl.DownBits.Ack = 0;

                } else {
                    
                        DEBUG_CONF("MType is error!\n");
                }

                down_fCtrl.DownBits.Adr 	 = fCtrl.Bits.Adr;
                down_fCtrl.DownBits.FOptsLen = fCtrl.Bits.FOptsLen;
                down_fCtrl.DownBits.RFU 	 = 0x0;

                /* 不是最后一包数据前都开启fpending位 */ 
                if (  *pakageid == *items)
                {
                        down_fCtrl.DownBits.FPending = 0x00;

                } else {
                        
                        down_fCtrl.DownBits.FPending = 0x01;
                }

                tx_buff.payload[5] = down_fCtrl.Value;    

                /* 读取节点对应的fcnt_down */
                if ( 0 == Fetch_GWinfo_FcntDown( db,&address,&fcnt_down ))
                        DEBUG_CONF("fetch fcntdown error!\n");
                
                /* +1 操作 */
                fcnt_down = fcnt_down +1;  

                /* 更新该节点的fcntdown到本地数据库 */
                if ( 0 == Update_GWinfo_FcntDown( db,zErrMsg,&address,&fcnt_down ))
                        DEBUG_CONF("update fcntdown error!\n");    
                
                tx_buff.payload[6] = (uint8_t)fcnt_down;
                tx_buff.payload[7] = (uint8_t)(fcnt_down>>8);
                /* 区分应用作用，默认0x0a */
                tx_buff.payload[8] = 0x0a;
                
                /* FRMpayload */
                /* 拷贝服务器下发的数据 */
                mymemcpy(tx_buff.payload+9,classa_payload,*value_len);

                if ( 0 == Fetch_Nwkskey_Appskey_Table (db,&address,nwkskey,appskey))
                            DEBUG_CONF("Fetch Nwkskey and Appskey error!\n");    
                
                /* 载荷加密 */         
                LoRaMacPayloadEncrypt ( tx_buff.payload+9, *value_len, appskey, address, DOWN_LINK,fcnt_down, encrypt_buff);
              
                /* 加密后的载荷再拷贝到发送的载荷中 */   
                mymemcpy ( tx_buff.payload+9, encrypt_buff, *value_len);  

                /* 计算mic */
                LoRaMacComputeMic(tx_buff.payload,(*value_len) + 9, nwkskey, address, DOWN_LINK,fcnt_down, &mic_gateway);
                tx_buff.payload[(*value_len)+9]  = (uint8_t) mic_gateway;
                tx_buff.payload[(*value_len)+10] = (uint8_t)(mic_gateway >>8);
                tx_buff.payload[(*value_len)+11] = (uint8_t)(mic_gateway >>16);
                tx_buff.payload[(*value_len)+12] = (uint8_t)(mic_gateway >>24);

                for ( int i = 0; i < tx_buff.size; i++ )
                            DEBUG_CONF ( "---------tx_buff.payload[%d]:0x%02x---------------\n",i, tx_buff.payload[i] );
                
                result=lgw_send(tx_buff); /* non-blocking scheduling of TX packet */
                
                /* 发送结果检查 */
                if ( result == LGW_HAL_ERROR) {

                        DEBUG_CONF("Join data send error\n");    
            
                }else if (result == LGW_LBT_ISSUE ){

                        DEBUG_CONF("Failed: Not allowed (LBT)\n");

                } else {
                            /* wait for packet to finish sending */
                            int loop = 0;
                            int scheduled_counter = 0;
                            
                            do 
                            {      
                                    loop+=1;
                                    wait_ms(5);
                                    lgw_status(TX_STATUS, &status_var); /* get TX status */

                                    if (status_var == TX_SCHEDULED) {
                                            
                                            scheduled_counter+=1;
                    
                                    }

                                    /* tx 错误处理机制  */
                                    if ( scheduled_counter >= 55) 
                                    {

                                            /* 中止发生 */      
                                            result = lgw_abort_tx();
                                            DEBUG_CONF("\n/*------------------------------------------------------- Payload TX_SCHEDULED is overtime! Abort tx packet! --------------------------------------------------*/\n");
                                            if ( result == LGW_HAL_SUCCESS ) {

                                                    DEBUG_CONF("\n LGW_HAL_SUCCESS: %d \n", result );

                                            } else {

                                                    DEBUG_CONF("\n LGW_HAL_ERROR: %d \n", result );         
                                            }

                                            /* 删除class a队列中首个节点  */                
                                            if ( ClassA_QueueIsEmpty( &class_a_line)){

                                                    DEBUG_CONF("queue is empty");

                                            } else {
                                                
                                                    ClassA_DeQueue(&node_temp, &class_a_line);
                                                    DEBUG_CONF("detele data queue\n");
                                            }
                                            
                                            continue;              
                                    } 

                            } while (status_var != TX_FREE);
                                    
                            DEBUG_CONF("\n /*--------------------------------------------- Confrim Data Send OK!!!-------------------------------------------------*/\n");           

                        }
                        
        }
        else /* 不分包 */
        {

                macHdr_temp.Value = node_payload[0];
               
                /* 删除class a队列中首个节点  */                
                if ( ClassA_QueueIsEmpty( &class_a_line)){

                        DEBUG_CONF("queue is empty");

                } else {
                    
                        ClassA_DeQueue(&node_temp, &class_a_line);
                        DEBUG_CONF("detele data queue\n");
                }

                tx_buff.freq_hz    =  down_frequency;
                tx_buff.bandwidth  =  *bandwidth;
                tx_buff.datarate   =  down_datarate;
                tx_buff.coderate   =  *coderate;
                tx_buff.size       = (*value_len) + 13;
                tx_buff.tx_mode    = TIMESTAMPED; 
                tx_buff.rf_chain   = TX_RF_CHAIN;
                tx_buff.rf_power   = pow; 
                tx_buff.modulation = MOD_LORA;
                tx_buff.invert_pol = invert;
                tx_buff.preamble   = preamb;
                tx_buff.count_us   = *tx_count_us;

                DEBUG_CONF("tx_buff.freq_hz :  %d\n",  tx_buff.freq_hz);
                DEBUG_CONF("tx_buff.bandwidth: %d\n",  tx_buff.bandwidth);
                DEBUG_CONF("tx_buff.datarate:  %d\n",  tx_buff.datarate);
                DEBUG_CONF("tx_buff.coderate:  %d\n",  tx_buff.coderate);
                DEBUG_CONF("tx_buff.size    :  %d\n",  tx_buff.size);
                DEBUG_CONF("tx_buff.count_us:  %d\n",  tx_buff.count_us);

                /* 改为下行不应答类型 */
                macHdr.Bits.MType  = FRAME_TYPE_DATA_UNCONFIRMED_DOWN;        
                tx_buff.payload[0] = node_payload[0];

                /* 修改下行数据类型 */
                tx_buff.payload[0] = ((uint8_t)((3<<tx_buff.payload[0])>>3) | (uint8_t)(macHdr.Bits.MType <<5)); 

                /* devaddr */
                tx_buff.payload[1] =  node_payload[1];
                tx_buff.payload[2] =  node_payload[2];
                tx_buff.payload[3] =  node_payload[3];
                tx_buff.payload[4] =  node_payload[4];

                address            =  node_payload[1];
                address           |= (node_payload[2]<<8);
                address           |= (node_payload[3]<<16);
                address           |= (node_payload[4]<<24);
                
                DEBUG_CONF ("--------------------address: 0x%02x-------------------\n", address);
                
                for ( int i = 0; i < *value_len; i++)
                        DEBUG_CONF("classa_payload[%d]:0x%02x\n",i,classa_payload[i]);

                fCtrl.Value = node_payload[5];

                /* 判断应答位是否需要使能 */ 
                if ( macHdr_temp.Bits.MType == 0x04 ){

                        down_fCtrl.DownBits.Ack = 1;
                        DEBUG_CONF("down_fCtrl.DownBits.Ack is true!\n");

                } else if ( macHdr_temp.Bits.MType == 0x02 ) {

                        down_fCtrl.DownBits.Ack = 0;
                        DEBUG_CONF("down_fCtrl.DownBits.Ack is false!\n");

                } else {
                    
                        DEBUG_CONF("MType is error!\n");
                }

                down_fCtrl.DownBits.Adr 	   = fCtrl.Bits.Adr;
                down_fCtrl.DownBits.FOptsLen   = fCtrl.Bits.FOptsLen;
                down_fCtrl.DownBits.RFU 	   = 0x0;
                down_fCtrl.DownBits.FPending   = 0x0;

                tx_buff.payload[5] = down_fCtrl.Value;    

                /* 读取节点对应的fcnt_down */
                if ( 0 == Fetch_GWinfo_FcntDown( db,&address,&fcnt_down ))
                        DEBUG_CONF("fetch fcntdown error!\n");
                
                /* +1 操作 */
                fcnt_down = fcnt_down +1;  

                /* 更新该节点的fcntdown到本地数据库 */
                if ( 0 == Update_GWinfo_FcntDown( db,zErrMsg,&address,&fcnt_down ))
                        DEBUG_CONF("update fcntdown error!\n");    
                
                tx_buff.payload[6] = (uint8_t)fcnt_down;
                tx_buff.payload[7] = (uint8_t)(fcnt_down>>8);
                tx_buff.payload[8] = node_payload[8];

                /* FRMpayload */
                /* 拷贝服务器下发的数据 */
                mymemcpy(tx_buff.payload+9,classa_payload,*value_len);

                if ( 0 == Fetch_Nwkskey_Appskey_Table (db,&address,nwkskey,appskey))
                            DEBUG_CONF("Fetch Nwkskey and Appskey error!\n");    
                
                /* 载荷加密 */         
                LoRaMacPayloadEncrypt(tx_buff.payload+9, *value_len, appskey, address, DOWN_LINK,fcnt_down, encrypt_buff);
              
                /* 加密后的载荷再拷贝到发送的载荷中 */   
                mymemcpy(tx_buff.payload+9,encrypt_buff, *value_len);  

                /* 计算mic */
                LoRaMacComputeMic(tx_buff.payload,(*value_len) + 9, nwkskey, address, DOWN_LINK,fcnt_down, &mic_gateway);
                tx_buff.payload[(*value_len)+9]  = (uint8_t) mic_gateway;
                tx_buff.payload[(*value_len)+10] = (uint8_t)(mic_gateway >>8);
                tx_buff.payload[(*value_len)+11] = (uint8_t)(mic_gateway >>16);
                tx_buff.payload[(*value_len)+12] = (uint8_t)(mic_gateway >>24);

                result=lgw_send(tx_buff); /* non-blocking scheduling of TX packet */
                
                /* 发送结果检查 */
                if ( result == LGW_HAL_ERROR) {

                        DEBUG_CONF("Join data send error\n");    
            
                }else if (result == LGW_LBT_ISSUE ){

                        DEBUG_CONF("Failed: Not allowed (LBT)\n");

                } else {
                            /* wait for packet to finish sending */
                            int loop = 0;
                            int scheduled_counter = 0;
                            
                            do 
                            {      
                                    loop+=1;
                                    wait_ms(5);
                                    lgw_status(TX_STATUS, &status_var); /* get TX status */

                                    if (status_var == TX_SCHEDULED) {
                                            
                                            scheduled_counter+=1;
                    
                                    }

                                    /* tx 错误处理机制  */
                                    if ( scheduled_counter >= 55) 
                                    {

                                            /* 中止发生 */      
                                            result = lgw_abort_tx();
                                            DEBUG_CONF("\n/*------------------------------------------------------- Payload TX_SCHEDULED is overtime! Abort tx packet! --------------------------------------------------*/\n");
                                            if ( result == LGW_HAL_SUCCESS ) {

                                                    DEBUG_CONF("\n LGW_HAL_SUCCESS: %d \n", result );

                                            } else {

                                                    DEBUG_CONF("\n LGW_HAL_ERROR: %d \n", result );         
                                            }

                                            /* 删除class a队列中首个节点  */                
                                            if ( ClassA_QueueIsEmpty( &class_a_line)){

                                                    DEBUG_CONF("queue is empty");

                                            } else {
                                                
                                                    ClassA_DeQueue(&node_temp, &class_a_line);
                                                    DEBUG_CONF("detele data queue\n");
                                            }
                                            
                                            continue;              
                                    } 

                            } while (status_var != TX_FREE);
                                    
                            DEBUG_CONF("\n /*--------------------------------------------- Confrim Data Send OK!!!-------------------------------------------------*/\n");           

                        }

        }

}

/*----------------------------------------------------------------------------------------------------------------------------------*/
/*----------------------------------------------------------LORAWAN官网自有函数部分----------------------------------------------------*/

/* --- THREAD 1: RECEIVING PACKETS AND FORWARDING THEM ---------------------- */
void thread_up(void) 
{ 
     #if 0 
    int i, j; /* loop variables */
    unsigned pkt_in_dgram; /* nb on Lora packet in the current datagram */

    /* allocate memory for packet fetching and processing */
    struct lgw_pkt_rx_s rxpkt[NB_PKT_MAX]; /* array containing inbound packets + metadata */
    struct lgw_pkt_rx_s *p; /* pointer on a RX packet */
    int nb_pkt;
      /* report management variable */
    bool send_report = false;
	  
	
    /* local copy of GPS time reference */
    bool ref_ok = false; /* determine if GPS time reference must be used or not */
    struct tref local_ref; /* time reference used for UTC <-> timestamp conversion */

    /* data buffers */
    uint8_t buff_up[TX_BUFF_SIZE]; /* buffer to compose the upstream packet */
    int buff_index;
    uint8_t buff_ack[32]; /* buffer to receive acknowledges */

    /* protocol variables */
    uint8_t token_h; /* random token for acknowledgement matching */
    uint8_t token_l; /* random token for acknowledgement matching */

    /* ping measurement variables */
    struct timespec send_time;
    struct timespec recv_time;

    /* GPS synchronization variables */
    struct timespec pkt_utc_time;
    struct tm * x; /* broken-up UTC time */
    struct timespec pkt_gps_time;
    uint64_t pkt_gps_time_ms;

    /* report management variable */
    bool send_report = false;

    /* mote info variables */
    uint32_t mote_addr = 0;
    uint16_t mote_fcnt = 0;
 
	
    /* set upstream socket RX timeout */
    i = setsockopt(sock_up, SOL_SOCKET, SO_RCVTIMEO, (void *)&push_timeout_half, sizeof push_timeout_half);
    if (i != 0) 
	{
        MSG("ERROR: [up] setsockopt returned %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
	

    /* pre-fill the data buffer with fixed fields */
    buff_up[0] = PROTOCOL_VERSION;
    buff_up[3] = PKT_PUSH_DATA;
    *(uint32_t *)(buff_up + 4) = net_mac_h;
    *(uint32_t *)(buff_up + 8) = net_mac_l;
 
	
    while (!exit_sig && !quit_sig) 
	{

        /* fetch packets */
        pthread_mutex_lock(&mx_concent);
        nb_pkt = lgw_receive(NB_PKT_MAX, rxpkt);
        pthread_mutex_unlock(&mx_concent);
        if (nb_pkt == LGW_HAL_ERROR) 
		{
            MSG("ERROR: [up] failed packet fetch, exiting\n");
            exit(EXIT_FAILURE);
     	}

        /* check if there are status report to send */
        send_report = report_ready; /* copy the variable so it doesn't change mid-function */
        /* no mutex, we're only reading */

        /* wait a short time if no packets, nor status report */
        if ((nb_pkt == 0) && (send_report == false)) 
		{
            wait_ms(FETCH_SLEEP_MS);
            continue;
         }

		
        /* get a copy of GPS time reference (avoid 1 mutex per packet) */
        if ((nb_pkt > 0) && (gps_enabled == true)) {
            pthread_mutex_lock(&mx_timeref);
            ref_ok = gps_ref_valid;
            local_ref = time_reference_gps;
            pthread_mutex_unlock(&mx_timeref);
        } 
		else 
		{
            ref_ok = false;
        }

        /* start composing datagram with the header */
        token_h = (uint8_t)rand(); /* random token */
        token_l = (uint8_t)rand(); /* random token */
        buff_up[1] = token_h;
        buff_up[2] = token_l;
        buff_index = 12; /* 12-byte header */

        /* start of JSON structure */
        memcpy((void *)(buff_up + buff_index), (void *)"{\"rxpk\":[", 9);
        buff_index += 9;

        /* serialize Lora packets metadata and payload */
        pkt_in_dgram = 0;
        for (i=0; i < nb_pkt; ++i) 
	{
            p = &rxpkt[i];

            /* Get mote information from current packet (addr, fcnt) */
            /* FHDR - DevAddr */
            mote_addr  = p->payload[1];
            mote_addr |= p->payload[2] << 8;
            mote_addr |= p->payload[3] << 16;
            mote_addr |= p->payload[4] << 24;
            /* FHDR - FCnt */
            mote_fcnt  = p->payload[6];
            mote_fcnt |= p->payload[7] << 8;

            /* basic packet filtering */
            pthread_mutex_lock(&mx_meas_up);
            meas_nb_rx_rcv += 1;
            switch(p->status) {
                case STAT_CRC_OK:
                    meas_nb_rx_ok += 1;
                    printf( "\nINFO: Received pkt from mote: %08X (fcnt=%u)\n", mote_addr, mote_fcnt );
                    if (!fwd_valid_pkt) {
                        pthread_mutex_unlock(&mx_meas_up);
                        continue; /* skip that packet */
                    }
                    break;
                case STAT_CRC_BAD:
                    meas_nb_rx_bad += 1;
                    if (!fwd_error_pkt) {
                        pthread_mutex_unlock(&mx_meas_up);
                        continue; /* skip that packet */
                    }
                    break;
                case STAT_NO_CRC:
                    meas_nb_rx_nocrc += 1;
                    if (!fwd_nocrc_pkt) {
                        pthread_mutex_unlock(&mx_meas_up);
                        continue; /* skip that packet */
                    }
                    break;
                default:
                    MSG("WARNING: [up] received packet with unknown status %u (size %u, modulation %u, BW %u, DR %u, RSSI %.1f)\n", p->status, p->size, p->modulation, p->bandwidth, p->datarate, p->rssi);
                    pthread_mutex_unlock(&mx_meas_up);
                    continue; /* skip that packet */
                    // exit(EXIT_FAILURE);
            }
            meas_up_pkt_fwd += 1;
            meas_up_payload_byte += p->size;
            pthread_mutex_unlock(&mx_meas_up);

            /* Start of packet, add inter-packet separator if necessary */
            if (pkt_in_dgram == 0) {
                buff_up[buff_index] = '{';
                ++buff_index;
            } else {
                buff_up[buff_index] = ',';
                buff_up[buff_index+1] = '{';
                buff_index += 2;
            }

            /* RAW timestamp, 8-17 useful chars */
            j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, "\"tmst\":%u", p->count_us);
            if (j > 0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                exit(EXIT_FAILURE);
            }

            /* Packet RX time (GPS based), 37 useful chars */
            if (ref_ok == true) {
                /* convert packet timestamp to UTC absolute time */
                j = lgw_cnt2utc(local_ref, p->count_us, &pkt_utc_time);
                if (j == LGW_GPS_SUCCESS) {
                    /* split the UNIX timestamp to its calendar components */
                    x = gmtime(&(pkt_utc_time.tv_sec));
                    j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, ",\"time\":\"%04i-%02i-%02iT%02i:%02i:%02i.%06liZ\"", (x->tm_year)+1900, (x->tm_mon)+1, x->tm_mday, x->tm_hour, x->tm_min, x->tm_sec, (pkt_utc_time.tv_nsec)/1000); /* ISO 8601 format */
                    if (j > 0) {
                        buff_index += j;
                    } else {
                        MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                        exit(EXIT_FAILURE);
                    }
                }
                /* convert packet timestamp to GPS absolute time */
                j = lgw_cnt2gps(local_ref, p->count_us, &pkt_gps_time);
                if (j == LGW_GPS_SUCCESS) {
                    pkt_gps_time_ms = pkt_gps_time.tv_sec * 1E3 + pkt_gps_time.tv_nsec / 1E6;
                    j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, ",\"tmms\":%llu",
                                    pkt_gps_time_ms); /* GPS time in milliseconds since 06.Jan.1980 */
                    if (j > 0) {
                        buff_index += j;
                    } else {
                        MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                        exit(EXIT_FAILURE);
                    }
                }
            }

            /* Packet concentrator channel, RF chain & RX frequency, 34-36 useful chars */
            j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, ",\"chan\":%1u,\"rfch\":%1u,\"freq\":%.6lf", p->if_chain, p->rf_chain, ((double)p->freq_hz / 1e6));
            if (j > 0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                exit(EXIT_FAILURE);
            }

            /* Packet status, 9-10 useful chars */
            switch (p->status) {
                case STAT_CRC_OK:
                    memcpy((void *)(buff_up + buff_index), (void *)",\"stat\":1", 9);
                    buff_index += 9;
                    break;
                case STAT_CRC_BAD:
                    memcpy((void *)(buff_up + buff_index), (void *)",\"stat\":-1", 10);
                    buff_index += 10;
                    break;
                case STAT_NO_CRC:
                    memcpy((void *)(buff_up + buff_index), (void *)",\"stat\":0", 9);
                    buff_index += 9;
                    break;
                default:
                    MSG("ERROR: [up] received packet with unknown status\n");
                    memcpy((void *)(buff_up + buff_index), (void *)",\"stat\":?", 9);
                    buff_index += 9;
                    exit(EXIT_FAILURE);
            }

            /* Packet modulation, 13-14 useful chars */
            if (p->modulation == MOD_LORA) {
                memcpy((void *)(buff_up + buff_index), (void *)",\"modu\":\"LORA\"", 14);
                buff_index += 14;

                /* Lora datarate & bandwidth, 16-19 useful chars */
                switch (p->datarate) {
                    case DR_LORA_SF7:
                        memcpy((void *)(buff_up + buff_index), (void *)",\"datr\":\"SF7", 12);
                        buff_index += 12;
                        break;
                    case DR_LORA_SF8:
                        memcpy((void *)(buff_up + buff_index), (void *)",\"datr\":\"SF8", 12);
                        buff_index += 12;
                        break;
                    case DR_LORA_SF9:
                        memcpy((void *)(buff_up + buff_index), (void *)",\"datr\":\"SF9", 12);
                        buff_index += 12;
                        break;
                    case DR_LORA_SF10:
                        memcpy((void *)(buff_up + buff_index), (void *)",\"datr\":\"SF10", 13);
                        buff_index += 13;
                        break;
                    case DR_LORA_SF11:
                        memcpy((void *)(buff_up + buff_index), (void *)",\"datr\":\"SF11", 13);
                        buff_index += 13;
                        break;
                    case DR_LORA_SF12:
                        memcpy((void *)(buff_up + buff_index), (void *)",\"datr\":\"SF12", 13);
                        buff_index += 13;
                        break;
                    default:
                        MSG("ERROR: [up] lora packet with unknown datarate\n");
                        memcpy((void *)(buff_up + buff_index), (void *)",\"datr\":\"SF?", 12);
                        buff_index += 12;
                        exit(EXIT_FAILURE);
                }
                switch (p->bandwidth) {
                    case BW_125KHZ:
                        memcpy((void *)(buff_up + buff_index), (void *)"BW125\"", 6);
                        buff_index += 6;
                        break;
                    case BW_250KHZ:
                        memcpy((void *)(buff_up + buff_index), (void *)"BW250\"", 6);
                        buff_index += 6;
                        break;
                    case BW_500KHZ:
                        memcpy((void *)(buff_up + buff_index), (void *)"BW500\"", 6);
                        buff_index += 6;
                        break;
                    default:
                        MSG("ERROR: [up] lora packet with unknown bandwidth\n");
                        memcpy((void *)(buff_up + buff_index), (void *)"BW?\"", 4);
                        buff_index += 4;
                        exit(EXIT_FAILURE);
                }

                /* Packet ECC coding rate, 11-13 useful chars */
                switch (p->coderate) {
                    case CR_LORA_4_5:
                        memcpy((void *)(buff_up + buff_index), (void *)",\"codr\":\"4/5\"", 13);
                        buff_index += 13;
                        break;
                    case CR_LORA_4_6:
                        memcpy((void *)(buff_up + buff_index), (void *)",\"codr\":\"4/6\"", 13);
                        buff_index += 13;
                        break;
                    case CR_LORA_4_7:
                        memcpy((void *)(buff_up + buff_index), (void *)",\"codr\":\"4/7\"", 13);
                        buff_index += 13;
                        break;
                    case CR_LORA_4_8:
                        memcpy((void *)(buff_up + buff_index), (void *)",\"codr\":\"4/8\"", 13);
                        buff_index += 13;
                        break;
                    case 0: /* treat the CR0 case (mostly false sync) */
                        memcpy((void *)(buff_up + buff_index), (void *)",\"codr\":\"OFF\"", 13);
                        buff_index += 13;
                        break;
                    default:
                        MSG("ERROR: [up] lora packet with unknown coderate\n");
                        memcpy((void *)(buff_up + buff_index), (void *)",\"codr\":\"?\"", 11);
                        buff_index += 11;
                        exit(EXIT_FAILURE);
                }

                /* Lora SNR, 11-13 useful chars */
                j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, ",\"lsnr\":%.1f", p->snr);
                if (j > 0) {
                    buff_index += j;
                } else {
                    MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                    exit(EXIT_FAILURE);
                }
            } else if (p->modulation == MOD_FSK) {
                memcpy((void *)(buff_up + buff_index), (void *)",\"modu\":\"FSK\"", 13);
                buff_index += 13;

                /* FSK datarate, 11-14 useful chars */
                j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, ",\"datr\":%u", p->datarate);
                if (j > 0) {
                    buff_index += j;
                } else {
                    MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                    exit(EXIT_FAILURE);
                }
            } else {
                MSG("ERROR: [up] received packet with unknown modulation\n");
                exit(EXIT_FAILURE);
            }

            /* Packet RSSI, payload size, 18-23 useful chars */
            j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, ",\"rssi\":%.0f,\"size\":%u", p->rssi, p->size);
            if (j > 0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                exit(EXIT_FAILURE);
            }

            /* Packet base64-encoded payload, 14-350 useful chars */
            memcpy((void *)(buff_up + buff_index), (void *)",\"data\":\"", 9);
            buff_index += 9;
            j = bin_to_b64(p->payload, p->size, (char *)(buff_up + buff_index), 341); /* 255 bytes = 340 chars in b64 + null char */
            if (j>=0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] bin_to_b64 failed line %u\n", (__LINE__ - 5));
                exit(EXIT_FAILURE);
            }
            buff_up[buff_index] = '"';
            ++buff_index;

            /* End of packet serialization */
            buff_up[buff_index] = '}';
            ++buff_index;
            ++pkt_in_dgram;
        }

        /* restart fetch sequence without sending empty JSON if all packets have been filtered out */
        if (pkt_in_dgram == 0) {
            if (send_report == true) {
                /* need to clean up the beginning of the payload */
                buff_index -= 8; /* removes "rxpk":[ */
            } else {
                /* all packet have been filtered out and no report, restart loop */
                continue;
            }
        } else {
            /* end of packet array */
            buff_up[buff_index] = ']';
            ++buff_index;
            /* add separator if needed */
            if (send_report == true) {
                buff_up[buff_index] = ',';
                ++buff_index;
            }
        }

        /* add status report if a new one is available */
        if (send_report == true) {
            pthread_mutex_lock(&mx_stat_rep);
            report_ready = false;
            j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, "%s", status_report);
            pthread_mutex_unlock(&mx_stat_rep);
            if (j > 0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 5));
                exit(EXIT_FAILURE);
            }
        }

        /* end of JSON datagram payload */
        buff_up[buff_index] = '}';
        ++buff_index;
        buff_up[buff_index] = 0; /* add string terminator, for safety */

        printf("\nJSON up: %s\n", (char *)(buff_up + 12)); /* DEBUG: display JSON payload */

        /* send datagram to server */
        send(sock_up, (void *)buff_up, buff_index, 0);
        clock_gettime(CLOCK_MONOTONIC, &send_time);
        pthread_mutex_lock(&mx_meas_up);
        meas_up_dgram_sent += 1;
        meas_up_network_byte += buff_index;

        /* wait for acknowledge (in 2 times, to catch extra packets) */
        for (i=0; i<2; ++i) {
            j = recv(sock_up, (void *)buff_ack, sizeof buff_ack, 0);
            clock_gettime(CLOCK_MONOTONIC, &recv_time);
            if (j == -1) {
                if (errno == EAGAIN) { /* timeout */
                    continue;
                } else { /* server connection error */
                    break;
                }
            } else if ((j < 4) || (buff_ack[0] != PROTOCOL_VERSION) || (buff_ack[3] != PKT_PUSH_ACK)) {
                //MSG("WARNING: [up] ignored invalid non-ACL packet\n");
                continue;
            } else if ((buff_ack[1] != token_h) || (buff_ack[2] != token_l)) {
                //MSG("WARNING: [up] ignored out-of sync ACK packet\n");
                continue;
            } else {
                MSG("INFO: [up] PUSH_ACK received in %i ms\n", (int)(1000 * difftimespec(recv_time, send_time)));
                meas_up_ack_rcv += 1;
                break;
            }
        }
        pthread_mutex_unlock(&mx_meas_up);
	 #endif
    //}
    MSG("\nINFO: End of upstream thread\n");
}

/* -------------------------------------------------------------------------- */
/* --- THREAD 2: POLLING SERVER AND ENQUEUING PACKETS IN JIT QUEUE ---------- */

void thread_down(void) 
{
    int i; /* loop variables */

    /* configuration and metadata for an outbound packet */
    struct lgw_pkt_tx_s txpkt;
    bool sent_immediate = false; /* option to sent the packet immediately */

    /* local timekeeping variables */
    struct timespec send_time; /* time of the pull request */
    struct timespec recv_time; /* time of return from recv socket call */

    /* data buffers */
    uint8_t buff_down[1000]; /* buffer to receive downstream packets */
    uint8_t buff_req[12]; /* buffer to compose pull requests */
    int msg_len;

    /* protocol variables */
    uint8_t token_h; /* random token for acknowledgement matching */
    uint8_t token_l; /* random token for acknowledgement matching */
    bool req_ack = false; /* keep track of whether PULL_DATA was acknowledged or not */

    /* JSON parsing variables */
    JSON_Value *root_val = NULL;
    JSON_Object *txpk_obj = NULL;
    JSON_Value *val = NULL; /* needed to detect the absence of some fields */
    const char *str; /* pointer to sub-strings in the JSON data */
    short x0, x1;
    uint64_t x2;
    double x3, x4;

    /* variables to send on GPS timestamp */
    struct tref local_ref; /* time reference used for GPS <-> timestamp conversion */
    struct timespec gps_tx; /* GPS time that needs to be converted to timestamp */

    /* beacon variables */
    struct lgw_pkt_tx_s beacon_pkt;
    uint8_t beacon_chan;
    uint8_t beacon_loop;
    size_t beacon_RFU1_size = 0;
    size_t beacon_RFU2_size = 0;
    uint8_t beacon_pyld_idx = 0;
    time_t diff_beacon_time;
    struct timespec next_beacon_gps_time; /* gps time of next beacon packet */
    struct timespec last_beacon_gps_time; /* gps time of last enqueued beacon packet */
    int retry;

    /* beacon data fields, byte 0 is Least Significant Byte */
    int32_t field_latitude; /* 3 bytes, derived from reference latitude */
    int32_t field_longitude; /* 3 bytes, derived from reference longitude */
    uint16_t field_crc1, field_crc2;

    /* auto-quit variable */
    uint32_t autoquit_cnt = 0; /* count the number of PULL_DATA sent since the latest PULL_ACK */

    /* Just In Time downlink */
    struct timeval current_unix_time;
    struct timeval current_concentrator_time;
    enum jit_error_e jit_result = JIT_ERROR_OK;
    enum jit_pkt_type_e downlink_type;

    /* set downstream socket RX timeout */
    i = setsockopt(sock_down, SOL_SOCKET, SO_RCVTIMEO, (void *)&pull_timeout, sizeof pull_timeout);
    if (i != 0) {
        MSG("ERROR: [down] setsockopt returned %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* pre-fill the pull request buffer with fixed fields */
    buff_req[0] = PROTOCOL_VERSION;
    buff_req[3] = PKT_PULL_DATA;
    *(uint32_t *)(buff_req + 4) = net_mac_h;
    *(uint32_t *)(buff_req + 8) = net_mac_l;

    /* beacon variables initialization */
    last_beacon_gps_time.tv_sec = 0;
    last_beacon_gps_time.tv_nsec = 0;

    /* beacon packet parameters */
    beacon_pkt.tx_mode = ON_GPS; /* send on PPS pulse */
    beacon_pkt.rf_chain = 0; /* antenna A */
    beacon_pkt.rf_power = beacon_power;
    beacon_pkt.modulation = MOD_LORA;
    switch (beacon_bw_hz) {
        case 125000:
            beacon_pkt.bandwidth = BW_125KHZ;
            break;
        case 500000:
            beacon_pkt.bandwidth = BW_500KHZ;
            break;
        default:
            /* should not happen */
            MSG("ERROR: unsupported bandwidth for beacon\n");
            exit(EXIT_FAILURE);
    }
    switch (beacon_datarate) {
        case 8:
            beacon_pkt.datarate = DR_LORA_SF8;
            beacon_RFU1_size = 1;
            beacon_RFU2_size = 3;
            break;
        case 9:
            beacon_pkt.datarate = DR_LORA_SF9;
            beacon_RFU1_size = 2;
            beacon_RFU2_size = 0;
            break;
        case 10:
            beacon_pkt.datarate = DR_LORA_SF10;
            beacon_RFU1_size = 3;
            beacon_RFU2_size = 1;
            break;
        case 12:
            beacon_pkt.datarate = DR_LORA_SF12;
            beacon_RFU1_size = 5;
            beacon_RFU2_size = 3;
            break;
        default:
            /* should not happen */
            MSG("ERROR: unsupported datarate for beacon\n");
            exit(EXIT_FAILURE);
    }
    beacon_pkt.size = beacon_RFU1_size + 4 + 2 + 7 + beacon_RFU2_size + 2;
    beacon_pkt.coderate = CR_LORA_4_5;
    beacon_pkt.invert_pol = false;
    beacon_pkt.preamble = 10;
    beacon_pkt.no_crc = true;
    beacon_pkt.no_header = true;

    /* network common part beacon fields (little endian) */
    for (i = 0; i < (int)beacon_RFU1_size; i++) {
        beacon_pkt.payload[beacon_pyld_idx++] = 0x0;
    }

    /* network common part beacon fields (little endian) */
    beacon_pyld_idx += 4; /* time (variable), filled later */
    beacon_pyld_idx += 2; /* crc1 (variable), filled later */

    /* calculate the latitude and longitude that must be publicly reported */
    field_latitude = (int32_t)((reference_coord.lat / 90.0) * (double)(1<<23));
    if (field_latitude > (int32_t)0x007FFFFF) {
        field_latitude = (int32_t)0x007FFFFF; /* +90 N is represented as 89.99999 N */
    } else if (field_latitude < (int32_t)0xFF800000) {
        field_latitude = (int32_t)0xFF800000;
    }
    field_longitude = (int32_t)((reference_coord.lon / 180.0) * (double)(1<<23));
    if (field_longitude > (int32_t)0x007FFFFF) {
        field_longitude = (int32_t)0x007FFFFF; /* +180 E is represented as 179.99999 E */
    } else if (field_longitude < (int32_t)0xFF800000) {
        field_longitude = (int32_t)0xFF800000;
    }

    /* gateway specific beacon fields */
    beacon_pkt.payload[beacon_pyld_idx++] = beacon_infodesc;
    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF &  field_latitude;
    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF & (field_latitude >>  8);
    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF & (field_latitude >> 16);
    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF &  field_longitude;
    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF & (field_longitude >>  8);
    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF & (field_longitude >> 16);

    /* RFU */
    for (i = 0; i < (int)beacon_RFU2_size; i++) {
        beacon_pkt.payload[beacon_pyld_idx++] = 0x0;
    }

    /* CRC of the beacon gateway specific part fields */
    field_crc2 = crc16((beacon_pkt.payload + 6 + beacon_RFU1_size), 7 + beacon_RFU2_size);
    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF &  field_crc2;
    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF & (field_crc2 >> 8);

    /* JIT queue initialization */
    jit_queue_init(&jit_queue);

    while (!exit_sig && !quit_sig) 
	{

        /* auto-quit if the threshold is crossed */
        if ((autoquit_threshold > 0) && (autoquit_cnt >= autoquit_threshold)) 
		{
            exit_sig = true;
            MSG("INFO: [down] the last %u PULL_DATA were not ACKed, exiting application\n", autoquit_threshold);
            break;
        }

        /* generate random token for request */
        token_h = (uint8_t)rand(); /* random token */
        token_l = (uint8_t)rand(); /* random token */
        buff_req[1] = token_h;
        buff_req[2] = token_l;

        /* send PULL request and record time */
        send(sock_down, (void *)buff_req, sizeof buff_req, 0);
        clock_gettime(CLOCK_MONOTONIC, &send_time);
        pthread_mutex_lock(&mx_meas_dw);
        meas_dw_pull_sent += 1;
        pthread_mutex_unlock(&mx_meas_dw);
        req_ack = false;
        autoquit_cnt++;

        /* listen to packets and process them until a new PULL request must be sent */
        recv_time = send_time;
        while ((int)difftimespec(recv_time, send_time) < keepalive_time) 
		{

            /* try to receive a datagram */
            msg_len = recv(sock_down, (void *)buff_down, (sizeof buff_down)-1, 0);
            clock_gettime(CLOCK_MONOTONIC, &recv_time);

            /* Pre-allocate beacon slots in JiT queue, to check downlink collisions */
            beacon_loop = JIT_NUM_BEACON_IN_QUEUE - jit_queue.num_beacon;
            retry = 0;
            while (beacon_loop && (beacon_period != 0)) 
			{
                pthread_mutex_lock(&mx_timeref);
                /* Wait for GPS to be ready before inserting beacons in JiT queue */
                if ((gps_ref_valid == true) && (xtal_correct_ok == true)) 
					{

                    /* compute GPS time for next beacon to come      */
                    /*   LoRaWAN: T = k*beacon_period + TBeaconDelay */
                    /*            with TBeaconDelay = [1.5ms +/- 1碌s]*/
                    if (last_beacon_gps_time.tv_sec == 0) 
					{
                        /* if no beacon has been queued, get next slot from current GPS time */
                        diff_beacon_time = time_reference_gps.gps.tv_sec % ((time_t)beacon_period);
                        next_beacon_gps_time.tv_sec = time_reference_gps.gps.tv_sec +
                                                        ((time_t)beacon_period - diff_beacon_time);
                    } 
					else 
					{
                        /* if there is already a beacon, take it as reference */
                        next_beacon_gps_time.tv_sec = last_beacon_gps_time.tv_sec + beacon_period;
                    }
                    /* now we can add a beacon_period to the reference to get next beacon GPS time */
                    next_beacon_gps_time.tv_sec += (retry * beacon_period);
                    next_beacon_gps_time.tv_nsec = 0;

#if DEBUG_BEACON
                    {
                    time_t time_unix;

                    time_unix = time_reference_gps.gps.tv_sec + UNIX_GPS_EPOCH_OFFSET;
                    MSG_DEBUG(DEBUG_BEACON, "GPS-now : %s", ctime(&time_unix));
                    time_unix = last_beacon_gps_time.tv_sec + UNIX_GPS_EPOCH_OFFSET;
                    MSG_DEBUG(DEBUG_BEACON, "GPS-last: %s", ctime(&time_unix));
                    time_unix = next_beacon_gps_time.tv_sec + UNIX_GPS_EPOCH_OFFSET;
                    MSG_DEBUG(DEBUG_BEACON, "GPS-next: %s", ctime(&time_unix));
                    }
#endif

                    /* convert GPS time to concentrator time, and set packet counter for JiT trigger */
                    lgw_gps2cnt(time_reference_gps, next_beacon_gps_time, &(beacon_pkt.count_us));
                    pthread_mutex_unlock(&mx_timeref);

                    /* apply frequency correction to beacon TX frequency */
                    if (beacon_freq_nb > 1) {
                        beacon_chan = (next_beacon_gps_time.tv_sec / beacon_period) % beacon_freq_nb; /* floor rounding */
                    } else {
                        beacon_chan = 0;
                    }
                    /* Compute beacon frequency */
                    beacon_pkt.freq_hz = beacon_freq_hz + (beacon_chan * beacon_freq_step);

                    /* load time in beacon payload */
                    beacon_pyld_idx = beacon_RFU1_size;
                    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF &  next_beacon_gps_time.tv_sec;
                    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF & (next_beacon_gps_time.tv_sec >>  8);
                    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF & (next_beacon_gps_time.tv_sec >> 16);
                    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF & (next_beacon_gps_time.tv_sec >> 24);

                    /* calculate CRC */
                    field_crc1 = crc16(beacon_pkt.payload, 4 + beacon_RFU1_size); /* CRC for the network common part */
                    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF & field_crc1;
                    beacon_pkt.payload[beacon_pyld_idx++] = 0xFF & (field_crc1 >> 8);

                    /* Insert beacon packet in JiT queue */
                    gettimeofday(&current_unix_time, NULL);
                    get_concentrator_time(&current_concentrator_time, current_unix_time);
                    jit_result = jit_enqueue(&jit_queue, &current_concentrator_time, &beacon_pkt, JIT_PKT_TYPE_BEACON);
                    if (jit_result == JIT_ERROR_OK) {
                        /* update stats */
                        pthread_mutex_lock(&mx_meas_dw);
                        meas_nb_beacon_queued += 1;
                        pthread_mutex_unlock(&mx_meas_dw);

                        /* One more beacon in the queue */
                        beacon_loop--;
                        retry = 0;
                        last_beacon_gps_time.tv_sec = next_beacon_gps_time.tv_sec; /* keep this beacon time as reference for next one to be programmed */

                        /* display beacon payload */
                        MSG("INFO: Beacon queued (count_us=%u, freq_hz=%u, size=%u):\n", beacon_pkt.count_us, beacon_pkt.freq_hz, beacon_pkt.size);
                        printf( "   => " );
                        for (i = 0; i < beacon_pkt.size; ++i) {
                            MSG("%02X ", beacon_pkt.payload[i]);
                        }
                        MSG("\n");
                    } else {
                        MSG_DEBUG(DEBUG_BEACON, "--> beacon queuing failed with %d\n", jit_result);
                        /* update stats */
                        pthread_mutex_lock(&mx_meas_dw);
                        if (jit_result != JIT_ERROR_COLLISION_BEACON) {
                            meas_nb_beacon_rejected += 1;
                        }
                        pthread_mutex_unlock(&mx_meas_dw);
                        /* In case previous enqueue failed, we retry one period later until it succeeds */
                        /* Note: In case the GPS has been unlocked for a while, there can be lots of retries */
                        /*       to be done from last beacon time to a new valid one */
                        retry++;
                        MSG_DEBUG(DEBUG_BEACON, "--> beacon queuing retry=%d\n", retry);
                    }
                } else {
                    pthread_mutex_unlock(&mx_timeref);
                    break;
                }
            }

            /* if no network message was received, got back to listening sock_down socket */
            if (msg_len == -1) {
                //MSG("WARNING: [down] recv returned %s\n", strerror(errno)); /* too verbose */
                continue;
            }

            /* if the datagram does not respect protocol, just ignore it */
            if ((msg_len < 4) || (buff_down[0] != PROTOCOL_VERSION) || ((buff_down[3] != PKT_PULL_RESP) && (buff_down[3] != PKT_PULL_ACK))) {
                MSG("WARNING: [down] ignoring invalid packet len=%d, protocol_version=%d, id=%d\n",
                        msg_len, buff_down[0], buff_down[3]);
                continue;
            }

            /* if the datagram is an ACK, check token */
            if (buff_down[3] == PKT_PULL_ACK) {
                if ((buff_down[1] == token_h) && (buff_down[2] == token_l)) {
                    if (req_ack) {
                        MSG("INFO: [down] duplicate ACK received :)\n");
                    } else { /* if that packet was not already acknowledged */
                        req_ack = true;
                        autoquit_cnt = 0;
                        pthread_mutex_lock(&mx_meas_dw);
                        meas_dw_ack_rcv += 1;
                        pthread_mutex_unlock(&mx_meas_dw);
                        MSG("INFO: [down] PULL_ACK received in %i ms\n", (int)(1000 * difftimespec(recv_time, send_time)));
                    }
                } else { /* out-of-sync token */
                    MSG("INFO: [down] received out-of-sync ACK\n");
                }
                continue;
            }

            /* the datagram is a PULL_RESP */
            buff_down[msg_len] = 0; /* add string terminator, just to be safe */
            MSG("INFO: [down] PULL_RESP received  - token[%d:%d] :)\n", buff_down[1], buff_down[2]); /* very verbose */
            printf("\nJSON down: %s\n", (char *)(buff_down + 4)); /* DEBUG: display JSON payload */

            /* initialize TX struct and try to parse JSON */
            memset(&txpkt, 0, sizeof txpkt);
            root_val = json_parse_string_with_comments((const char *)(buff_down + 4)); /* JSON offset */
            if (root_val == NULL) {
                MSG("WARNING: [down] invalid JSON, TX aborted\n");
                continue;
            }

            /* look for JSON sub-object 'txpk' */
            txpk_obj = json_object_get_object(json_value_get_object(root_val), "txpk");
            if (txpk_obj == NULL) {
                MSG("WARNING: [down] no \"txpk\" object in JSON, TX aborted\n");
                json_value_free(root_val);
                continue;
            }

            /* Parse "immediate" tag, or target timestamp, or UTC time to be converted by GPS (mandatory) */
            i = json_object_get_boolean(txpk_obj,"imme"); /* can be 1 if true, 0 if false, or -1 if not a JSON boolean */
            if (i == 1) {
                /* TX procedure: send immediately */
                sent_immediate = true;
                downlink_type = JIT_PKT_TYPE_DOWNLINK_CLASS_C;
                MSG("INFO: [down] a packet will be sent in \"immediate\" mode\n");
            } else {
                sent_immediate = false;
                val = json_object_get_value(txpk_obj,"tmst");
                if (val != NULL) {
                    /* TX procedure: send on timestamp value */
                    txpkt.count_us = (uint32_t)json_value_get_number(val);

                    /* Concentrator timestamp is given, we consider it is a Class A downlink */
                    downlink_type = JIT_PKT_TYPE_DOWNLINK_CLASS_A;
                } else {
                    /* TX procedure: send on GPS time (converted to timestamp value) */
                    val = json_object_get_value(txpk_obj, "tmms");
                    if (val == NULL) {
                        MSG("WARNING: [down] no mandatory \"txpk.tmst\" or \"txpk.tmms\" objects in JSON, TX aborted\n");
                        json_value_free(root_val);
                        continue;
                    }
                    if (gps_enabled == true) {
                        pthread_mutex_lock(&mx_timeref);
                        if (gps_ref_valid == true) {
                            local_ref = time_reference_gps;
                            pthread_mutex_unlock(&mx_timeref);
                        } else {
                            pthread_mutex_unlock(&mx_timeref);
                            MSG("WARNING: [down] no valid GPS time reference yet, impossible to send packet on specific GPS time, TX aborted\n");
                            json_value_free(root_val);

                            /* send acknoledge datagram to server */
                            send_tx_ack(buff_down[1], buff_down[2], JIT_ERROR_GPS_UNLOCKED);
                            continue;
                        }
                    } else {
                        MSG("WARNING: [down] GPS disabled, impossible to send packet on specific GPS time, TX aborted\n");
                        json_value_free(root_val);

                        /* send acknoledge datagram to server */
                        send_tx_ack(buff_down[1], buff_down[2], JIT_ERROR_GPS_UNLOCKED);
                        continue;
                    }

                    /* Get GPS time from JSON */
                    x2 = (uint64_t)json_value_get_number(val);

                    /* Convert GPS time from milliseconds to timespec */
                    x3 = modf((double)x2/1E3, &x4);
                    gps_tx.tv_sec = (time_t)x4; /* get seconds from integer part */
                    gps_tx.tv_nsec = (long)(x3 * 1E9); /* get nanoseconds from fractional part */

                    /* transform GPS time to timestamp */
                    i = lgw_gps2cnt(local_ref, gps_tx, &(txpkt.count_us));
                    if (i != LGW_GPS_SUCCESS) {
                        MSG("WARNING: [down] could not convert GPS time to timestamp, TX aborted\n");
                        json_value_free(root_val);
                        continue;
                    } else {
                        MSG("INFO: [down] a packet will be sent on timestamp value %u (calculated from GPS time)\n", txpkt.count_us);
                    }

                    /* GPS timestamp is given, we consider it is a Class B downlink */
                    downlink_type = JIT_PKT_TYPE_DOWNLINK_CLASS_B;
                }
            }

            /* Parse "No CRC" flag (optional field) */
            val = json_object_get_value(txpk_obj,"ncrc");
            if (val != NULL) {
                txpkt.no_crc = (bool)json_value_get_boolean(val);
            }

            /* parse target frequency (mandatory) */
            val = json_object_get_value(txpk_obj,"freq");
            if (val == NULL) {
                MSG("WARNING: [down] no mandatory \"txpk.freq\" object in JSON, TX aborted\n");
                json_value_free(root_val);
                continue;
            }
            txpkt.freq_hz = (uint32_t)((double)(1.0e6) * json_value_get_number(val));

            /* parse RF chain used for TX (mandatory) */
            val = json_object_get_value(txpk_obj,"rfch");
            if (val == NULL) {
                MSG("WARNING: [down] no mandatory \"txpk.rfch\" object in JSON, TX aborted\n");
                json_value_free(root_val);
                continue;
            }
            txpkt.rf_chain = (uint8_t)json_value_get_number(val);

            /* parse TX power (optional field) */
            val = json_object_get_value(txpk_obj,"powe");
            if (val != NULL) {
                txpkt.rf_power = (int8_t)json_value_get_number(val) - antenna_gain;
            }

            /* Parse modulation (mandatory) */
            str = json_object_get_string(txpk_obj, "modu");
            if (str == NULL) {
                MSG("WARNING: [down] no mandatory \"txpk.modu\" object in JSON, TX aborted\n");
                json_value_free(root_val);
                continue;
            }
            if (strcmp(str, "LORA") == 0) 
			{
                /* Lora modulation */
                txpkt.modulation = MOD_LORA;

                /* Parse Lora spreading-factor and modulation bandwidth (mandatory) */
                str = json_object_get_string(txpk_obj, "datr");
                if (str == NULL) {
                    MSG("WARNING: [down] no mandatory \"txpk.datr\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                i = sscanf(str, "SF%2hdBW%3hd", &x0, &x1);
                if (i != 2) {
                    MSG("WARNING: [down] format error in \"txpk.datr\", TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                switch (x0) {
                    case  7: txpkt.datarate = DR_LORA_SF7;  break;
                    case  8: txpkt.datarate = DR_LORA_SF8;  break;
                    case  9: txpkt.datarate = DR_LORA_SF9;  break;
                    case 10: txpkt.datarate = DR_LORA_SF10; break;
                    case 11: txpkt.datarate = DR_LORA_SF11; break;
                    case 12: txpkt.datarate = DR_LORA_SF12; break;
                    default:
                        MSG("WARNING: [down] format error in \"txpk.datr\", invalid SF, TX aborted\n");
                        json_value_free(root_val);
                        continue;
                }
                switch (x1) {
                    case 125: txpkt.bandwidth = BW_125KHZ; break;
                    case 250: txpkt.bandwidth = BW_250KHZ; break;
                    case 500: txpkt.bandwidth = BW_500KHZ; break;
                    default:
                        MSG("WARNING: [down] format error in \"txpk.datr\", invalid BW, TX aborted\n");
                        json_value_free(root_val);
                        continue;
                }

                /* Parse ECC coding rate (optional field) */
                str = json_object_get_string(txpk_obj, "codr");
                if (str == NULL) 
				{
                    MSG("WARNING: [down] no mandatory \"txpk.codr\" object in json, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                if      (strcmp(str, "4/5") == 0) txpkt.coderate = CR_LORA_4_5;
                else if (strcmp(str, "4/6") == 0) txpkt.coderate = CR_LORA_4_6;
                else if (strcmp(str, "2/3") == 0) txpkt.coderate = CR_LORA_4_6;
                else if (strcmp(str, "4/7") == 0) txpkt.coderate = CR_LORA_4_7;
                else if (strcmp(str, "4/8") == 0) txpkt.coderate = CR_LORA_4_8;
                else if (strcmp(str, "1/2") == 0) txpkt.coderate = CR_LORA_4_8;
                else {
                    MSG("WARNING: [down] format error in \"txpk.codr\", TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }

                /* Parse signal polarity switch (optional field) */
                val = json_object_get_value(txpk_obj,"ipol");
                if (val != NULL) {
                    txpkt.invert_pol = (bool)json_value_get_boolean(val);
                }

                /* parse Lora preamble length (optional field, optimum min value enforced) */
                val = json_object_get_value(txpk_obj,"prea");
                if (val != NULL) {
                    i = (int)json_value_get_number(val);
                    if (i >= MIN_LORA_PREAMB) {
                        txpkt.preamble = (uint16_t)i;
                    } else {
                        txpkt.preamble = (uint16_t)MIN_LORA_PREAMB;
                    }
                } else {
                    txpkt.preamble = (uint16_t)STD_LORA_PREAMB;
                }

            } else if (strcmp(str, "FSK") == 0) {
                /* FSK modulation */
                txpkt.modulation = MOD_FSK;

                /* parse FSK bitrate (mandatory) */
                val = json_object_get_value(txpk_obj,"datr");
                if (val == NULL) {
                    MSG("WARNING: [down] no mandatory \"txpk.datr\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                txpkt.datarate = (uint32_t)(json_value_get_number(val));

                /* parse frequency deviation (mandatory) */
                val = json_object_get_value(txpk_obj,"fdev");
                if (val == NULL) {
                    MSG("WARNING: [down] no mandatory \"txpk.fdev\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                txpkt.f_dev = (uint8_t)(json_value_get_number(val) / 1000.0); /* JSON value in Hz, txpkt.f_dev in kHz */

                /* parse FSK preamble length (optional field, optimum min value enforced) */
                val = json_object_get_value(txpk_obj,"prea");
                if (val != NULL) {
                    i = (int)json_value_get_number(val);
                    if (i >= MIN_FSK_PREAMB) {
                        txpkt.preamble = (uint16_t)i;
                    } else {
                        txpkt.preamble = (uint16_t)MIN_FSK_PREAMB;
                    }
                } else {
                    txpkt.preamble = (uint16_t)STD_FSK_PREAMB;
                }

            } else {
                MSG("WARNING: [down] invalid modulation in \"txpk.modu\", TX aborted\n");
                json_value_free(root_val);
                continue;
            }

            /* Parse payload length (mandatory) */
            val = json_object_get_value(txpk_obj,"size");
            if (val == NULL) {
                MSG("WARNING: [down] no mandatory \"txpk.size\" object in JSON, TX aborted\n");
                json_value_free(root_val);
                continue;
            }
            txpkt.size = (uint16_t)json_value_get_number(val);

            /* Parse payload data (mandatory) */
            str = json_object_get_string(txpk_obj, "data");
            if (str == NULL) {
                MSG("WARNING: [down] no mandatory \"txpk.data\" object in JSON, TX aborted\n");
                json_value_free(root_val);
                continue;
            }
            i = b64_to_bin(str, strlen(str), txpkt.payload, sizeof txpkt.payload);
            if (i != txpkt.size) {
                MSG("WARNING: [down] mismatch between .size and .data size once converter to binary\n");
            }

            /* free the JSON parse tree from memory */
            json_value_free(root_val);

            /* select TX mode */
            if (sent_immediate) {
                txpkt.tx_mode = IMMEDIATE;
            } else {
                txpkt.tx_mode = TIMESTAMPED;
            }

            /* record measurement data */
            pthread_mutex_lock(&mx_meas_dw);
            meas_dw_dgram_rcv += 1; /* count only datagrams with no JSON errors */
            meas_dw_network_byte += msg_len; /* meas_dw_network_byte */
            meas_dw_payload_byte += txpkt.size;
            pthread_mutex_unlock(&mx_meas_dw);

            /* check TX parameter before trying to queue packet */
            jit_result = JIT_ERROR_OK;
            if ((txpkt.freq_hz < tx_freq_min[txpkt.rf_chain]) || (txpkt.freq_hz > tx_freq_max[txpkt.rf_chain])) {
                jit_result = JIT_ERROR_TX_FREQ;
                MSG("ERROR: Packet REJECTED, unsupported frequency - %u (min:%u,max:%u)\n", txpkt.freq_hz, tx_freq_min[txpkt.rf_chain], tx_freq_max[txpkt.rf_chain]);
            }
            if (jit_result == JIT_ERROR_OK) {
                for (i=0; i<txlut.size; i++) {
                    if (txlut.lut[i].rf_power == txpkt.rf_power) {
                        /* this RF power is supported, we can continue */
                        break;
                    }
                }
                if (i == txlut.size) {
                    /* this RF power is not supported */
                    jit_result = JIT_ERROR_TX_POWER;
                    MSG("ERROR: Packet REJECTED, unsupported RF power for TX - %d\n", txpkt.rf_power);
                }
            }

            /* insert packet to be sent into JIT queue */
            if (jit_result == JIT_ERROR_OK) {
                gettimeofday(&current_unix_time, NULL);
                get_concentrator_time(&current_concentrator_time, current_unix_time);
                jit_result = jit_enqueue(&jit_queue, &current_concentrator_time, &txpkt, downlink_type);
                if (jit_result != JIT_ERROR_OK) {
                    printf("ERROR: Packet REJECTED (jit error=%d)\n", jit_result);
                }
                pthread_mutex_lock(&mx_meas_dw);
                meas_nb_tx_requested += 1;
                pthread_mutex_unlock(&mx_meas_dw);
            }

            /* Send acknoledge datagram to server */
            send_tx_ack(buff_down[1], buff_down[2], jit_result);
        }
    }
    MSG("\nINFO: End of downstream thread\n");
}

void print_tx_status(uint8_t tx_status) 
{
    switch (tx_status) {
        case TX_OFF:
            DEBUG_CONF("INFO: [jit] lgw_status returned TX_OFF\n");
            break;
        case TX_FREE:
            DEBUG_CONF("INFO: [jit] lgw_status returned TX_FREE\n");
            break;
        case TX_EMITTING:
            DEBUG_CONF("INFO: [jit] lgw_status returned TX_EMITTING\n");
            break;
        case TX_SCHEDULED:
            DEBUG_CONF("INFO: [jit] lgw_status returned TX_SCHEDULED\n");
            break;
        default:
            DEBUG_CONF("INFO: [jit] lgw_status returned UNKNOWN (%d)\n", tx_status);
            break;
    }
}


/* -------------------------------------------------------------------------- */
/* --- THREAD 3: CHECKING PACKETS TO BE SENT FROM JIT QUEUE AND SEND THEM --- */

void thread_jit(void) 
{
    int result = LGW_HAL_SUCCESS;
    struct lgw_pkt_tx_s pkt;
    int pkt_index = -1;
    struct timeval current_unix_time;
    struct timeval current_concentrator_time;
    enum jit_error_e jit_result;
    enum jit_pkt_type_e pkt_type;
    uint8_t tx_status;

    while (!exit_sig && !quit_sig) {
        wait_ms(10);

        /* transfer data and metadata to the concentrator, and schedule TX */
        gettimeofday(&current_unix_time, NULL);
        get_concentrator_time(&current_concentrator_time, current_unix_time);
        jit_result = jit_peek(&jit_queue, &current_concentrator_time, &pkt_index);
        if (jit_result == JIT_ERROR_OK) {
            if (pkt_index > -1) {
                jit_result = jit_dequeue(&jit_queue, pkt_index, &pkt, &pkt_type);
                if (jit_result == JIT_ERROR_OK) {
                    /* update beacon stats */
                    if (pkt_type == JIT_PKT_TYPE_BEACON) {
                        /* Compensate breacon frequency with xtal error */
                        pthread_mutex_lock(&mx_xcorr);
                        pkt.freq_hz = (uint32_t)(xtal_correct * (double)pkt.freq_hz);
                        MSG_DEBUG(DEBUG_BEACON, "beacon_pkt.freq_hz=%u (xtal_correct=%.15lf)\n", pkt.freq_hz, xtal_correct);
                        pthread_mutex_unlock(&mx_xcorr);

                        /* Update statistics */
                        pthread_mutex_lock(&mx_meas_dw);
                        meas_nb_beacon_sent += 1;
                        pthread_mutex_unlock(&mx_meas_dw);
                        MSG("INFO: Beacon dequeued (count_us=%u)\n", pkt.count_us);
                    }

                    /* check if concentrator is free for sending new packet */
                    pthread_mutex_lock(&mx_concent); /* may have to wait for a fetch to finish */
                    result = lgw_status(TX_STATUS, &tx_status);
                    pthread_mutex_unlock(&mx_concent); /* free concentrator ASAP */
                    if (result == LGW_HAL_ERROR) {
                        MSG("WARNING: [jit] lgw_status failed\n");
                    } else {
                        if (tx_status == TX_EMITTING) {
                            MSG("ERROR: concentrator is currently emitting\n");
                            print_tx_status(tx_status);
                            continue;
                        } else if (tx_status == TX_SCHEDULED) {
                            MSG("WARNING: a downlink was already scheduled, overwritting it...\n");
                            print_tx_status(tx_status);
                        } else {
                            /* Nothing to do */
                        }
                    }

                    /* send packet to concentrator */
                    pthread_mutex_lock(&mx_concent); /* may have to wait for a fetch to finish */
                    result = lgw_send(pkt);
                    pthread_mutex_unlock(&mx_concent); /* free concentrator ASAP */
                    if (result == LGW_HAL_ERROR) {
                        pthread_mutex_lock(&mx_meas_dw);
                        meas_nb_tx_fail += 1;
                        pthread_mutex_unlock(&mx_meas_dw);
                        MSG("WARNING: [jit] lgw_send failed\n");
                        continue;
                    } else {
                        pthread_mutex_lock(&mx_meas_dw);
                        meas_nb_tx_ok += 1;
                        pthread_mutex_unlock(&mx_meas_dw);
                        MSG_DEBUG(DEBUG_PKT_FWD, "lgw_send done: count_us=%u\n", pkt.count_us);
                    }
                } else {
                    MSG("ERROR: jit_dequeue failed with %d\n", jit_result);
                }
            }
        } else if (jit_result == JIT_ERROR_EMPTY) {
            /* Do nothing, it can happen */
        } else {
            MSG("ERROR: jit_peek failed with %d\n", jit_result);
        }
    }
}

/* -------------------------------------------------------------------------- */
/* --- THREAD 4: PARSE GPS MESSAGE AND KEEP GATEWAY IN SYNC ----------------- */

static void gps_process_sync(void) 
{
    struct timespec gps_time;
    struct timespec utc;
    uint32_t trig_tstamp; /* concentrator timestamp associated with PPM pulse */
    int i = lgw_gps_get(&utc, &gps_time, NULL, NULL);

    /* get GPS time for synchronization */
    if (i != LGW_GPS_SUCCESS) {
        MSG("WARNING: [gps] could not get GPS time from GPS\n");
        return;
    }

    /* get timestamp captured on PPM pulse  */
    pthread_mutex_lock(&mx_concent);
    i = lgw_get_trigcnt(&trig_tstamp);
    pthread_mutex_unlock(&mx_concent);
    if (i != LGW_HAL_SUCCESS) {
        MSG("WARNING: [gps] failed to read concentrator timestamp\n");
        return;
    }

    /* try to update time reference with the new GPS time & timestamp */
    pthread_mutex_lock(&mx_timeref);
    i = lgw_gps_sync(&time_reference_gps, trig_tstamp, utc, gps_time);
    pthread_mutex_unlock(&mx_timeref);
    if (i != LGW_GPS_SUCCESS) {
        MSG("WARNING: [gps] GPS out of sync, keeping previous time reference\n");
    }
}

static void gps_process_coords(void) 
{
    /* position variable */
    struct coord_s coord;
    struct coord_s gpserr;
    int    i = lgw_gps_get(NULL, NULL, &coord, &gpserr);

    /* update gateway coordinates */
    pthread_mutex_lock(&mx_meas_gps);
    if (i == LGW_GPS_SUCCESS) {
        gps_coord_valid = true;
        meas_gps_coord = coord;
        meas_gps_err = gpserr;
        // TODO: report other GPS statistics (typ. signal quality & integrity)
    } else {
        gps_coord_valid = false;
    }
    pthread_mutex_unlock(&mx_meas_gps);
}

void thread_gps(void) 
{
    /* serial variables */
    char serial_buff[128]; /* buffer to receive GPS data */
    size_t wr_idx = 0;     /* pointer to end of chars in buffer */

    /* variables for PPM pulse GPS synchronization */
    enum gps_msg latest_msg; /* keep track of latest NMEA message parsed */

    /* initialize some variables before loop */
    memset(serial_buff, 0, sizeof serial_buff);

    while (!exit_sig && !quit_sig) {
        size_t rd_idx = 0;
        size_t frame_end_idx = 0;

        /* blocking non-canonical read on serial port */
        ssize_t nb_char = read(gps_tty_fd, serial_buff + wr_idx, LGW_GPS_MIN_MSG_SIZE);
        if (nb_char <= 0) {
            MSG("WARNING: [gps] read() returned value %d\n", nb_char);
            continue;
        }
        wr_idx += (size_t)nb_char;

        /*******************************************
         * Scan buffer for UBX/NMEA sync chars and *
         * attempt to decode frame if one is found *
         *******************************************/
        while(rd_idx < wr_idx) {
            size_t frame_size = 0;

            /* Scan buffer for UBX sync char */
            if(serial_buff[rd_idx] == (char)LGW_GPS_UBX_SYNC_CHAR) {

                /***********************
                 * Found UBX sync char *
                 ***********************/
                latest_msg = lgw_parse_ubx(&serial_buff[rd_idx], (wr_idx - rd_idx), &frame_size);

                if (frame_size > 0) {
                    if (latest_msg == INCOMPLETE) {
                        /* UBX header found but frame appears to be missing bytes */
                        frame_size = 0;
                    } else if (latest_msg == INVALID) {
                        /* message header received but message appears to be corrupted */
                        MSG("WARNING: [gps] could not get a valid message from GPS (no time)\n");
                        frame_size = 0;
                    } else if (latest_msg == UBX_NAV_TIMEGPS) {
                        gps_process_sync();
                    }
                }
            } else if(serial_buff[rd_idx] == LGW_GPS_NMEA_SYNC_CHAR) {
                /************************
                 * Found NMEA sync char *
                 ************************/
                /* scan for NMEA end marker (LF = 0x0a) */
                char* nmea_end_ptr = memchr(&serial_buff[rd_idx],(int)0x0a, (wr_idx - rd_idx));

                if(nmea_end_ptr) {
                    /* found end marker */
                    frame_size = nmea_end_ptr - &serial_buff[rd_idx] + 1;
                    latest_msg = lgw_parse_nmea(&serial_buff[rd_idx], frame_size);

                    if(latest_msg == INVALID || latest_msg == UNKNOWN) {
                        /* checksum failed */
                        frame_size = 0;
                    } else if (latest_msg == NMEA_RMC) { /* Get location from RMC frames */
                        gps_process_coords();
                    }
                }
            }

            if(frame_size > 0) {
                /* At this point message is a checksum verified frame
                   we're processed or ignored. Remove frame from buffer */
                rd_idx += frame_size;
                frame_end_idx = rd_idx;
            } else {
                rd_idx++;
            }
        } /* ...for(rd_idx = 0... */

        if(frame_end_idx) {
          /* Frames have been processed. Remove bytes to end of last processed frame */
          memcpy(serial_buff, &serial_buff[frame_end_idx], wr_idx - frame_end_idx);
          wr_idx -= frame_end_idx;
        } /* ...for(rd_idx = 0... */

        /* Prevent buffer overflow */
        if((sizeof(serial_buff) - wr_idx) < LGW_GPS_MIN_MSG_SIZE) {
            memcpy(serial_buff, &serial_buff[LGW_GPS_MIN_MSG_SIZE], wr_idx - LGW_GPS_MIN_MSG_SIZE);
            wr_idx -= LGW_GPS_MIN_MSG_SIZE;
        }
    }
    MSG("\nINFO: End of GPS thread\n");
}

/* -------------------------------------------------------------------------- */
/* --- THREAD 5: CHECK TIME REFERENCE AND CALCULATE XTAL CORRECTION --------- */

void thread_valid(void) 
{

    /* GPS reference validation variables */
    long gps_ref_age = 0;
    bool ref_valid_local = false;
    double xtal_err_cpy;

    /* variables for XTAL correction averaging */
    unsigned init_cpt = 0;
    double init_acc = 0.0;
    double x;

    /* correction debug */
    // FILE * log_file = NULL;
    // time_t now_time;
    // char log_name[64];

    /* initialization */
    // time(&now_time);
    // strftime(log_name,sizeof log_name,"xtal_err_%Y%m%dT%H%M%SZ.csv",localtime(&now_time));
    // log_file = fopen(log_name, "w");
    // setbuf(log_file, NULL);
    // fprintf(log_file,"\"xtal_correct\",\"XERR_INIT_AVG %u XERR_FILT_COEF %u\"\n", XERR_INIT_AVG, XERR_FILT_COEF); // DEBUG

    /* main loop task */
    while (!exit_sig && !quit_sig) {
        wait_ms(1000);

        /* calculate when the time reference was last updated */
        pthread_mutex_lock(&mx_timeref);
        gps_ref_age = (long)difftime(time(NULL), time_reference_gps.systime);
        if ((gps_ref_age >= 0) && (gps_ref_age <= GPS_REF_MAX_AGE)) {
            /* time ref is ok, validate and  */
            gps_ref_valid = true;
            ref_valid_local = true;
            xtal_err_cpy = time_reference_gps.xtal_err;
            //printf("XTAL err: %.15lf (1/XTAL_err:%.15lf)\n", xtal_err_cpy, 1/xtal_err_cpy); // DEBUG
        } else {
            /* time ref is too old, invalidate */
            gps_ref_valid = false;
            ref_valid_local = false;
        }
        pthread_mutex_unlock(&mx_timeref);

        /* manage XTAL correction */
        if (ref_valid_local == false) {
            /* couldn't sync, or sync too old -> invalidate XTAL correction */
            pthread_mutex_lock(&mx_xcorr);
            xtal_correct_ok = false;
            xtal_correct = 1.0;
            pthread_mutex_unlock(&mx_xcorr);
            init_cpt = 0;
            init_acc = 0.0;
        } else {
            if (init_cpt < XERR_INIT_AVG) {
                /* initial accumulation */
                init_acc += xtal_err_cpy;
                ++init_cpt;
            } else if (init_cpt == XERR_INIT_AVG) {
                /* initial average calculation */
                pthread_mutex_lock(&mx_xcorr);
                xtal_correct = (double)(XERR_INIT_AVG) / init_acc;
                //printf("XERR_INIT_AVG=%d, init_acc=%.15lf\n", XERR_INIT_AVG, init_acc);
                xtal_correct_ok = true;
                pthread_mutex_unlock(&mx_xcorr);
                ++init_cpt;
                // fprintf(log_file,"%.18lf,\"average\"\n", xtal_correct); // DEBUG
            } else {
                /* tracking with low-pass filter */
                x = 1 / xtal_err_cpy;
                pthread_mutex_lock(&mx_xcorr);
                xtal_correct = xtal_correct - xtal_correct/XERR_FILT_COEF + x/XERR_FILT_COEF;
                pthread_mutex_unlock(&mx_xcorr);
                // fprintf(log_file,"%.18lf,\"track\"\n", xtal_correct); // DEBUG
            }
        }
        // printf("Time ref: %s, XTAL correct: %s (%.15lf)\n", ref_valid_local?"valid":"invalid", xtal_correct_ok?"valid":"invalid", xtal_correct); // DEBUG
    }
    MSG("\nINFO: End of validation thread\n");
}
/* --- EOF ------------------------------------------------------------------ */
