

# compiler flags:
# -Os                : optimize size
# -DF_CPU=16000000UL : define F_CPU as 16Mghz
# -mmcu=atmega328p   : target MCU


#MMCU=atmega328
MMCU=atmega2560

ifeq ($(MMCU),atmega328)
PROG_MMCU=atmega328p
PROG_DEV=arduino
AVRD_CONF=
endif

ifeq ($(MMCU),atmega2560)
PROG_MMCU=$(MMCU)
PROG_DEV=wiring
AVRD_CONF="-C/etc/avrdude/avrdude.conf"
endif

main.hex : main.c
	# compile to object file (optimize for size)
	avr-gcc -Os -DF_CPU=16000000UL -mmcu=$(MMCU) -o main.elf main.c 
	# convert ELF format to an IHEX format as used by avrdude
	avr-objcopy -O ihex -R .eeprom main.elf main.hex


burn:
	avrdude $(AVR_CONF) -v -p$(PROG_MMCU) -c$(PROG_DEV) -P/dev/ttyACM0 -b115200 -D -Uflash:w:main.hex:i 

clean :
	rm -f main.o main.elf main.hex


