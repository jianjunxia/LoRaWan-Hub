/*
    lierda | senthink
    Jianjun_xia 
    Update: 2018.10.11

*/

#ifndef  _COMMON_H_    
#define  _COMMON_H_
    
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define   uint8_t      unsigned char
#define   uint16_t     unsigned short int
#define   uint32_t     unsigned int

/* 宏定义,调试解析代码使用 */
//#define  _DEBUG_                             
#ifdef   _DEBUG_    
    #define DEBUG(fmt,args...)       fprintf(stderr,"%d: "fmt,__LINE__,##args)
#else
    #define DEBUG(args...)           //fprintf(stderr,args)
#endif

/*  宏定义，调试配置代码使用 */
#define   _DEBUG_SERVER_
#ifdef    _DEBUG_SERVER_
         #define  DEBUG_SERVER(fmt,args...)   fprintf(stderr,"[%d]: "fmt,__LINE__,##args)
#else
        #define   DEBUG_SERVER(fmt,args...)
#endif

/*  宏定义，调试配置代码使用  */
//#define   _DEBUG_CONF_
#ifdef   _DEBUG_CONF_
         #define  DEBUG_CONF(fmt,args...)   fprintf(stderr,"[%d]: "fmt,__LINE__,##args)
#else
        #define   DEBUG_CONF(fmt,args...)
#endif

/*----------------------------------------------公共函数-------------------------------------------------*/ 
//字符数组转换为字符串函数
//字符数组中存的值为16进制
char* ChArray_To_String(char*dst,uint8_t src[],size_t size);
//定义字符转换成数值函数
uint8_t ascii2hex(uint8_t src);
//字符串转换为字符数组中存储的16进制数值
void String_To_ChArray( uint8_t dst[],const uint8_t*src,size_t size);
//拷贝函数
void mymemcpy(uint8_t dst[],const uint8_t src[],int size);
//倒序拷贝函数
void mymemccpy(uint8_t dst[],const uint8_t src[],int size);
//16进制转换成字符串
int HexToChar( unsigned char buff[],int size );
//float型数据转换成4字节16进制数
void  FloatToByte(float *a,uint8_t *byteArray);
//4字节转换成float型数据
void  ByteToFloat(uint8_t *byteArray,float *a);
/* 两个数组值比较 */
int  ArrayValueCmp (uint8_t *dst, uint8_t *src, int size);
/*---------------------------------------------------------------------------------------------------------*/

#endif