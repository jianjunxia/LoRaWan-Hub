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
#include <sys/time.h>

//==================================服务器配置使用的一些宏定义=====================================//
//宏定义，调试配置代码使用
#define   _DEBUG_CONF_

#ifdef   _DEBUG_CONF_
         #define  DEBUG_CONF(fmt,args...)   fprintf(stderr,"[%d]: "fmt,__LINE__,##args)
#else
        #define   DEBUG_CONF(fmt,args...)
#endif

//定义本地通信端口
//本地通信指的是：与lora_pkt_fwd 之间的数据通信
//IP使用本地地址:127.0.0.1

#define     LOCAL_PORT    5555
#define     BUFF_LEN      1000
#define     LOCAL_IP      "127.0.0.1"    

#define     TX_BYTE_LEN     100

pthread_t   thread_printf;
pthread_t   thread_send;

void *Thread_Print();
void *Thread_Send(void *rxbuff);

int main(int argc, char const *argv[])
{
    int err;

    uint8_t rxbuff[5] =
    {    
        0x34,0x35,0x36,0x37,0x38

    } ;

 
 
        err = pthread_create(&thread_printf,NULL,Thread_Print,NULL);
        if(err != 0)
        {
            DEBUG_CONF("can't create task thread\n ");
        }
        else
        {
            DEBUG_CONF("task thread1  create successfully\n");
        }
        
        #if 0
      
        err = pthread_create(&thread_send,NULL,Thread_Send,rxbuff);
        if(err != 0)
        {
            DEBUG_CONF("can't create send join info pthread!\n ");
        }
        else
        {
            DEBUG_CONF("send join infothread3reate successfully\n");
        }
        #endif

      // sleep(1);

    pthread_join(thread_printf, NULL);
    //pthread_join(thread_send, NULL);
    return 0;
}

void *Thread_Print()
{

    printf("hello,world\n");
    while(1)
    {
        sleep(2);
        printf("HAHAHAHAHAHAHHAHAHA\n");
    }

    //pthread_exit(0);
}

void *Thread_Send(void *rxbuff)
{
 
    //创建与lora_pkt_server传输数据的udp连接
    int sock_pkt_server_fd;
    struct  sockaddr_in pkt_server_addr;  

    //lora_pkt_fwd <------> lora_pkt_server transport protocol: udp
    sock_pkt_server_fd = socket(AF_INET,SOCK_DGRAM,0);
    if(-1 == sock_pkt_server_fd)
    {
        DEBUG_CONF(" socket error!\n");
        close(sock_pkt_server_fd);
        exit(1);
    }
    //set sockaddr_in parameter
    memset(&pkt_server_addr,0,sizeof(struct sockaddr_in));
    pkt_server_addr.sin_family      = AF_INET;
    pkt_server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");   
    pkt_server_addr.sin_port        = htons(5555);

    int len = 0;
    len = sendto(sock_pkt_server_fd,rxbuff,5,0,(struct sockaddr*)&pkt_server_addr,sizeof(struct sockaddr_in));
    DEBUG_CONF("IP: %s\n",inet_ntoa(pkt_server_addr.sin_addr));
    DEBUG_CONF("sockfd:  %d\n",sock_pkt_server_fd);   
    
    if(len <= 0)
    {            
        DEBUG_CONF("can't send join data\n");
        close(sock_pkt_server_fd);
        pthread_exit(0);  
    }

    //memset(&pkt_server_addr,0,sizeof(struct sockaddr_in));
    close(sock_pkt_server_fd);
    pthread_exit(0);

}