/*
 Mouse Ear Recording Playback Tool

 (C) 2014 Jonathan Fether

 --

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.

 --

 This app plays back a file generated by Mouse Ear Recorder.

 The format is:

 XXXXXXXX: YY YY ... YY

 Where XXXXXXXX is an offset in milliseconds as a hexadecimal long (Generated by millis()) and YY is the 2400 baud serial data captured.

 For example, this message:
 002532CD: 55 AA 16 01 01 02 0E 05 2D

 Means output the byte sequence "55 AA 16 01 01 02 0E 05 2D" at time offset 0x002532CD (2437837 msec = 40m 37s 837msec).

 */
//#include <SD.h>
#include "SPIFFS.h"
// #include <stdio.h>

// On the Ethernet Shield, CS is pin 4. Note that even if it's not
// used as the CS pin, the hardware CS pin (10 on most Arduino boards,
// 53 on the Mega) must be left as an output or the SD library
// functions will not work.
//const int chipSelect = 4;
File datafile;

// The clock drifts while we send bytes, this throws off the show timing.
unsigned long clockdrift = 0;

#define SeekOffset 0 // Seek offset for header, if any.

#define ShowOffset 0x00000000   // Offset of show start time, for seeking in.

#define IRledPin 12  // Pin with IR LED connected in series with a current limiting resistor, or more powerful IR emitter via transistor.
#define DataBaud 2400 // Mouse Ear Protocol. We'll work on implementing this correctly later but for now I'll just leave it #defined and use fixed values for experimentation.

// This procedure sends a 38KHz pulse to the IRledPin
// for a certain # of microseconds. We'll use this whenever we need to send code.
// It's almost exactly LadyAda's modulation code but for some reason the timings were off on my board.

void pulseIR(long microsecs, int hilo) {
    // we'll count down from the number of microseconds we are told to wait

    while (microsecs > 0) {
        // 38 kHz is about 13 microseconds high and 13 microseconds low
        digitalWrite(IRledPin, hilo);  // this takes about 5 microseconds to happen
        delayMicroseconds(8);         // hang out for 8 microseconds
        digitalWrite(IRledPin, LOW);   // this also takes about 5 microseconds
        delayMicroseconds(8);         // hang out for 8 microseconds
        // so 26 microseconds altogether
        microsecs -= 26;
    }

}

// Send 8-N-1 data modulated at 38kHz and 2400 baud
void sendbyte(byte b)
{
    // Data consists of 1 Start Bit, 8 Data Bits, 1 Stop Bit, and NO parity bit
    // 1 bit is 417 microseconds @ 2400 baud
    pulseIR(400, HIGH); // Start bit
    byte i=0;
    while(i<8)
    {
        pulseIR(400, (b>>(i++)&1)?LOW:HIGH); // Data Bits
    }
    pulseIR(400, LOW); // Stop bit
}

void setup()
{
    // Open serial communications and wait for port to open:
    Serial.begin(115200);
    while (!Serial) {
        ; // wait for serial port to connect. Needed for Leonardo only
    }

    if(!SPIFFS.begin(true)){
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

    datafile = SPIFFS.open("/MRDF0007.TXT");
    if(!datafile){
        Serial.println("Failed to open file for reading");
        return;
    }
    if(datafile) Serial.println("File opened");

    datafile.seek(SeekOffset);

    pinMode(IRledPin, OUTPUT);
    pinMode(13, OUTPUT); // Clock Timing Monitor Pin
    Serial.print("Startstamp: ");
    Serial.println(millis());
}

byte bytefromhex(char hexed[2])
{
    // Since this application has no security risk, I'm just going to use a cheap ASCII offset.
    if(hexed[1]>'9') hexed[1] -= 7; // (7 is the offset from 'A' to ':' which follows '9')
    if(hexed[0]>'9') hexed[0] -= 7;
    return (byte)(((hexed[0] - '0')<<4) + hexed[1] - '0');
}

long longfromhex(byte hexed[8])
{
    long tmpl = 0;
    // Serial.println();
    for(int i = 0; i<8; i++)
    {
        if(hexed[i]>'9') hexed[i] -= 7;
        // Serial.print(hexed[i]-'0', HEX);
        tmpl+=((long)(hexed[i]-'0'))<<(4*(7-i)); // Bitshift each nibble. Have to be careful where the (long) is.
    }
    return tmpl;
}

void loop()
{
    int i;
    byte offbuf[8];
    for(int i=0;i<8;i++) offbuf[i] = datafile.read();  // Read 8 bytes (Program offset)
    datafile.read(); // ":"
    datafile.read(); // " " - Advanced to data
    long offset = longfromhex(offbuf);

    byte outbuf[256];
    byte ptr=0;
    while(datafile.peek()>='0')
    {
        char bytebuf[2];
        bytebuf[0] = datafile.read();
        bytebuf[1] = datafile.read();
        datafile.read(); // " "
        byte b=bytefromhex(bytebuf);
        outbuf[ptr++]=b;
    }

    while((offset-(ptr*4))>(millis()+ShowOffset+(clockdrift/1000))) // Show start offset.
    {
        // Wait for cue
   }

    if(offset>ShowOffset)
    {
        if(outbuf[0]==0x55 & outbuf[1]==0xaa) // Control Message
        {
            if((outbuf[2]==0x16)||(outbuf[2]==0x19)||(outbuf[2]==0x09)) // Moderate protocol reverse engineering
            {
                // Clock Message. Note that 0x16 and 0x19 are actually lengths, so this might not be correct per se...
                if (outbuf[2]==0x16)
                {
                    Serial.println("Show Clock Standby Keepalive");
                }
                else if((outbuf[2]&0x0f)==0x09)
                {
                    Serial.print("Show Clock: ");
                    Serial.print(outbuf[8], DEC);
                    Serial.print(":");
                    if(outbuf[9]<10) Serial.print("0");
                    Serial.print(outbuf[9], DEC);
                    Serial.print(":");
                    if(outbuf[10]<10) Serial.print("0");
                    Serial.print(outbuf[10], DEC);
                    Serial.print(", System Clock: ");
                    unsigned long tstamp=millis()+clockdrift;
                    Serial.print(tstamp/3600000, DEC); // Hours
                    Serial.print(":");
                    if(tstamp%3600000<600000) Serial.print("0");
                    Serial.print((tstamp%3600000)/60000, DEC); // Minutes
                    Serial.print(":");
                    if(tstamp%60000<10000) Serial.print("0");
                    Serial.print((tstamp%60000)/1000, DEC); // Seconds
                    Serial.print(".");
                    if(tstamp%1000<100) Serial.print("0");
                    if(tstamp%1000<10) Serial.print("0");
                    Serial.println(tstamp%1000);

                }
            }
        }

        for(i=0; i<=ptr; i++)
        {
            sendbyte(outbuf[i]);
        }
    }
    while(datafile.peek()>=' ') datafile.read(); // Advance to EOL (Read all printable chars)
    while(datafile.peek()<'0') datafile.read(); // Advance to beginning of next line
}
