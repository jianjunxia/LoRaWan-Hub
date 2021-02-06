/*
     Lierda | Senthink
     jianjun_xia 
     date: 2018.10.10
*/
#ifndef  _LORA_PKT_SERVER_H_    
#define  _LORA_PKT_SERVER_H_
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------服务器配置使用的一些宏定义---------------------------------------------*/

/* 
    定义本地通信端口
    规定：5555为监听join data的端口
         5556为监听confirm/unconfirm的端口 
         5557为下发给lora_pkt_fwd.c的应答数据的监听端口
         5558为下发class c 的数据
    本地通信指的是：与lora_pkt_fwd 之间的数据通信
    IP使用本地地址:127.0.0.1
*/

/* 
     定义HUB与服务器的通信版本协议号_v1.0.3 
     备注：v1.0.3中取消了注册

*/
#define     HUB_Server_Communication_Version_v_1_0_3    1 

#define     JOIN_PORT           5555
#define     NODE_PORT           5556
#define     ACK_PORT            5557
#define     CLASS_C_PORT        5558
#define     BUFF_LEN            1000
#define     LOCAL_IP            "127.0.0.1" 
#define     JOIN_DATA_LEN       300   /* join 数据包缓存大小 */
#define     TCP_PORT            8888

/* 服务器注册结果 */
#define     Register_Successful                 0x00   /* 注册成功 */
#define     Register_Info_Exist                 0x01   /* 注册已经存在 */
#define     Server_No_Hub_Info                  0x02   /* 服务器没有该HUB的信息 */

/* 消息类型定义 */
#define     HUB_Register                        0x0101 /*  HUB注册 */
#define     Server_Register_Ack                 0x8101 /*  服务器注册 应答 */
#define     HUB_Auth                            0x0102 /*  HUB鉴权       */
#define     Server_Auth_Ack                     0x8102 /*  服务器鉴权应答 */
#define     HUB_Send_Heart                      0x0103 /*  HUB向服务器发送心跳 */
#define     Server_Ack_Heart                    0x8103 /*  服务器应答心跳     */
#define     HUB_Report_Node_Data                0x0201 /*  HUB上报节点数据   */
#define     Server_Ack_Node_Data                0x8201 /*  服务器应答节点上报数据 */
#define     Server_Send_Class_C_Data            0x8301 /*  服务器主动下发class c数据类型 */

/* 用宏定义封装，屏蔽函数参数，同时利用宏的自解释性增加代码可读性 */
#define     HANDLE_0X8201_CASE                  case Server_Ack_Node_Data: 
#define     Handle_Server_Ack_Report_Data()     HANDLE_0X8201_CASE{Server_Ack_Report_Data(message_buff,message_len);break;}
#define     HANDLE_0X8103_CASE                  case Server_Ack_Heart: 
#define     Handle_Server_Heartbeat_Ack()       HANDLE_0X8103_CASE{Server_Heartbeat_Ack(message_buff,message_len);break;}
#define     HANDLE_0X8301_CASE                  case Server_Send_Class_C_Data: 
#define     Handle_Server_Class_C_Data()        HANDLE_0X8301_CASE{Server_Class_C_Data(message_buff,message_len);break;}   

/*-----------------------------------------------------消息管理函数--------------------------------------------------------------*/

#ifdef   HUB_Server_Communication_Version_v_1_0_3

          /* hub注册函数 */
          void Hub_Register(void);

          /* 更新HUB端本地数据库的注册信息 */
          void Update_Register_Info(uint8_t *data);

          /* 服务器鉴权函数 */
          void HUB_Authentication(void);

          /* 服务器鉴权应答函数  */
          int HUB_Authentication_Ack(void);

          /* 检查HUB是否成功注册 */
          int Check_Hub_Register(void);


#else

          /* hub注册函数 */
          void Hub_Register(void);

          /* 更新HUB端本地数据库的注册信息 */
          void Update_Register_Info(uint8_t *data);

          /* 服务器鉴权函数 */
          int HUB_Authentication_Ack(void);

          /* 检查HUB是否成功注册 */
          int Check_Hub_Register(void);


#endif


/* 服务器应答hub注册 */
int Server_Ack_Hub_Register(void);

/* 接收服务器应答的心跳数据 */
void  Server_Heartbeat_Ack(uint8_t *data,int len);

/* 服务器应答节点上报数据  */
void  Server_Ack_Report_Data(uint8_t *data,int len);

/* 服务器主动下发class c数据类型 */
void  Server_Class_C_Data(uint8_t *data,int len);

/* 处理消息类型函数  */
void  Handle_Message_Type(uint16_t message_type,uint8_t *message_buff, int message_len);


/**
 * brief:      服务器心跳应答
 * 
 * parameter : 接收数据首地址，接收数据的长度
 * 
 * return : NULL 
 * 
 */
void Tcp_Reconnection(void);


 /*!
 * \brief:    重新与服务器取得联系的步骤                           
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL
 * 
 */
void ReconnectWith_Server(void);


/*!
 * \brief:    打开数据库                            
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL
 * 
 */

 void S_sqlite3_open(const char *filename , sqlite3 **ppDb);



 /**
 * breif:       HUB端注册信息初始化
 * 
 * parameter:   NULL
 * 
 * return   :   NULL 
 */

void HUB_Register_Init(void);


/*---------------------------------------------------------------------------------------------------------------------------*/

/*-------------------------------------------------------线程函数--------------------------------------------------------------*/

/* 使用TCP进行与后台服务器连接 */
void *Thread_Tcp_Connect(void);

/* 上报join数据 */
void *Thread_Send_JoinData(void);

/* 上报confirm/unconfirm数据 */
void *Thread_Send_NodeData(void);

/* 接收服务器回复的应答数据  */
void *Thread_Recv_ServerData(void);

/* 定时发送心跳   */
void *Thread_Hub_Send_Heartbeat(void);
/*-------------------------------------------------------------------------------------------------------------------------------*/

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//+                                                                                                 +
//+                                    LoRaWAN-HUB与服务器通信协议                                     + 
//+                                                                                                 +
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                  传输方式：大端传输
//                           消息结构
//                                   ---------------------------------
//                                  | 消息头  | 消息体 | 校验码 | 后导码  |    
//                                   ---------------------------------
//                           消息头
//                                  ----------------------------------------------------------
//                                  | 消息版本 | 消息类型 | HUB编号 | 消息序号 | 消息体属性 | 分包属性 |
//                                   ----------------------------------------------------------                         
//                           消息类型
//   ----------------------------------------------------------------------------------------------------------------------------                                  
//  |  HUB注册  |  服务器注册 应答 | HUB鉴权 ｜ 服务器鉴权应答 | HUB向服务器发送心跳 | 服务器应答心跳 | HUB上报节点数据 | 服务器应答节点上报数据  |
//   ----------------------------------------------------------------------------------------------------------------------------    
//  |  0X0101  |     0X8101     | 0X0102  |   0X8102     |     0X0103       |    0X8103    |    0X0201     |       0X8201        |                
//   -----------------------------------------------------------------------------------------------------------------------------
//                           消息体属性
//                                   -----------------------------------------------
//                                  |15 14| 13 | 12 11  10  | 9 8 7 6 5 4 3 2 1 0  |    
//                                  |----------------------------------------------
//                                  | 保留 |分包| 消息体加密方式|       消息体长度       |
//                                  |----------------------------------------------
//                           消息体   
//                                  依据协议有多个相应的结构
#endif