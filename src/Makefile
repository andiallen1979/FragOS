CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
override CFLAGS += -std=c11
LDFLAGS ?=
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin

TARGET = ps1cfg
SRC = ps1cfg.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) -o $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
