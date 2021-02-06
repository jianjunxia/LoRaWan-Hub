#!/bin/sh

RESULT=$(cat OTAUpgrade | awk 'NR==2')

if [ -z "${RESULT}" ]; then
   
	 echo "No lora_pkt_communicate process"

else
   
	 echo "test read OTA FILE $RESULT"
fi


