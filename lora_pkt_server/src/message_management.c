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
update :  2018.12.13

*/

#include <sys/socket.h>     /* socket specific definitions */
#include <netinet/in.h>     /* INET constants and stuff */
#include <arpa/inet.h>      /* IP address conversion stuff */
#include <netdb.h>          /* gai_strerror */
#include <errno.h>          /* error messages */
#include <sys/errno.h>
#include <sqlite3.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>         /* sigaction */
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include "common.h"       
#include "parson.h"
#include "message_management.h"
#include "md5.h"

/* 引用lora_pkt_server.c中的全局变量 */
extern sqlite3 *db;
extern char    *zErrMsg ;
extern int     rc;
extern char    *sql;
extern const char    *data;
extern int    tcp_sock_fd;
extern int    tcp_send_len;
extern pid_t  pid_communicate;    


/* 处理杀死后的子进程，避免产生僵尸进程 */
static void sig_child(int signo)
{
    pid_t pid;
    int stat;

    /* 回收子进程 */
    while(pid = waitpid(-1,&stat,WNOHANG) > 0)
    {
        DEBUG_CONF("\n/-----------------------------------------------child %d terminated.------------------------------/\n",pid);
    }

}

/* 存储所有节点deveui信息 */
static uint8_t all_node_deveui_info[8000];
/* 存储所有节点appkey信息 */
static uint8_t all_node_appkey_info[8000];
/* 调用callback计数值 */
static uint16_t sqlite_callback_count;
/* 回调函数,隐藏为本地函数 */
static int ReadAllDeveuiCallback(void *NotUsed,int argc,char**argv,char**azColName)
{
    int i;
    int temp_deveui = 0;
    int temp_appkey = 0;
    
    /* 改变存储地址 */
    temp_deveui = sqlite_callback_count * 8;
    temp_appkey = sqlite_callback_count * 16;

    /* argv[2]: 对应Deveui字段地址 */
    if ( argv[2] != NULL)  
                String_To_ChArray(all_node_deveui_info+temp_deveui,argv[2],8);
    
    /* argv[1]: 对应Appkey字段地址 */
    if ( argv[1] != NULL)  
                String_To_ChArray(all_node_appkey_info+temp_appkey,argv[1],16); 

     DEBUG_CONF("/*------------------------------ReadAllDeveuiCallback-----------------------------*/\n");

    /* 每调用一次callback,计数值+1 */
    sqlite_callback_count++;

    return 0;
}


/*!
 * \brief 获取HUB上所有节点的DevEui信息.
 *
 * \param [IN]： NULL.
 *
 * \Returns   ： NULL.
 */
void ServerGetAllNodeDevEui(void)
{
    DEBUG_CONF("/*-----------------------recv cmd  read all deveui!---------------------------*/\n");
    char *sql = NULL;
    int rc; 
    /* 分包判断变量 */
    bool SubpackageIsOk = false;   
    /* 分包总数 */
    uint16_t package_total;
    /* 包序号 */
    uint16_t package_number;
    /* 分包起始地址变量 */
    uint16_t package_begin_address_temp = 0;
    uint16_t appkey_begin_address_temp  = 0;

    /* 最后一包数据包含多少个deveui */
    uint16_t package_last_number;
    int data_len = 0;
    int send_data_len = 0;

    /* Gwinfo */
    uint8_t  gwinfo[8];
    memset(gwinfo,0,8);
    
    /* Message id */
    static uint16_t message_id;
    /* 单包数据最大字节数 */
    uint8_t send_buff[8000];
    memset(send_buff,0,8000);
    uint8_t send_data[8000];
    memset(send_data,0,8000);

    /* 消息体属性共用体 */
    MessageHead_t MessageHead;
    memset(&MessageHead,0,sizeof(MessageHead_t));

    /* 
        ABP的key初始化为16bytes 0xff
        入网方式:0x01 --->OTAA
                0x02 --->ABP       
    */
    uint8_t abp[16];
    uint8_t nodeType[8000];
    memset (abp,      0xff, 16);
    memset (nodeType, 0,    16);

    /* 获取HUB编号，消息封装时使用 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");
    
    /* 提取HUB上所有节点的DevEui信息 */
    sqlite3_exec(db,"BEGIN TRANSACTION;", NULL,NULL,&zErrMsg);

    sql = "SELECT* FROM GWINFO";
    /* execute */
    rc = sqlite3_exec(db,sql,ReadAllDeveuiCallback,0,&zErrMsg);
    if( rc != SQLITE_OK ){
            DEBUG_CONF("SQL read all node deveui error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
    }
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);

    /* 
        判断有多少条节点数据
        1:如果节点数据小于8包，则不需要使用协议中的分包属性。
        2:如果节点数据大于8包，需使用分包属性 进行多包数据传输
    */

    if ( sqlite_callback_count <=8){
        SubpackageIsOk = false;
    }else{   
        SubpackageIsOk = true;
    }

    if ( SubpackageIsOk)
    {    
                /* 需要判断是否是8的倍数,注：一包消息体数据中最大存储8包deveui */
                if ( sqlite_callback_count % 8 == 0)
                {
                        /* 包总数 */
                        package_total  = (sqlite_callback_count / 8);
                        /* 包的最大序号，包序号从1开始 */
                        package_number = (sqlite_callback_count / 8);
                        /* 最后一包数据为8个deveui */    
                        package_last_number = 8;
                }
                else
                {
                        /* 包总数 */
                        package_total  = (sqlite_callback_count / 8) + 1;
                        /* 包的最大序号，包序号从1开始 */
                        package_number = (sqlite_callback_count / 8) + 1;
                        /* 判断最后一包数据包含多少个deveui */   
                        package_last_number = (sqlite_callback_count - (package_total-1) * 8) % 8;       
                
                }

                /* 消息封装 */
                for ( int Package_Id = 1; Package_Id <= package_number; Package_Id++)
                {
                        /* 消息版本     */
                        send_buff[data_len++] = 0x01;
                        /* 消息类型 2byte */
                        send_buff[data_len++] = 0x03;
                        send_buff[data_len++] = 0x02;
                        /* HUB唯一编号    */
                        mymemcpy(send_buff+data_len,gwinfo,8);
                        data_len+=8;
                        message_id++;
                        /* 消息序号  */
                        send_buff[data_len++] = (uint8_t)(message_id >> 8);
                        send_buff[data_len++] = (uint8_t)(message_id);
                        /* 
                            如果是最后一包数据，需根据最后一包数据大小计算消息体长度
                            deveui：8bytes, appkey：16bytes 
                            2bytes: T T 
                            tyep:  1byte
                            2bytes: T T
                        */
                        if ( Package_Id == package_number) 
                        {
                                MessageHead.MessageBodyProperty_t.MessageBody_Length = ( package_last_number * (8+1+2+2) ); /* 消息体字节数 */ 
                        }
                        else
                        {            
                                MessageHead.MessageBodyProperty_t.MessageBody_Length = ( 8 * (8+1+2+2) ); /* 消息体字节数  */
                        }
                        MessageHead.MessageBodyProperty_t.MessageEncryption = 0;/* 未加密 */
                        MessageHead.MessageBodyProperty_t.SubpackageBit = 1; /* 分包 */
                        MessageHead.MessageBodyProperty_t.RFU = 0;/* 保留 */  
                        /* 消息体属性 */
                        send_buff[data_len++] = (uint8_t)(MessageHead.Value >> 8); 
                        send_buff[data_len++] = (uint8_t) MessageHead.Value;             
                        /* 分包属性 */
                        /* 包总数 */
                        send_buff[data_len++] = (uint8_t)(package_total>>8);
                        send_buff[data_len++] = (uint8_t)(package_total); 
                        /* 包序号 */
                        send_buff[data_len++] = (uint8_t)(Package_Id>>8);
                        send_buff[data_len++] = (uint8_t)(Package_Id);                
                        
                        /* 需判断是否是最后一包数据 */
                        if ( Package_Id == package_number)
                        {          
                                mymemcpy(nodeType,all_node_appkey_info+appkey_begin_address_temp,(package_last_number * 16));     
                                
                                /* deveui */
                                //消息体 
                                for ( int i = 0; i < package_last_number; i++)
                                {

                                        int index_deveui = 0;
                                        index_deveui = ( (package_number -1) * 8 * 8 ) + 8 * i ;
                                        
                                        int index_appkey = 0;
                                        index_appkey = ( (package_number -1) * 16 * 8 ) + 8 * i ;

                                        /* deveui */
                                        //tag
                                        send_buff[data_len++] = 0x01; 
                                        //type
                                        send_buff[data_len++] = 0x0a;   
                                        //value
                                        mymemcpy(send_buff+data_len, all_node_deveui_info+index_deveui, 8);
                                        data_len +=8;


                                        /* appkey */
                                        //tag
                                        send_buff[data_len++] = 0x02; 
                                        //type
                                        send_buff[data_len++] = 0x02;

                                        /* 通过appkey判断节点的类型 */                      
                                        if ( 0 == ArrayValueCmp (abp, nodeType+index_appkey, 16)) { /* abp */

                                                send_buff[data_len++] = 0x02; 

                                        }else { /* otaa */
                                                
                                                send_buff[data_len++] = 0x01; 
                                        }
                                }

                        }
                        else
                        {

                                mymemcpy(nodeType,all_node_appkey_info+appkey_begin_address_temp, (8 * 16));              
                                appkey_begin_address_temp+=128;/* 一包数据最多填充8个appkey,一个appkey包含16个字节 */         

                                /* deveui */
                                //消息体 
                                for ( int i = 0; i < 8; i++)
                                {

                                        int index_deveui =  8 * i;
                                        int index_appkey = 16 * i;

                                        /* deveui */
                                        //tag
                                        send_buff[data_len++] = 0x01; 
                                        //type
                                        send_buff[data_len++] = 0x0a;   
                                        //value
                                        mymemcpy(send_buff+data_len, all_node_deveui_info+index_deveui, 8);
                                        data_len +=8;

                                        /* appkey */
                                        //tag
                                        send_buff[data_len++] = 0x02; 
                                        //type
                                        send_buff[data_len++] = 0x02;

                                        /* 通过appkey判断节点的类型 */                      
                                        if ( 0 == ArrayValueCmp (abp, nodeType+index_appkey, 16)) { /* abp */

                                                send_buff[data_len++] = 0x02; 

                                        }else { /* otaa */
                                                
                                                send_buff[data_len++] = 0x01; 
                                        }
                                }
                        }
                        
                        //计算校验码
                        send_buff[data_len] = Check_Xor(send_buff,data_len);
                        //转义处理
                        data_len++;
                        send_data_len = Transfer_Mean(send_data,send_buff,data_len);
                        //后导码
                        send_data[send_data_len] = 0x7e;
                        send_data_len+=1;
                        //进行数据发送
                        tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                        if ( tcp_send_len <= 0)
                        {
                                        DEBUG_CONF(" %d\n",errno);
                                        fprintf(stderr,"send error: %s\n",strerror(errno));
                                        ReconnectWith_Server(); 
                        }

                        //clear
                        data_len = 0;
                        send_data_len = 0;
                        memset(send_data,0,255);
                        memset(send_buff,0,255);
                        memset(&MessageHead,0,sizeof(MessageHead_t));

                }

                /* clear */
                sqlite_callback_count = 0;
                memset(gwinfo,0,8);
                memset(all_node_deveui_info,0,8000);
                memset(all_node_appkey_info,0,8000);    
    }
    else
    {
                //消息版本 1byte
                send_buff[data_len++] = 0x01;
                //消息类型 2byte
                send_buff[data_len++] = 0x03;
                send_buff[data_len++] = 0x02;
                //HUB唯一编号
                mymemcpy(send_buff+data_len,gwinfo,8);
                data_len+=8;
                message_id++;
                //消息序号
                send_buff[data_len++] = (uint8_t)(message_id >> 8);
                send_buff[data_len++] = (uint8_t)(message_id);
                //消息体属性            
                MessageHead.MessageBodyProperty_t.MessageBody_Length = ( sqlite_callback_count * (8+1+2+2) ); //消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                
                send_buff[data_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_buff[data_len++] = (uint8_t) MessageHead.Value;

                //拷贝所有appkey 到 nodeType中
                mymemcpy (nodeType,all_node_appkey_info,(sqlite_callback_count*16));   

                /* deveui */
                //消息体 
                for ( int i = 0; i < sqlite_callback_count; i++)
                {

                        int index_deveui =  8 * i;
                        int index_appkey = 16 * i;

                        /* deveui */
                        //tag
                        send_buff[data_len++] = 0x01; 
                        //type
                        send_buff[data_len++] = 0x0a;   
                        //value
                        mymemcpy(send_buff+data_len, all_node_deveui_info+index_deveui, 8);
                        data_len +=8;

                        /* appkey */
                        //tag
                        send_buff[data_len++] = 0x02; 
                        //type
                        send_buff[data_len++] = 0x02;

                        /* 通过appkey判断节点的类型 */                      
                        if ( 0 == ArrayValueCmp (abp, nodeType+index_appkey, 16)) { /* abp */

                                send_buff[data_len++] = 0x02; 

                        }else { /* otaa */
                                
                                send_buff[data_len++] = 0x01; 
                        }

                }

        
                //计算校验码
                send_buff[data_len] = Check_Xor(send_buff,data_len);
                //转义处理
                data_len++;

                for ( int i = 0; i < data_len; i++)
                                DEBUG_CONF("send_data[%d]:0x%02x\n",i,send_buff[i]);        

                send_data_len = Transfer_Mean(send_data,send_buff,data_len);
                //后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }

                /* clear */
                data_len = 0;
                send_data_len = 0;
                sqlite_callback_count = 0;
                memset(gwinfo,0,8);
                memset(send_data,0,255);
                memset(send_buff,0,255);
                memset(all_node_deveui_info,0,8000);
                memset(all_node_appkey_info,0,8000);
                memset(&MessageHead,0,sizeof(MessageHead_t));   

    }

}

/*!
 * \brief 在HUB端添加一个新的节点信息，包含：节点的DevEui,节点的AppKey
 *
 * \param [IN]： 服务器端传输的数据：DevEui,AppKey
 *
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerAddSingleNode(uint8_t *message_buff,int message_len)
{
    /*
        思路：1：先查询表中是否有该节点的信息
             2：表中已经有该节点的信息，则上报添加失败
            3：表中没有该节点的信息，则添加该节点，上报添加成功或者添加失败
    */

    int rc;
    sqlite3_stmt *stmt_add_node =NULL;
    sqlite3_stmt *stmt_deveui   = NULL;
    bool devEuiIsExist = false;
    bool InsertIsOk    = false;
    char *p_deveui   = NULL;
    char *p_appkey   = NULL;            
    char *sql_deveui = (char*)malloc(100);
    char *sql_appkey = (char*)malloc(200);
    char *deveui_string = (char*)malloc(100);
    char *appkey_string = (char*)malloc(100);
    uint8_t deveui_buff[8];
    uint8_t appkey_buff[16];
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;
    //max id
    int IdValue;

    //将表中的ID按降序排布，即寻找表中ID最大值
    char *sql_search = NULL;
    sql_search = "SELECT* FROM GWINFO ORDER BY ID DESC LIMIT 1";

    //initialization
    memset(deveui_buff,0,8);
    memset(appkey_buff,0,16);
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    IdValue = 0;
    //deveui index:17
    //appkey index:27
    mymemcpy(deveui_buff,message_buff+17,8);
    mymemcpy(appkey_buff,message_buff+28,16);
    p_deveui = ChArray_To_String(deveui_string,deveui_buff,8);
    p_appkey = ChArray_To_String(appkey_string,appkey_buff,16);
    
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    /* sql语句 */
    sprintf(sql_deveui,"SELECT* FROM GWINFO WHERE DEVEui ='%s';",p_deveui);
    DEBUG_CONF("p_deveui: %s\n",p_deveui);
    rc = sqlite3_prepare_v2(db,sql_deveui,strlen(sql_deveui),&stmt_add_node,NULL);
    if (SQLITE_OK !=rc || NULL == stmt_add_node)
    {
          DEBUG_CONF("\n\n ack write node prepare error!\n\n");
          sqlite3_close(db); 
    }
    //execute
    rc = sqlite3_step(stmt_add_node);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    
    if(SQLITE_ROW == rc)
    {
        devEuiIsExist = true;
        DEBUG_CONF("devEuiIsExist is true\n");
    }
    else
    {
        devEuiIsExist = false;
        DEBUG_CONF("devEuiIsExist is fasle\n");
    }

    //读取网关的信息,消息封装的时候使用
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    if(devEuiIsExist)//如果表中存在该deveui,则应答添加失败信息 
    {
            //消息版本 1byte
            send_data_buff[send_data_buff_len++] = 0x01;
            //消息类型 2byte
            send_data_buff[send_data_buff_len++] = 0x03;
            send_data_buff[send_data_buff_len++] = 0x03;
            //HUB唯一编号
            mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
            send_data_buff_len+=8;
            //消息序号
            message_id++;
            send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
            send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
            //消息体属性       
            MessageHead.MessageBodyProperty_t.MessageBody_Length =10+3;//消息体字节数 
            MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
            MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
            MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
            send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
            //消息体

            //tag
            send_data_buff[send_data_buff_len++] = 0x01;
            //type
            send_data_buff[send_data_buff_len++] = 0x0a;
            //value    
            mymemcpy(send_data_buff+send_data_buff_len, deveui_buff, 8);    
            send_data_buff_len+=8;   

            //tag
            send_data_buff[send_data_buff_len++] = 0x02;
            //type
            send_data_buff[send_data_buff_len++] = 0x02;
            //value: 0 successful  1 fail
            send_data_buff[send_data_buff_len++] = 0x01;
            //计算校验码
            send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
            //转义处理
            send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
            //添加后导码
            send_data[send_data_len] = 0x7e;
            send_data_len +=1;
            //进行数据发送
            tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
            if(tcp_send_len <= 0)
            {
                    DEBUG_CONF(" %d\n",errno);
                    fprintf(stderr,"send error: %s\n",strerror(errno));
                    ReconnectWith_Server(); 

            }
    }    
    else//表中无该deveui的信息，插入该节点的信息
    {
            sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
            rc = sqlite3_prepare_v2(db,sql_search,strlen(sql_search),&stmt_deveui,NULL);
            if (SQLITE_OK !=rc || NULL == stmt_deveui)
            {
                DEBUG_CONF("\n parse Prepare error! \n");
                sqlite3_close(db);
            }
            //找到最大值，取出，+1
            while(SQLITE_ROW == sqlite3_step(stmt_deveui))
            {          
                    DEBUG_CONF("MAX ID: %d\n",sqlite3_column_int(stmt_deveui,0));
                    IdValue = sqlite3_column_int(stmt_deveui,0);        
                    if ( stmt_deveui != NULL ) 
                            sqlite3_finalize(stmt_deveui);  
            }
            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
            //+1
            IdValue = IdValue+1;
            //插入一条新的数据到表中
            sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
            /* sql语句 */
            sprintf(sql_appkey,"INSERT INTO GWINFO (ID,APPKey,DEVEui) VALUES (%d,'%s','%s');",IdValue,p_appkey,p_deveui);
            /* execute */
            rc = sqlite3_exec(db, sql_appkey, 0, 0, &zErrMsg);
            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
            if( rc != SQLITE_OK )
            {
                    DEBUG_CONF("ack insert new appkey node error!: %s\n", zErrMsg);
                    sqlite3_free(zErrMsg);
                    InsertIsOk = false; //插入失败
            }
            else
            {
                    DEBUG_CONF("Server insert new appkey node successfully\n");
                    InsertIsOk = true; //插入成功
            }

            if(InsertIsOk) //发送插入成功的应答信息
            {
                    //消息版本 1byte
                    send_data_buff[send_data_buff_len++] = 0x01;
                    //消息类型 2byte
                    send_data_buff[send_data_buff_len++] = 0x03;
                    send_data_buff[send_data_buff_len++] = 0x03;
                    //HUB唯一编号
                    mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                    send_data_buff_len+=8;
                    //消息序号
                    message_id++;
                    send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                    send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                    //消息体属性       
                    MessageHead.MessageBodyProperty_t.MessageBody_Length =10+3;//消息体字节数 
                    MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                    MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                    MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                    send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                    send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                    //消息体

                    //tag
                    send_data_buff[send_data_buff_len++] = 0x01;
                    //type
                    send_data_buff[send_data_buff_len++] = 0x0a;
                    //value    
                    mymemcpy(send_data_buff+send_data_buff_len, deveui_buff, 8);    
                    send_data_buff_len+=8;   
                    
                    //tag
                    send_data_buff[send_data_buff_len++] = 0x02;
                    //type
                    send_data_buff[send_data_buff_len++] = 0x02;
                    //value: 0 successful  1 fail
                    send_data_buff[send_data_buff_len++] = 0x00;
                    //计算校验码
                    send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                    //转义处理
                    send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                    //添加后导码
                    send_data[send_data_len] = 0x7e;
                    send_data_len +=1;
                    //进行数据发送
                    tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                    if(tcp_send_len <= 0)
                    {
                            DEBUG_CONF(" %d\n",errno);
                            fprintf(stderr,"send error: %s\n",strerror(errno));
                            ReconnectWith_Server(); 

                    }

            }
            else //发送插入失败的应答信息
            {
                    //消息版本 1byte
                    send_data_buff[send_data_buff_len++] = 0x01;
                    //消息类型 2byte
                    send_data_buff[send_data_buff_len++] = 0x03;
                    send_data_buff[send_data_buff_len++] = 0x03;
                    //HUB唯一编号
                    mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                    send_data_buff_len+=8;
                    //消息序号
                    message_id++;
                    send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                    send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                    //消息体属性       
                    MessageHead.MessageBodyProperty_t.MessageBody_Length =10+3;//消息体字节数 
                    MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                    MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                    MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                    send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                    send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                    //消息体
                    
                    //tag
                    send_data_buff[send_data_buff_len++] = 0x01;
                    //type
                    send_data_buff[send_data_buff_len++] = 0x0a;
                    //value    
                    mymemcpy(send_data_buff+send_data_buff_len, deveui_buff, 8);    
                    send_data_buff_len+=8;   

                    //tag
                    send_data_buff[send_data_buff_len++] = 0x02;
                    //type
                    send_data_buff[send_data_buff_len++] = 0x02;
                    //value: 0 successful  1 fail
                    send_data_buff[send_data_buff_len++] = 0x01;
                    //计算校验码
                    send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                    //转义处理
                    send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                    //添加后导码
                    send_data[send_data_len] = 0x7e;
                    send_data_len +=1;
                    //进行数据发送
                    tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                    if(tcp_send_len <= 0)
                    {
                            DEBUG_CONF(" %d\n",errno);
                            fprintf(stderr,"send error: %s\n",strerror(errno));
                            ReconnectWith_Server(); 

                    }
        
            }
    }
    

    if (stmt_add_node != NULL) 
              sqlite3_finalize(stmt_add_node);

    if (p_deveui != NULL) 
            free(p_deveui);

    if (p_appkey != NULL) 
            free(p_appkey);

    if (sql_deveui != NULL) 
            free(sql_deveui);
    
    if (sql_deveui != NULL) 
            free(sql_appkey);
    //clear
    memset(deveui_buff,0,8);
    memset(appkey_buff,0,16);
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    memset(&MessageHead,0,sizeof(MessageHead_t));
    IdValue = 0;

}

