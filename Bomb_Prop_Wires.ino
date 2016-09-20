// Clock example using a seven segment display & GPS for time.
//
// Must have the Adafruit GPS library installed too!  See:
//   https://github.com/adafruit/Adafruit-GPS-Library
//
// Designed specifically to work with the Adafruit LED 7-Segment backpacks
// and ultimate GPS breakout/shield:
// ----> http://www.adafruit.com/products/881
// ----> http://www.adafruit.com/products/880
// ----> http://www.adafruit.com/products/879
// ----> http://www.adafruit.com/products/878
// ----> http://www.adafruit.com/products/746
//
// Adafruit invests time and resources providing this open source code,
// please support Adafruit and open-source hardware by purchasing
// products from Adafruit!
//
// Written by Tony DiCola for Adafruit Industries.
// Released under a MIT license: https://opensource.org/licenses/MIT

#include <SoftwareSerial.h>
#include <Wire.h>
#include "Adafruit_LEDBackpack.h"
#include "Adafruit_GFX.h"
#include "Chrono.h"               //Using Chrono library by Sofian Audry and Thomas Ouellet Fredericks
#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <ESP8266HTTPClient.h>    //HTTP Client to Post to Internet
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
//#include "Volume.h"               //Tone library enhanced.


Chrono secondsRunning(Chrono::SECONDS);         // Set up timer.
Chrono microSeconds(Chrono::MICROS);  // Debugging timer setup.

// I2C address of the display.  Stick with the default address of 0x70
// unless you've changed the address jumpers on the back of the display.
#define DISPLAY_ADDRESS   0x70
Adafruit_7segment clockDisplay = Adafruit_7segment();
int Adafruit_Brightness = 1;  // Brightness 0-15

// Set up global variables.
bool disarmed = false;         // Make sure disarmed starts out false.
int displayValueLast = 0;
WiFiServer server(80);

const short int LED_PIN = 16;  //Set GPIO pin for LED.
const short int TONE_PIN = 13; //Set GPIO pin for tone.
// Does not like GPIO15 for TONE on startup...won't program but otherwise works.

const short int WIRE[] = {5, 4, 12, 14};  //Set GPIO pins for wires.
// It crashes on pinMode() if GPIO1 is used.
// It won't startup with GPIO's 0, 2, 15 but runs fine after.
// It will startup with GPIO's 9 and 10 set to INPUT_PULLUP but locks when wires are applied.
// The following setup worked 0, 4, 5, 12, 14, but I needed 12 & 14 for IC2
// The following setup worked but I ran out of GPIO's for the buzzer: 4, 5, 12, 13.

// Basic Bomb Prop Settings
bool bombTriggered = false;
bool bombDisarmed = false;

// Wire Reading Settings
int  WIRE_debounceCount = 10;     // How many times debounce must be true in a row.
int  WIRE_debounceDelay = 10;     // Should be small because it delays the main loop of the program.
int  WIRE_debounceCounter[5];     // Count of same state for each wire.
bool WIRE_state[5] = {LOW};       // Current debounced state using boolean where LOW = connected and HIGH = disconnected.
bool WIRE_status[5];              // Instatenous status during debounce.
bool WIRE_last_status[5] = {LOW}; // Last State, using boolean where LOW = connected and HIGH = disconnected.
String WIRE_STATE = "";           // State of wires 0-4 in format XXXXX
String WIRE_LAST_STATE = "";      // Last state of wires 0-4 in format XXXXX.

// Timer Settings
int countdownTime = 3599;         // Duration of bomb countdown in seconds.
int secondsLeft = countdownTime;  // Seconds left on timer.
int secondsElapsed = 0;           // Seconds running.
int lastTime = 0;                 // Set up variable for beeping every second.
int debounceDelay = 25;           // How long to delay each debounce loop. This affects the main loop too.
// int milliSecondCounter = 0;

