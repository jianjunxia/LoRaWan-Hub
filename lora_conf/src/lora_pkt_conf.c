/*
    ____               _    _      _         _
  / ____|             | |  | |    (_)       | |
 | (___    ___  _ __  | |_ | |__   _  _ __  | | __
  \___ \  / _ \|  _ \ | __||  _ \ | ||  _ \ | |/ /
  ____) ||  __/| | | || |_ | | | || || | | ||   <
 |_____/  \___||_| |_| \__||_| |_||_||_| |_||_|\_\


Description:
            该程序用于PC端上位机配置网关
            该程序涉及一个父进程，两个子进程


Transport Protocols: UDP、LORAWAN 网关与 PC 端上位机传输协议
Lierda |  Senthink
autor:  jianjun_xia              
data :  2018.9.6

*/

/* ----------------------------------------------- DEPENDANCIES --------------------------------------------------------- */
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
#include <sqlite3.h>
#include <sys/time.h>
#include "parson.h"         /* 解析JSON文件依赖的头文件 */
#include "common.h"
#include "lora_pkt_conf.h"
#include "region.h"         /* 区域频段 */

/* 存储所有的deveui信息 */
static  uint8_t   udp_all_deveui_buff[8000];
/* 记录调用多少次回调函数 */
static  uint16_t  udp_callback_count;

/* 应答广播命令的内容 */
static uint8_t ack_broadcast_buff[ACK_BROCAST_LEN] = 
{
    /* Header:固定字节 */
    0xfa,0x02,0x00,0x08,
    /* 网关EUI:后续从文件中读出，并填充 */
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* MIC:    后续计算后填充 */
    0x00
};

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//+                                   读写命令码错误密说明                                     +
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//+                         返回值                            定义                           +  
//+                         0x01                            操作成功                         +  
//+                         0x02                           MIC校验错误                       +  
//+                         0x03                            命令错误                         +   
//+                         0x04                            其他错误                         +   
//+                         待补充...                                                        +  
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

/* 返回读写命令码列表 */
uint8_t  return_success    [RETURN_CMD_LEN] = {0x01};
uint8_t  return_mic_error  [RETURN_CMD_LEN] = {0x02};
uint8_t  return_cmd_error  [RETURN_CMD_LEN] = {0x03};
uint8_t  return_other_error[RETURN_CMD_LEN] = {0x04};

/* sqlite3数据库表 */
sqlite3 *db;
char    *zErrMsg   = NULL;
char    *sql       = NULL;

/* udp */
struct sockaddr_in udp_sourceaddr;
struct sockaddr_in udp_localaddr;
struct sockaddr_in udp_sendaddr;
int Reusraddr    = 1;
int udp_sockfd;  
int udp_ret;

/* 信号中断函数 */
/* signal handling variables */
struct sigaction sigact; /* SIGQUIT&SIGINT&SIGTERM signal handling */
int exit_sig = 0; /* 1 -> application terminates cleanly (shut down hardware, close open files, etc) */
int quit_sig = 0; /* 1 -> application terminates without shutting down the hardware */
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
}

/* 解析数据进程id */
pid_t pid_parse;

/* 数据上报服务器进程id */
pid_t pid_server;

/* 线程id:   处理服务器下发SX1301配置数据 */
pthread_t   thread_conf_sx1301_task;

/* 线程id:   上报解析后的数据 */
pthread_t   thread_decode_data_task;

/* 处理lora_pkt_fwd.c上报的节点的deveui 和 解析后的数据信息 */
void *ThreadHandleNodeDecodeData(void);

/* 线程函数：处理服务器下发sx1301的配置信息 */
void *ThreadHandleSx1301Task(void);

/* 回收子进程描述符 */
static void sig_child(int signo);

/*----------------------------------------------------main函数--------------------------------------------------------*/
int 
main(int argc,char *argv[])
{
    /* udp协议使用的一些变量 */
    int rc;
    int err;
    int  repeat = -1; /* by default, repeat until stopped */
    uint16_t cycle_count = 0;
    char *sql = NULL;
    /* 数据库名称 */
    char *database = "/lorawan/lorawan_hub/hub.db";

    /* 打开一个已有的数据库，如果没有则创建数据库 */
    Sqlite3_open(database,&db);
    
    /* GWINFO 创建 */
    SqliteGwInfoTable(db, zErrMsg);
    
    /* 初始化上下行频率表 */
    Channel_FrequencyTable_Init(db,zErrMsg);    

    /* 检测系统中是否有未被回收的子进程，若有则回收 */
    signal(SIGCHLD,sig_child); 
    
    /* 创建与后台服务器通信的进程 */
    if ( (pid_server = fork()) < 0)
    {
            DEBUG_CONF("Create server process error!\n");
    
    }else if ( pid_server == 0)
    {
             /* 调用与服务器通信进程 */      
            if ( execl("/lorawan/lorawan_hub/lora_pkt_server", "lora_pkt_server",NULL,NULL,(char*)0) == -1 ){
                    perror("execle error ");
                    exit(1);
            } 
    
    }
    else{}

    /* 创建解析lorawan协议进程 */
    if ( ( pid_parse = fork()) < 0)
    {
            DEBUG_CONF("Create parse process error!\n");
    
    }else if(pid_parse == 0)
    {
            /* 调用lorawan数据解析进程 */
            if( execl("/lorawan/lorawan_hub/restart_lora_pkt_fwd.sh", "restart_lora_pkt_fwd.sh", NULL,NULL,(char*)0) == -1 ){
                    perror("execle error ");
                    exit(1);
            }
    }
    else /* lora_pkt_conf 进程 */
    {  

            /* 线程函数： 监听服务器配置sx1301的数据的线程 */     
            err = pthread_create(&thread_conf_sx1301_task,NULL,ThreadHandleSx1301Task,NULL);
            if(err != 0){
                    DEBUG_CONF("can't create handle server sx1301 conf thread ");
            }else{
                    DEBUG_CONF("create  handle server sx1301 conf thread successfully\n");
            }

            /* 线程函数：处理lora_pkt_fwd.c上报的解析后的join macpayload 数据 */
            err = pthread_create(&thread_decode_data_task,NULL,ThreadHandleNodeDecodeData,NULL);       
            if ( err != 0 ){
                    DEBUG_CONF(" can't create handle node decode data thread");
            }else{
                    DEBUG_CONF("create  handle node decode data thread successfully\n");
            }
            
            /* PC端配置进程 */
            /* init */
            memset(udp_all_deveui_buff,0,8000);
            udp_callback_count = 0;
            
            /* udp <-------------------------> pc协议 */
            
            /* create udp socket */
            udp_sockfd = socket(AF_INET,SOCK_DGRAM,0);
            if(-1 == udp_sockfd)
            {
                    DEBUG_CONF("create udp socket fail!\n");
                    exit(1);
            }

            /* 设置socket属性 */
            udp_localaddr.sin_family        = AF_INET;//ipv4
            udp_localaddr.sin_addr.s_addr   = htonl(INADDR_ANY);
            /* 6789为监听上位机端口 */
            udp_localaddr.sin_port          = htons(UDP_PORT);

            /* 服务器地址和ip */
            udp_sendaddr.sin_family         = AF_INET;
            /* 上位机端口:8080 */
            udp_sendaddr.sin_port           = htons(UDP_SEND_PORT);

            /* bind at the local address */
            udp_ret = bind(udp_sockfd,(struct sockaddr*)&udp_localaddr,sizeof(struct sockaddr_in));
            if (-1 == udp_ret)
            {
                DEBUG_CONF("bind local listening addr fail，errno : %d \n", errno);
                close(udp_sockfd);
                exit(1);
            }
            
            cycle_count = 0;
	    while ((repeat == -1) || (cycle_count < repeat))
            {
                    ++cycle_count;
            
                    /*上位机配置函数*/
                    PC_Configuration();
            
                    /* 回收子进程 */
                    signal(SIGCHLD,sig_child); 

                    if ((quit_sig == 1) || (exit_sig == 1)) 
                    {           
                                /* 关闭全局变量socket */        
                                close(udp_sockfd);
                                break;
                    }                          
            }

            /* 等待sx1301配置线程执行完成 */
            pthread_join(thread_conf_sx1301_task,  NULL);
            /* 等待sx1301配置线程执行完成 */
            pthread_join(thread_decode_data_task,  NULL);
    
            exit(EXIT_SUCCESS);    
    }

}

/*------------------------------------------PC端上位机配置使用的函数实现---------------------------------------------*/

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

