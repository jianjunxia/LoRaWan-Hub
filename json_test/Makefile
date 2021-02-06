############jianjun_xia############
############data:2018.10.15##########

APP_NAME := json_test

LGW_PATH ?= ../lora_pkt_fwd
SQLITE_PATH ?=/usr/local/ARM-SQlite
ARCH :=arm
#CROSS_COMPILE :=arm-none-linux-gnueabi-
CROSS_COMPILE :=

OBJDIR = obj
SRC    = src
INC    = inc

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

CFLAGS := -O2 -Wall -Wextra -std=c99 -Iinc -I.
#LIBS := -lsqlite3
LIBS  :=
all: $(APP_NAME)

.PHONY:clean
clean:
	-rm -rf $(APP_NAME) $(OBJDIR)/*.o 

### Sub-modules compilationcle	
$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/parson.o: $(SRC)/parson.c $(INC)/parson.h | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@

$(OBJDIR)/$(APP_NAME).o: $(SRC)/$(APP_NAME).c | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@

$(APP_NAME): $(OBJDIR)/$(APP_NAME).o $(OBJDIR)/parson.o
	$(CC)  -L$(SQLITE_PATH)/lib -o $@ $^ $(LIBS)