
// Lierda | Senthink
// Jianjun_xia 
// date: 2018.10.10

#ifndef  _COMMON_H_    
#define  _COMMON_H_
    
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include<sys/time.h>
#include <stdarg.h>
#include <time.h>


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

/*----------------------------------------------------公共函数--------------------------------------------------*/ 
char* ChArray_To_String(char*dst,uint8_t src[],size_t size);
uint8_t ascii2hex(uint8_t src);
void String_To_ChArray( uint8_t dst[],const char*src,size_t size);
void mymemcpy (uint8_t dst[],const uint8_t src[],int size);
void mymemccpy(uint8_t dst[],const uint8_t src[],int size);
//计算校验码
uint8_t Check_Xor(const uint8_t *p,int size);
//转义处理函数
int Transfer_Mean(uint8_t *dst,const uint8_t *src,int size);
//转义还原
int Transfer_Restore(uint8_t *dst,const uint8_t *src,int size);
//ascii 转hex 
void Ascii_To_Hex( uint8_t dst[],const char*src, int len);
uint8_t asciitohex(uint8_t src);
//将hex转换成ascii中的字符
char* Hex_To_String(char*dst,uint8_t src[],size_t size);
//16进制转换成字符串
int HexToChar( unsigned char buff[],int size );


/**
 *  brief:     两个数组值比较
 * 
 *  parameter: 源地址， 目标地址,比较数值大小 
 * 
 *  return:   完全相同 返回0  不相同返回1
 * 
 *  
 */
int  ArrayValueCmp (uint8_t *dst, uint8_t *src, int size);

/*-----------------------------------------------------------------------------------------------------------------*/

/**
 *  brief:     debug  打印到输出文件中
 * 
 *  parameter: 文件指针 ，打开方式,  .....
 * 
 *  return:    成功: 0  失败: 1
 * 
 *  
 */
//int S_write_log (FILE* pFile, const char *format, ...);


#endif