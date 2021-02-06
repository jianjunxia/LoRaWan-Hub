/*
    ____               _    _      _         _
  / ____|             | |  | |    (_)       | |
 | (___    ___  _ __  | |_ | |__   _  _ __  | | __
  \___ \  / _ \|  _ \ | __||  _ \ | ||  _ \ | |/ /
  ____) ||  __/| | | || |_ | | | || || | | ||   <
 |_____/  \___||_| |_| \__||_| |_||_||_| |_||_|\_\


Description:
            
           新增管理类消息，协议v1.0.1 0x8302开始
           将PC端配置功能添加在服务器端

Lierda |  Senthink
autor:  jianjun_xia              
data :  2018.12.13

*/

#ifndef  _MESSAGE_MANAGEMENT_H_    
#define  _MESSAGE_MANAGEMENT_H_
//#include "common.h"
#include "region.h" //区域频段配置头文件

//定义布尔变量
#define     bool    int            
#define     true     1     
#define     false    0     

//定义读取md5值大小
#define     READ_DATA_SIZE  1024
#define     MD5_SIZE        16

//消息类型定义：
#define     Server_Get_ALL_Node_DevEui          0x8302 // 服务器获取HUB上所有节点DevEui
#define     HUB_Report_ALL_Node_DevEui          0x0302 // HUB应答上报所有节点DevEui
#define     Server_Add_Single_Node              0X8303 // 服务器添加单个节点
#define     HUB_Ack_ADD_Single_Node             0x0303 // HUB应答添加单个节点
#define     Server_Delete_ALL_Node              0x8304 // 服务器删除所有节点
#define     HUB_Ack_Delete_ALL_Node             0x0304 // HUB应答删除所有节点
#define     Server_Delete_Single_Node           0x8305 // 服务器删除单个节点
#define     HUB_Ack_Delete_Single_Node          0x0305 // HUB应答删除单个节点
#define     Server_Change_Single_Node_Appkey    0x8306 // 服务器修改单个节点AppKey
#define     HUB_Ack_Change_Single_Node_AppKey   0x0306 // HUB应答修改单个节点AppKey
#define     Server_Seek_Single_Node_AppKey      0x8307 // 服务器查询单个节点AppKey
#define     HUB_Ack_Seek_Single_Node_AppKey     0x0307 // HUB应答查询单个节点AppKey
#define     Server_Get_SX1301_Conf_Info         0x8308 // 服务器获取SX1301配置信息
#define     HUB_Ack_Get_SX1301_Conf_Info        0x0308 // HUB应答获取SX1301配置信息
#define     Server_Conf_SX1301                  0x8309 // 服务器配置SX1301信息
#define     HUB_Ack_Conf_SX1301                 0x0309 // HUB应答服务器配置SX1301信息
#define     Server_Get_ClassC_Freq              0x830a // 服务器获取class c下发频率                           
#define     HUB_Ack_Get_ClassC_Freq             0x030a // HUB应答获取class c下发频率
#define     Server_Conf_ClassC_Freq             0x830b // 服务器设置class c下发频率
#define     HUB_Ack_Conf_ClassC_Freq            0x030b // HUB应答设置class c下发频率
#define     Server_Get_Region_Freq              0x830c // 服务器获取区域频段信息           
#define     HUB_Ack_Get_Region_Freq             0x030c // HUB应答服务器获取区域频段信息
#define     Server_Conf_Region_Freq             0x830d // 服务器设置区域频段信息
#define     HUB_Ack_Conf_Region_Freq            0x030d // HUB应答服务器设置区域频段信息
#define     Server_Get_Rx1DRoffset_Info         0x830e // 服务器获取Rx1DRoffset信息
#define     HUB_Ack_Get_Rx1DRoffset_Info        0x030e // HUB应答服务器获取Rx1DRoffset信息
#define     Server_Conf_Rx1DRoffset_Info        0x830f // 服务器设置Rx1DRoffset信息
#define     HUB_Ack_Conf_Rx1DRoffset_Info       0x030f // HUB应答服务器设置Rx1DRoffset信息
#define     Server_Get_Uplink_Freq              0x8310 // 服务器获取上行信道频率
#define     HUB_Ack_Get_Uplink_Freq             0x0310 // HUB应答服务器获取上行信道频率
#define     Server_Get_Down_Freq                0x8311 // 服务器获取下行信道频率
#define     HUB_Ack_Get_Down_Freq               0x0311 // HUB应答服务器获取下行信道频率
#define     Server_Conf_Uplink_Freq             0x8312 // 服务器设置上行信道频率
#define     HUB_Ack_Conf_Uplink_Freq            0x0312 // HUB应答服务器设置上行信道频率
#define     Server_Conf_Down_Freq               0x8313 // 服务器设置下行信道频率
#define     HUB_Ack_Conf_Down_Freq              0x0313 // HUB应答服务器设置下行信道频率  

