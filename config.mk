# gitcrawl version
VERSION = 1.1.0

# Paths
PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man

# Compiler and linker flags
CC = cc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -O2 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_GNU_SOURCE -DVERSION=\"$(VERSION)\"
LDFLAGS = -lz
