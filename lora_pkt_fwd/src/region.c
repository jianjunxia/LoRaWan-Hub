#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include<netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "region.h"
#include "common.h"

#define REGION_AS923
#ifdef  REGION_AS923
    #define AS923_CASE                                 case LORAMAC_REGION_AS923:
    #define AS923_APPLY_DR_OFFSET( )                   AS923_CASE { return RegionAS923ApplyDrOffset( downlinkDwellTime, dr, drOffset ); }
#else
    #define AS923_APPLY_DR_OFFSET( )
#endif

#define REGION_AU915
#ifdef  REGION_AU915
    #define AU915_CASE                                 case LORAMAC_REGION_AU915:
    #define AU915_APPLY_DR_OFFSET( )                   AU915_CASE { return RegionAU915ApplyDrOffset( downlinkDwellTime, dr, drOffset ); }
#else
    #define AU915_APPLY_DR_OFFSET( )  
#endif

#define REGION_CN470
#ifdef  REGION_CN470
    #define CN470_CASE                                 case LORAMAC_REGION_CN470:
    #define CN470_APPLY_DR_OFFSET( )                   CN470_CASE { return RegionCN470ApplyDrOffset( downlinkDwellTime, dr, drOffset ); }
#else
    #define CN470_APPLY_DR_OFFSET( ) 
#endif

#define REGION_CN779
#ifdef  REGION_CN779
    #define CN779_CASE                                 case LORAMAC_REGION_CN779:
    #define CN779_APPLY_DR_OFFSET( )                   CN779_CASE { return RegionCN779ApplyDrOffset( downlinkDwellTime, dr, drOffset ); }
#else
    #define CN779_APPLY_DR_OFFSET( )  
#endif

#define REGION_EU433
#ifdef  REGION_EU433
    #define EU433_CASE                                 case LORAMAC_REGION_EU433:
    #define EU433_APPLY_DR_OFFSET( )                   EU433_CASE { return RegionEU433ApplyDrOffset( downlinkDwellTime, dr, drOffset ); }
#else
    #define EU433_APPLY_DR_OFFSET( ) 
#endif

#define REGION_EU868
#ifdef  REGION_EU868
    #define EU868_CASE                                 case LORAMAC_REGION_EU868:
    #define EU868_APPLY_DR_OFFSET( )                   EU868_CASE { return RegionEU868ApplyDrOffset( downlinkDwellTime, dr, drOffset ); }
#else
    #define EU868_APPLY_DR_OFFSET( ) 
#endif

#define REGION_KR920
#ifdef  REGION_KR920
    #define KR920_CASE                                 case LORAMAC_REGION_KR920:
    #define KR920_APPLY_DR_OFFSET( )                   KR920_CASE { return RegionKR920ApplyDrOffset( downlinkDwellTime, dr, drOffset ); }
#else
     #define KR920_APPLY_DR_OFFSET( ) 
#endif

#define REGION_IN865
#ifdef  REGION_IN865
    #define IN865_CASE                                 case LORAMAC_REGION_IN865:
    #define IN865_APPLY_DR_OFFSET( )                   IN865_CASE { return RegionIN865ApplyDrOffset( downlinkDwellTime, dr, drOffset ); }
#else
    #define IN865_APPLY_DR_OFFSET( ) 
#endif

#define REGION_US915
#ifdef  REGION_US915
    #define US915_CASE                                 case LORAMAC_REGION_US915:
    #define US915_APPLY_DR_OFFSET( )                   US915_CASE { return RegionUS915ApplyDrOffset( downlinkDwellTime, dr, drOffset ); }
#else
    #define US915_APPLY_DR_OFFSET( ) 
#endif

#define REGION_US915_HYBRID
#ifdef  REGION_US915_HYBRID
    #define US915_HYBRID_CASE                           case LORAMAC_REGION_US915_HYBRID:
    #define US915_HYBRID_APPLY_DR_OFFSET( )             US915_HYBRID_CASE { return RegionUS915HybridApplyDrOffset( downlinkDwellTime, dr, drOffset ); }
#else
    #define US915_HYBRID_APPLY_DR_OFFSET( ) 
#endif