/* update:2019.06.11 */
#define     Server_Add_ABP_Node                 0x8314 // 服务器添加ABP节点                        
#define     Server_Read_ABP_Node                0x8315 // 服务器读取单个ABP节点
#define     Read_Comm_Server_Info               0x8316 // 读取远程通信服务器信息
#define     Write_Comm_Server_Info              0x8317 // 写远程通信服务器信息
#define     OTA_Device_Upgrade                  0x8319 // OTA升级命令

/*!
 * \brief 获取HUB上所有节点的DevEui信息.
 *
 * \param [IN]： NULL.
 *
 * \Returns   ： NULL.
 */
void ServerGetAllNodeDevEui(void);

/*!
 * \brief 在HUB端添加一个新的节点信息，包含：节点的DevEui,节点的AppKey
 *
 * \param [IN]： 服务器端传输的数据：DevEui,AppKey
 *
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerAddSingleNode(uint8_t *message_buff,int message_len);

/*!
 * \brief 删除HUB端所有的节点信息.
 *
 * \param [IN]： NULL.
 *
 * \Returns   ： NULL.
 */
void ServerDeleteAllNode(void);

/*!
 * \brief 删除HUB端指定节点的信息
 *
 * \param [IN]： 服务器端传输的数据：DevEui
 *
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerDeleteSingleNode(uint8_t *message_buff,int message_len);

/*!
 * \brief 修改HUB端指定节点的AppKey
 *
 * \param [IN]： 服务器端传输的数据：DevEui AppKey
 *
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerChangeSingleNodeAppkey(uint8_t *message_buff,int message_len);

/*!
 * \brief 查询HUB端指定节点的AppKey
 *
 * \param [IN]： 服务器端传输的数据：DevEui
 *
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerSeekSingleNodeAppKey(uint8_t *message_buff, int message_len);

/*!
 * \brief 查询HUB端SX1301信息 包含SX1301 Radio A的中心频率、最小频率、最大频率
 *                                     Radio B的中心频率、最小频率、最大频率                                       
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetSX1301ConfInfo(void);

/*!
 * \brief 配置HUB端SX1301信息                                    
 *
 * \param [IN]： 服务器端传输的数据： SX1301 Radio A的中心频率、最小频率、最大频率
 *                                       Radio B的中心频率、最小频率、最大频率   
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerConfSX1301(uint8_t *message_buff, int message_len);

/*!
 * \brief 查询HUB端Class C频率                              
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetClassCFreq(void);

/*!
 * \brief 配置HUB端Class C频率
 *
 * \param [IN]： 服务器端传输的数据：Class C频率
 *
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerConfClassCFreq(uint8_t *message_buff, int message_len);

/*!
 * \brief 查询HUB端Class C频率                              
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetRegionFreq(void);

/*!
 * \brief 配置HUB端区域频段信息
 *
 * \param [IN]： 服务器端传输的数据：频段信息
 *  
 *   区域频段信息与Value映射表，详细参照 《LoRaWAN-HUB与服务器通信协议V1.0.1》
 *   ---------------------------------------------
 *  |区域频段(Ragion Spectrum) |     Value(1byte)  |
 *   --------------------------------------------- 
 *  |AS923                    |       0xa0        |
 *   --------------------------------------------- 
 *  |AU915-928                |       0xa1        |
 *   --------------------------------------------- 
 *  |CN470-510                |       0xa2        |
 *   ---------------------------------------------  
 *  |CN779-787                |       0xa3        | 
 *   --------------------------------------------- 
 *  |EU433                    |       0xa4        | 
 *   --------------------------------------------- 
 *  |EU868-870                |       0xa5        |  
 *   --------------------------------------------- 
 *  |IN865-867                |       0xa6        | 
 *   ---------------------------------------------    
 *  |KR920-923                |       0xa7        | 
 *   ---------------------------------------------     
 *  |US902-928                |       0xa8        | 
 *   --------------------------------------------- 
 *  |US902-928_HYBRID         |       0xa9        |  
 *   ---------------------------------------------
 *  |CN470_ASYNCHRONY         |       0xaa        |
 *   ---------------------------------------------
 *  \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerConfRegionFreq(uint8_t *message_buff,int message_len);


/*!
 * \brief 查询HUB端Rx1DRoffset信息                              
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetRx1DRoffsetInfo(void);

/*!
 * \brief 配置HUB端Rx1DRoffset信息
 *
 * \param [IN]： 服务器端传输的数据：Rx1DRoffset信息
 *  
 *   区域频段信息与Rx1DRoffset的可选范围，详细参照 《LoRaWAN-HUB与服务器通信协议V1.0.1》
 *   --------------------------------------------------------
 *  |区域频段(Ragion Spectrum) |    Rx1DRoffset Value(1byte)  |
 *   -------------------------------------------------------- 
 *  |AS923                    |             [0:7]            |
 *   -------------------------------------------------------- 
 *  |AU915-928                |             [0:5]            |
 *   -------------------------------------------------------- 
 *  |CN470-510                |             [0:5]            |
 *   --------------------------------------------------------  
 *  |CN779-787                |             [0:5]            | 
 *   -------------------------------------------------------- 
 *  |EU433                    |             [0:5]            | 
 *   -------------------------------------------------------- 
 *  |EU868-870                |             [0:5]            |  
 *   -------------------------------------------------------- 
 *  |IN865-867                |             [0:7]            | 
 *   --------------------------------------------------------    
 *  |KR920-923                |             [0:5]            | 
 *   --------------------------------------------------------      
 *  |US902-928                |             [0:3]            | 
 *   -------------------------------------------------------- 
 *  |US902-928_HYBRID         |             [0:3]            |  
 *   --------------------------------------------------------
 *  |CN470_ASYNCHRONY         |             [0:5]            |
 *   --------------------------------------------------------
 *  \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerConfRx1DRoffsetInfo(uint8_t *message_buff, int message_len);

/*!
 * \brief 获取HUB端上行信道频率信息                              
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetUplinkFreq(void);

/*!
 * \brief 获取HUB端下行信道频率信息                              
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetDownFreq(void);

/*!
 * \brief 设置HUB端上行信道频率信息                              
 *
 * \param [IN]：服务器端传输的数据：ch0~ch7信道频率 单位:Hz
 * \param [IN]：服务器端传输的数据长度
 * \Returns   ：NULL.
 */
