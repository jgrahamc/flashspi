// flashspi
//
// Small program to use with Arduino to connect to an SPI
// flash chip using the SPIMemory library. This program
// provides an interface over the serial connection at 115200
// baud that supports the following commands:
// 
// id   - prints the JEDEC, manufacturer and unique ID
// cap  - prints the flash chip's capacity in bytes
// dump - dumps the entire chip in hex (xxd format)
// wipe - wipes the entire chip
//
// Additionally, if a line in xxd format is sent to the code
// it will be written into the flash. Need to wipe first.
//
// This assumes that the flash chip is connected to the Arduino
// as follows:
//
// Arduino PIN       Flash chip
//  10                CS/SS
//  11                MOSI/DO
//  12                MISO/DI
//  13                SCK/CLK

#include <SPIMemory.h>

SPIFlash flash;

// printHex prints out 8, 12, ,16, 20, 24, 28 and 32 bit numbers
// in hex with leading zeroes. This is needed because Serial.print()
// on Arduino doesn't pad leading zeroes.
void printHex(uint32_t b, uint8_t bits) {
  char buffer[9];
  sprintf(buffer, "%08lx", b);

  Serial.print(&buffer[(32-bits)/4]); 
}

// printIDs dumps the three main IDs that can be extracted from
// the flash chip.
void printIDs() {
  printHex(flash.getJEDECID(), 24);
  Serial.println();
  printHex(flash.getManID(), 16);
  Serial.println();
  uint64_t u = flash.getUniqueID();
  printHex(u >> 32, 32);
  printHex(u & 0xFFFFFFFF, 32);
  Serial.println();
}

// printCapacity outputs the capacity of the flash chip in bytes.
void printCapacity() {
  Serial.println(flash.getCapacity());
}

// pages keeps count of the numbers of pages that need to be dumped
// and p is the next page to dump. p < pages then dumping should 
// happen. p == pages then the chip has been dumped.

static uint32_t pages = 0;
static uint32_t p = 0;

// dump starts a dump of the entire flash chip.
void dump() {
  pages = flash.getCapacity() / SPI_PAGESIZE;
  p = 0;
}

// _dump_page dumps a single page (256 bytes) from the flash chip
// in xxd default hex format. It moves the page counter p up by one
// so this can be repeatedly called.
void _dump_page() {
  if (p < pages) {
    uint32_t address = p*SPI_PAGESIZE;    
    uint8_t data[SPI_PAGESIZE];
    if (!flash.readByteArray(address, &data[0], SPI_PAGESIZE)) {
      Serial.println("Failed to read data from flash");
      flash.error(true);
      return;
    }
    
    for (int i = 0; i < SPI_PAGESIZE/16; i++) {
      printHex(address+i*16, 32);
      Serial.print(": ");
      for (int j = 0; j < 16; j += 2) {
        printHex(data[i*16+j],   8);
        printHex(data[i*16+j+1], 8);
        Serial.print(" ");       
      }
      Serial.print(" ");       
      for (int j = 0; j < 16; j++) {
        uint8_t c = data[i*16+j];
        if ((c < 32) || (c > 126)) {
          c = '.';
        }
        Serial.print(char(c));
      }
      
      Serial.println();
    }

    p += 1;
  } else {
    pages = 0;
    p = 0;
  }
}

// hex2byte converts two hex digits (upper or lowercase)
// to the byte they represent.
uint8_t hex2byte(char *s) {
  uint8_t b = 0;

  for (int i = 0; i < 2; i++) {
    if ((s[i] >= '0') && (s[i] <= '9')) {
      b = (b << 4) + s[i] - '0';
    } else {
      b = (b << 4) + toupper(s[i]) - 'A' + 10;
    }
  }
  
  return b;
}

// Buffer in which to keep data coming in from the serial connection
// until the \r is hit.
#define BUF_LENGTH 128
static char buf[BUF_LENGTH];
static int len = 0;

// _write_buffer parses and write the bytes from a single line
// of xxd format hex.
//
// 0123456789012345678901234567890123456789012345678901234567890123456
// 00003260: 5453 2d4d 4f4e 4f20 4d50 3320 185f 9da8  TS-MONO MP3 ._..
void _write_buffer() {
  uint32_t address = 0;
  int i;
  for (i = 0; i < 8; i += 2) {
    address <<= 8;
    address += hex2byte(&buf[i]);
  }

  uint8_t towrite[16];

  i += 2;

  printHex(address, 32);
  Serial.print(" -> ");

  for (int j = 0; j < 8; j++) {
    towrite[j*2]   = hex2byte(&buf[i]);
    towrite[j*2+1] = hex2byte(&buf[i+2]);
    i += 5;
    printHex(towrite[j*2], 8);
    printHex(towrite[j*2+1], 8);
    Serial.print(" ");
  }
  Serial.println();

  if (!flash.writeByteArray(address, &towrite[0], 16)) {
      Serial.println("Failed to write data to flash");
     flash.error(true);
   }
}

// handle deals with a command from the user or a line of xxd
// format hex to write to the chip.
void handle() {
  if (strcmp(buf, "id") == 0) {
    printIDs();
    return;
  }

  if (strcmp(buf, "cap") == 0) {
    printCapacity();
    return;
  }

  if (strcmp(buf, "dump") == 0) {
    dump();
    return;
  }

  if (strcmp(buf, "wipe") == 0) {
    if (!flash.eraseChip()) {
      Serial.println("Erasing chip failed");
      flash.error(true);
    }
    return;
  }

  // If we reach here this might be a line of hex to be written
  // into the flash. The format will be 8 hex digits followed
  // by a colon and the line will be 67 characters long.

  if (strlen(buf) == 67) {
    if (buf[8] == ':') {
      _write_buffer();
    }
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(100);
  }

  if (!flash.begin()) {
    Serial.println("Failed to set up the flash chip");    
    flash.error(true);
  }
}

void loop() {

  // If we're in the process of dumping memory then dump
  // out a page before handling any input.
  
  if (p < pages) {
    _dump_page();
  }

  // If there's something waiting on the serial connection
  // then read it and buffer until a \r is hit.
  
  while (Serial.available() > 0) {
    int d = Serial.read();
    Serial.print(char(d));
            
    if (d == '\r') {
      Serial.print('\n');
      buf[len] = 0;
      if (len > 0) {
        handle();
        len = 0;
      }
    } else {
      if (len < BUF_LENGTH - 1) {
        buf[len] = d;
        len += 1;
      }
    }
  }
}