/*!
 * \brief 删除HUB端所有的节点信息.
 *
 * \param [IN]： NULL.
 *
 * \Returns   ： NULL.
 */
void ServerDeleteAllNode(void)
{
    int rc;
    char *sql = NULL;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    bool DeleteIsOk = false;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead; 

    //Initialization
    rc = 0;
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    
    //删除表中所有节点的sql语句 ID MAX 最大为4200000000
    sql = "DELETE FROM GWINFO WHERE ID != 4200000000;";
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
    rc = sqlite3_exec(db, sql, 0, 0, &zErrMsg);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);

    if( rc != SQLITE_OK )
    {
        DEBUG_CONF("SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        DeleteIsOk = false; //删除失败
    }
    else
    {
        DEBUG_CONF("DELETE successfully\n");
        DeleteIsOk = true; //删除成功 
    }

    //读取网关的信息,消息封装的时候使用
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    
    if(DeleteIsOk) //发送插入成功的应答信息
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x04;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x00;
                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }
    }
    else //发送插入失败的应答信息    
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x04;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x01;
                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 
           
                }

    }
    
}

/*!
 * \brief 删除HUB端指定节点的信息
 *
 * \param [IN]： 服务器端传输的数据：DevEui
 *
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerDeleteSingleNode(uint8_t *message_buff,int message_len)
{
    //思路：1：直接删除表中的该deveui，0：成功 1：失败
    int rc;
    sqlite3_stmt  *stmt        = NULL;
    sqlite3_stmt  *stmt_delete = NULL;
    bool DeleteIsOk     = false;
    bool DeveuiIsExist  = false;
    char *deveui_string = (char*)malloc(2*8);
    char *sql_deveui    = (char*)malloc(100);
    char *p_deveui = NULL;
    uint8_t deveui_buff[8];
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;

    //initialization
    rc = 0;
    memset(deveui_buff,0,8);
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));

    /* 读取网关信息 */
    if ( -1 == ReadGwInfo(gwinfo))
                DEBUG_CONF("ReadGwInfo error!\n");

    //deveui index:17
    mymemcpy(deveui_buff,message_buff+17,8);
    p_deveui = ChArray_To_String(deveui_string,deveui_buff,8);

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    sprintf(sql_deveui,"SELECT *FROM GWINFO WHERE DEVEui ='%s';",p_deveui);
    DEBUG_CONF("sql_deveui:%s\n",sql_deveui);
    rc = sqlite3_prepare_v2(db,sql_deveui,strlen(sql_deveui),&stmt,NULL);
    if (SQLITE_OK !=rc || NULL == stmt)
    {
          DEBUG_CONF("\n\n ack write node prepare error!\n\n");
          sqlite3_close(db); 
    }

    //execute
    rc = sqlite3_step(stmt);

    if ( SQLITE_ROW == rc)
    {
                DeveuiIsExist = true;
    }
    else
    {
                DeveuiIsExist = false;
    }

    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    
   
    if ( DeveuiIsExist) 
    {
                
                sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
                sprintf(sql_deveui,"DELETE FROM GWINFO WHERE DEVEui ='%s';",p_deveui);
                DEBUG_CONF("sql_deveui:%s\n",sql_deveui);
                rc = sqlite3_prepare_v2(db,sql_deveui,strlen(sql_deveui),&stmt_delete,NULL);
                if (SQLITE_OK !=rc || NULL == stmt_delete)
                {
                        DEBUG_CONF("\n\n ack write node prepare error!\n\n");
                        sqlite3_close(db); 
                }

                //execute
                rc = sqlite3_step(stmt_delete);
       
                if ( SQLITE_DONE == rc)
                {
                        DEBUG_CONF("delete is successful!");
                        DeleteIsOk = true;
                }
                else
                {
                        DEBUG_CONF("delete is fail!");
                        DeleteIsOk = false;
                }
                
                sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL); 

                
                if ( DeleteIsOk) //删除成功，发送应答信息
                {
                        //消息版本 1byte
                        send_data_buff[send_data_buff_len++] = 0x01;
                        //消息类型 2byte
                        send_data_buff[send_data_buff_len++] = 0x03;
                        send_data_buff[send_data_buff_len++] = 0x05;
                        //HUB唯一编号
                        mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                        send_data_buff_len+=8;
                        //消息序号
                        message_id++;
                        send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                        send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                        //消息体属性       
                        MessageHead.MessageBodyProperty_t.MessageBody_Length =10+3;//消息体字节数 
                        MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                        MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                        MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                        send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                        send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                        //消息体

                        //tag
                        send_data_buff[send_data_buff_len++] = 0x01;
                        //type
                        send_data_buff[send_data_buff_len++] = 0x0a;
                        //value
                        mymemcpy(send_data_buff+send_data_buff_len,deveui_buff,8);
                        send_data_buff_len+=8;

                        //tag
                        send_data_buff[send_data_buff_len++] = 0x02;
                        //type
                        send_data_buff[send_data_buff_len++] = 0x02;
                        //value: 0 successful  1 fail
                        send_data_buff[send_data_buff_len++] = 0x00;

                        //计算校验码
                        send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                        //转义处理
                        send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                        //添加后导码
                        send_data[send_data_len] = 0x7e;
                        send_data_len +=1;
                        //进行数据发送
                        tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                        if(tcp_send_len <= 0)
                        {
                                DEBUG_CONF(" %d\n",errno);
                                fprintf(stderr,"send error: %s\n",strerror(errno));
                                ReconnectWith_Server(); 

                        }
        
                }
                else // 删除失败，发送应答信息
                {
                        //消息版本 1byte
                        send_data_buff[send_data_buff_len++] = 0x01;
                        //消息类型 2byte
                        send_data_buff[send_data_buff_len++] = 0x03;
                        send_data_buff[send_data_buff_len++] = 0x05;
                        //HUB唯一编号
                        mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                        send_data_buff_len+=8;
                        //消息序号
                        message_id++;
                        send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                        send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                        //消息体属性       
                        MessageHead.MessageBodyProperty_t.MessageBody_Length =10+3;//消息体字节数 
                        MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                        MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                        MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                        send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                        send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                        //消息体

                        //tag
                        send_data_buff[send_data_buff_len++] = 0x01;
                        //type
                        send_data_buff[send_data_buff_len++] = 0x0a;
                        //value
                        mymemcpy(send_data_buff+send_data_buff_len,deveui_buff,8);
                        send_data_buff_len+=8;

                        //tag
                        send_data_buff[send_data_buff_len++] = 0x02;
                        //type
                        send_data_buff[send_data_buff_len++] = 0x02;
                        //value: 0 successful  1 fail
                        send_data_buff[send_data_buff_len++] = 0x01;
                        //计算校验码
                        send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                        //转义处理
                        send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                        //添加后导码
                        send_data[send_data_len] = 0x7e;
                        send_data_len +=1;
                        //进行数据发送
                        tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                        if(tcp_send_len <= 0)
                        {
                                DEBUG_CONF(" %d\n",errno);
                                fprintf(stderr,"send error: %s\n",strerror(errno));
                                ReconnectWith_Server(); 

                        }
                }

    }
    else /* 节点不存在，返回删除成功 */
    {
                
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x05;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =10+3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体

                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x0a;
                //value
                mymemcpy(send_data_buff+send_data_buff_len,deveui_buff,8);
                send_data_buff_len+=8;

                //tag
                send_data_buff[send_data_buff_len++] = 0x02;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x00;

                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                } 

    }


    if ( stmt != NULL) 
            sqlite3_finalize(stmt);

     if ( stmt_delete != NULL) 
            sqlite3_finalize(stmt_delete);
   
    if ( p_deveui != NULL ) 
             free(p_deveui);

    if ( sql_deveui != NULL ) 
             free(sql_deveui); 
}