//update:增加中国区域异频频段
#define REGION_CN470_ASYNCHRONY
#ifdef  REGION_CN470_ASYNCHRONY
    #define CN470_ASYNCHRONY_CASE                       case LORAMAC_REGION_CN470_ASYNCHRONY:
    #define CN470_ASYNCHRONY_APPLY_DR_OFFSET( )         CN470_ASYNCHRONY_CASE { return AsynchronyRegionCN470ApplyDrOffset( downlinkDwellTime, dr, drOffset); }
#else
    #define CN470_ASYNCHRONY_APPLY_DR_OFFSET( )
#endif

uint8_t RegionAS923ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    // Initialize minDr for a downlink dwell time configuration of 0
    int8_t minDr = DR_0;

    // Update the minDR for a downlink dwell time configuration of 1
    if( downlinkDwellTime == 1 )
    {
        minDr = AS923_DWELL_LIMIT_DATARATE;
    }

    // Apply offset formula
    return MIN( DR_5, MAX( minDr, dr - EffectiveRx1DrOffsetAS923[drOffset] ) );
}

uint8_t RegionAU915ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    int8_t datarate = DatarateOffsetsAU915[dr][drOffset];

    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    return datarate;
}

//中国大陆同频收发函数
//2018.12.25:备注：同频和异频在计算datarate时没有区别,只是下行接收频率的不同
uint8_t RegionCN470ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    #if 1
    
    int8_t datarate = dr - drOffset;

    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    return datarate;

    #endif
}

//中国大陆异频收发函数
uint8_t AsynchronyRegionCN470ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    int8_t datarate = dr - drOffset;

    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    return datarate;
}

uint8_t RegionCN779ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    int8_t datarate = dr - drOffset;

    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    return datarate;
}

uint8_t RegionEU433ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    int8_t datarate = dr - drOffset;

    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    return datarate;
}

uint8_t RegionEU868ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    int8_t datarate = dr - drOffset;

    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    return datarate;
}

uint8_t RegionKR920ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    int8_t datarate = dr - drOffset;

    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    return datarate;
}

uint8_t RegionIN865ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    // Apply offset formula
    return MIN( DR_5, MAX( DR_0, dr - EffectiveRx1DrOffsetIN865[drOffset] ) );
}

uint8_t RegionUS915ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    int8_t datarate = DatarateOffsetsUS915[dr][drOffset];

    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    return datarate;
}

uint8_t RegionUS915HybridApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    int8_t datarate = DatarateOffsetsUS915_HYBRID[dr][drOffset];

    if( datarate < 0 )
    {
        datarate = DR_0;
    }
    return datarate;
}

uint8_t RegionApplyDrOffset( LoRaMacRegion_t region, uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset )
{
    switch( region )
    {
        AS923_APPLY_DR_OFFSET( );
        AU915_APPLY_DR_OFFSET( );
        CN470_APPLY_DR_OFFSET( );
        CN779_APPLY_DR_OFFSET( );
        EU433_APPLY_DR_OFFSET( );
        EU868_APPLY_DR_OFFSET( );
        KR920_APPLY_DR_OFFSET( );
        IN865_APPLY_DR_OFFSET( );
        US915_APPLY_DR_OFFSET( );
        US915_HYBRID_APPLY_DR_OFFSET( );
        CN470_ASYNCHRONY_APPLY_DR_OFFSET( );
        default:
        {
            return dr;
        }
    }
}

//引用lora_pkt_conf.c中的全局变量
extern sqlite3 *db;
extern char *zErrMsg;

//在lora_pkt_conf.c中引用
//#define LORA_PKT_CONF_H
#ifdef  LORA_PKT_CONF_H
    extern struct sockaddr_in udp_sendaddr;
    extern int udp_sockfd;
#else

#endif

// 回调函数写法：
static region_callback(void *NotUsed,int argc,char**argv,char**azColName)
{
    int i;
    for(i=0; i <argc;i++)
    {
        printf("%s = %s\n",azColName[i],argv[i]?argv[i]:"NULL");

    }
    printf("\n");
    return 0;
}

