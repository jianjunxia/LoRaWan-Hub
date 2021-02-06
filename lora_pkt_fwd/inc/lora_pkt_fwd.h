// Lierda | Senthink
// Jianjun_xia 
// date: 2018.10.11

#ifndef  _LORA_PKT_FWD_H    
#define  _LORA_PKT_FWD_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

//======================LORAWAN 解析头文件=========================================//

#ifndef _LORAWAN_PARSE_H
#define _LORAWAN_PARSE_H

#define false  0
#define true   1
//#define bool   int;

/*LoRaMAC头字段定义
* LoRaMAC header field definition(MHDR field)
* LoRaWAN Specification V1.0.2,chapter 4.2*
*
*/
/* return status code */
#define LGW_HAL_SUCCESS      0
#define LGW_HAL_ERROR       -1
#define LGW_LBT_ISSUE        1

/* values available for the 'modulation' parameters */
/* NOTE: arbitrary values */
#define MOD_UNDEFINED   0
#define MOD_LORA        0x10
#define MOD_FSK         0x20

/* values available for the 'bandwidth' parameters (LoRa & FSK) */
/* NOTE: directly encode FSK RX bandwidth, do not change */
#define BW_UNDEFINED    0
#define BW_500KHZ       0x01
#define BW_250KHZ       0x02
#define BW_125KHZ       0x03
#define BW_62K5HZ       0x04
#define BW_31K2HZ       0x05
#define BW_15K6HZ       0x06
#define BW_7K8HZ        0x07

/* values available for the 'datarate' parameters */
/* NOTE: LoRa values used directly to code SF bitmask in 'multi' modem, do not change */
#define DR_UNDEFINED    0
#define DR_LORA_SF7     0x02
#define DR_LORA_SF8     0x04
#define DR_LORA_SF9     0x08
#define DR_LORA_SF10    0x10
#define DR_LORA_SF11    0x20
#define DR_LORA_SF12    0x40
#define DR_LORA_MULTI   0x7E
/* NOTE: for FSK directly use baudrate between 500 bauds and 250 kbauds */
#define DR_FSK_MIN      500
#define DR_FSK_MAX      250000

/* values available for the 'coderate' parameters (LoRa only) */
/* NOTE: arbitrary values */
#define CR_UNDEFINED    0
#define CR_LORA_4_5     0x01
#define CR_LORA_4_6     0x02
#define CR_LORA_4_7     0x03
#define CR_LORA_4_8     0x04

/* values available for the 'status' parameter */
/* NOTE: values according to hardware specification */
#define STAT_UNDEFINED  0x00
#define STAT_NO_CRC     0x01
#define STAT_CRC_BAD    0x11
#define STAT_CRC_OK     0x10

/* values available for the 'tx_mode' parameter */
#define IMMEDIATE       0
#define TIMESTAMPED     1
#define ON_GPS          2
//#define ON_EVENT      3
//#define GPS_DELAYED   4
//#define EVENT_DELAYED 5

/* values available for 'select' in the status function */
#define TX_STATUS       1
#define RX_STATUS       2

/* status code for TX_STATUS */
/* NOTE: arbitrary values */
#define TX_STATUS_UNKNOWN   0
#define TX_OFF              1    /* TX modem disabled, it will ignore commands */
#define TX_FREE             2    /* TX modem is free, ready to receive a command */
#define TX_SCHEDULED        3    /* TX modem is loaded, ready to send the packet after an event and/or delay */
#define TX_EMITTING         4    /* TX modem is emitting */

/* status code for RX_STATUS */
/* NOTE: arbitrary values */
#define RX_STATUS_UNKNOWN   0
#define RX_OFF              1    /* RX modem is disabled, it will ignore commands  */
#define RX_ON               2    /* RX modem is receiving */
#define RX_SUSPENDED        3    /* RX is suspended while a TX is ongoing */

/* Maximum size of Tx gain LUT */
#define TX_GAIN_LUT_SIZE_MAX 16

/* LBT constants */
#define LBT_CHANNEL_FREQ_NB 8 /* Number of LBT channels */

/**
 * unconfirmed/confirmed data序列化之后上传到lora_pkt_server.c的数据结构体
 */
