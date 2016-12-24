# Name: Makefile
# Author: Eugene Lipchansky <eugene.lipchansky@gmail.com>

# Name of target controller
# (e.g. ‘at90s8515’, see the available avr-gcc mmcu options for possible values)
MCU=atmega328p

# id to use with programmer
# default: PROGRAMMER_MCU=$(MCU)
# In case the programmer used, e.g avrdude, doesn’t
# accept the same MCU name as avr-gcc (for example
# for ATmega8s, avr-gcc expects ‘atmega8’ and avrdude requires ‘m8’)
PROGRAMMER_MCU=m328p

# Name of our project (use a single word, e.g. ‘myproject’)
PROJECTNAME=sdlocker21

# Source files
# List C/C++/Assembly source files:
# (list all files to compile, e.g. ‘a.c b.cpp as.S’):
# Use .cc, .cpp or .C suffix for C++ files, use .S
# (NOT .s !!!) for assembly source code files.
#PRJSRC=main.c myclass.cpp lowlevelstuff.S
PRJSRC=sdlocker21.c

#####      Programmer specific details #####
# programmer id–check the avrdude for complete list of available opts.
# These should include stk500, avr910, avrisp, bsd, pony and more.  Set this to
# one of the valid “-c PROGRAMMER-ID” values described in the avrdude info page.
AVRDUDE_PROGRAMMERID=avrftdi

# port–serial or parallel port to which your
# hardware programmer is attached
AVRDUDE_PORT=/dev/ttyUSB0

# F_CPU - Target AVR clock rate in Hertz
F_CPU      = 8000000

# OBJECTS - The object files created from your source files. This list is
#                usually the same as the list of source files with suffix ".o".
OBJECTS    = sdlocker21.o

# FUSES - Parameters for avrdude to flash the fuses appropriately.
FUSES      = -U lfuse:w:0xe2:m -U hfuse:w:0xd9:m -U efuse:w:0xff:m


######################################################################
######################################################################

# Tune the lines below only if you know what you are doing:

AVRDUDE = avrdude -c $(AVRDUDE_PROGRAMMERID) -p $(PROGRAMMER_MCU)
COMPILE = avr-gcc -Wall -Os -DF_CPU=$(F_CPU) -mmcu=$(MCU)

# symbolic targets:
all:	$(PROJECTNAME).hex

.c.o:
	$(COMPILE) -c $< -o $@

.S.o:
	$(COMPILE) -x assembler-with-cpp -c $< -o $@
# "-x assembler-with-cpp" should not be necessary since this is the default
# file type for the .S (with capital S) extension. However, upper case
# characters are not always preserved on Windows. To ensure WinAVR
# compatibility define the file type manually.

.c.s:
	$(COMPILE) -S $< -o $@

flash:	all
	$(AVRDUDE) -U flash:w:$(PROJECTNAME).hex:i

fuse:
	$(AVRDUDE) $(FUSES)

install: flash fuse

# if you use a bootloader, change the command below appropriately:
load: all
	bootloadHID $(PROJECTNAME).hex

clean:
	rm -f $(PROJECTNAME).hex $(PROJECTNAME).elf $(OBJECTS)

# file targets:
$(PROJECTNAME).elf: $(OBJECTS)
	$(COMPILE) -o $(PROJECTNAME).elf $(OBJECTS)

$(PROJECTNAME).hex: $(PROJECTNAME).elf
	rm -f $(PROJECTNAME).hex
	avr-objcopy -j .text -j .data -O ihex $(PROJECTNAME).elf $(PROJECTNAME).hex
# If you have an EEPROM section, you must also create a hex file for the
# EEPROM and add it to the "flash" target.

# Targets for code debugging and analysis:
disasm:	$(PROJECTNAME).elf
	avr-objdump -d $(PROJECTNAME).elf

cpp:
	$(COMPILE) -E $(PRJSRC)