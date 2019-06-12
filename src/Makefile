##
## default is to build for Linux using GNU make and gcc
##

#ar rcs $lib $@
#ar s $lib
# -lm -ldl -lz -ltermcap -ledit
# -ltecla -lcurses

#libtry.a:	$(LIB_OBJS)
#	ar cr $@ $(LIB_OBJS)
#	ranlib $@

#sinclude ../mk/*?.mk

PREFIX ?= /usr/local
DESTDIR ?=
# path to the busybox binary
BUSYBOX = /bin/busybox
# path to the toybox binary
TOYBOX = /bin/toybox
# path to the sysvinit binary
SYSVINIT = /sbin/sysv-init
LUA_MOD_DIR ?= /lib/lua/5.3
LUA_CMOD_DIR ?= /lib64/lua/5.3
CROSS ?=
CP ?= cp
RM ?= rm -f
MKDIR ?= mkdir -p
SHELL ?= /bin/sh
STRIP ?= strip -s
#CC ?= gcc -std=gnu99 -g -Os
#CC = gcc -std=c89
CC = gcc
CXX = g++
LD = gcc -s

# Skalibs libs/headers
SKALIBS_HOME = /usr
SKALIBS_INC_DIR = $(SKALIBS_HOME)/include
SKALIBS_LIB_DIR = /lib
SKALIBS_LIB = -L$(SKALIBS_LIB_DIR) -lskarnet
SKALIBS_STATIC_LIB = $(SKALIBS_LIB_DIR)/skalibs/libskarnet.a

# Lua libs/headers
LUA_HOME = /usr
TCL_HOME = /usr/local
LUA_INC_DIR = $(LUA_HOME)/include
LUA_LIB_DIR = $(LUA_HOME)/lib64
LUA_LIB = $(LUA_LIB_DIR)/liblua.a
LUA_LIBS = -lm -ldl -L$(LUA_LIB_DIR) -llua
#LUA_LDFLAGS = -lm -ldl -Wl,-rpath,$(LUA_LIB_DIR)
LUA_LDFLAGS = -lm -ldl -L$(LUA_LIB_DIR) $(LUA_LIB)

# Squirrel libs/headers
SQ_HOME = /usr/local
SQ_INC_DIR = $(SQ_HOME)/include
SQ_LIB_DIR = $(SQ_HOME)/lib64
SQ_INC = -I$(SQ_INC_DIR)
SQ_LIBS = -lm -ldl -L$(SQ_LIB_DIR) -lsquirrel -lsqstdlib
SQ_STATIC_LIBS = -lm -ldl -L$(SQ_LIB_DIR) -lsquirrel-static -lsqstdlib-static
SQ_CFLAGS =
SQ_LDFLAGS = $(SQ_LIBS)
SQ_STATIC_LDFLAGS = -static -Wl,-static $(SQ_LIBS)

# Tcl libs/headers
TCL_INC_DIR = $(TCL_HOME)/include
TCL_LIB_DIR = $(TCL_HOME)/lib64
TCL_LIB = -ltcl8.6
TCL_LIBS = -L$(TCL_LIB_DIR) $(TCL_LIB)

# OpenRC einfo lib
EINFO_INC_DIR = /usr/include
EINFO_LIB_DIR = /lib64
EINFO_LIB = $(EINFO_LIB_DIR)/libeinfo.so

LIBS =
#INCLUDES = -I. -I$(LUA_HOME)/include
#INCS = -I$(LUA_HOME)/include -I$(TCL_HOME)/include
INCS = -I/usr/include -I/usr/local/include -I.
#CFLAGS = -Wall -Wpedantic -g -O2 $(INCS)
#CFLAGS += -Wall -Wpedantic -O2 $(INCS)
CFLAGS = -Wall -O2 $(INCS)
# -Wl,-rpath,/path/to/lib,/path/to/other/lib
#-rpath-link
#LDFLAGS = -Os -s -Wl,-rpath,/usr/local/lib64:/usr/lib64:/lib64
LDFLAGS = -s

inc = $(wildcard *?.h)
src = $(wildcard *?.c)
bin = delay fgrun lux pause pidfsup prcsup rcorder runas runlevel setutmpid
sbin = bbinit hardreboot hddown killall5 rmcgroup stage1 stage2 stage3 svinit tbinit testinit
bins = $(bin) $(sbin)
libs =
#inid_obj = main.o reboot.o respawn.o utils.o utmp.o
#obj = $(inid_obj) client.o hash.o rcorder.o runtcl.o
#objects = $(patsubst %.c,%.o,$(wildcard *.c))

.SUFFIXES : .c .h .cc .C .cpp .cxx .hh .H .o .so

#%.so :	%.c
#	$(CROSS)$(CC) $(CFLAGS) -fPIC -shared $<

