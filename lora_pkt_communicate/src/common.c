#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include "common.h"

/* 返回为转换后的字符串首地址 */
char *ChArray_To_String(char *dst, uint8_t *src, size_t size)
{    
    int  loop;
    char *p_buff = dst;
    //将字符数组转换成字符串:重要转换,自定义转换过程
    for(loop = 0; loop < size;loop++)
    {
        //%02x:表示一次将打印2个字符，左边为0则补0
        sprintf(dst,"%02x",src[loop]);
        //每次地址+2
        dst+=2;            
    }
    return p_buff;
}

/* 定义字符转换成数值函数 */
uint8_t ascii2hex(uint8_t src)
{
    uint8_t temp = 0;
    if(src == '\0')
    {
        printf("The strings is end\n");
    }
    else if(src >='0' && src <='9')
    {
         temp = src - '0';   
    }
    else if(src >='a' && src <= 'f')
    {
         temp = src - 'a' + 10;  
    }
    else if(src >= 'A' && src <= 'F')
    {
         temp = src - 'A' +10;
    }
    else
    {
        printf("Please input right strings");
    }
    return temp;
} 

/* 字符串转换为字符数组存储的16进制数值 */
void String_To_ChArray( uint8_t *dst,const char*src,size_t size)
{ 
   int loop;
   for(loop=0;loop < size;loop++) 
   {
 
        dst[loop]  = (uint8_t)ascii2hex(src[loop*2])<<4;
        dst[loop] += (uint8_t)ascii2hex(src[loop*2+1]);

   }

}

/* 拷贝函数 */
void mymemcpy(uint8_t *dst,const uint8_t *src,int size)
{
    while(size--)
    {
        *dst++ = *src++;        
    }
}

/* 倒序拷贝函数 */
void mymemccpy(uint8_t *dst,const uint8_t *src,int size)
{
    uint8_t *p_buff = NULL;
    p_buff = (uint8_t*)src + size -1;
    while(size--)
    {
        *dst++ = *p_buff--;        
    }
}

/*
    函数作用:  计算校验码
    输入参数： 校验数组地址，校验数组大小
    返回值  ： 校验值
    校验码计算方法： 从消息头开始，同后一字节进行异或，直到校验码前一个字节
*/
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

/*
    函数作用    : 对封装好的消息体进行转义处理
    输入参数    : 转义处理后的缓存数组地址，要进行转义处理的数组地址，要进行转义处理的数组大小
    返回值      : 转义处理后的数组大小
    转义处理规则 : 将消息头或者校验码中出现的0X7E转义为0X7D，并在其后紧跟一个0X02，
                 将消息头或者校验码中出现的0X7D转义为0X7D，并在其后紧跟一个0X01，
*/
int Transfer_Mean(uint8_t *dst,const uint8_t *src,int size)
{
    int src_len = 0;
    int dst_len = 0;
    while(size--)
    {
        if( (src[src_len] == 0x7e) || (src[src_len] == 0x7d) )
        {
            if( src[src_len] == 0x7e)
            {

                dst[dst_len++] = 0x7d;
                dst[dst_len]   = 0x02;
            }
            else
            {
                dst[dst_len++] = 0x7d;
                dst[dst_len]   = 0x01;
            }
        }
        else
        {
            dst[dst_len] = src[src_len];
        }
        src_len++;
        dst_len++;
    }

    return dst_len;
}

/* 
    函数作用    : 对封装好的消息体进行转义还原
    输入参数    : 还原处理后的缓存数组地址，要进行还原处理的数组地址，要进行还原处理的数组大小
    返回值      : 还原后的数组大小
    转义还原规则 : 将消息头或者校验码中出现的0X7D、0X02还原为0X7E
                 将消息头或者校验码中出现的0X7D、0X01还原为0X7D
*/
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

/* ascii 转 hex */
void Ascii_To_Hex( uint8_t *dst,const char *src, int len)
{
   int loop;
   for(loop=0;loop<len;loop++)
   {
      dst[loop]  = (uint8_t)asciitohex(src[loop]);
   }
}

uint8_t asciitohex(uint8_t src)
{
    uint8_t temp = 0;
    if(src == '\0')
    {
        return temp;
    }
    else
    {
        return src;
    }

}

/* 将hex转换成ascii中的字符 */
char *Hex_To_String( char *dst,uint8_t *src,size_t size )
{
    int  loop;
    char *p_buff = dst;
    //将字符数组转换成字符串:重要转换,自定义转换过程
    for(loop = 0; loop < size;loop++)
    {
        //%02x:表示一次将打印2个字符，左边为0则补0
        sprintf(dst,"%c",src[loop]);
        //每次地址+1
        dst++;
    }
    *(p_buff+size) = '\0';
    return p_buff;
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


/**
 *  brief:     debug  打印到输出文件中
 * 
 *  parameter: 文件指针 ，打开方式,  .....
 * 
 *  return:    成功: 0  失败: 1
 * 
 *  
 */
#if 0
int S_write_log (FILE* pFile, const char *format, ...)
{
    va_list arg;
	int done;

	va_start (arg, format);
	//done = vfprintf (stdout, format, arg);

	time_t time_log = time(NULL);
	struct tm* tm_log = localtime(&time_log);
	fprintf(pFile, "%04d-%02d-%02d %02d:%02d:%02d ", tm_log->tm_year + 1900, tm_log->tm_mon + 1, tm_log->tm_mday, tm_log->tm_hour, tm_log->tm_min, tm_log->tm_sec);

	done = vfprintf (pFile, format, arg);
	va_end (arg);

	fflush(pFile);
	return done;
}
#endif