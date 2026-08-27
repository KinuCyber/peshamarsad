# peshamarsad - A personal local-first job-hunt tracker
# Copyright (C) 2026  KinuCyber <kinucyber@kinu.uk>
# GPL v3
#
# Targets:
#   make                build x86_64 binary (default)
#   make arm32          build arm32 binary (run on Termux, requires clang)
#   make arm64          build arm64 binary (run on Termux, requires clang)
#   make schema.h       regenerate schema.h after editing schema.sql
#   make clean          remove objects and binaries

# Note:
#   make                tested on Celsius H770 (Arch Linux x86_64, gcc)
#   make arm32          tested on Redmi A2+ (Termux, clang)
#   make arm64          untested, no arm64 device available

CC_X86    = gcc
CC_ARM    = clang
CFLAGS    = -Wall -Wextra -std=c99 -g
SQLFLAGS  = -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION

.PHONY: all arm32 arm64 clean

all: schema.h peshamarsad_x86_64

arm32: schema.h peshamarsad_arm32

arm64: schema.h peshamarsad_arm64

peshamarsad_x86_64: peshamarsad.o commands.o sqlite3_x86.o
	$(CC_X86) $(CFLAGS) -o $@ $^

peshamarsad_arm32: peshamarsad.c commands.c sqlite3.c
	$(CC_ARM) $(CFLAGS) $(SQLFLAGS) -m32 -o $@ $^

peshamarsad_arm64: peshamarsad.c commands.c sqlite3.c
	$(CC_ARM) $(CFLAGS) $(SQLFLAGS) -m64 -o $@ $^

peshamarsad.o: peshamarsad.c commands.h cli.h db.h str.h schema.h
	$(CC_X86) $(CFLAGS) -c $< -o $@

commands.o: commands.c commands.h cli.h db.h str.h schema.h
	$(CC_X86) $(CFLAGS) -c $< -o $@

sqlite3_x86.o: sqlite3.c sqlite3.h
	$(CC_X86) $(SQLFLAGS) -c $< -o $@

schema.h: schema.sql
	xxd -i schema.sql | sed 's/^unsigned/static unsigned/' | grep -v 'sql_len' | sed 's/};/,  0x00\n};/' > schema.h

clean:
	rm -f *.o peshamarsad_x86_64 peshamarsad_arm32 peshamarsad_arm64