// Battery Monitor Settings
float vRefLowADC = 95;             // ADC Reading of Integer Point x(A) to Calibrate
float vRefLow = 0.28;              // Volatage Reading of Float Point y(A) to Calibrate
float vRefHighADC = 218;           // ADC Reading of Integer Point x(B) to Calibrate
float vRefHigh = 0.67;             // Volatage Reading of Float Point y(B) to Calibrate
float vM;                          // Slope of conversion
float vMcalibrate = 1.108;         // Calibration
float vB;                          // Y intercept of conversion
float R1 = 119700;                 // Resistor R1 Value
float R2 = 11700;                  // Resistor R2 Value
float vIn;                         // Battery Voltage
float vOut;                        // Voltage out of Divider into A0
// float vBatt[5];                 // Array to average values

// Setup IFTTT for SMS
/*  SMS via IFTTT
 *  String iftttMakerUrl = "https://maker.ifttt.com/trigger/bomb_prop_started/with/key/lsAw89rbmfI2ZlOrfVdioT43Qb5kKWn0FodfuBEeFvA";
*/
// Send update to IFTTT app.
String iftttMakerUrl = "https://maker.ifttt.com/trigger/bomb_update/with/key/lsAw89rbmfI2ZlOrfVdioT43Qb5kKWn0FodfuBEeFvA";

String SMSmessage = "";

void setup() {
  // Setup function runs once at startup to initialize the display.

  // Setup Serial port to print debug output.
  Serial.begin(115200);
  Wire.begin(0, 2);       // Define I2C data lines (SDA, SCL);
  delay(10);
  Serial.println();
  Serial.println("Clock starting!");

  // Setup the display.
  clockDisplay.begin(DISPLAY_ADDRESS);
  clockDisplay.setBrightness(Adafruit_Brightness);

  // Make sure clock is stopped.
  secondsRunning.restart(0); // Set counter to 0 seconds.
  secondsRunning.stop();     // Pause and wait for start signal in loop.

  // Set Timer to Debug in Microseconds
  microSeconds.restart(0);

  // Set up the ADC to read the volatage of the supply battery.
  // ADC_MODE(ADC_VCC);
  vM = vMcalibrate * (vRefHigh - vRefLow) / (vRefHighADC - vRefLowADC);
  Serial.print("Voltage conversion: vM = ");
  Serial.print(vM);
  vB = vRefLow - (vM * vRefLowADC);
  Serial.print(" and vB = ");
  Serial.println(vB);
  // vBatt[0] = 0;             // Seed and initial value.

  // Setup WiFi
  WiFiManager wifiManager;
  // String mySSID = "Paradox" + String(ESP.getChipId());  // Use ChipID to create unique SSID
  // Serial.print("Proposed name of SSID: ");
  // Serial.println(mySSID);
  wifiManager.autoConnect("Paradox_Bomb", "MCEscher");  // First parameter is name of access point, second is the password.
  // wifiManager.autoConnect(mySSID, "MCEscher");
  // Serial.print("Actual SSID: ");
  // Serial.println("ParadoxX");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  delay(10);

  // Start the server
  server.begin();
  Serial.println("Server started");
  delay(20);

  // Prepare Input/Output Pins
  pinMode(LED_PIN, OUTPUT);      //Onboard LED
  digitalWrite(LED_PIN, LOW);    //Initial state is ON

  for (int i = 0; i < 4; i++) {         //Configure wires to be disconnected to disarm.
    pinMode(WIRE[i], INPUT_PULLUP); //Set to inpute iwth internal pull-up resistor.
    Serial.print("Set pinMode for wire ");
    Serial.print(i);
    Serial.print(" which is GPIO ");
    Serial.println(WIRE[i]);
  }                                 //disconnected = HIGH | connected = LOW

  // Check wifi connection.
  Serial.print("Connected to: ");
  Serial.println(WiFi.SSID());
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  IPAddress HTTPS_ServerIP = WiFi.softAPIP(); // Obtain the IP of the Server
  Serial.print("Server IP is: "); // Print the IP to the monitor window
  Serial.println(HTTPS_ServerIP);

  // tone(TONE_PIN, 2000, 500);  //Will send a tone to TONE_PIN at 2000Hz for 0.5 seconds.
  //  vol.begin();

  // Send Startup Battery Level to SMS
  checkBattery();
  SMSmessage = "Battery(v) at startup is ";
  SMSmessage += vIn;
  sendSMS(SMSmessage);

  Serial.println("Now jumping to resetBomb routine.");
  resetBomb();                //Make sure bomb wires are set properly.
}

