
CXX      := g++
TARGET   := luma
SRC      := main.cpp
INCL     := -I. -I./BackEnds -I./Engines -I./LV2Host
OBJ      := $(SRC:.cpp=.o)
DEP      := $(OBJ:.o=.d)

PKG_CFLAGS   := $(shell pkg-config --cflags jack lilv-0 x11)
PKG_LIBS     := $(shell pkg-config --libs jack lilv-0 x11)
GTK2CXXFLAGS := 


# GTK2 auto-detect
GTK2_FOUND := $(shell pkg-config --exists gtk+-2.0 && echo 1)

ifeq (,$(findstring clean,$(MAKECMDGOALS)))
    ifeq ($(GTK2_FOUND),1)
        $(info GTK2 found — enabling GTK support)
        PKG_CFLAGS += $(shell pkg-config --cflags gtk+-2.0)
        PKG_LIBS   += $(shell pkg-config --libs gtk+-2.0)
        GTK2CXXFLAGS   += -DHAVE_GTK2 -Wno-deprecated-declarations
    else
        $(info GTK2 not found — building X11 only)
    endif
endif

CXXFLAGS += -std=c++17 -Wall -Wextra -O2
LDFLAGS  := -ldl

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $^ -o $@ $(PKG_LIBS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(GTK2CXXFLAGS) $(INCL) $(PKG_CFLAGS) -MMD -MP -c $< -o $@

-include $(DEP)

debug: CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0 -DDEBUG
debug: clean all

clean:
	rm -f $(TARGET) $(OBJ) $(DEP)

.PHONY: all clean debug
