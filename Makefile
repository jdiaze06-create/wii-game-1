#---------------------------------------------------------------------------------
# Alley Showdown - Wii homebrew (devkitPPC + libogc)
#---------------------------------------------------------------------------------
ifeq ($(strip $(DEVKITPPC)),)
$(error "Set DEVKITPPC (e.g. export DEVKITPPC=/opt/devkitpro/devkitPPC)")
endif

PREFIX   := $(DEVKITPPC)/bin/powerpc-eabi-
CC       := $(PREFIX)gcc
ELF2DOL  := $(DEVKITPRO)/tools/bin/elf2dol

TARGET   := Alley_Showdown
SOURCES  := src
INCLUDES := -I$(DEVKITPRO)/libogc/include
LIBPATHS := -L$(DEVKITPRO)/libogc/lib/wii

MACHDEP  := -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float
CFLAGS   := -O3 -Wall $(MACHDEP) $(INCLUDES)
LDFLAGS  := $(MACHDEP)
LIBS     := -lwiiuse -lbte -logc -lm

.PHONY: all clean

all: $(TARGET).dol

$(TARGET).elf: $(SOURCES)/main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LIBPATHS) $(LIBS)

$(TARGET).dol: $(TARGET).elf
	$(ELF2DOL) $< $@

clean:
	rm -f $(TARGET).dol $(TARGET).elf