void Channel_FrequencyTable_Init(sqlite3 *db,char *zErrMsg)
{
    int rc;
    char *sql = NULL;

    /* ID               主键 
     * Region           频段所在的区域
     * Rx1DRoffset      接收窗口1的下行偏移量
     * UpLinkFreq       上行频率
     * DownLinkFreq     下行对应频率    
     */
    
    /*!
    * Region       | Rx1DRoffset Rang
    * ------------ | :--------------:
    * AS923        |    [0:7]
    * AU915        |    [0:5]
    * CN470        |    [0:5]
    * CN779        |    [0:5]
    * EU433        |    [0:5]
    * EU868        |    [0:5]
    * IN865        |    [0:7]
    * KR920        |    [0:5]
    * US915        |    [0:3]
    * US915_HYBRID |    [0:3]
    */
    sql = "CREATE TABLE IF NOT EXISTS Region(" \
            "ID               INT        PRIMARY KEY   NOT  NULL,"\
            "Region_Info      INTEGER                  NOT  NULL,"\
            "Rx1DRoffset      INTEGER                  NOT  NULL,"\ 
            "UpLinkFreq       INTEGER                           ,"\
            "DownLinkFreq     INTEGER                            );";
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    rc = sqlite3_exec(db,sql,0,0,&zErrMsg);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg);
    if(rc != SQLITE_OK)
    {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
    }
    else
    {
        fprintf(stdout," Region Table created successfully\n");            
    }     
    
    // 初始化为CN470频段  162-->0xa2
    // 初始化Rx1DRoffset 0
    // 上下行频率信道为8
    sql =  "INSERT OR IGNORE INTO Region (ID,Region_Info,Rx1DRoffset,UpLinkFreq,DownLinkFreq)"\
           "VALUES (1,162,0,0,0);"\
           "INSERT OR IGNORE INTO Region (ID,Region_Info,Rx1DRoffset,UpLinkFreq,DownLinkFreq)"\
           "VALUES (2,162,0,0,0);"\
           "INSERT OR IGNORE INTO Region (ID,Region_Info,Rx1DRoffset,UpLinkFreq,DownLinkFreq)"\
           "VALUES (3,162,0,0,0);"\
           "INSERT OR IGNORE INTO Region (ID,Region_Info,Rx1DRoffset,UpLinkFreq,DownLinkFreq)"\
           "VALUES (4,162,0,0,0);"\
           "INSERT OR IGNORE INTO Region (ID,Region_Info,Rx1DRoffset,UpLinkFreq,DownLinkFreq)"\
           "VALUES (5,162,0,0,0);"\
           "INSERT OR IGNORE INTO Region (ID,Region_Info,Rx1DRoffset,UpLinkFreq,DownLinkFreq)"\
           "VALUES (6,162,0,0,0);"\
           "INSERT OR IGNORE INTO Region (ID,Region_Info,Rx1DRoffset,UpLinkFreq,DownLinkFreq)"\
           "VALUES (7,162,0,0,0);"\
           "INSERT OR IGNORE INTO Region (ID,Region_Info,Rx1DRoffset,UpLinkFreq,DownLinkFreq)"\
           "VALUES (8,162,0,0,0);";
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg);
    if(rc != SQLITE_OK)
    {
        DEBUG_CONF("SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
    }
    else
    {
        DEBUG_CONF("Table init successfully\n");            
    }

    #if 0
    //debug 检测表是否建好了
    sql = "SELECT* FROM Region";
    rc = sqlite3_exec(db,sql,region_callback,NULL,&zErrMsg);
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg);
    if(rc != SQLITE_OK)
    {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
    }
    else
    {
        DEBUG_CONF("Region Table List successfully\n");   
    }
    #endif
}

#ifdef LORA_PKT_CONF_H
//保存区域频段信息，应答配置区域频段信息命令
void Ack_Region_Freq_Info(uint8_t *buff)
{
   int rc;
   char *sql=(char*)malloc(100);
   uint8_t send_buff[6];
   memset(send_buff,0,6);

   sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
   //把8个信道的区域频段设置为统一值。
   sprintf(sql,"UPDATE Region SET Region_Info = %u WHERE ID != 9999",buff[4]);
   //execute
   rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg); 
   sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg);
   if(rc != SQLITE_OK)
   {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
        //更新错误应答   
        send_buff[0] = 0xfa;
        send_buff[1] = 0x1a;
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        //send fail
        send_buff[4] = 0x04;
        //mic
        send_buff[5] = checksum(send_buff,5);
        rc = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(rc  < 0)
        {
            DEBUG_CONF("\nsend ack region info cmd error!\n");
            close(udp_sockfd);
        }     
        free(sql);
    }
    else
    {
        fprintf(stdout,"update region info successfully\n");            
        //更新成功应答
        send_buff[0] = 0xfa;
        send_buff[1] = 0x1a;
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        //send successful
        send_buff[4] = 0x01;
        //mic
        send_buff[5] = checksum(send_buff,5);
        rc = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(rc  < 0)
        {
            DEBUG_CONF("\nsend ack region info cmd error!\n");
            close(udp_sockfd);
        }     
        free(sql);
    }
}