/*!
 * \brief 修改HUB端指定节点的AppKey
 *
 * \param [IN]： 服务器端传输的数据：DevEui AppKey
 *
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerChangeSingleNodeAppkey(uint8_t *message_buff,int message_len)
{
    /*
        思路：1：先查询表中是否有该节点的deveui,若没有则修改失败
             2: 更新该节点的appkey,  0：更新成功，发送应答消息，1：更新失败，发送应答消息
    */
    int rc;
    sqlite3_stmt *stmt = NULL;
    bool devEuiIsExist = false;
    bool ChangeIsOk    = false;
    char *p_deveui   = NULL;
    char *p_appkey   = NULL;            
    char *sql_deveui = (char*)malloc(100);
    char *sql_appkey = (char*)malloc(100);
    char *deveui_string = (char*)malloc(2*8);
    char *appkey_string = (char*)malloc(2*16);
    uint8_t deveui_buff[8];
    uint8_t appkey_buff[16];
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;

    //initialization
    memset(deveui_buff,0,8);
    memset(appkey_buff,0,16);
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    //deveui index:17
    //appkey index:27
    mymemcpy(deveui_buff,message_buff+17,8);
    mymemcpy(appkey_buff,message_buff+28,16);
    p_deveui = ChArray_To_String(deveui_string,deveui_buff,8);
    p_appkey = ChArray_To_String(appkey_string,appkey_buff,16);

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    sprintf(sql_deveui,"SELECT* FROM GWINFO WHERE DEVEui ='%s';",p_deveui);
    rc = sqlite3_prepare_v2(db,sql_deveui,strlen(sql_deveui),&stmt,NULL);
    if (SQLITE_OK !=rc || NULL == stmt)
    {
          DEBUG_CONF("\n\n ack write node prepare error!\n\n");
          sqlite3_close(db); 
    }
    //execute
    rc = sqlite3_step(stmt);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    
    if(SQLITE_ROW == rc)
    {
        devEuiIsExist = true;
    }
    else
    {
        devEuiIsExist = false;
    }

    /* 读取网关的信息,消息封装的时候使用 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    if(devEuiIsExist) //查询成功，进行修改操作
    {
        //修改Appkey
        sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
        sprintf(sql_appkey,"UPDATE GWINFO SET APPKey = '%s' WHERE DEVEui ='%s';", p_appkey,p_deveui);
        rc = sqlite3_exec(db, sql_appkey, 0, 0, &zErrMsg);
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
        if( rc != SQLITE_OK )
        {
            DEBUG_CONF("ack insert new appkey node error!: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
            ChangeIsOk = false; //修改失败
        }
        else
        {
            DEBUG_CONF("Server insert new appkey node successfully\n");
            ChangeIsOk = true; //修改成功
        }

        if(ChangeIsOk) //修改成功，发生应答消息
        {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x06;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x00;
                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }
    
        }
        else  //修改失败，发生应答消息
        {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x06;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x01;
                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }
      
        }
    
    }
    else //查询失败，发送操作失败的应答消息
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x06;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x01;
                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }

    }  

    if ( stmt != NULL ) 
            sqlite3_finalize(stmt);


    if ( p_deveui != NULL )
             free(p_deveui);
    

    if ( p_appkey != NULL )
             free(p_appkey);
 

    if ( sql_deveui != NULL )
             free(sql_deveui);


    if ( sql_appkey != NULL )
             free(sql_appkey);

    memset(deveui_buff,0,8);
    memset(appkey_buff,0,16);
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    memset(&MessageHead,0,sizeof(MessageHead_t));

}

/*!
 * \brief 查询HUB端指定节点的AppKey
 *
 * \param [IN]： 服务器端传输的数据：DevEui
 *
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerSeekSingleNodeAppKey(uint8_t *message_buff, int message_len)
{
  //思路：1：根据deveui进行查询操作
  //     2： 查询成功，取出appkey,上报，应答。
    int rc;
    sqlite3_stmt *stmt = NULL;
    bool  devEuiIsExist = false;
    char *p_deveui   = NULL;          
    char *sql_deveui = (char*)malloc(100);
    char *deveui_string = (char*)malloc(2*8);
    char *appkey_string = NULL;
    uint8_t deveui_buff[8];
    uint8_t appkey_buff[16];
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;

    //initialization
    memset(deveui_buff,0,8);
    memset(appkey_buff,0,16);
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));

    //deveui index:17
    mymemcpy(deveui_buff,message_buff+17,8);
    p_deveui = ChArray_To_String(deveui_string,deveui_buff,8);
        
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    sprintf(sql_deveui,"SELECT* FROM GWINFO WHERE DEVEui ='%s';",p_deveui);
    rc = sqlite3_prepare_v2(db,sql_deveui,strlen(sql_deveui),&stmt,NULL);
    if (SQLITE_OK !=rc || NULL == stmt)
    {
          DEBUG_CONF("\n\n ack write node prepare error!\n\n");
          sqlite3_close(db); 
    }
    //execute
    rc = sqlite3_step(stmt);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    if(SQLITE_ROW == rc)
    {
        devEuiIsExist = true;
    }
    else
    {
        devEuiIsExist = false;
    }

    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    if(devEuiIsExist)//找到对应的appkey,进行数据应答
    {           
            appkey_string = sqlite3_column_text(stmt,1);
            String_To_ChArray(appkey_buff,appkey_string,16);

            //应答消息封装
            //消息版本 1byte
            send_data_buff[send_data_buff_len++] = 0x01;
            //消息类型 2byte
            send_data_buff[send_data_buff_len++] = 0x03;
            send_data_buff[send_data_buff_len++] = 0x07;
            //HUB唯一编号
            mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
            send_data_buff_len+=8;
            //消息序号
            message_id++;
            send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
            send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
            //消息体属性       
            MessageHead.MessageBodyProperty_t.MessageBody_Length =29;//消息体字节数 
            MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
            MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
            MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
            send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
            //消息体
            //tag
            send_data_buff[send_data_buff_len++] = 0x01;
            //type
            send_data_buff[send_data_buff_len++] = 0x0A;
            //deveui
            mymemcpy(send_data_buff+send_data_buff_len,deveui_buff,8);
            send_data_buff_len+=8;
            //tag
            send_data_buff[send_data_buff_len++] = 0x02;
            //type
            send_data_buff[send_data_buff_len++] = 0x0C;
            //length
            send_data_buff[send_data_buff_len++] = 0x10;
            //Appkey
            mymemcpy(send_data_buff+send_data_buff_len,appkey_buff,16);
            send_data_buff_len+=16;                
            //计算校验码
            send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
            //转义处理
            send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
            //添加后导码
            send_data[send_data_len] = 0x7e;
            send_data_len +=1;
            //进行数据发送
            tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
            if(tcp_send_len <= 0)
            {
                    DEBUG_CONF(" %d\n",errno);
                    fprintf(stderr,"send error: %s\n",strerror(errno));
                    ReconnectWith_Server(); 

            }

    }
    else //不处理
    {
        DEBUG_CONF("HUB no this deveui,Please check it!");

    }
    
    if ( stmt != NULL ) 
            sqlite3_finalize(stmt);
    
    if ( sql_deveui != NULL ) 
            free(sql_deveui);

    if ( p_deveui != NULL ) 
            free(p_deveui);
}

/*!
 * \brief 查询HUB端SX1301信息 包含SX1301 Radio A的中心频率、最小频率、最大频率
 *                                     Radio B的中心频率、最小频率、最大频率                                       
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetSX1301ConfInfo(void)
{
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    int i;

    //GWinfo
    uint8_t gwinfo[8];
    memset(gwinfo,0,8);
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;

    //读取本地SX1301配置信息
    char *read_cfg_path ="/lorawan/lorawan_hub/global_conf.json";
    char conf_obj[] ="SX1301_conf";
    char param_name[32];
    //JSON变量
    JSON_Value  *read_val;
    JSON_Object *read_obj  = NULL;
    JSON_Object *read_conf = NULL;

    //定义一个临时结构体
    //用于存储 radio_a radio_b 的频率
    struct Freq_Buff
    {
        uint32_t center_freq;
        uint32_t freq_min;
        uint32_t freq_max;
    };  
    struct Freq_Buff radio_freq[2];
    
    //initialization 
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    memset(&radio_freq,0,sizeof(radio_freq));
    memset(param_name,0,32);

    /* 读取网关信息 */
    if ( -1 == ReadGwInfo(gwinfo))
        DEBUG_CONF("ReadGwInfo error!\n");
 
    //读取本地sx1301配置文件
    if (access(read_cfg_path, R_OK) == 0) 
    {
            DEBUG_CONF("INFO: found global configuration file %s, parsing it\n", read_cfg_path); 
            read_val = json_parse_file_with_comments(read_cfg_path);
            read_obj = json_value_get_object(read_val);
            if (read_obj == NULL) 
            {
                    DEBUG_CONF("ERROR: %s id not a valid JSON file\n", read_cfg_path);
                    exit(EXIT_FAILURE);
            }
            read_conf = json_object_get_object(read_obj, conf_obj);
            if (read_conf == NULL) 
            {
                    DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj);
                
            } 

            for(i=0;i < 2;++i)
            {
                    snprintf(param_name, sizeof param_name, "radio_%i.freq", i);
                    //center_freq
                    radio_freq[i].center_freq = (uint32_t)json_object_dotget_number(read_conf, param_name);

                    snprintf(param_name, sizeof param_name, "radio_%i.tx_freq_min", i);
                    //freq_min
                    radio_freq[i].freq_min = (uint32_t)json_object_dotget_number(read_conf, param_name);

                    snprintf(param_name, sizeof param_name, "radio_%i.tx_freq_max", i);
                    //freq_max
                    radio_freq[i].freq_max = (uint32_t)json_object_dotget_number(read_conf, param_name);
            }
            //应答消息封装
            //消息版本 1byte
            send_data_buff[send_data_buff_len++] = 0x01;
            //消息类型 2byte
            send_data_buff[send_data_buff_len++] = 0x03;
            send_data_buff[send_data_buff_len++] = 0x08;
            //HUB唯一编号
            mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
            send_data_buff_len+=8;
            //消息序号
            message_id++;
            send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
            send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
            //消息体属性       
            MessageHead.MessageBodyProperty_t.MessageBody_Length =36;//消息体字节数 
            MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
            MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
            MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
            send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
            //消息体
            //tag
            send_data_buff[send_data_buff_len++] = 0x01;
            //type
            send_data_buff[send_data_buff_len++] = 0x06;
            //radio_a center frequency
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].center_freq >>24); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].center_freq >>16); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].center_freq >>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].center_freq);
            //tag
            send_data_buff[send_data_buff_len++] = 0x02;
            //type
            send_data_buff[send_data_buff_len++] = 0x06;
            //radio_a min frequency
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].freq_min >>24); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].freq_min >>16); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].freq_min >>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].freq_min);
            //tag
            send_data_buff[send_data_buff_len++] = 0x03;
            //type
            send_data_buff[send_data_buff_len++] = 0x06;
            //radio_a max frequency
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].freq_max >>24); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].freq_max >>16); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].freq_max >>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[0].freq_max);
            //tag
            send_data_buff[send_data_buff_len++] = 0x04;
            //type
            send_data_buff[send_data_buff_len++] = 0x06;
            //radio_a center frequency
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].center_freq >>24); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].center_freq >>16); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].center_freq >>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].center_freq);
            //tag
            send_data_buff[send_data_buff_len++] = 0x05;
            //type
            send_data_buff[send_data_buff_len++] = 0x06;
            //radio_a min frequency
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].freq_min >>24); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].freq_min >>16); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].freq_min >>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].freq_min);
            //tag
            send_data_buff[send_data_buff_len++] = 0x06;
            //type
            send_data_buff[send_data_buff_len++] = 0x06;
            //radio_a max frequency
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].freq_max >>24); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].freq_max >>16); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].freq_max >>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t)(radio_freq[1].freq_max);
            //计算校验码
            send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
            //转义处理
            send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
            //添加后导码
            send_data[send_data_len] = 0x7e;
            send_data_len +=1;
            //进行数据发送
            tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
            if(tcp_send_len <= 0)
            {
                    DEBUG_CONF(" %d\n",errno);
                    fprintf(stderr,"send error: %s\n",strerror(errno));
                    ReconnectWith_Server(); 

            } 
    }
    else
    {

        DEBUG_CONF("Sorry,No Found the conf Json Path,please check it\n");
    }
    //每次操作JSON文件后需序列化文件，并释放指针
    json_serialize_to_file(read_val,read_cfg_path); 
    json_value_free(read_val);

}

