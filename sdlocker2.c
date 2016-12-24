/*
 *  sdlocker2      lock/unlock an SD card, uses ATmega328P
 */

#include  <stdio.h>
#include  <string.h>
#include  <ctype.h>

#include  <util/delay.h>

#include  <avr/io.h>
#include  <avr/pgmspace.h>
#include  <avr/interrupt.h>

#include "uart.h"


#ifndef  FALSE
#define  FALSE		0
#define  TRUE		!FALSE
#endif


/*
 *  Calc the value to write to the UART baud rate register, based on desired
 *  baud rate and MCU operating frequency (F_CPU).
 */
#define  BAUDRATE				38400L
#define  BAUDREG				((unsigned int)((F_CPU/(BAUDRATE*8UL))-1))



/*
 *  Define commands for the SD card
 */
#define  SD_GO_IDLE			(0x40 + 0)			/* CMD0 - go to idle state */
#define  SD_INIT			(0x40 + 1)			/* CMD1 - start initialization */
#define  SD_SEND_IF_COND	(0x40 + 8)			/* CMD8 - send interface (conditional), works for SDHC only */
#define  SD_SEND_CSD		(0x40 + 9)			/* CMD9 - send CSD block (16 bytes) */
#define  SD_SEND_CID		(0x40 + 10)			/* CMD10 - send CID block (16 bytes) */
#define  SD_SEND_STATUS		(0x40 + 13)			/* CMD13 - send card status */
#define  SD_SET_BLK_LEN		(0x40 + 16)			/* CMD16 - set length of block in bytes */
#define  SD_READ_BLK		(0x40 + 17)			/* read single block */
#define  SD_LOCK_UNLOCK		(0x40 + 42)			/* CMD42 - lock/unlock card */
#define  CMD55				(0x40 + 55)			/* multi-byte preface command */
#define  SD_READ_OCR		(0x40 + 58)			/* read OCR */
#define  SD_ADV_INIT		(0xc0 + 41)			/* ACMD41, for SDHC cards - advanced start initialization */
#define  SD_PROGRAM_CSD		(0x40 + 27)			/* CMD27 - get CSD block (15 bytes data + CRC) */


/*
 *  Define error tokens that can be returned following a data read/write
 *  request.
 */
#define  ERRTKN_CARD_LOCKED			(1<<4)
#define  ERRTKN_OUT_OF_RANGE		(1<<3)
#define  ERRTKN_CARD_ECC			(1<<2)
#define  ERRTKN_CARD_CC				(1<<1)


/*
 *  Define error codes that can be returned by local functions
 */
#define  SDCARD_OK					0			/* success */
#define  SDCARD_NO_DETECT			1			/* unable to detect SD card */
#define  SDCARD_TIMEOUT				2			/* last operation timed out */
#define  SDCARD_RWFAIL				-1			/* read/write command failed */


/*
 *  Define options for accessing the SD card's PWD (CMD42)
 */
#define  MASK_ERASE					0x08		/* erase the entire card */
#define  MASK_LOCK_UNLOCK			0x04		/* lock or unlock the card with password */
#define  MASK_CLR_PWD				0x02		/* clear password */
#define  MASK_SET_PWD				0x01		/* set password */


/*
 *  Define card types that could be reported by the SD card during probe
 */
#define  SDTYPE_UNKNOWN			0				/* card type not determined */
#define  SDTYPE_SD				1				/* SD v1 (1 MB to 2 GB) */
#define  SDTYPE_SDHC			2				/* SDHC (4 GB to 32 GB) */



/*
 *  Define values for the various switch closure states
 */
#define  SW_LOCK		1
#define  SW_UNLOCK		2
#define  SW_NONE		3
#define  SW_INFO		4
#define  SW_READBLK		5
#define  SW_PWD_LOCK	6
#define  SW_PWD_UNLOCK	7
#define  SW_PWD_CHECK	8
#define  SW_LOCK_CHECK	9
#define  SW_ERASE		10



/*
 *  Define the port and DDR used by the SPI.
 */
#define  SPI_PORT		PORTB
#define  SPI_DDR		DDRB


/*
 *  Define bits used by the SPI port.
 */
#define  MOSI_BIT		3
#define  MISO_BIT		4
#define  SCK_BIT		5


/*
 *  Define the port, DDR, and bit used as chip-select for the
 *  SD card.
 */
#define  SD_CS_PORT		PORTB
#define  SD_CS_DDR		DDRB
#define  SD_CS_BIT		2
#define  SD_CS_MASK		(1<<SD_CS_BIT)


/*
 *  Define the port and bit used for the lock LED.
 */
