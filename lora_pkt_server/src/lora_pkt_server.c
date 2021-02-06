/*
    ____               _    _      _         _
  / ____|             | |  | |    (_)       | |
 | (___    ___  _ __  | |_ | |__   _  _ __  | | __
  \___ \  / _ \|  _ \ | __||  _ \ | ||  _ \ | |/ /
  ____) ||  __/| | | || |_ | | | || || | | ||   <
 |_____/  \___||_| |_| \__||_| |_||_||_| |_||_|\_\


Description:
            
            与服务器通信的进程


Transport Protocols: UDP传输协议、TCP传输协议、LoRaWAN-HUB与服务器通信协议
Lierda |  Senthink
autor:  jianjun_xia              
data :  2018.10.10

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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/errno.h>
#include <errno.h>
#include <sys/time.h>
#include <sqlite3.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <linux/if.h>
#include "lora_pkt_server.h"
#include "common.h"             /* 常用函数头文件      */
#include "parson.h"             /* json解析头文件     */
#include "message_management.h" /* pc端配置功能头文件 */
#include "md5.h"                /* md5加密文件      */

/*------------------------------------------Senthink服务器定义变量---------------------------------------*/

/* 全局套接字描述符 */
int    tcp_sock_fd;
int    tcp_send_len;

/* join 数据消息序号 */
static uint16_t  join_message_id  = 0;
/* confirm/unconfirm 数据消息序列号 */
static uint16_t  node_message_id  = 0;
/* 心跳 数据消息序列号 */
static uint16_t  heart_message_id = 0;
/* 心跳消息序列 */
static uint16_t  heart_beat_id    = 0;
/* 鉴权消息序列 */
static uint16_t  auth_message_id  = 0;
/* 鉴权成功标识 */
static uint16_t  Auth_Successful_Flag;
/* socket 断开标识 */
static uint32_t  closeSocket_Bymyself;
/* 心跳与数据指令的间隔值 */
static  time_t   Heart_Cmd_Interval;


/* TCP 重连保护 */
static pthread_mutex_t   mx_tcp_reconnect            = PTHREAD_MUTEX_INITIALIZER;

/* 标识自己关闭了socket  */
static pthread_rwlock_t  rw_close_socket_flag        = PTHREAD_RWLOCK_INITIALIZER;

/* 鉴权读写锁保护 */
static pthread_rwlock_t  rw__auth_success_flag        = PTHREAD_RWLOCK_INITIALIZER;

/* 用于维护 心跳与数据指令间隔的时间值: 读写锁 */
static  pthread_rwlock_t rw_heart_command_time        = PTHREAD_RWLOCK_INITIALIZER;

/* 用于写入confirmed data的数据保护: 读写锁 */
static  pthread_rwlock_t rw_confirmed_data            = PTHREAD_RWLOCK_INITIALIZER;

/* 线程ID：负责TCP连接 */
pthread_t   thread_tcp_connect;
/* 线程ID：负责发送join data */ 
pthread_t   thread_send_join_data;
/* 线程ID：负责发送confirm/unconfirm数据 */
pthread_t   thread_send_node_data;
/* 线程ID：接收服务器回复的数据 */
pthread_t   thread_recv_data;
/* 线程ID：发送心跳线程 */
pthread_t   thread_send_heart;

/* 数据通信进程id */
pid_t       pid_communicate;    

/*------------------------------------------------------------------------------------------------------------*/


/*------------------------------------------------------------------------------------------------------------*/

/* 数据库变量 */
sqlite3 *db;
char    *zErrMsg =NULL;
int     rc;
char    *sql = NULL;
const char    *data= "Callback function called";

/* 接收数据缓存 */
uint8_t tcp_recv_buff[1000];
int     tcp_recv_len;
/* 取鉴权码的长度 */
int fetch_buff_len;

/* 硬件版本和固件版本缓存区 */
static uint8_t hardware_buff[30] = 
{
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00
};

static uint8_t firmware_buff[30] = 
{
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00
};

/* 
        存储上报的confirmed data 的freq_hz 和 datarate数值，
        用于计算下行数据 是否分包 
*/
static uint8_t Confirmed_Data[5] = 
{
     0x00,0x00,0x00,0x00,0x00,0x00
};

/* 处理杀死后的子进程，避免产生僵尸进程 */
static void sig_child(int signo)
{
    pid_t pid;
    int stat;

    /* 回收子进程 */
    while(pid = waitpid(-1,&stat,WNOHANG) > 0)
    {
         DEBUG_CONF("\n/-----------------------------------------------lora_pkt_server child %d terminated.------------------------------/\n",pid);
    }

}