/*!
 * \brief 配置HUB端SX1301信息                                    
 *
 * \param [IN]： 服务器端传输的数据： SX1301 Radio A的中心频率、最小频率、最大频率
 *                                       Radio B的中心频率、最小频率、最大频率   
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerConfSX1301(uint8_t *message_buff, int message_len)
{
    //建立UDP通信，将服务器端的配置数据给到lora_pkt_conf进程。配置完成后，让lora_pkt_conf进程重启restart_lora_pkt_fwd.sh进程
    int ret;
    uint8_t send_data[30];
    uint8_t send_data_buff[30];
    uint8_t recv_data[4];
    uint32_t ChangeSuccessfulFlag; 
    int send_data_len;
    int send_data_buff_len;
    int i;
    //创建与lora_pkt_fwd.c传输数据的连接
    int socket_fd;
    struct sockaddr_in server_addr;
    int udp_len;
    int server_addr_len;
    int Reusraddr    = 1;
    //判断配置是否成功
    bool ConfIsOk = false;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;

    //定义一个临时结构体
    //用于存储 radio_a radio_b 的频率
    struct Freq_Buff
    {
        uint32_t center_freq;
        uint32_t freq_min;
        uint32_t freq_max;
    };  
    struct Freq_Buff write_radio_freq[2];

    //initialization 
    memset(send_data,0,30);
    memset(send_data_buff,0,30);
    send_data_len = 0;
    send_data_buff_len = 0;
    memset(write_radio_freq,0,sizeof(struct Freq_Buff)*2); //定义的结构体 需初始化 否则会导致取出的数据有问题
    memset(recv_data,0,4);
    ChangeSuccessfulFlag = 0;
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    
    //提取服务器发送的数据
    for(int loop =0,radio_address = 0;loop < 2; loop++)//外层控制结构体
    {
        for(int address = 0,index = 24; address < 4; address++) //内层控制地址,index: 变量向左移动的位数
        {
            write_radio_freq[loop].center_freq |= (*(message_buff+17+address+radio_address)) << index;
            index-=8; //8 bit   
        }
        radio_address +=18;   //到下一个radio.center_freq的地址      
    }
    
    for(int loop =0,radio_address = 0;loop < 2; loop++)//外层控制结构体
    {
        for(int address = 0,index = 24; address < 4; address++) //内层控制地址,index: 变量向左移动的位数
        {
            write_radio_freq[loop].freq_min |= (*(message_buff+23+address+radio_address)) << index;
            index-=8; //8 bit   
        }
        radio_address +=18;   //到下一个radio.freq_min的地址      
    }

    for(int loop =0,radio_address = 0;loop < 2; loop++)//外层控制结构体
    {
        for(int address = 0,index = 24; address < 4; address++) //内层控制地址,index: 变量向左移动的位数
        {
            write_radio_freq[loop].freq_max |= (*(message_buff+29+address+radio_address)) << index;
            index-=8; //8 bit   
        }
        radio_address +=18;   //到下一个radio.freq_max的地址      
    }
    //                      大端传输
    // lora_pkt_fwd.c<-------------------->lora_pkt_conf.c 
    for(int i=0;i < 2;i++)//i:控制write_radio_freq的循环
    {
        for(int loop=0;loop < 3;loop++)//loop:控制赋值的频率
        {
            if(0 == loop)
            {
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].center_freq >> 24);
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].center_freq >> 16);
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].center_freq >>  8);
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].center_freq >>  0);
            }
            if(1 == loop)
            {
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].freq_min >> 24);
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].freq_min >> 16);
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].freq_min >>  8);
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].freq_min >>  0);
            }
            if(2 == loop)
            {
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].freq_max >> 24);
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].freq_max >> 16);
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].freq_max >>  8);
                send_data[send_data_len++] =  (uint8_t)(write_radio_freq[i].freq_max >>  0);
            }
        }               
    }
    //creat sockfd
    socket_fd = socket(AF_INET,SOCK_DGRAM,0);//ipv4,udp
    if(-1 == socket_fd)
    {
        DEBUG_CONF("socket error!\n");
        close(socket_fd);
    }
    //set sockaddr_in parameter
    memset(&server_addr,0,sizeof(struct sockaddr_in));
    server_addr.sin_family      = AF_INET; //ipv4
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port        = htons(SERVER_CONF_SX1301);
    server_addr_len = sizeof(struct sockaddr_in);
    DEBUG_CONF("send_data_len: %d\n",send_data_len);
    
    //set REUSEADDR properties address reuse
    ret = setsockopt(socket_fd,SOL_SOCKET,SO_REUSEADDR,&Reusraddr,sizeof(int));
    if(ret != 0)
    {
        DEBUG_CONF("setsocketopt reuseaddr fail,ret:%d,error:%d\n",ret,errno);
        close(socket_fd);
    }
 
    udp_len = sendto(socket_fd,send_data,send_data_len,0,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
    if(udp_len <= 0)
    {            
        DEBUG_CONF("can't send join data\n");
        close(socket_fd);
        ConfIsOk = false;
    } 

    udp_len = recvfrom(socket_fd,recv_data,100,0,(struct sockaddr*)&server_addr,&server_addr_len );
    if(udp_len <= 0)
    {            
        DEBUG_CONF("can't send join data\n");
        close(socket_fd);
        ConfIsOk = false;
    }
    //                      大端传输
    //lora_pkt_conf.c<-------------------->lora_pkt_fwd.c
    //ChangeSuccessfulFlag = 0xfafbfcfd: successful
    //ChangeSuccessfulFlag = 0xffffffff: fail
    //debug
    ChangeSuccessfulFlag  = (recv_data[3] <<  0);
    ChangeSuccessfulFlag |= (recv_data[2] <<  8);
    ChangeSuccessfulFlag |= (recv_data[1] << 16);
    ChangeSuccessfulFlag |= (recv_data[0] << 24);
    DEBUG_CONF("ChangeSuccessfulFlag is 0x%02x\n",ChangeSuccessfulFlag);
    if(0xfafbfcfd == ChangeSuccessfulFlag)
    {
        ConfIsOk = true;
    }
    else if(0xffffffff == ChangeSuccessfulFlag)
    {
        ConfIsOk = false;
    }
    else
    {
        DEBUG_CONF("recv data is error! please check it!");
        ConfIsOk = false;
    }

    //clear
    memset(send_data,0,30);
    memset(send_data_buff,0,30);
    send_data_len = 0;
    send_data_buff_len = 0;
    
    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    //应答配置SX1301信息
    //0:successful 1: fail
    if(ConfIsOk)
    {
            //消息版本 1byte
            send_data_buff[send_data_buff_len++] = 0x01;
            //消息类型 2byte
            send_data_buff[send_data_buff_len++] = 0x03;
            send_data_buff[send_data_buff_len++] = 0x09;
            //HUB唯一编号
            mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
            send_data_buff_len+=8;
            //消息序号
            message_id++;
            send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
            send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
            //消息体属性       
            MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
            MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
            MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
            MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
            send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
            //消息体
            //tag
            send_data_buff[send_data_buff_len++] = 0x01;
            //type
            send_data_buff[send_data_buff_len++] = 0x02;
            //value: 0 successful  1 fail
            send_data_buff[send_data_buff_len++] = 0x00;
            //计算校验码
            send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
            //转义处理
            send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
            //添加后导码
            send_data[send_data_len] = 0x7e;
            send_data_len +=1;
            //进行数据发送
            tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
            if(tcp_send_len <= 0)
            {
                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"send error: %s\n",strerror(errno));
                ReconnectWith_Server(); 

            }

    }
    else
    {
            //消息版本 1byte
            send_data_buff[send_data_buff_len++] = 0x01;
            //消息类型 2byte
            send_data_buff[send_data_buff_len++] = 0x03;
            send_data_buff[send_data_buff_len++] = 0x09;
            //HUB唯一编号
            mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
            send_data_buff_len+=8;
            //消息序号
            message_id++;
            send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
            send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
            //消息体属性       
            MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
            MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
            MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
            MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
            send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
            //消息体
            //tag
            send_data_buff[send_data_buff_len++] = 0x01;
            //type
            send_data_buff[send_data_buff_len++] = 0x02;
            //value: 0 successful  1 fail
            send_data_buff[send_data_buff_len++] = 0x00;
            //计算校验码
            send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
            //转义处理
            send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
            //添加后导码
            send_data[send_data_len] = 0x7e;
            send_data_len +=1;
            //进行数据发送
            tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
            if(tcp_send_len <= 0)
            {
                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"send error: %s\n",strerror(errno));
                ReconnectWith_Server(); 

            }
            
    }
}

/*!
 * \brief 查询HUB端Class C频率                              
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetClassCFreq(void)
{
    //思路：所有节点的class c的频率相同，读取一个class c的频率即可。
    int rc;
    sqlite3_stmt *stmt = NULL;
    char *sql = NULL;
    uint32_t frequency;
    bool ReadRx2IsOk    = false;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;
    

    //initialization
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);
    //所有节点的rx2_freq目前做统一设置处理
    sql = "SELECT* FROM GWINFO WHERE ID != 4200000000;";  
    rc = sqlite3_prepare_v2(db,sql,strlen(sql),&stmt,NULL);
    if (SQLITE_OK !=rc || NULL == stmt)
    {
          DEBUG_CONF("\n\n ack_read_node prepare error!\n\n");
          sqlite3_close(db); 
    }
    //执行查询命令
    rc = sqlite3_step(stmt);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);

    if(SQLITE_ROW == rc)
    {
        frequency = sqlite3_column_int(stmt,9);
        DEBUG_CONF("frequency:%u\n",frequency);
        ReadRx2IsOk = true;                 
    }
    else
    {
        DEBUG_CONF("sorry,no this rx2_frequency,please check it!");
        ReadRx2IsOk = false;
    }
    
    /* 读取网关信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    if( ReadRx2IsOk )// 成功取出rx2_freq,封装应答消息
    {
        //消息版本 1byte
        send_data_buff[send_data_buff_len++] = 0x01;
        //消息类型 2byte
        send_data_buff[send_data_buff_len++] = 0x03;
        send_data_buff[send_data_buff_len++] = 0x0a;
        //HUB唯一编号
        mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
        send_data_buff_len+=8;
        //消息序号
        message_id++;
        send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
        send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
        //消息体属性       
        MessageHead.MessageBodyProperty_t.MessageBody_Length =6;//消息体字节数 
        MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
        MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
        MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
        send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
        send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
        //消息体
        //tag
        send_data_buff[send_data_buff_len++] = 0x01;
        //type
        send_data_buff[send_data_buff_len++] = 0x02;
        //class_c frequency
        send_data_buff[send_data_buff_len++] = (uint8_t)(frequency >> 24);
        send_data_buff[send_data_buff_len++] = (uint8_t)(frequency >> 16);
        send_data_buff[send_data_buff_len++] = (uint8_t)(frequency >> 8);
        send_data_buff[send_data_buff_len++] = (uint8_t)(frequency);
        //计算校验码
        send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
        //转义处理
        send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
        //添加后导码
        send_data[send_data_len] = 0x7e;
        send_data_len +=1;
        //进行数据发送
        tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
        if(tcp_send_len <= 0)
        {
            DEBUG_CONF(" %d\n",errno);
            fprintf(stderr,"send error: %s\n",strerror(errno));
            ReconnectWith_Server(); 

        }
    }
    else// 取出失败
    {

    }

    if ( stmt != NULL ) 
            sqlite3_finalize(stmt);
    
}

/*!
 * \brief 配置HUB端Class C频率
 *
 * \param [IN]： 服务器端传输的数据：Class C频率
 *
 * \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerConfClassCFreq(uint8_t *message_buff, int message_len)
{
    int rc;
    char *sql = NULL;
    char *sql_buff = (char*)malloc(100);
    uint32_t frequency;
    bool SetRx2IsOk    = false;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;
    
    //initialization
    rc = 0;
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));

    //提取server端下发的频率
    frequency  = (message_buff[20]);
    frequency |= (message_buff[19]<<8);
    frequency |= (message_buff[18]<<16);
    frequency |= (message_buff[17]<<24);
    DEBUG_CONF("Server Set Frequency:%u\n",frequency);
    
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);
    sprintf(sql_buff,"UPDATE GWINFO SET Rx2_Freq = %u WHERE ID != 4200000000",frequency);
    //execute
    rc = sqlite3_exec(db,sql_buff,NULL,NULL,&zErrMsg);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);

    if( rc != SQLITE_OK)
    {
        DEBUG_CONF("SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        SetRx2IsOk = false;
    }
    else
    {
        SetRx2IsOk = true;
    }
    
    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    if(SetRx2IsOk) //设置成功，发送应答消息
    {
            //消息版本 1byte
            send_data_buff[send_data_buff_len++] = 0x01;
            //消息类型 2byte
            send_data_buff[send_data_buff_len++] = 0x03;
            send_data_buff[send_data_buff_len++] = 0x0b;
            //HUB唯一编号
            mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
            send_data_buff_len+=8;
            //消息序号
            message_id = 0;
            send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
            send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
            //消息体属性       
            MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
            MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
            MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
            MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
            send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
            //消息体
            //tag
            send_data_buff[send_data_buff_len++] = 0x01;
            //type
            send_data_buff[send_data_buff_len++] = 0x02;
            //value: 0 successful  1 fail
            send_data_buff[send_data_buff_len++] = 0x00;
            //计算校验码
            send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
            //转义处理
            send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
            //添加后导码
            send_data[send_data_len] = 0x7e;
            send_data_len +=1;
            //进行数据发送
            tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
            if(tcp_send_len <= 0)
            {
                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"send error: %s\n",strerror(errno));
                ReconnectWith_Server(); 

            }
    }
    else //设置失败，发生应答消息
    {
            //消息版本 1byte
            send_data_buff[send_data_buff_len++] = 0x01;
            //消息类型 2byte
            send_data_buff[send_data_buff_len++] = 0x03;
            send_data_buff[send_data_buff_len++] = 0x0b;
            //HUB唯一编号
            mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
            send_data_buff_len+=8;
            //消息序号
            message_id = 0;
            send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
            send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
            //消息体属性       
            MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
            MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
            MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
            MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
            send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
            //消息体
            //tag
            send_data_buff[send_data_buff_len++] = 0x01;
            //type
            send_data_buff[send_data_buff_len++] = 0x02;
            //value: 0 successful  1 fail
            send_data_buff[send_data_buff_len++] = 0x01;
            //计算校验码
            send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
            //转义处理
            send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
            //添加后导码
            send_data[send_data_len] = 0x7e;
            send_data_len +=1;
            //进行数据发送
            tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
            if(tcp_send_len <= 0)
            {
                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"send error: %s\n",strerror(errno));
                ReconnectWith_Server(); 

            }

    }

    if ( sql_buff != NULL) 
            free(sql_buff);
}

/*!
 * \brief 查询HUB端区域频段信息                              
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetRegionFreq(void)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    char *sql = NULL;
    char *sql_buff = (char*)malloc(100);
    uint32_t frequency;
    bool GetRegionIsOk    = false;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;
    //区域频段的值
    int RegionValue;
    
    //initialization
    rc = 0;
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    RegionValue = 0;       

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);  
    sprintf(sql_buff,"SELECT* FROM Region WHERE ID != 9999;");
    //parpare
    rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt,NULL);
    if (SQLITE_OK !=rc || NULL == stmt)
    {
          printf("\nfetch region values prepare error!\n");
          sqlite3_close(db);
    }
    //execute
    rc = sqlite3_step(stmt);

    if(SQLITE_ROW == rc)
    {  
        GetRegionIsOk = true;                 
    }
    else
    {
        DEBUG_CONF("Sorry,No This Region Info,Please Check it!");
        GetRegionIsOk = false;
    }

    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    //应答消息封装
    if(GetRegionIsOk) 
    {           
                RegionValue = sqlite3_column_int(stmt,1);
                DEBUG_CONF("RegionValue: 0x%02x\n",RegionValue);
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x0c;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //Region Value: 
                send_data_buff[send_data_buff_len++] =(uint8_t)RegionValue;
                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                    DEBUG_CONF(" %d\n",errno);
                    fprintf(stderr,"send error: %s\n",strerror(errno));
                    ReconnectWith_Server(); 

                }
    }  
    else
    {
           DEBUG_CONF("Sorry,No Get Region Info,Please Check it!");
    }
    
    if ( stmt != NULL) 
            sqlite3_finalize(stmt);

    if ( sql_buff != NULL)
            free(sql_buff);
}

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
 *  |CN470-510_ASYNCHRONY     |       0xaa        | 
 *   ---------------------------------------------
 *  \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerConfRegionFreq(uint8_t *message_buff,int message_len)
{
    int rc;
    char *sql = (char*)malloc(100);
    uint32_t frequency;
    bool SetRegionIsOk    = false;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;
    //区域频段的值
    uint8_t RegionValue;
    
    //initialization
    rc = 0;
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    RegionValue = 0;       

    //获取服务器端配置region的信息
    RegionValue = message_buff[17];

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    //把8个信道的区域频段设置为统一值。
    sprintf(sql,"UPDATE Region SET Region_Info = %u WHERE ID != 9999",RegionValue);
    //execute
    rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg); 
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg);  

    if(rc != SQLITE_OK)
    {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
        SetRegionIsOk = false;
    }       
    else
    {
        SetRegionIsOk = true;
    }
    
    /* 读取网关信息 */
    if ( -1 == ReadGwInfo(gwinfo))
                DEBUG_CONF("ReadGwInfo error!\n");

    //应答消息封装
    if(SetRegionIsOk)
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x0d;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x00;
                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }
    }
    else
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x0d;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x01;
                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }

    }

    if( sql != NULL) 
            free(sql);
}

/*!
 * \brief 查询HUB端Rx1DRoffset信息                              
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetRx1DRoffsetInfo(void)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    char *sql = (char*)malloc(100);
    char *sql_buff = (char*)malloc(100);
    uint32_t frequency;
    bool GetRx1DRoffsetIsOk    = false;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;
    //rx1droffset的值
    uint8_t Rx1DRoffsetValue;
    
    //initialization
    rc = 0;
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    Rx1DRoffsetValue = 0;       

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);  
    sprintf(sql_buff,"SELECT* FROM Region WHERE ID = 1;");
    //parpare
    rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt,NULL);
    if (SQLITE_OK !=rc || NULL == stmt)
    {
          printf("\nfetch region values prepare error!\n");
          sqlite3_close(db);
    }
    //execute
    rc = sqlite3_step(stmt);

    if(SQLITE_ROW == rc)
    {    
        GetRx1DRoffsetIsOk = true;                 
    }
    else
    {
        DEBUG_CONF("Sorry,No This Rx1DRoffset Info,Please Check it!");
        GetRx1DRoffsetIsOk = false;
    }

    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    //应答消息封装
    if(GetRx1DRoffsetIsOk) 
    {
                Rx1DRoffsetValue = sqlite3_column_int(stmt,2);
                DEBUG_CONF("Rx1DRoffsetValue: %d\n",Rx1DRoffsetValue);
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x0e;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //Region Value: 
                send_data_buff[send_data_buff_len++] =(uint8_t)Rx1DRoffsetValue;
                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }
    }  
    else
    {
           DEBUG_CONF("Sorry,No Get Rx1DRoffset Info,Please Check it!");
    }

    if ( stmt != NULL) 
            sqlite3_finalize(stmt);
    
    if ( sql != NULL)
            free(sql);

    if ( sql_buff != NULL) 
            free(sql_buff);
}

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
 *  |CN470-510_ASYNCHRONY     |             [0:5]            |  
 *   --------------------------------------------------------
 *  \param [IN]： 服务器端传输的数据长度
 * 
 * \Returns   ： NULL.
 */