//更新上行信道频率配置信息，并应答
void Ack_UpLink_Freq_Info(uint8_t *buff)
{
    uint32_t ch_freq[8];
    memset(ch_freq,0,8);
    int index;
    //ch0 信道频率低地址的起始地址
    index = 7;
    char *sql = (char*)malloc(100); 
    int rc;
    uint8_t send_buff[6];
    memset(send_buff,0,6);

    for(int i=0; i< 8; i++) //c99
    {
        ch_freq[i]  =  buff[index];
        ch_freq[i] |= (buff[index-1] << 8);  
        ch_freq[i] |= (buff[index-2] << 16);
        ch_freq[i] |= (buff[index-3] << 24);   
        index+=4;
    }
     sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    //更新数据库中上行信道频率信息    
    for(int i=0;i < 8; i++)
    {
        sprintf(sql,"UPDATE Region SET UpLinkFreq = %u WHERE ID = %u;",ch_freq[i],i+1);//id=i+1
         rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg); 
    }
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg);
    if(rc != SQLITE_OK)
    {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
        //更新错误应答   
        send_buff[0] = 0xfa;
        send_buff[1] = 0x1c;
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        //send fail
        send_buff[4] = 0x04;
        //mic
        send_buff[5] = checksum(send_buff,5);
        rc = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(rc  < 0)
        {
            DEBUG_CONF("\nsend ack region info cmd error!\n");
            close(udp_sockfd);
        }     
        free(sql);
    }
    else
    {
        fprintf(stdout,"update region info successfully\n");            
        //更新成功应答
        send_buff[0] = 0xfa;
        send_buff[1] = 0x1c;
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        //send successful
        send_buff[4] = 0x01;
        //mic
        send_buff[5] = checksum(send_buff,5);
        rc = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(rc  < 0)
        {
            DEBUG_CONF("\nsend ack region info cmd error!\n");
            close(udp_sockfd);
        }     
        free(sql);
    }

}

//更新下行信道频率配置信息，并应答
void Ack_DownLink_Freq_Info(uint8_t *buff)
{
    uint32_t ch_freq[8];
    memset(ch_freq,0,8);
    //ch0 信道频率低地址的起始地址
    int index;
    index = 7;
    char *sql = (char*)malloc(100); 
    int rc;
    uint8_t send_buff[6];
    memset(send_buff,0,6);

    for(int i=0; i< 8; i++) //c99
    {
        ch_freq[i]  =  buff[index];
        ch_freq[i] |= (buff[index-1] << 8);  
        ch_freq[i] |= (buff[index-2] << 16);
        ch_freq[i] |= (buff[index-3] << 24);   
        index+=4;
    }
     sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
    //更新数据库中下行信道频率信息    
    for(int i=0;i < 8; i++)
    {
        sprintf(sql,"UPDATE Region SET DownLinkFreq = %u WHERE ID = %u;",ch_freq[i],i+1);//id=i+1
         rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg); 
    }
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg);
    if(rc != SQLITE_OK)
    {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
        //更新错误应答   
        send_buff[0] = 0xfa;
        send_buff[1] = 0x1e;
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        //send fail
        send_buff[4] = 0x04;
        //mic
        send_buff[5] = checksum(send_buff,5);
        rc = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(rc  < 0)
        {
            DEBUG_CONF("\nsend ack region info cmd error!\n");
            close(udp_sockfd);
        }     
        free(sql);
    }
    else
    {
        fprintf(stdout,"update region info successfully\n");            
        //更新成功应答
        send_buff[0] = 0xfa;
        send_buff[1] = 0x1e;
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        //send successful
        send_buff[4] = 0x01;
        //mic
        send_buff[5] = checksum(send_buff,5);
        rc = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(rc  < 0)
        {
            DEBUG_CONF("\nsend ack region info cmd error!\n");
            close(udp_sockfd);
        }     
        free(sql);
    }
}