int 
main(int argc, char const *argv[])
{
    int err;
    int loop_flag = 1;
    int ack_regester_data;
    const  char *database = "/lorawan/lorawan_hub/hub.db";

    /* 打开数据库 */    
    S_sqlite3_open(database, &db);
    
    /* 注册信息初始化 */
    HUB_Register_Init();
  
    /*线程函数：创建TCP握手连接 */
    err = pthread_create(&thread_tcp_connect,NULL,Thread_Tcp_Connect,NULL);
    if(err != 0){
            DEBUG_CONF("can't create tcp connect pthread!\n ");
    }else{
            DEBUG_CONF("tcp connect pthread create successfully\n");
    }
    /* 延时5s，先调用tcp连接线程 */
    sleep(5);
    while(loop_flag)
    {   
            /* 
                上电先进行注册处理，等待服务器回复。
                1：如果服务器回复0x00，则注册成功将注册码存储到数据库中，然后检测数据库信息
                2：如果服务器回复0x01，则表示已经在该服务器上注册过，然后去检测数据库信息
                3：如果服务器回复0x02,则表示服务器端无该HUB信息。
            */


#ifdef   HUB_Server_Communication_Version_v_1_0_3
           
        /* 无需注册 */   
        ack_regester_data = 0x00;

#else

           /* HUB信息注册 */
            Hub_Register();
            /* 应答注册处理 */
            ack_regester_data =  Server_Ack_Hub_Register();      
            DEBUG_CONF("ack_regester_data:0x%02x\n",ack_regester_data);        
#endif
 
            if( (Register_Successful == ack_regester_data) || (Register_Info_Exist == ack_regester_data) )
            { 
                    
                    
        #ifdef   HUB_Server_Communication_Version_v_1_0_3    

                  /* 直接进行鉴权 */   
                 if (1)
                 {  

        #else 
                 /* 检查是否完成注册 */   
                 if ( 1 == Check_Hub_Register())
                 {  

        #endif
                            pthread_rwlock_rdlock(&rw__auth_success_flag);                          
                            /* 注册完成后检查是否成功鉴权 */
                            if(1 == Auth_Successful_Flag)
                            {
                                    
                                    /* 解锁 */
                                    pthread_rwlock_unlock(&rw__auth_success_flag);   
                                    
                                    /* 循环变量置为0 */
                                    loop_flag = 0; 

                                    /* 启动监听管理命令的线程 */
                                    err = pthread_create(&thread_recv_data,NULL,Thread_Recv_ServerData,NULL);
                                    if(err != 0){
                                            DEBUG_CONF("can't create recv data info pthread!\n ");       
                                    }else{
                                            DEBUG_CONF("recv data info pthread create successfully\n");        
                                    }

                                    /* 启动发送心跳线程 */
                                    err = pthread_create(&thread_send_heart,NULL,Thread_Hub_Send_Heartbeat,NULL);
                                    if(err != 0){
                                            DEBUG_CONF("can't create send node data info pthread!\n ");
                                    }else{
                                            DEBUG_CONF("send node info pthread create successfully\n");
                                    }
           
                                     /* 鉴权成功，启动数据通信进程 */
                                     if ( (pid_communicate = fork()) < 0)
                                     {
                                                DEBUG_CONF("Create communicate process error!\n");
                                        
                                     }else if ( pid_communicate == 0)
                                     {
                                                /* 调用与服务器通信进程 */      
                                                if ( execl("/lorawan/lorawan_hub/restart_lora_pkt_communicate.sh", "restart_lora_pkt_communicate.sh",NULL,NULL,(char*)0) == -1 ){
                                                        perror("execle error ");
                                                        exit(1);
                                                }
                                        
                                      }

                                    /*  
                                        update:2019.05.24
                                        数据通信部分放入独立进程 
                                    */
                                    
                                    #if 0            
                                                /* 启动上报join数据的线程 */
                                                err = pthread_create(&thread_send_join_data,NULL,Thread_Send_JoinData,NULL);
                                                if(err != 0){
                                                        DEBUG_CONF("can't create send join data info pthread!\n ");
                                                } else{
                                                        DEBUG_CONF("send join info pthread create successfully\n");
                                                }

                                                /* 启动上报unconfirmed/confirmed数据的线程 */ 
                                                err = pthread_create(&thread_send_node_data,NULL,Thread_Send_NodeData,NULL);
                                                if(err != 0){
                                                        DEBUG_CONF("can't create send node data info pthread!\n ");
                                                }else{
                                                        DEBUG_CONF("send node info pthread create successfully\n");
                                                }
                                    #endif    



                            }
                            else
                            {
                                        /* 解锁 */
                                        pthread_rwlock_unlock(&rw__auth_success_flag);   
                                        
                                        /* 注册成功后，延时1s开始鉴权 */    
                                        sleep(1); 
                                    
                                        for ( int i = 0; i < 3; i++)
                                        {
                                                /* 发送鉴权 */
                                                HUB_Authentication();
                                                DEBUG_CONF("\ni'm authentication..\n");
                                        
                                                /* 鉴权失败 */
                                                if ( 1 ==  HUB_Authentication_Ack()){

                                                        i = 0;

                                                } else { /* 鉴权成功 */

                                                                i = 4;
                                                }

                                        }
                                    
                            }

                    }
                    else
                    {
                            DEBUG_CONF("/--------------------------Registration failed and will be re-registered 5 minutes later!----------------------------------/\n");                        
                            sleep(300);
                    }

            } /* 服务器没有该hub的信息 */
            else if (Server_No_Hub_Info == ack_regester_data)
            {
                    DEBUG_CONF("Server no this HUB info.Please check it!.\n");
            }
            else /*睡眠一段时间后重新尝试注册 */
            {
                    DEBUG_CONF("/--------------------------Registration failed and will be re-registered 5 minutes later!----------------------------------/\n");                        
                    sleep(300);
            }

    } 

    /* 等待tcp连接的线程结束 */
    pthread_join(thread_tcp_connect,NULL);
    /* 等待接收数据的线程结束 */
    pthread_join(thread_recv_data,NULL);
    /* 等待发送心跳线程结束 */
    pthread_join(thread_send_heart,NULL);

    /*  update:2019.05.24
        数据通信部分放入独立进程 
    */
    #if 0    
        /* 等待发送join数据的线程结束 */
        pthread_join(thread_send_join_data,NULL);
        /* 等待发送confirm/unconfirm数据的线程结束 */
        pthread_join(thread_send_node_data,NULL);
    #endif    
    
    return 0;

}
/*---------------------------------------------------------------------------------------------------------------------------------*/





/*----------------------------------------------------------------函数实现区域----------------------------------------------------------------*/

/**
 * brief: 线程函数:使用TCP进行与后台服务器连接
 * 
 * parameter : 接收数据首地址，接收数据的长度
 * 
 * return : NULL 
 * 
 */
void *Thread_Tcp_Connect(void)
{
    struct  sockaddr_in tcp_server_addr;
    int     tcp_ret;

    char *read_cfg_path = "/lorawan/lorawan_hub/server_conf.json";
    char conf_obj[]     = "Server_conf";  
    char  *ip_name      = "server_ip"; 
    char  *ip_port      = "server_port";
    
    /* 读取的服务器地址 */ 
    const char  *server_address = NULL;
    /* 读取的服务器端口 */    
    uint32_t server_port;
    
    JSON_Value  *read_val;
    JSON_Object *read       = NULL;
    JSON_Object *read_conf  = NULL;
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
    DEBUG_CONF("server_port: %d\n",server_port);
    
    /*
        建立与Senthink服务器的TCP链接
        socket-->connect-->send-->close
    */
    tcp_sock_fd = socket(AF_INET,SOCK_STREAM,0); // ipv4,tcp
    if(-1 == tcp_sock_fd)
    {
        DEBUG_CONF(" %d\n",errno);
        fprintf(stderr,"socket error: %s\n",strerror(errno));
    }

    #if 0 
    /* 若不进行绑定网卡的操作，则会导致，关闭全局变量socket后，再次绑定到相同地址，该socket不可用 */
    tcp_ret = setsockopt(tcp_sock_fd,SOL_SOCKET,SO_LINGER,(char*)&DontLinger, sizeof (bool) );
    if(tcp_ret != 0)
    {         
        DEBUG_CONF(" %d\n",errno);
        fprintf(stderr,"setsocketopt reuseaddr fail: %s\n",strerror(errno));
    }   
    #endif

    /* set sockaddr_in parameter */
    memset(&tcp_server_addr, 0, sizeof(struct sockaddr_in));//clear
    tcp_server_addr.sin_family = AF_INET;
    tcp_server_addr.sin_port = htons(server_port);
    tcp_ret = inet_aton(server_address,&tcp_server_addr.sin_addr);
    if(0 == tcp_ret)
    {
        fprintf(stderr,"server_ip error.\n");
        close(tcp_sock_fd);
    }
    
    /*  等待握手成功 */

    pthread_rwlock_wrlock(&rw_close_socket_flag); 

    for( int  i = 0; i < 3; i++)
    {

                tcp_ret = connect(tcp_sock_fd,(const struct sockaddr*)&tcp_server_addr,sizeof(struct sockaddr));
                if(-1 == tcp_ret)
                {
                         DEBUG_CONF("\n/-----------------------------Connect is fail!...Retry the connection after 30 seconds---------------------------/\n");      
                         
                         DEBUG_CONF(" %d\n",errno);    
                         fprintf(stderr,"connect error:%s\n",strerror(errno));
                        // ReconnectWith_Server(); 
                        
                        /* 置为 0 */
                        i = 0;

                        /* 每30s 尝试重新握手 */
                        sleep(30);
                
                }
                else
                {
                        DEBUG_CONF("\n/-----------------------------Connect is successful!---------------------------/\n");   
                        /* 跳出循环 */
                        i = 4;
                }   

    }
     
     pthread_rwlock_unlock(&rw_close_socket_flag);
       
    /* 每次操作JSON文件后需序列化文件，并释放指针 */
    json_serialize_to_file(read_val,read_cfg_path); 
    json_value_free(read_val); 

}

/**
 * brief:      线程函数：上报join数据
 * 
 * parameter : 接收数据首地址，接收数据的长度
 * 
 * return : NULL 
 * 
 */
void *Thread_Send_JoinData(void)
{
    /* code */
   int sockfd;
   struct sockaddr_in localaddr;
   struct sockaddr_in fwd_addr;
   int addrlen;
   socklen_t recv_len = 0;
   int ret;
   uint8_t buffer[BUFF_LEN];
   memset(buffer,0,BUFF_LEN);
   addrlen = sizeof(struct sockaddr_in);
   int Reusraddr    = 1;

    /* 缓存数组 */
    uint8_t join_data[JOIN_DATA_LEN];
    memset(join_data,0,JOIN_DATA_LEN);
    int data_len = 0;

    /* 发送封装、转义后的缓存数组 */
    uint8_t send_data[1000];
    memset(send_data,0,1000);
    int send_data_len = 0;
   
    /* GWinfo */
    int fd_gw;
    int size_gw;
    uint8_t *gwinfo_string = (uint8_t*)malloc(8);
    uint8_t  gwinfo[8];
    memset(gwinfo,0,8);

    /* 读入网关信息的文件 */
    fd_gw = open("/lorawan/lorawan_hub/gwinfo",O_RDWR);
    if(-1 == fd_gw)
    {
        DEBUG_CONF("sorry,There is no gwinfo file in this directory,Please check it!\n");            
        close(fd_gw);
    }
    size_gw = read(fd_gw,gwinfo_string,16);
    close(fd_gw);

    /* ascii to hex */
    String_To_ChArray(gwinfo,gwinfo_string,8);

    /* creat socket ipv4 udp */
    sockfd = socket(AF_INET,SOCK_DGRAM,0); 
    if(-1 == sockfd){
                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"socket creat error: %s\n",strerror(errno));
    }
    
    /* set REUSEADDR properties address reuse */
    ret = setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&Reusraddr,sizeof(int));
    if(ret != 0)
    {
              DEBUG_CONF(" %d\n",errno);
               fprintf(stderr,"send error: %s\n",strerror(errno));
    }

    /* clear */
    memset(&fwd_addr,0,addrlen);
    /* set sockaddr_in parameter */
    fwd_addr.sin_family        = AF_INET;
    fwd_addr.sin_addr.s_addr   = inet_addr(LOCAL_IP);
    /* 5555:监听上报join data的端口 */
    fwd_addr.sin_port          = htons(JOIN_PORT);


    /* bind */
    ret = bind(sockfd,(struct sockaddr*)(&fwd_addr),addrlen);
    if(-1 == ret)
    {
              DEBUG_SERVER(" %d\n",errno);
              fprintf(stderr,"bind error: %s\n",strerror(errno));
    }
    while(1)
    {
            if( ( recv_len = recvfrom(sockfd,buffer,1024,0,(struct sockaddr*)&localaddr,&addrlen)) <= 0)
            {
                        DEBUG_SERVER(" %d\n",errno);
                        fprintf(stderr,"recvfrom error: %s\n",strerror(errno));
            }
            else
            {
                    DEBUG_SERVER("Get message from pkt_fwd: %s\n",inet_ntoa(localaddr.sin_addr));
                    for(int i=0; i<recv_len; i++)
                            DEBUG_SERVER("recv data[%d]: 0x%02x\n",i,buffer[i]);    
                    /*
                        参考LoRaWAN-HUB与服务器通信协议
                        大端传输
                        进行数据封装，上报至服务器
                         ---------------------------------
                        | 消息头  | 消息体 | 校验码 |  后导码 |  
                         ---------------------------------
                         ----------------------------------------------------------
                        | 消息版本 | 消息类型 | HUB编号 | 消息序号 | 消息体属性 | 分包属性 |
                         ---------------------------------------------------------- 
                    */
                    
                    /* 消息版本 */
                    join_data[data_len++] = 0x01;
                    /* 消息类型 */
                    join_data[data_len++] = 0x02;
                    join_data[data_len++] = 0x01;
                    /* HUB唯一编号 */
                    mymemcpy(join_data+data_len,gwinfo,8);
                    data_len+=8;
                    join_message_id++;
                    if(65535 ==join_message_id)
                    {
                        join_message_id = 0;
                    }
                    /* 消息序号 */
                    join_data[data_len++] = join_message_id>>8;
                    join_data[data_len++] = join_message_id;
                    /*
                        消息体属性
                        -----------------------------------------------
                        |15 14| 13 | 12 11  10  | 9 8 7 6 5 4 3 2 1 0  |    
                        |----------------------------------------------
                        | 保留 |分包| 消息体加密方式| 消息体长度            |
                        |----------------------------------------------
                        join data 消息体长度为52
                    */
                    join_data[data_len++] = 0x00;
                    join_data[data_len++] = 0x34;
                    
                    /*
                        消息体
                         ---------------------------------------------
                        | 消息体类型          |   消息体数据             |      
                         --------------------------------------------- 
                        消息体类型
                    */

                    /* Tag */
                    join_data[data_len++] = 0x01;
                    /* Type */
                    join_data[data_len++] = 0x02;
                    /* value */
                    join_data[data_len++] = 0x01;
                    
                    /* 
                        节点join
                         -------------------------------------------------------------------------------------------
                        | ifChain | rfChain | freq | datarate | bandwidth | coderate | rssi | snr | AppEui | DevEui | 
                         -------------------------------------------------------------------------------------------
                    */
                    
                    /* IfChain */
                    /* Tag */
                    join_data[data_len++] = 0x01;
                    /* Type */
                    join_data[data_len++] = 0x02;
                    /* Value */
                    join_data[data_len++] = buffer[0];
                    
                    /* RfChain */
                    /* Tag     */
                    join_data[data_len++] = 0x02;
                    /* Type    */
                    join_data[data_len++] = 0x02;
                    /* Value   */
                    join_data[data_len++] = buffer[1];
                    
                    /* Freq */
                    /* Tag  */
                    join_data[data_len++] = 0x03;
                    /* Type */
                    join_data[data_len++] = 0x06;
                    /* Value */
                    join_data[data_len++] = buffer[2];
                    join_data[data_len++] = buffer[3];
                    join_data[data_len++] = buffer[4];
                    join_data[data_len++] = buffer[5];
                    
                    /* Datarate */
                    /* Tag */
                    join_data[data_len++] = 0x04;
                    /* Type */
                    join_data[data_len++] = 0x02;
                    /* Value */
                    join_data[data_len++] = buffer[6];
                    
                    /* Bandwidth */
                    /* Tag */
                    join_data[data_len++] = 0x05;
                    /* Type */
                    join_data[data_len++] = 0x02;
                    /* Value */
                    join_data[data_len++] = buffer[7];
                    
                    /* Coderate */
                    /* Tag */
                    join_data[data_len++] = 0x06;
                    /* Type */
                    join_data[data_len++] = 0x02;
                    /* Value */
                    join_data[data_len++] = buffer[8];
                    
                    /* Rssi */
                    /* Tag  */
                    join_data[data_len++] = 0x07;
                    /* Type */
                    join_data[data_len++] = 0x05;
                    /* Value */
                    join_data[data_len++] = buffer[9];
                    join_data[data_len++] = buffer[10];
                    
                    /* Snr */
                    /* Tag */
                    join_data[data_len++] = 0x08;
                    /* Type */
                    join_data[data_len++] = 0x05;
                    /* Value */
                    join_data[data_len++] = buffer[11];
                    join_data[data_len++] = buffer[12];
                    
                    /* AppEui */
                    /* Tag */
                    join_data[data_len++] = 0x09;
                    /* Type */
                    join_data[data_len++] = 0x0a;
                    /* Value */
                    mymemcpy(join_data+data_len,buffer+13,8);
                    data_len+=8;

                    /* DevEui */
                    /* Tag */
                    join_data[data_len++] = 0x0a;
                    /* Type */
                    join_data[data_len++] = 0x0a;
                    /* Value */
                    mymemcpy(join_data+data_len,buffer+21,8);
                    data_len+=8;
                    
                    /* 计算校验码 */
                    join_data[data_len] = Check_Xor(join_data,data_len); 
                    /* 转义处理 */
                    data_len++;
                    send_data_len  = Transfer_Mean(send_data,join_data,data_len);
                    send_data[send_data_len]   = 0x7e;
                    send_data_len += 1;

                    /* 保证同一时刻只有一个线程操作该套接字 */
                    pthread_rwlock_wrlock(&rw_close_socket_flag);       
                    /* 进行数据发送 */
                    tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                    
                    if(tcp_send_len <= 0)
                    {
                                /* 解锁 */
                                pthread_rwlock_unlock(&rw_close_socket_flag);
                                DEBUG_CONF(" %d\n",errno);
                                fprintf(stderr,"send error: %s\n",strerror(errno));
                                ReconnectWith_Server(); 

                    }
                    else
                    {
                                /* 数据发送成功后，写值到临界区 */
                                pthread_rwlock_wrlock(&rw_heart_command_time);
                                Heart_Cmd_Interval = time((time_t *)NULL);
                                pthread_rwlock_unlock(&rw_heart_command_time);
                                DEBUG_CONF("Heart_Cmd_Interval: %u\n",Heart_Cmd_Interval);  
                                /* 解锁 */
                                pthread_rwlock_unlock(&rw_close_socket_flag);
                                /* 清除缓存区 */
                                memset(buffer,0,1000);
                                memset(join_data,0,300);
                                memset(send_data,0,1000);
                                data_len = 0;
                                send_data_len = 0;   
                    }
        
        }
    
    }    
}


/**
 * brief:      线程函数：上报confirm/unconfirm数据
 * 
 * parameter : 接收数据首地址，接收数据的长度
 * 
 * return : NULL 
 * 
 */
 /*
    备注：
        node一包数据最多上报的字节数为512
        向服务器上报数据时不用考虑分包属性
 */

