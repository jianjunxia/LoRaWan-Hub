/*
    
    tcp_client.c
    tcp 测试代码，测试是否能够传输ELF文件 

    jainjun_xia
    date: 2019.5.22

*/

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <memory.h> 
#include <stdlib.h>
#include <sys/errno.h>

#define PORT_NUMBER 5678

/* wirte函数最多一次写入8192个字节 */
#define BUF_SIZE  (8192)
unsigned char fileBuf[BUF_SIZE];

/* socket->connect->send->close */

int 
main(int argc, char *argv[])
{
    int sock_fd;
    struct sockaddr_in server_addr;
    int ret;
    unsigned char *send_buf = "hello,tcp! i'm test transport ELF file";
    int send_len;
    int lenpath;

    /* 文件相关变量 */
    char    filepath[100];
    FILE    *fp;
    ssize_t fileTrans;
    ssize_t netSize;
    unsigned int fileSize;
    unsigned char buf[10];

    /* socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);//AF_INET:IPV4;SOCK_STREAM:TCP
    if (-1 == sock_fd)
    {
       fprintf(stderr,"socket error:%s\n\a", strerror(errno));
       exit(1);
    }

    /* set sockaddr_in parameter*/
    memset(&server_addr, 0, sizeof(struct sockaddr_in));//clear
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT_NUMBER);
    ret = inet_aton("192.168.1.104", &server_addr.sin_addr);

    if ( 0 == ret)
    {
        fprintf(stderr,"server_ip error.\n");
        close(sock_fd);
        exit(1);
    }

    /* connect */
    ret = connect(sock_fd, (const struct sockaddr *)&server_addr, sizeof(struct sockaddr));    
    if (-1 == ret)
    {
        fprintf(stderr,"connect error:%s\n\a", strerror(errno));
        close(sock_fd);
        exit(1);
    }

    /* 输入文件地址 */
    printf("file path:\n");
    scanf("%s",filepath);
    fp = fopen(filepath,"r+");//opne file
    if ( fp==NULL)
    {
        fprintf(stderr,"file open error:%s\n\a", strerror(errno));
        return 0;

    }
    printf("filepath : %s\n",filepath);

    /* 计算文件大小 */
    fseek ( fp, 0, SEEK_END);
    fileSize = ftell(fp);
    
    /* 将文件指针移动到文件头 */
    fseek ( fp, 0, SEEK_SET);

    /* 发送文件大小 */
    if ( write(sock_fd, (unsigned char *)&fileSize, 4) != 4) 
    {        
     
            perror("write");        
            close(sock_fd);           
            exit(1);    
    
    }

    /* 查看server端是否回复了 "OK" */
    if ( read(sock_fd, buf, 2) != 2) 
    {
            perror("read");
            close(sock_fd);
            exit(1);
    }

    printf("buf: %s\n",buf);

    sleep(3);
 
    while ( (fileTrans = fread(fileBuf,sizeof(char),BUF_SIZE,fp)) > 0)
    {
        printf("fileTrans =%ld\n",fileTrans);
        unsigned int size = 0;

        while( size < fileTrans )
        {
           
            if( (netSize = write(sock_fd, fileBuf + size, fileTrans - size) ) < 0 ) 
            {               
                    perror("write");               
                    close (sock_fd);                           
                    exit(1);            
            }
            
            size += netSize;

        }
        
    }

    fclose(fp);
    /* close */
    close(sock_fd);
    exit(0);

}