//保存PC端发送的配置rx1droffset的信息，并应答命令
void Ack_Rx1DRoffset_Info(uint8_t *buff)
{

   int rc;
   char *sql=(char*)malloc(100);
   uint8_t send_buff[6];
   memset(send_buff,0,6);

   sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg);  
   //将8个信道的rx1droffset设置为统一值。
   sprintf(sql,"UPDATE Region SET Rx1DRoffset = %u WHERE ID != 9999",buff[4]);
   //execute
   rc = sqlite3_exec(db,sql,NULL,NULL,&zErrMsg); 
   sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,&zErrMsg);
   if(rc != SQLITE_OK)
   {
        fprintf(stderr,"SQL error: %s\n",zErrMsg);
        sqlite3_free(zErrMsg);
        //更新错误应答   
        send_buff[0] = 0xfa;
        send_buff[1] = 0x20;
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        //send fail
        send_buff[4] = 0x04;
        //mic
        send_buff[5] = checksum(send_buff,5);
        rc = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(rc  < 0)
        {
            DEBUG_CONF("\nsend ack Rx1DRoffset info cmd error!\n");
            close(udp_sockfd);
        }     
        free(sql);
    }
    else
    {
        fprintf(stdout,"update Rx1DRoffset info successfully\n");            
        //更新成功应答
        send_buff[0] = 0xfa;
        send_buff[1] = 0x20;
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        //send successful
        send_buff[4] = 0x01;
        //mic
        send_buff[5] = checksum(send_buff,5);
        rc = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(rc  < 0)
        {
            DEBUG_CONF("\nsend ack Rx1DRoffset info cmd error!\n");
            close(udp_sockfd);
        }     
        free(sql);
    }
}

//应答获取频段信息命令
void Ack_Fetch_Region_Info(void)
{
    uint8_t region;
    bool FetchRegionIsOk = false;
    uint8_t send_buff[6];
    memset(send_buff,0,6);
    int rc;

    if(1 == Fetch_Region_Info(db,&region))
    {
        FetchRegionIsOk = true;
    }

    if( FetchRegionIsOk )//取出频段信息，进行应答处理
    {
        send_buff[0] = 0xfa;
        send_buff[1] = 0x22;
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        send_buff[4] = region;
        //mic
        send_buff[5] = checksum(send_buff,5);
        rc = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(rc  < 0)
        {
            DEBUG_CONF("\nsend ack Rx1DRoffset info cmd error!\n");
            close(udp_sockfd);
        }   

    }
    else  //未正确取出，不进行应答
    {
        DEBUG_CONF("Fetch Region Is Fail\n");
    }

}

//应答获取HUB上行信道频率信息
void Ack_Fetch_Uplink_Info(void)
{
    sqlite3_stmt *stmt;
    int rc;
    char *sql = NULL;
    char *sql_buff = (char*)malloc(100);
    bool GetUplinkFreqIsOk    = false;
    uint8_t send_buff[100];
    int freq_buff[8];
    int send_len;


    //initialization
    memset(send_buff,0,100);
    memset(freq_buff,0,8);

    for(int id = 1; id < 9; id++)//c_99
    {
        sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL); 
        sprintf(sql_buff,"SELECT* FROM Region WHERE ID = %u;",id);
        //parpare
        rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt,NULL);
        if (SQLITE_OK !=rc || NULL == stmt)
        {
          printf("\nfetch region values prepare error!\n");
          sqlite3_close(db);
        }
        //execute
        while(SQLITE_ROW == sqlite3_step(stmt))
        {
            freq_buff[id-1] = sqlite3_column_int(stmt,3);
            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
            //sqlite3_reset (stmt);
            GetUplinkFreqIsOk = true;
        }

    }
    if(GetUplinkFreqIsOk)
    {
            //preamble
            send_buff[send_len++] = 0xfa;
            //cmd
            send_buff[send_len++] = 0x26;
            //len
            send_buff[send_len++] = 0x00;
            send_buff[send_len++] = 0x20;

            //data
            for(int i=0; i < 8; i++) //c99
            {
                send_buff[send_len++] = (uint8_t)(freq_buff[i] >> 24);                
                send_buff[send_len++] = (uint8_t)(freq_buff[i] >> 16);   
                send_buff[send_len++] = (uint8_t)(freq_buff[i] >>  8);   
                send_buff[send_len++] = (uint8_t)(freq_buff[i] >>  0);                           
            }
            //mic
            send_buff[send_len] = checksum(send_buff,send_len);
            send_len++;
            rc = sendto(udp_sockfd,send_buff,send_len,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
            if(rc  < 0)
            {
                DEBUG_CONF("\nsend ack Rx1DRoffset info cmd error!\n");
                close(udp_sockfd);
            }  

    }
    else//不处理
    {

        DEBUG_CONF("Fetch Uplink Frequency Is Fail\n");
    }

}