void *Thread_Send_NodeData(void)
{

   int sockfd;
   struct sockaddr_in localaddr;
   struct sockaddr_in fwd_addr;
   int addrlen;
   socklen_t recv_len = 0;
   
   /* 有效数据的长度 */
   int payload_len    = 0;
   int ret;
   uint8_t buffer[1000];
   memset(buffer,0,1000);
   addrlen = sizeof(struct sockaddr_in);
   int Reusraddr    = 1;

   /* 上报的join data缓存数组 */
    uint8_t join_data[1000];
    memset(join_data,0,1000);
    int data_len = 0;

    /* 发送封装、转义后的缓存数组 */
    uint8_t send_data[1000];
    memset(send_data,0,1000);
    int send_data_len = 0;
    uint16_t message_length;

    /* GWinfo */
    int fd_gw;
    int size_gw;
    uint8_t *gwinfo_string = (uint8_t*)malloc(100);
    uint8_t  gwinfo[8];
    memset(gwinfo,0,8);

    /* 读入网关信息的文件 */
    fd_gw = open("/lorawan/lorawan_hub/gwinfo",O_RDWR);
    if(-1 == fd_gw)
    {
        DEBUG_CONF("sorry,There is no gwinfo file in this directory,Please check it!\n");            
        close(fd_gw);
    }
    size_gw = read(fd_gw,gwinfo_string,16);
    close(fd_gw);
    /* ascii to hex */
    String_To_ChArray(gwinfo,gwinfo_string,8);

   /* creat socket */
   sockfd = socket(AF_INET,SOCK_DGRAM,0); /* ipv4,udp */ 
   if(-1 == sockfd)
   {
        DEBUG_SERVER("sockfd creat fail!\n");
   }
       
   /* set REUSEADDR properties address reuse */
   ret = setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&Reusraddr,sizeof(int));
   if(ret != 0)
   {
                DEBUG_SERVER("setsocketopt reuseaddr fail,ret:%d,error:%d\n",ret,errno);
                fprintf(stderr,"send error: %s\n",strerror(errno));
                close(sockfd);
    }

    /* clear */
    memset(&fwd_addr,0,addrlen);
    /* set sockaddr_in parameter */
    fwd_addr.sin_family        = AF_INET;
    fwd_addr.sin_addr.s_addr   = inet_addr(LOCAL_IP);
    fwd_addr.sin_port          = htons(5556);//5556:监听上报confirm/unconfirm的数据


    /* bind  */
    ret = bind(sockfd,(struct sockaddr*)(&fwd_addr),addrlen);
    if(-1 == ret)
    {
                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"bind error: %s\n",strerror(errno));
                close(sockfd);
    }
    while(1)
    {   
            recv_len = recvfrom(sockfd,buffer,1000,0,(struct sockaddr*)&localaddr,&addrlen);
            if(recv_len <= 0)
            {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"recvfrom error: %s\n",strerror(errno));
                        close(sockfd);
            }
            else
            {
                    DEBUG_SERVER("Get message from pkt_fwd: %s\n",inet_ntoa(localaddr.sin_addr));
                    /*
                        参考LoRaWAN-HUB与服务器通信协议
                        大端传输
                        进行数据封装，上报至服务器
                         ---------------------------------
                        | 消息头  | 消息体 | 校验码 | 后导码 ｜ 
                         ---------------------------------
                         ----------------------------------------------------------
                        | 消息版本 | 消息类型 | HUB编号 | 消息序号 | 消息体属性 | 分包属性 |
                         ---------------------------------------------------------- 
                    */

                    /* 消息版本 */
                    join_data[data_len++] = 0x01;
                    /* 消息类型 */
                    join_data[data_len++] = 0x02;
                    join_data[data_len++] = 0x01;
                    /* HUB唯一编号 */
                    mymemcpy(join_data+data_len,gwinfo,8);
                    data_len+=8;
                    node_message_id++;
                    if(65535 == node_message_id)
                    {
                            node_message_id = 0;
                    }
                    /* 消息序号 */
                    join_data[data_len++] = node_message_id>>8;
                    join_data[data_len++] = node_message_id;
                    /*
                        消息体属性
                        -----------------------------------------------
                        |15 14| 13 | 12 11  10  | 9 8 7 6 5 4 3 2 1 0  |    
                        |----------------------------------------------
                        | 保留 |分包| 消息体加密方式| 消息体长度            |
                        |----------------------------------------------
                        join data 消息体长度为 53 +2 + 载荷长度 -1(1:为数据类型，最后一个字节)
                    */
                    message_length = 55 + recv_len -29 -1;
                    join_data[data_len++] = message_length >>8;
                    join_data[data_len++] = message_length;
                    
                    /*
                        消息体
                         ---------------------------------------------
                        | 消息体类型          |   消息体数据             |      
                         --------------------------------------------- 
                    */  
                    
                    /*  消息体类型 */
                    /*  Tag */
                    join_data[data_len++] = 0x01;
                    /*  Type */
                    join_data[data_len++] = 0x02;

                    /*  value 
                        需判断是unconfirmed类型还是confirmed类型
                        udp本地传输最后一个字节为上报服务器的数据类型:unconfirmed/confirmed
                    */

                    /* confirmed */
                    if( buffer[recv_len-1] == 0x04)
                    {
                                /* 将confirmed 数据 缓存到本地，后面下发时进行分包位判断 */               
                                pthread_rwlock_wrlock(&rw_confirmed_data); 
                                
                                /* 
                                        根据上报的datarate 对应相应的DR值
                                        定义在lora_gateway/hal.h中

                                        0x40 --> sf12 --> dr0
                                        0x20 --> sf11 --> dr1  
                                        0x10 --> sf10 --> dr2 
                                        0x08 --> sf9  --> dr3 
                                        0x04 --> sf8  --> dr4 
                                        0x02 --> sf7  --> dr5 

                                */
                                mymemcpy(Confirmed_Data,buffer+2,5);
                                
                                switch (Confirmed_Data[4]){
                                
                                case 0x40: Confirmed_Data[4] = 0; break;
                                case 0x20: Confirmed_Data[4] = 1; break;
                                case 0x10: Confirmed_Data[4] = 2; break;
                                case 0x08: Confirmed_Data[4] = 3; break;
                                case 0x04: Confirmed_Data[4] = 4; break;
                                case 0x02: Confirmed_Data[4] = 5; break;             
                                              
                                default: DEBUG_CONF("dr error!\n");
                                        break;
                              }
                                
                                pthread_rwlock_unlock(&rw_confirmed_data);              
                                join_data[data_len++] = 0x03;

                    }
                    else if(buffer[recv_len-1] == 0x02)
                    {
                                 join_data[data_len++] = 0x02;
                    }
                    else
                    {
                                DEBUG_CONF(" type error!\n");
                    }

                    /*
                        节点join
                         -------------------------------------------------------------------------------------------
                        | ifChain | rfChain | freq | datarate | bandwidth | coderate | rssi | snr | AppEui | DevEui | 
                         -------------------------------------------------------------------------------------------
                    */

                    /* IfChain */
                    /* Tag  */
                    join_data[data_len++] = 0x01;
                    /* Type */
                    join_data[data_len++] = 0x02;
                    /* Value */
                    join_data[data_len++] = buffer[0];
                    
                    /* RfChain */
                    /* Tag   */
                    join_data[data_len++] = 0x02;
                    /* Type  */
                    join_data[data_len++] = 0x02;
                    /* Value */
                    join_data[data_len++] = buffer[1];
                    
                    /* Freq  */
                    /* Tag   */
                    join_data[data_len++] = 0x03;
                    /* Type  */
                    join_data[data_len++] = 0x06;
                    /* Value */
                    join_data[data_len++] = buffer[2];
                    join_data[data_len++] = buffer[3];
                    join_data[data_len++] = buffer[4];
                    join_data[data_len++] = buffer[5];
                    
                    /* Datarate */
                    /* Tag     */
                    join_data[data_len++] = 0x04;
                    /* Type   */
                    join_data[data_len++] = 0x02;
                    /* Value  */
                    join_data[data_len++] = buffer[6];
                    
                    /* Bandwidth */
                    /* Tag  */
                    join_data[data_len++] = 0x05;
                    /* Type */
                    join_data[data_len++] = 0x02;
                    /* Value */
                    join_data[data_len++] = buffer[7];
                    
                    /* Coderate */
                    /* Tag */
                    join_data[data_len++] = 0x06;
                    /* Type */
                    join_data[data_len++] = 0x02;
                    /* Value */
                    join_data[data_len++] = buffer[8];
                    
                    /* Rssi  */
                    /* Tag   */
                    join_data[data_len++] = 0x07;
                    /* Type  */
                    join_data[data_len++] = 0x05;
                    /* Value */
                    join_data[data_len++] = buffer[9];
                    join_data[data_len++] = buffer[10];
                    
                    /* Snr   */
                    /* Tag   */
                    join_data[data_len++] = 0x08;
                    /* Type  */
                    join_data[data_len++] = 0x05;
                    /* Value */
                    join_data[data_len++] = buffer[11];
                    join_data[data_len++] = buffer[12];
                    
                    /* AppEui */
                    /* Tag    */
                    join_data[data_len++] = 0x09;
                    /* Type   */
                    join_data[data_len++] = 0x0a;
                    /* Value  */
                    mymemcpy(join_data+data_len,buffer+13,8);
                    data_len+=8;
                    
                    /* DevEui */
                    /* Tag    */
                    join_data[data_len++] = 0x0a;
                    /* Type   */
                    join_data[data_len++] = 0x0a;
                    /* Value  */
                    mymemcpy(join_data+data_len,buffer+21,8);
                    data_len+=8;

                    /* payload的长度 = 接收到的数据长度 - 需固定处理的数据长度 -1 (1:为数据类型，最后一个字节) */
                    payload_len  = recv_len - 29 -1;
                    
                    /* Payload */
                    /* Tag */
                    join_data[data_len++] = 0x0b;
                    /* Type */
                    join_data[data_len++] = 0x0c;
                    /* Length */
                    join_data[data_len++] = payload_len;
                    /* Value */
                    mymemcpy(join_data+data_len,buffer+29,payload_len);
                    data_len+=payload_len;
                    /* 计算校验码  */
                    join_data[data_len] = Check_Xor(join_data,data_len); 
                    data_len++;
                    /* 转义处理 */
                    send_data_len  = Transfer_Mean(send_data,join_data,data_len);
                    send_data[send_data_len]   = 0x7e;
                    send_data_len += 1;  

                     /* 保证同一时刻只有一个线程操作该套接字 */
                    pthread_rwlock_wrlock(&rw_close_socket_flag);   
                    
                    /* 进行数据发送 */
                    tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
                    
                    if(tcp_send_len <= 0)
                    {
                                /* 解锁 */
                                pthread_rwlock_unlock(&rw_close_socket_flag);
                                DEBUG_CONF(" %d\n",errno);
                                fprintf(stderr,"send error: %s\n",strerror(errno));
                                ReconnectWith_Server(); 
                    }
                    else
                    {
                                /* 数据发送成功后，写值到临界区 */
                                pthread_rwlock_wrlock(&rw_heart_command_time);
                                Heart_Cmd_Interval = time((time_t *)NULL);
                                pthread_rwlock_unlock(&rw_heart_command_time);
                                DEBUG_CONF("Heart_Cmd_Interval: %u\n",Heart_Cmd_Interval);  
                                
                                /* 解锁 */
                                pthread_rwlock_unlock(&rw_close_socket_flag);
                                /* 清除缓存区 */
                                memset(buffer,0,1000);
                                memset(join_data,0,300);
                                memset(send_data,0,1000);
                                data_len = 0;
                                send_data_len = 0;  
                    }

            }

    }

}


/**
 * brief:      线程函数：接收服务器回复的应答数据
 * 
 * parameter : 接收数据首地址，接收数据的长度
 * 
 * return : NULL 
 * 
 */
