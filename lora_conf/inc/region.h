// Lierda | Senthink
// Jianjun_xia 
// date: 2018.12.05
//根据不同区域，建立频段的对应关系
//可参考 << LoRaWAN 1.0.2 Regional Parameters >>

#ifndef  _REGION_H_    
#define  _REGION_H_
    
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

/*!
 * \brief Returns the minimum value between a and b
 *
 * \param [IN] a 1st value
 * \param [IN] b 2nd value
 * \retval minValue Minimum value
 */
#define MIN( a, b ) ( ( ( a ) < ( b ) ) ? ( a ) : ( b ) )

/*!
 * \brief Returns the maximum value between a and b
 *
 * \param [IN] a 1st value
 * \param [IN] b 2nd value
 * \retval maxValue Maximum value
 */
#define MAX( a, b ) ( ( ( a ) > ( b ) ) ? ( a ) : ( b ) )


//本地建立区域的映射值，方便命令码传输到lora_pkt_fwd.c,从而根据不同的区域，选择计算dr的不同方式
/*!
 * Region           |  Value (1byte)
 * ------------     | :----------:
 * AS923            |     0xa0
 * AU915            |     0xa1
 * CN470            |     0xa2
 * CN779            |     0xa3
 * EU433            |     0xa4
 * EU868            |     0xa5
 * IN865            |     0xa6
 * KR920            |     0xa7
 * US915            |     0xa8
 * US915_HYBRID     |     0xa9
 * CN470_ASYNCHRONY |     0xaa
 */
#define         AS923               0xa0
#define         AU915               0xa1
#define         CN470               0xa2
#define         CN779               0xa3
#define         EU433               0xa4
#define         EU868               0xa5
#define         IN865               0xa6
#define         KR920               0xa7
#define         US915               0xa8
#define         US915_HYBRID        0xa9 
#define         CN470_ASYNCHRONY    0xaa
/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | SF12 - BW125
 * AU915        | SF10 - BW125
 * CN470        | SF12 - BW125
 * CN779        | SF12 - BW125
 * EU433        | SF12 - BW125
 * EU868        | SF12 - BW125
 * IN865        | SF12 - BW125
 * KR920        | SF12 - BW125
 * US915        | SF10 - BW125
 * US915_HYBRID | SF10 - BW125
 */
#define DR_0                                        0

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | SF11 - BW125
 * AU915        | SF9  - BW125
 * CN470        | SF11 - BW125
 * CN779        | SF11 - BW125
 * EU433        | SF11 - BW125
 * EU868        | SF11 - BW125
 * IN865        | SF11 - BW125
 * KR920        | SF11 - BW125
 * US915        | SF9  - BW125
 * US915_HYBRID | SF9  - BW125
 */
#define DR_1                                        1

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | SF10 - BW125
 * AU915        | SF8  - BW125
 * CN470        | SF10 - BW125
 * CN779        | SF10 - BW125
 * EU433        | SF10 - BW125
 * EU868        | SF10 - BW125
 * IN865        | SF10 - BW125
 * KR920        | SF10 - BW125
 * US915        | SF8  - BW125
 * US915_HYBRID | SF8  - BW125
 */
#define DR_2                                        2

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | SF9  - BW125
 * AU915        | SF7  - BW125
 * CN470        | SF9  - BW125
 * CN779        | SF9  - BW125
 * EU433        | SF9  - BW125
 * EU868        | SF9  - BW125
 * IN865        | SF9  - BW125
 * KR920        | SF9  - BW125
 * US915        | SF7  - BW125
 * US915_HYBRID | SF7  - BW125
 */
#define DR_3                                        3

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | SF8  - BW125
 * AU915        | SF8  - BW500
 * CN470        | SF8  - BW125
 * CN779        | SF8  - BW125
 * EU433        | SF8  - BW125
 * EU868        | SF8  - BW125
 * IN865        | SF8  - BW125
 * KR920        | SF8  - BW125
 * US915        | SF8  - BW500
 * US915_HYBRID | SF8  - BW500
 */
#define DR_4                                        4

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | SF7  - BW125
 * AU915        | RFU
 * CN470        | SF7  - BW125
 * CN779        | SF7  - BW125
 * EU433        | SF7  - BW125
 * EU868        | SF7  - BW125
 * IN865        | SF7  - BW125
 * KR920        | SF7  - BW125
 * US915        | RFU
 * US915_HYBRID | RFU
 */
#define DR_5                                        5

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | SF7  - BW250
 * AU915        | RFU
 * CN470        | SF12 - BW125
 * CN779        | SF7  - BW250
 * EU433        | SF7  - BW250
 * EU868        | SF7  - BW250
 * IN865        | SF7  - BW250
 * KR920        | RFU
 * US915        | RFU
 * US915_HYBRID | RFU
 */
#define DR_6                                        6

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | FSK
 * AU915        | RFU
 * CN470        | SF12 - BW125
 * CN779        | FSK
 * EU433        | FSK
 * EU868        | FSK
 * IN865        | FSK
 * KR920        | RFU
 * US915        | RFU
 * US915_HYBRID | RFU
 */
