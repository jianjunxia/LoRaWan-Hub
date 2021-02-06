/*
    ____               _    _      _         _
  / ____|             | |  | |    (_)       | |
 | (___    ___  _ __  | |_ | |__   _  _ __  | | __
  \___ \  / _ \|  _ \ | __||  _ \ | ||  _ \ | |/ /
  ____) ||  __/| | | || |_ | | | || || | | ||   <
 |_____/  \___||_| |_| \__||_| |_||_||_| |_||_|\_\


Description:
            模拟后台服务器，进行数据传输的测试使用


Transport Protocols: LoRaWAN-HUB与服务器通信协议  TCP传输协议
Lierda |  Senthink
autor:  jianjun_xia              
data :  2018.10.9

*/

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

//==================================上位机配置使用的一些宏定义=====================================//
//宏定义,调试配置代码使用
#define  _DEBUG_CONF_                             
#ifdef   _DEBUG_CONF_    
    #define DEBUG_CONF(fmt,args...)       fprintf(stderr,"%d: "fmt,__LINE__,##args)
#else
    #define DEBUG_CONF(args...)           //fprintf(stderr,args)
#endif
//定义udp的端口为6789
#define  TCP_PORT           8888
#define  BUFF_SIZE          1000
//==============================================================================================//
//=========================================公共函数区域===========================================//

//转义还原
int Transfer_Restore(uint8_t *dst,const uint8_t *src,int size);
//计算校验码
uint8_t Check_Xor(const uint8_t *p,int size);

//==============================================================================================//

//socket-->bind-->listen-->accept-->read-->close

int main(int argc, char const *argv[])
{
    /* code */
    // 监听套接字
    int sock_fd;
    // 已连接套接字
    int new_fd;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    int ret;
    int addr_len;
    int recv_len;
    int send_len;
    uint8_t recv_buff[BUFF_SIZE];
    memset(recv_buff,0,1000);
    uint8_t message_buff[BUFF_SIZE];
    memset(message_buff,0,1000);
    int message_len = 0;
    int client_num = -1;
    uint8_t mic = 0;
    int loop;
    char ack_data[] = "I have received the data";

    //socket
    sock_fd = socket(AF_INET,SOCK_STREAM,0); //AF_INET: IPV4; SOCK_STREAM: TCP
    if(-1 == sock_fd)
    {
        fprintf(stderr,"socket error: %s\n",strerror(errno));
        exit(1);
    }

    //set server sockaddr_in
    memset(&server_addr,0,sizeof(struct sockaddr_in)); // clear
    server_addr.sin_family      = AF_INET;           // IPV4
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // all ip
    server_addr.sin_port        = htons(TCP_PORT);

    //bind
    ret = bind(sock_fd,(struct sockaddr*)(&server_addr),sizeof(struct sockaddr));
    if(-1 == ret)
    {
        fprintf(stderr,"bind error: %s\n",strerror(errno));
        close(sock_fd);
        exit(1);
    }

    //listen
    ret = listen(sock_fd,128);
    if(-1 == ret)
    {
        fprintf(stderr,"listen error: %s\n",strerror(errno));
        close(sock_fd);
        exit(1);
    }

    while(1)
    {
        addr_len = sizeof(struct sockaddr);
        //此处返回新的已连接的套节字
        //阻塞等待连接
        new_fd = accept(sock_fd,(struct sockaddr*)&client_addr,&addr_len);

        if(-1 == new_fd)
        {
            fprintf(stderr,"accept error:%s\n\a", strerror(errno));
            close(sock_fd);
            exit(1);     
        }
        client_num++;
        fprintf(stderr,"Server get connetion from client%d:%s\n",client_num,inet_ntoa(client_addr.sin_addr));

        if(!fork())
        {
            //child process
            while(1)
            {
                recv_len = recv(new_fd,recv_buff,1000,0);
                if(recv_len <= 0)
                {
                    fprintf(stderr, "recv error:%s\n\a", strerror(errno));
                    close(new_fd);    
                    exit(1);
                }
                else
                {
                    //转义还原 --> 验证校验码 --> 解析消息 
                    //转义从消息头开始
                    message_len = Transfer_Restore(message_buff+1,recv_buff+1,recv_len-1);
                    message_buff[0] = recv_buff[0];
                    mic = Check_Xor(message_buff,message_len);
                    //验证校验码
                    if(message_buff[message_len] ==  mic)
                    {
                        //校验码通过，打印数据
                        for(loop = 0; loop <=message_len; loop++)
                        {
                            DEBUG_CONF("recv data[%d]: %02x\n",loop,message_buff[loop]);
                        }
                        send_len = send(new_fd,ack_data,24,0);
                    }   
                    else
                    {
                        DEBUG_CONF("sorry mic error\n");
                    }         
                }  
            }
        }      
    }
    return 0;
}
// 函数作用    : 对封装好的消息体进行转义还原
// 输入参数    : 还原处理后的缓存数组地址，要进行还原处理的数组地址，要进行还原处理的数组大小
// 返回值      : 还原后的数组大小
// 转义还原规则 : 将消息头或者校验码中出现的0X7D、0X02还原为0X7E
//              将消息头或者校验码中出现的0X7D、0X01还原为0X7D
int Transfer_Restore(uint8_t *dst,const uint8_t *src,int size)
{
    int src_len = 0;
    int dst_len = 0;
    int count   = 0;
    while(size--)
    {
        if( (src[src_len]==0x7d && src[src_len+1]==0x02) || (src[src_len]==0x7d && src[src_len+1]==0x01) )
        {
            if(src[src_len]==0x7d && src[src_len+1]==0x02)
            {

                dst[dst_len] = 0x7e;
                //跳过0x02
                src_len++;
                //记录出现多少个 0x7e或0x7d
                count++;
            }
            else
            {
                dst[dst_len] = 0x7d;
                //跳过0x01
                src_len++;
                //记录出现多少个 0x7e或0x7d
                count++;
            }
        }
        else
        {
            dst[dst_len] = src[src_len];
        }
        src_len++;
        dst_len++;
    }
    dst_len = dst_len - count;
    return dst_len;
}

// 函数作用:  计算校验码
// 输入参数： 校验数组地址，校验数组大小
// 返回值  ： 校验值
//校验码计算方法： 从消息头开始，同后一字节进行异或，直到校验码前一个字节
uint8_t Check_Xor(const uint8_t *p,int size)
{
    int     len  = 0;
    uint8_t buff = 0;
    buff = p[len++];
    //需要异或的次数
    size = size-1;
    while(size--)
    {
        buff = buff^p[len++];
    }
    
    return buff;
}

 