void loop() {
  // Loop function runs over and over again to implement the clock logic.
  //  int tart = .elapsed();

  // Determine Time Left
  secondsElapsed = secondsRunning.elapsed();
  secondsLeft = (countdownTime - (secondsElapsed));
  //  int lastTime = secondsLeft;
  ShowTime(secondsLeft);            // Send time left to display.

  // Beep While Clock is Ticking Down
  if (lastTime != secondsLeft) {    // Beep buzzer every time seconds changes.
    if (secondsLeft > 300) {        // High pitched short beep.
      tone(TONE_PIN, 1000, 10);
    }
    else {                          // During the last 5 minutes make beep longer and higher.
      tone(TONE_PIN, 1000, 500);
    }
    ProcessHTTP();                  // Check HTTP reqeusts at top of second only when running.
    // Serial.println("Calling HTTP WHILE running.");
    // checkBattery();
    // Serial.println("Ok, returned from 1st checkBattery function call.");
  }

  //  Serial.print(" since sartup is ");
  //  Serial.print(.elapsed());

  //  Serial.print(" and since sart of http request is ");
  //  Serial.println(.elapsed() - tart);

  // Check for Bomb Countdown Timeout (i.e. complete)
  if (secondsLeft <= 0) {           // Check Time and Fail if it ran out.
    Serial.println("Bomb detonated due to timeout.");
    checkBattery();
    SMSmessage = "Battery(v) at detonation due to timeout is ";
    SMSmessage += vIn;
    SMSmessage += " with ";
    SMSmessage += secondsLeft;
    SMSmessage += " seconds left.";
    sendSMS(SMSmessage);
    detonateBomb();
    Serial.println("Resetting bomb now.");
  }

  // Check for Wire Disarm Sequence (i.e. failure or disarmed)
  if (secondsRunning.isRunning()) {  // Only check wires when clock is running.
    checkWires();                    // Check wires to see if they are being disconnected in the right order.
  }
  else {
    ProcessHTTP();                  // When clock not running, check HTTP every pass.
    // Serial.println("Calling HTTP while NOT running.");
  }
  // Can put else statement here to call a different check wires and show if all are connected.
  lastTime = secondsLeft;         // Update lastTime to match secondsLeft;
  delay(debounceDelay);
  // Serial.println("Got to end of main loop.");
}