#define DR_7                                        7

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | RFU
 * AU915        | SF12 - BW500
 * CN470        | RFU
 * CN779        | RFU
 * EU433        | RFU
 * EU868        | RFU
 * IN865        | RFU
 * KR920        | RFU
 * US915        | SF12 - BW500
 * US915_HYBRID | SF12 - BW500
 */
#define DR_8                                        8

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | RFU
 * AU915        | SF11 - BW500
 * CN470        | RFU
 * CN779        | RFU
 * EU433        | RFU
 * EU868        | RFU
 * IN865        | RFU
 * KR920        | RFU
 * US915        | SF11 - BW500
 * US915_HYBRID | SF11 - BW500
 */
#define DR_9                                        9

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | RFU
 * AU915        | SF10 - BW500
 * CN470        | RFU
 * CN779        | RFU
 * EU433        | RFU
 * EU868        | RFU
 * IN865        | RFU
 * KR920        | RFU
 * US915        | SF10 - BW500
 * US915_HYBRID | SF10 - BW500
 */
#define DR_10                                       10

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | RFU
 * AU915        | SF9  - BW500
 * CN470        | RFU
 * CN779        | RFU
 * EU433        | RFU
 * EU868        | RFU
 * IN865        | RFU
 * KR920        | RFU
 * US915        | SF9  - BW500
 * US915_HYBRID | SF9  - BW500
 */
#define DR_11                                       11

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | RFU
 * AU915        | SF8  - BW500
 * CN470        | RFU
 * CN779        | RFU
 * EU433        | RFU
 * EU868        | RFU
 * IN865        | RFU
 * KR920        | RFU
 * US915        | SF8  - BW500
 * US915_HYBRID | SF8  - BW500
 */
#define DR_12                                       12

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | RFU
 * AU915        | SF7  - BW500
 * CN470        | RFU
 * CN779        | RFU
 * EU433        | RFU
 * EU868        | RFU
 * IN865        | RFU
 * KR920        | RFU
 * US915        | SF7  - BW500
 * US915_HYBRID | SF7  - BW500
 */
#define DR_13                                       13

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | RFU
 * AU915        | RFU
 * CN470        | RFU
 * CN779        | RFU
 * EU433        | RFU
 * EU868        | RFU
 * IN865        | RFU
 * KR920        | RFU
 * US915        | RFU
 * US915_HYBRID | RFU
 */
#define DR_14                                       14

/*!
 * Region       | SF
 * ------------ | :-----:
 * AS923        | RFU
 * AU915        | RFU
 * CN470        | RFU
 * CN779        | RFU
 * EU433        | RFU
 * EU868        | RFU
 * IN865        | RFU
 * KR920        | RFU
 * US915        | RFU
 * US915_HYBRID | RFU
 */
#define DR_15                                       15


//区域 和 Rx1DRoffset Rang 对应关系

/*!
 * Region       | Rx1DRoffset Rang
 * ------------ | :-----:
 * AS923        | [0:7]
 * AU915        | [0:5]
 * CN470        | [0:5]
 * CN779        | [0:5]
 * EU433        | [0:5]
 * EU868        | [0:5]
 * IN865        | [0:7]
 * KR920        | [0:5]
 * US915        | [0:3]
 * US915_HYBRID | [0:3]
 */

/*!
 * The minimum datarate which is used when the
 * dwell time is limited.
 */
#define AS923_DWELL_LIMIT_DATARATE                  DR_2

/*!
 * LoRaMAC region enumeration
 */
typedef enum eLoRaMacRegion_t
{
    /*!
     * AS band on 923MHz
     */
    LORAMAC_REGION_AS923 = 0xa0,
    /*!
     * Australian band on 915MHz
     */
    LORAMAC_REGION_AU915,
    /*!
     * Chinese band on 470MHz
     */
    LORAMAC_REGION_CN470,
    /*!
     * Chinese band on 779MHz
     */
    LORAMAC_REGION_CN779,
    /*!
     * European band on 433MHz
     */
    LORAMAC_REGION_EU433,
    /*!
     * European band on 868MHz
     */
    LORAMAC_REGION_EU868,
    /*!
     * South korean band on 920MHz
     */
    LORAMAC_REGION_KR920,
    /*!
     * India band on 865MHz
     */
    LORAMAC_REGION_IN865,
    /*!
     * North american band on 915MHz
     */
    LORAMAC_REGION_US915,
    /*!
     * North american band on 915MHz with a maximum of 16 channels
     */
    LORAMAC_REGION_US915_HYBRID,

    //增加中国CN470-510异频收发
    LORAMAC_REGION_CN470_ASYNCHRONY,

}LoRaMacRegion_t;


/*!
 * Effective datarate offsets for receive window 1.
 */
static const int8_t EffectiveRx1DrOffsetAS923[] = { 0, 1, 2, 3, 4, 5, -1, -2 };

/*!
 * Up/Down link data rates offset definition
 */