void ServerConfRx1DRoffsetInfo(uint8_t *message_buff, int message_len)
{
    int rc;
    char *sql = (char*)malloc(100);
    char *sql_buff = (char*)malloc(100);
    uint32_t frequency;
    bool SetRx1DRoffsetIsOk    = false;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;
    //区域频段的值
    uint8_t Rx1DRoffsetValue;
    
    //initialization
    rc = 0;
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    Rx1DRoffsetValue = 0;       

    //获取服务器端配置region的信息
    Rx1DRoffsetValue = message_buff[17];

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    //把8个信道的区域频段设置为统一值。
    sprintf(sql,"UPDATE Region SET Rx1DRoffset = %u WHERE ID != 9999",Rx1DRoffsetValue);
    //execute
    rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg); 
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg);  

    if(rc != SQLITE_OK)
    {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
        SetRx1DRoffsetIsOk = false;
    }       
    else
    {
        SetRx1DRoffsetIsOk = true;
    }
    
    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    //应答消息封装
    if(SetRx1DRoffsetIsOk)
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x0f;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x00;
                //计算校验码
                send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_buff_len++;
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }
    }
    else
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x0f;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x01;
                //计算校验码
                send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_buff_len++;
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }
    }

    if ( sql != NULL)
            free(sql);

    if ( sql_buff != NULL) 
            free(sql_buff);
}

/*!
 * \brief 获取HUB端上行信道频率信息                              
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetUplinkFreq(void)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    char *sql = (char*)malloc(100);
    char *sql_buff = (char*)malloc(100);
    bool GetUplinkFreqIsOk    = false;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    int freq_buff[8];
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;

    //initialization
    rc = 0;
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    memset(freq_buff,0,4);  

    for(int id = 1; id < 9; id++)//c_99
    {
        sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL); 
        sprintf(sql_buff,"SELECT* FROM Region WHERE ID = %u;",id);
        //parpare
        rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt,NULL);
        if (SQLITE_OK !=rc || NULL == stmt)
        {
          printf("\nfetch region values prepare error!\n");
          sqlite3_close(db);
        }
        //execute
        while(SQLITE_ROW == sqlite3_step(stmt))
        {
            freq_buff[id-1] = sqlite3_column_int(stmt,3);
            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
           // sqlite3_reset (stmt);
            GetUplinkFreqIsOk = true;
        }

    }
    
    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    //应答消息封装
    if(GetUplinkFreqIsOk)
    {
            //消息版本 1byte
            send_data_buff[send_data_buff_len++] = 0x01;
            //消息类型 2byte
            send_data_buff[send_data_buff_len++] = 0x03;
            send_data_buff[send_data_buff_len++] = 0x10;
            //HUB唯一编号
            mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
            send_data_buff_len+=8;
            //消息序号
            message_id++;
            send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
            send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
            //消息体属性       
            MessageHead.MessageBodyProperty_t.MessageBody_Length =48;//消息体字节数 
            MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
            MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
            MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
            send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
            //消息体
            for(int loop=0;loop < 8;loop++) //c99
            {       //tag
                    send_data_buff[send_data_buff_len++] =(loop+1);
                    //type
                    send_data_buff[send_data_buff_len++] = 0x06;
                    //value
                    send_data_buff[send_data_buff_len++] = (uint8_t)(freq_buff[loop] >> 24);
                    send_data_buff[send_data_buff_len++] = (uint8_t)(freq_buff[loop] >> 16);
                    send_data_buff[send_data_buff_len++] = (uint8_t)(freq_buff[loop] >>  8);
                    send_data_buff[send_data_buff_len++] = (uint8_t)(freq_buff[loop] >>  0);
            }
            //计算校验码
            send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
            //转义处理
            send_data_buff_len++;
            send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
            //添加后导码
            send_data[send_data_len] = 0x7e;
            send_data_len +=1;
            //进行数据发送
            tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
            if(tcp_send_len <= 0)
            {
                    DEBUG_CONF(" %d\n",errno);
                    fprintf(stderr,"send error: %s\n",strerror(errno));
                    ReconnectWith_Server(); 

            }

    }
    else
    {
          DEBUG_CONF("Sorry,Get Uplink Frequency Is Error,Please Check It!\n"); 
    }


    if ( stmt != NULL) 
            sqlite3_finalize(stmt);
    
    if ( sql != NULL)
            free(sql);

    if ( sql_buff != NULL) 
            free(sql_buff);
}

/*!
 * \brief 获取HUB端下行信道频率信息                              
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL.
 */
void ServerGetDownFreq(void)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    char *sql = (char*)malloc(100);
    char *sql_buff = (char*)malloc(100);
    bool GetDownlinkFreqIsOk    = false;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len;
    int freq_buff[8];
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;

    //initialization
    rc = 0;
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    memset(freq_buff,0,4);  

    for(int id = 1; id < 9; id++)//c_99
    {
        sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL); 
        sprintf(sql_buff,"SELECT* FROM Region WHERE ID = %u;",id);
        //parpare
        rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt,NULL);
        if (SQLITE_OK !=rc || NULL == stmt)
        {
          printf("\nfetch region values prepare error!\n");
          sqlite3_close(db);
        }
        //execute
        while(SQLITE_ROW == sqlite3_step(stmt))
        {
            freq_buff[id-1] = sqlite3_column_int(stmt,4);
            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
           // sqlite3_reset (stmt);
            GetDownlinkFreqIsOk = true;
        }

    }
    
    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    //应答消息封装
    if(GetDownlinkFreqIsOk)
    {
            //消息版本 1byte
            send_data_buff[send_data_buff_len++] = 0x01;
            //消息类型 2byte
            send_data_buff[send_data_buff_len++] = 0x03;
            send_data_buff[send_data_buff_len++] = 0x11;
            //HUB唯一编号
            mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
            send_data_buff_len+=8;
            //消息序号
            message_id++;
            send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
            send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
            //消息体属性       
            MessageHead.MessageBodyProperty_t.MessageBody_Length =48;//消息体字节数 
            MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
            MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
            MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
            send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
            //消息体
            for(int loop=0;loop < 8;loop++) //c99
            {       //tag
                    send_data_buff[send_data_buff_len++] =(loop+1);
                    //type
                    send_data_buff[send_data_buff_len++] = 0x06;
                    //value
                    send_data_buff[send_data_buff_len++] = (uint8_t)(freq_buff[loop] >> 24);
                    send_data_buff[send_data_buff_len++] = (uint8_t)(freq_buff[loop] >> 16);
                    send_data_buff[send_data_buff_len++] = (uint8_t)(freq_buff[loop] >>  8);
                    send_data_buff[send_data_buff_len++] = (uint8_t)(freq_buff[loop] >>  0);
            }
            //计算校验码
            send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
            //转义处理
            send_data_buff_len++;
            send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
            //添加后导码
            send_data[send_data_len] = 0x7e;
            send_data_len +=1;
            //进行数据发送
            tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
            if(tcp_send_len <= 0)
            {
                    DEBUG_CONF(" %d\n",errno);
                    fprintf(stderr,"send error: %s\n",strerror(errno));
                    ReconnectWith_Server(); 

            }
            
    }
    else
    {
          DEBUG_CONF("Sorry,Get Uplink Frequency Is Error,Please Check It!\n"); 
    }

    if ( stmt != NULL) 
            sqlite3_finalize(stmt);
    
    if ( sql != NULL)
            free(sql);

    if ( sql_buff != NULL) 
            free(sql_buff);
}

/*!
 * \brief 设置HUB端上行信道频率信息                              
 *
 * \param [IN]：服务器端传输的数据：ch0~ch7信道频率 单位:Hz
 * \param [IN]：服务器端传输的数据长度
 * \Returns   ：NULL.
 */
void ServerConfUplinkFreq(uint8_t *message_buff, int message_len)
{
    uint32_t ch_freq[8];
    memset(ch_freq,0,8);
    int rc;
    char *sql = (char*)malloc(100);
    bool SetUplinkFreqIsOk    = false;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len; 
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;

    //initialization
    rc = 0;
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    
    //获取服务端的上行信道频率
    for(int i = 0,index = 20; i < 8; i++) //index 为频率的地址
    {
        ch_freq[i]  =  message_buff[index];
        ch_freq[i] |= (message_buff[index-1] << 8);
        ch_freq[i] |= (message_buff[index-2] << 16);
        ch_freq[i] |= (message_buff[index-3] << 24);
        index+=6; //移动到下一个频率的地址
    }  

    //更新表中上行信道频率信息
    for(int i=0;i < 8; i++)
    {
        sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
        sprintf(sql,"UPDATE Region SET UpLinkFreq = %u WHERE ID = %u;",ch_freq[i],i+1);//id=i+1
        rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg);
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg); 
    }    
    if(rc != SQLITE_OK)
    {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
        SetUplinkFreqIsOk = false;
    }
    else
    {
        SetUplinkFreqIsOk = true;
    }
    
    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    //消息数据封装
    if(SetUplinkFreqIsOk)//更新成功，回复应答消息
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x12;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x00;
                //计算校验码
                send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_buff_len++;
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }

    }
    else //更新失败，回复应答消息
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x12;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x01;
                //计算校验码
                send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_buff_len++;
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }
    }


    if ( sql != NULL)
            free(sql);
}

/*!
 * \brief 设置HUB端下行信道频率信息                              
 *
 * \param [IN]：服务器端传输的数据：ch0~ch7信道频率 单位:Hz
 * \param [IN]：服务器端传输的数据长度
 * \Returns   ：NULL.
 */
void ServerConfDownFreq(uint8_t *message_buff, int message_len)
{
    uint32_t ch_freq[8];
    memset(ch_freq,0,8);
    int rc;
    char *sql = (char*)malloc(100);
    bool SetDownlinkFreqIsOk    = false;
    uint8_t send_data_buff[100];
    uint8_t send_data[100];
    int send_data_buff_len;
    int send_data_len; 
    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    //消息体属性
    MessageHead_t MessageHead;

    //initialization
    rc = 0;
    memset(send_data_buff,0,100);
    memset(send_data,0,100);
    send_data_buff_len = 0;
    send_data_len = 0;
    memset(gwinfo,0,8);
    message_id = 0;
    memset(&MessageHead,0,sizeof(MessageHead_t));
    
    //获取服务端的上行信道频率
    for(int i = 0,index = 20; i < 8; i++) //index 为频率的地址
    {
        ch_freq[i]  =  message_buff[index];
        ch_freq[i] |= (message_buff[index-1] << 8);
        ch_freq[i] |= (message_buff[index-2] << 16);
        ch_freq[i] |= (message_buff[index-3] << 24);
        index+=6; //移动到下一个频率的地址
    }  

    //更新表中上行信道频率信息
    for(int i=0;i < 8; i++)
    {
        sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
        sprintf(sql,"UPDATE Region SET DownLinkFreq = %u WHERE ID = %u;",ch_freq[i],i+1);//id=i+1
        rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg);
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg); 
    }    
    if(rc != SQLITE_OK)
    {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
        SetDownlinkFreqIsOk = false;
    }
    else
    {
        SetDownlinkFreqIsOk = true;
    }
    
    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    //消息数据封装
    if(SetDownlinkFreqIsOk)//更新成功，回复应答消息
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x13;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x00;
                //计算校验码
                send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_buff_len++;
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }

    }
    else //更新失败，回复应答消息
    {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x13;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail
                send_data_buff[send_data_buff_len++] = 0x01;
                //计算校验码
                send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_buff_len++;
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                }
    }

    if ( sql != NULL)
            free(sql);
}


/*!
 * \brief:  根据freq_hz 和 datarate 判断每包数据的最大载荷                             
 *
 * \param [IN] : 频率、速率
 * \param [OUT]：最大载荷大小
 * \Returns    ： 0： 成功，  -1：失败
 */
int CalculateDownlinkMaxpayload( const uint32_t *freq, const uint8_t *datarate, int *maxpayload)
{
        uint32_t freq_hz;
        uint8_t  dataRate;

        /* 定义枚举变量，存储不同区域 */
        enum Region{ eu863 = 1, us902, cn780, eu433, au915, cn470, as923, kr920, india865} region;

        if ( freq == NULL)
                return -1;
        
        if ( datarate == NULL) 
                return -1;

        freq_hz   = *freq;
        dataRate  = *datarate;

        DEBUG_SERVER("fpending: freq_hz:  %u\n", freq_hz);   
        DEBUG_SERVER("fpending: dataRate: %u\n", dataRate);  

        /* 判断频率所在区域 */
        if ( ( freq_hz >= 863000000) && (freq_hz <= 870000000))
                region = eu863;

        if ( ( freq_hz >= 902000000) && (freq_hz <= 928000000))
                region = us902;

        if ( ( freq_hz >= 779000000) && (freq_hz <= 787000000))
                region = cn780;
        
        if (   freq_hz == 433000000 )
                region = eu433;

        if ( ( freq_hz >= 915000000) && (freq_hz <= 928000000))
                region = au915;
        
        if ( ( freq_hz >= 470000000) && (freq_hz <= 510000000))
                region = cn470;
        
        if (   freq_hz == 923000000 )
                region = as923;

        if ( ( freq_hz >= 920000000) && (freq_hz < 923000000))
                region = kr920;

        if ( ( freq_hz >= 865000000) && (freq_hz <= 867000000))
                region = india865;

        
        DEBUG_SERVER("fpending: region: %u\n", region);  
        
        switch (region){
        
        case eu863:
        {    
            switch (dataRate){
            case  0: *maxpayload = 51;  break;
            case  1: *maxpayload = 51;  break; 
            case  2: *maxpayload = 51;  break; 
            case  3: *maxpayload = 115; break; 
            case  4: *maxpayload = 222; break; 
            case  5: *maxpayload = 222; break; 
            case  6: *maxpayload = 222; break; 
            case  7: *maxpayload = 222; break; 
            default: break;
            }
        break;
        } 

        case us902:
        {    
            switch (dataRate){
            case  0:  *maxpayload = 11;  break;
            case  1:  *maxpayload = 53;  break; 
            case  2:  *maxpayload = 125;  break; 
            case  3:  *maxpayload = 242; break; 
            case  4:  *maxpayload = 242; break;
            case  8:  *maxpayload =  33; break; 
            case  9:  *maxpayload = 109; break; 
            case  10: *maxpayload = 222; break; 
            case  11: *maxpayload = 222; break; 
            case  12: *maxpayload = 222; break; 
            case  13: *maxpayload = 222; break; 
            default: break;
            }
        break;
        }                       
        
        case cn780:
        {    
            switch (dataRate){
            case  0: *maxpayload = 51;  break;
            case  1: *maxpayload = 51;  break; 
            case  2: *maxpayload = 51;  break; 
            case  3: *maxpayload = 115; break; 
            case  4: *maxpayload = 222; break; 
            case  5: *maxpayload = 222; break; 
            case  6: *maxpayload = 242; break; 
            case  7: *maxpayload = 222; break; 
            default: break;
            }
        break;
        }             

        case eu433:
        {    
            switch (dataRate){
            case  0: *maxpayload = 51;  break;
            case  1: *maxpayload = 51;  break; 
            case  2: *maxpayload = 51;  break; 
            case  3: *maxpayload = 115; break; 
            case  4: *maxpayload = 222; break; 
            case  5: *maxpayload = 222; break; 
            case  6: *maxpayload = 222; break; 
            case  7: *maxpayload = 222; break; 
            default: break;
            }
        break;
        }      
                        
        case au915:
        {    
            switch (dataRate){
            case  0:  *maxpayload =  51;  break;
            case  1:  *maxpayload =  51;  break; 
            case  2:  *maxpayload =  51;  break; 
            case  3:  *maxpayload = 115;  break; 
            case  4:  *maxpayload = 222;  break;
            case  5:  *maxpayload = 222;  break;
            case  6:  *maxpayload = 222;  break;
            case  8:  *maxpayload =  33;  break; 
            case  9:  *maxpayload = 109;  break; 
            case  10: *maxpayload = 222;  break; 
            case  11: *maxpayload = 222;  break; 
            case  12: *maxpayload = 222;  break; 
            case  13: *maxpayload = 222;  break; 
            default: break;
            }
        break;
        }

        case cn470:
        {    
            switch (dataRate){
            case  0: *maxpayload = 51;  break;
            case  1: *maxpayload = 51;  break; 
            case  2: *maxpayload = 51;  break; 
            case  3: *maxpayload = 115; break; 
            case  4: *maxpayload = 222; break; 
            case  5: *maxpayload = 222; break; 
            default: break;
            }
        break;
        }       

        case as923:
        {    
            switch (dataRate){
            case  0: *maxpayload = 59;  break;
            case  1: *maxpayload = 59;  break; 
            case  2: *maxpayload = 59;  break; 
            case  3: *maxpayload = 123; break; 
            case  4: *maxpayload = 250; break; 
            case  5: *maxpayload = 250; break; 
            case  6: *maxpayload = 250; break; 
            case  7: *maxpayload = 250; break; 
            default: break;
            }
        break;
        }  
    
        case kr920:
        {    
            switch (dataRate){
            case  0: *maxpayload = 51;  break;
            case  1: *maxpayload = 51;  break; 
            case  2: *maxpayload = 51;  break; 
            case  3: *maxpayload = 115; break; 
            case  4: *maxpayload = 222; break; 
            case  5: *maxpayload = 222; break; 
            default: break;
            }
        break;
        } 

        case india865:
        {    
            switch (dataRate){
            case  0: *maxpayload = 59;  break;
            case  1: *maxpayload = 59;  break; 
            case  2: *maxpayload = 59;  break; 
            case  3: *maxpayload = 123; break; 
            case  4: *maxpayload = 250; break; 
            case  5: *maxpayload = 250; break; 
            case  6: *maxpayload = 250; break; 
            case  7: *maxpayload = 250; break; 
            default: break;
            }
        break;
        } 
       
        default:
                break;
        }

       DEBUG_SERVER("fpending: maxpayload: %u\n", *maxpayload); 
       return 0; 
        
}