typedef struct serialization_data
{
    uint8_t  data[1024];
    uint16_t length;
}Serialization_data_type;

/**
 * 网关层将解析后的数据上报给lora_pkt_conf.c
 */ 
typedef struct decode_data
{
    uint8_t  data[1024];
    uint16_t length;
}Decode_data;


//mhdr结构体
typedef union uLoRaMacHeader
{
    uint8_t Value;

    struct sHdrBits
    {
       uint8_t  Major    :2;
       uint8_t  RFU      :3;
       uint8_t  MType    :3;
    }Bits;

}LoRaMacHeader_t;

/** LoRaWAN的帧类型
 * LoRaMAC frame types
 * LoRaWAN Specification V1.0.2,chapter 4.2.1,table1
 *
 */
 typedef enum eLoRaMacFrameType
 {
    FRAME_TYPE_JOIN_REQ               = 0X00,

    FRAME_TYPE_JOIN_ACCEPT            = 0X01,

    FRAME_TYPE_DATA_UNCONFIRMED_UP    = 0X02,

    FRAME_TYPE_DATA_UNCONFIRMED_DOWN  = 0X03,

    FRAME_TYPE_DATA_CONFIRMED_UP      = 0X04,

    FRAME_TYPE_DATA_CONFIRMED_DOWN    = 0X05,

    FRAME_TYPE_RFU                    = 0X06,

    FRAME_TYPE_PROPRIETARY            = 0X07,

 }LoRaMacFrameType_t;

/** LoRaWAN的FCtrl 数据
 *  LoRaWAN frame control definition(FCtrl)
 *
 *  LoRaWAN Specification V1.0.2,chapter 4.2.1,table1
 */

 //UPlink Frame
 typedef union uLoRaMacFrameCtrl
 {
    uint8_t Value;

    struct sCtrlBits
    {

      uint8_t FOptsLen   :4;

     // 上行数据中有RFU
     // 下行数据是 FPending
      uint8_t RFU        :1;

      uint8_t Ack        :1;

      uint8_t AdrAckReq  :1;

      uint8_t Adr        :1;

    }Bits;
 }LoRaMacFrameCtrl_t;

  //Down link Frame
 typedef union uLoRaMacDownFrameCtrl
 {
    uint8_t Value;

    struct sDownCtrlBits
    {

      uint8_t FOptsLen   :4;

     // 上行数据中有RFU
     // 下行数据是 FPending
      uint8_t FPending   :1;

      uint8_t Ack        :1;

      uint8_t RFU        :1;

      uint8_t Adr        :1;

    }DownBits;
 }LoRaMacFrameDownCtrl_t;

//添加DLSettings字段
typedef union Join_uDLSettings
{
    uint8_t Value;
    struct  uDLSettings
    {
        uint8_t  RX2DataRate  :4;
        uint8_t  RX1DRoffset  :3;
        uint8_t  RFU          :1;

    }uDLSettings_t;
}Join_uDLSettings_t;

 typedef struct LoRaDownPktData
 {

    LoRaMacHeader_t MHDR;
    //3bytes
    uint8_t AppNonce1;
    uint8_t AppNonce2;
    uint8_t AppNonce3;
    //3bytes
    uint8_t NetID1;
    uint8_t NetID2;
    uint8_t NetID3;

    uint32_t Devaddr;
    Join_uDLSettings_t  DLSettings;
    uint8_t  RxDelay;

 }Join_Accept;
#endif

//==============================================================================//

//============================LORAWAN解析使用的变量============================//
#define  LORAMAC_MFR_LEN            4
#define  UP_LINK                    0
#define  DOWN_LINK                  1
#define  LORAMAC_PHY_MAXPAYLOAD     255
#define  TX_BYTE_LEN				300
#define  PORT_NUMBER   				8888

#define  Com_MIC_Size       		13
#define  Join_Accept_Len   			17

#define IMMEDIATE       			0
#define TIMESTAMPED    			 	1
#define ON_GPS          			2
#define TX_RF_CHAIN                 0    /* TX only supported on radio A */
/* --- PRIVATE CONSTANTS ---------------------------------------------------- */

