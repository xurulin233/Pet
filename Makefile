PROG ?= pet
SOURCES = main.c ini.c mongoose.c
CFLAGS = -W -Wall -Wextra -O2 -g -I.
LDLIBS = -lm

all: $(PROG)

$(PROG): $(SOURCES) mongoose.h ini.h
	$(CC) $(SOURCES) $(CFLAGS) -o $(PROG) $(LDLIBS)

clean:
	rm -f $(PROG)

.PHONY: all clean
