STUDENT ?= 313512009
TARGET  ?= sched_demo_$(STUDENT)

CC      ?= riscv64-linux-gnu-gcc
CFLAGS  ?= -O2 -Wall -Wextra -Werror -pedantic
LDFLAGS ?= -static -pthread

SRC := sched_demo_$(STUDENT).c
OBJ := $(SRC:.c=.o)

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)
