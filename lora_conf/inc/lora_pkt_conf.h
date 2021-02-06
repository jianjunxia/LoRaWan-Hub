/*
    lierda | senthink
    jianjun_xia 
    Date: 2018.10.11
*/

#ifndef  _LORA_PKT_CONF_H_    
#define  _LORA_PKT_CONF_H_
    
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 定义udp接收数据端口为6789  */
#define  UDP_PORT             6789    
/* 定义udp发送数据端口为8080  */
#define  UDP_SEND_PORT        8080

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//+                                   CMD  命令表                                            +
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//+     01  广播命令                                                                         +      
//+     02  应答广播命令                                                                     +        
//+     03  读取网关上所有的节点信息命令                                                     +
//+     04  应答读取网关上所有的节点信息命令                                                 +
//+     05  读单个节点信息命令                                                               +
//+     06  应答读单个节点信息命令                                                           +
//+     07  写单个节点信息命令                                                               +
//+     08  应答写单个节点信息命令                                                           +
//+     09  读SX1301配置信息命令                                                             + 
//+     0A  应答读SX1301配置信息命令                                                         +
//+     0B  写SX1301配置信息命令                                                             +
//+     0C  应答写SX1301配置信息命令                                                         +
//+     0D  读远程服务器配置命令                                                             +
//+     0E  应答读远程服务器配置命令                                                         +
//+     OF  写远程服务器配置命令                                                             +
//+     10  应答写远程服务器配置命令                                                         +
//+     11  删除单个节点信息命令                                                             +
//+     12  应答删除单个节点信息命令                                                         +
//+     13  删除所有节点信息命令                                                             +
//+     14  应答删除所有节点信息命令                                                          +
//+     update: 2018.11.23                                                                 + 
//+     15  获取所有节点的rx2_frequency命令                                                  +
//+     16  应答获取所有节点的rx2_frewquency命令                                              +  
//+     17  配置所有节点的rx2_frequency命令                                                  +      
//+     18  应答配置所有节点的rx2_frewquency命令                                             +                                                  
//+     update: 2018.12.06                                                                +
//+     19  发送配置区域频段信息命令                                                          +  
//+     1A  应答发送配置区域频段信息命令                                                      +  
//+     1B  发生HUB上行信道频率列表                                                         +    
//+     1C  应答发送HUB上行信道频率列表                                                     +
//+     1D  发送HUB下行信道频率列表                                                        + 
//+     1E  应答发送HUB下行信道频率列表                                                     +
//+     update: 2018.12.07                                                               +
//+     1F  发送配置Rx1DRoffset信息命令                                                   +              
//+     20  应答发送配置Rx1DRoffset信息命令                                                 +   
//+     update: 2018.12.11                                                               +        
//+     21  获取频段信息命令                                                              +
//+     22  应答获取频段信息命令                                                            +
//+     23  获取HUB上行信道频率信息                                                        +
//+     24  应答获取HUB上行信道频率信息                                                      +
//+     25  获取HUB下行信道频率信息                                                        +
//+     26  应答获取HUB下行信道频率信息                                                     +
//+     27  获取Rx1DRoffset信息命令                                                         +
//+     28  应答获取Rx1DRoffset信息命令                                                     +
//+     update: 2019.2.18                                                                 +
//+     29  ABP写节点信息命令                                                                +                                                                                                    
//+     2A  应答ABP写节点信息命令                                                               + 
//+     update: 2019.2.19                                                                   +
//+     2C  ABP读节点信息命令                                                                   +
//+     2D  应答ABP读节点信息命令                                                              +
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#define       BROADCAST_CMD                 0X01
#define       ACK_BROCAST_CMD               0X02
#define       READ_ALL_NODE_CMD             0X03
#define       ACK_READ_ALL_NODE_CMD         0X04
#define       READ_NODE_CMD                 0X05
#define       ACK_READ_NODE_CMD             0X06
#define       WRITE_NODE_CMD                0X07
#define       ACK_WRITE_NODE_CMD            0X08
#define       READ_SX1301_CMD               0X09
#define       ACK_READ_SX1301_CMD           0X0A
#define       WRITE_SX1301_CMD              0X0B
#define       ACK_WRITE_SX1301_CMD          0X0C
#define       READ_SERVER_CMD               0X0D
#define       ACK_READ_SERVER_CMD           0X0E
#define       WRITE_SERVER_CMD              0X0F
#define       ACK_WRITE_SERVER_CMD          0X10
#define       DELETE_NODE_CMD               0X11
#define       ACK_DELETE_NODE_CMD           0X12
#define       DELETE_ALL_NODE_CMD           0X13
#define       ACK_DELETE_ALL_NODE_CMD       0X14
#define       READ_ALL_RX2_FREQ_CMD         0X15
#define       ACK_READ_ALL_RX2_FREQ_CMD     0X16
#define       CONF_ALL_RX2_FREQ_CMD         0X17
#define       ACK_CONF_ALL_FREQ_CMD         0X18
#define       REGION_FREQ_INFO_CMD          0X19
#define       ACK_REGION_FREQ_INFO_CMD      0X1A
#define       UPLINK_FREQ_LIST_CMD          0x1B
#define       ACK_UPLINK_FREQ_LIST_CMD      0X1C
#define       DOWNLINK_FREQ_LIST_CMD        0X1D
#define       ACK_DOWNLINK_FREQ_LIST_CMD    0X1E
#define       Rx1DRoffset_CMD               0X1F
#define       ACK_Rx1DRoffset_CMD           0X20
#define       FETCH_REGION_INFO_CMD         0X21
#define       ACK_FETCH_REGION_INFO_CMD     0X22
#define       FETCH_UPLINK_FREQ_CMD         0X23
#define       ACK_FETCH_UPLINK_FREQ_CMD     0X24
#define       FETCH_DOWN_FREQ_CMD           0X25
#define       ACK_FETCH_DOWN_FREQ_CMD       0X26
#define       FETCH_RX1DROFFSET_CMD         0X27
#define       ACK_FETCH_RX1DROFFSET_CMD     0X28
#define       ABP_CONF_NODE_INFO_CMD        0X29
#define       ACK_ABP_CONF_NODE_INFO_CMD    0X2A
#define       ABP_READ_NODE_INFO_CMD        0X2C  
#define       ACK_ABP_READ_NODE_INFO_CMD    0X2D
#define       READ_COMMUNICATE_SERVER_CMD   0X2E
#define       WRITE_COMMUNICATE_SERVER_CMD  0X30  