/*  test 函数:  回调数据库中函数。查看创建的表  */
static int test_callback(void *data, int argc, char **argv, char **azColName)
{
   int i;

   for(i=0; i<argc; i++)
        DEBUG_CONF("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   
   DEBUG_CONF("\n");
   return 0;

}

/*  
    回调函数
    读取 GWINFO 中所有节点deveui 

*/
static int ReadAllDeveuiCallback(void *NotUsed,int argc,char**argv,char**azColName)
{
    int i;
    int tmp = 0;

    /* 改变存储地址 */
    tmp = udp_callback_count * 8;
    
    /* argv[2]的值依据实际表格对应的数据 */
    if ( argv[2] != NULL ) {

            String_To_ChArray(udp_all_deveui_buff+tmp,argv[2],8);
    
    }else {
            DEBUG_CONF("argv[2] is NULL!\n");
    }

    udp_callback_count++;

    return 0;
}

/*  
    brief:      PC端配置信息解析
    parameter:  null
    return:     null 
*/
void PC_Configuration(void)
{
    uint8_t rec_cmd_buff[RECE_CMD_MAX_LEN];  
    int recv_len;
    int index_cmd_len = 0;
    int mic_loc_flag;
    int udp_addr_len;
 
    char *udp_send_ip = (char*)malloc(50);
    
    mic_loc_flag = RECE_CMD_MAX_LEN;
    udp_addr_len = sizeof(struct sockaddr_in);

    /* init */
    memset(rec_cmd_buff,0,RECE_CMD_MAX_LEN);  
    
    recv_len = recvfrom(  udp_sockfd,
                          rec_cmd_buff,
                          RECE_CMD_MAX_LEN,
                          0,
                          (struct sockaddr*)&udp_sourceaddr,
                            &udp_addr_len
                        );
    /*
        step1: 解析PC端IP和PORT  无实际意义，观测
        step2: 解析PC端发出的命令，并进行相应的回复。
    */
    if(recv_len != -1)
    {
        /* 解析pc上位机的IP和端口 */
        DEBUG_CONF("Receive PC ip is: \n");
        DEBUG_CONF(" %u:%u:%u:%u port: %u \n\n",
                                             *((uint8_t*)&udp_sourceaddr.sin_addr  ),
                                             *((uint8_t*)&udp_sourceaddr.sin_addr+1),
                                             *((uint8_t*)&udp_sourceaddr.sin_addr+2),
                                             *((uint8_t*)&udp_sourceaddr.sin_addr+3),
                                              ntohs(udp_sourceaddr.sin_port)
                                             );

        sprintf(udp_send_ip,"%d.%d.%d.%d",
                                        *((uint8_t*)&udp_sourceaddr.sin_addr  ),
                                        *((uint8_t*)&udp_sourceaddr.sin_addr+1),
                                        *((uint8_t*)&udp_sourceaddr.sin_addr+2),
                                        *((uint8_t*)&udp_sourceaddr.sin_addr+3)   
                                        );

        DEBUG_CONF("RECV IP value[%d]:%s\n",strlen(udp_send_ip),udp_send_ip);                                
        
        /* 将解析出的IP地址赋值给需发送的IP地址端 */  
        recv_len = inet_aton(udp_send_ip, &udp_sendaddr.sin_addr);
        if(0 == recv_len)
        {
            DEBUG_CONF("send ip error!\n");
            close(udp_sockfd);
        }            
        
        /* 命令解析 */
        
        /* 解析前导码 */
        if(rec_cmd_buff[index_cmd_len++] == 0XFA )
        { 
                /*
                    mic校验:先找到mic的位置
                    从一包命令数据的最后开始查找，遇到第一个非0数值即为mic值
                */
                while(mic_loc_flag--)
                {
                    if(rec_cmd_buff[mic_loc_flag] != 0x00)
                            break; /* 找到后跳出循环 */   
                }

                /* 调用校验函数 进行mic校验 */
                if ( 0xff == checkout(&rec_cmd_buff[mic_loc_flag],
                                                     rec_cmd_buff,
                                                     mic_loc_flag                       
                                     )    
                    )
                {   

                        /* 进行命令判断 */
                        switch(rec_cmd_buff[index_cmd_len++])
                        {
                            case  BROADCAST_CMD                 : { ack_broadcast();break; }
                            case  READ_ALL_NODE_CMD             : { ack_read_all_node();break; }
                            case  READ_NODE_CMD                 : { ack_read_node(rec_cmd_buff);break; } 
                            case  WRITE_NODE_CMD                : { ack_write_node(rec_cmd_buff);break; }
                            case  READ_SX1301_CMD               : { ack_read_sx1301();break; } 
                            case  WRITE_SX1301_CMD              : { ack_write_sx1301(rec_cmd_buff);break; }
                            case  READ_SERVER_CMD               : { ack_read_server();break; }
                            case  WRITE_SERVER_CMD              : { ack_write_server(rec_cmd_buff);break; }
                            case  DELETE_NODE_CMD               : { ack_delete_node(rec_cmd_buff);break; }
                            case  DELETE_ALL_NODE_CMD           : { ack_delete_all_node();break; }           
                            case  READ_ALL_RX2_FREQ_CMD         : { read_all_rx2_freq();break; }
                            case  CONF_ALL_RX2_FREQ_CMD         : { conf_all_rx2_freq(rec_cmd_buff);break; }
                            case  REGION_FREQ_INFO_CMD          : { Ack_Region_Freq_Info(rec_cmd_buff);break; }
                            case  UPLINK_FREQ_LIST_CMD          : { Ack_UpLink_Freq_Info(rec_cmd_buff);break; }
                            case  DOWNLINK_FREQ_LIST_CMD        : { Ack_DownLink_Freq_Info(rec_cmd_buff);break; }
                            case  Rx1DRoffset_CMD               : { Ack_Rx1DRoffset_Info(rec_cmd_buff);break; }
                            case  FETCH_REGION_INFO_CMD         : { Ack_Fetch_Region_Info();break; }
                            case  FETCH_UPLINK_FREQ_CMD         : { Ack_Fetch_Uplink_Info();break; }
                            case  FETCH_DOWN_FREQ_CMD           : { Ack_Fetch_Downlink_Info();break; }
                            case  FETCH_RX1DROFFSET_CMD         : { Ack_Fetch_Rx1DRoffset_Info();break; }
                            case  ABP_CONF_NODE_INFO_CMD        : { ABP_Configure_Node_Info(rec_cmd_buff);break; }
                            case  ABP_READ_NODE_INFO_CMD        : { ABP_Fetch_Node_Info(rec_cmd_buff); break; }
                            case  READ_COMMUNICATE_SERVER_CMD   : { AckReadCommunicateServer();break; }
                            case  WRITE_COMMUNICATE_SERVER_CMD  : { AckWriteCommunicateServer(rec_cmd_buff);break; }                                            
                            default:{
                                        /* 没有对应的命令 */
                                        recv_len = sendto( udp_sockfd,
                                                        return_cmd_error,
                                                        RETURN_CMD_LEN,
                                                        0,
                                                        (struct sockaddr*)&udp_sendaddr,
                                                        udp_addr_len
                                                        ); 

                                        if(recv_len < 0)
                                        {
                                            DEBUG_CONF("\nsend return cmd error!\n");
                                            close(udp_sockfd);
                                            exit(errno);
                                        
                                        }      
                            }break;


                        }
                        /* clear */
                        memset(rec_cmd_buff,0,RECE_CMD_MAX_LEN); 
                        index_cmd_len = 0;        
                }
                else
                {
                        /* 发生mic校验错误信息码 */
                        recv_len = sendto( udp_sockfd,
                                            return_mic_error,
                                            RETURN_CMD_LEN,
                                            0,
                                            (struct sockaddr*)&udp_sendaddr,
                                            sizeof(struct sockaddr_in)
                                        );
                        
                        if ( recv_len < 0)
                        {
                                DEBUG_CONF("\nsend return mic error!\n");
                                close(udp_sockfd);
                                exit(errno);  
                        }  

                } 
        }
        /* 发送解析前导码错误 */
        else{}

        /* clear */
        memset(rec_cmd_buff,0,RECE_CMD_MAX_LEN); 
        index_cmd_len = 0;
        
    }
    else{}

}

/*  
    brief:      应答广播命令的函数
    parameter:  null
    return:     null 
*/
void ack_broadcast(void)
{
    /* gwinfo */
    int fd_gw;
    int size_gw;
    uint8_t *gwinfo_string = (uint8_t*)malloc(16);
    uint8_t  gwinfo[8];
    memset(gwinfo,0,8);

    /* 读入网关信息的文件 */
    fd_gw = open("/lorawan/lorawan_hub/gwinfo",O_RDWR);
    if(-1 == fd_gw)
    {
            DEBUG_CONF("sorry,There is no gwinfo file in this directory,Please check it!\n");            
            close(fd_gw);
            exit(1);
    }
    size_gw = read(fd_gw,gwinfo_string,16);
    /* ascii to hex */
    if ( gwinfo_string != NULL) 
            String_To_ChArray(gwinfo,gwinfo_string,8);
    close(fd_gw);

    /* 存储到应答广播命令的数组中 */
    mymemcpy(ack_broadcast_buff+4,gwinfo,8);
    
    /* mic */
    ack_broadcast_buff[ACK_BROCAST_LEN-1] = checksum
                                            (
                                                ack_broadcast_buff,
                                                ACK_BROCAST_LEN -1                                                
                                            );
    /* 回复命令 */
    fd_gw = sendto(udp_sockfd,ack_broadcast_buff,ACK_BROCAST_LEN,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
    if(fd_gw < 0)
    {
        DEBUG_CONF("\nsend ack broadcast cmd error!\n");
        close(udp_sockfd);
        exit(errno);
    }  

    /* 释放堆区资源 */
    if ( gwinfo_string != NULL)
            free(gwinfo_string); 
}

/*
    brief         应答读取网关上所有节点的信息
    param [IN]：  NULL
    Returns   :   NULL

*/
void  ack_read_all_node(void)
{
    char *sql = NULL;
    int rc;
    int loop_a;
    int loop_b;
    int loop_c;

    int fd_read_all;

    uint8_t  *pointer_buff = NULL;
    uint8_t  *pointer_all_deveui = NULL;
    uint16_t ack_read_len = 0;
    
    /* 分包位 */
    bool SubpackageIsOk = false;
    
    /* 最后一包数据包含多少个deveui */
    uint16_t package_last_number = 0;

    /* 分包总数 */
    uint16_t package_total;
    
    /* 包序号  */
    uint16_t package_number;
    
    /* 分包起始地址变量 */
    uint16_t package_begin_address_temp = 0;
    int data_len = 0;

    /* 一包数据最大字节数是71 */
    uint8_t ack_read_buff[100];
    memset(ack_read_buff,0,100);
    
    sqlite3_exec(db,"BEGIN TRANSACTION;", NULL,NULL,&zErrMsg);
    /* sql语句 */
    sql ="SELECT* FROM GWINFO";   

    /* execute */
    rc = sqlite3_exec(db, sql, ReadAllDeveuiCallback, 0, &zErrMsg);
    
    if( rc != SQLITE_OK ){
            
            DEBUG_CONF("SQL read all node deveui error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
    }
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    
    /* 判断包总数 */
    if( udp_callback_count <= 8 )
    {
        SubpackageIsOk = false;
    }
    else
    {
        SubpackageIsOk = true;
    } 
    DEBUG_CONF("udp_callback_count: %d\n",udp_callback_count);
    DEBUG_CONF("ack_read_buff[4]:%d\n",ack_read_buff[4]);


    if ( SubpackageIsOk)
    {
                if ( udp_callback_count % 8 == 0)
                {
                        /* 包总数 */
                        package_total  = (udp_callback_count / 8);
                        /* 包的最大序号，包序号从1开始 */
                        package_number = (udp_callback_count / 8) -1;
                        /* 最后一包数据 */    
                        package_last_number = 8;
                }
                else
                {
                        package_total  = (udp_callback_count / 8) + 1;
                        package_number = (udp_callback_count / 8);
                        /* 判断最后一包数据 */   
                        package_last_number = (udp_callback_count - (package_total-1) * 8) % 8;       
                }

                /* 消息封装 */
                for ( int Package_Id = 0; Package_Id <= package_number; Package_Id++)
                {
                        /* preamble */
                        ack_read_buff[data_len++] = 0xfa;
                        /* cmd  */
                        ack_read_buff[data_len++] = 0x04;
                        /* 如果是最后一包数据，需根据最后一包数据大小计算消息体长度 */  
                        if(Package_Id == package_number)
                        {
                                ack_read_len = 2 + (package_last_number * 8);
                        }
                        else
                        {             
                                ack_read_len = 2 + (8 * 8);           
                        }

                        /* len 大端存储 */
                        ack_read_buff[data_len++] = (uint8_t)ack_read_len >> 8;
                        ack_read_buff[data_len++] = (uint8_t)ack_read_len;
                        /* 包的总数 */
                        ack_read_buff[data_len++] = package_total;
                        /* 包的序号 */
                        ack_read_buff[data_len++] = Package_Id;
                        
                        /* 
                                deveui data
                                需判断是否是最后一包数据
                        */
                        if(Package_Id == package_number)
                        {
                                mymemcpy(ack_read_buff+data_len,udp_all_deveui_buff+package_begin_address_temp,(package_last_number * 8));            
                                data_len += (package_last_number * 8);
                        }
                        else
                        {
                                mymemcpy(ack_read_buff+data_len,udp_all_deveui_buff+package_begin_address_temp, (8 * 8));         
                                /* 一包数据最多填充8个deveui,一个deveui包含8个字节 */  
                                package_begin_address_temp+=64;
                                data_len +=64; 
                        }

                        /* mic */
                        ack_read_buff[data_len] = checksum(ack_read_buff,data_len);
                        data_len++;
                        
                        /* 数据发送 */
                        fd_read_all = sendto(udp_sockfd,ack_read_buff,data_len,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
                        if(fd_read_all < 0)
                        {
                                DEBUG_CONF(" %d\n",errno);
                                fprintf(stderr,"send ack read all node cmd error!: %s\n",strerror(errno));
                                close(udp_sockfd);         
                        }
                        
                        /* clear */
                        data_len = 0;
                        memset(ack_read_buff,0,100);
                } 

            /* 清除deveui缓存 */
            udp_callback_count = 0;
            memset(udp_all_deveui_buff,0,8000);
    }
    else/* 不需要分包处理 */
    {
                /* preamble */
                ack_read_buff[data_len++] = 0XFA;
                /* cmd */
                ack_read_buff[data_len++] = 0X04;
                ack_read_len = (udp_callback_count * 8) + 2;
                /* len 大端存储 */ 
                ack_read_buff[data_len++] = (uint8_t)ack_read_len >> 8;
                ack_read_buff[data_len++] = (uint8_t)ack_read_len;
                /* 包的总数 */
                ack_read_buff[data_len++] = 0x01;
                /* 包的序号 */
                ack_read_buff[data_len++] = 0x00;

                /* deveui data */
                mymemcpy( ack_read_buff+data_len,udp_all_deveui_buff,(udp_callback_count*8));
                data_len += (udp_callback_count*8);   
                /* mic */
                ack_read_buff[data_len] = checksum(ack_read_buff,data_len);
                data_len++;

                /* 数据发送 */
                fd_read_all = sendto(udp_sockfd,ack_read_buff,data_len,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
                if(fd_read_all < 0)
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send ack read all node cmd error!: %s\n",strerror(errno));
                        close(udp_sockfd);           
                }

                /* clear */
                data_len = 0;
                memset(ack_read_buff,0,100);
                /* 清除deveui缓存 */
                udp_callback_count = 0;
                memset(udp_all_deveui_buff,0,8000);

    }    
   
}

/*
    brief        应答读单个节点信息
    param [IN]：  NULL
    Returns   :   NULL

*/
void ack_read_node(uint8_t *buff)
{
    sqlite3_stmt *stmt_read_node = NULL;
    int rc;
    int fd;
    char *p_buff = NULL;
    char *deveui_buff_string = (char*)malloc(2*8); 
    char *sql_deveui_buff    = (char*)malloc(100);
    const char *appkey_buff_string = NULL;
      
    uint8_t deveui_buff[8];
    memset(deveui_buff,0,8);
    uint8_t appkey_buff[16];
    memset(appkey_buff,0,16);
    uint8_t send_buff[21];
    memset(send_buff,0,21);

    /* 拷贝到deveui缓存数组中 */
    mymemcpy(deveui_buff,buff+4,8);
    p_buff = ChArray_To_String(deveui_buff_string,deveui_buff,8);
    DEBUG_CONF("ack read node:%s\n",p_buff);

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    
    /* sql */
    sprintf(sql_deveui_buff,"SELECT* FROM GWINFO WHERE DEVEui ='%s';",p_buff);     
    rc = sqlite3_prepare_v2(db,sql_deveui_buff,strlen(sql_deveui_buff),&stmt_read_node,NULL);
    if (SQLITE_OK !=rc || NULL == stmt_read_node)
    {
          DEBUG_CONF("\n\n ack_read_node prepare error!\n\n");
          sqlite3_close(db); 
    } 
    /* execute */
    rc = sqlite3_step(stmt_read_node);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);

    if(SQLITE_ROW == rc)
    {
            appkey_buff_string = sqlite3_column_text(stmt_read_node,1);         
            if ( appkey_buff_string != NULL )  
                    String_To_ChArray(appkey_buff,appkey_buff_string,16);
            
            DEBUG_CONF("appkey_buff_string: %s\n",appkey_buff_string);
    }
    else
    {
        /* 不处理，不让节点入网 */
        DEBUG_CONF("Sorry,This gwinfo table no serach this deveui.\n");
    }
    
    /* 消息封装 */
    
    /* appkey */
    mymemcpy(send_buff+4,appkey_buff,16);
    /* preamble */
    send_buff[0] = 0xfa;
    /* cmd */
    send_buff[1] = 0x06;
    /* len */
    send_buff[2] = 0x00;
    send_buff[3] = 0x10;
    /* mic */
    send_buff[20] = checksum(send_buff,20);
    fd = sendto(udp_sockfd,send_buff,21,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
    if ( fd  < 0)
    {
        DEBUG_CONF(" %d\n",errno);
        fprintf(stderr,"send ack single node cmd error!: %s\n",strerror(errno));
       
    }

    if ( p_buff != NULL)
            free(p_buff);

    if ( sql_deveui_buff != NULL)
            free(sql_deveui_buff);
    
    if ( stmt_read_node != NULL) 
            sqlite3_finalize(stmt_read_node);

}

/**
 * brief：应答写单个节点信息命令
 * 
 * parameter：需要写入的节点信息
 * 
 * return：  NULL
 * 
 */
void ack_write_node ( uint8_t *buff )
{
    
    sqlite3_stmt *stmt_node = NULL;
    sqlite3_stmt *stmt_data = NULL;    
    int rc;
    int id_buff = 0;
    int send_len;
    int fd_write;

    /* 将表中的ID按降序排布，即寻找表中ID最大值 */
    char *sql_search ="SELECT* FROM GWINFO ORDER BY ID DESC LIMIT 1";

    char *p_deveui = NULL;
    char *p_appkey = NULL;

    char *sql_deveui         = (char*)malloc(100);
    char *sql_appkey         = (char*)malloc(240);
    char *deveui_str         = (char*)malloc(50); 
    char *appkey_str         = (char*)malloc(50);

    uint8_t send_buff[6];
    uint8_t deveui_buff[8];
    uint8_t appkey_buff[16];

    memset(send_buff,       0,6);
    memset(deveui_buff,     0,8);
    memset(appkey_buff,     0,16);

    /* 拷贝deveui, appkey */
    mymemcpy(deveui_buff, buff+4,   8);
    mymemcpy(appkey_buff, buff+12,  16);

    send_len = sizeof(send_buff);

    p_deveui = ChArray_To_String(deveui_str,    deveui_buff,    8);
    p_appkey = ChArray_To_String(appkey_str,    appkey_buff,    16);
    
    DEBUG_CONF("ack write node:%s\n",p_deveui);
    DEBUG_CONF("ack write node:%s\n",p_appkey);

    /* sql */ 
    sprintf(sql_deveui,"SELECT* FROM GWINFO WHERE DEVEui ='%s';",p_deveui);
    DEBUG_CONF("sql_deveui[%d]%s\n",strlen(sql_deveui),sql_deveui); 

    rc = sqlite3_prepare_v2(db,sql_deveui,strlen(sql_deveui),&stmt_node,NULL);
    if ( SQLITE_OK !=rc || NULL == stmt_node ){
            DEBUG_CONF("\n\n ack write node prepare error!\n\n");
            sqlite3_close(db); 
    }

    /* 执行查询命令 */
    rc = sqlite3_step(stmt_node);

    /*
        判断数据库表中是否有这个节点，如果有该节点则将原数据覆盖
        如果无该节点，则在表中添加为新的节点 
    */    
    if ( SQLITE_ROW == rc ) 
    {

            sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
            
            /* sql */
            sprintf(sql_appkey,"UPDATE GWINFO SET APPKey='%s' WHERE DEVEui='%s';", p_appkey,p_deveui);
            DEBUG_CONF("sql_appkey[%d]%s\n",strlen(sql_appkey),sql_appkey);
            
            /* 更新操作 */
            rc = sqlite3_exec(db,sql_appkey,0,0,&zErrMsg);
                  
            if ( rc != SQLITE_OK ){
                    DEBUG_CONF("update appkey node error!: %s\n",zErrMsg);
                    sqlite3_free(zErrMsg);
            } else{

                    DEBUG_CONF("update appkey node successfully\n");
            }
            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);

    }
    else
    {   
            sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
            
            /* parper */
            rc = sqlite3_prepare_v2(db,sql_search,strlen(sql_search),&stmt_data,NULL);

            if ( SQLITE_OK !=rc || NULL == stmt_data ){

                    DEBUG_CONF("\n parse Prepare error! \n");
                    sqlite3_close(db);
            }

            /* 找到ID最大值并取出 */
            while(SQLITE_ROW == sqlite3_step(stmt_data))
            {        
                    id_buff = sqlite3_column_int (stmt_data, 0); 
                    DEBUG_CONF("MAX ID: %d\n",id_buff);
            }

            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);

            /* 将ID最大值+1并在表中插入 */
            id_buff = id_buff+1; 

            /* 插入一条新数据 */
            sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
            /* sql */
            sprintf(sql_appkey,"INSERT INTO GWINFO (ID,APPKey,DEVEui) VALUES (%d,'%s','%s');",id_buff,p_appkey,p_deveui);
            rc = sqlite3_exec(db, sql_appkey, 0, 0, &zErrMsg);

            if ( rc != SQLITE_OK ){
                    
                    DEBUG_CONF("ack insert new appkey node error!: %s\n", zErrMsg);
                    sqlite3_free(zErrMsg);

            } else{

                    DEBUG_CONF("ack insert new appkey node successfully\n");
            }

            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    
    }

    /* preamble */
    send_buff[0] = 0xfa;
    /* cmd */
    send_buff[1] = 0x08;
    /* len */
    send_buff[2] = 0x00;
    send_buff[3] = 0x01;
    /* send success */
    send_buff[4] = 0x01;
    /* mic */
    send_buff[5] = checksum(send_buff,5);

    /* 发送写应答命令 */
    fd_write = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
    if ( fd_write  < 0 ){

            DEBUG_CONF(" %d\n",errno);
            fprintf(stderr,"send send ack write cmd error: %s\n",strerror(errno));
    }

    /* 释放堆区资源 */
    if ( stmt_node != NULL)
            sqlite3_finalize(stmt_node);
    
    if ( stmt_data != NULL)
            sqlite3_finalize(stmt_data);
            
    if ( sql_appkey != NULL) 
                    free(sql_appkey);
    
    if ( sql_deveui != NULL) 
                    free(sql_deveui);
 
    if ( p_deveui != NULL) 
                    free(p_deveui);

    if ( p_appkey != NULL) 
                    free(p_appkey);

}

/*
   
   brief：    应答读sx1301
   parameter：需要写入的节点信息
   return：  NULL 

*/
void ack_read_sx1301(void)
{
    int i;
    int fd_sx1301;

    /* 读取本地配置文件中的信息 */
    char *read_cfg_path =  "/lorawan/lorawan_hub/global_conf.json";
    char conf_obj[] = "SX1301_conf";
    char param_name[32];

    JSON_Value  *read_val;
    JSON_Object *read      = NULL;
    JSON_Object *read_conf = NULL;

    uint8_t send_sx1301_buff[29];

    /*
        定义一个临时结构体
        用于存储 radio_a radio_b 的频率
    */
    struct Freq_Buff
    {
        uint32_t center_freq;
        uint32_t freq_min;
        uint32_t freq_max;
    };
    
    struct Freq_Buff radio_freq[2];
    
    /* init */
    memset(send_sx1301_buff,0,29);
    memset(&radio_freq,0,sizeof(radio_freq));
    memset(param_name,0,32);

    if (access(read_cfg_path, R_OK) == 0) 
    { 
                DEBUG_CONF("INFO: found global configuration file %s, parsing it\n", read_cfg_path);

                read_val = json_parse_file_with_comments(read_cfg_path);
                read = json_value_get_object(read_val);
                if ( read == NULL) 
                {
                        DEBUG_CONF("ERROR: %s id not a valid JSON file\n", read_cfg_path);
                        exit(EXIT_FAILURE);
                }

                read_conf = json_object_get_object(read, conf_obj);
                if ( read_conf == NULL) 
                {
                        DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj);
                        
                } 

                for ( i=0;i < 2;++i)
                {
                        snprintf(param_name, sizeof param_name, "radio_%i.freq", i);
                        /* center_freq */
                        radio_freq[i].center_freq = (uint32_t)json_object_dotget_number(read_conf, param_name);

                        snprintf(param_name, sizeof param_name, "radio_%i.tx_freq_min", i);
                        /* freq_min */
                        radio_freq[i].freq_min = (uint32_t)json_object_dotget_number(read_conf, param_name);

                        snprintf(param_name, sizeof param_name, "radio_%i.tx_freq_max", i);
                        /* freq_max */
                        radio_freq[i].freq_max = (uint32_t)json_object_dotget_number(read_conf, param_name);

                }
                /* 应答读取sx1301的数据 */
                send_sx1301_buff[0]   = 0xfa;
                send_sx1301_buff[1]   = 0x0a;
                send_sx1301_buff[2]   = 0x00;
                send_sx1301_buff[3]   = 0x18;

                /* radio a center freq */
                send_sx1301_buff[4]  |= (uint8_t)(radio_freq[0].center_freq >>24);
                send_sx1301_buff[5]  |= (uint8_t)(radio_freq[0].center_freq >>16);
                send_sx1301_buff[6]  |= (uint8_t)(radio_freq[0].center_freq >>8);
                send_sx1301_buff[7]   = (uint8_t)(radio_freq[0].center_freq);

                /* radio a freq_min */
                send_sx1301_buff[8]  |= (uint8_t)(radio_freq[0].freq_min >>24);
                send_sx1301_buff[9]  |= (uint8_t)(radio_freq[0].freq_min >>16);
                send_sx1301_buff[10] |= (uint8_t)(radio_freq[0].freq_min >>8);
                send_sx1301_buff[11]  = (uint8_t)(radio_freq[0].freq_min);

                /* radio a freq_max */
                send_sx1301_buff[12] |= (uint8_t)(radio_freq[0].freq_max >>24);
                send_sx1301_buff[13] |= (uint8_t)(radio_freq[0].freq_max >>16);
                send_sx1301_buff[14] |= (uint8_t)(radio_freq[0].freq_max >>8); 
                send_sx1301_buff[15]  = (uint8_t)(radio_freq[0].freq_max);

                /* radio b center freq */
                send_sx1301_buff[16] |= (uint8_t)(radio_freq[1].center_freq >>24);
                send_sx1301_buff[17] |= (uint8_t)(radio_freq[1].center_freq >>16);
                send_sx1301_buff[18] |= (uint8_t)(radio_freq[1].center_freq >>8);  
                send_sx1301_buff[19]  = (uint8_t)(radio_freq[1].center_freq);

                /* radio b freq_min */
                send_sx1301_buff[20] |= (uint8_t)(radio_freq[1].freq_min >>24);
                send_sx1301_buff[21] |= (uint8_t)(radio_freq[1].freq_min >>16);
                send_sx1301_buff[22] |= (uint8_t)(radio_freq[1].freq_min >>8);
                send_sx1301_buff[23]  = (uint8_t)(radio_freq[1].freq_min);

                /* radio b freq_max */
                send_sx1301_buff[24] |= (uint8_t)(radio_freq[1].freq_max >>24);
                send_sx1301_buff[25] |= (uint8_t)(radio_freq[1].freq_max >>16);
                send_sx1301_buff[26] |= (uint8_t)(radio_freq[1].freq_max >>8);
                send_sx1301_buff[27]  = (uint8_t)(radio_freq[1].freq_max);

                /* mic */
                send_sx1301_buff[28] = checksum(send_sx1301_buff,28);

                fd_sx1301 = sendto(udp_sockfd,send_sx1301_buff,29,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
                if(fd_sx1301 < 0)
                {
                        DEBUG_CONF("\nsend read sx1301 cmd error!\n");
                        close(udp_sockfd);
                        exit(errno);
                        
                }   
    }
    else
    {
                DEBUG_CONF("Sorry,No Found the conf Json Path,please check it\n");

    }

    /* 每次操作JSON文件后需序列化文件，并释放指针 */
    json_serialize_to_file(read_val,read_cfg_path); 
    json_value_free(read_val);

}

/*
   brief：    应答写sx1301命令
   parameter：需要写入的节点信息
   return：  NULL 
*/
void ack_write_sx1301(uint8_t *buff)
{    
    int  Send_Flag = 0;
    int  fd_write_sx;
    char *read_cfg_path         ="/lorawan/lorawan_hub/global_conf.json";
    const char  conf_obj_name[] ="SX1301_conf";
    char  param_name[32];
    JSON_Value  *root_val = NULL;
    JSON_Object *conf_obj = NULL;

    uint8_t send_buff[6];
    /*
        定义一个临时结构体
        用于存储 radio_a radio_b 的频率
    */
    struct Radio_Buff
    {
        uint32_t center_freq;
        uint32_t freq_min;
        uint32_t freq_max;
    };
    struct Radio_Buff  radio[2];
    struct Radio_Buff  debug_buff[2]; 

    memset(send_buff, 0,6);
    memset(param_name,0,32);
    memset(radio,     0,sizeof(struct Radio_Buff)*2);
    memset(debug_buff,0,sizeof(struct Radio_Buff)*2);
    
    /* 提取PC端传输的数据 */
    /* radio a center_freq */
    radio[0].center_freq |= (*(buff+4)) <<24;
    radio[0].center_freq |= (*(buff+5)) <<16;
    radio[0].center_freq |= (*(buff+6)) <<8;
    radio[0].center_freq |= (*(buff+7));

    DEBUG_CONF("buff[4] values:  0x%x\n",(*(buff+4)));
    DEBUG_CONF("buff[5] values:  0x%x\n",(*(buff+5)));
    DEBUG_CONF("buff[6] values:  0x%x\n",(*(buff+6)));
    DEBUG_CONF("buff[7] values:  0x%x\n",(*(buff+7))); 
    DEBUG_CONF(" radio[0].center_freq:  0x%x\n",radio[0].center_freq);

    /* radio a freq_min */
    radio[0].freq_min    |= (*(buff+8)) <<24;
    radio[0].freq_min    |= (*(buff+9)) <<16;
    radio[0].freq_min    |= (*(buff+10)) <<8;
    radio[0].freq_min    |= (*(buff+11));

    DEBUG_CONF("buff[8]  values:  0x%x\n",(*(buff+8)));
    DEBUG_CONF("buff[9]  values:  0x%x\n",(*(buff+9)));
    DEBUG_CONF("buff[10] values:  0x%x\n",(*(buff+10)));
    DEBUG_CONF("buff[11] values:  0x%x\n",(*(buff+11))); 
    DEBUG_CONF(" radio[0].freq_min:  0x%x\n",radio[0].freq_min);

    /* radio a freq_max */
    radio[0].freq_max    |= (*(buff+12)) <<24;
    radio[0].freq_max    |= (*(buff+13)) <<16;
    radio[0].freq_max    |= (*(buff+14))  <<8;
    radio[0].freq_max    |= (*(buff+15));

    DEBUG_CONF("buff[12] values:  0x%x\n",(*(buff+12)));
    DEBUG_CONF("buff[13] values:  0x%x\n",(*(buff+13)));
    DEBUG_CONF("buff[14] values:  0x%x\n",(*(buff+14)));
    DEBUG_CONF("buff[15] values:  0x%x\n",(*(buff+15))); 
    DEBUG_CONF(" radio[0].freq_max:  0x%x\n",radio[0].freq_max);

    /* radio b center_freq */
    radio[1].center_freq |= (*(buff+16)) <<24;
    radio[1].center_freq |= (*(buff+17)) <<16;
    radio[1].center_freq |= (*(buff+18))  <<8;
    radio[1].center_freq |= (*(buff+19)); 

    DEBUG_CONF("buff[16] values:  0x%x\n",(*(buff+16)));
    DEBUG_CONF("buff[17] values:  0x%x\n",(*(buff+17)));
    DEBUG_CONF("buff[18] values:  0x%x\n",(*(buff+18)));
    DEBUG_CONF("buff[19] values:  0x%x\n",(*(buff+19))); 
    DEBUG_CONF(" radio[1].center_freq:  0x%x\n",radio[1].center_freq);

    /* radio b freq_min */
    radio[1].freq_min    |= (*(buff+20)) <<24;
    radio[1].freq_min    |= (*(buff+21)) <<16;
    radio[1].freq_min    |= (*(buff+22))  <<8;
    radio[1].freq_min    |= (*(buff+23));

    DEBUG_CONF("buff[20] values:  0x%x\n",(*(buff+20)));
    DEBUG_CONF("buff[21] values:  0x%x\n",(*(buff+21)));
    DEBUG_CONF("buff[22] values:  0x%x\n",(*(buff+22)));
    DEBUG_CONF("buff[23] values:  0x%x\n",(*(buff+23))); 
    DEBUG_CONF(" radio[1].freq_min:  0x%x\n",radio[1].freq_min);

    /* radio b freq_max */
    radio[1].freq_max    |= (*(buff+24)) <<24;
    radio[1].freq_max    |= (*(buff+25)) <<16;
    radio[1].freq_max    |= (*(buff+26))  <<8;
    radio[1].freq_max    |= (*(buff+27));
    
    DEBUG_CONF("buff[24] values:  0x%x\n",(*(buff+24)));
    DEBUG_CONF("buff[25] values:  0x%x\n",(*(buff+25)));
    DEBUG_CONF("buff[26] values:  0x%x\n",(*(buff+26)));
    DEBUG_CONF("buff[27] values:  0x%x\n",(*(buff+27))); 
    DEBUG_CONF(" radio[1].freq_max:  0x%x\n",radio[1].freq_max);   

   
    for(int i=0; i < 2; i++)
    {     
        DEBUG_CONF("radio[%d]_freq.freq:    %d\n",i,radio[i].center_freq);
        DEBUG_CONF("radio[%d]_freq.freq_min:%d\n",i,radio[i].freq_min);
        DEBUG_CONF("radio[%d]_freq.freq_max:%d\n",i,radio[i].freq_max);
    }
    
    /* parse json file */
    root_val = json_parse_file(read_cfg_path);
    conf_obj = json_object_get_object(json_value_get_object(root_val),conf_obj_name);
    if (conf_obj == NULL) 
    {
        DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj_name);
        
    }

    /* 修改JSON文件中对应的数值 */
    for(int loop = 0; loop < 2;loop++)
    {
                snprintf(param_name, sizeof (param_name), "radio_%d.freq", loop);
                json_object_dotset_number(conf_obj,param_name,radio[loop].center_freq);

                snprintf(param_name, sizeof (param_name), "radio_%d.tx_freq_min", loop);   
                json_object_dotset_number(conf_obj,param_name,radio[loop].freq_min);

                snprintf(param_name, sizeof (param_name), "radio_%d.tx_freq_max", loop);     
                json_object_dotset_number(conf_obj,param_name,radio[loop].freq_max);
    }

    /* 检测是否正常写入 */
    for(int i = 0; i < 2;i++)
    {
                snprintf(param_name, sizeof(param_name), "radio_%d.freq",        i);
                debug_buff[i].center_freq = json_object_dotget_number(conf_obj,param_name);

                snprintf(param_name, sizeof(param_name), "radio_%d.tx_freq_min", i);   
                debug_buff[i].freq_min    = json_object_dotget_number(conf_obj,param_name);

                snprintf(param_name, sizeof(param_name), "radio_%d.tx_freq_max", i);     
                debug_buff[i].freq_max    = json_object_dotget_number(conf_obj,param_name);

                DEBUG_CONF("radio_[%d].center_freq : %d\n",i,debug_buff[i].center_freq); 
                DEBUG_CONF("radio_[%d].freq_min    : %d\n",i,debug_buff[i].freq_min); 
                DEBUG_CONF("radio_[%d].freq_max    : %d\n",i,debug_buff[i].freq_max); 

                /* 判断是否写入成功 */
                if(debug_buff[i].center_freq == radio[i].center_freq)
                {
                        if( debug_buff[i].freq_min == radio[i].freq_min)
                        {
                                if(debug_buff[i].freq_max == radio[i].freq_max){
                                                Send_Flag = 1;
                                
                                }else{
                                                Send_Flag = 0;
                                }
                        }
                        else
                        {
                                Send_Flag = 0; 
                        }
                }
                else
                {
                        Send_Flag = 0;
                }
    }

    /* 释放json类型指针 */
    json_serialize_to_file(root_val,read_cfg_path); 
    json_value_free(root_val); 
    
    /* 对SX1301写成功的判断 */
    if (1 == Send_Flag)
    {
        /* 数据封装    */

        /* preamble */
        send_buff[0] = 0xfa;
        /* cmd      */
        send_buff[1] = 0x0c;
        /* len      */
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        /* data     */
        send_buff[4] = 0x01;
        /* mic      */ 
        send_buff[5] = checksum(send_buff,5);

        fd_write_sx  = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
  
        /* 杀死重启lora_pkt_fwd的脚本进程 */
        kill(pid_parse,SIGKILL);
        /* 回收子进程                   */
        signal(SIGCHLD,sig_child); 
        /* 重新启动解析进程              */
        if ( (pid_parse = fork()) < 0)
	{
		DEBUG_CONF("fork error");
	}
        else if ( pid_parse == 0)
	{
                /* 调用解析的子进程,依据网关上具体路径填写 */      
                if( execl("/lorawan/lorawan_hub/restart_lora_pkt_fwd.sh", "restart_lora_pkt_fwd.sh",NULL,NULL,(char*)0) == -1 )
                {
                        perror("execle error ");
                        exit(1);
                } 
	} 

 
    }
    else
    {
                /* 数据封装    */
                /* preamble  */
                send_buff[0] = 0xfa;
                /* cmd       */
                send_buff[1] = 0x0c;
                /* len       */
                send_buff[2] = 0x00;
                send_buff[3] = 0x01;

                /* error info */
                send_buff[4] = 0x04;
                /* mic */
                send_buff[5] = checksum(send_buff,5);
                fd_write_sx = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
    }
}

/*
   brief：    应答读远程服务器配置命令
   parameter：需要写入的节点信息
   return：   NULL 
*/
void ack_read_server()
{
    int fd_server;
    uint8_t send_buff[11];
    uint8_t server_ip_buff[4];
    char *read_cfg_path = "/lorawan/lorawan_hub/server_conf.json";
    char conf_obj[]     = "Server_conf";  
    char  *ip_name      = "server_ip"; 
    char  *ip_port      = "server_port";
    /* 读取的服务器地址 */ 
    const char  *server_address   =  NULL;
    /* 读取的服务器端口 */    
    uint16_t server_port;
    int ip_len;
 
    JSON_Value  *read_val;
    JSON_Object *read       = NULL;
    JSON_Object *read_conf  = NULL;

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

    DEBUG_CONF("server_address: %s\n",server_address);
    DEBUG_CONF("server_port:    %d\n",server_port);

    ip_len = strlen(server_address);
    memset(server_ip_buff,0,4);
    
    /* 将字符串ip地址转换成hex */
    Ip_to_Charray(server_ip_buff,server_address,ip_len);

    /* preamble */
    send_buff[0]  = 0xfa;
    /* cmd */
    send_buff[1]  = 0x0e;
    /* len */
    send_buff[2]  = 0x00;
    send_buff[3]  = 0x06;

    /* ip_adddress */
    send_buff[4]  = server_ip_buff[0];
    send_buff[5]  = server_ip_buff[1];
    send_buff[6]  = server_ip_buff[2];
    send_buff[7]  = server_ip_buff[3];
    /* port */
    send_buff[8]  = (uint8_t)(server_port>>8);
    send_buff[9]  = (uint8_t)server_port;
    /* mic */
    send_buff[10] = checksum(send_buff,10);
    /* send */
    fd_server = sendto(udp_sockfd,send_buff,11,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
    if(fd_server  < 0)
    {
        DEBUG_CONF("\nsend ack read server cmd error!\n");
        close(udp_sockfd);
        exit(errno);
    }

    /* 释放json文件指针 */
    json_serialize_to_file(read_val,read_cfg_path); 
    json_value_free(read_val);

}

/*
   brief：    应答写远程服务器配置命令
   parameter：需要写入的节点信息
   return：   NULL 
*/
void ack_write_server(uint8_t *buff)
{
    int fd_server;
    uint8_t send_buff[6];
    memset(send_buff,0,6);
    uint8_t ip_buff[4];
    memset(ip_buff,0,4);
    char *read_cfg_path = "/lorawan/lorawan_hub/server_conf.json";
    char conf_obj[]     = "Server_conf";
    char  *ip_name      = "server_ip"; 
    char  *ip_port      = "server_port";
 
    uint16_t udp_ip_port;
    char *server_ip = malloc(100);     

    /* json文件初始化 */
    JSON_Value  *read_val;
    JSON_Object *read       = NULL;
    JSON_Object *read_conf  = NULL;
    /* 接收pc端数据 */
    ip_buff[0] = *(buff+4);
    ip_buff[1] = *(buff+5);
    ip_buff[2] = *(buff+6);
    ip_buff[3] = *(buff+7);
    udp_ip_port    = (*(buff+8)) << 8;
    udp_ip_port   |= (*(buff+9));
    sprintf(server_ip,"%d.%d.%d.%d",ip_buff[0],ip_buff[1],ip_buff[2],ip_buff[3]);
    
    /* Debug */
    for(int i=0; i<4;i++)
    {
        DEBUG_CONF("ip_buff[%d]:0x%x\n",i,ip_buff[i]);
    }
    DEBUG_CONF("ip_port:%d\n",udp_ip_port);

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

        /* 释放json类型指针 */
        json_object_dotset_string(read_conf,ip_name,server_ip);
        json_object_dotset_number(read_conf,ip_port,udp_ip_port);  
    }

    /* preamble */
    send_buff[0] = 0xfa;
    /* cmd */
    send_buff[1] = 0x10;
    /* len */
    send_buff[2] = 0x00;
    send_buff[3] = 0x01;
    /* data */
    send_buff[4] = 0x01;
    /* mic  */
    send_buff[5] = checksum(send_buff,5);
    fd_server = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
    if(fd_server  < 0)
    {
        DEBUG_CONF("\nsend ack write server cmd error!\n");
        close(udp_sockfd);
        exit(errno);
    }
    
    /* 
        每次写过服务器配置之后，重启lora_pkt_server进程
        杀死解析进程
    */
    kill(pid_server,SIGKILL);
    /*  回收子进程，避免僵尸进程 */
    signal(pid_server,sig_child); 

    /*  重新启动解析进程 */
    if ( ( pid_server = fork()) < 0)
    {
	        DEBUG_CONF("fork error");
    }
    else if ( pid_server == 0)
    {
        /* 重启解析进程 */    
        if( execl("/lorawan/lorawan_hub/lora_pkt_server", "lora_pkt_server",NULL,NULL,(char*)0) == -1 )
        {
                perror("execle error ");
                exit(1);
        }

    }
    
     /* 释放堆区内存 */   
     free(server_ip);       
    /* 释放json指针 */
    json_serialize_to_file(read_val,read_cfg_path); 
    json_value_free(read_val);

}


/**
 * brief：应答删除单个节点信息
 * 
 * parameter：需要写入的节点信息
 * 
 * return：  NULL
 * 
 */
void ack_delete_node(uint8_t *buff)
{
    sqlite3_stmt *stmt_delete = NULL;

    int fd_delete;
    int rc;
    char *deveui_string = (char*)malloc(2*8);
    char *deveui_p = NULL;
    char *sql_deveui   = (char*)malloc(100);

    uint8_t delete_info[8];
    uint8_t send_buff[6];
    memset(delete_info,0,8);
    memset(send_buff,0,6);

    /* 提取需要删除的节点的信息 */
    mymemcpy(delete_info,buff+4,8);
    deveui_p = ChArray_To_String(deveui_string,delete_info,8);
    DEBUG_CONF("deveui:%s\n",deveui_p);

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
    
    /* sql */
    sprintf(sql_deveui,"DELETE FROM GWINFO WHERE DEVEui ='%s';",deveui_p); 
    DEBUG_CONF("sql_deveui:%s\n",sql_deveui);

    /* prepare */
    rc = sqlite3_prepare_v2(db,sql_deveui,strlen(sql_deveui),&stmt_delete,NULL);  
    if( rc != SQLITE_OK || NULL == stmt_delete)
    {
        DEBUG_CONF("SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    }
    else
    {      
        DEBUG_CONF("Delete node successfully\n");
    }

     /* execute */
     rc = sqlite3_step(stmt_delete);

     if ( SQLITE_DONE == rc)
     {
            /* 数据封装 */
            
            /* preamble */
            send_buff[0] = 0xfa;
            /* cmd */
            send_buff[1] = 0x12;
            /* len */
            send_buff[2] = 0x00;
            send_buff[3] = 0x01;
            /* data */
            send_buff[4] = 0x01;
            /* mic  */
            send_buff[5] = checksum(send_buff,5);

            /* 发送 */
            fd_delete = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
            if(fd_delete  < 0)
            {
                    DEBUG_CONF(" %d\n",errno);
                    fprintf(stderr,"send error: %s\n",strerror(errno));
            }
     }
     sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);

    if ( stmt_delete != NULL) 
            sqlite3_finalize(stmt_delete); 
    
    if ( deveui_p != NULL) 
            free(deveui_p);
    
    if ( sql_deveui != NULL) 
            free(sql_deveui);

}

/**
 * brief：    应答删除所有节点信息
 * 
 * parameter：NULL
 * 
 * return：  NULL
 * 
 */
void ack_delete_all_node(void)
{
    int rc;
    int fd_all;
    char *sql = NULL; 
    uint8_t send_buff[6];
    memset(send_buff,0,6); 
    bool DeleteIsOk = false;

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);

    /* 删除所有节点SQL命令 */
    sql = "DELETE FROM GWINFO WHERE ID != 4200000000";
    
    rc = sqlite3_exec(db, sql, 0, 0, &zErrMsg);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);

    if ( rc != SQLITE_OK ) {

            DEBUG_CONF("SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
    
    } else {

            DEBUG_CONF("DELETE successfully\n");
            DeleteIsOk = true;
    }
    
    if ( DeleteIsOk ) {

            /* 删除成功,发送正确应答 */
            //preamble
            send_buff[0] = 0xfa;
            //cmd
            send_buff[1] = 0x10;
            //len
            send_buff[2] = 0x00;
            //len
            send_buff[3] = 0x01;
            //data
            send_buff[4] = 0x01;
            //mic
            send_buff[5] = checksum(send_buff,5);

            /* 发送 */
            fd_all = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        
            if ( fd_all  < 0 ){
            
                    DEBUG_CONF("\nsend ack write server cmd error!\n");
                    close(udp_sockfd);
                    exit(errno);    
            }

    } else {

                DEBUG_CONF("delete all node info is fail\n");        

    }
    
}

/**
 * brief：    读取所有rx2_freq 用于class c设备类型
 * 
 * parameter： NULL
 * 
 * return：    NULL
 * 
 */
void read_all_rx2_freq(void)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    int fd;
    char *sql = NULL;
    uint32_t frequency;
    uint8_t send_buff[9];

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);

    /* sql */
    sql = "SELECT* FROM GWINFO WHERE ID != 4200000000;";  
    
    /* parpare */
    rc = sqlite3_prepare_v2(db,sql,strlen(sql),&stmt,NULL);
    if (SQLITE_OK !=rc || NULL == stmt){
            DEBUG_CONF("\nread rx2 prepare error!\n");
            sqlite3_close(db); 
    }
    
    /* execute */
    rc = sqlite3_step(stmt);
 
    if(SQLITE_ROW == rc){
            frequency = sqlite3_column_int(stmt,9);
            DEBUG_CONF("frequency:%u\n",frequency);                 
    }else{
            DEBUG_CONF("sorry,no this rx2_frequency,please check it!");
    }
    
    /* 消息封装 */
    
    /* preamble */
    send_buff[0] = 0xfa;
    /* cmd */
    send_buff[1] = 0x16;
    /* len */
    send_buff[2] = 0x00;
    send_buff[3] = 0x04;
    /* freq */
    send_buff[4] = (uint8_t)(frequency>>24);
    send_buff[5] = (uint8_t)(frequency>>16);
    send_buff[6] = (uint8_t)(frequency>>8);
    send_buff[7] = (uint8_t)frequency;
    /* mic */
    send_buff[8] = checksum(send_buff,8);
    
    fd = sendto(udp_sockfd,send_buff,9,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
    if(fd  < 0)
    {
        DEBUG_CONF(" %d\n",errno);
        fprintf(stderr,"send error: %s\n",strerror(errno));
       
    }
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    
    if ( stmt != NULL) 
            sqlite3_finalize(stmt);
}

