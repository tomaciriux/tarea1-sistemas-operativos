CC = gcc
CFLAGS = -Wall -Wextra -std=c17
LDFLAGS = -lpthread

TARGET = planificador
SRCS = planificador.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET) *.o