void ProcessHTTP() {

  /* Setup battery status to send SMS
    checkBattery();
    SMSmessage = "Battery(v) at ";
    SMSmessage += secondsLeft;
    SMSmessage += " seconds left is ";
    SMSmessage += vIn;
    SendSms(SMSmessage);
  */

  int startTime = microSeconds.elapsed();

  //  Process HTTP Reqeusts
  WiFiClient client = server.available(); // Check if a HTTP client has connected.
  if (client) {                           // If so, then process HTTP request.
    String request = client.readStringUntil('\r');
    // Serial.print("Request = ");
    // Serial.println(request);
    request.trim();
    // reqeust.toLowerCase();             // May be necessary if what is sent over doesn't match.
    Serial.print("Trimmed Request = ");
    Serial.println(request);

    // String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n";
    String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n";
    // s += "{";  // Open JSON statement.

    // Process HTTP reqeust
    if (request == "") {                                      // Do nothing with empty request, was causing problem.
      Serial.println("Got empty HTTP request.");
      client.flush();
      Serial.print("It took ");
      Serial.print(microSeconds.elapsed() - startTime);
      Serial.println(" microseconds to process.");
      SMSmessage = "Got empty HTTP request at ";
      SMSmessage += secondsLeft;
      SMSmessage += " which took ";
      SMSmessage += (microSeconds.elapsed() - startTime);
      SMSmessage += " microseconds to process.";
      sendSMS(SMSmessage);
      return;
    }
    else if (request == "GET /status HTTP/1.1") {
      // s += "status:";
      if (bombTriggered == true) {
        s += "failed";
      }
      else if (bombDisarmed == true) {
        s += "complete";
      }
      else if (secondsLeft > 0) {
        s += "incomplete";
      }
      else {
        s += "failed";
      }
    }
    else {
      if (request == "GET /trigger/start HTTP/1.1") {
        if (!secondsRunning.isRunning()) {                      // Only resume when not running.
          secondsRunning.resume();
        }
        s += "started";
      }
      else if (request == "GET /trigger/stop HTTP/1.1") {
        secondsRunning.stop();
        s += "stopped";
      }
      else if (request == "GET /trigger/reset HTTP/1.1") {
        secondsRunning.restart(0);
        secondsRunning.stop();
        s += "reset";
        resetBomb();
      }
      else if (request == "GET /trigger/time=58 HTTP/1.1") {
        secondsRunning.restart(119);
        s += "time=58";
      }
      else if (request == "GET /trigger/time=45 HTTP/1.1") {
        secondsRunning.restart(899);
        s += "time=45";
      }
      else if (request == "GET /trigger/time=30 HTTP/1.1") {
        secondsRunning.restart(1799);
        s += "time=30";
      }
      else if (request == "GET /trigger/time=15 HTTP/1.1") {
        secondsRunning.restart(2699);
        s += "time=15";
      }
      else if (request == "GET /trigger/time=10 HTTP/1.1") {
        secondsRunning.restart(2999);
        s += "time=10";
      }
      else if (request == "GET /trigger/time=5 HTTP/1.1") {
        secondsRunning.restart(3299);
        s += "time=5";
      }
      else if (request == "GET /trigger/time=3 HTTP/1.1") {
        secondsRunning.restart(3419);
        s += "time=3";
      }
      else if (request == "GET /trigger/time=1 HTTP/1.1") {
        secondsRunning.restart(3539);
        s += "time=1";
      }
      else if (request == "GET /state HTTP/1.1") {
        s += "status:";
        if (bombTriggered == true) {
          s += "failed";
        }
        else if (bombDisarmed == true) {
          s += "complete";
        }
        else if (secondsLeft > 0) {
          s += "incomplete";
        }
        else {
          s += "failed";
        }
        s += "</br>secondsLeft:";
        s += secondsLeft;
        s += "</br>battVoltage:";
        s += vIn;
      }
      checkBattery();
      SMSmessage = "Battery(v) at ";
      SMSmessage += secondsLeft;
      SMSmessage += " seconds left is ";
      SMSmessage += vIn;
      sendSMS(SMSmessage);
    }

    Serial.print("It took ");
    Serial.print(microSeconds.elapsed() - startTime);
    Serial.println(" microseconds to process.");

    /* Borrow SMSmessage string to send HTTP request time.
      SMSmessage = "The HTTP request ";
      SMSmessage += request;
      SMSmessage += " took ";
      SMSmessage += (microSeconds.elapsed() - startTime);
      SMSmessage += " microseconds to process.";
      sendSMS(SMSmessage);
    */

    // else if () {
    //   s = "<link rel=\"shortcut icon\" href="https://www.paradoxrooms.com/wp-content/uploads/2016/06/cropped-PARDOX-Logo-Box.png\">"
    // }
    // s += "}";                        // Close JSON
    s += "\r\n</html>\n";             // Close HTML Response Document

    // Send the response to the client
    Serial.println("The HTTP response is: ");
    Serial.println(s);
    client.print(s);
    delay(1);
    Serial.println("Client disonnected");
    client.flush();    // The client will actually be disconnected adnt the client object destroyed.
  }
}

