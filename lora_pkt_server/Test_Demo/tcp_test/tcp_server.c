/*
    
    tcp_server.c
    tcp 测试代码，测试是否能够传输ELF文件 
    
    jainjun_xia
    date: 2019.5.22

*/

#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>      
#include <sys/socket.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>


#define PORT_NUMBER 5678
#define BACKLOG     10

/* write函数最多一次写入8192字节 */
#define BUF_SIZE (8192)
unsigned char fileBuf[BUF_SIZE];

/* socket->bind->listen->accept->send/recv->close*/

int 
main(int argc, char **argv)
{
    int sock_fd, new_fd ;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    int ret;
    int addr_len;
    int recv_len;
    unsigned char recv_buf[1000];
    int client_num = -1;

    /* 文件传输变量 */
    FILE *fp;
    char *path = "test_hello";
    ssize_t  recv_size;
    ssize_t  nodeSize;
    
    /* 文件总共大小 */
    unsigned int      fileSize;

    /* 接收文件大小 */
    unsigned int Recv_fileSize;

    /* socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);//AF_INET:IPV4;SOCK_STREAM:TCP
    if (-1 == sock_fd)
    {
        fprintf(stderr,"socket error:%s\n\a", strerror(errno));
        exit(1);
    }

    /* set server sockaddr_in */
    memset(&server_addr, 0, sizeof(struct sockaddr_in));//clear
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);//INADDR_ANY:This machine all IP
    server_addr.sin_port = htons(PORT_NUMBER);

    /* bind */
    ret = bind(sock_fd, (struct sockaddr *)(&server_addr), sizeof(struct sockaddr));
    if ( -1 == ret)
    {
        fprintf(stderr,"bind error:%s\n\a", strerror(errno));
        close(sock_fd);
        exit(1);
    }

    /* listen */
    ret = listen(sock_fd, BACKLOG);
    if (-1 == ret)
    {
        fprintf(stderr,"listen error:%s\n\a", strerror(errno));
        close(sock_fd);
        exit(1);
    }
    addr_len = sizeof(struct sockaddr);
    new_fd = accept(sock_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (-1 == new_fd)
    {
        fprintf(stderr,"accept error:%s\n\a", strerror(errno));
        close(sock_fd);
        exit(1);
    }

    fprintf(stderr, "Server get connetion form client%d: %s\n", client_num, inet_ntoa(client_addr.sin_addr));


    /* 首先接收到文件大小 */    
    recv_size = read(new_fd, (unsigned char *)&fileSize, 4);    
    if ( recv_size != 4 ) 
    {        
            fprintf(stderr,"file size is error:%s\n\a", strerror(errno));
            close(new_fd);    
            exit(-1);    

    }    
    printf("file size:%d\n", fileSize);
    
    printf("begin start recv  file!\n");

    /* 传输给client端 "OK" 标识 */
    if ( (recv_size = write(new_fd, "OK", 2) ) < 0 ) 
    {
            perror("write");
            close(new_fd);
            exit(1);
    }

    /* 打开文件，给文件赋最大权限 */
    fp = fopen (path, "wb+");
    
    /* 给文件赋予权限 */    
     if ( -1 == chmod ( path, S_IRUSR | S_IWUSR | S_IXUSR |
                  S_IRGRP | S_IROTH | S_IXOTH |
                  S_IROTH | S_IWOTH | S_IXOTH 
          )
     ) /* rwx - rwx - rwx */
            perror("error");

    if (fp == NULL)
            perror("open");

    Recv_fileSize = 0;
    while ( (recv_size = read(new_fd, fileBuf, sizeof(fileBuf))) > 0) 
    {        
            
            unsigned int size_count = 0;        
            while ( size_count < recv_size ) 
            {            
                if ( (nodeSize = fwrite(fileBuf + size_count, 1, recv_size - size_count, fp) ) < 0 ) 
                {        
                    perror("write");                
                    close(new_fd);                
                    exit(1);            
                }            
                
                size_count += nodeSize;        
            }        
            
            Recv_fileSize += recv_size;        
            
            /* 文件接收完毕，断开 */
            if ( Recv_fileSize >= fileSize) 
                                    break;       
                   
            memset(fileBuf, 0, sizeof(fileBuf));

    }

   
    fclose(fp);
    /* close */
    close(new_fd);
    close(sock_fd);
    exit(0); 

}