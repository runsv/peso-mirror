## default is to build on Linux using GNU make

sinclude mk/*?.mk

all :	src Lua

Lua :
	cd ./Lua && $(MAKE) all

sh :
	cd ./sh && $(MAKE) all

src :
	cd ./src && $(MAKE) all

installr-exe :
	cd ./src && $(MAKE) install

install-Lua :
	cd ./Lua && $(MAKE) install

install :	install-exe install-Lua

clean :

check :

test :		check

help :
	@echo valid make targets:

###########################################################################

all install uninstall clean:
	cd src && $(MAKE) $@

TAG = $(shell git describe --abbrev=0 --tags)
VERSION = $(shell echo $(TAG) | sed s/^v//)
FORMAT = tar.gz

dist:
	@ if [ -n "`git tag --list $(TAG)`" ]; \
	then \
		git archive --verbose --format=$(FORMAT) \
		--prefix=imapfilter-$(VERSION)/ \
		--output=imapfilter-$(VERSION).$(FORMAT) v$(VERSION); \
		echo "Created Git archive: imapfilter-$(VERSION).$(FORMAT)"; \
	else \
		echo "No such tag in the Git repository: $(TAG)"; \
	fi

distclean:
	rm -f imapfilter-*.*