#define TX_RF_CHAIN                 0 /* TX only supported on radio A */
#define DEFAULT_RSSI_OFFSET         0.0
#define DEFAULT_MODULATION          "LORA"
#define DEFAULT_BR_KBPS             50
#define DEFAULT_FDEV_KHZ            25
#define DEFAULT_NOTCH_FREQ          129000U /* 129 kHz */
#define DEFAULT_SX127X_RSSI_OFFSET  -4 /* dB */

/* -------------------------------------------------------------------------- */

//==========================存储KEY值的缓存区==================================//

/**
 * AES encryption/decryption network session key
 */
uint8_t LoRaMacNwkSKey[16]=
{
    0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,
    0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00
};
/**
 * AES encryption/decryption cipher session key
 */
uint8_t LoRaMacAppSKey[16]=
{
    0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,
    0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00
};

//公共的信息
uint8_t AppKey[16]=
{
    0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00,
    0X00,0X00,0X00,0X00,0X00,0X00,0X00,0X00
};

uint8_t LoRaMacRxPayload[LORAMAC_PHY_MAXPAYLOAD];
//===========================================================================//

//===========================数据库表使用的一些宏定义============================//
#define     uint8_t             unsigned char
#define     uint16_t            unsigned short int
#define     uint32_t            unsigned int
#define     uint64_t            unsigned long long  
#define     MAX_ID              1000  
#define     MALLOC_SIZE         100
#define     DEVEUI_LEN          8
#define     APPEUI_LEN          8
#define     APPKEY_LEN          16
#define     NWKSKEY_LEN         16
#define     APPSKEY_LEN         16
#define     DEVADDR_LEN         4
#define     ADDRESS_IDENTICAL   1
#define     ADDRESS_DIFFERENT   0

//定义一个宏，用于检测每次执行完sqlite3_exec()后的rc值
//注意警告问题可能是\后面多了空格
#define CHECK_RC(rc,zErrMsg,db)     if(rc != SQLITE_OK)\
                            {\
                                fprintf(stderr,"SQL error: %s\n",zErrMsg);\
                                sqlite3_free(zErrMsg);\
                            }

//=====================================与lora_pkt_server本地传输数据的一些定义========================//
//清除载荷时使用
#define     PAYLOAD_LEN      256
//上行通道起始频率 channel 0~95 470.3MHZ~489.3MHZ
#define     UPSTRAM          470300000
//下行通道起始频率 channel 0~47 500.3MHZ~509.7MHZ
#define     DOWNSTRAM        500300000
//增长频率200khz
#define     INCREMENT        200000
//与lora_pkt_server的通信端口
#define     PKT_SERVER_PORT  5555
//本地IP loopback
#define     PKT_SERVER_IP    "127.0.0.1"

//====================================================================================================//

//=============================================函数声明区域=============================================//
//函数具体实现参考：lora_pkt_fwd.c

//LoRaWAN协议解析
void LoRaWAN_Parse(struct lgw_pkt_rx_s *rxbuff,int nb_pkt,sqlite3 *db,char *zErrMsg);
//LoRaWAN join数据序列化
int LoRaWAN_Join_Serialization(struct lgw_pkt_rx_s *rxbuff,uint8_t *send_buff);
//LoRaWAN 上报的数据序列化
int LoRaWAN_Data_Serialization(struct lgw_pkt_rx_s *rxbuff,uint8_t *send_buff);

//=====================================数据库使用函数声明========================================//
//回调函数
int callback(void *NotUsed,int argc,char**argv,char**azColName);
//模拟插入GW的设备信息
void Insert_GwInfo_Table(sqlite3 *db,char *zErrMsg);



/**
 * brief: 查询GW的设备信息DEVEUI 
 *        如果表中存在这个节点,则把该节点原来的devaddr, nwkskey, appskey, appeui进行覆盖
 * 
 * update: 
 *        2018.10.11在数据库中增加了appeui
 * 
 */
void Update_GwInfo_Table(sqlite3 *db,
                         char *zErrMsg,
                         uint8_t *deveui,
                         uint8_t *nwkskey,
                         uint8_t *appskey,
                         uint8_t *devaddr,
                         uint8_t *appeui 
                        );

