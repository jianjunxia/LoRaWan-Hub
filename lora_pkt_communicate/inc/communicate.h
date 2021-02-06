/*
     Lierda | Senthink

     jianjun_xia 
     
     update: 2019.05.23

*/

#ifndef  _COMMUNICATE_H_    
#define  _COMMUNICATE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*---------------------------------------------服务器配置使用的一些宏定义----------------------------------------------*/

/* 
    定义本地通信端口
    规定：5555为监听join request的端口
         5556为监听confirm/unconfirm的端口 
         5557为下发给lora_pkt_fwd.c的应答数据的监听端口
         5558为下发class c 的数据
    本地通信指的是：与lora_pkt_fwd 之间的数据通信
    IP使用本地地址:127.0.0.1
*/

#define     JOIN_PORT           5555
#define     NODE_PORT           5556
#define     ACK_PORT            5557
#define     CLASS_C_PORT        5558
#define     CLASS_A_PORT        5560      
#define     BUFF_LEN            1000
#define     LOCAL_IP            "127.0.0.1" 
#define     JOIN_DATA_LEN       300   /* join 数据包缓存大小 */

/* 消息类型定义 */
#define     Server_Ack_Node_Data                0x8201 /*  服务器应答节点上报数据 */
#define     Server_Send_Class_C_Data            0x8301 /*  服务器主动下发class c数据类型 */
#define     Server_Ack_Heart                    0x8103 /*  服务器应答心跳      */
#define     Debug_Class_A_Node                  0x8318 /*  调试class a设备命令 */  

/* 用宏定义封装，屏蔽函数参数，同时利用宏的自解释性增加代码可读性 */
#define     HANDLE_0X8201_CASE                  case Server_Ack_Node_Data: 
#define     Handle_Server_Ack_Report_Data()     HANDLE_0X8201_CASE{Server_Ack_Report_Data(message_buff,message_len);break;}
#define     HANDLE_0X8301_CASE                  case Server_Send_Class_C_Data: 
#define     Handle_Server_Class_C_Data()        HANDLE_0X8301_CASE{Server_Class_C_Data(message_buff,message_len);break;}   
#define     HANDLE_0X8103_CASE                  case Server_Ack_Heart: 
#define     Handle_Server_Heartbeat_Ack()       HANDLE_0X8103_CASE{Server_Heartbeat_Ack(message_buff,message_len);break;}
#define     HANDLE_0X8318_CASE                  case Debug_Class_A_Node:
#define     Handle_Debug_ClassA_Node()          HANDLE_0X8318_CASE{Debug_ClassA_Node(message_buff,message_len);break;}                  

/*------------------------------------------------------数据通信类函数--------------------------------------------------------------*/

/* Tcp重新握手 */
void  Tcp_Reconnection(void);

/* 重新与服务器取得联系 */
void  ReconnectWith_Server(void);

/* 处理消息类型函数  */
void  Handle_Message_Type(uint16_t message_type,uint8_t *message_buff, int message_len);

/* 服务器应答节点上报数据  */
void  Server_Ack_Report_Data(uint8_t *data,int len);

/* 服务器主动下发class c数据类型 */
void  Server_Class_C_Data(uint8_t *data,int len);

/* 根据freq_hz 和 datarate 判断每包数据的最大载荷 */                             
int CalculateDownlinkMaxpayload( const uint32_t *freq, const uint8_t *datarate, int *maxpayload);

/* 接收服务器应答的心跳数据 */
void  Server_Heartbeat_Ack(uint8_t *data,int len);

/* 服务器下发class a 调试数据 */
void  Debug_ClassA_Node(uint8_t *data,int len);

/*--------------------------------------------------------------------------------------------------------------------------------*/

/*---------------------------------------------------------线程函数----------------------------------------------------------------*/

/* 使用TCP进行与后台服务器连接 */
void *Thread_Tcp_Connect(void);

/* 上报join数据 */
void *Thread_Send_JoinData(void);

/* 上报confirm/unconfirm数据 */
void *Thread_Send_NodeData(void);

/* 接收服务器回复的应答数据  */
void *Thread_Recv_ServerData(void);

/* 心跳应答线程 */
void *Thread_Hub_Send_Heartbeat(void);
/*----------------------------------------------------------------------------------------------------------------------------------*/


#endif