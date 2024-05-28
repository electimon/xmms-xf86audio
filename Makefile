# $Id: Makefile 592 2006-07-09 01:30:47Z aqua $

CC=gcc

OPT=-O2
DEBUG=-g

CFLAGS=-fPIC $(OPT) $(DEBUG) -Wall `gtk-config --cflags` `xmms-config --cflags`
LIBS=`gtk-config --libs` `xmms-config --libs`

PLUGIN=libxf86audio.so
PLUGINDIR=`xmms-config --general-plugin-dir`

all : $(PLUGIN)

install : $(PLUGIN)
	mkdir -p $(PLUGINDIR)
	install $(PLUGIN) $(PLUGINDIR)

$(PLUGIN) : xf86audio.o
	$(CC) $(LIBS) -shared $? -o $@

.c.o :
	$(CC) $(CFLAGS) -c $? -o $@

clean :
	rm -f $(PLUGIN) *.o

	
