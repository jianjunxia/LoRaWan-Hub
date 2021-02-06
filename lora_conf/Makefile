
############jianjun_xia############
############data:2018.9.7##########

APP_NAME := lora_pkt_conf

LGW_PATH ?= ../lora_pkt_fwd
SQLITE_PATH ?=/usr/local/ARM-SQlite
ARCH :=arm
#CROSS_COMPILE :=arm-none-linux-gnueabi-
CROSS_COMPILE :=arm-openwrt-linux-
OBJDIR = obj
SRC    = src
INC    = inc

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

CFLAGS := -O2 -Wall -Wextra -std=c99 -Iinc -I.
LIBS := -lpthread -lsqlite3

all: $(APP_NAME)

.PHONY:clean
clean:
	-rm -rf $(APP_NAME) $(OBJDIR)/*.o 

### Sub-modules compilationcle	
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/parson.o: $(SRC)/parson.c $(INC)/parson.h | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@

$(OBJDIR)/common.o: $(SRC)/common.c $(INC)/common.h | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@
#update:2018.12.06
$(OBJDIR)/region.o: $(SRC)/region.c $(INC)/region.h | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@

$(OBJDIR)/$(APP_NAME).o: $(SRC)/$(APP_NAME).c $(INC)/$(APP_NAME).h | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@

$(APP_NAME): $(OBJDIR)/$(APP_NAME).o $(OBJDIR)/common.o $(OBJDIR)/parson.o $(OBJDIR)/region.o
	$(CC)  -L$(SQLITE_PATH)/lib -o $@ $^ $(LIBS)