void *Thread_Recv_ServerData(void)
{
    uint8_t mic = 0;
    uint8_t recv_buff[1024];
    memset(recv_buff,0,1024);

    uint8_t message_buff[1024];
    memset(message_buff,0,1024); 
   
   // socklen_t recv_len;
     int recv_len;   

    int message_len;
    /* 消息类型 */
    uint16_t message_type;

    uint8_t *head = NULL;
    uint8_t *rear = NULL;
    uint8_t *seek = NULL;
    head = &recv_buff[0];
    rear = &recv_buff[0];
    uint8_t data[1024];
    memset(data,0,1024);
    int complete_count   = 0; /* 完整数据包统计 */
    int complete_flag    = 0; /* 完整数据包尾部为0x7e标志 */

    /* temp */    
    uint32_t close_socket_value;

    while(1)
    {
                /* 检测系统中是否有未被回收的子进程，若有则回收 */
                signal(SIGCHLD,sig_child); 
                if ( (recv_len = recv(tcp_sock_fd,recv_buff,1024,0)) <= 0 )
                {
                        DEBUG_CONF(" %d\n",errno);
                        fprintf(stderr,"recv error: %s\n",strerror(errno)); 

                        /* update: 2019.5.5 增加重连机制 */
                        DEBUG_CONF("/*------------------------Try to Reconnect----------------------------*/\n");
                        /* 
                        需要检测是自己断开，还是服务器断开的情况
                        自己断开，不需要再次创建socket,服务器断开需要建立重连机制
                        */  
                        pthread_rwlock_rdlock(&rw_close_socket_flag);
                        close_socket_value = closeSocket_Bymyself;
                        pthread_rwlock_unlock(&rw_close_socket_flag);

                        /* 自己方检测断开 */
                        if ( close_socket_value ) 
                        {
                                DEBUG_CONF("close by myself!\n");
                                pthread_rwlock_wrlock(&rw_close_socket_flag);
                                closeSocket_Bymyself = 0;
                                pthread_rwlock_unlock(&rw_close_socket_flag);
                                continue; 

                        }
                        else
                        {
                                        DEBUG_CONF("close by server!\n");
                                        /* 保证只有一个线程去关闭socket */
                                        pthread_rwlock_wrlock(&rw_close_socket_flag); 
                                        
                                        shutdown(tcp_sock_fd,SHUT_RDWR);
                                        close(tcp_sock_fd);
                                        Tcp_Reconnection();
                                        
                                        /* 重新握手成功后释放锁 */
                                        pthread_rwlock_unlock(&rw_close_socket_flag); 

                                        /* 不改变变量的值 */
                                        closeSocket_Bymyself = 0;
                                       
                                        for( int i = 0; i < 3; i++)
                                        {                                                       
                                                /* 发送鉴权 */
                                                HUB_Authentication();
                                                DEBUG_CONF("\n/------------------------i'm authentication....--------------------------------/\n");
                                                /* 鉴权应答 */
                                                HUB_Authentication_Ack();

                                                pthread_rwlock_rdlock(&rw__auth_success_flag);      
                                                if (Auth_Successful_Flag == 0) 
                                                {
                                                        DEBUG_CONF("\n/--------------------Re-Register Server fail!....Register again in 5 seconds----------------------------/\n");         
                                                        /* 解锁 */                       
                                                        pthread_rwlock_unlock(&rw__auth_success_flag); 
                                                        sleep(5);
                                                }
                                                else
                                                {
                                                        DEBUG_CONF("\n/--------------------Re-Register Server successful!....------------------------/\n");  
                                                        /* 解锁 */                       
                                                        pthread_rwlock_unlock(&rw__auth_success_flag); 
                                                        /* 跳出循环 */
                                                        i = 4; 
                                                }
                                        }
                                        continue; 
                                      
                        }

                }
                else
                {
                        /* 成功接收到数据包，写入时间戳 */
                        pthread_rwlock_wrlock(&rw_heart_command_time);
                        Heart_Cmd_Interval = time((time_t *)NULL);       
                        pthread_rwlock_unlock(&rw_heart_command_time);
                        DEBUG_CONF("Heart_Cmd_Interval: %u\n",Heart_Cmd_Interval);   
                        


                        DEBUG_CONF("recv_len:%d\n",recv_len);
                        /* 尾指针地址 */
                        rear = &recv_buff[0] + recv_len -1;
                        /* 处理数据,seek开始从head位置找0x7e */
                        seek = head;
                        
                        #if 0
                        for(int loop_a=0;loop_a < recv_len;loop_a++)
                        {
                        printf("recv_buff[%d]:0x%02x ",loop_a,recv_buff[loop_a]);
                        }
                        #endif

                        while(seek != (rear+1))
                        {
                                /* 找到不止一包数据 */
                                if((seek != rear)&&(*seek == 0x7e))
                                {
                                        complete_count++;
                                }else if((seek == rear)&&(*seek == 0x7e)) /* 刚好在尾部找到一包数据 */
                                {
                                        complete_flag = 1;
                                }
                                seek++;

                        }

                        /* 
                                2种可能情况
                                1:找到不止一包完整数据
                                2:找到刚好一包完整数据
                        */
                        if((complete_count != 0)&&(complete_flag == 1)) /* 对应第一种情况 */
                        {
                                DEBUG_CONF("The first case!\n");
                                complete_count = complete_count +1 ; /* 加上尾部那包数据 */
                                DEBUG_CONF("complete_count:%d\n",complete_count);
                                
                                for(int i=0; i<complete_count; i++)
                                {
                                        /* 将seek重置到head处 */
                                        seek = head;

                                        /* seek 每次移动到0x7e处 */
                                        while( (seek != (rear+1)) && ((*seek) != 0x7e) )
                                        {
                                                seek++;
                                        }

                                        /* 数据拷贝 */
                                        mymemcpy(data,head,(seek - head));
                                        DEBUG_CONF("seek - head:%d\n",seek - head);
                                        DEBUG_CONF("data_len:%d\n",seek-head);
                                        DEBUG_CONF("seek:0x%02x\n",*seek);
                                        DEBUG_CONF("seek mic:0x%02x\n",*(seek-1));
                                        
                                        /* 
                                                数据处理部分
                                                转义中不再需要后导码0x7e
                                        */
                                        message_len = Transfer_Restore(message_buff,data,(seek - head));       
                                        mic = Check_Xor(message_buff,message_len-1);
                                        DEBUG_CONF("mic:0x%02x\n",mic);
                                        DEBUG_CONF("message_buff[message_len-1]:0x%02x\n",message_buff[message_len-1]);
                                        DEBUG_CONF("message_len:%d\n",message_len);
                                        
                                        /* 验证校验码 */
                                        if(message_buff[message_len-1] == mic)
                                        {
                                                /* 提取消息头中的消息类型 */
                                                message_type  = message_buff[2];
                                                message_type |= message_buff[1]<<8;
                                                DEBUG_CONF("message_type:0x%02x\n",message_type);
                                                
                                                /* 处理消息类型 */    
                                                Handle_Message_Type(message_type,message_buff,message_len);
                                        }
                                        else
                                        {
                                                DEBUG_CONF("sorry mic error\n");
                                        }
                                        
                                        /* 
                                                第一包数据处理完毕
                                                移动head到第一包0x7e的后面一个位置
                                        */
                                        head = seek +1;
                                }

                        }
                        else if((complete_count == 0)&&(complete_flag == 1)) /* 对应第二种情况 */
                        {
                                DEBUG_CONF("The second case!\n");
                                /* 数据拷贝 */
                                mymemcpy(data,head,(rear - head));

                                #if 0
                                DEBUG_CONF("data_len:%d\n",rear-head);
                                for(int i=0;i<rear-head;i++)
                                {
                                DEBUG_CONF("data[%d]:0x%02x\n",i,data[i]);
                                }
                                #endif

                                /* 
                                        数据处理部分
                                        转义中不再需要后导码0x7e
                                */
                                message_len = Transfer_Restore(message_buff,data,(rear - head));       
                                mic = Check_Xor(message_buff,message_len-1);
                                DEBUG_CONF("mic:0x%02x\n",mic);
                                DEBUG_CONF("message_buff[message_len-1]:0x%02x\n",message_buff[message_len-1]);
                                DEBUG_CONF("message_len:%d\n",message_len);
                                
                                /* 验证校验码 */
                                if(message_buff[message_len-1] == mic)
                                {
                                        /* 提取消息头中的消息类型 */
                                        message_type  = message_buff[2];
                                        message_type |= message_buff[1]<<8;
                                        DEBUG_CONF("message_type:0x%02x\n",message_type);

                                        /* 处理消息类型 */    
                                        Handle_Message_Type(message_type,message_buff,message_len);
                                }
                                else
                                {
                                        DEBUG_CONF("sorry mic error\n");
                                }

                        }
                        else
                        {
                                DEBUG_CONF("recv data error!\n");
                        }

                        /* 重置头指针尾指针 */
                        head = &recv_buff[0];
                        rear = &recv_buff[0];
                        memset(recv_buff,0,1024);
                        memset(data,0,1024); 
                        complete_count = 0;
                        complete_flag    = 0;

            }
    
    }


}

/**
 * brief:      服务器应答节点上报数据
 * 
 * parameter : 接收数据首地址，接收数据的长度
 * 
 * return : NULL 
 * 
 */