void ShowTime(int secondsToDisplay) {                 //Sends time to display based on secondsToDisplay

  // Show the time on the display by turning it into a numeric
  // value, like 3:30 turns into 330, by multiplying the hour by
  // 100 and then adding the minutes.
  // int displayValue = hours*100 + minutes;

  // Convert Time Left to Display Value
  int displayMinutes = (secondsToDisplay / 60);
  int displaySeconds = (secondsToDisplay - (displayMinutes * 60));
  int displayValue = ((displayMinutes * 100) + displaySeconds);

  if (displayValue != displayValueLast) {       //Show displayValue for debugging.
    Serial.print("displayValue is ");
    Serial.println(displayValue);
    // Serial.print(" and secondsElapsed is ");
    // Serial.println(secondsElapsed);
    displayValueLast = displayValue;
  }

  // Now print the time value to the display.
  clockDisplay.print(displayValue, DEC);

  // Go in and explicitly add these zeros when time is low.
  // if (displayMinutes < 1) {                 // Add zero in one minutes place (_0:__).
  //    clockDisplay.writeDigitNum(1, 0);
  // }
  if (displaySeconds < 10) {                // Add zero in ten seconds place (__:0_).
    clockDisplay.writeDigitNum(2, 0);
  }
  clockDisplay.drawColon(displaySeconds % 2 == 0);
  clockDisplay.writeDisplay();      // Now push out to the display the new values that were set above.
}


void checkWires() {

  // Check status of wires here with debounce.
  // Success: Switch disarmed to success, play tune, pause for 5 mintues before
  // automaticall resetting.
  // Failure: Trigger buzzer for 10 seconds and then pause (make subroutine?)

  bombTriggered = false;          // Reset bomb bombTrigger.
  bombDisarmed = false;           // Reset bomb bombDisarmed.

  bool allConnected  = false;     // Assume all not connected at start.
  // bool WIRE_status[4];         // Instatenous status during debounce.

  int debounceCount = 10;         // How many times debounce must be true in a row.
  //  int debounceDelay = 100;    // How long to delay each debounce loop.
  int x = debounceCount;          // Load for first pass through while loop.
  int i;                          // Counter

  // Get status of all wires.
  //  Serial.print("WIRE_status[] are ");
  for (i = 0; i < 4; i++) {                            // Read pins 0-4.
    WIRE_status[i] = digitalRead(WIRE[i]);                 // Get status of wire.
    //    Serial.print(WIRE_status[i]);
    if (WIRE_status[i] == WIRE_last_status[i]) {           // If status is the same, then do debounce count and switch state when justified.
      WIRE_debounceCounter[i]++;                           // Incrment counter.
      if (WIRE_debounceCounter[i] > WIRE_debounceCount) {  // Check to see if time to change state.
        WIRE_state[i] = WIRE_status[i];                    // Change wire state to debounced wire status.
        //        WIRE_last_status[i] = WIRE_status[i];    // Update last state to match current state.
        WIRE_debounceCounter[i] = 0;                       // Reset counter to 0 with change of state so it doesn't overflow.
      }
    }
    else {                                                 // Every time the status changes reset the counter.
      WIRE_debounceCounter[i] = 0;
      WIRE_last_status[i] = WIRE_status[i];
    }
  }
  /*
    Serial.print(" with WIRE_debounceCounter[0] = ");
    Serial.print(WIRE_debounceCounter[0]);
    Serial.print(" and WIRE_state[0] of ");
    Serial.print(WIRE_state[0]);
    Serial.println(".");
  */
  // Check to make sure wires are disconnected in the right order.
  WIRE_STATE = "";
  // Serial.print("Wire states 0-3 are ");
  for (int i = 0; i < 4; i++) {
    WIRE_STATE = WIRE_STATE + String(WIRE_state[i] == HIGH);
    //    Serial.print(WIRE_state[i]);
  }
  //  Serial.print(" and last wire state was ");
  //  Serial.println((WIRE_LAST_STATE) + ".");
  if (WIRE_STATE == WIRE_LAST_STATE) {}                                 // No change, so ok.
  else if  (WIRE_STATE == "0000") {}                                    // Starting point, so ok.
  else if ((WIRE_STATE == "1000") && (WIRE_LAST_STATE = "0000")) {}     // Correct progression, so ok.
  else if ((WIRE_STATE == "1100") && (WIRE_LAST_STATE = "1000")) {}     // Correct progression, so ok.
  else if ((WIRE_STATE == "1110") && (WIRE_LAST_STATE = "1100")) {}     // Correct progression, so ok.
  else if ((WIRE_STATE == "1111") && (WIRE_LAST_STATE = "1110")) {      // Correct progression, bomb disarmed and exit function!
    Serial.println("Bomb disarmed!");
    checkBattery();
    SMSmessage = "Battery(v) at disarmed is ";
    SMSmessage += vIn;
    SMSmessage += " with ";
    SMSmessage += secondsLeft;
    SMSmessage += " seconds left.";
    sendSMS(SMSmessage);
    disarmBomb();
    return;
  }
  else {                                                                  // Otherwise, wrong progression so detonate bomb.
    Serial.println("Bomb detonated due to wrong sequence of wires!");
    checkBattery();
    SMSmessage = "Battery(v) at detonation due to wires is ";
    SMSmessage += vIn;
    SMSmessage += " with ";
    SMSmessage += secondsLeft;
    SMSmessage += " seconds left.";
    sendSMS(SMSmessage);

    detonateBomb();
    return;
  }
  WIRE_LAST_STATE = WIRE_STATE;
}  // End of checkWires