%.o :	%.c
	@echo "  CC	$@"
	$(CROSS)$(CC) -c $(CFLAGS) $<

#% :	%.o
#	@echo "  LD	$@"
#	$(CROSS)$(LD) $(LDFLAGS) -o $@ $^

all :	libs exe

bbinit :	bbinit.c
	$(CROSS)$(CC) $(LDFLAGS) $(CFLAGS) -DBB=$(BUSYBOX) -o $@ $^

tbinit :	bbinit.c
	$(CROSS)$(CC) $(LDFLAGS) $(CFLAGS) -DBB=$(TOYBOX) -o $@ $^

svinit :	bbinit.c
	$(CROSS)$(CC) $(LDFLAGS) $(CFLAGS) -DBB=$(SYSVINIT) -o $@ $^

testinit :	reboot.o testinit.o
	@echo "  LD	$@"
	$(CROSS)$(LD) $(LDFLAGS) -o $@ $^

stage2 :	reboot.o stage2.o
	@echo "  LD	$@"
	$(CROSS)$(LD) $(LDFLAGS) -o $@ $^

stage3 :	reboot.o stage3.o
	@echo "  LD	$@"
	$(CROSS)$(LD) $(LDFLAGS) -o $@ $^

oinid :		$(inid_obj)
	@echo "  LD	$@"
	$(CROSS)$(LD) $(LDFLAGS) -o $@ $^

rcorder :	hash.o rcorder.o
	@echo "  LD	$@"
	$(CROSS)$(LD) $(LDFLAGS) -o $@ $^

runtcl.o :	runtcl.c
	@echo "  CC	$@"
	$(CROSS)$(CC) $(CFLAGS) $(INCS) -I$(TCL_INC_DIR) -c $<

runtcl :	runtcl.o
	@echo "  LD	$@"
	$(CROSS)$(LD) $(LDFLAGS) $(TCL_LIBS) -o $@ $^

rt :	runtcl

sys.o :		sys.c
	@echo "  CC	$@"
	$(CROSS)$(CC) $(CFLAGS) -I$(LUA_INC_DIR) -fPIC -O2 -c $^

sys.so :	sys.o
	@echo "  LD	$@"
	$(CROSS)$(LD) $(LDFLAGS) -fPIC -shared -o $@ $^

pesi.so :	sys.o
	@echo "  LD	$@"
	$(CROSS)$(LD) $(LDFLAGS) -fPIC -shared -o $@ $^

xinu.so :	sys.o
	@echo "  LD	$@"
	$(CROSS)$(LD) $(LDFLAGS) -fPIC -shared -o $@ $^

lua_einfo.o :		lua_einfo.c
	@echo "  CC	$@"
	$(CROSS)$(CC) $(CFLAGS) -I$(EINFO_INC_DIR) -I$(LUA_INC_DIR) -fPIC -O2 -c $^

einfo.so :		lua_einfo.o
	@echo "  LD	$@"
	$(CROSS)$(LD) $(LDFLAGS) -fPIC -shared -o $@ $^ $(EINFO_LIB)

einfo :		lua einfo.so

pesi :		pesi.so sys.so xinu.so

lux :	sys.c
	@echo "  CCLD	$@"
	$(CROSS)$(CC) $(LDFLAGS) $(CFLAGS) -I$(LUA_INC_DIR) $(LUA_LIBS) -DSTANDALONE -o $@ $^ $(LUA_LIB)

rl :	runlua

lua :	sys.so runlua

runsq.o :	runsq.c
	@echo "  CC	$@"

runsq :
	@echo "  LD	$@"

sq :	runsq

tcl :	runtcl

exe :		$(obj) $(bins)

strip :		exe
	$(CROSS)$(STRIP) $(bins) *?.so

clean :
	@$(RM) -f *?\~ *?.o *?.so *?.a a.out runtcl runlua $(bins)

install-conf :

install-doc :

install-man :

install-mod :

install-exe :		exe

install-strip-exe :		exe strip

install :		all install-conf install-exe

install-strip :		all install-conf install-strip-exe

overwrite :

check :		all

checks :	check

test :		check

tests :		check

full :		all lua tcl

install :	all strip
	$(MKDIR) -pm 755 $(DESTDIR)/bin $(DESTDIR)/sbin $(DESTDIR)/etc
	@echo "  INSTALL	$(bin)"
	$(INSTALL) $(bin) $(DESTDIR)/bin
	@echo "  INSTALL	$(sbin)"
	$(INSTALL) $(sbin) $(DESTDIR)/sbin
	ifeq ($(DESTDIR),)
	@echo
	@echo
	@echo cd /sbin
	@echo ln -s inid init
	@echo ln -s inidc reboot
	@echo ln -s inidc poweroff
	@echo ...
	@echo
	@echo If you want to use inid as new (sys)init.
	@echo Remember to backup the old tools before (sym)linking in case anything goes wrong !!
	@echo
	@echo
	endif

