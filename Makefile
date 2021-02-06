##update:2018.12.24
### Environment constants 

ARCH:=arm
CROSS_COMPILE :=arm-openwrt-linux-
export

### general build targets

all:
	$(MAKE) all -e -C lora_conf
	$(MAKE) all -e -C lora_pkt_fwd
	$(MAKE) all -e -C lora_pkt_server
	$(MAKE) all -e -C lora_pkt_communicate
clean:
	$(MAKE) clean -e -C lora_conf
	$(MAKE) clean -e -C lora_pkt_fwd
	$(MAKE) clean -e -C lora_pkt_server
	$(MAKE) clean -e -C lora_pkt_communicate
### EOF