void ServerConfUplinkFreq(uint8_t *message_buff, int message_len);


/*!
 * \brief 设置HUB端下行信道频率信息                              
 *
 * \param [IN]：服务器端传输的数据：ch0~ch7信道频率 单位:Hz
 * \param [IN]：服务器端传输的数据长度
 * \Returns   ：NULL.
 */
void ServerConfDownFreq(uint8_t *message_buff, int message_len);


/*!
 * \brief 服务器添加ABP节点                              
 *
 * \param [IN]：服务器端传输的ABP节点数据
 * \param [IN]：服务器端传输的数据长度
 * \Returns   ：NULL.
 */
void ServerAddAbpNode(uint8_t *message_buff, int message_len);


/*!
 * \brief 服务器读取ABP节点信息                             
 *
 * \param [IN]：服务器端传输的ABP节点数据
 * \param [IN]：服务器端传输的数据长度
 * \Returns   ：NULL.
 */
void ServerReadAbpNode(uint8_t *message_buff, int message_len);


/*!
 * \brief 读取远程通信服务器ip和端口信息                             
 *
 * \param [IN]：服务器端传输的数据地址
 * \param [IN]：服务器端传输的数据长度
 * \Returns   ：NULL.
 */
void ReadCommunicationServerInfo(uint8_t *message_buff, int message_len);


