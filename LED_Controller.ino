#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#define FASTLED_ESP8266_RAW_PIN_ORDER
#include <FastLED.h>
#include <LittleFS.h>

//These two are not related
#define ledPin 15
#define numLEDs 13
#define resetPin 5
#define groundPin 16

#define fudge 1

ESP8266WebServer server(80);
CRGB colors[numLEDs];

byte* settings = NULL;
int settingsLength = 0;

int timeStuff = 0;
unsigned long lastMillis = 0;
int now = 0;
int transition = -1;
bool enabled = true;

void setup() {
  //Begin setup
  Serial.begin(115200);
  Serial.println("Beginning setup");

  //Just in case this helps
  delay(1000);

  //Set up filesystem
  Serial.println("Setting up LittleFS");
  LittleFS.begin();

  //Enable pins
  Serial.println("Setting output pin");  
  pinMode(ledPin, OUTPUT);

  //Create "reset" pins
  pinMode(resetPin, INPUT_PULLUP);
  pinMode(groundPin, OUTPUT);
  digitalWrite(groundPin, LOW);

  //Initialize LEDs to black
  Serial.println("Initializing LEDs");
  FastLED.addLeds<WS2812B, ledPin, GRB>(colors, numLEDs);
  for (int i = 0; i < numLEDs; i++) {
    colors[i] = CRGB(0, 0, 0);
  }

  //Allow for EEPROM reading
  EEPROM.begin(2048);
  //Uncomment these to initialize
  //EEPROM.write(0, 0);
  //EEPROM.commit();
  settingsLength = EEPROM.read(0);
  Serial.println(settingsLength);
  
  //Initialize to white if no data exists
  if (settingsLength == 0) {
    Serial.println("Setting LEDs");
    Serial.println(EEPROM.read(0));
    setAllWhite();
  }

  Serial.println("Refreshing LEDs");

  //Read from EEPROM and display values
  refreshLEDs();

  Serial.println("Turning on WiFi");

  //Set WiFi mode and config
  WiFi.mode(WIFI_AP);
  IPAddress apIP(192, 168, 0, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  //Private password plz do not steal
  WiFi.softAP("LEDControlsChase", "DasaniKeyton");

  //Route paths
  server.on("/", root);
  server.on("/data", data);
  server.on("/submit", submit);
  server.on("/getTimes", getTimes);
  server.on("/setTimes", setTimes);

  //Start server
  Serial.println("Turning on server");
  server.begin();
  Serial.println("Done with setup");

  //Enables the time thing
  lastMillis = millis();
}

//Main loops
void loop() {
  server.handleClient();

  //In case some bad data gets in there, you can short these pins to reset data! I'm great at this
  if (digitalRead(resetPin) == LOW) {
    setAllWhite();
    refreshLEDs();
  }

  //Do the part where we keep track of time
  if (millis() - lastMillis > 100) {
    bool shouldRefresh = false;
    now += millis() - lastMillis;
    //timeStuff += millis() - lastMillis;
    lastMillis = millis();
    if (now > 86400 * 1000 * fudge && enabled) {
      now = now % (int)(86400 * 1000 * fudge);
    }
    if (now > transition && !enabled) {
      enabled = true;
      shouldRefresh = true;
    } else if (now < transition && enabled) {
      enabled = false;
      shouldRefresh = true;
    }
    if (shouldRefresh) {
      refreshLEDs();
    }
    /*if (timeStuff > 10000) {
      //Serial.println("Ten seconds have passed");
      Serial.print(millis());
      Serial.print(" ");
      Serial.println(timeStuff);
      timeStuff = timeStuff % 10000;
    }*/
  }
}

//LED functions

//Set all LEDs to white on disk
void setAllWhite() {
  //Begin writing from 0
  int address = 0;
  //Set number of rows (One)
  EEPROM.write(address++, 1);
  //Set RGB value of row (#808080)
  EEPROM.write(address++, 0x80);
  EEPROM.write(address++, 0x80);
  EEPROM.write(address++, 0x80);
  
  //Set boolean values for each LED
  bool trueArray[numLEDs];
  for (int i = 0; i < numLEDs; i++) {
    trueArray[i] = true;
  }
  
  //Prepare byte array
  const int numBytes = getNumBytes();
  byte toSet[numBytes];

  //Create bytes
  boolsToBytes(trueArray, toSet);

  //Write toSet
  for (int i = 0; i < numBytes; i++) {
    EEPROM.write(address++, toSet[i]);
  }

  EEPROM.commit();
}

//Update LED stuff based on EEPROM
void refreshLEDs() {
  //Initialize each light to off
  for (int i = 0; i < numLEDs; i++) {
    colors[i] = CRGB(0, 0, 0);
  }
  
  //Read settings length and set address
  int address = 0;
  settingsLength = EEPROM.read(address++);
  
  //Prepare server values
  const int numBytes = getNumBytes();
  settings = (byte*) realloc(settings, settingsLength * (3 + numBytes) * sizeof(byte));

  //Read each row
  for (int i = 0; i < settingsLength; i++) {
    //First three values are RGB values
    settings[i * (3 + numBytes) + 0] = EEPROM.read(address++);
    settings[i * (3 + numBytes) + 1] = EEPROM.read(address++);
    settings[i * (3 + numBytes) + 2] = EEPROM.read(address++);

    //Read row byte values into buffer
    byte rowBytes[numBytes];
    for (int j = 0; j < numBytes; j++) {
      rowBytes[j] = EEPROM.read(address++);
    }

    //Convert into boolean
    bool ledBools[numLEDs];
    bytesToBools(rowBytes, ledBools);

    //Set LED values based on settings
    for (int j = 0; j < numLEDs; j++) {
      if (ledBools[j] && enabled) {
        colors[j] = CRGB(settings[i * (3 + numBytes) + 0],
                         settings[i * (3 + numBytes) + 1],
                         settings[i * (3 + numBytes) + 2]);
      }
    }

    //Save bytes buffer to settings
    for (int j = 0; j < numBytes; j++) {
      settings[(i * (3 + numBytes)) + 3 + j] = rowBytes[j];
    }
  }

  FastLED.show();
}

//Data functions

//Get the number of bytes needed to represent the current number of LEDs
int getNumBytes() {
  //Bad estimate (Wrong for 0, 8 ,16, etc.)
  int toReturn = numLEDs / 8 + 1;
  //Fix estimate by handling those cases
  if (numLEDs % 8 == 0) {
    toReturn -= 1;
  }
  return toReturn;
}

//Converts an array of bytes to an array of booleans. Bases size off numLEDs
void bytesToBools(byte* in, bool* out) {
  for (int i = 0; i < numLEDs; i++) {
    out[i] = bool((in[i / 8] & (1 << (i % 8))) > 0); //Extract individual bit from correct byte
  }
}

//Converts an array of bytes to an array of booleans. Bases size off numLEDs
void boolsToBytes(bool* in, byte* out) {
  //Get the number of bytes
  int numBytes = getNumBytes();
  //Initialize all of them to 0
  for (int i = 0; i < numBytes; i++) {
    out[i] = 0;
  }
  //Set each bit
  for (int i = 0; i < numLEDs; i++) {
    if (in[i]) {
      out[i / 8] |= (1 << (i % 8)); //Set the correct bit of the correct byte
    }
  }
}


//Server functions

void root() {
  //Send main HTML file
  File file = LittleFS.open("main.html", "r");
  server.streamFile(file, "text/html");
  file.close();
}

void data() {
  //Prepare data buffer
  char dataBuffer[192];
  //Put all the settings into it
  for (int i = 0; i < settingsLength * (3 + getNumBytes()); i++) {
    sprintf(dataBuffer + (2 * i), "%02x", settings[i]);
  }
  //Print it alongside the number of LEDs
  char toSend[256];
  sprintf(toSend, "%d:%s", numLEDs, dataBuffer);
  //Send to client
  server.send(200, "text/plain", toSend);
}

void submit() {
  //Get data from request
  String rows = server.arg("rows");
  String data = server.arg("data");

  //Print for debugging
  Serial.println(rows);
  Serial.println(data);

  //Write the new number of rows
  int address = 0;
  EEPROM.write(address++, rows.toInt());

  //Get the position data
  const char* pos = data.c_str();

  //Copy each byte to EEPROM
  for (int i = 0; i < rows.toInt() * (3 + getNumBytes()); i++) {
    byte toWrite = 0;
    sscanf(pos + i * 2, "%2hhx", &toWrite); //Need to convert from hex to byte
    EEPROM.write(address++, toWrite);
  }

  //Finish
  EEPROM.commit();
  server.send(200, "text/plain", "Success!");

  //Show changes
  refreshLEDs();
}

void getTimes() {
  char toSend[256];
  sprintf(toSend, "%d:%d", (int) (now / fudge), (int) (transition / fudge));
  server.send(200, "text/plain", toSend);
}

void setTimes() {
  String transitionText = server.arg("transition");
  String nowText = server.arg("now");
  transition = (int) (transitionText.toInt() * fudge);
  now = (int) (nowText.toInt() * fudge);
  Serial.println(now);
  Serial.println(transition);
  server.send(200, "text/plain", "Success!");
}
