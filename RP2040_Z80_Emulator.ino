/*    RP2040 Z80 Emulator with Nascom Basic
      Will run CP/M 2.2 from SD card if detected     
      David J Bottrill April 2023

      Use the RP2040 core from:
      https://github.com/earlephilhower/arduino-pico

*/
#include <SPI.h>
#include <SD.h>
#include "zprintf.h"

#include "globals.h"
#include "init8250.h"
#include "basic.h"
#include "disk.h"
#include "cpu.h"

//*********************************************************************************************
//****                     Setup for CPU supervisor runs on core 0                         ****
//*********************************************************************************************
void setup() {

#ifdef ILI9341
  setup_tft();
#endif
  //BreakPoint switch input
  pinMode(swA, INPUT_PULLUP);
  //pinMode(swB, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  int ii = 20;
  while (ii > 0) {                    //Wait for serial port to connect
    digitalWrite(LED_BUILTIN, HIGH);  //Flash the LED until Serial connects
    delay(125);
    digitalWrite(LED_BUILTIN, LOW);  //Flash the LED until Serial connects;
    delay(125);
    if (Serial) ii = 1;  //Bail out if serial port connects
    ii--;
  }

  delay(500);
  Serial.println();
  Serial.println();
  Serial.println();
  Serial.write(27);   //Print "esc"
  Serial.print("c");  //Send esc c to reset screen

//Print the logon logo
  for(int i = 0; i <11; i++){
    Serial.println(banner[i]);
  };

  Serial.println("Initialising Virtual Disk Controller:");
  SPI.setRX(MISO);
  SPI.setTX(MOSI);
  SPI.setSCK(SCK);
  SPI1.setRX(MISO1);
  SPI1.setTX(MOSI1);
  SPI1.setSCK(SCK1);
  //try both SPI busses in order to try and find an SD card.
  sdfound = false;
  if (SD.begin(SS)) {
    Serial.println("SD Card Mount Success on SPI 0");
    sdfound = true;
  }
  if (sdfound == false) {
    if (SD.begin(SS1, SPI1)) {
      Serial.println("SD Card Mount Success on SPI 1");
      sdfound = true;
    }
  }
  if (sdfound == false) Serial.println("SD Card Mount Failed");


  //Initialise virtual PIO ports
  Serial.println("Initialising Virtual PIO Ports");
  portOut(GPP, 0);        //Port 0 GPIO A 0 - 7 off
  portOut(GPP + 1, 255);  //Port 1 GPIO A 0 - 7 Outputs
  portOut(GPP + 2, 0);    //Port 0 GPIO B 1 - 7 off
  portOut(GPP + 3, 255);  //Port 1 GPIO B 0 - 7 Outputs

  //Initialise virtual 6850 UART
  Serial.println("Initialising Virtual 6850 UART");
  pIn[UART_LSR] = 0x40;  //Set bit to say TX buffer is empty

#ifdef ARDUINO_RASPBERRY_PI_PICO_W
  Serial.print("Connecting to WiFi: ");
  // Initialize the WiFi module
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostName);
  WiFi.begin(ssid, password);
  // Wait for the WiFi connection to be established
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));  //Flash LED quickly while waiting for WiFi to connect
    delay(50);
  }
  Serial.println("Done!");
  Serial.println("Telnet Service Started");
  server.begin();
  Serial.print("Use 'telnet ");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
#endif

  bootstrap();  //Load boot images from SD Card or Flash

  //Depending if swA is pressed run in breakpoint - prompt for settings
  if (digitalRead(swA) == 0) {
    BP = 0x0000;  //Set initial breakpoint
    BPmode = 0;   //Mode 0 is normal run mode

    Serial.println("Breakpoints enabled");

    Serial.println("\n\rBreakpoint modes: ");
    Serial.println("1 - Single Step");
    Serial.println("2 - Single Step from Breakpoint");
    Serial.println("3 - Stop each time Breakpoint Reached");
    Serial.print("\n\rEnter Breakpoint mode: ");
    BPmode = getInput().toInt();
    if (BPmode > 3) BPmode = 3;
    if (BPmode < 1) BPmode = 1;
    zprintf("\n\rBreakpoint mode set to: %1d\n\r", BPmode);

    Serial.print("\n\rEnter Breakpoint address in HEX: ");
    BP = hexToDec(getInput());
    if (BP > 0xffff) BP = 0xffff;
    zprintf("\n\rBreakpoint set to: %.4X\n\r", BP);

    Serial.println("Press button A to start");
    buttonA();
  }


  switch (BPmode) {
    case 0:
      Serial.println("\n\rStarting Z80\n\r");
      SingleStep = false;
      bpOn = false;
      break;

    case 1:
      Serial.println("\n\rStarting Z80 in Single Step Mode\n\r");
      SingleStep = true;
      bpOn = false;
      dumpReg();  //Dump registers at start
      buttonA();  //Wait for button press to start
      break;

    case 2:
      Serial.println("\n\rStarting Z80 in Single step from Breakpoint Mode\n\r");
      bpOn = true;  //Run until BP
      SingleStep = false;
      break;

    case 3:
      Serial.println("\n\rStarting Z80 in Stop at Breakpoint Mode\n\r");
      bpOn = true;  //Run until BP
      SingleStep = false;
      break;
  }

  PC = 0;  //Set program counter
  RUN = true;
}