#define  LOCK_LED_PORT	PORTD
#define  LOCK_LED_DDR	DDRD
#define  LOCK_LED_BIT	2
#define  LOCK_LED_MASK	(1<<LOCK_LED_BIT)
#define  LOCK_LED_OFF	(LOCK_LED_PORT=LOCK_LED_PORT&~LOCK_LED_MASK)
#define  LOCK_LED_ON	(LOCK_LED_PORT=LOCK_LED_PORT|LOCK_LED_MASK)


/*
 *  Define the port and bit used for the unlock LED.
 */
#define  UNLOCK_LED_PORT	PORTD
#define  UNLOCK_LED_DDR		DDRD
#define  UNLOCK_LED_BIT		3
#define  UNLOCK_LED_MASK	(1<<UNLOCK_LED_BIT)
#define  UNLOCK_LED_OFF		(UNLOCK_LED_PORT=UNLOCK_LED_PORT&~UNLOCK_LED_MASK)
#define  UNLOCK_LED_ON		(UNLOCK_LED_PORT=UNLOCK_LED_PORT|UNLOCK_LED_MASK)



/*
 *  Define the port and bit used for the switches.
 */
#define  SW_PORT		PORTC
#define  SW_DDR			DDRC
#define  SW_PIN			PINC

#define  SW_LOCK_BIT	0
#define  SW_UNLOCK_BIT	1
#define  SW_PWD_BIT		2

#define  SW_LOCK_MASK		(1<<SW_LOCK_BIT)
#define  SW_UNLOCK_MASK		(1<<SW_UNLOCK_BIT)
#define  SW_PWD_MASK		(1<<SW_PWD_BIT)
#define  SW_ALL_MASK		(SW_LOCK_MASK | SW_UNLOCK_MASK | SW_PWD_MASK)


/*
 *  Define LED patterns.
 */
#define  PATTERN_NO_DETECT		0xc800c800
#define  PATTERN_CANNOT_CHG		0xa5000000


/*
 *  Define the CRC7 polynomial
 */
#define  CRC7_POLY		0x89		/* polynomial used for CSD CRCs */


/*
 *  Define bit masks for fields in the lock/unlock command (CMD42) data structure
 */
#define  SET_PWD_MASK		(1<<0)
#define  CLR_PWD_MASK		(1<<1)
#define  LOCK_UNLOCK_MASK	(1<<2)
#define  ERASE_MASK			(1<<3)



/*
 *  Local variables
 */
uint32_t						LEDPattern;
uint8_t							sdtype;				// flag for SD card type
uint8_t							csd[16];
uint8_t							cid[16];
uint8_t							ocr[4];
uint8_t							crctable[256];
uint8_t							block[512];
uint8_t							cardstatus[2];		// updated by ReadLockStatus
uint8_t							pwd[16];
uint8_t							pwd_len;

const char						GlobalPWDStr[16] PROGMEM =
								{'F', 'o', 'u', 'r', 't', 'h', ' ', 'A',
								 'm', 'e', 'n', 'd', 'm', 'e', 'n', 't'};
#define  GLOBAL_PWD_LEN			(sizeof(GlobalPWDStr))




/*
 *  Local functions
 */
static void						select(void);
static void						deselect(void);
static uint8_t					xchg(uint8_t  c);
static int8_t					SDInit(void);
static void						BlinkLED(uint32_t  pattern);
static uint8_t					ReadSwitch(void);
static void  					ProcessSwitch(void);
static int8_t					ExamineSD(void);
static int8_t  					ReadOCR(void);
static int8_t  					ReadCID(void);
static int8_t  					ReadCSD(void);
static int8_t					WriteCSD(void);
static int8_t					ReadBlock(uint32_t  blocknum, uint8_t  *buffer);
static void						ShowBlock(void);
static void						ShowErrorCode(int8_t  status);
static int8_t  					ReadCardStatus(void);
static void						ShowCardStatus(void);
static void						ShowLockState(void);
static void						LoadGlobalPWD(void);
static int8_t					ModifyPWD(uint8_t  mask);
static int8_t					ForceErase(void);

static  int8_t  				sd_send_command(uint8_t  command, uint32_t  arg);
static  int8_t					sd_wait_for_data(void);

static void 					GenerateCRCTable(void);
static uint8_t 					AddByteToCRC(uint8_t  crc, uint8_t  b);