install-tcl :		all tcl install
	$(MKDIR) -pm 755 $(DESTDIR)/bin $(DESTDIR)/sbin $(DESTDIR)/etc
	if test -f runtcl ; then \
	@echo "  INSTALL	runtcl" ; \
	$(STRIP) runtcl ; $(INSTALL) runtcl $(DESTDIR)/bin ; fi

install-lua :		all lua install
	$(MKDIR) -pm 755 $(DESTDIR)/bin $(DESTDIR)/sbin $(DESTDIR)/etc \
	$(DESTDIR)$(LUA_CMOD_DIR) $(DESTDIR)$(LUA_MOD_DIR)
	if test -f runlua ; then \
	@echo "  INSTALL	runlua" ; \
	$(STRIP) runlua ; $(INSTALL) runlua $(DESTDIR)/bin ; fi
	if test -f sys.so ; then \
	@echo "  INSTALL	sys.so" ; \
	$(STRIP) sys.so ; \
	$(INSTALL) sys.so $(DESTDIR)$(LUA_MOD_DIR) ; \
	$(INSTALL) sys.so $(DESTDIR)$(LUA_MOD_DIR)/pesi.so ; \
	$(INSTALL) sys.so $(DESTDIR)$(LUA_MOD_DIR)/xinu.so ; \
	$(INSTALL) sys.so $(DESTDIR)$(LUA_MOD_DIR)/unx.so ; \
	$(INSTALL) sys.so $(DESTDIR)$(LUA_MOD_DIR)/osunix.so ; \
	fi

install-all :		all lua tcl install install-lua install-tcl

.PHONY :	help clean all install

#####################################################################

DESTDIR ?=
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
SHAREDIR = $(PREFIX)/share/imapfilter
MANDIR = $(PREFIX)/man

MYCFLAGS =
MYLDFLAGS =
MYLIBS =

INCDIRS =
LIBDIRS =

LIBLUA = -llua
LIBPCRE = -lpcre
LIBSSL = -lssl
LIBCRYPTO = -lcrypto

CFLAGS = -Wall -Wextra -O -DCONFIG_SHAREDIR='"$(SHAREDIR)"' $(INCDIRS) $(MYCFLAGS)
LDFLAGS = $(LIBDIRS) $(MYLDFLAGS)
LIBS = -lm -ldl $(LIBLUA) $(LIBPCRE) $(LIBSSL) $(LIBCRYPTO) $(MYLIBS)

MAN1 = imapfilter.1
MAN5 = imapfilter_config.5

LUA = common.lua set.lua regex.lua account.lua mailbox.lua message.lua \
      options.lua auxiliary.lua

BIN = imapfilter
OBJ = auth.o buffer.o cert.o core.o file.o imapfilter.o list.o log.o lua.o \
      memory.o misc.o namespace.o pcre.o regexp.o request.o response.o \
      session.o signal.o socket.o system.o

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) -o $(BIN) $(LDFLAGS) $(OBJ) $(LIBS)

$(OBJ): imapfilter.h
buffer.o: buffer.h 
cert.o: pathnames.h session.h
file.o: pathnames.h
imapfilter.o: buffer.h list.h pathnames.h regexp.h session.h version.h
list.o: list.h
log.o: list.h pathnames.h session.h
lua.o: pathnames.h
namespace.o: buffer.h 
regexp.o: regexp.h
request.o: buffer.h session.h
response.o: buffer.h regexp.h session.h
session.o: list.h session.h
socket.o: session.h

install: $(BIN)
	mkdir -p $(DESTDIR)$(BINDIR) && \
	cp -f $(BIN) $(DESTDIR)$(BINDIR) && \
	chmod 0755 $(DESTDIR)$(BINDIR)/$(BIN)
	mkdir -p $(DESTDIR)$(SHAREDIR) && \
	cp -f $(LUA) $(DESTDIR)$(SHAREDIR) && \
	chmod 0644 $(DESTDIR)$(SHAREDIR)/$(LUA)
	mkdir -p $(DESTDIR)$(MANDIR)/man1 && \
	cp -f ../doc/$(MAN1) $(DESTDIR)$(MANDIR)/man1 && \
	chmod 0644 $(DESTDIR)$(MANDIR)/man1/$(MAN1)
	mkdir -p $(DESTDIR)$(MANDIR)/man5 && \
	cp -f ../doc/$(MAN5) $(DESTDIR)$(MANDIR)/man5 && \
	chmod 0644 $(DESTDIR)$(MANDIR)/man5/$(MAN5)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	cd $(DESTDIR)$(SHAREDIR) && rm -f $(LUA)
	rm -f $(DESTDIR)$(MANDIR)/man1/$(MAN1)
	rm -f $(DESTDIR)$(MANDIR)/man5/$(MAN5)