/*
    brief:          读取网关eui信息
    parameter out:  网关信息
    return:         0: success 1; fail
*/
int ReadGwInfo(uint8_t *buffer)
{
        
        int fd;
        char *info_str = (char*)malloc(50);

        if ( buffer == NULL)
                return -1;

        fd = open("/lorawan/lorawan_hub/gwinfo",O_RDWR);
        if( -1 == fd){
                DEBUG_CONF("sorry,There is no gwinfo file in this directory,Please check it!\n");
                close(fd);
                return -1;   
        }

        if ( (read(fd, info_str,16)) == -1) {
                DEBUG_CONF("read gwinfo error!\n");
                return -1;     
        }

        if ( info_str != NULL)
                String_To_ChArray(buffer,info_str,8);      

        if ( close(fd) == -1) {
                DEBUG_CONF("close gwinfo error!\n");
                return -1;   
        }

        if ( info_str != NULL)
                free(info_str);

        return 0;  
}

/*
     brief       :   服务器添加ABP节点                              
     parameter in：  服务器端传输的ABP节点数据
     parameter in：  服务器端传输的数据长度
     return      ：  null
 */
void ServerAddAbpNode(uint8_t *message_buff, int message_len)
{
        
        /*
                思路：
                        1: 提取devaddr信息 查找数据库中是否有该devaddr 如果已经存在则返回0x05 否则进行下面的操作 
                        2: 提取deveui,在数据库中进行寻找
                        3: 找到后，将原数据进行覆盖 
                        4: 没找到，将该节点信息作为新的节点信息进行插入  
        */
        int rc;
        sqlite3_stmt    *stmt_devaddr  = NULL;  
        sqlite3_stmt    *stmt          = NULL;
        sqlite3_stmt    *stmt_searchid = NULL;
        uint8_t          deveui[8];
        uint8_t          devaddr[4];
        uint8_t          nwkskey[16];
        uint8_t          appskey[16];
        bool  DevaddrIsExist =  false;
        bool  SeekDeveuiIsOk =  false;
        bool  InsertNodeIsOk =  false;  
        char *search_adddr   = (char*)malloc(100);
        char *deveui_str     = (char*)malloc(2*8);
        char *deveui_sql     = (char*)malloc(200);
        char *deveui_p       =  NULL;

        char *devaddr_str    = (char*)malloc(2*4);
        char *devaddr_sql    = (char*)malloc(150);
        char *devaddr_p      =  NULL;

        char *nwkskey_str    = (char*)malloc(2*16);
        char *nwkskey_sql    = (char*)malloc(150);
        char *nwkskey_p      =  NULL;

        char *appskey_str    = (char*)malloc(2*16);
        char *appskey_sql    = (char*)malloc(150);
        char *appskey_p      =  NULL;

        char *search_sql     = (char*)malloc(100);
        char *insertnode_sql = (char*)malloc(230); 
        char *appkey         =  NULL;

        uint8_t send_data_buff[100];
        uint8_t send_data[100];
        int     send_data_buff_len;
        int     send_data_len;

        //GWinfo
        uint8_t  gwinfo[8];
        static uint32_t message_id;
        //消息体属性
        MessageHead_t MessageHead;
        //max id
        int IdValue;
        int maxid;

        //将表中的ID按降序排布，即寻找表中ID最大值
        char *sql_search = NULL;
        sql_search = "SELECT* FROM GWINFO ORDER BY ID DESC LIMIT 1";

        /* 填充appkey字段 */
        appkey = "ffffffffffffffffffffffffffffffff";

        //initialization
        memset(deveui,  0,  8);
        memset(devaddr, 0,  4);
        memset(nwkskey, 0, 16);
        memset(appskey, 0, 16);
        memset(send_data_buff,0,100);
        memset(send_data,0,100);
        send_data_buff_len = 0;
        send_data_len = 0;
        memset(gwinfo,0,8);
        message_id = 0;
        memset(&MessageHead,0,sizeof(MessageHead_t));
        IdValue = 0;

        /* 信息提取 */

        /* deveui index:  17 */
        mymemcpy(deveui,  message_buff+17, 8);

        /* devaddr index: 27 */
        mymemcpy(devaddr, message_buff+27, 4);
        
        /* appskey index: 33 */
        mymemcpy(appskey, message_buff+33, 16);

        /* nwkskey index: 51 */
        mymemcpy(nwkskey, message_buff+51, 16);

        deveui_p  = ChArray_To_String(deveui_str,   deveui,    8);
        devaddr_p = ChArray_To_String(devaddr_str,  devaddr,   4);
        nwkskey_p = ChArray_To_String(appskey_str,  appskey,  16);
        appskey_p = ChArray_To_String(nwkskey_str,  nwkskey,  16);


        /* 读取网关的信息 */
        if ( -1 == ReadGwInfo(gwinfo))
                DEBUG_CONF("ReadGwInfo error!\n");

        /* 在GWINFO中寻找Devaddr是否已经存在 */
        sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL); 
        sprintf(search_adddr,"SELECT* FROM GWINFO WHERE Devaddr ='%s';",devaddr_p);
        rc = sqlite3_prepare_v2(db,search_adddr,strlen(search_adddr),&stmt_devaddr,NULL);
        if ( SQLITE_OK !=rc || NULL == stmt_devaddr){
                DEBUG_CONF("\n serach devaddr prepare error!\n");
                sqlite3_close(db); 
        }
        rc = sqlite3_step(stmt_devaddr);
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL); 
        
        if ( SQLITE_ROW == rc) {/*  找到devaddr */
                
                DevaddrIsExist = true;
                sqlite3_finalize(stmt_devaddr);  

        }else{
                
                DevaddrIsExist = false;
                sqlite3_finalize(stmt_devaddr);  
        }

        /* 地址冲突: 返回0x02 */
        if ( DevaddrIsExist)
        {
                //消息版本 1byte
                send_data_buff[send_data_buff_len++] = 0x01;
                //消息类型 2byte
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x14;
                //HUB唯一编号
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                //消息序号
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                //消息体属性       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =10+3;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                
                //消息体
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x0a;
                //value
                mymemcpy(send_data_buff+send_data_buff_len,deveui,8);
                send_data_buff_len+=8;

                //tag
                send_data_buff[send_data_buff_len++] = 0x02;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                //value: 0 successful  1 fail 2:address conflict 3:deveui conflict
                send_data_buff[send_data_buff_len++] = 0x02;
                //计算校验码
                send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_buff_len++;
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                if(tcp_send_len <= 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 

                } 

        }
        else /* 地址不冲突 */
        { 
                /* sqlite:开启事务,提升查找速度 */     
                sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);  
                sprintf(deveui_sql,"SELECT* FROM GWINFO WHERE DEVEui = '%s';",deveui_p);

                rc = sqlite3_prepare_v2(db,deveui_sql,strlen(deveui_sql),&stmt,NULL);
                if ( SQLITE_OK !=rc || NULL == stmt){
                
                        DEBUG_CONF("\n ack write node prepare error!\n");
                        sqlite3_close(db); 
                }

                /* 执行绑定后的sql语句 */  
                rc = sqlite3_step(stmt);

                /* sqlite:关闭事物 */
                sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL); 

                /* 判断是否成功找到 */
                if ( SQLITE_ROW == rc) {
                        
                        SeekDeveuiIsOk = true;
                        DEBUG_CONF("SeekDeveuiIsOk is true\n");

                }else{
                        
                        SeekDeveuiIsOk = false;
                        DEBUG_CONF("SeekDeveuiIsOk is false\n");
                }

                /* 在GWINFO表中成功找到该节点，则返回0x03:deveui冲突 */   
                if ( SeekDeveuiIsOk) 
                {
                        //消息版本 1byte
                        send_data_buff[send_data_buff_len++] = 0x01;
                        //消息类型 2byte
                        send_data_buff[send_data_buff_len++] = 0x03;
                        send_data_buff[send_data_buff_len++] = 0x14;
                        //HUB唯一编号
                        mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                        send_data_buff_len+=8;
                        //消息序号
                        message_id++;
                        send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                        send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                        //消息体属性       
                        MessageHead.MessageBodyProperty_t.MessageBody_Length =10+3;//消息体字节数 
                        MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                        MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                        MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                        send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                        send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                        
                        //消息体
                        //tag
                        send_data_buff[send_data_buff_len++] = 0x01;
                        //type
                        send_data_buff[send_data_buff_len++] = 0x0a;
                        //value
                        mymemcpy(send_data_buff+send_data_buff_len,deveui,8);
                        send_data_buff_len+=8;

                        //tag
                        send_data_buff[send_data_buff_len++] = 0x02;
                        //type
                        send_data_buff[send_data_buff_len++] = 0x02;
                        //value: 0 successful  1 fail 2:address conflict 3:deveui conflict
                        send_data_buff[send_data_buff_len++] = 0x03;
                        //计算校验码
                        send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
                        //转义处理
                        send_data_buff_len++;
                        send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                        //添加后导码
                        send_data[send_data_len] = 0x7e;
                        send_data_len +=1;
                        //进行数据发送
                        tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                        if(tcp_send_len <= 0)
                        {
                                DEBUG_CONF(" %d\n",errno);
                                fprintf(stderr,"send error: %s\n",strerror(errno));
                                ReconnectWith_Server(); 

                        } 

                }
                else  /* deveui不存在，插入一个新的ABP节点 */
                {
                   
                        /**
                         * 将表中的id按降序排布，寻找表中id的最大值 
                         * 
                         *  ascending   order: 升序
                         *  descending  order: 降序
                         */
                        sprintf (search_sql,"SELECT* FROM GWINFO ORDER BY ID DESC LIMIT 1");
                        DEBUG_CONF("search_sql: %s\n",search_sql);

                        /* search max id --> id+1 --> insert new node */
                        sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);
                        rc = sqlite3_prepare_v2(db,search_sql,strlen(search_sql),&stmt_searchid,NULL);
                        if( SQLITE_OK !=rc || NULL == stmt_searchid) {
                                
                                DEBUG_CONF("\n parse Prepare error! \n");
                                sqlite3_close(db);
                        }
                        while(SQLITE_ROW == sqlite3_step(stmt_searchid)){   
                                maxid = sqlite3_column_int(stmt_searchid,0);
                                sqlite3_finalize(stmt_searchid);
                        }        
                
                        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);  
                        
                        DEBUG_CONF("maxid: %d\n",maxid);

                        maxid +=1;


                        sqlite3_exec(db,"BEGIN  TRANSACTION;", NULL,NULL,&zErrMsg);
                        sprintf(insertnode_sql,"INSERT INTO GWINFO (ID,APPKey,DEVEui,Devaddr,Nwkskey,APPskey) VALUES (%d,'%s','%s','%s','%s','%s');",\
                                                maxid,appkey,deveui_p,devaddr_p,nwkskey_p,appskey_p);
                        
                        DEBUG_CONF("insert_sql: %s\n",insertnode_sql);

                        rc = sqlite3_exec(db, insertnode_sql, 0, 0, &zErrMsg);
                        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
                        if( rc != SQLITE_OK ){
                                DEBUG_CONF("ack insert new appkey node error!: %s\n", zErrMsg);
                                sqlite3_free(zErrMsg);
                                InsertNodeIsOk = false;
                                DEBUG_CONF("InsertNodeIsOk is false\n");
                        }else{
                                
                                InsertNodeIsOk = true;
                                DEBUG_CONF("InsertNodeIsOk is true\n");
                        } 

                        /* 插入成功 */
                        if ( InsertNodeIsOk) 
                        {                       
                                //消息版本 1byte
                                send_data_buff[send_data_buff_len++] = 0x01;
                                //消息类型 2byte
                                send_data_buff[send_data_buff_len++] = 0x03;
                                send_data_buff[send_data_buff_len++] = 0x14;
                                //HUB唯一编号
                                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                                send_data_buff_len+=8;
                                //消息序号
                                message_id++;
                                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                                //消息体属性       
                                MessageHead.MessageBodyProperty_t.MessageBody_Length =10+3;//消息体字节数 
                                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                                
                                //消息体
                                //tag
                                send_data_buff[send_data_buff_len++] = 0x01;
                                //type
                                send_data_buff[send_data_buff_len++] = 0x0a;
                                //value
                                mymemcpy(send_data_buff+send_data_buff_len,deveui,8);
                                send_data_buff_len+=8;

                                //tag
                                send_data_buff[send_data_buff_len++] = 0x02;
                                //type
                                send_data_buff[send_data_buff_len++] = 0x02;
                                //value: 0 successful  1 fail 2:address conflict 3:deveui conflict
                                send_data_buff[send_data_buff_len++] = 0x00;
                                //计算校验码
                                send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
                                //转义处理
                                send_data_buff_len++;
                                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                                //添加后导码
                                send_data[send_data_len] = 0x7e;
                                send_data_len +=1;
                                //进行数据发送
                                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                                if(tcp_send_len <= 0)
                                {
                                        DEBUG_CONF(" %d\n",errno);
                                        fprintf(stderr,"send error: %s\n",strerror(errno));
                                        ReconnectWith_Server(); 

                                } 

                        }
                        else /* 插入失败 */
                        {
                                
                                //消息版本 1byte
                                send_data_buff[send_data_buff_len++] = 0x01;
                                //消息类型 2byte
                                send_data_buff[send_data_buff_len++] = 0x03;
                                send_data_buff[send_data_buff_len++] = 0x14;
                                //HUB唯一编号
                                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                                send_data_buff_len+=8;
                                //消息序号
                                message_id++;
                                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                                //消息体属性       
                                MessageHead.MessageBodyProperty_t.MessageBody_Length =10+3;//消息体字节数 
                                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                                
                                //消息体
                                //tag
                                send_data_buff[send_data_buff_len++] = 0x01;
                                //type
                                send_data_buff[send_data_buff_len++] = 0x0a;
                                //value
                                mymemcpy(send_data_buff+send_data_buff_len,deveui,8);
                                send_data_buff_len+=8;

                                //tag
                                send_data_buff[send_data_buff_len++] = 0x02;
                                //type
                                send_data_buff[send_data_buff_len++] = 0x02;
                                //value: 0 successful  1 fail 2:address conflict 3:deveui conflict
                                send_data_buff[send_data_buff_len++] = 0x01;
                                //计算校验码
                                send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
                                //转义处理
                                send_data_buff_len++;
                                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                                //添加后导码
                                send_data[send_data_len] = 0x7e;
                                send_data_len +=1;
                                //进行数据发送
                                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                                if(tcp_send_len <= 0)
                                {
                                        DEBUG_CONF(" %d\n",errno);
                                        fprintf(stderr,"send error: %s\n",strerror(errno));
                                        ReconnectWith_Server(); 

                                }        

                        }
                        
                }
                
        }

       
        /* 释放堆区资源 */    
        if ( deveui_p != NULL ) 
                free(deveui_p); 

        if ( devaddr_p != NULL ) 
                free(devaddr_p); 

        if ( nwkskey_p != NULL ) 
                free(nwkskey_p); 

        if ( appskey_p != NULL ) 
                free(appskey_p); 

        if ( search_sql != NULL ) 
                free(search_sql); 

        if ( deveui_sql != NULL ) 
                free(deveui_sql); 
        
        if ( devaddr_sql != NULL ) 
                free(devaddr_sql); 

        if ( nwkskey_sql != NULL ) 
                free(nwkskey_sql); 
        
        if ( appskey_sql != NULL ) 
                free(appskey_sql); 

        if ( insertnode_sql != NULL ) 
                free(insertnode_sql); 

        if ( search_adddr != NULL ) 
                free(search_adddr); 

        if ( stmt != NULL ) 
                sqlite3_finalize(stmt);

        //clear
        memset(send_data_buff,0,100);
        memset(send_data,0,100);
        send_data_buff_len = 0;
        send_data_len = 0;
        memset(gwinfo,0,8);
        memset(&MessageHead,0,sizeof(MessageHead_t));
        IdValue = 0;

}


