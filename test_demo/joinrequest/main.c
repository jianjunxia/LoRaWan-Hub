#include <stdio.h>
#include <stdlib.h>
//#include <>
#include "aes.h"
#include "cmac.h"
#include "utilities.h"
#define  uint8_t  unsigned char
#define  uint16_t unsigned short int
#define  uint32_t unsigned int

static char appkey[16] =
{
    0x00,0x11,0x22,0x33,
    0x44,0x55,0x66,0x77,
    0x88,0x99,0xAA,0xBB,
    0XCC,0XDD,0XEE,0XFF
};

/* �����ַ�ת������ֵ���� */
uint8_t ascii2hex(uint8_t src);

/* �ַ���ת��Ϊ�ַ�����洢��16������ֵ */
static void String_To_ChArray( uint8_t dst[],const char*src,size_t size);

int main(int argc, char**argv)
{

    FILE *fp = NULL;
    char buf[50];
    uint8_t LoRaMacBuffer[50];
    uint32_t mic = 0;

    /* init */
    memset(buf,0,50);
    memset(LoRaMacBuffer,0,50);

    fp = fopen("mic.txt", "r");
    if (NULL == fp) /* ���ʧ���� */
    {
        printf("����");
        exit(1); /* ��ֹ���� */
    }

     fread(buf,1,36,fp);
    // printf("%s\n",buf);

     String_To_ChArray(LoRaMacBuffer,buf,18);

    /*
     for(int i=0; i< 8; i++){

             printf("LoRaMacBuffer[%d]: 0x%02x\n", i,LoRaMacBuffer[i] );
     }
      */


     LoRaMacJoinComputeMic(LoRaMacBuffer,18,appkey,&mic);
    printf("/*-------------mic����ֵ---------------------*/\n");
    printf("            mic[0]: 0x%02x\n", (mic & 0xff) );
    printf("            mic[1]: 0x%02x\n", ((mic >> 8) & 0xff) );
    printf("            mic[2]: 0x%02x\n", ((mic >>16) & 0xff) );
    printf("            mic[3]: 0x%02x\n", ((mic >>24) & 0xff) );

    fclose(fp); /* �ر��ļ� */
    fp = NULL; /* ��Ҫָ��գ������ָ��ԭ���ļ���ַ */
    system("pause");
    return 0;

}

/* �ַ���ת��Ϊ�ַ�����洢��16������ֵ */
static void String_To_ChArray( uint8_t dst[],const char*src,size_t size)
{
   int loop;
   for(loop=0;loop < size;loop++)
   {
      //���ַ����н�����ֵʱ�����ڽ���˳�����⣺�ӵ͵�ַ�����������
      //�Լ������������
      // sscanf(src,"%02x",(unsigned int*)&dst[loop]);
      // src+=2;
     dst[loop]  = (uint8_t)ascii2hex(src[loop*2])<<4;
     dst[loop] += (uint8_t)ascii2hex(src[loop*2+1]);

   }
}
/* �����ַ�ת������ֵ���� */
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
