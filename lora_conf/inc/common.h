// Lierda | Senthink
// Jianjun_xia 
// date: 2018.10.11

#ifndef  _COMMON_H_    
#define  _COMMON_H_
    
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define   uint8_t      unsigned char
#define   uint16_t     unsigned short int
#define   uint32_t     unsigned int

//宏定义，调试配置代码使用
#define   _DEBUG_SERVER_

#ifdef   _DEBUG_SERVER_
         #define  DEBUG_SERVER(fmt,args...)   fprintf(stderr,"[%d]: "fmt,__LINE__,##args)
#else
        #define   DEBUG_SERVER(fmt,args...)
#endif

//宏定义，调试配置代码使用
#define   _DEBUG_CONF_

#ifdef   _DEBUG_CONF_
         #define  DEBUG_CONF(fmt,args...)   fprintf(stderr,"[%d]: "fmt,__LINE__,##args)
#else
        #define   DEBUG_CONF(fmt,args...)
#endif

//===========================公共使用函数声明========================================//

//time caculation
//long timecacul();
//字符数组转换为字符串函数
//字符数组中存的值为16进制
char* ChArray_To_String(char*dst,uint8_t src[],size_t size);
//定义字符转换成数值函数
uint8_t ascii2hex(uint8_t src);
//字符串转换为字符数组中存储的16进制数值
void String_To_ChArray( uint8_t dst[],const char*src,size_t size);
//拷贝函数
void mymemcpy(uint8_t dst[],const uint8_t src[],int size);
//倒序拷贝函数
void mymemccpy(uint8_t dst[],const uint8_t src[],int size);
//将字符串IP地址转换成hex类型
void Ip_to_Charray(uint8_t dst[],const char*src,int size);

//累加和,MIC生成函数
//输入要校验的字符数组地址，和大小
//返回值为生成的MIC
uint8_t checksum(uint8_t *buf, int nword);

//校验MIC函数
//输入MIC值，接收的字符数组地址和大小
//若返回值为0xff则校验正确
uint8_t checkout(uint8_t *mic,uint8_t*src,int size);
//=================================================================================//

#endif