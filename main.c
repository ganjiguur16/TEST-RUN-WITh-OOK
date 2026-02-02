#include <16F1789.h>
#use delay(clock=16M, crystal)
#FUSES NOWDT, NOBROWNOUT, HS

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// --- UART CONFIGURATION ---
// Assuming OBC is connected to these pins (Stream: PC)
#use rs232(baud=9600, parity=N, xmit=PIN_C6, rcv=PIN_C7, bits=8, stream=PC) 
#use rs232(baud=9600, parity=N, xmit=PIN_B1, rcv=PIN_B2, bits=8, stream=EXT) 
#use spi(MASTER, CLK=PIN_C3, DI=PIN_C4, DO=PIN_C5, BAUD=1000000, BITS=8, STREAM=RF_SPI, MODE=0)

// --- GLOBALS & BUFFER ---
int32 uptime_seconds = 0;
char beacon_payload[32]; // Buffer for internal beacon
char rx_buffer[32];      // Buffer for incoming UART data from OBC
unsigned int8 rx_index = 0;
int1 msg_received = 0;   // Flag to signal main loop

// RFM98 Definitions (Same as your original code)
#define CS_PIN_RFM     PIN_D7
#define RESET_PIN_RFM  PIN_D5
// ... [Keep your original Register Defines here] ...
#define REG_OPMODE     0x01
#define REG_FRF_MSB    0x06
#define REG_FRF_MID    0x07
#define REG_FRF_LSB    0x08
#define REG_PA_CONFIG  0x09
#define REG_PA_RAMP    0x0A 
#define REG_OCP        0x0B
#define REG_PA_DAC     0x4D
#define MODE_STDBY     0x09 
#define MODE_TX        0x0B 
#define UNIT_MS        50   

// --- UART INTERRUPT (New Code) ---
// This runs automatically whenever data arrives on Pin C7
#INT_RDA
void serial_isr() {
   char c;
   c = fgetc(PC); // Read char from OBC

   // Check for Terminator (Carriage Return)
   if(c == '\r') {
      rx_buffer[rx_index] = '\0'; // Add null terminator to make it a valid string
      msg_received = 1;           // Tell main loop we have a message
      rx_index = 0;               // Reset buffer for next time
   } 
   // Safety check: Prevent buffer overflow
   else if(rx_index < 30) {
      rx_buffer[rx_index] = c;
      rx_index++;
   }
}

// --- Low Level RFM ---
void rfm_write(unsigned int8 reg, unsigned int8 val) {
   output_low(CS_PIN_RFM);
   spi_xfer(RF_SPI, reg | 0x80);
   spi_xfer(RF_SPI, val);
   output_high(CS_PIN_RFM);
}

void rfm_set_freq_hz(unsigned int32 freq_hz) {
   unsigned int32 frf = (unsigned int32)((float)freq_hz / 61.03515625);
   rfm_write(REG_FRF_MSB, (unsigned int8)(frf >> 16));
   rfm_write(REG_FRF_MID, (unsigned int8)(frf >> 8));
   rfm_write(REG_FRF_LSB, (unsigned int8)(frf));
}

void RFM_Config_OOK_SafePower(unsigned int32 freq_hz) {
   rfm_write(REG_OPMODE, 0x00); // Sleep
   delay_ms(10);
   rfm_write(REG_OPMODE, 0x21); // OOK Mode + Standby
   delay_ms(10);

   rfm_set_freq_hz(freq_hz);

   // 1. MAX POWER (PA_BOOST)
   rfm_write(REG_PA_CONFIG, 0xFF); 

   // 2. +20dBm Turbo Mode
   rfm_write(REG_PA_DAC, 0x87); 

   // 3. High Current Limit (150mA)
   rfm_write(REG_OCP, 0x3B); 

   // 4. SOFT RAMP (Crucial for preventing dropouts at 50ms speed)
   // 0x09 = 40us Rise Time. This stops the "Click" from crashing the voltage.
   rfm_write(REG_PA_RAMP, 0x09); 
}

// Ensure you include your Morse functions here!
void rf_on()  { rfm_write(REG_OPMODE, 0x20 | MODE_TX); }
void rf_off() { rfm_write(REG_OPMODE, 0x20 | MODE_STDBY); }

void dot()  { rf_on(); delay_ms(UNIT_MS);   rf_off(); delay_ms(UNIT_MS); }
void dash() { rf_on(); delay_ms(3*UNIT_MS); rf_off(); delay_ms(UNIT_MS); }

