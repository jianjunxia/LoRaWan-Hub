/*
    测试代码：测试是否能传输ELF文件


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
#include <stdio.h>
#include <stdlib.h>

/* 常用数据类型的宏定义 */
//#define     uint8_t     unsigned char
//#define     uint16_t    unsigned short int
//#define     uint32_t    unsigned int
//#define     int8_t      char
//#define     int16_t     short int
//#define     int32_t     int
int main()
{
   #if 0
    int a = 0x12345678;
    char *p = NULL;
    p = (char*)&a;

    if (*p == 0x12)
    {
        printf("The CPU is big endian\n");

        printf("The a adress is %p\n\n",&a);

        printf("The p value  is 0x%x\n",*p);
        printf("The p adress is %p\n\n",p++);

        printf("The p+1 value  is 0x%x\n",*p);
        printf("The p+1 adress is %p\n\n",p++);

        printf("The p+2 value  is 0x%x\n",*p);
        printf("The p+2 adress is %p\n\n",p++);

        printf("The p+3 value  is 0x%x\n",*p);
        printf("The p+3 adress is %p\n\n",p++);
    }
    if (*p == 0x78)
    {
        printf("The CPU is little endian\n");

        printf("The a adress is %p\n\n",&a);

        printf("The p value  is 0x%x\n",*p);
        printf("The p adress is %p\n\n",p++);

        printf("The p+1 value  is 0x%x\n",*p);
        printf("The p+1 adress is %p\n\n",p++);

        printf("The p+2 value  is 0x%x\n",*p);
        printf("The p+2 adress is %p\n\n",p++);

        printf("The p+3 value  is 0x%x\n",*p);
        printf("The p+3 adress is %p\n\n",p++);

    }
#endif

	while(1)
	{
		printf("hello world! change world!\n");
		sleep(2);		

	}


    return 0;
}
