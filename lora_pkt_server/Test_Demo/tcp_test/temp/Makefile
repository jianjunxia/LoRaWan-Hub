APP_NAME := main 
OBJDIR := obj
SRC    := src
INC    := inc
CROSS_COMPILE ?=
CC := $(CROSS_COMPILE)gcc

all:$(APP_NAME)

clean:
	rm -f $(OBJDIR)/*.o
	rm -f $(APP_NAME)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o:$(SRC)/%.c $(INC)/%.h
	$(CC) -c -I$(INC) -o $@ $<

$(APP_NAME): $(OBJDIR)/main.o $(OBJDIR)/printf.o 
	$(CC) -L$(SRC) -o $@ $^