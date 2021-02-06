//jianjun_xia
//lierda | senthink
//udp_test.c
//date:2018.11.1
//测试lora_pkt_server.c下发服务器的应答数据是否正确

#include<stdlib.h>
#include<errno.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<string.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<stdio.h>
#include<signal.h>

#define port_number 5557

#define uint8_t      unsigned char
#define uint16_t     unsigned short int
#define uint32_t     unsigned int

//宏定义,调试配置代码使用
#define  _DEBUG_CONF_                             
#ifdef   _DEBUG_CONF_    
    #define DEBUG_CONF(fmt,args...)       fprintf(stderr,"%d: "fmt,__LINE__,##args)
#else
    #define DEBUG_CONF(args...)           //fprintf(stderr,args)
#endif


//socket --> bind --> -->recvfrom/send to -->close
int main(int argc, char const *argv[])
{
    int sock_fd;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    int ret;
    int addr_len;
    int recv_len;
    uint8_t recv_buff[1000];
    memset(recv_buff,0,1000);

    //socket
    sock_fd = socket(AF_INET,SOCK_DGRAM,0); //at_inet:ipv4,sock_dgram:udp
    if(-1 == sock_fd)
    {
        DEBUG_CONF("socket error:%s\n",strerror(errno));
        exit(1);
    }

    //set sockaddr_in parameter
    memset(&server_addr,0,sizeof(struct sockaddr_in));//clear
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);//inaddr_any: this machine all ip
    server_addr.sin_port = htons(port_number);

    //bind
    ret = bind(sock_fd,(struct sockaddr*)(&server_addr),sizeof(struct sockaddr));
    if(-1 == ret)
    {
        DEBUG_CONF("bind error:%s\n\a", strerror(errno));
        close(sock_fd);
        exit(1);
    }

    while(1)
    {
        //recvfrom
        addr_len = sizeof(struct sockaddr);
        recv_len = recvfrom(sock_fd, recv_buff, 1000, 0, (struct sockaddr *)&client_addr, &addr_len);

        if(recv_len <= 0)
        {
            DEBUG_CONF("recvfrom error: %s\n",strerror(errno));
            close(sock_fd);
            exit(1);
        }
        else
        {
            DEBUG_CONF("recv data : \n");
            for(int i=0;i<recv_len;i++)
            {
                DEBUG_CONF("recv_buff[%d]:0x%x\n",i,recv_buff[i]);
            }
        }
    }
    //close
    close(sock_fd);
    return 0;
}