int  main(void)
{
/*
 *  Set up the hardware lines and ports associated with accessing the SD card.
 */
	SD_CS_DDR = SD_CS_DDR | SD_CS_MASK;		// make CS line an output
	deselect();								// always start with SD card deselected


	SPI_PORT = SPI_PORT | ((1<<MOSI_BIT) | (1<<SCK_BIT));	// drive outputs to the SPI port
	SPI_DDR = SPI_DDR | ((1<<MOSI_BIT) | (1<<SCK_BIT));		// make the proper lines outputs
	SPI_PORT = SPI_PORT | (1<<MISO_BIT);						// turn on pull-up for DI

	SPCR = (1<<SPE) | (1<<MSTR) | (1<<SPR1) | (1<<SPR0);

/*
 *  Set up the hardware line and port for accessing the LEDs.
 */
	LOCK_LED_OFF;									// start with output line low
	LOCK_LED_DDR = LOCK_LED_DDR | LOCK_LED_MASK;	// make the LED line an output
	UNLOCK_LED_OFF;									// start with output line low
	UNLOCK_LED_DDR = UNLOCK_LED_DDR | UNLOCK_LED_MASK;	// make the LED line an output

/*
 *  Set up the switch lines for input.
 */
 	SW_DDR = SW_DDR & ~(SW_PWD_MASK | SW_LOCK_MASK | SW_UNLOCK_MASK);
	SW_PORT = SW_PORT | (SW_PWD_MASK | SW_LOCK_MASK | SW_UNLOCK_MASK);	// turn on pullups for switch lines

/*
 *  Set up the UART, then connect to standard I/O streams.
 */

    uart_init();
    stdout = &uart_output;
    stdin  = &uart_input;
	stderr = &uart_output;
	sei();									// let the UART ISR work

	printf_P(PSTR("\r\nSDLocker2.1\r\n"));
	printf_P(PSTR("? - SD info\r\n"));
	printf_P(PSTR("u - Write Unlock\r\n"));
	printf_P(PSTR("l - Write Lock\r\n"));
	printf_P(PSTR("p - Password Unlock\r\n"));
	printf_P(PSTR("P - Password Lock\r\n"));
	printf_P(PSTR("E - Erase\r\n"));
	printf_P(PSTR("r - Read\r\n"));

	GenerateCRCTable();

	while (1)
	{
		ProcessSwitch();
	}
	return  0;						// should never happen
}



void  BlinkLED(uint32_t  pattern)
{
	uint8_t							i;

	for (i=0; i<32; i++)
	{
		if (pattern & 0x80000000)
		{
			LOCK_LED_ON;
		}
		else
		{
			LOCK_LED_OFF;
			if (pattern == 0) break;		// leave blink loop if no more ON bits
		}
		_delay_ms(50);
		pattern = pattern << 1;
	}
}




static void GenerateCRCTable()
{
    int i, j;

    // generate a table value for all 256 possible byte values
    for (i = 0; i < 256; i++)
    {
        crctable[i] = (i & 0x80) ? i ^ CRC7_POLY : i;
        for (j = 1; j < 8; j++)
        {
            crctable[i] <<= 1;
            if (crctable[i] & 0x80)
                crctable[i] ^= CRC7_POLY;
        }
    }
}



static uint8_t  AddByteToCRC(uint8_t  crc, uint8_t  b)
{
	return crctable[(crc << 1) ^ b];
}



