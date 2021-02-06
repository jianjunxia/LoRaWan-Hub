// Lierda | Senthink
// Jianjun_xia 
// date: 2018.10.11

/**
 * Function:公共函数的实现  
 * 
 * */


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
void String_To_ChArray( uint8_t dst[],const uint8_t*src,size_t size)
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
//=================================================================================//
//
/** \brief // 16进制转换成字符
 *
 * \param   输入1个十六进制数值
 * \param
 * \return  -1：fail  0：successful
 *
 */
int HexToChar( unsigned char buff[],int size )
{
	
	for(int i = 0; i < size; i++)
	{
			//判断是否是数字 0~9
		if((buff[i] >=0x30) && (buff[i] <= 0x39))
		{
			switch(buff[i])
			{
			   case 0x30:  buff[i] = '0';break;
			   case 0x31:  buff[i] = '1';break;
			   case 0x32:  buff[i] = '2';break;
			   case 0x33:  buff[i] = '3';break;
			   case 0x34:  buff[i] = '4';break;
			   case 0x35:  buff[i] = '5';break;
			   case 0x36:  buff[i] = '6';break;
			   case 0x37:  buff[i] = '7';break;
			   case 0x38:  buff[i] = '8';break;
			   case 0x39:  buff[i] = '9';break;

			   default:
					   return -1;
					   break;
			}
		}
			//判断是否是输入的大写字母
		else if ((buff[i] >=0x41) && (buff[i] <= 0x5a))
		{
			switch(buff[i])
			{
			   case 0x41:  buff[i] = 'A';break;
			   case 0x42:  buff[i] = 'B';break;
			   case 0x43:  buff[i] = 'C';break;
			   case 0x44:  buff[i] = 'D';break;
			   case 0x45:  buff[i] = 'E';break;
			   case 0x46:  buff[i] = 'F';break;
			   case 0x47:  buff[i] = 'G';break;
			   case 0x48:  buff[i] = 'H';break;
			   case 0x49:  buff[i] = 'I';break;
			   case 0x4a:  buff[i] = 'J';break;
			   case 0x4b:  buff[i] = 'K';break;
			   case 0x4c:  buff[i] = 'L';break;
			   case 0x4d:  buff[i] = 'M';break;
			   case 0x4e:  buff[i] = 'N';break;
			   case 0x4f:  buff[i] = 'O';break;
			   case 0x50:  buff[i] = 'P';break;
			   case 0x51:  buff[i] = 'Q';break;
			   case 0x52:  buff[i] = 'R';break;
			   case 0x53:  buff[i] = 'S';break;
			   case 0x54:  buff[i] = 'T';break;
			   case 0x55:  buff[i] = 'U';break;
			   case 0x56:  buff[i] = 'V';break;
			   case 0x57:  buff[i] = 'W';break;
			   case 0x58:  buff[i] = 'X';break;
			   case 0x59:  buff[i] = 'Y';break;
			   case 0x5a:  buff[i] = 'Z';break;
	
			   default:
					   return -1;
					   break;
			}
	
		}//判断是否是输入的小写字母
		else if ((buff[i] >=0x61) && (buff[i] <= 0x7a))
		{
			switch(buff[i])
			{
			   case 0x61:  buff[i] = 'a';break;
			   case 0x62:  buff[i] = 'b';break;
			   case 0x63:  buff[i] = 'c';break;
			   case 0x64:  buff[i] = 'd';break;
			   case 0x65:  buff[i] = 'e';break;
			   case 0x66:  buff[i] = 'f';break;
			   case 0x67:  buff[i] = 'g';break;
			   case 0x68:  buff[i] = 'h';break;
			   case 0x69:  buff[i] = 'i';break;
			   case 0x6a:  buff[i] = 'j';break;
			   case 0x6b:  buff[i] = 'k';break;
			   case 0x6c:  buff[i] = 'l';break;
			   case 0x6d:  buff[i] = 'm';break;
			   case 0x6e:  buff[i] = 'n';break;
			   case 0x6f:  buff[i] = 'o';break;
			   case 0x70:  buff[i] = 'p';break;
			   case 0x71:  buff[i] = 'q';break;
			   case 0x72:  buff[i] = 'r';break;
			   case 0x73:  buff[i] = 's';break;
			   case 0x74:  buff[i] = 't';break;
			   case 0x75:  buff[i] = 'u';break;
			   case 0x76:  buff[i] = 'v';break;
			   case 0x77:  buff[i] = 'w';break;
			   case 0x78:  buff[i] = 'x';break;
			   case 0x79:  buff[i] = 'y';break;
			   case 0x7a:  buff[i] = 'z';break;
	
			   default:
					   return -1;
					   break;
			}
		}//判断输入的是否是符号，因符号不如数字，字符常用，所以检索的时候放在后面
		else if ((buff[i] >=0x20) && (buff[i] <= 0x2f))
		{
			switch(buff[i])
			{
			   case 0x20:  buff[i] = ' ';break;
			   case 0x21:  buff[i] = '!';break;
			   case 0x22:  buff[i] = '"';break;
			   case 0x23:  buff[i] = '#';break;
			   case 0x24:  buff[i] = '$';break;
			   case 0x25:  buff[i] = '%';break;
			   case 0x26:  buff[i] = '&';break;
			   case 0x27:  buff[i] = '`';break;
			   case 0x28:  buff[i] = '(';break;
			   case 0x29:  buff[i] = ')';break;
			   case 0x2a:  buff[i] = '*';break;
			   case 0x2b:  buff[i] = '+';break;
			   case 0x2c:  buff[i] = ',';break;
			   case 0x2d:  buff[i] = '-';break;
			   case 0x2e:  buff[i] = '.';break;
			   case 0x2f:  buff[i] = '/';break;

			   default:
					   return -1;
					   break;
			}
		}
		else if ((buff[i] >=0x3a) && (buff[i] <= 0x40))
		{
			switch(buff[i])
			{
			   case 0x3a:  buff[i] = ':';break;
			   case 0x3b:  buff[i] = ';';break;
			   case 0x3c:  buff[i] = '<';break;
			   case 0x3d:  buff[i] = '=';break;
			   case 0x3e:  buff[i] = '>';break;
			   case 0x3f:  buff[i] = '?';break;
			   case 0x40:  buff[i] = '@';break;
	
	
			   default:
					   return -1;
					   break;
			}
		}
		else if ((buff[i] >=0x5b) && (buff[i] <= 0x60))
		{
			switch(buff[i])
			{
			   case 0x5b:  buff[i] = '[';break;
			   case 0x5c:  buff[i] = '\\';break;
			   case 0x5d:  buff[i] = ']';break;
			   case 0x5e:  buff[i] = '^';break;
			   case 0x5f:  buff[i] = '_';break;
			   case 0x60:  buff[i] = '\'';break;


			   default:
					   return -1;
					   break;
			}
		}
		else if ((buff[i] >=0x7b) && (buff[i] <= 0x7e))
		{
			switch(buff[i])
			{
			   case 0x7b:  buff[i] = '{';break;
			   case 0x7c:  buff[i] = '|';break;
			   case 0x7d:  buff[i] = '}';break;
			   case 0x7e:  buff[i] = '~';break;

			   default:
					   return -1;
					   break;
			}
		}
		else
		{
				return -1;
		}
		   // 末尾添0
			buff[size] = '\0';
	}
		return 0;
}
/** \brief :  浮点数转化成4个字节
 *
 * \param  :  输入一个浮点数地址
 * \param
 * \return
 *
 */