//解析数据时使用
//从表中查找是否有对应的deveui的值
//从表中读取devaddr对应的nwkskey和appskey用于解析lorawan数据
//brief:从表中查询对应的deveui,并将提取出的数据存入到parse_new_nwkskey[],parse_new_appskey[]
void Parse_GwInfo_Table(sqlite3 *db,
                        uint32_t *address, 
                        uint8_t parse_new_nwkskey[],
                        uint8_t parse_new_appskey[],
                        uint8_t data_deveui[],
                        uint8_t data_appeui[]
                        );

//防止地址碰撞
//如果地址相同则返回1
//如果地址不同则返回0
int Prevent_Addr_Identical(sqlite3 *db,uint32_t *address);

//每次从上报的DEVEUI中读取APPKEY
//便于后面生成appskey 和nwkskey使用
int Fetch_Appkey_Table(sqlite3 *db,uint8_t *deveui,uint8_t Appkey[]);

//使用地址取nwkskey和appskey
//便于后面生成appskey 和nwkskey使用
int Fetch_Nwkskey_Appskey_Table(sqlite3 *db,uint32_t *address,uint8_t *nwkskey,uint8_t *appskey);

//当有join数据上报时，检查数据库中书否含有该deveui
//如果没有则跳出本次处理，节省处理时间
int Check_Deveui(sqlite3 *db,uint8_t *deveui);

//当有数据上报时，检查数据库中书否含有该devaddr
//如果没有则跳出本次处理，节省处理时间
int Check_Devaddr(sqlite3 *db,uint32_t *address);

//根据上报的地址取对应的fcntup
int Fetch_GWinfo_FcntUp(sqlite3 *db,uint32_t *address,uint32_t *fcntup);

//根据上报的地址取对应的fcntdown
int Fetch_GWinfo_FcntDown(sqlite3 *db,uint32_t *address,uint32_t *fcntdown);

//更新新的fcnt_up到数据库中
int Update_GWinfo_FcntUp(sqlite3 *db,char *zErrMsg,uint32_t *address,uint32_t *fcntup);

//更新新的fcnt_down到数据库中
int Update_GWinfo_FcntDown(sqlite3 *db,char *zErrMsg,uint32_t *address,uint32_t *fcntdown);

//清除deveui对应的fcnt_up的值
int Clear_GWinfo_FcntUp(sqlite3 *db,char *zErrMsg,uint8_t deveui[]);

//清除deveui对应的fcnt_down的值
int Clear_GWinfo_FcntDown(sqlite3 *db,char *zErrMsg,uint8_t deveui[]);

//根据服务器下发的class c数据的deveui取出对应的rx2_freq 和该设备的devaddr
int Fetch_Rx2_Freq(sqlite3 *db, const uint8_t *deveui,uint32_t *rx2_freq,uint32_t *devaddr);

/* update: 2019.3.12 */
/**
 *  增加计算 TOA 时间 
 *  参考官网代码: lora_gateway-master/loragw_hal.c/ lgw_time_on_air()
 *  parme[in]: 接受数据包结构体指针
 *  return:    toa时间 us
 */
uint32_t rx_lgw_time_on_air(struct lgw_pkt_rx_s *packet);

#if 1
//update:2018.12.21 增加CN470-510异频下发频率的函数
//parameter 上行频率
//return: 下行频率
uint32_t RegionCN470AsynchronyTxFrequency(uint32_t frequency);
//异频下发的datarate
//uint8_t RegionCN470ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );
#endif

/*
    @ update    ： 2019.06.14
    @ brief     ： 下发class a 的调试数据
    @ parameter ： sqlite3数据库、上行频率、带宽、速率、下发时间、
                   载荷、队列项数、第几包  
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
                                
                            );


#define CN470_FIRST_UpLink_Frequency                     ( (uint32_t) 470300000 )

#define CN470_LAST_UpLink_Frequency                       ( (uint32_t) 489300000 )

/*!
 * Defines the first channel for RX window 1 for CN470 band
 */
#define CN470_FIRST_DownLink_Frequency                     ( (uint32_t) 500300000 )

/*!
 * Defines the last channel for RX window 1 for CN470 band
 */
#define CN470_LAST_DownLink_Frequency                       ( (uint32_t) 509700000 )

/*!
 * Defines the step width of the channels for RX window 1
 */
#define CN470_STEPWIDTH_Frequency                              ( (uint32_t) 200000 )

#define  DR_0                                                  (0)    


#endif