static void  ProcessSwitch(void)
{
	uint8_t				sw;
	static 	uint8_t		prev_sw = 0;
	uint8_t				r;


	sw = ReadSwitch();
	if ((sw != prev_sw) && (prev_sw == SW_NONE))
	{
/*
 *  Need to access the card.  In all cases, first try to initialize
 *  the card.
 */
		r = SDInit();
		if (r != SDCARD_OK)
		{
			printf_P(PSTR("\n\r\n\rCannot initialize card.  Make sure the card is plugged in properly."));
			BlinkLED(PATTERN_NO_DETECT);
		}
/*
 *  Now see what we need to do.
 */
		if (sw == SW_INFO)
		{
			LOCK_LED_OFF;
			UNLOCK_LED_OFF;
			printf_P(PSTR("\r\nCard type %d"), sdtype);
			r = ExamineSD();
			if (r == SDCARD_OK)
			{
				printf_P(PSTR("\r\nOCR = "));
				for (r = 0; r<4; r++)
				{
					printf_P(PSTR("%02X "), ocr[r]);
				}
				printf_P(PSTR("\r\nCSD = "));
				for (r=0; r<16; r++)
				{
					printf_P(PSTR("%02X "), csd[r]);
				}
				printf_P(PSTR("\r\nCID = "));
				for (r=0; r<16; r++)
				{
					printf_P(PSTR("%02X "), cid[r]);
				}
				ShowCardStatus();
			}
			else
			{
				printf_P(PSTR("\r\nUnable to read CSD."));
			}
		}

		else if (sw == SW_LOCK)
		{
			LOCK_LED_OFF;
			UNLOCK_LED_OFF;
			printf_P(PSTR("\r\nSetting temporary lock on SD card..."));
			r = ReadCSD();
			if (r == SDCARD_OK)
			{
				csd[14] = csd[14] | 0x10;	// set bit 12 of CSD (temp lock)
				r = WriteCSD();
				if (r == SDCARD_OK)
				{
					ReadOCR();
					r = ReadCSD();
					if (r == SDCARD_OK)
					{
						ShowLockState();
						printf_P(PSTR("done."));
					}
					else
					{
						printf_P(PSTR("failed; cannot read CSD to confirm."));
					}
				}
				else
				{
					printf_P(PSTR("failed; response was %d."), r);
					BlinkLED(PATTERN_CANNOT_CHG);
				}
			}
			else
			{
				printf_P(PSTR("failed; unable to read CSD."));
				BlinkLED(PATTERN_NO_DETECT);
			}
		}
		else if (sw == SW_UNLOCK)
		{
			LOCK_LED_OFF;
			UNLOCK_LED_OFF;
			printf_P(PSTR("\r\nClearing temporary lock on SD card..."));
			r = ReadCSD();
			if (r == SDCARD_OK)
			{
				csd[14] = csd[14] & ~0x10;	// clear bit 12 of CSD (temp lock)
				r = WriteCSD();
				if (r == SDCARD_OK)
				{
					ReadOCR();
					r = ReadCSD();
					if (r == SDCARD_OK)
					{
						ShowLockState();
						printf_P(PSTR("done."));
					}
					else
					{
						printf_P(PSTR("failed; cannot read CSD to confirm."));
					}
				}
				else
				{
					printf_P(PSTR("failed; response was %d."), r);
					BlinkLED(PATTERN_CANNOT_CHG);
				}
			}
			else
			{
				printf_P(PSTR("failed; unable to read CSD."));
				BlinkLED(PATTERN_NO_DETECT);
			}
		}
		else if (sw == SW_READBLK)
		{
			printf_P(PSTR("\r\nTest read of block 0 on SD card..."));
			r = ReadBlock(0, block);
			if (r == SDCARD_OK)
			{
				ShowBlock();
			}
		}
		else if (sw == SW_ERASE)
		{
            printf_P(PSTR("\r\nTrying to ERASE SD CARD..."));
			LOCK_LED_OFF;
			UNLOCK_LED_OFF;
			ReadCardStatus();
			if (cardstatus[1] & 0x01)		// if card is locked...
			{
				r = ForceErase();

                printf_P(PSTR("please wait..."));
                _delay_ms(1000);

				ReadCardStatus();

				if (cardstatus[1] & 0x01)	// if card is still locked...
				{
					r = ForceErase();		// erasing failed, try one more time

                    printf_P(PSTR("please wait..."));
                    _delay_ms(1000);

					ReadCardStatus();
				}
				if (cardstatus[1] & 0x01)	// if card is still locked...
				{
					printf_P(PSTR("failed!  Card is still locked."));
					LOCK_LED_ON;
				}
				else
				{
					printf_P(PSTR("done."));
					UNLOCK_LED_ON;
				}
			}
			else							// silly person, card is already unlocked
			{
                printf_P(PSTR("the card is not locked"));
				UNLOCK_LED_ON;
			}
		}
		else if (sw == SW_PWD_UNLOCK)
		{
			LOCK_LED_OFF;
			UNLOCK_LED_OFF;
			ReadCardStatus();
			if (cardstatus[1] & 0x01)		// if card is locked...
			{
				printf_P(PSTR("\r\nTrying to unlock card..."));
				LoadGlobalPWD();
				r = ModifyPWD(MASK_CLR_PWD);
				ReadCardStatus();
				if (cardstatus[1] & 0x01)	// if card is still locked...
				{
					r = ModifyPWD(MASK_CLR_PWD);		// the unlock failed, try one more time
					ReadCardStatus();
				}
				if (cardstatus[1] & 0x01)	// if card is still locked...
				{
					printf_P(PSTR("failed!  Card is still locked."));
					LOCK_LED_ON;
				}
				else
				{
					printf_P(PSTR("done."));
					UNLOCK_LED_ON;
				}
			}
			else							// silly person, card is already unlocked
			{
				UNLOCK_LED_ON;
			}
		}
		else if (sw == SW_PWD_LOCK)
		{
			LOCK_LED_OFF;
			UNLOCK_LED_OFF;
			ReadCardStatus();
			if ((cardstatus[1] & 0x01) == 0)	// if card is unlocked...
			{
				printf_P(PSTR("\r\nTrying to lock card..."));
				LoadGlobalPWD();
				r = ModifyPWD(MASK_SET_PWD);
				ReadCardStatus();

				r = ModifyPWD(MASK_LOCK_UNLOCK);
				ReadCardStatus();
				if ((cardstatus[1] & 0x01) == 0)	// if card is still unlocked...
				{
					printf_P(PSTR("failed!  Card is still unlocked."));
					UNLOCK_LED_ON;
				}
				else
				{
					printf_P(PSTR("done."));
					LOCK_LED_ON;
				}
			}
			else							// silly person, card is already locked
			{
				LOCK_LED_ON;
			}
		}
		else if (sw == SW_PWD_CHECK)
		{
			LOCK_LED_OFF;
			UNLOCK_LED_OFF;
			printf_P(PSTR("\r\nChecking PWD state..."));
			ReadCardStatus();
			if ((cardstatus[1] & 0x01) == 0)	// if card is unlocked...
			{
				UNLOCK_LED_ON;
			}
			else
			{
				LOCK_LED_ON;
			}
		}
		else if (sw == SW_LOCK_CHECK)
		{
			printf_P(PSTR("\r\nChecking temp-lock state..."));
			ReadOCR();
			r = ReadCSD();
			if (r == SDCARD_OK)
			{
				ShowLockState();
			}
			else
			{
				BlinkLED(PATTERN_NO_DETECT);
			}
		}
	}
	prev_sw = sw;
}