void  FloatToByte(float *a,uint8_t *byteArray)
{
    unsigned char*px;
    unsigned char i;
    unsigned char x[4];
    void          *pf;
    px=x;
    pf=a;
    for(i=0;i<4;i++)
    {
      *(px+i) = *((char*)pf+i);
    }
    for(i=0;i<4;i++)
    {
      byteArray[i]=x[i];
      DEBUG_CONF("byteArray[%d]:0x%x\n",i,byteArray[i]);
    }

}
/** \brief  将字节型数组转换成32bits浮点型
 *  \param  输入参数为4bytes的字节数组
 *  \param
 *  \return 返回浮点型数据
 *
 */
void ByteToFloat(uint8_t *byteArray,float *a)
{
       *a =*((float*)byteArray);
}


/**
 *  brief:     两个数组值比较
 * 
 *  parameter: 源地址， 目标地址,比较数值大小 
 * 
 *  return:   完全相同 返回0  不相同返回1
 * 
 *  
 */
int  ArrayValueCmp (uint8_t *dst, uint8_t *src, int size)
{
        if ( dst == NULL || src == NULL) 
                return 1;

        if (size <= 0)
                return 1;

        while(size)
        {
                if (*dst++ == *src++) {                   
                        size--;

                }else{
                    
                        return 1;
                }
        }
        return 0;

}