/*
思路：
        服务器回复join request数据是定长数据
        通过判断len的长度，从而判断服务器回复的是join request数据的应答包，还是join macpayload数据的应答包 
        join数据的应答包: 数据无意义    
                               udp 大端传输                          tcp 大端传输
        传输方式: lora_pkt_fwd.c <------------------>lora_pkt_server.c <------------------> senthink server 
*/
void  Server_Ack_Report_Data(uint8_t *data,int len)
{

    uint16_t value_len ;
    uint8_t  send_data[1024];
    memset(send_data,0,1024);
    int  send_len;
    uint8_t data_type;

    /* 服务器应答数据 */    
    static uint8_t ServerAckData[5000]; 
    uint8_t *ptr = NULL;   

    /* 服务器分包字节总和 */
    static uint16_t sum_len ;

    uint32_t freq_hz;
    uint8_t  dataRate;
    int      maxpayload;
    
    /* 应用服务器分包标志位 */
    bool Multiple_Packets_Flag = false;
    bool FpendingIsOk = false;    
    
    /* 消息体属性 */
    uint16_t Message_Properties;
    
    /* lorwan下行分包总数 */
    uint16_t packet_sum;
    /* lorwan下行 最后一包字节书大小 */
    int last_packet;
    /* lorwan下行 分包序  */
    uint16_t packet_number;

     /* 服务器应答分包总数 */
     uint16_t server_sum;
     /* 服务器应答分包序 */
     uint16_t server_number;


    /* 创建与lora_pkt_server传输数据的udp连接  */
    int socket_fd;
    struct sockaddr_in server_addr;
    int udp_len;

    /* 数组地址索引 */
    uint8_t *p = NULL;

     /*
        fixed bug: 因每次调用函数都需创建一个socket  因此，每次发送数据完成后需要关闭socket  
        linux kernel 最大可创建socket 640000

      */   

    /* creat sockfd  */
    socket_fd = socket(AF_INET,SOCK_DGRAM,0);
    if ( -1 == socket_fd ){
            DEBUG_CONF(" %d\n",errno);
            DEBUG_CONF("socket error!:%s\n",strerror(errno));
            close(socket_fd);
    }
       
    /* set sockaddr_in parameter */
    memset(&server_addr,0,sizeof(struct sockaddr_in));
    server_addr.sin_family     = AF_INET; 
    server_addr.sin_addr.s_addr  = inet_addr("127.0.0.1");
    
    /* 规定: 5557为下发给lora_pkt_fwd.c的应答数据的监听端口 */
    server_addr.sin_port         = htons(5557); 
    
    Message_Properties  = data[14];
    Message_Properties |= data[13] << 8;

    /* 判断分包位,分包位：消息属性的第13位 */
    if ( ( Message_Properties & 0x2000 ) ){

            Multiple_Packets_Flag = true;

    } else {

            Multiple_Packets_Flag = false;
    }
    
    /* 需要根据频率和DataRate判断下行数据是否进行分包 */

        pthread_rwlock_rdlock(&rw_confirmed_data);    
        freq_hz  = Confirmed_Data[3];
        freq_hz |= Confirmed_Data[2] <<  8;   
        freq_hz |= Confirmed_Data[1] << 16; 
        freq_hz |= Confirmed_Data[0] << 24;
        dataRate = Confirmed_Data[4];
        pthread_rwlock_unlock(&rw_confirmed_data);     

        if ( -1 == CalculateDownlinkMaxpayload( &freq_hz, &dataRate, &maxpayload))    
                        DEBUG_SERVER("calcute downlink maxpayload is error!,please check it!\n");
     
        DEBUG_SERVER("freq_hz:    %u\n",    freq_hz);
        DEBUG_SERVER("dataRate:   %u\n",   dataRate);
        DEBUG_SERVER("maxpayload: %u\n", maxpayload);

     /* 判断是否进行分包操作 */
    if ( Multiple_Packets_Flag )
    {           
                /* 分包位清零 */
                Multiple_Packets_Flag = false;

                server_sum       =  data[16];
                server_sum      |=  data[15] << 8;

                server_number    = data[18];
                server_number   |= data[17]<<8;

                /* 没有到最后一包数据 */
                if ( server_number != server_sum) 
                {
                        DEBUG_SERVER("server sum:     %u\n",server_sum);
                        DEBUG_SERVER("server_number:  %u\n",server_number);
                        
                        value_len  = data[29];
                        value_len |= data[28] << 8;
                        sum_len += value_len;
                        DEBUG_SERVER("value_len: %u\n",value_len);
                        DEBUG_SERVER("sum_len:   %u\n",sum_len);
                      
                        ptr = ServerAckData + (server_number-1)*value_len;

                        /* 拷贝数据到缓存区域 */
                        mymemcpy( ptr, data+30,value_len);

                }
                else/* 服务器上最后一包数据 */
                {
                                
                        DEBUG_SERVER("server sum:     %u\n",server_sum);
                        DEBUG_SERVER("server_number:  %u\n",server_number);
                        
                        value_len  = data[29];
                        value_len |= data[28] << 8;
                        sum_len += value_len;
                        DEBUG_SERVER("value_len: %u\n",value_len);
                        DEBUG_SERVER("sum_len:   %u\n",sum_len);
                        
                        ptr = ServerAckData + (server_number-1)*value_len;

                        /* 拷贝数据到缓存区域 */
                        mymemcpy( ptr, data+30,value_len);


                        /* 进行lorawan协议下行数据分包 */
                        
                        /* 判断是否需要启动fpending */
                        if ( sum_len > maxpayload) 
                        {
                                FpendingIsOk = true; 

                                if ( sum_len % maxpayload == 0 ) {
                                
                                        packet_sum  =   sum_len / maxpayload;
                                        /* 最后一包字节数*/
                                        last_packet =   maxpayload; 

                                }else{
                                        packet_sum  =   (sum_len / maxpayload) + 1;       
                                        last_packet =   sum_len - ( (packet_sum-1) * maxpayload );
                                }       
                        }
                        else
                        {
                                FpendingIsOk = false;     
                        }     

                        DEBUG_SERVER("packet_sum:  %u\n", packet_sum);  
                        DEBUG_SERVER("last_packet: %u\n", last_packet);

                        /* 需要启用fpending */
                        if ( FpendingIsOk ) 
                        {
                                for ( packet_number = 1; packet_number <= packet_sum; packet_number++ )
                                {
                                        DEBUG_SERVER("/*----------------------------Debug: packet_number:%u---------------------------*/\n",packet_number);
        
                                        /* 不是最后一包数据 */
                                        if (packet_number != packet_sum) {

                                                p = ServerAckData + (packet_number-1)*maxpayload;
                                                
                                                /*  data copy */ 
                                                mymemcpy(send_data, p,  maxpayload);
                                                /* 四字节标识码 */
                                                send_data[maxpayload]   = 0xfb;
                                                send_data[maxpayload+1] = 0xfc;
                                                send_data[maxpayload+2] = 0xfd;
                                                send_data[maxpayload+3] = 0xfe;
                                                send_data[maxpayload+4] = packet_sum >> 8;
                                                send_data[maxpayload+5] = packet_sum;
                                                send_data[maxpayload+6] = packet_number >> 8;
                                                send_data[maxpayload+7] = packet_number;
                                                
                                                send_len = maxpayload + 8;

                                                udp_len = sendto(socket_fd,send_data,send_len,0,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
                                                if(udp_len <= 0){

                                                        DEBUG_CONF(" %d\n",errno);
                                                        fprintf(stderr,"send join data is error!: %s\n",strerror(errno));
                                                        close(socket_fd);
                                                }                                

                                        }else{/* 最后一包数据 */

                                                p = ServerAckData + (packet_number-1)*maxpayload;
                                                
                                                /*  data copy */ 
                                                mymemcpy(send_data, p,  last_packet);
                                                /* 四字节标识码 */
                                                send_data[last_packet]   = 0xfb;
                                                send_data[last_packet+1] = 0xfc;
                                                send_data[last_packet+2] = 0xfd;
                                                send_data[last_packet+3] = 0xfe;
                                                send_data[last_packet+4] = packet_sum >> 8;
                                                send_data[last_packet+5] = packet_sum;
                                                send_data[last_packet+6] = packet_number >> 8;
                                                send_data[last_packet+7] = packet_number;
                                                
                                                send_len = last_packet + 8;

                                                udp_len = sendto(socket_fd,send_data,send_len,0,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
                                                if(udp_len <= 0){
                                                        DEBUG_CONF(" %d\n",errno);
                                                        fprintf(stderr,"send join data is error!: %s\n",strerror(errno));
                                                        close(socket_fd);
                                                }                                        

                                        }
                                        
                                }
                                
                        }
                        else /* 服务器分包 但却没超过maxpayload的值:服务器主动分包情况 */
                        {
                                /* copy */ 
                                mymemcpy(send_data,data+30,value_len);
                                udp_len = sendto(socket_fd,send_data,value_len,0,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
                                if(udp_len <= 0) {     
                                                
                                                DEBUG_CONF(" %d\n",errno);
                                                fprintf(stderr,"send join data is error!: %s\n",strerror(errno));
                                                close(socket_fd);
                                }    
                                
                        }

                        /* clear */
                        sum_len = 0;
                        memset(ServerAckData, 0, 5000);  
                        memset(data, 0, len);
                }

    }
    else/* 服务器未分包 */
    {
                value_len  = data[25];
                value_len |= data[24] << 8;
                DEBUG_SERVER("value_len: %u\n",value_len); 
                
                /* 判断是否需要启动fpending */
                if ( value_len > maxpayload) 
                {
                        FpendingIsOk = true; 
                        if ( value_len % maxpayload == 0 ) {
                                
                                packet_sum  =   value_len / maxpayload;
                                /* 最后一包字节数*/
                                last_packet =   maxpayload; 

                        }else{
                                packet_sum  =   (value_len / maxpayload) + 1;       
                                last_packet =   value_len - ( (packet_sum-1) * maxpayload );
                        }       
                }
                else
                {
                        FpendingIsOk = false;

                }

                DEBUG_SERVER("packet_sum:  %u\n", packet_sum);  
                DEBUG_SERVER("last_packet: %u\n", last_packet);

                /* 不需要启用fpending */
                if ( !FpendingIsOk )  
                {
                        /* 判断是否是join 应答包 */    
                        if ( data[21] == 0x01 )
                        {
                                DEBUG_CONF("ACK JOIN DATA: \n");
                                for(int i=0; i <= len; i++)
                                        DEBUG_CONF("ack join data[%d]: 0x%02x\n",i,data[i]);
                        } 
                        /* unconfirmed 或  confirmed */
                        else if ( (data[21] == 0x02 ) || (data[21] == 0x03 ))
                        {
                                data_type = data[21];
                                /* 如果是unconfirmed类型，并且服务器没有回复数据，则不用处理 */
                                if ( (data_type == 0x02) && (value_len == 0x00)) {}
                                /* 如果是confirmed 类型，并且服务器没有回复数据，则网关下发一包空的数据 */
                                else if ( (data_type == 0x03) && (value_len == 0x00)){}
                                else/* 如果有数据，不论是unconfirmed,还是confirmed则下发数据 */  
                                {
                                        /* copy */ 
                                        mymemcpy(send_data,data+26,value_len);
                                        udp_len = sendto(socket_fd,send_data,value_len,0,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
                                        if(udp_len <= 0) {     
                                                        
                                                        DEBUG_CONF(" %d\n",errno);
                                                        fprintf(stderr,"send join data is error!: %s\n",strerror(errno));
                                                        close(socket_fd);
                                        }    
                                }
                        }
                        else
                        {
                                DEBUG_SERVER("Message type is error\n"); 
                        }

                }
                else/* 需要开启fpending */
                {
                        for ( packet_number = 1; packet_number <= packet_sum; packet_number++ )
                       {
                                DEBUG_SERVER("/*----------------------------Debug: packet_number:%u---------------------------*/\n",packet_number);
                                DEBUG_SERVER("packet_sum:  %u\n", packet_sum);   
                                
                                /* 不是最后一包数据 */
                                if (packet_number != packet_sum) {

                                        p =  data + 30 + (packet_number-1)*maxpayload;
                                        
                                        /*  data copy */ 
                                        mymemcpy(send_data, p,  maxpayload);
                                        /* 四字节标识码 */
                                        send_data[maxpayload]   = 0xfb;
                                        send_data[maxpayload+1] = 0xfc;
                                        send_data[maxpayload+2] = 0xfd;
                                        send_data[maxpayload+3] = 0xfe;
                                        send_data[maxpayload+4] = packet_sum >> 8;
                                        send_data[maxpayload+5] = packet_sum;
                                        send_data[maxpayload+6] = packet_number >> 8;
                                        send_data[maxpayload+7] = packet_number;
                                        
                                        send_len = maxpayload + 8;

                                        udp_len = sendto(socket_fd,send_data,send_len,0,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
                                        if(udp_len <= 0){

                                                DEBUG_CONF(" %d\n",errno);
                                                fprintf(stderr,"send join data is error!: %s\n",strerror(errno));
                                                close(socket_fd);
                                        }                                

                                }else{/* 最后一包数据 */

                                        p =  data + 30 + (packet_number-1)*maxpayload;
                                        
                                        /*  data copy */ 
                                        mymemcpy(send_data, p,  last_packet);
                                        /* 四字节标识码 */
                                        send_data[last_packet]   = 0xfb;
                                        send_data[last_packet+1] = 0xfc;
                                        send_data[last_packet+2] = 0xfd;
                                        send_data[last_packet+3] = 0xfe;
                                        send_data[last_packet+4] = packet_sum >> 8;
                                        send_data[last_packet+5] = packet_sum;
                                        send_data[last_packet+6] = packet_number >> 8;
                                        send_data[last_packet+7] = packet_number;
                                        
                                        send_len = last_packet + 8;

                                        udp_len = sendto(socket_fd,send_data,send_len,0,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
                                        if(udp_len <= 0){
                                                DEBUG_CONF(" %d\n",errno);
                                                fprintf(stderr,"send join data is error!: %s\n",strerror(errno));
                                                close(socket_fd);
                                        }                                        

                                }
                        
                        }     

                }

                /* clear */
                value_len = 0;
                memset(data, 0, len);
    
    }
        
    /* close socket */
    close (socket_fd);

}

/**
 * breif:       HUB注册
 * 
 * parameter:   NULL
 * 
 * return   :   NULL 
 */
void Hub_Register(void)
{ 
    char *read_cfg_path = "/lorawan/lorawan_hub/server_conf.json";
    char  conf_obj[]    = "Version_conf";  
    char  *hardware     = "Hardware_version"; 
    char  *firmware     = "Firmware_version";
    
    const char  *hardware_p   = NULL;
    const char  *firmware_p   = NULL;

    JSON_Value  *read_val;
    JSON_Object *read_obj   = NULL;
    JSON_Object *read_conf  = NULL;

    int   hardware_version_len;
    int   firmware_version_len;

    int buff_len;
    int hardware_version_buff_len;
    int firmware_version_buff_len; 
    
    uint8_t join_data[JOIN_DATA_LEN];
    memset(join_data,0,JOIN_DATA_LEN);
    int data_len = 0;

    /* GWinfo */
    int fd_gw;
    int size_gw;
    uint8_t *gwinfo_string = (uint8_t*)malloc(100);
    uint8_t  gwinfo[8];
    memset(gwinfo,0,8);
    static uint32_t join_message_id = 0;
    /* 发送封装、转义后的缓存数组 */
    uint8_t send_data[200];
    memset(send_data,0,200);
    int send_data_len = 0; 
    /* 读入网关信息的文件 */
    fd_gw = open("/lorawan/lorawan_hub/gwinfo",O_RDWR);
    if(-1 == fd_gw){
            DEBUG_CONF("sorry,There is no gwinfo file in this directory,Please check it!\n");            
            close(fd_gw);
            pthread_exit(0); 
    }
    size_gw = read(fd_gw,gwinfo_string,16);
    close(fd_gw);
    /* ascii to hex */
    String_To_ChArray(gwinfo,gwinfo_string,8);
    if(access(read_cfg_path,R_OK) == 0)
    {
            DEBUG_CONF(" %s file is exit, parsing this file\n",read_cfg_path);
            read_val     = json_parse_file_with_comments(read_cfg_path);
            read_obj     = json_value_get_object(read_val);
            if (read_obj == NULL){
                    DEBUG_CONF("ERROR: %s is not a valid JSON file\n",read_cfg_path);
                    exit(EXIT_FAILURE);
            }
            read_conf = json_object_get_object(read_obj,conf_obj);
            if(read_conf == NULL){
                    DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj);
            }
            hardware_p = json_object_get_string(read_conf,hardware);
            firmware_p = json_object_get_string(read_conf,firmware);  
            hardware_version_len = strlen(hardware_p);
            /* ascii 转 hex */
            Ascii_To_Hex( hardware_buff,hardware_p, hardware_version_len);
            firmware_version_len = strlen(firmware_p);
            Ascii_To_Hex( firmware_buff,firmware_p, firmware_version_len);    
    }
    
    /* debug */
    #if 0
    DEBUG_CONF("hardware_p:%s\n",hardware_p);
    DEBUG_CONF("firmware_p:%s\n",firmware_p);
    DEBUG_CONF("hardware_version_len:%d\n",hardware_version_len);
    DEBUG_CONF("firmware_version_len:%d\n",firmware_version_len);
    
    for(int i=0;i<hardware_version_len;i++)
    {
        DEBUG_CONF("hardware_buff[%d]:0x%x\n",i,hardware_buff[i]);
    }
    
    for(int i=0;i<firmware_version_len;i++)
    {
        DEBUG_CONF("firmware_buff[%d]:0x%x\n",i,firmware_buff[i]);
    }
    #endif

    /* 参考LoRaWAN-HUB与服务器通信协议
       大端传输
       进行数据封装，上报至服务器
       ---------------------------------
      | 消息头  | 消息体 | 校验码 |  后导码 |   
       ---------------------------------
       ----------------------------------------------------------
      | 消息版本 | 消息类型 | HUB编号 | 消息序号 | 消息体属性 | 分包属性 |
       ---------------------------------------------------------- 
    */

    /* 消息版本 */
    join_data[data_len++] = 0x01;
    /* 消息类型 2byte */
    join_data[data_len++] = 0x01;
    join_data[data_len++] = 0x01;
    /* HUB唯一编号 */
    mymemcpy(join_data+data_len,gwinfo,8);
    data_len+=8;
    join_message_id++;
    /* 消息序号 */
    join_data[data_len++] = (uint8_t)(join_message_id >> 8);
    join_data[data_len++] = (uint8_t) join_message_id;
    
    /* 
      消息体属性
       -----------------------------------------------
      |15 14| 13 | 12 11  10  | 9 8 7 6 5 4 3 2 1 0  |    
      |----------------------------------------------
      | 保留 |分包| 消息体加密方式| 消息体长度            |
      |----------------------------------------------
    
    */

    /* 消息体注册长度为12 */
    join_data[data_len++] = 0x00;
    join_data[data_len++] = 0x12;  
    
    /* 
      消息体结构   
       ---------------------------------------------
      | 消息体类型          |   消息体数据             |      
       --------------------------------------------- 
    */

    /* 消息体类型 */
    /* Tag */
    join_data[data_len++] = 0x01;
    /* Type */
    join_data[data_len++] = 0x0B;
    /* Length */
    join_data[data_len++] = 0x06;
    /* Value */
    buff_len = data_len + hardware_version_len;
    hardware_version_buff_len = hardware_version_len;    
    /* Value */
    while(hardware_version_len){
            join_data[buff_len--] = hardware_buff[hardware_version_len--];
    }
    join_data[data_len] = hardware_buff[0];
    data_len = data_len + hardware_version_buff_len; 
    
    /* Tag */
    join_data[data_len++] = 0x02;
    /* Type */
    join_data[data_len++] = 0x0B;
    /* Length */
    join_data[data_len++] = 0x06;
    buff_len = data_len + firmware_version_len;
    firmware_version_buff_len = firmware_version_len;    
    /* Value */
    while(firmware_version_len){
            join_data[buff_len--] = firmware_buff[firmware_version_len--];
    }
    join_data[data_len] = firmware_buff[0];
    data_len = data_len + firmware_version_buff_len; 
    /* 计算校验码 */
    join_data[data_len] = Check_Xor(join_data,data_len); 
    /* 
        转义处理
        从前导码之后进行处理
    */
    data_len++;
    send_data_len  = Transfer_Mean(send_data,join_data,data_len);
    /* 后导码 */
    send_data[send_data_len] = 0x7e;
    send_data_len += 1;

    /* 保证同一时刻只有一个线程操作该套接字 */
    pthread_rwlock_wrlock(&rw_close_socket_flag);      
    /* 进行数据发送 */
    tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
    if(tcp_send_len <= 0)
    {
            /* 解锁 */
            pthread_rwlock_unlock(&rw_close_socket_flag);
            DEBUG_CONF(" %d\n",errno);
            DEBUG_CONF("send error: %s\n",strerror(errno));
            ReconnectWith_Server( );

    }
    else
    {
            /* 解锁 */
            pthread_rwlock_unlock(&rw_close_socket_flag);
    }
        
    /* 每次操作JSON文件后需序列化文件，并释放指针 */
    json_serialize_to_file(read_val,read_cfg_path); 
    json_value_free(read_val);
}

/**
 * breif:       更新HUB端本地数据库的注册信息
 * 
 * parameter:   NULL
 * 
 * return   :   NULL 
 */

void Update_Register_Info(uint8_t *data)
{

    char  *auth_code = (char*)malloc(100);
    char  *sql_auth  = (char*)malloc(200);
    char  *auth_p    =  NULL;
    uint8_t auth_len;
    auth_len = data[24];

    /* 将返回的鉴权码存入到数据库表中 */            
    auth_p = Hex_To_String(auth_code,data+25,auth_len);
    DEBUG_CONF("auth string: %s\n",auth_p);

    /* 数据大端传输，不用倒序 */
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
    sprintf(sql_auth,"UPDATE Hub_Register SET Auth_Code = '%s';", auth_p);
    rc = sqlite3_exec(db,sql_auth,0,0,&zErrMsg);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg); 

    if ( sql_auth != NULL)
                free(sql_auth);

    if ( auth_p != NULL) 
                free(auth_p);
   
    
}

#ifdef   HUB_Server_Communication_Version_v_1_0_3

/**
 * breif:       HUB鉴权
 * 
 * parameter:   NULL
 * 
 * return   :   NULL 
 */
void HUB_Authentication(void)
{
    
    DEBUG_CONF("/*-------------------------------------Begin authentication!-----------------------------------*/\n");
    char  *read_cfg_path = "/lorawan/lorawan_hub/server_conf.json";
    char  *conf_obj      = "Version_conf";  
    char  *hardware      = "Hardware_version"; 
    char  *firmware      = "Firmware_version";
    
    const char  *hardware_p   = NULL;
    const char  *firmware_p   = NULL;

    JSON_Value  *read_val;
    JSON_Object *read_obj   = NULL;
    JSON_Object *read_conf  = NULL;

    int   hardware_version_len;
    int   firmware_version_len;

    int buff_len;
    int hardware_version_buff_len;
    int firmware_version_buff_len; 
    
    uint8_t join_data[JOIN_DATA_LEN];
    memset(join_data,0,JOIN_DATA_LEN);
    int data_len = 0;

    /* GWinfo */
    int fd_gw;
    int size_gw;
    uint8_t  gwinfo[8];
    memset(gwinfo,0,8);
    static uint32_t join_message_id = 0;
    /* 发送封装、转义后的缓存数组 */
    uint8_t send_data[200];
    memset(send_data,0,200);
    int send_data_len = 0; 

    /* 读入网关信息的文件 */
    if (-1 == ReadGwInfo(gwinfo))
            DEBUG_CONF("ReadGwInfo error!\n");

    if(access(read_cfg_path,R_OK) == 0)
    {
            DEBUG_CONF(" %s file is exit, parsing this file\n",read_cfg_path);
            read_val     = json_parse_file_with_comments(read_cfg_path);
            read_obj     = json_value_get_object(read_val);
            if (read_obj == NULL){
                    DEBUG_CONF("ERROR: %s is not a valid JSON file\n",read_cfg_path);
                    exit(EXIT_FAILURE);
            }
            read_conf = json_object_get_object(read_obj,conf_obj);
            if(read_conf == NULL){
                    DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj);
            }
            hardware_p = json_object_get_string(read_conf,hardware);
            firmware_p = json_object_get_string(read_conf,firmware);  
            hardware_version_len = strlen(hardware_p);
            /* ascii 转 hex */
            Ascii_To_Hex( hardware_buff,hardware_p, hardware_version_len);
            firmware_version_len = strlen(firmware_p);
            Ascii_To_Hex( firmware_buff,firmware_p, firmware_version_len);    
    }
    
    /* 参考LoRaWAN-HUB与服务器通信协议
       大端传输
       进行数据封装，上报至服务器
       ---------------------------------
      | 消息头  | 消息体 | 校验码 |  后导码 |   
       ---------------------------------
       ----------------------------------------------------------
      | 消息版本 | 消息类型 | HUB编号 | 消息序号 | 消息体属性 | 分包属性 |
       ---------------------------------------------------------- 
    */

    /* 消息版本 */
    join_data[data_len++] = 0x01;
    /* 消息类型 2byte */
    join_data[data_len++] = 0x01;
    join_data[data_len++] = 0x02;
    /* HUB唯一编号 */
    mymemcpy(join_data+data_len,gwinfo,8);
    data_len+=8;
    join_message_id++;
    /* 消息序号 */
    join_data[data_len++] = (uint8_t)(auth_message_id >> 8);
    join_data[data_len++] = (uint8_t) auth_message_id;
    
    /* 
      消息体属性
       -----------------------------------------------
      |15 14| 13 | 12 11  10  | 9 8 7 6 5 4 3 2 1 0  |    
      |----------------------------------------------
      | 保留 |分包| 消息体加密方式| 消息体长度            |
      |----------------------------------------------
    
    */

    /* 消息体鉴权长度为12 */
    join_data[data_len++] = 0x00;
    join_data[data_len++] = 0x12;  
    
    /* 
      消息体结构   
       ---------------------------------------------
      | 消息体类型          |   消息体数据             |      
       --------------------------------------------- 
    */

    /* 消息体类型 */
    /* Tag */
    join_data[data_len++] = 0x01;
    /* Type */
    join_data[data_len++] = 0x0B;
    /* Length */
    join_data[data_len++] = 0x06;

    /* Hardware Value */
    buff_len = data_len + hardware_version_len;
    hardware_version_buff_len = hardware_version_len;    
    
    /* copy */
    while(hardware_version_len)
                join_data[buff_len--] = hardware_buff[hardware_version_len--];
    
    join_data[data_len] = hardware_buff[0];
    data_len = data_len + hardware_version_buff_len; 
    

    /* Tag */
    join_data[data_len++] = 0x02;
    /* Type */
    join_data[data_len++] = 0x0B;
    /* Length */
    join_data[data_len++] = 0x06;

    /* Firmware Value */
    buff_len = data_len + firmware_version_len;
    firmware_version_buff_len = firmware_version_len;    

    /* copy */    
    while(firmware_version_len)
                join_data[buff_len--] = firmware_buff[firmware_version_len--];
    
    join_data[data_len] = firmware_buff[0];
    data_len = data_len + firmware_version_buff_len; 
    
    
    /* 计算校验码 */
    join_data[data_len] = Check_Xor(join_data,data_len); 
    /* 
        转义处理
        从前导码之后进行处理
    */
    data_len++;
    send_data_len  = Transfer_Mean(send_data,join_data,data_len);
    /* 后导码 */
    send_data[send_data_len] = 0x7e;
    send_data_len += 1;

    /* 保证同一时刻只有一个线程操作该套接字 */
    pthread_rwlock_wrlock(&rw_close_socket_flag);      
    /* 进行数据发送 */
    tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
    if(tcp_send_len <= 0)
    {
            /* 解锁 */
            pthread_rwlock_unlock(&rw_close_socket_flag);
            DEBUG_CONF(" %d\n",errno);
            DEBUG_CONF("send error: %s\n",strerror(errno));
            ReconnectWith_Server( );

    }
    else
    {
            /* 解锁 */
            pthread_rwlock_unlock(&rw_close_socket_flag);
    }
        
    /* 每次操作JSON文件后需序列化文件，并释放指针 */
    json_serialize_to_file(read_val,read_cfg_path); 
    json_value_free(read_val);

}



#else


/**
 * breif:       HUB鉴权
 * 
 * parameter:   NULL
 * 
 * return   :   NULL 
 */
void HUB_Authentication(void)
{    
    DEBUG_CONF("Begin authentication!\n");
    sqlite3_stmt *stmt_authcode = NULL;
    char *sql_buff =(char*)malloc(100);
    const char  *fetch_buff = NULL;
    uint8_t auth_codebuff[30];
    memset(auth_codebuff,0,30);

    /* gwinfo */
    int fd_gw;
    int size_gw;
    uint8_t *gwinfo_string = (uint8_t*)malloc(100);
    uint8_t  gwinfo[8];
    memset(gwinfo,0,8);

    uint8_t join_data[JOIN_DATA_LEN];
    memset(join_data,0,JOIN_DATA_LEN);
    int data_len = 0;
    static uint16_t join_message_id;
    int message_len; 
    int data_len_buff;
    uint8_t send_data[1000];
    memset(send_data,0,1000);
    uint16_t send_data_len;

    int auth_buff_len = 0x02;
 
    /* 读入网关信息的文件 */
    fd_gw = open("/lorawan/lorawan_hub/gwinfo",O_RDWR);
    if(-1 == fd_gw)
    {
            DEBUG_CONF("sorry,There is no gwinfo file in this directory,Please check it!\n");            
            close(fd_gw);
            pthread_exit(0); 
    }
    size_gw = read(fd_gw,gwinfo_string,16);
    close(fd_gw);
    /* ascii to hex */
    String_To_ChArray(gwinfo,gwinfo_string,8);

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL); 
    /* sql语句 */
    sql_buff = "SELECT* FROM Hub_Register WHERE ID = 1;";    

    /* parpare */
    rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt_authcode,NULL);
    if (SQLITE_OK !=rc || NULL == stmt_authcode)
    {
          printf("\n\n fetch prepare error!\n\n");
          sqlite3_close(db);
    }

    /* execute */
    while(SQLITE_ROW == sqlite3_step(stmt_authcode))
    {
            fetch_buff = sqlite3_column_text(stmt_authcode, 1);
            auth_buff_len = strlen(fetch_buff);
            Ascii_To_Hex(auth_codebuff,fetch_buff,auth_buff_len);
    }
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg); 
    
    /*
        消息封装
        参考LoRaWAN-HUB与服务器通信协议
        大端传输
        进行数据封装，上报至服务器
         ---------------------------------
        | 消息头  | 消息体 | 校验码 |  后导码 | 
         ---------------------------------
         ----------------------------------------------------------
        | 消息版本 | 消息类型 | HUB编号 | 消息序号 | 消息体属性 | 分包属性 |
         ---------------------------------------------------------- 
    */

    /* 消息版本 */
    join_data[data_len++] = 0x01;
    /* 消息类型 */
    join_data[data_len++] = 0x01;
    join_data[data_len++] = 0x02;
    /* HUB唯一编号 */
    mymemcpy(join_data+data_len,gwinfo,8);
    data_len+=8;
    auth_message_id++;
    if( 65535 == auth_message_id )
            auth_message_id = 0;
   
    /* 消息序号 */
    join_data[data_len++] = (uint8_t)auth_message_id>>8;
    join_data[data_len++] = (uint8_t)auth_message_id;

    /*
        消息体属性
        -----------------------------------------------
        |15 14| 13 | 12 11  10  | 9 8 7 6 5 4 3 2 1 0  |    
        |----------------------------------------------
        | 保留 |分包| 消息体加密方式| 消息体长度            |
        |----------------------------------------------
        鉴权的消息体长度
        大端传输
    */
    message_len = auth_buff_len+3;
    join_data[data_len++] = (uint8_t)(message_len >> 8);
    join_data[data_len++] = (uint8_t)message_len;

    /* 消息体 */
    join_data[data_len++] = 0x01;
    join_data[data_len++] = 0x0b;
    /* 消息体长度 */
    join_data[data_len++] = auth_buff_len;
    data_len_buff = data_len + auth_buff_len;
    while(auth_buff_len)
    {
            join_data[data_len_buff--] = auth_codebuff[auth_buff_len--];
    }
    join_data[data_len] = auth_codebuff[0];
    data_len = data_len + message_len -3;
    /* 计算校验码 */
    join_data[data_len] = Check_Xor(join_data,data_len);
    data_len++; 
    /* 转义处理 */
    send_data_len  = Transfer_Mean(send_data,join_data,data_len);
    /* 后导码 */
    send_data[send_data_len] = 0x7e;
    send_data_len += 1;
    /* 进行数据发送 */

     /* 保证同一时刻只有一个线程操作该套接字 */
     pthread_rwlock_wrlock(&rw_close_socket_flag);

    tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);
    if(tcp_send_len <= 0)
    {
            /* 解锁 */
            pthread_rwlock_unlock(&rw_close_socket_flag);       
            DEBUG_CONF("send error: %s\n",strerror(errno));
            close(tcp_sock_fd); 
    }
    else
    {
                /* 解锁 */
                pthread_rwlock_unlock(&rw_close_socket_flag);   
                if ( gwinfo_string != NULL ) 
                                free(gwinfo_string);

                if ( stmt_authcode != NULL ) 
                                sqlite3_finalize(stmt_authcode);
    }
    
}

