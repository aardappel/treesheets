ifeq ($(findstring debug, $(MAKECMDGOALS)), debug)
CXXFLAGS+=	-g
else ifeq ($(findstring release, $(MAKECMDGOALS)), release)
CXXFLAGS+=	-O3
endif

include Makefile.common