//*********************************************************************************************
//****                       Main control loop, runs on core 0                             ****
//*********************************************************************************************
void loop() {

  serialIO();  //Deal with serial I/O

#ifdef ILI9341
  update_tft();
#endif

  //if button A is pressed during run mode then reboot
  if (BPmode == 0 && digitalRead(swA) == 0) {  //Pressing button A will force a restart
    RUN = false;
    zprintf("\n\r\nCPU Halted @ %.4X ...rebooting...", PC - 1);
    buttonA();              //Wait for breakpoint button to be released
    portOut(GPP, 0);        //Port 0 GPIO A 0 - 7 off
    portOut(GPP + 1, 255);  //Port 1 GPIO A 0 - 7 Outputs
    portOut(GPP + 2, 0);    //Port 0 GPIO B 1 - 7 off
    portOut(GPP + 3, 255);  //Port 1 GPIO B 0 - 7 Outputs
    Serial.println();
    bootstrap();            //reload boot images from SD Card or Flash
    PC = 0;                 //Set program counter
    Serial.println("\n\rStarting Z80\n\r");
    RUN = true;
  }


  if (RUN == false) {  //if CPU has halted deal with it depending on Breakpoint mode
    switch (BPmode) {
      case 0:  //Run mode
        zprintf("\n\rCPU Halted @ %.4X ...rebooting...\n\r", PC - 1);
        bootstrap();            //reload boot images from SD Card or Flash
        PC = 0;                 //Set program counter
        portOut(GPP, 0);        //Port 0 GPIO A 0 - 7 off
        portOut(GPP + 1, 255);  //Port 1 GPIO A 0 - 7 Outputs
        portOut(GPP + 2, 0);    //Port 0 GPIO B 1 - 7 off
        portOut(GPP + 3, 255);  //Port 1 GPIO B 0 - 7 Outputs
        break;

      case 1:  //Single step mode
        dumpReg();
        buttonA();
        break;

      case 2:  //Single step from breakpoint mode
        dumpReg();
        buttonA();
        BPmode = 1;         //Switch to single step mode
        SingleStep = true;  //Carry on single stepping
        bpOn = false;
        break;

      case 3:  //Stop at breakpoint mode
        dumpReg();
        buttonA();
        break;
    }
    RUN = true;  //Restart CPU
  }
}



//*********************************************************************************************
//****                 Wait for button a to be pressed and released                        ****
//*********************************************************************************************
void buttonA(void) {
  if(digitalRead(swA) == 1){
    while (digitalRead(swA) == 1) delay(5);
  }
  while (digitalRead(swA) == 0) delay(5);
}


//*********************************************************************************************
//****                   Serial receive and send buffer function                           ****
//*********************************************************************************************
void serialIO(void) {
  char c;
#ifdef ARDUINO_RASPBERRY_PI_PICO_W
  if (server.hasClient()) {
    serverClient = server.available();
    Serial.print("\n\rNew Telnet client @ ");
    Serial.println(serverClient.remoteIP());

    serverClient.write(255);  // IAC
    serverClient.write(251);  // WILL
    serverClient.write(1);    // ECHO

    serverClient.write(255);  // IAC
    serverClient.write(251);  // WILL
    serverClient.write(3);    // suppress go ahead

    serverClient.write(255);  // IAC
    serverClient.write(252);  // WONT
    serverClient.write(34);   // LINEMODE
    delay(100);
    serverClient.write(27);   //Print "esc"
    serverClient.print("c");  //Send esc c to reset screen

    for(int i = 0; i <11; i++){
      serverClient.println(banner[i]);
    };

    while (serverClient.available()) serverClient.read();  //Get rid of any garbage received
    RUN = false;                                           //Force Z80 reboot
  }
#endif

  // Check for Received chars
  while (Serial.available()) {
    rxBuf[rxInPtr] = Serial.read();
    delay(2);
    rxInPtr++;
    if (rxInPtr == sizeof(rxBuf)) rxInPtr = 0;
  }

#ifdef ARDUINO_RASPBERRY_PI_PICO_W
  // Check for Received chars from Telnet
  while (serverClient.available()) {
    c = serverClient.read();
    if (c == 127) c = 8;  //Translate Backspace
    rxBuf[rxInPtr] = c;
    rxInPtr++;
    if (rxInPtr == sizeof(rxBuf)) rxInPtr = 0;
  }
#endif

  // Check if the virtual UART register is empty if so and there is a char waiting then put in UART register
  if (rxOutPtr != rxInPtr && bitRead(pIn[UART_LSR], 0) == 0) {  //Have we received any chars and the read buffer is empty?
    pIn[UART_PORT] = rxBuf[rxOutPtr];                           //Put char in UART port
    rxOutPtr++;                                                 //Inc Output buffer pointer
    if (rxOutPtr == sizeof(rxBuf)) rxOutPtr = 0;
    bitWrite(pIn[UART_LSR], 0, 1);  //Set bit to say char can be read
  }

  //Check for chars to be sent
  while (txOutPtr != txInPtr) {     //Have we received any chars?)
    Serial.write(txBuf[txOutPtr]);  //Send char to console
#ifdef ARDUINO_RASPBERRY_PI_PICO_W
    if (serverClient.connected()) serverClient.write(txBuf[txOutPtr]);  //Send via Telnet if client connected
#endif
    txOutPtr++;                                   //Inc Output buffer pointer
    if (txOutPtr == sizeof(txBuf)) txOutPtr = 0;  //Wrap around circular buffer
  }
}