#endif


/**
 * breif:       HUB鉴权应答
 * 
 * parameter:   NULL
 * 
 * return   :   0: successful 1:fail  
 */
int HUB_Authentication_Ack(void)
{
    uint8_t auth_buff[300];
    memset(auth_buff,0,300);
    int auth_len;
    uint8_t auth_decode_buff[300];
    memset(auth_decode_buff,0,300);    
    int auth_decode_len;
    uint8_t auth_mic;
    uint8_t message_id_buff;
    uint16_t auth_cmd = 0;

     /* 保证同一时刻只有一个线程操作该套接字 */
     pthread_rwlock_wrlock(&rw_close_socket_flag);

    if( ( auth_len = recv(tcp_sock_fd,auth_buff,1000,0)) <= 0 )
    {
            /* 解锁 */
            pthread_rwlock_unlock(&rw_close_socket_flag);
            DEBUG_CONF(" %d\n",errno);
            fprintf(stderr, "recv error:%s\n\a", strerror(errno));
            close(tcp_sock_fd);    
    }
    else
    {     
            /* 解锁 */
            pthread_rwlock_unlock(&rw_close_socket_flag);
            /* 校验后导码 --> 转义还原 --> 验证校验码 --> 解析消息 */
            if(auth_buff[auth_len-1] == 0x7e)
            {
                    auth_decode_len = Transfer_Restore(auth_decode_buff,auth_buff,auth_len-1);
                    auth_mic = Check_Xor(auth_decode_buff,auth_decode_len-1);
                    DEBUG_CONF("auth_decode_len:%d\n",auth_decode_len);

                    for(int i=0;i<auth_decode_len;i++)
                            DEBUG_CONF("auth_decode_buff[%d]:0x%02x\n",i,auth_decode_buff[i]);
                    
                    DEBUG_CONF("auth_mic:0x%02x\n",auth_mic);

                    /* 验证校验码 */
                    if(auth_decode_buff[auth_decode_len-1] ==  auth_mic)
                    {
                            /* auth_cmd:消息类型 */
                            auth_cmd  = auth_decode_buff[2];
                            auth_cmd |= auth_decode_buff[1]<<8;
                            
                            /* 验证消息类型是否正确 */
                            if(Server_Auth_Ack == auth_cmd)
                            {
                                    message_id_buff  = auth_decode_buff[18];
                                    message_id_buff |= auth_decode_buff[17] << 8;
                                    
                                    if( (message_id_buff == auth_message_id) && (0x00 == auth_decode_buff[21]) )
                                    {
                                        /* 鉴权成功标志位 */
                                        pthread_rwlock_wrlock(&rw__auth_success_flag);
                                        Auth_Successful_Flag = 1;
                                        pthread_rwlock_unlock(&rw__auth_success_flag);

                                        memset(auth_decode_buff,0,300); 
                                        DEBUG_CONF("authentication successful");   
                                        return 0;

                                    }
                                    else
                                    {
                                        DEBUG_CONF("sorry, Authentication is fail!\n");
                                        memset(auth_decode_buff,0,300);
                                        return 1; 

                                    }       
                            }
                            else
                            {
                                    DEBUG_CONF("sorry message type error\n");
                                    memset(auth_decode_buff,0,300); 
                                    return 1;    
                            }

                    }   
                    else
                    {
                                    DEBUG_CONF("sorry mic error\n");
                                    return 1;
                    }

            }
            else
            {
                    DEBUG_CONF("sorry rear check code is error\n");
                    return 1;   
            }
    }  
    
}