// Detonate Bomb
void detonateBomb() {

  int holdTime = 60;                              // How long to hold display befor auto-reset.
  secondsRunning.restart(0);
  secondsLeft = holdTime;

  Serial.println("Boom!!!  Bomb Detonated!");

  /*
    for (int i = 0; i < 4; i++) {             // Send "0000" to display.
      clockDisplay.writeDigitNum(i, 0);   // Send "0000" to display.
    }
    clockDisplay.drawColon(0);            // Send ":" to display to show "00:00:
    clockDisplay.writeDisplay();          // Write display to show it.
  */

  //  Serial.println("Buzzer on...");       // ??? Does the next statement freeze up the controller or let it keep going?
  tone(TONE_PIN, 2000, 5000);           // Send a tone to TONE_PIN at 1000Hz for 5 seconds.
  //  Serial.println("Buzzer off.");        // ??? If it doesn't freeze, does the tone not last very long?
  //                                        // ??? If it does, then will it not response to HTTP reqeusts?
  //                                        // Appears to NOT hold up rest of program.

  while (secondsLeft > 0) {                           // Freeze display for for holdTime but keep responding to HTTP requests.
    WiFiClient client = server.available();           //Continue to answer HTTP reqeusts
    if (client) {                                     // Check for HTTP reqeust and parse if necessary
      String request = client.readStringUntil('\r');
      Serial.print("Request = ");
      Serial.println(request);
      request.trim();
      Serial.print("Trimmed Request = ");
      Serial.println(request);
      String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n";
      // s += "{";  // Create string for JSON statement.
      // Process HTTP reqeust
      if (request == "GET /trigger/reset HTTP/1.1") {     // Respond to reset request and exit.
        secondsRunning.restart(0);
        secondsRunning.stop();
        s += "reset";
        s += "\r\n</html>\n";
        Serial.print("The HTTP response is: ");
        Serial.println(s);                                // Send to serial for debugging
        client.print(s);                                  // Send to HTTP client.
        delay(1);
        Serial.println("Client disonnected.");            // Send to serial for debugging
        client.flush();
        resetBomb();
        return;                                           // Jump out of function.
      }
      else if (request == "GET /status HTTP/1.1") {       //Reply to status requests and keep going.
        s += "failed";
        s += "\r\n</html>\n";
        Serial.print("The HTTP response is: ");
        Serial.println(s);
        client.print(s);
        delay(1);
        Serial.println("Client disonnected.");
        client.flush();
      }
    }
    // client.flush();  // The client will actually be disconnected when the function returns and 'client' object is detroyed
    delay(100);          // ??? This delay might be too long or short.
    secondsLeft = (holdTime - secondsRunning.elapsed());
    Serial.print("secondsLeft = ");
    Serial.println(secondsLeft);
  }
  resetBomb();                    //Reset Bomb
}