/**
 * brief：    配置所有节点的频率，用于class c设备类型
 * 
 * parameter： NULL
 * 
 * return：    NULL
 * 
 */
void conf_all_rx2_freq(uint8_t *buff)
{ 
    int rc;
    int fd;
    char *sql = NULL; 
    char *sql_buff = (char*)malloc(100);
    bool  Set_rx2_is_Ok = false;
    uint8_t send_buff[6];
    uint32_t frequency;

    frequency  =  buff[7];
    frequency |= (buff[6]<<8);    
    frequency |= (buff[5]<<16);
    frequency |= (buff[4]<<24);    
    DEBUG_CONF("frequency:%u\n",frequency);

    /* sql */
    sprintf(sql_buff,"UPDATE GWINFO SET Rx2_Freq = %u WHERE ID != 4200000000",frequency);
    DEBUG_CONF("sql:%s\n",sql_buff);
    
    /* execute */
    rc = sqlite3_exec(db,sql_buff,NULL,NULL,&zErrMsg);
    if( rc != SQLITE_OK)
    {
            DEBUG_CONF("SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
            Set_rx2_is_Ok = false;
    }
    else
    {      
            DEBUG_CONF("Set Rx2_Frequency Successfully\n");
            Set_rx2_is_Ok = true;
    }

    if(Set_rx2_is_Ok)
    {
            /* 应答配置所有节点的rx2_frequency */
            
            /* preamble */
            send_buff[0] = 0xfa;
            send_buff[1] = 0x18;
            send_buff[2] = 0x00;
            send_buff[3] = 0x01;
            /* data */
            send_buff[4] = 0x01;
            /* mic */
            send_buff[5] = checksum(send_buff,5);
            fd = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
            if(fd  < 0)
            {
                    DEBUG_CONF(" %d\n",errno);
                    fprintf(stderr,"send error: %s\n",strerror(errno));   
            }
    }
    else
    {
            /* preamble */
            send_buff[0] = 0xfa;
            send_buff[1] = 0x18;
            send_buff[2] = 0x00;
            send_buff[3] = 0x01;
            /* data */
            send_buff[4] = 0x04;
            /* mic */
            send_buff[5] = checksum(send_buff,5);
            fd = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
            if(fd  < 0)
            {
                    DEBUG_CONF(" %d\n",errno);
                    fprintf(stderr,"send error: %s\n",strerror(errno));     
            }
    }
}
 
/**
 *  Update:2019.2.18
 *  ABP入网配置数据处理
 */
/**
 *                                 ABP写节点信息命令: pc-->网关
 *  ___________________________________________________________________________________
 * |                                  |                                       |        | 
 * |            Header                |                 Data                  |   MIC  | 
 * |__________________________________|_______________________________________|________|
 * |                                  |         |         |         |         |        | 
 * |    Preamble  |   CMD   |   LEN   |  Deveui | Devaddr | Nwkskey | Appskey |        | 
 * |      1byte   |  1byte  |  2bytes |_________|_________|_________|_________|        | 
 * |              |         |         |         |         |         |         |        | 
 * |       FA     |    29   |   002C  |  8bytes |  4bytes | 16bytes | 16bytes |  1byte | 
 * |__________________________________________________________________________|________|        
 */
/**       应答ABP写节点信息命令: 网关-->PC
        +-----------------------+------------------+-------+
        |        Header         |      Data        |  MIC  |
        +-----------------------+------------------+-------+
        | Preamble CMD    LEN   | 0x00 success     |       |
        | 1byte   1byte  2bytes | 0X01 fail        |       |
        |  FA      2A     0001  |                  |1bytes |
        +-----------------------+------------------+-------+
   */  

void ABP_Configure_Node_Info(uint8_t *buff)
{
    /**
         思路： 
         
     *    1: 提取devaddr信息 查找数据库中是否有该devaddr 如果已经存在则返回0x05 否则进行下面的操作 
     *    2: 提取deveui,在数据库中进行寻找
     *    3: 找到后，将原数据进行覆盖 
     *    4: 没找到，将该节点信息作为新的节点信息进行插入
     */

     /* 创建需要的变量 */ 
    int rc;
    int maxid;
    int fd;
    uint8_t *p;
    sqlite3_stmt    *stmt_devaddr = NULL;
    sqlite3_stmt    *stmt = NULL;
    sqlite3_stmt    *stmt_searchid = NULL;
    uint8_t          deveui[8];
    uint8_t          devaddr[4];
    uint8_t          nwkskey[16];
    uint8_t          appskey[16];
    uint8_t          sendbuff[10];
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
    
    /* 变量初始化 */ 
    p = buff;
    memset(deveui,  0,  8);
    memset(devaddr, 0,  4);
    memset(nwkskey, 0, 16);
    memset(appskey, 0, 16);
    memset(sendbuff,0, 10);
    /* 仅为了填充appkey字段 */
    appkey = "ffffffffffffffffffffffffffffffff";
    /* 将ABP入网配置的有效字节转换为字符串格式 */
       
    mymemcpy(deveui,  p + 4,  8);
    mymemcpy(devaddr, p + 12, 4);
    mymemcpy(nwkskey, p + 16,16);

    /* 该语句会报 Segment fault, 暂不知道什么原因导致 */
    //mymemcpy(appskey, p + 32,16);

    deveui_p  = ChArray_To_String(deveui_str,   deveui,   8);
    devaddr_p = ChArray_To_String(devaddr_str,  devaddr,  4);
    nwkskey_p = ChArray_To_String(nwkskey_str,  nwkskey, 16);
    appskey_p = ChArray_To_String(appskey_str,  p + 32,  16);

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

 
    if ( DevaddrIsExist) {
            sendbuff[0] = 0xfa;
            sendbuff[1] = 0x2a;
            sendbuff[2] = 0x00;
            sendbuff[3] = 0x01;
            /* devaddr conflict */
            sendbuff[4] = 0x05;
            sendbuff[5] = checksum(sendbuff,5);
            fd = sendto(udp_sockfd,sendbuff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
            if(fd  < 0){
                    
                    DEBUG_CONF("\nsend ack abp cmd error!\n");
                    close(udp_sockfd);          
            }

    }else{ /* 地址不冲突 */
    
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

            /* 在GWINFO表中成功找到该节点，则覆盖原来的devaddr,nwkskey,appskey */   
            if ( SeekDeveuiIsOk) {
            
                sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL);  

                sprintf( devaddr_sql,"UPDATE GWINFO SET Devaddr = '%s' WHERE DEVEui = '%s';",devaddr_p,deveui_p);
                rc = sqlite3_exec( db,devaddr_sql,0,0,&zErrMsg);
                CHECK_RC(rc,zErrMsg,db);    
            
                sprintf( nwkskey_sql,"UPDATE GWINFO SET Nwkskey = '%s' WHERE DEVEui ='%s';", nwkskey_p,deveui_p);
                rc = sqlite3_exec( db,nwkskey_sql,0,0,&zErrMsg);
                CHECK_RC(rc,zErrMsg,db);  

                sprintf( appskey_sql,"UPDATE GWINFO SET APPskey = '%s' WHERE DEVEui ='%s';", appskey_p, deveui_p);
                rc = sqlite3_exec( db,appskey_sql,0,0,&zErrMsg);
                CHECK_RC(rc,zErrMsg,db);

                sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);  
        

            }else{ /* 表中没有该节点的信息，插入一条新的节点信息 */

                    /**
                     *将表中的id按降序排布，寻找表中id的最大值 
                    * 
                    *  ascending   order: 升序
                    *  descending  order: 降序
                    * 
                    * limit 1:限制返回结果只有1个
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

                    /**
                     * update:2019.2.20
                     * 待优化的地方，新插入的节点的地址，需要进行防重复机制 
                     * 
                     */
                    
                    /**
                     * 开发人员注：因前期创建GWINFO 数据库表时， APPKey 字段为 NOT NULL，
                     * 而该数据库表仅在开始创建1次。
                     * 因此：需在该函数内部创建appkey字符串变量填充该字段 
                     * 默认为32个字面量'0'
                     */

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
            }

            /**
             * 找到该节点，更新成功 
             * 
             * 未找到该节点，插入节点： 0x00:插入成功， 0x01 插入失败
             * 
             */
            if (SeekDeveuiIsOk) {
                    sendbuff[0] = 0xfa;
                    sendbuff[1] = 0x2a;
                    sendbuff[2] = 0x00;
                    sendbuff[3] = 0x01;
                    /* configure successful */
                    sendbuff[4] = 0x01;
                    sendbuff[5] = checksum(sendbuff,5);
                    fd = sendto(udp_sockfd,sendbuff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
                    if(fd  < 0){
                            
                            DEBUG_CONF("\nsend ack abp cmd error!\n");
                            close(udp_sockfd);          
                    }

            }else{
                
                    if ( InsertNodeIsOk) {   
                            sendbuff[0] = 0xfa;
                            sendbuff[1] = 0x2a;
                            sendbuff[2] = 0x00;
                            sendbuff[3] = 0x01;
                            /* configure successful */
                            sendbuff[4] = 0x01;
                            sendbuff[5] = checksum(sendbuff,5);
                            fd = sendto(udp_sockfd,sendbuff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
                            if(fd  < 0){

                                    DEBUG_CONF("\nsend ack abp cmd error!\n");
                                    close(udp_sockfd);
                            }

                    }else{
                            sendbuff[0] = 0xfa;
                            sendbuff[1] = 0x2a;
                            sendbuff[2] = 0x00;
                            sendbuff[3] = 0x01;
                            /* configure fail */
                            sendbuff[4] = 0x04;
                            sendbuff[5] = checksum(sendbuff,5);
                            fd = sendto(udp_sockfd,sendbuff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
                            if(fd  < 0){

                                    DEBUG_CONF("\nsend ack abp cmd error!\n");
                                    close(udp_sockfd);
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

} 

/**
 *  Update:2019.2.19
 *  ABP读节点信息命令
 */
/**
 *                    ABP读节点信息命令            
    +-----------+--------+--------+---------+--------+--+
    |             Header          |  Data   |   MIC  |  |
    +-----------+--------+--------+---------+--------+--+
    | Preamble  | CMD    |  LEN   |  DevEui |  1byte |  |
    | 1byte     | 1byte  | 2byte  |  8byte  |        |  |
    |  FA       |  2C    |  0008  |         |        |  |
    +-----------+--------+--------+---------+--------+--+
*/
/**
                     应答ABP读节点信息命令  
    +-----------+--------+--------+-----------+---------+---------+--------+
    |              Header         |               Data            |  MIC   |
    +-----------+--------+--------+-----------+---------+---------+--------+
    | Preamble  |  CMD   |  LEN   |  Devaddr  | Nwkskey | Appskey |  1byte |
    | 1byte     | 1byte  | 2byte  |   4byte   |  16byte | 16byte  |        |
    |  FA       |  2C    |  002C  |           |         |         |        |
    +-----------+--------+--------+-----------+---------+---------+--------+

 */

void ABP_Fetch_Node_Info(uint8_t *buff)
{
     /* 创建需要的变量 */ 
    int rc;
    int fd;
    sqlite3_stmt    *stmt = NULL;
    uint8_t          deveui[8];
    uint8_t          devaddr[4];
    uint8_t          nwkskey[16];
    uint8_t          appskey[16];
    uint8_t          sendbuff[55];
    bool  SeekDeveuiIsOk =  false;
    char *deveui_str     = (char*)malloc(2*8);
    char *deveui_sql     = (char*)malloc(100);
    char *deveui_p       =  NULL;
    const char *devaddr_str    = NULL;
    const char *nwkskey_str    = NULL;
    const char *appskey_str    = NULL;

    /* 变量初始化 */ 
    memset(deveui,  0,  8);
    memset(devaddr, 0,  4);
    memset(nwkskey, 0, 16);
    memset(appskey, 0, 16);
    memset(sendbuff,0, 10);

    /* 将deveui有效字节转换为字符串格式 */
    mymemcpy(deveui,  buff+4, 8);
    deveui_p  = ChArray_To_String(deveui_str,  deveui,   8);

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
            
            /* preamble */
            sendbuff[0] = 0xfa;
            /* cmd */
            sendbuff[1] = 0x2d;
            /* len */
            sendbuff[2] = 0x00;
            sendbuff[3] = 0x24;
            /* devaddr */
            mymemcpy( sendbuff+4, devaddr,4);
            /* nwkskey */
            mymemcpy( sendbuff+8, nwkskey,16);
            /* appskey */
            mymemcpy( sendbuff+24,appskey,16);  
            /* mic */
            sendbuff[40] = checksum( sendbuff,40); 

            /* send data*/
            fd = sendto(udp_sockfd,sendbuff,41,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
            if(fd  < 0){

                    DEBUG_CONF("\nsend ack write cmd error!\n");
                    close(udp_sockfd);
            }
            
    }else{

        /* 没有找到该节点数据，不应答 */
    }
    
    /* 释放堆区资源 */
    if ( deveui_sql != NULL)
            free(deveui_sql);

    if ( deveui_p != NULL)
        free(deveui_p);

    if (stmt != NULL)
            sqlite3_finalize(stmt);

}

//插入原始数据
//测试使用
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
    int index7;
    int index8;
    int index9;
    int index10;

    int ID = 0;
    uint32_t fcnt_up_a   = 0;
    uint32_t fcnt_down_a = 0;
    uint32_t fcnt_up_b   = 0;
    uint32_t fcnt_down_b = 0;
    uint32_t fcnt_up_c   = 0;
    uint32_t fcnt_down_c = 0;
    uint32_t freq_a      = 0;
    uint32_t freq_b      = 0;
    uint32_t freq_c      = 0;     

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
    char *Appeui_a   ="0000000000000000";   
    //group b data  
    char *Appkey_b   ="00112233445566778899aabbccddeeff";
    char *Deveui_b   ="004a7700660033c9";
    char *Devaddr_b  ="00112233";
    char *Nwkskey_b  ="0123456789abcedf0123456789abcdef";
    char *APPskey_b  ="0123456789abcedf0123456789abcdef";
    char *Appeui_b   ="0000000000000000";  
    //group c data
    char *Appkey_c   ="00112233445566778899aabbccddeeff";
    char *Deveui_c   ="004a770066000b33"; 
    char *Devaddr_c  ="00112233";
    char *Nwkskey_c  ="0123456789abcedf0123456789abcdef";
    char *APPskey_c  ="0123456789abcedf0123456789abcdef";
    char *Appeui_c   ="0000000000000000";  

    sqlite3_stmt *stmt = NULL;
    //语句并不执行。生成二进制sql语句
    // 准备语句并不执行，生成二进制sql语句
     if(sqlite3_prepare_v2
                         (
                             db,
                             "INSERT INTO GWINFO VALUES(:x,:y,:z,:l,:m,:k,:v,:p,:q,:u);",
                             -1,
                             &stmt,
                             0
                         )
                         != SQLITE_OK
       )
     {
  
        DEBUG_CONF("\nCould not prepare statement.\n");
        sqlite3_free(zErrMsg);
        sqlite3_close(db);        
     }
     index1  = sqlite3_bind_parameter_index(stmt,":x");
     index2  = sqlite3_bind_parameter_index(stmt,":y");
     index3  = sqlite3_bind_parameter_index(stmt,":z");
     index4  = sqlite3_bind_parameter_index(stmt,":l");
     index5  = sqlite3_bind_parameter_index(stmt,":m");
     index6  = sqlite3_bind_parameter_index(stmt,":k");
     index7  = sqlite3_bind_parameter_index(stmt,":v"); 
     index8  = sqlite3_bind_parameter_index(stmt,":p"); 
     index9  = sqlite3_bind_parameter_index(stmt,":q");
     index10 = sqlite3_bind_parameter_index(stmt,":u"); 
     DEBUG_CONF("\n The statement has %d wildcards\n",sqlite3_bind_parameter_count(stmt));
     //记录插入数据库表的开始时间
    // starttime1 = timecacul(); 
     //开BEGIN TRANSACTION功能，可以大大提高插入表格的效率
     sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
     for (ID=1;ID < 4; ID++)
    {
        switch(ID)
        {
            case 1:
            {
                if(sqlite3_bind_int(stmt,index1,ID)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                } 
                if(sqlite3_bind_text(stmt,index2,Appkey_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");
                     sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index3,Deveui_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index4,Devaddr_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index5,Nwkskey_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index6,APPskey_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index7,Appeui_a,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_int(stmt,index8,fcnt_up_a)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                }
                if(sqlite3_bind_int(stmt,index9,fcnt_down_a)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                }
                if(sqlite3_bind_int(stmt,index10,freq_a)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                }
                if (sqlite3_step(stmt) != SQLITE_DONE)
                {
                    DEBUG_CONF("\nCould not step (execute)stmt.\n");
                    sqlite3_free(zErrMsg);
                    sqlite3_close(db);  
                } 
                    sqlite3_reset(stmt);   
            }break;

            case 2:
            {
                if(sqlite3_bind_int(stmt,index1,ID)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                } 
                if(sqlite3_bind_text(stmt,index2,Appkey_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");
                     sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index3,Deveui_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index4,Devaddr_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index5,Nwkskey_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index6,APPskey_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index7,Appeui_b,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_int(stmt,index8,fcnt_up_b)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                }
                if(sqlite3_bind_int(stmt,index9,fcnt_down_b)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                }
                if(sqlite3_bind_int(stmt,index10,freq_b)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                }
                if (sqlite3_step(stmt) != SQLITE_DONE)
                {
                    DEBUG_CONF("\nCould not step (execute)stmt.\n");
                    sqlite3_free(zErrMsg);
                    sqlite3_close(db);  
                }
                    sqlite3_reset(stmt);   
            }break;
            
            case 3:
            {
                if(sqlite3_bind_int(stmt,index1,ID)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                } 
                if(sqlite3_bind_text(stmt,index2,Appkey_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");
                     sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index3,Deveui_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index4,Devaddr_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index5,Nwkskey_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index6,APPskey_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_text(stmt,index7,Appeui_c,-1,SQLITE_STATIC)!= SQLITE_OK)
                {
                    DEBUG_CONF("\nCould not bind int.\n");
                    sqlite3_free(zErrMsg);
                }
                if(sqlite3_bind_int(stmt,index8,fcnt_up_c)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                }
                if(sqlite3_bind_int(stmt,index9,fcnt_down_c)!= SQLITE_OK)
                {
                     DEBUG_CONF("\nCould not bind int.\n");     
                     sqlite3_free(zErrMsg);  
                }
                if(sqlite3_bind_int(stmt,index10,freq_c)!= SQLITE_OK)
                {
                        DEBUG_CONF("\nCould not bind int.\n");     
                        sqlite3_free(zErrMsg);  
                }
                if (sqlite3_step(stmt) != SQLITE_DONE)
                {
                    DEBUG_CONF("\nCould not step (execute)stmt.\n");
                    sqlite3_free(zErrMsg);
                    sqlite3_close(db);  
                }
                    sqlite3_reset(stmt);   
            }break;
            #if 0
            default:
                   {
                      printf("NOT NEED NEW ID INSERT\n");
                     
                   } break;
            #endif 
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

/*  
    brief:      线程函数 处理服务器下发sx1301的配置信息
    parameter:  null
    return:     null 
*/
void *ThreadHandleSx1301Task(void)
{
    uint16_t cycle_count;
    int repeat = -1; 
    int sock_fd;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    int ret;
    socklen_t addr_len;
    int recv_len;
    int send_len;
    uint8_t recv_buff[30];
    uint8_t send_buff[4];


    bool ConfIsOk    = false;
    bool RecvIsOk    = false;

    /* 读取本地SX1301配置信息 */
    char *read_cfg_path  ="/lorawan/lorawan_hub/global_conf.json";
    char conf_obj_name[] ="SX1301_conf";
    char param_name[32];
    
    /* JSON变量 */
    JSON_Value  *root_val = NULL;
    JSON_Object *conf_obj = NULL;

    /*
        定义一个临时结构体
        用于存储 radio_a radio_b 的频率
    */
    struct Freq_Buff
    {
        uint32_t center_freq;
        uint32_t freq_min;
        uint32_t freq_max;
    };  
    struct Freq_Buff write_radio_freq[2];
    struct Freq_Buff read_radio_freq[2];

    /* init */ 
    memset(recv_buff,0,30);
    memset(send_buff,0, 4);
    memset(write_radio_freq,0,sizeof(struct Freq_Buff)*2); /* 定义的结构体 需初始化 否则会导致取出的数据有问题 */
    memset(read_radio_freq, 0,sizeof(struct Freq_Buff)*2);
    memset(param_name,0,32);

    /*  creat socket */
    sock_fd = socket(AF_INET,SOCK_DGRAM,0); /* AF_INET:ipv4;SOCK_DGRAM:udp */
    if(-1 == sock_fd)
    {
        DEBUG_CONF("socket error:%s\n",strerror(errno));
        close(sock_fd);
    }
    
    memset(&server_addr,0,sizeof(struct sockaddr_in));
    
    /* set sockaddr_in parameter */
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    /* 规定：5559端口用于接收服务器下发的sx1301配置的数据 */
    server_addr.sin_port   = htons(SERVER_CONF_SX1301);

    /* bind */
    ret = bind(sock_fd,(struct sockaddr*)(&server_addr),sizeof(struct sockaddr) );
    if(-1 == ret)
    {
        DEBUG_CONF("bind error:%s\n",strerror(errno));
        close(sock_fd);

    }
    
    while ((repeat == -1) || (cycle_count < repeat))
    {   
        cycle_count++;
        addr_len = sizeof(struct sockaddr_in); 
        
        recv_len = recvfrom(sock_fd,recv_buff,300,0,(struct sockaddr*)&client_addr,&addr_len);
        if(recv_len <= 0)
        {
                DEBUG_CONF("recv_from error:%s\n",strerror(errno));
            
                /*
                    防止此时recvfrom函数捕捉到lora_pkt_fwd.c的进程被pc上位机重启后产生的信号
                    此时为了避免recvfrom函数被中断，加入自重启机制
                */
                if ( errno == EINTR){
                        continue;
                } else{
                        close(sock_fd);
                        RecvIsOk = false;
                }
        }
        else
        {
            RecvIsOk = true;
        }

        if(RecvIsOk)
        {
                
                /* 
                                        大端传输
                    lora_pkt_fwd.c<-------------------->lora_pkt_conf.c
                */
                
                /* index:控制赋值的频率的索引 */
                int index = 0;

                /* i:控制write_radio_freq的循环 */
                for ( int i=0; i < 2; i++)
                {
                        /* loop:控制哪个频率进行赋值 */
                        for ( int loop = 0; loop < 3; loop++)
                        {
                                /* loop_a:控制循环 */
                                for ( int loop_a = 0,bitmove = 24;loop_a < 4;loop_a++)
                                {
                                        if(0 == loop)
                                        {
                                            write_radio_freq[i].center_freq |= (recv_buff[index++] << bitmove);
                                            bitmove-=8; //8 bit
                                        }
                                        if(1 == loop)
                                        {
                                            write_radio_freq[i].freq_min    |= (recv_buff[index++] << bitmove);
                                            bitmove-=8; //8 bit
                                        }
                                        if(2 == loop)
                                        {
                                            write_radio_freq[i].freq_max    |= (recv_buff[index++] << bitmove);
                                            bitmove-=8; //8 bit
                                        }

                                }
                        }    
                }

                /* 解析json file */
                root_val = json_parse_file(read_cfg_path);
                conf_obj = json_object_get_object(json_value_get_object(root_val),conf_obj_name);
                if (conf_obj == NULL) {

                        DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj_name);
                }

                /* 修改JSON文件中对应的数值 */
                for(int loop = 0; loop < 2;loop++)
                {
                        snprintf(param_name, sizeof (param_name), "radio_%d.freq", loop);
                        json_object_dotset_number(conf_obj,param_name,write_radio_freq[loop].center_freq);
  
                        snprintf(param_name, sizeof (param_name), "radio_%d.tx_freq_min", loop);   
                        json_object_dotset_number(conf_obj,param_name,write_radio_freq[loop].freq_min);

                        snprintf(param_name, sizeof (param_name), "radio_%d.tx_freq_max", loop);     
                        json_object_dotset_number(conf_obj,param_name,write_radio_freq[loop].freq_max);
                }

                /* 检测是否成功写入数值 */
                for(int i = 0; i < 2;i++)
                {
                        snprintf(param_name, sizeof(param_name), "radio_%d.freq", i);
                        read_radio_freq[i].center_freq = json_object_dotget_number(conf_obj,param_name);

                        snprintf(param_name, sizeof(param_name), "radio_%d.tx_freq_min", i);   
                        read_radio_freq[i].freq_min    = json_object_dotget_number(conf_obj,param_name);

                        snprintf(param_name, sizeof(param_name), "radio_%d.tx_freq_max", i);     
                        read_radio_freq[i].freq_max    = json_object_dotget_number(conf_obj,param_name);
                           
                        /* 判断是否写入成功 */
                        if ( read_radio_freq[i].center_freq == write_radio_freq[i].center_freq ){
                                    
                                    if ( read_radio_freq[i].freq_min == write_radio_freq[i].freq_min ){

                                                if ( read_radio_freq[i].freq_max == write_radio_freq[i].freq_max ){

                                                        ConfIsOk = true;
                                                
                                                }else{
                                                       
                                                        ConfIsOk = false;
                                                }
                                    }else{
                                        
                                        ConfIsOk = false;
                                    }

                        }else{
                            
                            ConfIsOk = false;
                        }
                }

                /* 释放json对象 */
                json_serialize_to_file(root_val,read_cfg_path); 
                json_value_free(root_val);  

                /*
                        配置成功，发送消息给lora_pkt_server.c 
                                            
                                            大端传输
                        lora_pkt_fwd.c<----------------------->lora_pkt_conf.c
                        send_data = 0xfafbfcfd: successful
                        send_data = 0xffffffff: fail
                */

                if ( ConfIsOk)
                {          
                        DEBUG_CONF("ConfIsOk = true");   
                        for ( int i=0; i < 4; i++)
                        {
                            send_buff[i] = (0xfa + i);
                        }
                        send_len = sendto(sock_fd,send_buff,4,0,(struct sockaddr*)&client_addr,addr_len);
                        if(send_len <= 0)
                        {
                            DEBUG_CONF("sendto error:%s\n",strerror(errno));
                            close(sock_fd);
                        }
                }
                else
                {
                        DEBUG_CONF("ConfIsOk = false");  
                        for ( int i=0; i < 4; i++)
                        {
                            send_buff[i] = 0xff;
                        }
                        send_len = sendto(sock_fd,send_buff,4,0,(struct sockaddr*)&client_addr,addr_len);
                        if(send_len <= 0)
                        {
                            DEBUG_CONF("sento error:%s\n",strerror(errno));
                            close(sock_fd);
                        }
                }
        }
        else
        {
                DEBUG_CONF("recv sx1301 data is error!\n");
                
                for(int i=0; i < 4; i++)
                {
                    send_buff[i] = 0xff;
                }
                send_len = sendto(sock_fd,send_buff,4,0,(struct sockaddr*)&client_addr,addr_len);
                if(send_len <= 0)
                {
                    DEBUG_CONF("recv_from error:%s\n",strerror(errno));
                    close(sock_fd);
                }        
        }

        /*
            重启lora_pkt_fwd.c进程
            回收子进程
        */
        kill(pid_parse,SIGKILL);
        signal(SIGCHLD,sig_child); 
        
        /* 重新生成解析进程pid */
        if ( ( pid_parse = fork()) < 0)
        {
            DEBUG_CONF("fork error");
        }
        else if ( pid_parse == 0)
        {
                /* 重启lora_pkt_fwd的脚本进程 */ 
                if( execl("/lorawan/lorawan_hub/restart_lora_pkt_fwd.sh", "restart_lora_pkt_fwd.sh",NULL,NULL,(char*)0) == -1 )
                {
                    perror("execle error ");
                    exit(1);
                } 

        }

        /* clear */
        memset(write_radio_freq,0,sizeof(struct Freq_Buff)*2);
        memset(read_radio_freq, 0,sizeof(struct Freq_Buff)*2); 
        memset(recv_buff,0,30);
        memset(send_buff,0, 4);
        memset(param_name,0,32);
        recv_len = 0;
        send_len = 0;
    }
    
    /* 关闭套接字 */
    close(sock_fd);

}

/**
 *  brief     :      线程函数   ：处理lora_pkt_fwd.c上报的节点的deveui 
 *                   和 解析后的数据信息,发送到GateWay数据接收窗口
 *  
 *  parameter : NULL
 * 
 *  return    : NULL
 */
void *ThreadHandleNodeDecodeData(void)
{
  
        /* udp 变量 */
        int sockfd;
        struct sockaddr_in localaddr;
        struct sockaddr_in fwd_addr;

        uint16_t recv_len = 0;
        int      addrlen;
        addrlen = sizeof(struct sockaddr_in);

        /* 有效数据的长度 */
        int payload_len    = 0;
        int ret;

        uint8_t buffer[1024];
  
        uint8_t senddata[1024];
        int     senddatalen = 0;
       
        /* init */
        memset(senddata,0,1024);
        memset(buffer,  0,1024);

        /* creat socket */
        sockfd = socket(AF_INET,SOCK_DGRAM,0); 

        if (-1 == sockfd ){

                DEBUG_SERVER("sockfd creat fail!\n");

        }

        /* set sockaddr_in parameter */
        memset(&fwd_addr,0,addrlen);

        fwd_addr.sin_family        = AF_INET;
        fwd_addr.sin_addr.s_addr   = inet_addr("127.0.0.1");  
        
        /* 7789：监听lora_pkt_fwd.c上报的解密join macpayload的数据 */
        fwd_addr.sin_port          = htons(7789);

        /* bind */
        ret = bind(sockfd,(struct sockaddr*)(&fwd_addr),addrlen);
        if ( -1 == ret ) {

                DEBUG_SERVER("bind erro!\n");
                close(sockfd);
             
        }

        while(1)
        {
            /* 接收解密后的join macpayload 数据 */
            recv_len = recvfrom(sockfd,buffer,1024,0,(struct sockaddr*)&localaddr,&addrlen);
            if ( recv_len <= 0 ){

                    DEBUG_SERVER("recvfrom error!\n");
                    close(sockfd);
                    continue; 
            
            } else {

                /*
                     消息封装 
                     消息格式参照：<< lorawan 与 gateway 通信协议 >> 
                 */
                senddata[senddatalen++]  = 0xfa;
                senddata[senddatalen++]  = 0x2b;
                
                /* data len */  
                senddata[senddatalen++]  = (uint8_t)( recv_len >> 8);
                senddata[senddatalen++]  = (uint8_t)  recv_len;
                
                /* copy deveui(8bytes) + decode data */
                mymemcpy(senddata + senddatalen,buffer,recv_len);
                senddatalen += recv_len;

                /* mic生成 */
                senddata[senddatalen++]  = checksum(senddata,senddatalen);
                
                /* 数据发生到gateway 数据接收区 */
                ret = sendto(udp_sockfd,senddata,senddatalen,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
                if ( ret  < 0 ){

                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"send decode data is error: %s\n",strerror(errno));
                        close(udp_sockfd);
                        continue;
                }
                
                /* clear */
                memset(senddata,0,1024);  
                memset(buffer,  0,1024);  
                senddatalen = 0;
                recv_len    = 0;

            }

        }

}

/*
    brief:          初始化gwinfo数据库
    parameter in:   数据库地址，错误标志地址
    return      :   null
*/
void SqliteGwInfoTable(sqlite3 *db,char *zErrMsg)
{
    int rc;
    char *sql = NULL;

    #if 0
    sql = "DROP TABLE IF EXISTS GWINFO";
    rc = sqlite3_exec(db,sql,0,0,&zErrMsg);
    if(rc)
    {
        fprintf(stderr,"Can't detele database: %s\n",sqlite3_errmsg(db));
        exit(0);
    }
    else
    {
        fprintf(stderr,"detele gwinfo database successfully\n");            
    }
    #endif
    
    /* 
        创建一个数据库表
        2018.11.9 增加FcntUp,FcntDown字段
    */
    sql = "CREATE TABLE IF NOT EXISTS GWINFO(" \
            "ID INT PRIMARY KEY   NOT  NULL," \
            "APPKey   TEXT                             ,"\
            "DEVEui   TEXT                     NOT NULL,"\
            "Devaddr  TEXT                             ,"\
            "Nwkskey  TEXT                             ,"\
            "APPskey  TEXT                             ,"\
            "APPEui   TEXT                             ,"\
            "FcntUp   INTEGER                          ,"\
            "FcntDown INTEGER                          ,"\  
            "Rx2_Freq INTEGER                            );";

    rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg);
    if(rc != SQLITE_OK)
    {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
    }
    else
    {
        fprintf(stdout," gwinfo table created successfully\n");            
    } 

    /*----------------------------sqlite3 debug--------------------------*/
   
    sql = "SELECT *FROM GWINFO";
                
    /* 执行SQL语句 */
    rc = sqlite3_exec(db,sql,test_callback,0,&zErrMsg);

    if ( rc != SQLITE_OK ) {

            DEBUG_CONF("SQL error: %s\n",zErrMsg);
            sqlite3_free(zErrMsg);

    } else {

            DEBUG_CONF("GWINFO Table watch successfully\n");            
    }
    /*--------------------------------------------------------------------*/

}

/*
    
    brief:          打开数据库
    parameter in:   数据库地址，错误标志地址
    return      :   0： success     -1 :fail 

*/
int Sqlite3_open(const char *filename , sqlite3 **ppDb)
{

        int rc;
        /* 打开数据库：hub.db */
        rc = sqlite3_open(filename , ppDb);

        if ( rc != SQLITE_OK ){

                DEBUG_CONF( "Can't open database: %s\n",sqlite3_errmsg(ppDb) );
                return -1;

        } else {

                DEBUG_CONF( "Open database successfully\n");            
        }

        return 0;
}


/*
    
    brief:          关闭数据库
    parameter in:   数据库地址，错误标志地址
    return      :   0： success     -1 :fail 

*/
int Sqlite3_close(sqlite3 *ppDb)
{
        int rc;
        /* 打开数据库：hub.db */
        rc = sqlite3_close(ppDb);

        if ( rc != SQLITE_OK ){

                DEBUG_CONF( "Can't close database: %s\n",sqlite3_errmsg(ppDb) );
                return -1;

        } else {

                DEBUG_CONF( "close database successfully\n");            
        }

        return 0;

}

/*
   
   update：2019.05.24 

   brief：    应答读远程通信服务器命令
   parameter：需要写入的节点信息
   return：   NULL 

*/
void AckReadCommunicateServer(void)
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

        /* preamble */
        send_buff[0]  = 0xfa;
        /* cmd */
        send_buff[1]  = 0x2f;
        /* len */
        send_buff[2]  = 0x00;
        send_buff[3]  = 0x06;

        /* ip_adddress */
        send_buff[4]  = server_ip_buff[0];
        send_buff[5]  = server_ip_buff[1];
        send_buff[6]  = server_ip_buff[2];
        send_buff[7]  = server_ip_buff[3];
        /* port */
        send_buff[8]  = (uint8_t)(server_port>>8);
        send_buff[9]  = (uint8_t)server_port;
        /* mic */
        send_buff[10] = checksum(send_buff,10);
        /* send */
        fd_server = sendto(udp_sockfd,send_buff,11,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(fd_server  < 0)
        {
                DEBUG_CONF("\nsend ack read server cmd error!\n");
                close(udp_sockfd);
                exit(errno);
        }

        /* 释放json文件指针 */
        json_serialize_to_file(read_val,read_cfg_path); 
        json_value_free(read_val);

}


/*
   
   update：2019.05.24

   brief：    应答写远程通信服务器命令
   parameter：需要写入的节点信息
   return：   NULL 

*/
void AckWriteCommunicateServer(uint8_t *buff)
{
    
        int     fd_server;
        uint8_t send_buff[6];
        uint8_t ip_buff[4];

        char  *read_cfg_path  = "/lorawan/lorawan_hub/server_conf.json";
        char  *conf_obj       = "Communicate_conf";
        char  *ip_name        = "communicate_ip"; 
        char  *ip_port        = "communicate_port";

        /* init */
        memset(send_buff,0,6);
        memset(ip_buff,0,4);

        uint16_t  udp_ip_port;
        char     *server_ip = malloc(100);

        /* json文件初始化 */
        JSON_Value  *read_val;
        JSON_Object *read       = NULL;
        JSON_Object *read_conf  = NULL;
        /* 接收pc端数据 */
        ip_buff[0] = *(buff+4);
        ip_buff[1] = *(buff+5);
        ip_buff[2] = *(buff+6);
        ip_buff[3] = *(buff+7);
        udp_ip_port    = (*(buff+8)) << 8;
        udp_ip_port   |= (*(buff+9));
        sprintf(server_ip,"%d.%d.%d.%d",ip_buff[0],ip_buff[1],ip_buff[2],ip_buff[3]);
        
        /* Debug */
        for(int i=0; i<4;i++)
        {
                DEBUG_CONF("ip_buff[%d]:0x%x\n",i,ip_buff[i]);
        }
        DEBUG_CONF("ip_port:%d\n",udp_ip_port);

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
                json_object_dotset_number(read_conf,ip_port,udp_ip_port);  
        }

        /* preamble */
        send_buff[0] = 0xfa;
        /* cmd */
        send_buff[1] = 0x31;
        /* len */
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        /* data */
        send_buff[4] = 0x01;
        /* mic  */
        send_buff[5] = checksum(send_buff,5);
        fd_server = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(fd_server  < 0)
        {
                DEBUG_CONF("\nsend ack write server cmd error!\n");
                close(udp_sockfd);
                exit(errno);
        }
        
        /* 
                重新配置 通信服务器之后，
               
                重启lora_pkt_server进程
        */
        kill(pid_server,SIGKILL);
        /*  回收子进程，避免僵尸进程 */
        signal(SIGCHLD,sig_child); 
        /*  重新启动解析进程 */
        if ( ( pid_server = fork()) < 0)
        {
                DEBUG_CONF("fork error");
        }
        else if ( pid_server == 0)
        {
                /* 重启解析进程 */    
                if( execl("/lorawan/lorawan_hub/lora_pkt_server", "lora_pkt_server",NULL,NULL,(char*)0) == -1 )
                {
                        perror("execle error ");
                        exit(1);
                }
        
        }

        /*  释放堆区内存 */
        free(server_ip);
        /* 释放json指针 */
        json_serialize_to_file(read_val,read_cfg_path); 
        json_value_free(read_val);

}