/*
     brief       :   服务器读取ABP节点                              
     parameter in：  服务器端传输的ABP节点数据
     parameter in：  服务器端传输的数据长度
     return      ：  null
 */
void ServerReadAbpNode(uint8_t *message_buff, int message_len)
{
    
    /* 创建需要的变量 */ 
    int rc;
    int fd;
    sqlite3_stmt    *stmt = NULL;
    uint8_t          deveui[8];
    uint8_t          devaddr[4];
    uint8_t          nwkskey[16];
    uint8_t          appskey[16];
    bool  SeekDeveuiIsOk =  false;
    char *deveui_str     = (char*)malloc(2*8);
    char *deveui_sql     = (char*)malloc(100);
    char *deveui_p       =  NULL;
    const char *devaddr_str    = NULL;
    const char *nwkskey_str    = NULL;
    const char *appskey_str    = NULL;
    uint8_t send_data_buff[300];
    uint8_t send_data[100];
    int     send_data_buff_len;
    int     send_data_len;

    //GWinfo
    uint8_t  gwinfo[8];
    static uint32_t message_id;
    
    //消息体属性
    MessageHead_t MessageHead;

    /* 变量初始化 */ 
    memset(deveui,  0,  8);
    memset(devaddr, 0,  4);
    memset(nwkskey, 0, 16);
    memset(appskey, 0, 16);

    /* 将deveui有效字节转换为字符串格式 */
    mymemcpy(deveui,  message_buff+17, 8);
    deveui_p  = ChArray_To_String(deveui_str,  deveui, 8);

    /* sqlite:开启事务,提升查找速度 */     
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);  
    sprintf(deveui_sql,"SELECT* FROM GWINFO WHERE DEVEui ='%s';",deveui_p);
    DEBUG_CONF("deveui_sql: %s\n",deveui_sql);
    rc = sqlite3_prepare_v2(db,deveui_sql,strlen(deveui_sql),&stmt,NULL);
    if ( SQLITE_OK !=rc || NULL == stmt){
           DEBUG_CONF("\n ack write node prepare error!\n");
           sqlite3_close(db); 
    }

    /* 执行绑定后的sql语句 */  
    rc = sqlite3_step(stmt);

     /* sqlite:关闭事物 */
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL); 

     /* 判断是否成功找到 */
    if ( SQLITE_ROW == rc) {
        
        SeekDeveuiIsOk = true;
      
    }else{
        
        SeekDeveuiIsOk = false;
    }

    /* 读取网关的信息 */
    if ( -1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");


    if ( SeekDeveuiIsOk) {
           
            /* *_str 经过转换后变成const型,不用释放，由系统回收 */
            devaddr_str = sqlite3_column_text(stmt,3);            
            nwkskey_str = sqlite3_column_text(stmt,4);    
            appskey_str = sqlite3_column_text(stmt,5);    

            if ( devaddr_str != NULL) 
                    String_To_ChArray(devaddr, devaddr_str,  4);
            
             if ( nwkskey_str != NULL) 
                    String_To_ChArray(nwkskey, nwkskey_str, 16);

             if ( appskey_str != NULL) 
                    String_To_ChArray(appskey, appskey_str, 16);

            /* 应答数据封装 */
                        
            //消息版本 1byte
            send_data_buff[send_data_buff_len++] = 0x01;
            //消息类型 2byte
            send_data_buff[send_data_buff_len++] = 0x03;
            send_data_buff[send_data_buff_len++] = 0x15;
            //HUB唯一编号
            mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
            send_data_buff_len+=8;
            //消息序号
            message_id++;
            send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
            send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
            //消息体属性       
            MessageHead.MessageBodyProperty_t.MessageBody_Length =52;//消息体字节数 
            MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
            MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
            MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
            send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
            send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
            
            //消息体
            //tag
            send_data_buff[send_data_buff_len++] = 0x01;
            //type
            send_data_buff[send_data_buff_len++] = 0x0a;
            //deveui value
            mymemcpy(send_data_buff+send_data_buff_len,deveui,8);
            send_data_buff_len +=8;

            //tag
            send_data_buff[send_data_buff_len++] = 0x02;
            //type
            send_data_buff[send_data_buff_len++] = 0x06;
            //devaddr value
            mymemcpy(send_data_buff+send_data_buff_len,devaddr,4);
            send_data_buff_len +=4;    

            //tag
            send_data_buff[send_data_buff_len++] = 0x03;
            //type
            send_data_buff[send_data_buff_len++] = 0x0c;
            //appskey value
            mymemcpy(send_data_buff+send_data_buff_len,appskey,16);
            send_data_buff_len +=16;

            //tag
            send_data_buff[send_data_buff_len++] = 0x04;
            //type
            send_data_buff[send_data_buff_len++] = 0x0c;
            //appskey value
            mymemcpy(send_data_buff+send_data_buff_len,nwkskey,16);
            send_data_buff_len +=16;   

            //计算校验码
            send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
            //转义处理
            send_data_buff_len++;
            send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
            //添加后导码
            send_data[send_data_len] = 0x7e;
            send_data_len +=1;
            //进行数据发送
            tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
            if(tcp_send_len <= 0)
            {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send error: %s\n",strerror(errno));
                        ReconnectWith_Server(); 
            }        
            
    }
    else /* 应答错误: 暂时不处理 */
    { 

    }
    
    /* 释放堆区资源 */
    if ( deveui_sql != NULL)
            free(deveui_sql);

    if ( deveui_p != NULL)
        free(deveui_p);

    if (stmt != NULL)
            sqlite3_finalize(stmt);

}

/*
     brief       :   读取远程通信服务器ip和端口信息                                 
     parameter in：  服务器端传输的数据地址
     parameter in：  服务器端传输的数据长度
     return      ：  null
 */
void ReadCommunicationServerInfo(uint8_t *message_buff, int message_len)
{

        int fd_server;
        uint8_t send_buff[11];
        uint8_t server_ip_buff[4];
        char *read_cfg_path = "/lorawan/lorawan_hub/server_conf.json";
        char conf_obj[]     = "Communicate_conf";  
        char  *ip_name      = "communicate_ip"; 
        char  *ip_port      = "communicate_port";

        /* 读取的服务器地址 */ 
        const char  *server_address   =  NULL;
        /* 读取的服务器端口 */    
        uint16_t server_port;
        int ip_len;

        JSON_Value  *read_val;
        JSON_Object *read       = NULL;
        JSON_Object *read_conf  = NULL;
        uint8_t send_data_buff[300];
        uint8_t send_data[100];
        int     send_data_buff_len;
        int     send_data_len;

        //GWinfo
        uint8_t  gwinfo[8];
        static uint32_t message_id;

        //消息体属性
        MessageHead_t MessageHead;

        /* init */
        memset(send_buff,0,11);

        /* 读取json文件信息 */    
        if(access(read_cfg_path,R_OK) == 0)
        {
                DEBUG_CONF(" %s file is exit, parsing this file\n",read_cfg_path);

                read_val = json_parse_file_with_comments(read_cfg_path);
                read     = json_value_get_object(read_val);
                if(read == NULL)
                {
                DEBUG_CONF("ERROR: %s is not a valid JSON file\n",read_cfg_path);
                exit(EXIT_FAILURE);
                }
                read_conf = json_object_get_object(read,conf_obj);
                if(read_conf == NULL)
                {
                DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj);
                }
                server_address = json_object_get_string(read_conf,ip_name);
                server_port    = json_object_dotget_number(read_conf,ip_port);  
        }

        DEBUG_CONF("communicate_ip:      %s\n",server_address);
        DEBUG_CONF("communicate_port:    %d\n",server_port);

        ip_len = strlen(server_address);
        memset(server_ip_buff,0,4);

        /* 将字符串ip地址转换成hex */
        Ip_to_Charray(server_ip_buff,server_address,ip_len);

        /* 读取网关的信息 */
        if ( -1 == ReadGwInfo(gwinfo))
                DEBUG_CONF("ReadGwInfo error!\n");

        /* 应答数据封装 */
                
        //消息版本 1byte
        send_data_buff[send_data_buff_len++] = 0x01;
        //消息类型 2byte
        send_data_buff[send_data_buff_len++] = 0x03;
        send_data_buff[send_data_buff_len++] = 0x16;
        //HUB唯一编号
        mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
        send_data_buff_len+=8;
        //消息序号
        message_id++;
        send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
        send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
        //消息体属性       
        MessageHead.MessageBodyProperty_t.MessageBody_Length =10;//消息体字节数 
        MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
        MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
        MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
        send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
        send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
        
        //消息体
        //tag
        send_data_buff[send_data_buff_len++] = 0x01;
        //type
        send_data_buff[send_data_buff_len++] = 0x06;
        //ip value
        send_data_buff[send_data_buff_len++] = server_ip_buff[0];
        send_data_buff[send_data_buff_len++] = server_ip_buff[1];
        send_data_buff[send_data_buff_len++] = server_ip_buff[2];
        send_data_buff[send_data_buff_len++] = server_ip_buff[3];

        //tag
        send_data_buff[send_data_buff_len++] = 0x02;
        //type
        send_data_buff[send_data_buff_len++] = 0x04;
        //port value
        send_data_buff[send_data_buff_len++] = (uint8_t)(server_port>>8);
        send_data_buff[send_data_buff_len++] = (uint8_t) server_port;

        //计算校验码
        send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
        //转义处理
        send_data_buff_len++;
        send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
        //添加后导码
        send_data[send_data_len] = 0x7e;
        send_data_len +=1;
        //进行数据发送
        tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
        if(tcp_send_len <= 0)
        {
                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"send error: %s\n",strerror(errno));
                ReconnectWith_Server(); 
        }     


        /* 释放json文件指针 */
        json_serialize_to_file(read_val,read_cfg_path); 
        json_value_free(read_val);

}


/*
     brief       :   写远程通信服务器ip和端口信息                                 
     parameter in：  服务器端传输的数据地址
     parameter in：  服务器端传输的数据长度
     return      ：  null
*/
void WriteCommunicationServerInfo(uint8_t *message_buff, int message_len)
{
        int     fd_server;
        uint8_t ip_buff[4];

        char  *read_cfg_path  = "/lorawan/lorawan_hub/server_conf.json";
        char  *conf_obj       = "Communicate_conf";
        char  *ip_name        = "communicate_ip"; 
        char  *ip_port        = "communicate_port";

        uint8_t send_data_buff[300];
        uint8_t send_data[100];
        int     send_data_buff_len;
        int     send_data_len;

        //GWinfo
        uint8_t  gwinfo[8];
        static uint32_t message_id;
        
        //消息体属性
        MessageHead_t MessageHead;

        /* init */
        memset(ip_buff,0,4);

        uint16_t  tcp_ip_port;
        char     *server_ip = malloc(100);

        /* json文件初始化 */
        JSON_Value  *read_val;
        JSON_Object *read       = NULL;
        JSON_Object *read_conf  = NULL;
        
        /* 接收pc端数据 */
        ip_buff[0] = *(message_buff+17);
        ip_buff[1] = *(message_buff+18);
        ip_buff[2] = *(message_buff+19);
        ip_buff[3] = *(message_buff+20);
        tcp_ip_port    = (*(message_buff+23)) << 8;
        tcp_ip_port   |= (*(message_buff+24));
        sprintf(server_ip,"%d.%d.%d.%d",ip_buff[0],ip_buff[1],ip_buff[2],ip_buff[3]);
        DEBUG_CONF("server_ip:%s\n",server_ip);
        DEBUG_CONF("server_port:%d\n",tcp_ip_port);

        /* 读取网关的信息 */
        if ( -1 == ReadGwInfo(gwinfo))
                DEBUG_CONF("ReadGwInfo error!\n");

        if( access( read_cfg_path,R_OK) == 0)
        {
                DEBUG_CONF(" %s file is exit, parsing this file\n",read_cfg_path);

                read_val = json_parse_file_with_comments(read_cfg_path);
                read     = json_value_get_object(read_val);
                if(read == NULL)
                {
                        DEBUG_CONF("ERROR: %s is not a valid JSON file\n",read_cfg_path);
                        exit(EXIT_FAILURE);
                }
                read_conf = json_object_get_object(read,conf_obj);
                if(read_conf == NULL)
                {
                        DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj);
                }

                /* 设置通信服务器ip和端口 */
                json_object_dotset_string(read_conf,ip_name,server_ip);
                json_object_dotset_number(read_conf,ip_port,tcp_ip_port);  
        
        }


        /* 应答数据封装 */
                
        //消息版本 1byte
        send_data_buff[send_data_buff_len++] = 0x01;
        //消息类型 2byte
        send_data_buff[send_data_buff_len++] = 0x03;
        send_data_buff[send_data_buff_len++] = 0x17;
        //HUB唯一编号
        mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
        send_data_buff_len+=8;
        //消息序号
        message_id++;
        send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
        send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
        //消息体属性       
        MessageHead.MessageBodyProperty_t.MessageBody_Length =3;//消息体字节数 
        MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
        MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
        MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
        send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
        send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
        
        //消息体
        //tag
        send_data_buff[send_data_buff_len++] = 0x01;
        //type
        send_data_buff[send_data_buff_len++] = 0x02;
        //value
        send_data_buff[send_data_buff_len++] = 0x00;

        //计算校验码
        send_data_buff[send_data_buff_len] = Check_Xor(send_data_buff,send_data_buff_len); 
        //转义处理
        send_data_buff_len++;
        send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
        //添加后导码
        send_data[send_data_len] = 0x7e;
        send_data_len +=1;
        //进行数据发送
        tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
        if(tcp_send_len <= 0)
        {
                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"send error: %s\n",strerror(errno));
                ReconnectWith_Server(); 
        }    


        /* 
                每次写过服务器配置之后，重启lora_pkt_commuincatin进程
                杀死解析进程
        */
        kill(pid_communicate,SIGKILL);
        /*  回收子进程，避免僵尸进程 */
        signal(SIGCHLD,sig_child); 
        /*  重新启动解析进程 */
        if ( ( pid_communicate = fork()) < 0)
        {
                        DEBUG_CONF("fork error");
        }
        else if ( pid_communicate == 0)
        {
                /* 重启解析进程 */    
                if( execl("/lorawan/lorawan_hub/restart_lora_pkt_communicate.sh", "restart_lora_pkt_communicate.sh",NULL,NULL,(char*)0) == -1 )
                {
                        perror("execle error ");
                        exit(1);
                }
        
        }else{}
      
        /*  释放堆区内存 */
        free(server_ip);
        /* 释放json指针 */
        json_serialize_to_file(read_val,read_cfg_path); 
        json_value_free(read_val);

}