//应答获取HUB下行信道频率信息
void Ack_Fetch_Downlink_Info(void)
{
    sqlite3_stmt *stmt;
    int rc;
    char *sql = NULL;
    char *sql_buff = (char*)malloc(100);
    bool GetDownlinkFreqIsOk    = false;
    uint8_t send_buff[100];
    int freq_buff[8];
    int send_len;


    //initialization
    memset(send_buff,0,100);
    memset(freq_buff,0,8);

    for(int id = 1; id < 9; id++)//c_99
    {
        sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,NULL); 
        sprintf(sql_buff,"SELECT* FROM Region WHERE ID = %u;",id);
        //parpare
        rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt,NULL);
        if (SQLITE_OK !=rc || NULL == stmt)
        {
          printf("\nfetch region values prepare error!\n");
          sqlite3_close(db);
        }
        //execute
        while(SQLITE_ROW == sqlite3_step(stmt))
        {
            freq_buff[id-1] = sqlite3_column_int(stmt,4);
            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
            //sqlite3_reset (stmt);
            GetDownlinkFreqIsOk = true;
        }

    }

    if(GetDownlinkFreqIsOk)
    {
            //preamble
            send_buff[send_len++] = 0xfa;
            //cmd
            send_buff[send_len++] = 0x24;
            //len
            send_buff[send_len++] = 0x00;
            send_buff[send_len++] = 0x20;

            //data
            for(int i=0; i < 8; i++) //c99
            {
                send_buff[send_len++] = (uint8_t)(freq_buff[i] >> 24);                
                send_buff[send_len++] = (uint8_t)(freq_buff[i] >> 16);   
                send_buff[send_len++] = (uint8_t)(freq_buff[i] >>  8);   
                send_buff[send_len++] = (uint8_t)(freq_buff[i] >>  0);                           
            }
            //mic
            send_buff[send_len] = checksum(send_buff,send_len);
            send_len++;
            rc = sendto(udp_sockfd,send_buff,send_len,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
            if(rc  < 0)
            {
                DEBUG_CONF("\nsend ack Rx1DRoffset info cmd error!\n");
                close(udp_sockfd);
            }  

    }
    else//不处理
    {
         DEBUG_CONF("Fetch Downlink Frequency Is Fail\n");
    }

}

//应答获取Rx1DRoffset信息
void Ack_Fetch_Rx1DRoffset_Info(void)
{
    uint8_t rx1droffset;
    bool FetchRx1DRoffsetIsOk = false;
    uint8_t send_buff[6];
    memset(send_buff,0,6);
    int rc;

    if(1 == Fetch_RX1DRoffset_Info(db,&rx1droffset))
    {
        FetchRx1DRoffsetIsOk = true;
    }

    if( FetchRx1DRoffsetIsOk )//取出频段信息，进行应答处理
    {
        send_buff[0] = 0xfa;
        send_buff[1] = 0x28;
        send_buff[2] = 0x00;
        send_buff[3] = 0x01;
        send_buff[4] = rx1droffset;
        //mic
        send_buff[5] = checksum(send_buff,5);
        rc = sendto(udp_sockfd,send_buff,6,0,(struct sockaddr*)&udp_sendaddr,sizeof(struct sockaddr_in));
        if(rc  < 0)
        {
            DEBUG_CONF("\nsend ack Rx1DRoffset info cmd error!\n");
            close(udp_sockfd);
        }   
    }
    else  //未正确取出，不进行应答
    {
        DEBUG_CONF("Fetch Rx1DRoffset Info Is Fail\n");
    }

}

