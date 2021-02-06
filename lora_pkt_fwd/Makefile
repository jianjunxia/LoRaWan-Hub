### Application-specific constants

APP_NAME := lora_pkt_fwd

### Environment constants 

LGW_PATH ?= ../../lora_gateway/libloragw
SQLITE_PATH ?=/usr/local/ARM-SQlite
#GW_PARSE_PATH := ../../gateway_parse

ARCH :=arm
#CROSS_COMPILE :=arm-none-linux-gnueabi-
CROSS_COMPILE :=arm-openwrt-linux-
OBJDIR = obj
INC = inc
SRC = src
INCLUDES = $(wildcard inc/*.h)

### External constant definitions
# must get library build option to know if mpsse must be linked or not

include $(LGW_PATH)/library.cfg
RELEASE_VERSION := `cat ../VERSION`

### Constant symbols

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

CFLAGS := -O2 -Wall -Wextra -std=c99 -Iinc -I.
VFLAG := -D VERSION_STRING="\"$(RELEASE_VERSION)\""

### Constants for Lora concentrator HAL library
# List the library sub-modules that are used by the application

LGW_INC =
ifneq ($(wildcard $(LGW_PATH)/inc/config.h),)
  # only for HAL version 1.3 and beyond
  LGW_INC += $(LGW_PATH)/inc/config.h
endif
LGW_INC += $(LGW_PATH)/inc/loragw_hal.h
LGW_INC += $(LGW_PATH)/inc/loragw_gps.h

### Linking options

#LIBS := -lloragw -lrt -lpthread -lm -lgateway
LIBS := -lloragw -lrt -lpthread -lm -lsqlite3
#LIBS := -lgateway -lloragw -lrt -lpthread -lm
### General build targets

all: $(APP_NAME)

clean:
	rm -f $(OBJDIR)/*.o
	rm -f $(APP_NAME)

### Sub-modules compilationcle	
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: $(SRC)/%.c $(INCLUDES) | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(LGW_PATH)/inc -I$(SQLITE_PATH)/include $< -o $@

### Main program compilation and assembly

#$(OBJDIR)/common.o: $(SRC)/common.c $(INC)/common.h | $(OBJDIR)
#	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@

#$(OBJDIR)/delay_queue.o: $(SRC)/delay_queue.c $(INC)/delay_queue.h | $(OBJDIR)
#	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@

$(OBJDIR)/$(APP_NAME).o: src/$(APP_NAME).c $(LGW_INC) $(INCLUDES) | $(OBJDIR)
	$(CC) -c $(CFLAGS) $(VFLAG) -I$(LGW_PATH)/inc -I$(SQLITE_PATH)/include $< -o $@

$(APP_NAME): $(OBJDIR)/$(APP_NAME).o $(LGW_PATH)/libloragw.a $(OBJDIR)/common.o $(OBJDIR)/delay_queue.o $(OBJDIR)/aes.o $(OBJDIR)/cmac.o $(OBJDIR)/LoRaMacCrypto.o $(OBJDIR)/utilities.o $(OBJDIR)/base64.o $(OBJDIR)/parson.o $(OBJDIR)/jitqueue.o $(OBJDIR)/timersync.o $(OBJDIR)/task_queue.o  $(OBJDIR)/data_queue.o $(OBJDIR)/region.o $(OBJDIR)/classA_queue.o 
	$(CC) -L$(LGW_PATH) -L$(SQLITE_PATH)/lib $^ -o $@ $(LIBS)

#$(APP_NAME): $(OBJDIR)/$(APP_NAME).o $(LGW_PATH)/libloragw.a $(OBJDIR)/aes.o $(OBJDIR)/cmac.o $(OBJDIR)/LoRaMacCrypto.o $(OBJDIR)/utilities.o $(OBJDIR)/base64.o $(OBJDIR)/parson.o $(OBJDIR)/jitqueue.o $(OBJDIR)/timersync.o
#	$(CC) -L$(LGW_PATH) -L$(SQLITE_PATH)/lib $< $(OBJDIR)/aes.o $(OBJDIR)/cmac.o $(OBJDIR)/LoRaMacCrypto.o $(OBJDIR)/utilities.o $(OBJDIR)/parson.o $(OBJDIR)/base64.o $(OBJDIR)/jitqueue.o $(OBJDIR)/timersync.o -o $@ $(LIBS)
### EOF
