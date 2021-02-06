### Application-specific constants
APP_NAME := lora_pkt_communicate

ARCH :=arm
CROSS_COMPILE :=arm-openwrt-linux-
OBJDIR = obj
SRC    = src
INC    = inc
SQLITE_PATH ?=/usr/local/ARM-SQlite

### External constant definitions
# must get library build option to know if mpsse must be linked or not

### Constant symbols

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

CFLAGS := -O2 -Wall -Wextra -std=c99 -Iinc -I.
### Constants for Lora concentrator HAL library
# List the library sub-modules that are used by the application

### Linking options

LIBS := -lpthread -lsqlite3

### General build targets

all: $(APP_NAME)

clean:
	rm -f $(OBJDIR)/*.o
	rm -f $(APP_NAME)

### Sub-modules compilationcle	
$(OBJDIR):
	mkdir -p $(OBJDIR)

#$(OBJDIR)/common.o: $(SRC)/common.c $(INC)/common.h | $(OBJDIR)
#	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@
#$(OBJDIR)/parson.o: $(SRC)/parson.c $(INC)/parson.h | $(OBJDIR)
#	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@
#$(OBJDIR)/communicate.o: $(SRC)/communicate.c $(INC)/communicate.h | $(OBJDIR)
#	$(CC) -c $(CFLAGS) -I$(SQLITE_PATH)/include $< -o $@
#$(APP_NAME): $(OBJDIR)/communicate.o $(OBJDIR)/common.o $(OBJDIR)/parson.o
#	$(CC)  -L$(SQLITE_PATH)/lib -o $@ $^ $(LIBS)


$(OBJDIR)/%.o: $(SRC)/%.c $(INC)/%.h | $(OBJDIR)
	$(CC) -c $(CFLAGS) -I$(LGW_PATH)/inc -I$(SQLITE_PATH)/include -o $@ $<

$(APP_NAME): $(OBJDIR)/communicate.o $(OBJDIR)/common.o $(OBJDIR)/parson.o
	$(CC) -L$(SQLITE_PATH)/lib -o $@ $^ $(LIBS)

### EOF