/**
 * brief:      发送心跳包线程
 * 
 * parameter : 接收数据首地址，接收数据的长度
 * 
 * return : NULL 
 * 
 */
/*
        消息封装
        参考LoRaWAN-HUB与服务器通信协议
        大端传输
        进行数据封装，上报至服务器
        ---------------------------------
        | 消息头  | 消息体 | 校验码 |  后导码 |
        ---------------------------------
        ----------------------------------------------------------
        | 消息版本 | 消息类型 | HUB编号 | 消息序号 | 消息体属性 | 分包属性 |
        ---------------------------------------------------------- 
*/

void *Thread_Hub_Send_Heartbeat(void)
{
        uint8_t send_data[1000];
        memset(send_data,0,1000);
        uint16_t send_data_len;
        uint8_t join_data[1000];
        memset(join_data,0,1000);
        int data_len = 0;

        /* 时间变量 */
        time_t   current_time;    
        time_t   read_value;
        time_t   diff_time;
        /* 心跳缓存值 */
        time_t   heartbeat_temp;
        bool     SendHeartDataIsOk = false;
        

        /* GWinfo */
        int fd_gw;
        int size_gw;
        uint8_t *gwinfo_string = (uint8_t*)malloc(100);
        uint8_t  gwinfo[8];
        memset(gwinfo,0,8);

        /* 读入网关信息的文件 */
        fd_gw = open("/lorawan/lorawan_hub/gwinfo",O_RDWR);
        if(-1 == fd_gw)
        {
        DEBUG_CONF("sorry,There is no gwinfo file in this directory,Please check it!\n");            
        close(fd_gw);
        }
        size_gw = read(fd_gw,gwinfo_string,16);
        close(fd_gw);
        /* ascii to hex */
        String_To_ChArray(gwinfo,gwinfo_string,8);
        
        if (gwinfo_string != NULL) 
                free(gwinfo_string);

        while(1)
        {

                /* 允许3s误差判断 */        
                heartbeat_temp = 60 -3 ;                
                /* 读取间隔值，判断网络是否繁忙 */
                pthread_rwlock_rdlock(&rw_heart_command_time);
                read_value = Heart_Cmd_Interval;
                pthread_rwlock_unlock(&rw_heart_command_time);
                /* 读取当前绝对时间 */
                current_time = time((time_t *)NULL);
                diff_time    = current_time -  read_value;

                DEBUG_CONF("current_time:      %u\n",current_time);
                DEBUG_CONF("read_value:        %u\n",read_value);
                DEBUG_CONF("diff_time:         %u\n",diff_time);
                DEBUG_CONF("heartbeat_temp:    %u\n",heartbeat_temp);

                if (  diff_time >= heartbeat_temp )
                {
                        SendHeartDataIsOk = true; 
                        DEBUG_CONF("SendHeartDataIsOk is true!\n");    
                
                } else {

                        SendHeartDataIsOk = false; 
                        DEBUG_CONF("SendHeartDataIsOk is false!\n");    
                }
        
                if( SendHeartDataIsOk )
                {

                        /* 消息版本 */
                        join_data[data_len++] = 0x01;
                        /* 消息类型 */
                        join_data[data_len++] = 0x01;
                        join_data[data_len++] = 0x03;
                        /* HUB唯一编号 */
                        mymemcpy(join_data+data_len,gwinfo,8);
                        data_len+=8;
                        heart_beat_id++;

                        if(65535 == heart_beat_id)
                                        heart_beat_id = 0;

                        /* 消息序号   */
                        join_data[data_len++] = (uint8_t)(heart_beat_id>>8);
                        join_data[data_len++] = (uint8_t)heart_beat_id;
                        /* 消息体属性 */
                        join_data[data_len++] = 0x00;
                        join_data[data_len++] = 0x00;

                        /* 计算mic */
                        join_data[data_len++] = Check_Xor(join_data,data_len);
                        /* 转义处理 */
                        send_data_len  = Transfer_Mean(send_data,join_data,data_len);
                        /* 后导码 */
                        send_data[send_data_len] = 0x7e;
                        send_data_len += 1;

                        /* 保证同一时刻只有一个线程操作该套接字 */
                        pthread_rwlock_wrlock(&rw_close_socket_flag); 

                        /* 进行数据发送 */
                        tcp_send_len = send(tcp_sock_fd,send_data,send_data_len,0);

                        if ( tcp_send_len <= 0){
                                        /* 解锁 */
                                        pthread_rwlock_unlock(&rw_close_socket_flag);
                                        DEBUG_CONF(" %d\n",errno);
                                        fprintf(stderr,"send error: %s\n",strerror(errno));
                                        ReconnectWith_Server();
                        } else{

                                        /* 读取数据后，将值写回到临界区 */
                                        pthread_rwlock_wrlock(&rw_heart_command_time);
                                        Heart_Cmd_Interval = time((time_t *)NULL);
                                        pthread_rwlock_unlock(&rw_heart_command_time);
                                        
                                        /* 解锁 */
                                        pthread_rwlock_unlock(&rw_close_socket_flag);
                                        /* clear */
                                        memset(join_data,0,1000);
                                        memset(send_data,0,1000);
                                        data_len      = 0;
                                        send_data_len = 0;

                                        /* 发送心跳60s一次 */
                                        sleep(60);
                        }

                }
                else
                {
                        /* 发送心跳60s一次 */
                        sleep(60);
                }
                
        }

}

/**
 * brief:      服务器心跳应答
 * 
 * parameter : 接收数据首地址，接收数据的长度
 * 
 * return : NULL 
 * 
 */
void Server_Heartbeat_Ack(uint8_t *data,int len)
{

    /* 接收的心跳序列号 */
    uint16_t recv_heart_beat_id = 0;  
    recv_heart_beat_id  = data[18] ;
    recv_heart_beat_id |= data[17]<<8;
    DEBUG_CONF("recv_heart_beat_id:0x%02x\n",recv_heart_beat_id);
    DEBUG_CONF("heart_beat_id:     0x%02x\n",heart_beat_id);

    if(recv_heart_beat_id == heart_beat_id){
            DEBUG_CONF("/------------------------------------TCP is keeping connect!--------------------------------------/\n");

    }else{
                ReconnectWith_Server();      
    }

}

/**
 * breif:       检测hub是否注册成功
 * 
 * parameter:   NULL
 * 
 * return   :   NULL 
 */

int Check_Hub_Register(void)
{
    sqlite3_stmt *stmt_authcode = NULL;
    char *sql_buff =(char*)malloc(100);
    const char  *fetch_buff = (char*)malloc(100);
    uint8_t auth_codebuff[30];
    memset(auth_codebuff,0,30);

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL); 
    
    /* sql语句 */
    sql_buff = "SELECT* FROM Hub_Register WHERE ID = 1;";    
    
    /* parpare */
    rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt_authcode,NULL);
    if (SQLITE_OK !=rc || NULL == stmt_authcode)
    {
          printf("\n\n fetch prepare error!\n\n");
          sqlite3_close(db);
    }
    
    /* execute */
    while(SQLITE_ROW == sqlite3_step(stmt_authcode))
    {
            fetch_buff = sqlite3_column_text(stmt_authcode, 1);
            fetch_buff_len = strlen(fetch_buff);
            fetch_buff_len = fetch_buff_len / 2;
            String_To_ChArray(auth_codebuff,fetch_buff,fetch_buff_len);
            sqlite3_reset(stmt_authcode);
            /* 鉴权码不可能为初始值“000000”的情况 */
            
            if( 
                    (auth_codebuff[0]==0x00) 
                    && 
                    (auth_codebuff[1]==0x00) 
                    && 
                    (auth_codebuff[2]==0x00)
              )  
            {
                sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg); 
                DEBUG_CONF("return 0\n");
                return 0;
            }
            else
            {
                sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg); 
                DEBUG_CONF("return 1\n");
                return 1;
            }
    }
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg); 
    DEBUG_CONF("return 0\n");
    return 0;

}

/**
 * 函数：网关对class c数据进行处理
 * 
 * parameter: 接收数据首地址，接收数据的长度
 *
 * return: NULL
 * 
 */

void  Server_Class_C_Data(uint8_t *data,int len)
{
    /* 暂时不考虑服务器主动下发时的分包情况 */
    uint16_t value_len;
    int send_len = 0;
    uint8_t send_data[1024];
    memset(send_data,0,1024);

    /* 创建与lora_pkt_fwd.c传输数据的连接 */
    int socket_fd;
    struct sockaddr_in server_addr;
    int udp_len;
   
    /* creat sockfd */
    socket_fd = socket(AF_INET,SOCK_DGRAM,0);
    if ( -1 == socket_fd ){

            DEBUG_CONF("socket error!\n");
            close(socket_fd);
    }

    /* set sockaddr_in parameter */
    memset(&server_addr,0,sizeof(struct sockaddr_in));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    /* 规定：5558为下发给lora_pkt_fwd.c的class c数据的监听端口 */
    server_addr.sin_port        = htons(CLASS_C_PORT);
    
    /* class c消息体结构
     *
     * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
     * ｜   消息体类型   ｜ 节点的DevEui  |       消息体数据          ｜
     * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
     *
     */
     
     /* 拷贝消息体类型 */
     mymemcpy(send_data,data+17,1);
    
    /**
     * 判断下发类型
     * 
     * mType:
     * 
     * 0x03: unconfirmed data down
     * 
     * 0x05:   confirmed data down
     * 
     */ 

    if ( 0x02 == data[17] ){
            send_data[0] = 0x03;

    } else if ( 0x03 == data[17] ) {
            send_data[0] = 0x05;

    } else {
            DEBUG_CONF("sorry server send mtype is error!\n");
    }

    send_len = send_len + 1;
    
    /* 拷贝节点的deveui */
    mymemcpy(send_data+1,data+20,8);
    send_len   = send_len + 8;
    value_len  = data[31];
    value_len |= data[30]<<8;
    
    /* 拷贝消息体数据 */
    mymemcpy(send_data+9,data+32,value_len);

    /* +9: 1byte mtype 8byte deveui */
    value_len = value_len+9;
    udp_len = sendto(socket_fd,send_data,value_len,0,(struct sockaddr*)&server_addr,sizeof(struct sockaddr_in));
    
    if ( udp_len <= 0 ) {

            DEBUG_CONF("can't send join data\n");
            close(socket_fd);
    }

    /* close socket */
    close ( socket_fd );      
}