static uint8_t  ReadSwitch(void)
{
	uint8_t						r;
	static uint8_t				prev_sw = SW_ALL_MASK;
	static uint16_t             sw_hold_counter = 0;
	uint8_t						sw;

	_delay_ms(50);
	r = SW_NONE;
	if (uart_pending_data())
	{
		r = getchar();
		if      (r == 'u')  r = SW_UNLOCK;
		else if (r == 'l')  r = SW_LOCK;
		else if (r == '?')  r = SW_INFO;
		else if (r == 'r')  r = SW_READBLK;
		else if (r == 'p')  r = SW_PWD_UNLOCK;
		else if (r == 'P')  r = SW_PWD_LOCK;
		else if (r == 'E')  r = SW_ERASE;
		else				r = SW_NONE;
	}

	if (r == SW_NONE)
	{
		sw = SW_PIN & SW_ALL_MASK;
		if (sw != SW_ALL_MASK)						// if at least one switch is down...
		{
			if (((sw & SW_PWD_MASK) == 0) && ((prev_sw & SW_PWD_MASK) == 0))	// if PWD switch is down both scans...
			{
                sw_hold_counter++;
                if(sw_hold_counter > 0xB0) // PWD hold (about 10 sec timeout)
                {
                    sw_hold_counter = 0;
                    r = SW_ERASE;
                }
				else if (((sw & SW_LOCK_MASK) == 0) && (prev_sw & SW_LOCK_MASK))	// if LOCK switch was just pressed...
				{
                    sw_hold_counter = 0;
					r = SW_PWD_LOCK;
				}
				else if (((sw & SW_UNLOCK_MASK) == 0) && (prev_sw & SW_UNLOCK_MASK))	// if UNLOCK switch was just pressed...
				{
                    sw_hold_counter = 0;
					r = SW_PWD_UNLOCK;
				}
			}
			else if (((sw & SW_PWD_MASK) == 0) && (prev_sw & SW_PWD_MASK))		// if PWD switch was just pressed...
			{
                sw_hold_counter = 0;
				if ((sw & (SW_LOCK_MASK | SW_UNLOCK_MASK)) == (SW_LOCK_MASK | SW_UNLOCK_MASK))	// if other switches are open...
				{
					r = SW_PWD_CHECK;
				}
			}
			else if ((sw & SW_PWD_MASK) == SW_PWD_MASK)					// if PWD switch is now open...
			{
                sw_hold_counter = 0;
				if ((sw & (SW_LOCK_MASK | SW_UNLOCK_MASK)) == SW_UNLOCK_MASK)	// if LOCK switch is pressed...
				{
					if (prev_sw & SW_LOCK_MASK)							// but LOCK switch wasn't pressed before...
					{
						r = SW_LOCK;
					}
				}
				else if ((sw & (SW_LOCK_MASK | SW_UNLOCK_MASK)) == SW_LOCK_MASK)	// if UNLOCK switch is pressed...
				{
					if (prev_sw & SW_UNLOCK_MASK)							// but UNLOCK switch wasn't pressed before...
					{
						r = SW_UNLOCK;
					}
				}
			}
		}
		else														// no switches are down...
		{
            sw_hold_counter = 0;
			if ((prev_sw & SW_PWD_MASK) == 0)						// if PWD switch was just released...
			{
				r = SW_LOCK_CHECK;
			}
		}
		prev_sw = sw;												// record for next time
	}

	return  r;
}



static void  ShowLockState(void)
{
	LOCK_LED_OFF;
	UNLOCK_LED_OFF;

	if (csd[14] & 0x10)				// check lock bit in CSD...
	{
		LOCK_LED_ON;
	}
	else
	{
		UNLOCK_LED_ON;
	}
}



/*
 *  select      select (enable) the SD card
 */
static  void  select(void)
{
	SD_CS_PORT = SD_CS_PORT & ~SD_CS_MASK;
}



/*
 *  deselect      deselect (disable) the SD card.
 */
