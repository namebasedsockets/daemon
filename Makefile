TARGET := namestackd

CFLAGS := -g -I../kernel/include -I../module

C_SRCS := \
	  daemon.c \
	  dns.c \
          main.c

LFLAGS := -lresolv -lpthread

OBJECTS := $(patsubst %.c,%.o,$(C_SRCS))

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LFLAGS) -o $@

.PHONY: clean
clean:
	-rm $(TARGET) $(OBJECTS)
