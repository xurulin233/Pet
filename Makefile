PROG ?= pet
SOURCES = main.c mongoose.c
CFLAGS = -W -Wall -Wextra -O2 -g -I.
LDLIBS = -lm

all: $(PROG)
	./$(PROG)

$(PROG): $(SOURCES) mongoose.h
	$(CC) $(SOURCES) $(CFLAGS) -o $(PROG) $(LDLIBS)

clean:
	rm -f $(PROG)

.PHONY: all clean
