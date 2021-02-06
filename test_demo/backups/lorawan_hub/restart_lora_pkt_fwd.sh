#!/bin/sh                                                                                                                                                                                                   

RESULT=$(ps | grep ".\/lora_[p]kt_fwd")
if [ -z "${RESULT}" ]; then
    echo "No such process"
else
    echo $RESULT | awk '{print $1}' | xargs kill -9
fi

chdir /lorawan/lorawan_hub                                                                                                                                                                                  
                                                                                                                                                                                                                                                                                                                                                 
./reset_pkt_fwd_A.sh start;                                                                                                              
./lora_pkt_fwd

