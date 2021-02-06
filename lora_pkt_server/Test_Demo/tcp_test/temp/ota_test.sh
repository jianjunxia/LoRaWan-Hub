#!/bin/sh

PassWord=1


## 更新软链接
RESULT=$(ps -ef | grep ".\/t[e]st")
if [ -z "${RESULT}" ]; then
	echo "No test process"
else
#	echo $RESULT | awk '{print $2}' | xargs kill -9 
	echo "hello~"
fi

#echo $PassWord | sudo -S rm -rf /bin/elf/test
echo $PassWord | sudo -S ln -snf /home/senthink/Desktop/lorawan_github/LoRaWAN-HUB_Project_Version/lora_pkt_server/Test_Demo/tcp_test/temp/hello /bin/elf/test

chdir /bin/elf
./test