// Disarm Bomb
void disarmBomb() {       // Stop clock, hold display with solve time for 60 seconds, reset bomb.

  int holdTime = 60;                              // How long to hold display befor auto-reset.
  secondsRunning.restart(0);
  secondsLeft = holdTime;

  Serial.println("Hooray!  Bomb Deactivated!");

  //  Serial.println("Buzzer on...");  // Three high short beeps for success!
  tone(TONE_PIN, 5000, 100);           // Send a tone to TONE_PIN at 5000Hz for 0.1 seconds.
  delay(200);
  tone(TONE_PIN, 5000, 100);           // Send a tone to TONE_PIN at 5000Hz for 0.1 seconds.
  delay(200);
  tone(TONE_PIN, 5000, 100);           // Send a tone to TONE_PIN at 5000Hz for 0.1 seconds.
  delay(200);

  while (secondsLeft > 0) {                           // Freeze display for for holdTime but keep responding to HTTP requests.
    WiFiClient client = server.available();           //Continue to answer HTTP reqeusts
    if (client) {                                     // Check for HTTP reqeust and parse if necessary
      String request = client.readStringUntil('\r');
      Serial.print("Request = ");
      Serial.println(request);
      request.trim();
      Serial.print("Trimmed Request = ");
      Serial.println(request);
      String s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nAccess-Control-Allow-Origin: *\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\n";
      s += "";  // Create string for JSON statement.
      // Process HTTP reqeust
      if (request == "GET /trigger/reset HTTP/1.1") {     // Respond to reset request and exit.
        secondsRunning.restart(0);
        secondsRunning.stop();
        s += "{\"trigger\"\:\"reset\"}";
        s += "\r\n</html>\n";
        Serial.print("The HTTP response is: ");
        Serial.println(s);                                // Send to serial for debugging
        client.print(s);                                  // Send to HTTP client.
        delay(1);
        Serial.println("Client disonnected.");            // Send to serial for debugging
        client.flush();
        resetBomb();
        return;                                           // Jump out of function.
      }
      else if (request == "GET /status HTTP/1.1") {       //Reply to status requests and keep going.
        s += "{\"status\"\:\"complete\"}";
        s += "\r\n</html>\n";
        Serial.print("The HTTP response is: ");
        Serial.println(s);
        client.print(s);
        delay(1);
        Serial.println("Client disonnected.");
        client.flush();
      }
    }
    // client.flush();  // The client will actually be disconnected when the function returns and 'client' object is detroyed
    delay(100);          // ??? This delay might be too long or short.
    secondsLeft = (holdTime - secondsRunning.elapsed());
    Serial.print("secondsLeft = ");
    Serial.println(secondsLeft);
  }
  resetBomb();                    //Reset Bomb
}

