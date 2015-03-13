# Created by: lightside <lightside@gmx.com>
# This file released into the public domain

WX_CONFIG?=	wx-config
WX_CXXFLAGS=	`$(WX_CONFIG) --cxxflags`
WX_LIBS=	`$(WX_CONFIG) --libs all`
CXX?=	c++
CXXFLAGS+=	-std=c++11 $(WX_CXXFLAGS)
LDFLAGS+=	$(WX_LIBS)

APPNAME=	treesheets
SRCS=	main.cpp
OBJS=	$(SRCS:.cpp=.o)

all: CXXFLAGS+= -O3
all: $(SRCS) $(APPNAME)

debug: CXXFLAGS+= -g
debug: $(SRCS) $(APPNAME)

$(APPNAME): $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o $@
	mv treesheets ../TS
	rm -f *.o

.o:
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(APPNAME) *.o

.PHONY: all