static const int8_t DatarateOffsetsAU915[7][6] =
{
    { DR_8 , DR_8 , DR_8 , DR_8 , DR_8 , DR_8  }, // DR_0
    { DR_9 , DR_8 , DR_8 , DR_8 , DR_8 , DR_8  }, // DR_1
    { DR_10, DR_9 , DR_8 , DR_8 , DR_8 , DR_8  }, // DR_2
    { DR_11, DR_10, DR_9 , DR_8 , DR_8 , DR_8  }, // DR_3
    { DR_12, DR_11, DR_10, DR_9 , DR_8 , DR_8  }, // DR_4
    { DR_13, DR_12, DR_11, DR_10, DR_9 , DR_8  }, // DR_5
    { DR_13, DR_13, DR_12, DR_11, DR_10, DR_9  }, // DR_6
};

/*!
 * Effective datarate offsets for receive window 1.
 */
static const int8_t EffectiveRx1DrOffsetIN865[] = { 0, 1, 2, 3, 4, 5, -1, -2 };

/*!
 * Up/Down link data rates offset definition
 */
static const int8_t DatarateOffsetsUS915_HYBRID[5][4] =
{
    { DR_10, DR_9 , DR_8 , DR_8  }, // DR_0
    { DR_11, DR_10, DR_9 , DR_8  }, // DR_1
    { DR_12, DR_11, DR_10, DR_9  }, // DR_2
    { DR_13, DR_12, DR_11, DR_10 }, // DR_3
    { DR_13, DR_13, DR_12, DR_11 }, // DR_4
};

/*!
 * Up/Down link data rates offset definition
 */
static const int8_t DatarateOffsetsUS915[5][4] =
{
    { DR_10, DR_9 , DR_8 , DR_8  }, // DR_0
    { DR_11, DR_10, DR_9 , DR_8  }, // DR_1
    { DR_12, DR_11, DR_10, DR_9  }, // DR_2
    { DR_13, DR_12, DR_11, DR_10 }, // DR_3
    { DR_13, DR_13, DR_12, DR_11 }, // DR_4
};


/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionApplyDrOffset( LoRaMacRegion_t region, uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );

/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionAS923ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );

/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionAU915ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );

/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionCN470ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );


//update:增加CN470-510异频收发函数
/*!
 * \brief 
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t AsynchronyRegionCN470ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );


/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionCN779ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );

/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionEU433ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );

/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionEU868ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );

/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionIN865ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );

/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionKR920ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );

/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionUS915HybridApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );

/*!
 * \brief Computes new datarate according to the given offset
 *
 * \param [IN] downlinkDwellTime Downlink dwell time configuration. 0: No limit, 1: 400ms
 *
 * \param [IN] dr Current datarate
 *
 * \param [IN] drOffset Offset to be applied
 *
 * \retval newDr Computed datarate.
 */
uint8_t RegionUS915ApplyDrOffset( uint8_t downlinkDwellTime, int8_t dr, int8_t drOffset );

//初始化一个包含区域频段、Rx1DRoffset、上下行信道频率对应的表
void Channel_FrequencyTable_Init(sqlite3 *db,char *zErrMsg);

//保存区域频段信息，应答配置区域频段信息命令
void Ack_Region_Freq_Info(uint8_t *buff);

//保存HUB上行信道频率信息，并应答命令
void Ack_UpLink_Freq_Info(uint8_t *buff);

//保存HUB下行信道频率信息，并应答命令
void Ack_DownLink_Freq_Info(uint8_t *buff);

//保存PC端发送的配置rx1droffset的信息，并应答命令
void Ack_Rx1DRoffset_Info(uint8_t *buff);


//应答获取频段信息命令
void Ack_Fetch_Region_Info(void);

//应答获取HUB上行信道频率信息
void Ack_Fetch_Uplink_Info(void);

//应答获取HUB下行信道频率信息
void Ack_Fetch_Downlink_Info(void);

//应答获取Rx1DRoffset信息
void Ack_Fetch_Rx1DRoffset_Info(void);


/*!
 * \brief 根据数据库中的上行频率去取对应的下行频率
 *
 * \param [IN] sqlite3 *db,uplinkfrq
 *
 * \param [OUT] downlinkfreq
 *
 * \return  1:successful  0: fail      
 */

int Fetch_DownLink_Freq_Info(sqlite3 *db,uint32_t *uplinkfreq,uint32_t *downlinkfreq);

/*!
 * \brief 取出数据库中的Rx1DRoffset信息
 *
 * \param [IN]  sqlite3 *db
 *
 * \param [OUT] rx1droffset
 *
 * \return  1:successful  0: fail      
 */

int Fetch_RX1DRoffset_Info(sqlite3 *db,uint8_t *rx1droffset);

/*!
 * \brief 取出数据库中的区域频段信息
 *
 * \param [IN]  sqlite3 *db
 *
 * \param [OUT] region
 *
 * \return  1:successful  0: fail      
 */

int Fetch_Region_Info(sqlite3 *db,uint8_t *region);

#endif