// Reset Bomb
void resetBomb() {              // Resets the clock and waits for all wires to be hooked back up.

  bombTriggered = false;        // Reset bomb bombTrigger.
  bombDisarmed = false;         // Reset bomb bombDisarmed.
  secondsLeft = countdownTime;  // Seconds left on timer.
  secondsElapsed = 0;           // Seconds running.
  lastTime = 0;                 // Set up variable for beeping every second.

  bool allConnected  = false;    //Assume all not connected at start.
  bool STATUS_WIRE[4];

  secondsRunning.restart(0);     // Reset timer.
  secondsRunning.stop();         // Pause timer and wait for start signal in loop().

  int debounceCount = 10;        //How many times debounce must be true in a row.
  int x = debounceCount;         //Load for first pass through while loop.
  // int displayValue = 0;
  int i;                         //Counter to loop through

  //Wait on wires to be reconnected (assuming LOW when connected).
  while (x > 0) {                //Debounce: Countdown, must be true debounceCount times in a row.
    if (!allConnected) {         //Check if wires are not all connected.
      // displayValue = 0;
      Serial.print("Status of Wire ");
      for (i = 0; i < 4; i++) {               //Read pins 0-3 and send to display.
        STATUS_WIRE[i] = digitalRead(WIRE[i]);
        Serial.print(i);
        Serial.print(":");
        if (STATUS_WIRE[i] == HIGH) {         //For X_:__ digit.
          clockDisplay.writeDigitNum(i, 0);   //Not connected, so show 0
          Serial.print("0, ");
        }
        else {
          clockDisplay.writeDigitNum(i, 1);   // Connected, so show 1
          Serial.print("1, ");
          // displayValue = (displayValue + (10 ^ (3 - i)));
        }
        clockDisplay.writeDisplay();

        // With the above __:_X (position 3) is BLANK which is i=2, but it is checking it!?!?!
        // and i=3 is showing up in __:X_ (position 2).  ???

      }

      //      if (STATUS_WIRE[4] = HIGH) {        //For __X__ colon.
      //        clockDisplay.drawColon(0);        //Not connected, so blank
      //      }
      //      else {
      //        clockDisplay.drawColon(1);        //Not connected, so blank
      //      }

      // Serial.print("displayValue = ");
      // Serial.println(displayValue);
      // clockDisplay.print(displayValue, DEC);
      // clockDisplay.writeDisplay();                //Now push out to the display.

      allConnected = (!STATUS_WIRE[0] && !STATUS_WIRE[1] && !STATUS_WIRE[2] && !STATUS_WIRE[3]);
      Serial.print("and allConnected = ");
      Serial.println(allConnected);
    }
    //Check to see if allConnected for debounceCount before exiting.
    if (allConnected) {
      x--;
    }
    else {
      x = debounceCount;
    }
    delay(debounceDelay);
  }                                   //Display will show "11:11" for debounce period.
  WIRE_LAST_STATE = WIRE_STATE;       // Reset so it dosen't go off immeadately on second time through.
}

void checkBattery() {

  int adcValue;                         // adcValue is local but all others are global.

  adcValue = analogRead(A0);            // Get integer volatage value from ADC
  vOut = (vM * adcValue) + vB;          // Convert voltage from 10 bit integer to float.
  vIn = ((vOut * (R1 + R2)) / R2);      // Calcluate supply volatage.
}

void sendSMS(String message) {
  Serial.println("making POST request to ifttt for sending sms..\n");

  HTTPClient http;          // ?Create HTTP Client Object?

  http.begin(iftttMakerUrl, "A9 81 E1 35 B3 7F 81 B9 87 9D 11 DD 48 55 43 2C 8F C3 EC 87");  // httpsFingerprint

  http.addHeader("content-type", "application/json");
  int result = http.POST("{\"value1\":\"" + message + "\"}");

  Serial.println(String("status code: " + result).c_str());

  if (result > 0) {
    Serial.println("body:\r\n");
    Serial.println((http.getString() + "\r\n").c_str());
  } else {
    Serial.println("FAILED. error:");
    Serial.println((http.errorToString(result) + "\n").c_str());
    Serial.println("body:\r\n");
    Serial.println((http.getString() + "\r\n").c_str());
  }

  http.end();
}

