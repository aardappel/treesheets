# Created by: lightside <lightside@gmx.com>
# This file released into the public domain

WX_CONFIG?=	wx-config
WX_CXXFLAGS=	`$(WX_CONFIG) --cxxflags`
WX_LIBS=	`$(WX_CONFIG) --libs aui adv core xml net`
PACKAGE_VERSION?=	__DATE__
CXX?=	c++
CXXFLAGS+=	-DPACKAGE_VERSION=$(PACKAGE_VERSION) -std=c++17 $(WX_CXXFLAGS) -I ../lobster/include -I ../lobster/src
LDFLAGS+=	$(WX_LIBS)

ARCH=	$(firstword $(shell uname -m))
SYS=	$(firstword $(shell uname -s))
DISTFILE?=	$(DISTNAME)$(DISTEXT)
DISTNAME?=	$(APPNAME)_$(SYS)_$(ARCH)
DISTEXT?=	.tar.gz
DIST_CMD?=	tar
DIST_ARGS?=	-caf

APPNAME=	treesheets
SRCS=	main.cpp \
        lobster_impl.cpp \
        ../lobster/external/flatbuffers/src/idl_gen_text.cpp \
        ../lobster/external/flatbuffers/src/idl_parser.cpp \
        ../lobster/external/flatbuffers/src/util.cpp \
        ../lobster/src/builtins.cpp \
        ../lobster/src/compiler.cpp \
        ../lobster/src/disasm.cpp \
        ../lobster/src/file.cpp \
        ../lobster/src/lobsterreader.cpp \
        ../lobster/src/platform.cpp \
        ../lobster/src/vm.cpp \
        ../lobster/src/vmdata.cpp \
        ../lobster/src/vmlog.cpp
OBJS=	$(SRCS:.cpp=.o)

all: $(SRCS) $(APPNAME)

release: CXXFLAGS+= -O3
release: install clean

debug: CXXFLAGS+= -g
debug: install clean

$(APPNAME): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@

install: all
	cp -f $(APPNAME) ../TS

deinstall:
	rm -f ../TS/$(APPNAME)

clean:
	rm -f $(APPNAME) *.o

dist-release: release dist

dist-debug: debug dist

dist-all: install clean dist

dist:
	cd .. && $(DIST_CMD) $(DIST_ARGS) $(DISTFILE) TS

dist-clean:
	cd .. && rm -f $(DISTFILE)

.PHONY: all debug release clean install deinstall dist dist-all dist-debug \
	dist-release dist-clean
