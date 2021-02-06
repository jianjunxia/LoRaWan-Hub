/*
    ____               _    _      _         _
  / ____|             | |  | |    (_)       | |
 | (___    ___  _ __  | |_ | |__   _  _ __  | | __
  \___ \  / _ \|  _ \ | __||  _ \ | ||  _ \ | |/ /
  ____) ||  __/| | | || |_ | | | || || | | ||   <
 |_____/  \___||_| |_| \__||_| |_||_||_| |_||_|\_\


Description:
            
            增加数据存储功能：
            目前存储的方案是：
                    1：每天只存储一条数据，如果当前有多条数据，以时间戳最新的为主，作为日结的基础
                    2：每个月存储到最后一天，次月清除。作为月结的基础
                    3：增加时间同步功能。


Transport Protocols: UDP传输协议、TCP传输协议、LoRaWAN-HUB与服务器通信协议
Lierda |  Senthink
autor:  jianjun_xia              
data :  2019.01.09

*/

//注释：本头文件中所有函数均以：“S_”开头，代表与节点上报的lorwan数据存储有关的函数
//     函数命名方式采用驼峰式


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include<netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "common.h"

//引用lora_pkt_conf.c中的全局变量
extern sqlite3 *db;
extern char *zErrMsg;

/*!
 * \brief:lorawan数据存储表初始化
 * 
 * \param [IN] ： db 数据库地址参数
 * 
 * \param [IN] ： zErrMsg 错误显示参数
 * 
 * \Returns    ： NULL.
 */

void S_LoRaWanDataStoreTableInit(sqlite3 *db,char *zErrMsg);



/*!
 * \brief:节点上报的解析后的lorawan数据存储
 * 
 * \param [IN] ： db 数据库地址参数
 * 
 * \param [IN] ： zErrMsg 错误显示参数
 * 
 * \param [IN] ： devaddr 节点上报的4字节的短地址
 * 
 * \Returns    ： NULL.
 */


void S_NodeLoRaWanDataStore(sqlite3 *db,char *zErrMsg,uint32_t *devaddr);


/*!
 * \brief:节点上报的解析后的lorawan数据存储
 * 
 * \param [IN] ： db 数据库地址参数
 * 
 * \param [IN] ： zErrMsg 错误显示参数
 * 
 * \param [IN] ： devaddr 节点上报的4字节的短地址
 * 
 * \Returns    ： NULL.
 */





