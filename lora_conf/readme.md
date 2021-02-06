Modify the history

Lierda |  Senthink

autor:  jianjun_xia

data :  2018.10.11

更新工程的结构

                                src/lora_pkt_conf.c
                                         |
                                         |  依赖    
                                         |  
                        ----------------------------------
                        |                |               |
                        |                |               |
              src/lora_pkt_conf.h   src/common.c     src/parson.c
                                    inc/common.h     src/parson.h 



Lierda    |   Senthink

autor     :   jianjun_xia

push time :   2018.9.26

1：该程序为上位机配置部分

2：具体通信协议参照 LORAWAN网关与上位机传输协议.pdf

3：后期可在其协议上进行扩展或修改

4：涉及其他协议制定时，此协议可提供参考。