/*!
 * \brief  写远程通信服务器ip和端口信息                              
 *
 * \param [IN]：服务器端传输的数据地址
 * \param [IN]：服务器端传输的数据长度
 * \Returns   ：NULL.
 */
void WriteCommunicationServerInfo(uint8_t *message_buff, int message_len);


/*!
 * \brief  写远程通信服务器ip和端口信息                              
 *
 * \param [IN]：服务器端传输的数据地址
 * \param [IN]：服务器端传输的数据长度
 * \Returns   ：NULL.
 */
void ServerOTADeviceUpgrade(uint8_t *message_buff, int message_len);


/*!
 * \brief  计算文件的md5值                              
 *
 * \param [IN]： 文件的路径和名称
 * \param [OUT]：输出的md5值
 * \param [OUT]：输出的文件大小
 * \Returns   ： 0 : ok  -1 : fail
 */
int Compute_file_md5(const char *file_path, unsigned char *md5_str,unsigned int *filesize);



//用宏定义封装，屏蔽函数参数，同时利用宏的自解释性增加代码可读性
#define     HANDLE_0X8302_CASE                    case Server_Get_ALL_Node_DevEui:
#define     HandleServerGetAllNodeDevEui()        HANDLE_0X8302_CASE{ServerGetAllNodeDevEui();break;}
#define     HANDLE_0X8303_CASE                    case Server_Add_Single_Node:
#define     HandleServerAddSingleNode()           HANDLE_0X8303_CASE{ServerAddSingleNode(message_buff,message_len);break;}
#define     HANDLE_0X8304_CASE                    case Server_Delete_ALL_Node:
#define     HandleServerDeleteAllNode()           HANDLE_0X8304_CASE{ServerDeleteAllNode();break;}
#define     HANDLE_0X8305_CASE                    case Server_Delete_Single_Node:  
#define     HandleServerDeleteSingleNode()        HANDLE_0X8305_CASE{ServerDeleteSingleNode(message_buff,message_len);break;}
#define     HANDLE_0X8306_CASE                    case Server_Change_Single_Node_Appkey:
#define     HandleServerChangeSingleNodeAppkey()  HANDLE_0X8306_CASE{ServerChangeSingleNodeAppkey(message_buff,message_len);break;}
#define     HANDLE_0X8307_CASE                    case Server_Seek_Single_Node_AppKey:      
#define     HandleServerSeekSingleNodeAppKey()    HANDLE_0X8307_CASE{ServerSeekSingleNodeAppKey(message_buff,message_len);break;}                       
#define     HANDLE_0X8308_CASE                    case Server_Get_SX1301_Conf_Info:
#define     HandleServerGetSX1301ConfInfo()       HANDLE_0X8308_CASE{ServerGetSX1301ConfInfo();break;}
#define     HANDLE_0X8309_CASE                    case Server_Conf_SX1301:
#define     HandleServerConfSX1301()              HANDLE_0X8309_CASE{ServerConfSX1301(message_buff,message_len);break;} 
#define     HANDLE_0X830a_CASE                    case Server_Get_ClassC_Freq:            
#define     HandleServerGetClassCFreq()           HANDLE_0X830a_CASE{ServerGetClassCFreq();break;} 
#define     HANDLE_0X830b_CASE                    case Server_Conf_ClassC_Freq:
#define     HandleServerConfClassCFreq()          HANDLE_0X830b_CASE{ServerConfClassCFreq(message_buff,message_len);break;}
#define     HANDLE_0X830c_CASE                    case Server_Get_Region_Freq:         
#define     HandleServerGetRegionFreq()           HANDLE_0X830c_CASE{ServerGetRegionFreq();break;}
#define     HANDLE_0X830d_CASE                    case Server_Conf_Region_Freq:     
#define     HandleServerConfRegionFreq()          HANDLE_0X830d_CASE{ServerConfRegionFreq(message_buff,message_len);break;}
#define     HANDLE_0X830e_CASE                    case Server_Get_Rx1DRoffset_Info:
#define     HandleServerGetRx1DRoffsetInfo()      HANDLE_0X830e_CASE{ServerGetRx1DRoffsetInfo();break;}
#define     HANDLE_0X830f_CASE                    case Server_Conf_Rx1DRoffset_Info:      
#define     HandleServerConfRx1DRoffsetInfo()     HANDLE_0X830f_CASE{ServerConfRx1DRoffsetInfo(message_buff,message_len);break;}
#define     HANDLE_0X8310_CASE                    case Server_Get_Uplink_Freq:    
#define     HandleServerGetUplinkFreq()           HANDLE_0X8310_CASE{ServerGetUplinkFreq();break;} 
#define     HANDLE_0X8311_CASE                    case Server_Get_Down_Freq:
#define     HandleServerGetDownFreq()             HANDLE_0X8311_CASE{ServerGetDownFreq();break;}     
#define     HANDLE_0X8312_CASE                    case Server_Conf_Uplink_Freq:
#define     HandleServerConfUplinkFreq()          HANDLE_0X8312_CASE{ServerConfUplinkFreq(message_buff,message_len);break;}
#define     HANDLE_0X8313_CASE                    case Server_Conf_Down_Freq:
#define     HandleServerConfDownFreq()            HANDLE_0X8313_CASE{ServerConfDownFreq(message_buff,message_len);break;}
#define     HANDLE_0X8314_CASE                    case Server_Add_ABP_Node:
#define     HandleServerAddAbpNode()              HANDLE_0X8314_CASE{ServerAddAbpNode(message_buff,message_len);break;}
#define     HANDLE_0X8315_CASE                    case Server_Read_ABP_Node:
#define     HandleServerReadAbpNode()             HANDLE_0X8315_CASE{ServerReadAbpNode(message_buff,message_len);break;}
#define     HANDLE_0X8316_CASE                    case Read_Comm_Server_Info:
#define     HandleReadCommunicationServerInfo()   HANDLE_0X8316_CASE{ReadCommunicationServerInfo(message_buff,message_len);break;}
#define     HANDLE_0X8317_CASE                    case Write_Comm_Server_Info:
#define     HandleWriteCommunicationServerInfo()  HANDLE_0X8317_CASE{WriteCommunicationServerInfo(message_buff,message_len);break;}
#define     HANDLE_0X8319_CASE                    case OTA_Device_Upgrade:
#define     HANDLE_ServerOTADeviceUpgrade()       HANDLE_0X8319_CASE{ServerOTADeviceUpgrade(message_buff,message_len);break;}
 

//根据《LoRaWAN-HUB与服务器通信协议》增加消息体属性结构
typedef union MessageHead_s
{
  uint16_t Value;
  struct MessageBodyProperty_s
  {
        uint16_t MessageBody_Length : 10; //消息体长度
        uint16_t MessageEncryption  :  3; //消息体加密方式：000 未加密，001 TEA加密，002 AES-128 ECB加密
        uint16_t SubpackageBit      :  1; //分包位 0：未分包， 1：分包
        uint16_t RFU                :  2; //保留

  }MessageBodyProperty_t;

}MessageHead_t;

#define     SERVER_CONF_SX1301  5559


/*!
 * \brief:  根据freq_hz 和 datarate 判断每包数据的最大载荷                             
 *
 * \param [IN] : 频率、速率
 * \param [OUT]：最大载荷大小
 * \Returns    ： 0： 成功，  1：失败
 */
int CalculateDownlinkMaxpayload(const uint32_t *freq, const uint8_t *datarate,int *maxpayload);


/*
    brief:          读取网关eui信息
    parameter out:  网关信息
    return:         0: success 1; fail
*/
int ReadGwInfo(uint8_t *buffer);



#endif