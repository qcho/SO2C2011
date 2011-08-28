# Binary Name
BINARY := bin/tp1.out

CFLAGS := -Wall -g -lpthread -lm -lncurses -std=c99 -D_XOPEN_SOURCE
INCLUDE_DIR = include
# Directories belonging to the project
PROJDIRS := src include
# All source files of the project
CSRCS := $(shell find -L $(PROJDIRS) -type f -name "*.c")

# All object files in the library
OBJS := $(patsubst src/%.c,bin/%.o,$(CSRCS))
OBJDIRS := $(sort $(dir $(OBJS)))

# Includes
C_INCLUDE_PATH = include

.PHONY: all clean

all: $(OBJDIRS) $(BINARY)

$(OBJDIRS):
	mkdir -p $@

$(BINARY): $(OBJS)
	gcc $(CFLAGS) $(OBJS) -o $@

clean:
	-@$(RM) -rf $(OBJDIRS)

$(OBJS): bin/%.o : src/%.c
	gcc $(CFLAGS) -I $(INCLUDE_DIR) -MMD -MP -c $< -o $@