static  void  deselect(void)
{
	SD_CS_PORT = SD_CS_PORT | SD_CS_MASK;
}



/*
 *  xchg      exchange a byte of data with the SD card via host's SPI bus
 */
static  unsigned char  xchg(unsigned char  c)
{
	SPDR = c;
	while ((SPSR & (1<<SPIF)) == 0)  ;
	return  SPDR;
}







static int8_t  SDInit(void)
{
	int					i;
	int8_t				response;

	sdtype = SDTYPE_UNKNOWN;			// assume this fails
/*
 *  Begin initialization by sending CMD0 and waiting until SD card
 *  responds with In Idle Mode (0x01).  If the response is not 0x01
 *  within a reasonable amount of time, there is no SD card on the bus.
 */
	deselect();							// always make sure
	for (i=0; i<10; i++)				// send several clocks while card power stabilizes
		xchg(0xff);

	for (i=0; i<0x10; i++)
	{
		response = sd_send_command(SD_GO_IDLE, 0);	// send CMD0 - go to idle state
		if (response == 1)  break;
	}
	if (response != 1)
	{
		return  SDCARD_NO_DETECT;
	}

	sd_send_command(SD_SET_BLK_LEN, 512);		// always set block length (CMD6) to 512 bytes

	response = sd_send_command(SD_SEND_IF_COND, 0x1aa);	// probe to see if card is SDv2 (SDHC)
	if (response == 0x01)						// if card is SDHC...
	{
		for (i=0; i<4; i++)						// burn the 4-byte response (OCR)
		{
			xchg(0xff);
		}
		for (i=20000; i>0; i--)
		{
			response = sd_send_command(SD_ADV_INIT, 1UL<<30);
			if (response == 0)  break;
		}
		sdtype = SDTYPE_SDHC;
	}
	else
	{
		response = sd_send_command(SD_READ_OCR, 0);
		if (response == 0x01)
		{
			for (i=0; i<4; i++)					// OCR is 4 bytes
			{
				xchg(0xff);					// burn the 4-byte response (OCR)
			}
			for (i=20000; i>0; i--)
			{
				response = sd_send_command(SD_INIT, 0);
				if (response == 0)  break;
			}
			sd_send_command(SD_SET_BLK_LEN, 512);
			sdtype = SDTYPE_SD;
		}
	}

	xchg(0xff);								// send 8 final clocks

/*
 *  At this point, the SD card has completed initialization.  The calling routine
 *  can now increase the SPI clock rate for the SD card to the maximum allowed by
 *  the SD card (typically, 20 MHz).
 */
	return  SDCARD_OK;					// if no power routine or turning off the card, call it good
}



static  void  ShowBlock(void)
{
	uint32_t				i;
	uint8_t					str[17];

	str[16] = 0;
	str[0] = 0;			// only need for first newline, overwritten as chars are processed

	printf_P(PSTR("\n\rContents of block buffer:"));
	for (i=0; i<512; i++)
	{
		if ((i % 16) == 0)
		{
			printf_P(PSTR(" %s\n\r%04X: "), str, i);
		}
		printf_P(PSTR("%02X "), (uint8_t)block[i]);
		if (isalpha(block[i]) || isdigit(block[i]))  str[i%16] = block[i];
		else									     str[i%16] = '.';
	}
	printf_P(PSTR(" %s\n\r"), str);
}



static int8_t  ExamineSD(void)
{
	int8_t			response;

	response = ReadOCR();		// this fails with Samsung; don't test response until know why
	response = ReadCSD();
	if (response == SDCARD_OK)
	{
//		printf_P(PSTR(" ReadCSD is OK "));
		response = ReadCID();
	}
	if (response == SDCARD_OK)
	{
//		printf_P(PSTR(" ReadCID is OK "));
		response = ReadCardStatus();
	}

	return  response;
}




static int8_t  ReadOCR(void)
{
	uint8_t				i;
	int8_t				response;

	for (i=0; i<4;  i++)  ocr[i] = 0;

	if (sdtype == SDTYPE_SDHC)
	{
		response = sd_send_command(SD_SEND_IF_COND, 0x1aa);
		if (response != 0)
		{
			return  SDCARD_RWFAIL;
		}
		for (i=0; i<4; i++)
		{
			ocr[i] = xchg(0xff);
		}
		xchg(0xff);							// burn the CRC
	}
	else
	{
		response = sd_send_command(SD_READ_OCR, 0);
		if (response != 0x00)
		{
			return  SDCARD_RWFAIL;
		}
		for (i=0; i<4; i++)					// OCR is 4 bytes
		{
			ocr[i] = xchg(0xff);
		}
		xchg(0xff);
	}
	return  SDCARD_OK;
}