/**
 * brief:函数：服务器应答hub注册
 * 
 * parameter : NULL
 *
 * return: NULL
 * 
 */

/*   
     服务器应答hub注册
  1：如果服务器回复0x00, 则注册成功将注册码存储到数据库中，然后检测数据库信息
  2：如果服务器回复0x01, 则表示已经在该服务器上注册过，然后去检测数据库信息
  3：如果服务器回复0x02, 则表示服务器端无该HUB信息。
*/
int Server_Ack_Hub_Register(void)
{
    uint8_t  register_ack_buff[1000];
    memset(register_ack_buff,0,1000);
    int  register_ack_len = 0;
    uint8_t  register_decode_buff[1000];
    memset(register_decode_buff,0,1000);
    int  register_decode_len;
    uint8_t register_mic;

    /* 保证同一时刻只有一个线程操作该套接字 */
    pthread_rwlock_wrlock(&rw_close_socket_flag);    
    register_ack_len = recv(tcp_sock_fd,register_ack_buff,1000,0);
    if(register_ack_len <= 0)
    {
                /* 解锁 */
                pthread_rwlock_unlock(&rw_close_socket_flag);
                
                DEBUG_CONF(" %d\n",errno);   
                fprintf(stderr, "recv error:%s\n\a", strerror(errno));
                //close(tcp_sock_fd);
                return -1;  
    }
    else
    {     
            /* 解锁 */
            pthread_rwlock_unlock(&rw_close_socket_flag);
            
            /* 检测尾部是否为0x7e --> 转义还原 --> 验证校验码 --> 解析消息 */ 
            if(register_ack_buff[register_ack_len-1] == 0x7e)
            {
                    register_decode_len = Transfer_Restore(register_decode_buff,register_ack_buff,register_ack_len-1);
                    register_mic = Check_Xor(register_decode_buff,register_decode_len-1);
                    /* 验证校验码 */                    
                    if(register_decode_buff[register_decode_len-1] ==  register_mic)
                    {
                        /* 判断hub注册的状态 */
                        if(register_decode_buff[21] == 0x00)
                        {
                            /* 将服务器回复的应答数据写入数据库表中 */
                            Update_Register_Info(register_decode_buff);
                            return 0;
                        }
                        else if(register_decode_buff[21] == 0x01)/* 已经注册成功 */
                        {
                            
                            DEBUG_CONF("This HUB shows registered on the server side.\n");
                            return 1;         
                        }
                        else 
                        {
                            DEBUG_CONF("Server is no this HUB info.Please check it!\n");
                            return 2;
                        }          
                    }   
                    else
                    {
                        DEBUG_CONF("sorry mic error\n");
                    }   
            }
            else
            {
                        DEBUG_CONF("sorry rear check code is error\n");        
            }
    
    }

}

/*!
 * \brief 处理消息类型函数                            
 *
 * \param [IN]：服务器端传输的消息类型： message_type
 * 
 * \param [IN]：服务器端传输的数据：    message_buff
 * 
 * \param [IN]：服务器端传输的数据长度: len 
 * 
 * \Returns   ：NULL.
 */

 void  Handle_Message_Type(uint16_t message_type,uint8_t *message_buff, int message_len)
 {
     switch(message_type)
     {
          
            /*    
                update:2019.05.24
                数据通信放到单独的通信进程 
            */
            
            #if 0
                Handle_Server_Ack_Report_Data();                
                Handle_Server_Class_C_Data();
            #endif

            Handle_Server_Heartbeat_Ack();
            
            /* update:2018.12.13 */
            HandleServerGetAllNodeDevEui();
            HandleServerAddSingleNode();
            HandleServerDeleteAllNode();
            HandleServerDeleteSingleNode();
            HandleServerChangeSingleNodeAppkey();
            HandleServerSeekSingleNodeAppKey();
            HandleServerGetSX1301ConfInfo();
            HandleServerConfSX1301();
            HandleServerGetClassCFreq();
            HandleServerConfClassCFreq();      
            HandleServerGetRegionFreq();  
            HandleServerConfRegionFreq();     
            HandleServerGetRx1DRoffsetInfo();
            HandleServerConfRx1DRoffsetInfo();
            HandleServerGetUplinkFreq();
            HandleServerGetDownFreq();
            HandleServerConfUplinkFreq();     
            HandleServerConfDownFreq();
            HandleServerAddAbpNode();
            HandleServerReadAbpNode();
            HandleReadCommunicationServerInfo();
            HandleWriteCommunicationServerInfo();
            /* OTA设备升级 */
            HANDLE_ServerOTADeviceUpgrade();

            default:
            {
                break;
            }
     }
 }

 /**
 * brief:      TCP重新握手
 * 
 * parameter : 接收数据首地址，接收数据的长度
 * 
 * return : NULL 
 * 
 */
void Tcp_Reconnection(void)
{
        
        /* 加锁:保证只有一个线程可以执行 */
        pthread_mutex_lock(&mx_tcp_reconnect);
        
        struct  sockaddr_in tcp_server_addr;
        int     tcp_ret;

        char *read_cfg_path = "/lorawan/lorawan_hub/server_conf.json";
        char conf_obj[]     = "Server_conf";  
        char  *ip_name      = "server_ip"; 
        char  *ip_port      = "server_port";
        /* 读取的服务器地址 */ 
        const char  *server_address   =  NULL;
        /* 读取的服务器端口 */   
        uint32_t server_port;

        /* json类型 */
        JSON_Value  *read_val;
        JSON_Object *read       = NULL;
        JSON_Object *read_conf  = NULL;

        bool ConnectIsOk = false;
        uint32_t time_out =1;

      
        /* 重新进行socket连接 */
        DEBUG_CONF("/-------------------------TCP is lost!Reconnect to the server-----------------------------/\n");

        if(access(read_cfg_path,R_OK) == 0)
        {
                DEBUG_CONF(" %s file is exit, parsing this file\n",read_cfg_path);
                read_val = json_parse_file_with_comments(read_cfg_path);
                read     = json_value_get_object(read_val);
                if(read == NULL){
                        DEBUG_CONF("ERROR: %s is not a valid JSON file\n",read_cfg_path);
                        exit(EXIT_FAILURE);
                }

                read_conf = json_object_get_object(read,conf_obj);
                if(read_conf == NULL){
                        DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj);
                }

                server_address = json_object_get_string(read_conf,ip_name);
                server_port    = json_object_dotget_number(read_conf,ip_port);  
        }
        DEBUG_CONF("server_address: %s\n",server_address);
        DEBUG_CONF("server_port: %d\n",server_port);
    
        /* socket */
        tcp_sock_fd = socket(AF_INET,SOCK_STREAM,0);
        if ( -1 == tcp_sock_fd )
        {
                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"socket error: %s\n",strerror(errno));
        }

        /* set sockaddr_in parameter */
        memset(&tcp_server_addr,0,sizeof(struct sockaddr_in));
        tcp_server_addr.sin_family = AF_INET;
        tcp_server_addr.sin_port = htons(server_port);
        tcp_ret = inet_aton(server_address, &tcp_server_addr.sin_addr);
        if( 0 == tcp_ret )
        {
                DEBUG_CONF(" %d\n",errno);
                fprintf(stderr,"inet_aton error: %s\n",strerror(errno));         
        }
        else
        { 
                        for( int  i = 0; i < 12; i++)
                        {
                                if (ConnectIsOk == false)
                                {          
                                        /* connect */
                                        tcp_ret = connect(tcp_sock_fd,(const struct sockaddr*)&tcp_server_addr,sizeof(struct sockaddr));
                                        DEBUG_CONF("tcp_ret: %d\n",tcp_ret);
                                        if(-1 == tcp_ret){

                                                DEBUG_CONF("Tcp connect fail!,Next, reconnect! \n");   
                                                ConnectIsOk = false;   
                                                
                                                time_out = time_out * 2;
                                                DEBUG_CONF("/*---------------------------------time_out is: %u-------------------------------------*/\n", time_out);
                                                sleep (time_out);
                                                
                                                /* 重置 */
                                                if ( i == 11 )
                                                {    
                                                        /* i置0 */        
                                                        i = 0;
                                                        time_out = 1;
                                                }

                                                DEBUG_CONF(" %d\n",errno);
                                                fprintf(stderr,"connect error:%s\n",strerror(errno));
                                        
                                        }else{
                                                
                                                /* 连接成功跳出循环 */
                                                ConnectIsOk = true;
                                                i = 12;
                                        }
                                }        
                        }

        }
        
        pthread_mutex_unlock(&mx_tcp_reconnect);

        /* 每次操作JSON文件后需序列化文件，并释放指针 */
        json_serialize_to_file(read_val,read_cfg_path); 
        json_value_free(read_val);

}



 /*!
 * \brief:    重新与服务器取得联系的步骤                           
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL
 * 
 */
void ReconnectWith_Server(void)
{
        /*
                1：重新建立握手
                2：发送登录帧
                3：发生心跳
        */


     /* 
        socket 创建成功后 写变量，标识socket 是自己这边关闭的 
        需保证同一时刻只有一个线程关闭socket并创建新的socket进行重连操作
     
     */
        
        pthread_rwlock_wrlock(&rw_close_socket_flag);
       
        /* 立即关闭套接字  通知recv 函数 */
        shutdown(tcp_sock_fd,SHUT_RDWR);
        close(tcp_sock_fd);
        Tcp_Reconnection();        
        closeSocket_Bymyself = 1;        
        
        /* 鉴权成功标志位重新置为0 */
        pthread_rwlock_wrlock(&rw__auth_success_flag);
        Auth_Successful_Flag = 0;
        pthread_rwlock_unlock(&rw__auth_success_flag);

        
        /* 重新发送鉴权 */
        for( int i = 0; i < 3; i++)
        {
                pthread_rwlock_rdlock(&rw__auth_success_flag);      
                if (Auth_Successful_Flag == 0) 
                {
                        /* 解锁 */                       
                        pthread_rwlock_unlock(&rw__auth_success_flag);  

                        /* 发送鉴权 */
                        HUB_Authentication();
                        DEBUG_CONF("\n/------------------------i'm authentication....--------------------------------/\n");
                        /* 鉴权应答 */
                        HUB_Authentication_Ack();       

                }
                else /* 鉴权成功 */
                {
                         /* 解锁 */                       
                        pthread_rwlock_unlock(&rw__auth_success_flag);  
                        
                        DEBUG_CONF("\n/------------------------Re-Register Server successful!-------------------------/\n");  
                        /* 跳出循环 */
                        i = 4;
                }
                
        }

        /* 鉴权成功后释放锁 */
        pthread_rwlock_unlock(&rw_close_socket_flag);
        

}

/*!
 * \brief:    打开数据库                            
 *
 * \param [IN]：NULL
 * 
 * \Returns   ：NULL
 * 
 */

 void S_sqlite3_open(const char *filename , sqlite3 **ppDb)
 {

        int rc;
        /* 打开数据库：hub.db */
        rc = sqlite3_open(filename , ppDb);

        if ( rc != SQLITE_OK ){

                DEBUG_CONF( "Can't open database: %s\n",sqlite3_errmsg(ppDb) );
                //exit(0);

        } else {

                DEBUG_CONF( "Open database successfully\n");            
        }
 }



#if 1
/*  test 函数:  回调数据库中函数。查看创建的表  */
static int test_callback(void *data, int argc, char **argv, char **azColName)
{
   int i;
   //fprintf(stderr, "%s: ", (const char*)data);

   for(i=0; i<argc; i++)
   {

      DEBUG_CONF("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   
   }
   
   DEBUG_CONF("\n");
   return 0;

}
#endif


/**
 * breif:       HUB端注册信息初始化
 * 
 * parameter:   NULL
 * 
 * return   :   NULL 
*/

void HUB_Register_Init(void)
{
        
        char *sql = NULL;
        int rc;   

        /* 创建记录hub鉴权码的数据库 */

        sql = "CREATE TABLE IF NOT EXISTS Hub_Register(ID INT PRIMARY KEY NOT NULL, Auth_Code TEXT);";

        rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg);
        if(rc != SQLITE_OK){
                fprintf(stderr,"SQL error: %s\n",zErrMsg);
                sqlite3_free(zErrMsg);
        }else{
                fprintf(stdout,"Table created successfully\n");            
        }


        /* 向数据库中插入主键和初始化鉴权码 */
        sql = "INSERT INTO Hub_Register(ID,Auth_Code)VALUES(1,'000000');";
        rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg);
        if(rc != SQLITE_OK){
                fprintf(stderr,"SQL error: %s\n",zErrMsg);
                sqlite3_free(zErrMsg);
        }else{
                fprintf(stdout,"Insert hub_register info successfully\n");            
        }

        
        /*-------------------------- sqlite debug --------------------------  */

        #if 1
        
        sql = "SELECT *FROM Hub_Register";
                 
        /* 执行SQL语句 */
        rc = sqlite3_exec(db,sql,test_callback,0,&zErrMsg);

        if ( rc != SQLITE_OK ) {

                DEBUG_CONF("SQL error: %s\n",zErrMsg);
                sqlite3_free(zErrMsg);

        } else {

                DEBUG_CONF("Hub_Register Table watch successfully\n");            
        }
        #endif

        /*-----------------------------------------------------------------------*/


}


/*----------------------------------------------------------------------End of-------------------------------------------------------------------------------------------*/