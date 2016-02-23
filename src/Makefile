# Created by: lightside <lightside@gmx.com>
# This file released into the public domain

WX_CONFIG?=	wx-config
WX_CXXFLAGS=	`$(WX_CONFIG) --cxxflags`
WX_LIBS=	`$(WX_CONFIG) --libs aui adv core xml net`
PACKAGE_VERSION?=	__DATE__
CXX?=	c++
CXXFLAGS+=	-DPACKAGE_VERSION=$(PACKAGE_VERSION) -std=c++11 $(WX_CXXFLAGS)
LDFLAGS+=	$(WX_LIBS)

ARCH=	$(firstword $(shell uname -m))
SYS=	$(firstword $(shell uname -s))
DISTFILE?=	$(DISTNAME)$(DISTEXT)
DISTNAME?=	$(APPNAME)_$(SYS)_$(ARCH)
DISTEXT?=	.tar.gz
DIST_CMD?=	tar
DIST_ARGS?=	-caf

APPNAME=	treesheets
SRCS=	main.cpp
OBJS=	$(SRCS:.cpp=.o)

release: CXXFLAGS+= -O3
release: install clean

debug: CXXFLAGS+= -g
debug: install clean

all: $(SRCS) $(APPNAME)

$(APPNAME): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@

.o:
	$(CXX) $(CXXFLAGS) $< -o $@

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