static  int8_t  ReadCSD(void)
{
	uint8_t			i;
	int8_t			response;

	for (i=0; i<16; i++)  csd[i] = 0;

	response = sd_send_command(SD_SEND_CSD, 0);
	response = sd_wait_for_data();
	if (response != (int8_t)0xfe)
	{
		printf_P(PSTR("\n\rReadCSD(), sd_wait_for_data returns %02x."), response);
		return  SDCARD_RWFAIL;
	}

	for (i=0; i<16; i++)
	{
		csd[i] = xchg(0xff);
	}
	xchg(0xff);							// burn the CRC
	return  SDCARD_OK;
}



static  int8_t  ReadCID(void)
{
	uint8_t			i;
	int8_t			response;

	for (i=0; i<16; i++)  cid[i] = 0;

	response = sd_send_command(SD_SEND_CID, 0);
	response = sd_wait_for_data();
	if (response != (int8_t)0xfe)
	{
		return  SDCARD_RWFAIL;
	}

	for (i=0; i<16; i++)
	{
		cid[i] = xchg(0xff);
	}
	xchg(0xff);							// burn the CRC
	return  SDCARD_OK;
}



static int8_t  WriteCSD(void)
{
	int8_t				response;
	uint8_t				tcrc;
	uint16_t			i;

	response = sd_send_command(SD_PROGRAM_CSD, 0);
	if (response != 0)
	{
		return  SDCARD_RWFAIL;
	}
	xchg(0xfe);							// send data token marking start of data block

	tcrc = 0;
	for (i=0; i<15; i++)				// for all 15 data bytes in CSD...
	{
    	xchg(csd[i]);					// send each byte via SPI
		tcrc = AddByteToCRC(tcrc, csd[i]);		// add byte to CRC
	}
	xchg((tcrc<<1) + 1);				// format the CRC7 value and send it

	xchg(0xff);							// ignore dummy checksum
	xchg(0xff);							// ignore dummy checksum

	i = 0xffff;							// max timeout
	while (!xchg(0xFF) && (--i))  ;		// wait until we are not busy

	if (i)  return  SDCARD_OK;			// return success
	else  return  SDCARD_RWFAIL;		// nope, didn't work
}






static int8_t  ReadCardStatus(void)
{
	cardstatus[0] = sd_send_command(SD_SEND_STATUS, 0);
	cardstatus[1] = xchg(0xff);
//	printf_P(PSTR("\r\nReadCardStatus = %02x %02x"), cardstatus[0], cardstatus[1]);
	xchg(0xff);
	return  SDCARD_OK;
}




static int8_t  ReadBlock(uint32_t  blocknum, uint8_t  *buffer)
{
    uint16_t					i;
	uint8_t						status;
	uint32_t					addr;

/*
 *  Compute byte address of start of desired sector.
 *
 *  For SD cards, the argument to CMD17 must be a byte address.
 *  For SDHC cards, the argument to CMD17 must be a block (512 bytes) number.
 */
	if (sdtype == SDTYPE_SD) 	addr = blocknum << 9;	// SD card; convert block number to byte addr

    status = sd_send_command(SD_READ_BLK, addr);    // send read command and logical sector address
	if (status != SDCARD_OK)
	{
		return  SDCARD_RWFAIL;
	}

	status = sd_wait_for_data();		// wait for valid data token from card
	if (status != 0xfe)					// card must return 0xfe for CMD17
    {
		ShowErrorCode(status);			// tell the user
        return  SDCARD_RWFAIL;			// return error code
    }

    for (i=0; i<512; i++)           	// read sector data
        block[i] = xchg(0xff);

    xchg(0xff);                		 	// ignore CRC
    xchg(0xff);                		 	// ignore CRC

    return  SDCARD_OK;					// return success
}



static int8_t  ModifyPWD(uint8_t  mask)
{
	int8_t						r;
	uint16_t					i;

	mask = mask & 0x07;					// top five bits MUST be 0, do not allow forced-erase!
	r = sd_send_command(SD_LOCK_UNLOCK, 0);
	if (r != 0)
	{
		return  SDCARD_RWFAIL;
	}
	xchg(0xfe);							// send data token marking start of data block

	xchg(mask);							// always start with required command
	xchg(pwd_len);						// then send the password length
	for (i=0; i<512; i++)				// need to send one full block for CMD42
	{
		if (i < pwd_len)
		{
    		xchg(pwd[i]);					// send each byte via SPI
		}
		else
		{
			xchg(0xff);
		}
	}

	xchg(0xff);							// ignore dummy checksum
	xchg(0xff);							// ignore dummy checksum

	i = 0xffff;							// max timeout
	while (!xchg(0xFF) && (--i))  ;		// wait until we are not busy

	if (i)  return  SDCARD_OK;			// return success
	else  return  SDCARD_RWFAIL;		// nope, didn't work
}


