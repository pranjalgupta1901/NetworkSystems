CC=gcc
CFLAGS=-lpthread
CFLAGS += -w
TARGET=server
SRCS=server.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) -o $(TARGET) $(SRCS) $(CFLAGS)

clean:
	rm -f $(TARGET)