/*!
 * \brief  计算文件的md5值                              
 *
 * \param [IN]： 文件的路径和名称
 * \param [OUT]：输出的md5值
 * \param [OUT]：输出的文件大小
 * \Returns   ： 0 : ok 1 : fail
 */
int Compute_file_md5(const char *file_path, unsigned char *md5_str,unsigned int *filesize)
{
        int i;
	int fd;
	int ret;
        uint32_t file_sum = 0;
	unsigned char data[READ_DATA_SIZE];
	unsigned char md5_value[MD5_SIZE];
	MD5_CTX md5;

        
	if (md5_str == NULL) 
              perror("error! md5_str is null!\n");  
        
        fd = open(file_path, O_RDONLY);
	if (-1 == fd)
	{
		perror("open");
		return -1;
	}

	// init md5
	MD5Init(&md5);

	while (1)
	{
		ret = read(fd, data, READ_DATA_SIZE);
		if (-1 == ret)
		{
			perror("read");
			close(fd);
			return -1;
		}

		MD5Update(&md5, data, ret);

                /* 统计文件大小 */
                 file_sum += (uint32_t)ret; 

		if (0 == ret || ret < READ_DATA_SIZE)
		{
			break;
		}
	}

	close(fd);

	MD5Final(&md5, md5_value);

        mymemcpy(md5_str,md5_value, MD5_SIZE);

        *filesize = file_sum;

	return 0;


}



/*!
 * \brief  写远程通信服务器ip和端口信息                              
 *
 * \param [IN]：服务器端传输的数据地址
 * \param [IN]：服务器端传输的数据长度
 * \Returns   ：NULL.
 */
void ServerOTADeviceUpgrade(uint8_t *message_buff, int message_len)
{
        /*

           1： 获取固件id，进行文件下载
           2： 下载完成后检查文件大小，检查md5校验值
           3： 校验成功，更新固件版本，更新软链接
           4： 重新启动，完成升级     

        */

       /* 定义固件名称 */
       static char *firmware_name[] = { "lora_pkt_conf", "lora_pkt_fwd", "lora_pkt_server", "lora_pkt_communicate"};
       char *filepath = NULL; 
       char *ota_filepath = (char*)malloc(100);     

       /* 固件id */ 
       char     *firmware_id_str = (char*)malloc(50);
       uint8_t   firmware_id[4];

       uint8_t   version_len; 
       uint8_t   firmware_version[100];
       //uint8_t   *version_str = (uint8_t*)malloc(100); 
       //uint8_t   *version_p   = NULL; 
       uint8_t   name_tag; 
       pid_t     pid_ota;
       pid_t     pid_softlink;

        /* 
                OTA升级文件名称 
                文件内容：
                        第一行为固件ip地址
                        第二行为升级的固件的名称
        */
        char    *filename = "/lorawan/lorawan_hub/OTAUpgrade";
        int     fd;
        off_t   curr;
        ssize_t numWrite;

        /* 接收的md5值 */
        uint8_t receive_md5[16];
        /* 计算的md5值 */
        uint8_t calculate_md5[16];
        /* 接收固件大小 */
        uint32_t  recv_firm_size;
        /* 计算固件大小 */
        uint32_t  calc_firm_size;
        bool  OtaUpgradeIsOk = false;

        /* json文件 */
        JSON_Value  *read_val;
        JSON_Object *read_obj      = NULL;
        JSON_Object *read_conf     = NULL;
        char        *read_cfg_path = "/lorawan/lorawan_hub/server_conf.json";
        char        *firmware      = "Firmware_version";
        char        *conf_obj      = "Server_conf";
        char        *ip_name       = "server_ip"; 
        char        *server_ip     = NULL;

        /* 应答数据 */
        uint8_t send_data_buff[100];
        uint8_t send_data[100];
        int send_data_buff_len;
        int send_data_len;
        /* GWinfo */
        uint8_t  gwinfo[8];
        static uint32_t message_id;
        //消息体属性
        MessageHead_t MessageHead;


        /* init */
        version_len    = 0;
        recv_firm_size = 0;
        name_tag       = 0;
        memset ( firmware_id_str,      0,   4);  
        memset ( firmware_version, 0, 100);
        memset ( receive_md5,   0, 16);
        memset ( calculate_md5, 0, 16);
        memset ( &MessageHead,0,sizeof(MessageHead_t));

        if ( message_buff == NULL)
                DEBUG_CONF("error!message_buff is null!\n");

        /* 获取固件id */
        sprintf ( firmware_id_str, "%02x%02x%02x%02x",message_buff[17],message_buff[18],message_buff[19],message_buff[20]);
        DEBUG_CONF("firmware_id_str: %s\n",firmware_id_str);

        /* 拷贝固件的id */
        mymemcpy ( firmware_id, &message_buff[17], 4);
        
        /* 获取传输的md5值 */
        mymemcpy ( receive_md5, &message_buff[29], 16);
        
        /* 获取文件大小 */
        recv_firm_size  =    message_buff[26];
        recv_firm_size |=  ( message_buff[25] << 8);
        recv_firm_size |=  ( message_buff[24] << 16);
        recv_firm_size |=  ( message_buff[23] << 24);
      

        /* 
                获取固件的名称

                0x01:   lora_pkt_conf
                0x02：  lora_pkt_fwd         
                0x03：  lora_pkt_server
                0x04：  lora_pkt_communicate
        */
        name_tag = message_buff[47];
        DEBUG_CONF("name_tag: %d\n",name_tag);
        filepath = firmware_name[name_tag-1];
        DEBUG_CONF("filepath: %s\n",filepath);

        /* 获取版本号 */
        version_len = message_buff[50];
        DEBUG_CONF("version_len: %d\n",version_len);
        mymemcpy ( firmware_version, &message_buff[51], version_len);
        
        /* 转换成ascii字母 */
        if ( 0 != HexToChar ( firmware_version,version_len ))
                        DEBUG_CONF("Conversion Ascii failure!\n");

        DEBUG_CONF("firmware_version is %s\n",firmware_version);

        /* 将升级所需信息写入OTA升级文件中 */
        fd = open(filename, O_RDWR | O_CREAT,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP |
                        S_IROTH |S_IWOTH); /* rw-rw-rw */
            
        if (fd == -1)
                DEBUG_CONF("open OTA file error!\n");

        /* 将偏移量置为文件头： 覆盖 */
        curr = lseek(fd,0,SEEK_SET);
        if( curr == -1)
                DEBUG_CONF("lseek OTA file error!\n");
        
        /* 写入 固件id 信息 */
        numWrite = write(fd,firmware_id_str,strlen(firmware_id_str));
        if (numWrite == -1)
                DEBUG_CONF("write ip info error!\n");

        #if 0
                /* 将偏移量置为文件尾 */
                curr = lseek(fd,0,SEEK_END);
                if( curr == -1)
                        DEBUG_CONF("lseek OTA file error!\n");
        #endif
        
        /* 写入换行 */
        numWrite = write(fd ,"\n",1);
        if (numWrite == -1)
                DEBUG_CONF("write ip info error!\n");

        /* 写入文件名称信息 */
        numWrite = write(fd,filepath,strlen(filepath));
        if (numWrite == -1)
                DEBUG_CONF("write ip info error!\n");
        /* 写入换行 */
        numWrite = write(fd ,"\n",1);

        #if 1
        /* 启动升级脚本发送http请求 下载固件 */           
        if ( (pid_ota = fork()) < 0)
	{
		DEBUG_CONF("fork error");
	}
        else if ( pid_ota == 0)
	{
                /* 调用解析的子进程,依据网关上具体路径填写 */      
                if( execl("/lorawan/lorawan_hub/OTA_upgrade.sh", "OTA_upgrade.sh",NULL,NULL,(char*)0) == -1 )
                {
                        perror("execle error ");
                        exit(1);
                } 
	}
        #endif

        /* 等待约10s,等待下载完成 */
        sleep(10);

        sprintf ( ota_filepath,"/lorawan/lorawan_hub/ota/%s",filepath);
        DEBUG_CONF("ota_filepath : %s\n",ota_filepath);
        /* 进行固件校验 */
        if ( 0 != Compute_file_md5 ( ota_filepath, calculate_md5, &calc_firm_size))
                        DEBUG_CONF("compute md5 is error!\n");

        /* 释放堆区内存 */
        if ( ota_filepath != NULL)
                        free(ota_filepath);

        /* 大小和 md5校验均通过，则更新软链接 */
        if (( calc_firm_size==recv_firm_size) && ( 0==ArrayValueCmp(calculate_md5, receive_md5,16))){

                        OtaUpgradeIsOk = true;
        } 
        
        /*---------------------debug--------------------*/
        DEBUG_CONF("recv_firm_size: %d\n",recv_firm_size);     
        for ( int i=0; i < 16; i++)
                printf("%02x",receive_md5[i]);
        printf("\n");
        
        DEBUG_CONF("calc_firm_size: %d\n",calc_firm_size);
        for ( int i=0; i < 16; i++)
                printf("%02x",calculate_md5[i]);
        printf("\n");
        /*---------------------------------------------*/

        /* 读取网关的信息,消息封装的时候使用 */
        if ( -1 == ReadGwInfo(gwinfo))
                DEBUG_CONF("ReadGwInfo error!\n");
        
        if ( OtaUpgradeIsOk) 
        {
                /* 更新固件版本 */
                if ( access(read_cfg_path,R_OK) == 0)
                {
                        DEBUG_CONF(" %s file is exit, parsing this file\n",read_cfg_path);
                        read_val     = json_parse_file_with_comments(read_cfg_path);
                        read_obj     = json_value_get_object(read_val);
                        if (read_obj == NULL){
                                DEBUG_CONF("ERROR: %s is not a valid JSON file\n",read_cfg_path);
                                exit(EXIT_FAILURE);
                        }
                        read_conf = json_object_get_object(read_obj,conf_obj);
                        if(read_conf == NULL)
                                DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj);
                        
                
                        json_object_dotset_string(read_conf,firmware,firmware_version);

                        /* 获取配置服务器ip写入OTAUpgrade文件中 */
                        server_ip = json_object_get_string(read_conf,ip_name);
  
                }
                
                /* 写入配置服务器信息 */
                numWrite = write(fd,server_ip,strlen(server_ip));
                if (numWrite == -1)
                        DEBUG_CONF("write ip info error!\n");

                /* 写入换行 */
                numWrite = write(fd ,"\n",1);
                if (numWrite == -1)
                        DEBUG_CONF("write ip info error!\n");

                /* 关闭文件 */
                close(fd);


                /* 释放json指针 */
                json_serialize_to_file(read_val,read_cfg_path); 
                json_value_free(read_val);


                /* 发送OTA设备升级成功 */
                
                /* 消息版本 1byte */
                send_data_buff[send_data_buff_len++] = 0x01;
                /* 消息类型 2byte */
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x19;
                /* HUB唯一编号    */
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                /* 消息序号       */
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                /* 消息体属性      */       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =9;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                
                /* 消息体 */
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                /* 固件id */
                mymemcpy ( send_data_buff+send_data_buff_len, firmware_id, 4);
                send_data_buff_len +=4;

                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                /* 0x00 : successful  0x01: fail */
                send_data_buff[send_data_buff_len++] = 0x00;

                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0); 


                /* 更新软链接 */
                #if 1
                        if ( (pid_softlink = fork()) < 0)
                        {
                                DEBUG_CONF("fork error");
                        }
                        else if ( pid_softlink == 0)
                        {
                                /* 调用解析的子进程,依据网关上具体路径填写 */      
                                if( execl("/lorawan/lorawan_hub/UpdateSoftlink.sh", "UpdateSoftlink.sh",NULL,NULL,(char*)0) == -1 )
                                {
                                        perror("execle error ");
                                        exit(1);
                                } 
                        } 
                #endif

                /* 重启程序 */
                system("reboot");
        }
        else
        {
                
                DEBUG_CONF("OTA upgrade is fail!\n");

                /* 发送OTA设备升级失败 */
        
                /* 消息版本 1byte */
                send_data_buff[send_data_buff_len++] = 0x01;
                /* 消息类型 2byte */
                send_data_buff[send_data_buff_len++] = 0x03;
                send_data_buff[send_data_buff_len++] = 0x19;
                /* HUB唯一编号    */
                mymemcpy(send_data_buff+send_data_buff_len,gwinfo,8);
                send_data_buff_len+=8;
                /* 消息序号       */
                message_id++;
                send_data_buff[send_data_buff_len++] = (uint8_t)(message_id >> 8);
                send_data_buff[send_data_buff_len++] = (uint8_t) message_id; 
                /* 消息体属性      */       
                MessageHead.MessageBodyProperty_t.MessageBody_Length =9;//消息体字节数 
                MessageHead.MessageBodyProperty_t.MessageEncryption = 0;//未加密
                MessageHead.MessageBodyProperty_t.SubpackageBit = 0; //未分包
                MessageHead.MessageBodyProperty_t.RFU = 0;//保留  
                send_data_buff[send_data_buff_len++] = (uint8_t)(MessageHead.Value>>8); 
                send_data_buff[send_data_buff_len++] = (uint8_t) MessageHead.Value;
                
                /* 消息体 */
                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                /* 固件id */
                mymemcpy ( send_data_buff+send_data_buff_len, firmware_id, 4);
                send_data_buff_len +=4;

                //tag
                send_data_buff[send_data_buff_len++] = 0x01;
                //type
                send_data_buff[send_data_buff_len++] = 0x02;
                /* 0x00 : successful  0x01: fail */
                send_data_buff[send_data_buff_len++] = 0x01;

                //计算校验码
                send_data_buff[send_data_buff_len++] = Check_Xor(send_data_buff,send_data_buff_len); 
                //转义处理
                send_data_len = Transfer_Mean(send_data,send_data_buff,send_data_buff_len);
                //添加后导码
                send_data[send_data_len] = 0x7e;
                send_data_len +=1;
                //进行数据发送
                tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0); 
        
        
        }

}