static int8_t  ForceErase(void)
{
	int8_t	r;

	sd_send_command(SD_SET_BLK_LEN, 1);		// always set block length (CMD6) to 512 bytes

	r = sd_send_command(SD_LOCK_UNLOCK, 0);
	if (r != 0)
	{
		return  SDCARD_RWFAIL;
	}
	xchg(0xfe);							// send data token marking start of data block

	xchg(MASK_ERASE);					// always start with required command

	return  SDCARD_OK;			// return success
}


static void  ShowErrorCode(int8_t  status)
{
	if ((status & 0xe0) == 0)			// if status byte has an error value...
	{
		printf_P(PSTR("\n\rDate error:"));
		if (status & ERRTKN_CARD_LOCKED)
		{
			printf_P(PSTR(" Card is locked!"));
		}
		if (status & ERRTKN_OUT_OF_RANGE)
		{
			printf_P(PSTR(" Address is out of range!"));
		}
		if (status & ERRTKN_CARD_ECC)
		{
			printf_P(PSTR(" Card ECC failed!"));
		}
		if (status & ERRTKN_CARD_CC)
		{
			printf_P(PSTR(" Card CC failed!"));
		}
	}
}





static void  ShowCardStatus(void)
{
	ReadCardStatus();
	printf_P(PSTR("\r\nPassword status: "));
	if ((cardstatus[1] & 0x01) ==  0) {
        printf_P(PSTR("unlocked"));
        UNLOCK_LED_ON;
    }
    else {
        printf_P(PSTR("locked"));
        LOCK_LED_ON;
	}
}




static void  LoadGlobalPWD(void)
{
	uint8_t				i;

	for (i=0; i<GLOBAL_PWD_LEN; i++)
	{
		pwd[i] = pgm_read_byte(&(GlobalPWDStr[i]));
	}
	pwd_len = GLOBAL_PWD_LEN;
}





/*
 *  ==========================================================================
 *
 *  sd_send_command      send raw command to SD card, return response
 *
 *  This routine accepts a single SD command and a 4-byte argument.  It sends
 *  the command plus argument, adding the appropriate CRC.  It then returns
 *  the one-byte response from the SD card.
 *
 *  For advanced commands (those with a command byte having bit 7 set), this
 *  routine automatically sends the required preface command (CMD55) before
 *  sending the requested command.
 *
 *  Upon exit, this routine returns the response byte from the SD card.
 *  Possible responses are:
 *    0xff	No response from card; card might actually be missing
 *    0x01  SD card returned 0x01, which is OK for most commands
 *    0x?? 	other responses are command-specific
 */
static  int8_t  sd_send_command(uint8_t  command, uint32_t  arg)
{
	uint8_t				response;
	uint8_t				i;
	uint8_t				crc;

	if (command & 0x80)					// special case, ACMD(n) is sent as CMD55 and CMDn
	{
		command = command & 0x7f;		// strip high bit for later
		response = sd_send_command(CMD55, 0);	// send first part (recursion)
		if (response > 1)  return response;
	}

	deselect();
	xchg(0xff);
	select();							// enable CS
	xchg(0xff);

    xchg(command | 0x40);				// command always has bit 6 set!
	xchg((unsigned char)(arg>>24));		// send data, starting with top byte
	xchg((unsigned char)(arg>>16));
	xchg((unsigned char)(arg>>8));
	xchg((unsigned char)(arg&0xff));
	crc = 0x01;							// good for most cases
	if (command == SD_GO_IDLE)  crc = 0x95;			// this will be good enough for most commands
	if (command == SD_SEND_IF_COND)  crc = 0x87;	// special case, have to use different CRC
    xchg(crc);         					// send final byte

	for (i=0; i<10; i++)				// loop until timeout or response
	{
		response = xchg(0xff);
		if ((response & 0x80) == 0)  break;	// high bit cleared means we got a response
	}

/*
 *  We have issued the command but the SD card is still selected.  We
 *  only deselect the card if the command we just sent is NOT a command
 *  that requires additional data exchange, such as reading or writing
 *  a block.
 */
	if ((command != SD_READ_BLK) &&
		(command != SD_READ_OCR) &&
		(command != SD_SEND_CSD) &&
		(command != SD_SEND_STATUS) &&
		(command != SD_SEND_CID) &&
		(command != SD_SEND_IF_COND) &&
		(command != SD_LOCK_UNLOCK) &&
		(command != SD_PROGRAM_CSD))
	{
		deselect();							// all done
		xchg(0xff);							// close with eight more clocks
	}

	return  response;					// let the caller sort it out
}



static int8_t  sd_wait_for_data(void)
{
	int16_t				i;
	uint8_t				r;

	for (i=0; i<100; i++)
	{
		r = xchg(0xff);
		if (r != 0xff)  break;
	}
	return  (int8_t) r;
}










