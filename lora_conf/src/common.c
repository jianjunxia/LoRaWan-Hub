// Lierda | Senthink
// Jianjun_xia 
// date: 2018.10.11
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

//返回为转换后的字符串首地址
char* ChArray_To_String(char*dst,uint8_t src[],size_t size)
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

//定义字符转换成数值函数
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

//字符串转换为字符数组存储的16进制数值
void String_To_ChArray( uint8_t dst[],const char*src,size_t size)
{ 
   int loop;
   for(loop=0;loop < size;loop++) 
   {
      //从字符串中解析数值时，存在解析顺序问题：从低地址解析不会出错
      //自己定义解析函数
      // sscanf(src,"%02x",(unsigned int*)&dst[loop]);
      // src+=2;   
     dst[loop]  = (uint8_t)ascii2hex(src[loop*2])<<4;
     dst[loop] += (uint8_t)ascii2hex(src[loop*2+1]);

   }
}

//拷贝函数
void mymemcpy(uint8_t dst[],const uint8_t src[],int size)
{
    while(size--)
    {
        *dst++ = *src++;        
    }
}

//倒序拷贝函数
void mymemccpy(uint8_t dst[],const uint8_t src[],int size)
{
    uint8_t *p_buff = NULL;
    p_buff = (uint8_t*)src + size -1;
    while(size--)
    {
        *dst++ = *p_buff--;        
    }
}

//function : 将字符串IP地址转换成hex类型
//parameter: dst:存储转换成hex类型的值(4个字节)，src:ip首地址,字符串类型,size:字符串大小
//
void Ip_to_Charray(uint8_t dst[],const char*src,int size)
{
     char    *src_buff = NULL;
     uint8_t *dst_buff = NULL;
     src_buff = src;
     dst_buff = dst;
     uint32_t temp = 0;
     int index = 0;

     while(size--)
     {
         //DEBUG_CONF(" src_buff: %c\n",*src_buff);
        //如果输入的字符为"."则跳过
        if(*src_buff == '.')
        {
            dst[index] = temp;
            index++;
            temp = 0;
            src_buff++;
            continue;
        }
        else
        {
            if(*src_buff >= '0' && *src_buff <= '9')
            {
                //核心语句
                temp = temp*10+((*src_buff) - '0');
                dst_buff++;
            }
            else
            {
                DEBUG_CONF("Please check the address for errors\n");
            }
        }
        src_buff++;
    }
    //最后一个'.'后面的数字
    dst[index] = temp;
    temp = 0;
}

uint8_t checksum(uint8_t *buf, int nword)
{
    unsigned long long sum;
    uint8_t  mic=0;

    for(sum = 0; nword > 0; nword--)
    {
        sum += *buf++;              /*获取buf的累加和*/
    }
    //DEBUG_CONF("sum values； 0x%x\n",sum);
    mic = sum%256;
    //DEBUG_CONF("mic values； 0x%x\n",mic);
    return ~mic;
}

uint8_t checkout(uint8_t *mic,uint8_t*src,int size)
{
    uint8_t buff =0;
    
    for(buff =0;size>0;size--)
    {
        buff+=*src++;
    }
    buff = buff%256;
    buff = buff + (*mic);
    return buff;
}