/* 应答广播命令为固定字节 */
#define       ACK_BROCAST_LEN               13
/* 接收命令的最大字节数 */
#define       RECE_CMD_MAX_LEN              255
/* 回复命令码长度 */
#define       RETURN_CMD_LEN                1
/* 接收服务器下发sx1301配置信息 */
#define       SERVER_CONF_SX1301            5559

/*----------------------------------------------------上位机配置函数声明---------------------------------------------------*/

/* pc上位机配置函数 */
void PC_Configuration(void);

/* 应答广播命令的函数 */
void ack_broadcast(void);

/* 应答读取网关上所有节点的信息 */
void ack_read_all_node(void);

/* 应答读单个节点信息 */
void ack_read_node(uint8_t *buff);

/* 应答写单个节点信息命令 */
void ack_write_node(uint8_t *buff);

/* 应答读SX1301 */
void ack_read_sx1301(void);

/* 应答写SX1301命令 */
void ack_write_sx1301(uint8_t *buff);

/* 应答读远程服务器配置命令 */
void ack_read_server();

/* 应答写远程服务器配置命令 */
void ack_write_server(uint8_t *buff);

/* 应答删除单个节点信息 */
void ack_delete_node(uint8_t  *buff);

/* 应答删除所有节点信息 */
void ack_delete_all_node(void);

/* 插入测试数据 */
void Insert_GwInfo_Table(db,zErrMsg);

/* 读取所有节点的频率，用于class c设备类型 */
void read_all_rx2_freq(void);

/* 配置所有节点的频率，用于class c设备类型 */
void conf_all_rx2_freq(uint8_t *buff);

/**
 * update:2019.2.18 增加ABP入网方式 
 */
/*!
 * \brief: ABP写节点信息命令
 *
 * \param [IN]  buff
 *
 * \param [OUT] NULL
 *
 * \return      NULL      
 */
void ABP_Configure_Node_Info(uint8_t *buff);


/**
 * update:2019.2.18
 * 
 * 定义一个宏，用于检测每次执行完sqlite3_exec()后的rc值
 * 
 */ 

#define CHECK_RC(rc,zErrMsg,db)     if(rc != SQLITE_OK)\
                            {\
                                fprintf(stderr,"SQL error: %s\n",zErrMsg);\
                                sqlite3_free(zErrMsg);\
                            }



/**
 * update:2019.2.18 增加ABP入网方式 
 */
/*!
 * \brief ABP读节点信息命令:
 *
 * \param [IN]  buff
 *
 * \param [OUT] NULL
 *
 * \return      NULL      
 */
void ABP_Fetch_Node_Info(uint8_t *buff);


/*
    
    brief:          初始化gwinfo数据库
    parameter in:   数据库地址，错误标志地址
    return      :   null

*/
void SqliteGwInfoTable(sqlite3 *db,char *zErrMsg);


/*
    
    brief:          打开数据库
    parameter in:   数据库地址，错误标志地址
    return      :   0： success     -1 :fail 

*/

int Sqlite3_open(const char *filename , sqlite3 **ppDb);


/*
    
    brief:          关闭数据库
    parameter in:   数据库地址，错误标志地址
    return      :   0： success     -1 :fail 

*/
int Sqlite3_close(sqlite3 *ppDb);


/*
   
   update：2019.05.24 

   brief：    应答读远程通信服务器命令
   parameter：需要写入的节点信息
   return：   NULL 

*/
void AckReadCommunicateServer(void);


/*
   
   update：2019.05.24

   brief：    应答写远程通信服务器命令
   parameter：需要写入的节点信息
   return：   NULL 

*/
void AckWriteCommunicateServer(uint8_t *buff);

/*------------------------------------------------------------------------------------------------------------------------------*/
#endif