#else
    void Ack_Region_Freq_Info(uint8_t *buff)
    {


    }

    void Ack_UpLink_Freq_Info(uint8_t *buff)
    {


    }

    void Ack_DownLink_Freq_Info(uint8_t *buff)
    {


    }

    void Ack_Rx1DRoffset_Info(uint8_t *buff)
    {


    }
    //应答获取频段信息命令
    void Ack_Fetch_Region_Info(void)
    {



    }

    //应答获取HUB上行信道频率信息
    void Ack_Fetch_Uplink_Info(void)
    {




    }

    //应答获取HUB下行信道频率信息
    void Ack_Fetch_Downlink_Info(void)
    {




    }

    //应答获取Rx1DRoffset信息
    void Ack_Fetch_Rx1DRoffset_Info(void)
    {


    }

#endif


/**
 * brief: 根据上行频率去取对应的下行频率
 * 
 * return: 成功：返回1  失败：返回0
 * 
 * 
 */

int Fetch_DownLink_Freq_Info(sqlite3 *db,uint32_t *uplinkfreq,uint32_t *downlinkfreq)
{
    sqlite3_stmt *stmt;
    int rc;
    char *sql_buff =(char*)malloc(100); 

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
    sprintf(sql_buff,"SELECT* FROM Region WHERE UpLinkFreq = %u;",*uplinkfreq);

    /* parpare */
    rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt,NULL);

    if (SQLITE_OK !=rc || NULL == stmt){
          
            printf("\nfetch uplinkfreq values prepare error!\n");
            sqlite3_close(db);
    } 

    /* execute */
    while(SQLITE_ROW == sqlite3_step(stmt))
    {

            *downlinkfreq = sqlite3_column_int(stmt,4);
            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
            sqlite3_finalize(stmt);
            free(sql_buff);  
            return 1;
    }
    
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    sqlite3_finalize(stmt);
    free(sql_buff);  
    return 0;
}

/**
 * brief:    取出数据库中rx1droffset的信息
 * return :  successful :1 fail : 0
 * 
 */
int Fetch_RX1DRoffset_Info(sqlite3 *db,uint8_t *rx1droffset)
{
    sqlite3_stmt *stmt;
    int rc;
    char *sql_buff = (char*)malloc(100);
    
    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
    sprintf(sql_buff,"SELECT* FROM Region WHERE ID = 1;");

    /* parpare */
    rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt,NULL);
    
    if (SQLITE_OK !=rc || NULL == stmt){

          printf("\nfetch rx1droffset values prepare error!\n");
          sqlite3_close(db);
    }

    /* execute */
    while(SQLITE_ROW == sqlite3_step(stmt)){

            *rx1droffset = sqlite3_column_int(stmt,2);
            sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
            sqlite3_finalize(stmt);
            free(sql_buff);  
            return 1;
    }
    
    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    sqlite3_finalize(stmt);
    free(sql_buff);  
    return 0;
}

/**
 *  取出数据库中region的信息
 * 
 *  return: successful : 1  fail : 0
 *  
 */ 
int Fetch_Region_Info(sqlite3 *db,uint8_t *region)
{
    sqlite3_stmt *stmt;
    int rc;
    char *sql_buff = (char*)malloc(100);

    sqlite3_exec(db,"BEGIN TRANSACTION;",NULL,NULL,&zErrMsg); 
    sprintf(sql_buff,"SELECT* FROM Region WHERE ID = 1;");

    /* parpare */
    rc = sqlite3_prepare_v2(db,sql_buff,strlen(sql_buff),&stmt,NULL);
    
    if ( SQLITE_OK !=rc || NULL == stmt ) {

            printf("\nfetch region values prepare error!\n");
            sqlite3_close(db);
    }

    /* execute */
    while(SQLITE_ROW == sqlite3_step(stmt))
    {
        *region = sqlite3_column_int(stmt,1);
        sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
        sqlite3_finalize(stmt);
        free(sql_buff);  
        return 1;
    }

    sqlite3_exec(db,"COMMIT TRANSACTION;",NULL,NULL,NULL);
    sqlite3_finalize(stmt);
    free(sql_buff);
    return 0;
}