//*********************************************************************************************
//****                       Serial input string function                                  ****
//*********************************************************************************************
String getInput() {
  bool gotS = false;
  String rs = "";
  char received;
  while (gotS == false) {
    while (Serial.available() > 0) {
      received = Serial.read();
      Serial.write(received);  //Echo input
      if (received == '\r' || received == '\n') {
        gotS = true;
      } else {
        rs += received;
      }
    }
  }
  return (rs);
}



//*********************************************************************************************
//****                           Convert HEX to Decimal                                    ****
//*********************************************************************************************
unsigned int hexToDec(String hexString) {
  unsigned int decValue = 0;
  int nextInt;
  for (int i = 0; i < hexString.length(); i++) {
    nextInt = int(hexString.charAt(i));
    if (nextInt >= 48 && nextInt <= 57) nextInt = map(nextInt, 48, 57, 0, 9);
    if (nextInt >= 65 && nextInt <= 70) nextInt = map(nextInt, 65, 70, 10, 15);
    if (nextInt >= 97 && nextInt <= 102) nextInt = map(nextInt, 97, 102, 10, 15);
    nextInt = constrain(nextInt, 0, 15);

    decValue = (decValue * 16) + nextInt;
  }
  return decValue;
}





//*********************************************************************************************
//****                        HEX Dump 256 byte block of RAM                               ****
//*********************************************************************************************
void dumpRAM(uint16_t s) {
  uint8_t i, ii;
  for (i = 0; i < 16; i++) {
    zprintf("%.4X  ", s + i * 16);
    for (ii = 0; ii < 16; ii++) {
      zprintf("%.2X ", RAM[s + ii + i * 16]);
    }
    Serial.println();
  }
}

//*********************************************************************************************
//****                       Dump Z80 Register for Breakpoint                              ****
//*********************************************************************************************
void dumpReg(void) {
  bitWrite(Fl, 7, Sf);
  bitWrite(Fl, 6, Zf);
  bitWrite(Fl, 4, Hf);
  bitWrite(Fl, 2, Pf);
  bitWrite(Fl, 1, Nf);
  bitWrite(Fl, 0, Cf);
  V16 = RAM[PC + 1] + 256 * RAM[PC + 2];  //Get the 16 bit operand
  zprintf("\n\n\r");
  zprintf("PC: %.4X  %.2X %.2X %.2X (%.2X)\n\r", PC, RAM[PC], RAM[PC + 1], RAM[PC + 2], RAM[V16]);
  zprintf("AF: %.2X %.2X\t\tAF': %.2X %.2X \n\r", A, Fl, Aa, Fla);
  zprintf("BC: %.2X %.2X (%.2X)\t\tBC': %.2X %.2X \n\r", B, C, RAM[(B * 256) + C], Ba, Ca);
  zprintf("DE: %.2X %.2X (%.2X)\t\tDE': %.2X %.2X \n\r", D, E, RAM[(D * 256) + E], Da, Ca);
  zprintf("HL: %.2X %.2X (%.2X)\t\tHL': %.2X %.2X \n\r", H, L, RAM[(H * 256) + L], Ha, La);
  zprintf("IX: %.4X  IY: %.4X\n\r", IX, IY);
  zprintf("SP: %.4X  Top entry: %.4X\n\r", SP, (RAM[SP] + (256 * RAM[SP + 1])));
  zprintf("S:%1d  Z:%1d  H:%1d  P/V:%1d  N:%1d  C:%1d\n\r\n", Sf, Zf, Hf, Pf, Nf, Cf);
}