void send_morse_char(char c) {
   if(c == ' ') { delay_ms(4*UNIT_MS); return; }
   if(c >= 'a' && c <= 'z') c -= 32; 
   switch(c) {
      case 'A': dot(); dash(); break;
      case 'B': dash(); dot(); dot(); dot(); break;
      case 'C': dash(); dot(); dash(); dot(); break;
      case 'D': dash(); dot(); dot(); break;
      case 'E': dot(); break;
      case 'F': dot(); dot(); dash(); dot(); break;
      case 'G': dash(); dash(); dot(); break;
      case 'H': dot(); dot(); dot(); dot(); break;
      case 'I': dot(); dot(); break;
      case 'J': dot(); dash(); dash(); dash(); break;
      case 'K': dash(); dot(); dash(); break;
      case 'L': dot(); dash(); dot(); dot(); break;
      case 'M': dash(); dash(); break;
      case 'N': dash(); dot(); break;
      case 'O': dash(); dash(); dash(); break;
      case 'P': dot(); dash(); dash(); dot(); break;
      case 'Q': dash(); dash(); dot(); dash(); break;
      case 'R': dot(); dash(); dot(); break;
      case 'S': dot(); dot(); dot(); break;
      case 'T': dash(); break;
      case 'U': dot(); dot(); dash(); break;
      case 'V': dot(); dot(); dot(); dash(); break;
      case 'W': dot(); dash(); dash(); break;
      case 'X': dash(); dot(); dot(); dash(); break;
      case 'Y': dash(); dot(); dash(); dash(); break;
      case 'Z': dash(); dash(); dot(); dot(); break;
      case '0': dash(); dash(); dash(); dash(); dash(); break;
      case '1': dot(); dash(); dash(); dash(); dash(); break;
      case '2': dot(); dot(); dash(); dash(); dash(); break;
      case '3': dot(); dot(); dot(); dash(); dash(); break;
      case '4': dot(); dot(); dot(); dot(); dash(); break;
      case '5': dot(); dot(); dot(); dot(); dot(); break;
      case '6': dash(); dot(); dot(); dot(); dot(); break;
      case '7': dash(); dash(); dot(); dot(); dot(); break;
      case '8': dash(); dash(); dash(); dot(); dot(); break;
      case '9': dash(); dash(); dash(); dash(); dot(); break;
   }
   delay_ms(2*UNIT_MS); 
}
void send_morse_string(char *s) {
   // --- WARM UP SEQUENCE (Essential for High Power) ---
   // Holds the carrier ON for 2 seconds to stabilize voltage sag
   rf_on();
   delay_ms(2000); 
   rf_off();
   delay_ms(1000); // 1 second silence before data
   
   while(*s) { send_morse_char(*s); s++; }
}

// --- MAIN LOOP ---
void main() {
   int32 beacon_timer = 0;

   // 1. Enable Interrupts
   enable_interrupts(INT_RDA); // Enable UART Receive interrupt
   enable_interrupts(GLOBAL);  // Enable Global interrupts

   delay_ms(500); 
   fprintf(EXT, "\r\n--- COM SYSTEM READY ---\r\n");
   
   output_high(CS_PIN_RFM);
   RFM_Config_OOK_SafePower(437135000UL); // Your original Config

   while(TRUE) {
      delay_ms(1);
      beacon_timer++;

      // --- PRIORITY 1: Check for Message from OBC ---
      if(msg_received == 1) {
         fprintf(EXT, "\r\n[OBC RX]: %s", rx_buffer);
         
         // Transmit the Hex String received from OBC
         send_morse_string(rx_buffer); 
         
         fprintf(EXT, " [TX DONE]\r\n");
         
         msg_received = 0; // Reset flag
         beacon_timer = 0; // Reset timer so we don't send internal beacon immediately after
      }

      // --- PRIORITY 2: Internal Beacon (Every 30s) ---
      if(beacon_timer > 30000) {
         uptime_seconds += 30;
         sprintf(beacon_payload, "TML1CUBESAT UP %luS", uptime_seconds);
         
         fprintf(EXT, "\r\n[INTERNAL TX]: %s", beacon_payload);
         send_morse_string(beacon_payload);
         fprintf(EXT, " [DONE]\r\n");
         
         beacon_timer = 0;
      }
   }
}