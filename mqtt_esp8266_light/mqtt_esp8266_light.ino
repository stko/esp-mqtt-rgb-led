/*
 * ESP8266 MQTT Lights for Home Assistant.
 *
 * Created DIY lights for Home Assistant using MQTT and JSON.
 * This project supports single-color, RGB, and RGBW lights.
 *
 * Copy the included `config-sample.h` file to `config.h` and update
 * accordingly for your setup.
 *
 * See https://github.com/corbanmailloux/esp-mqtt-rgb-led for more information.
 */

/*
 * Complete Hardware can be found here:
 * 
 * https://nathan.chantrell.net/20170525/h801-wi-fi-rgbw-led-controller-with-mqtt-esp8266/
 * https://www.aliexpress.com/item/RGBWW-Strip-WiFi-Controller-1-Port-Control-200-Lights-Communicate-with-Android-Phone-Via-WLAN-to/32502007408.html
 *  
 */ 

// Set configuration options for LED type, pins, WiFi, and MQTT in the following file:
#include "config.h"

// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>

#include <ESP8266WiFi.h>

// http://pubsubclient.knolleary.net/
#include <PubSubClient.h>

const bool rgb = (CONFIG_STRIP == RGB) || (CONFIG_STRIP == RGBW ) || (CONFIG_STRIP == RGBWW);
const bool includeWhite = (CONFIG_STRIP == BRIGHTNESS) || (CONFIG_STRIP == RGBW) || (CONFIG_STRIP == RGBWW);
const bool includeColdWhite = (CONFIG_STRIP == BRIGHTNESS)  || (CONFIG_STRIP == RGBWW);

const int BUFFER_SIZE = JSON_OBJECT_SIZE(20);

#include "sunrise.h"
// Maintained state for reporting to HA
byte red = 255;
byte green = 255;
byte blue = 255;
byte white = 255;
byte coldWhite = 255;
byte brightness = 255;

// Real values to write to the LEDs (ex. including brightness and state)
byte realRed = 0;
byte realGreen = 0;
byte realBlue = 0;
byte realWhite = 0;
byte realColdWhite = 0;

bool stateOn = false;

// Globals for fade/transitions
bool startFade = false;
unsigned long lastLoop = 0;
unsigned long transitionTime = 0;
bool inFade = false;
int loopCount = 0;
int stepR, stepG, stepB, stepW, stepCW;
int redVal, grnVal, bluVal, whtVal, cwhtVal;

// Globals for flash
bool flash = false;
bool startFlash = false;
int flashLength = 0;
unsigned long flashStartTime = 0;
byte flashRed = red;
byte flashGreen = green;
byte flashBlue = blue;
byte flashWhite = white;
byte flashColdWhite = coldWhite;
byte flashBrightness = brightness;

// Globals for colorfade
bool colorfade = false;

// Globals for Movies
moviePixel * movie=0;
const movieStruct * actualMovie = 0;
unsigned long startMovieTime = 0;
unsigned long lastFrame = 0;
unsigned long frameStartTime=0;
bool movieLoop;


int currentColor = 0;
// {red, grn, blu, wht}
const byte colors[][4] = {
  {255, 0, 0, 0},
  {0, 255, 0, 0},
  {0, 0, 255, 0},
  {255, 80, 0, 0},
  {163, 0, 255, 0},
  {0, 255, 255, 0},
  {255, 255, 0, 0}
};
const int numColors = 7;

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {

#ifdef MODULE_H801

  //********** CHANGE PIN FUNCTION  TO TX/RX **********
  // taken from https://www.esp8266.com/wiki/doku.php?id=esp8266_gpio_pin_allocations#pin_functions
  //GPIO 2 (TX) swap the pin to a TX.
  pinMode(2, FUNCTION_4); 
  //GPIO 3 (RX) swap the pin to a RX.
  // pinMode(3, FUNCTION_0); 
  //***************************************************

#endif


  
  if (rgb) {
    pinMode(CONFIG_PIN_RED, OUTPUT);
    pinMode(CONFIG_PIN_GREEN, OUTPUT);
    pinMode(CONFIG_PIN_BLUE, OUTPUT);
  }
  if (includeWhite) {
    pinMode(CONFIG_PIN_WARM_WHITE, OUTPUT);
  }
  if (includeColdWhite) {
    pinMode(CONFIG_PIN_COLD_WHITE, OUTPUT);
  }

  // Set the BUILTIN_LED based on the CONFIG_BUILTIN_LED_MODE
  switch (CONFIG_BUILTIN_LED_MODE) {
    case 0:
      pinMode(BUILTIN_LED, OUTPUT);
      digitalWrite(BUILTIN_LED, LOW);
      break;
    case 1:
      pinMode(BUILTIN_LED, OUTPUT);
      digitalWrite(BUILTIN_LED, HIGH);
      break;
    default: // Other options (like -1) are ignored.
      break;
  }

  analogWriteRange(255);

  if (CONFIG_DEBUG) {
    Serial.begin(115200);
  }

  setup_wifi();
  client.setServer(CONFIG_MQTT_HOST, CONFIG_MQTT_PORT);
  client.setCallback(callback);
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(CONFIG_WIFI_SSID);

  WiFi.mode(WIFI_STA); // Disable the built-in WiFi access point.
  WiFi.begin(CONFIG_WIFI_SSID, CONFIG_WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

  /*
  SAMPLE PAYLOAD (BRIGHTNESS):
    {
      "brightness": 120,
      "flash": 2,
      "transition": 5,
      "state": "ON"
    }

  SAMPLE PAYLOAD (RGBW):
    {
      "brightness": 120,
      "color": {
        "r": 255,
        "g": 100,
        "b": 100
      },
      "white_value": 255,
      "cold_white_value": 255, //optional to be compatible 
      "flash": 2,
      "transition": 5,
      "state": "ON",
      "effect": "colorfade_fast"
    }

{"brightness": 120, "color": {"r": 255,"g": 255,"b":255}, "state": "ON","cold_white_value":255}


    
  */
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);
  Serial.flush();
  if (!processJson(message)) {
    return;
  }
  Serial.println("JSON ok");
  Serial.flush();
 
  if (stateOn) {
    // Update lights
    realRed = map(red, 0, 255, 0, brightness);
    realGreen = map(green, 0, 255, 0, brightness);
    realBlue = map(blue, 0, 255, 0, brightness);
    realWhite = map(white, 0, 255, 0, brightness);
    realColdWhite = map(coldWhite, 0, 255, 0, brightness);
  }
  else {
    realRed = 0;
    realGreen = 0;
    realBlue = 0;
    realWhite = 0;
    realColdWhite = 0;
  }

  if (!movie){ // if the command requests a movie, we must not set startFade to True
    startFade = true;
  }
  inFade = false; // Kill the current fade

  sendState();
}

bool processJson(char* message) {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.parseObject(message);

  if (!root.success()) {
    Serial.println("parseObject() failed");
    return false;
  }

  if (root.containsKey("state")) {
    if (strcmp(root["state"], CONFIG_MQTT_PAYLOAD_ON) == 0) {
      stateOn = true;
    }
    else if (strcmp(root["state"], CONFIG_MQTT_PAYLOAD_OFF) == 0) {
      stateOn = false;
    }
  }

  // If "flash" is included, treat RGB and brightness differently
  if (root.containsKey("flash") ||
       (root.containsKey("effect") && strcmp(root["effect"], "flash") == 0)) {

    if (root.containsKey("flash")) {
      flashLength = (int)root["flash"] * 1000;
    }
    else {
      flashLength = CONFIG_DEFAULT_FLASH_LENGTH * 1000;
    }

    if (root.containsKey("brightness")) {
      flashBrightness = root["brightness"];
    }
    else {
      flashBrightness = brightness;
    }

    if (rgb && root.containsKey("color")) {
      flashRed = root["color"]["r"];
      flashGreen = root["color"]["g"];
      flashBlue = root["color"]["b"];
    }
    else {
      flashRed = red;
      flashGreen = green;
      flashBlue = blue;
    }

    if (includeWhite && root.containsKey("white_value")) {
      flashWhite = root["white_value"];
    }
    else {
      flashWhite = white;
    }

    if (includeColdWhite && root.containsKey("cold_white_value")) {
      flashColdWhite = root["cold_white_value"];
    }
    else {
      flashColdWhite = white;
    }

    flashRed = map(flashRed, 0, 255, 0, flashBrightness);
    flashGreen = map(flashGreen, 0, 255, 0, flashBrightness);
    flashBlue = map(flashBlue, 0, 255, 0, flashBrightness);
    flashWhite = map(flashWhite, 0, 255, 0, flashBrightness);
   flashColdWhite = map(flashColdWhite, 0, 255, 0, flashBrightness);

    flash = true;
    startFlash = true;
  }
  else if (rgb && root.containsKey("effect")) {
      if  (strcmp(root["effect"], "colorfade_slow") == 0 || strcmp(root["effect"], "colorfade_fast") == 0) {
      flash = false;
      colorfade = true;
      currentColor = 0;
      if (strcmp(root["effect"], "colorfade_slow") == 0) {
        transitionTime = CONFIG_COLORFADE_TIME_SLOW;
      }
      else {
        transitionTime = CONFIG_COLORFADE_TIME_FAST;
      }
    }


    for(int movieIndex = 0; movieIndex < movieArraySize ; movieIndex++){
      Serial.print("Found Movie Name...");
      Serial.println(movieArray[movieIndex].movieName);
    
      //if  (strcmp(root["effect"], "sunrise") == 0 ) {
       if  (strcmp(root["effect"], movieArray[movieIndex].movieName) == 0 ) {
        flash = false;
        colorfade = false;
        currentColor = 0;
        actualMovie = &movieArray[movieIndex];
        movie=movieArray[movieIndex].moviePixels;
        transitionTime = movieArray[movieIndex].transitionTime ; // in millisecs;
        if (root.containsKey("transition")) {
          transitionTime = root["transition"];
          transitionTime = transitionTime * 1000; // in millisecs
        }
        movieLoop = movieArray[movieIndex].loop ;
        if (root.containsKey("loop")) {
          movieLoop = strcmp(root["loop"], "true") == 0;
        }
      }
    }
  }
  else if (colorfade && !root.containsKey("color") && root.containsKey("brightness")) {
    // Adjust brightness during colorfade
    // (will be applied when fading to the next color)
    brightness = root["brightness"];
  }
  else { // No effect
    flash = false;
    colorfade = false;

    if (rgb && root.containsKey("color")) {
      red = root["color"]["r"];
      green = root["color"]["g"];
      blue = root["color"]["b"];
    }

    if (includeWhite && root.containsKey("white_value")) {
      white = root["white_value"];
    }

    if (includeColdWhite && root.containsKey("cold_white_value")) {
      coldWhite = root["cold_white_value"];
    }

    if (root.containsKey("brightness")) {
      brightness = root["brightness"];
    }

    if (root.containsKey("transition")) {
      transitionTime = root["transition"];
    }
    else {
      transitionTime = 0;
    }
  }

  return true;
}

void sendState() {
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;

  JsonObject& root = jsonBuffer.createObject();

  root["state"] = (stateOn) ? CONFIG_MQTT_PAYLOAD_ON : CONFIG_MQTT_PAYLOAD_OFF;
  if (rgb) {
    JsonObject& color = root.createNestedObject("color");
    color["r"] = red;
    color["g"] = green;
    color["b"] = blue;
  }

  root["brightness"] = brightness;

  if (includeWhite) {
    root["white_value"] = white;
  }

  if (includeColdWhite) {
    root["cold_white_value"] = coldWhite;
  }

  if (rgb && colorfade) {
    if (transitionTime == CONFIG_COLORFADE_TIME_SLOW) {
      root["effect"] = "colorfade_slow";
    }
    else {
      root["effect"] = "colorfade_fast";
    }
  }
  else {
    root["effect"] = "null";
  }

  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  client.publish(CONFIG_MQTT_TOPIC_STATE, buffer, true);
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(CONFIG_MQTT_CLIENT_ID, CONFIG_MQTT_USER, CONFIG_MQTT_PASS)) {
      Serial.println("connected");
      client.subscribe(CONFIG_MQTT_TOPIC_SET);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setColor(int inR, int inG, int inB, int inW, int inCW) {
/* digitalWrite(CONFIG_PIN_RED, HIGH);
  digitalWrite(CONFIG_PIN_GREEN, HIGH);
  digitalWrite(CONFIG_PIN_BLUE, HIGH);
  digitalWrite(CONFIG_PIN_WARM_WHITE, HIGH);
  digitalWrite(CONFIG_PIN_COLD_WHITE, HIGH);

return;
*/

  if (CONFIG_INVERT_LED_LOGIC) {
    inR = (255 - inR);
    inG = (255 - inG);
    inB = (255 - inB);
    inW = (255 - inW);
    inCW = (255 - inCW);
  }

  if (rgb) {
    analogWrite(CONFIG_PIN_RED, inR);
    analogWrite(CONFIG_PIN_GREEN, inG);
    analogWrite(CONFIG_PIN_BLUE, inB);
  }

  if (includeWhite) {
    analogWrite(CONFIG_PIN_WARM_WHITE, inW);
  }

  if (includeColdWhite) {
    analogWrite(CONFIG_PIN_COLD_WHITE, inCW);
  }

  if (CONFIG_DEBUG) {
    Serial.print("Setting LEDs: {");
    if (rgb) {
      Serial.print("r: ");
      Serial.print(inR);
      Serial.print(" , g: ");
      Serial.print(inG);
      Serial.print(" , b: ");
      Serial.print(inB);
    }

    if (includeWhite) {
      if (rgb) {
        Serial.print(", ");
      }
      Serial.print("w: ");
      Serial.print(inW);
    }

    if (includeColdWhite) {
      if (rgb) {
        Serial.print(", ");
      }
      Serial.print("cw: ");
      Serial.print(inCW);
    }

    Serial.println("}");
    Serial.flush();
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }

  client.loop();
  if (colorfade){
        Serial.println("colorfade is on! ?");
  }
  if (inFade){
        Serial.println("inFade is on! ?");
  }

  if (flash) {
    if (startFlash) {
      startFlash = false;
      flashStartTime = millis();
    }

    if ((millis() - flashStartTime) <= flashLength) {
      if ((millis() - flashStartTime) % 1000 <= 500) {
        setColor(flashRed, flashGreen, flashBlue, flashWhite, flashColdWhite);
      }
      else {
        setColor(0, 0, 0, 0, 0);
        // If you'd prefer the flashing to happen "on top of"
        // the current color, uncomment the next line.
        // setColor(realRed, realGreen, realBlue, realWhite, realColdWhite);
      }
    }
    else {
      flash = false;
      setColor(realRed, realGreen, realBlue, realWhite, realColdWhite);
    }
  }
  else if (rgb && colorfade && !inFade) {
    realRed = map(colors[currentColor][0], 0, 255, 0, brightness);
    realGreen = map(colors[currentColor][1], 0, 255, 0, brightness);
    realBlue = map(colors[currentColor][2], 0, 255, 0, brightness);
    realWhite = map(colors[currentColor][3], 0, 255, 0, brightness);
    realColdWhite = map(colors[currentColor][3], 0, 255, 0, brightness);
    currentColor = (currentColor + 1) % numColors;
    startFade = true;
  }

  if (startFade) {
    // If we don't want to fade, skip it.
    if (transitionTime == 0) {
      setColor(realRed, realGreen, realBlue, realWhite, realColdWhite);

      redVal = realRed;
      grnVal = realGreen;
      bluVal = realBlue;
      whtVal = realWhite;
      cwhtVal = realColdWhite;

      startFade = false;
    }
    else {
      loopCount = 0;
      stepR = calculateStep(redVal, realRed);
      stepG = calculateStep(grnVal, realGreen);
      stepB = calculateStep(bluVal, realBlue);
      stepW = calculateStep(whtVal, realWhite);
      stepCW = calculateStep(cwhtVal, realColdWhite);

      inFade = true;
    }
  }

  if (inFade) {
    startFade = false;
    unsigned long now = millis();
    if (now - lastLoop > transitionTime) {
      if (loopCount <= 1020) {
        lastLoop = now;

        redVal = calculateVal(stepR, redVal, loopCount);
        grnVal = calculateVal(stepG, grnVal, loopCount);
        bluVal = calculateVal(stepB, bluVal, loopCount);
        whtVal = calculateVal(stepW, whtVal, loopCount);
        cwhtVal = calculateVal(stepCW, cwhtVal, loopCount);

        setColor(redVal, grnVal, bluVal, whtVal, cwhtVal); // Write current values to LED pins

        Serial.print("Loop count: ");
        Serial.println(loopCount);
        loopCount++;
      }
      else {
        inFade = false;
      }
    }
  }
  if (movie){
    unsigned long now = millis();
    if (startMovieTime==0){
      startMovieTime = now;
      Serial.print("start movie at time: ");
      Serial.println(startMovieTime);
    }
    // do a frame
    unsigned long actualFrame=map(now-startMovieTime,0,transitionTime,0,actualMovie->size);
    if (actualFrame>=actualMovie->size-2){ //exceed end of table?
      if (movieLoop){ // restart the movie
        movie=actualMovie->moviePixels;
        startMovieTime=now;
      }else{
        movie=0;
        transitionTime=0;
        startMovieTime=0;
        Serial.print("movie ends now at time: ");
        Serial.println(now);
     }
     setColor(realRed, realGreen, realBlue, realWhite, realColdWhite);
   }else{
      if (lastFrame!=actualFrame){
        frameStartTime=now;
      }
      lastFrame=actualFrame;
      if (CONFIG_DEBUG) {
        Serial.print("actualFrame: ");
        Serial.println(actualFrame);
      }
      if (actualMovie->hardChange){
        realRed = movie[actualFrame].RGB[2];
        realGreen = movie[actualFrame].RGB[1];
        realBlue = movie[actualFrame].RGB[0];
      }else{
        realRed = map(now,frameStartTime,frameStartTime+(transitionTime/(actualMovie->size-1)), movie[actualFrame].RGB[2],movie[actualFrame+1].RGB[2]);
        realGreen = map(now,frameStartTime,frameStartTime+(transitionTime/(actualMovie->size-1)), movie[actualFrame].RGB[1],movie[actualFrame+1].RGB[1]);
        realBlue = map(now,frameStartTime,frameStartTime+(transitionTime/(actualMovie->size-1)), movie[actualFrame].RGB[0],movie[actualFrame+1].RGB[0]);
      }
      realWhite = realRed ;
      realWhite = realWhite > realGreen ? realGreen : realWhite  ;
      realWhite = realWhite > realBlue ? realBlue : realWhite  ;
      unsigned long longWhite= realWhite;
      //realWhite = (longWhite * longWhite* longWhite)/65536;
      realWhite = (longWhite * longWhite)/256;
      realColdWhite = realWhite;
      setColor(realRed, realGreen, realBlue, realWhite, realColdWhite);

      delay(30); // sleep 30ms makes around 30 frames/sec.
    }
  }
}

// From https://www.arduino.cc/en/Tutorial/ColorCrossfader
/* BELOW THIS LINE IS THE MATH -- YOU SHOULDN'T NEED TO CHANGE THIS FOR THE BASICS
*
* The program works like this:
* Imagine a crossfade that moves the red LED from 0-10,
*   the green from 0-5, and the blue from 10 to 7, in
*   ten steps.
*   We'd want to count the 10 steps and increase or
*   decrease color values in evenly stepped increments.
*   Imagine a + indicates raising a value by 1, and a -
*   equals lowering it. Our 10 step fade would look like:
*
*   1 2 3 4 5 6 7 8 9 10
* R + + + + + + + + + +
* G   +   +   +   +   +
* B     -     -     -
*
* The red rises from 0 to 10 in ten steps, the green from
* 0-5 in 5 steps, and the blue falls from 10 to 7 in three steps.
*
* In the real program, the color percentages are converted to
* 0-255 values, and there are 1020 steps (255*4).
*
* To figure out how big a step there should be between one up- or
* down-tick of one of the LED values, we call calculateStep(),
* which calculates the absolute gap between the start and end values,
* and then divides that gap by 1020 to determine the size of the step
* between adjustments in the value.
*/
int calculateStep(int prevValue, int endValue) {
    int step = endValue - prevValue; // What's the overall gap?
    if (step) {                      // If its non-zero,
        step = 1020/step;            //   divide by 1020
    }

    return step;
}

/* The next function is calculateVal. When the loop value, i,
*  reaches the step size appropriate for one of the
*  colors, it increases or decreases the value of that color by 1.
*  (R, G, and B are each calculated separately.)
*/
int calculateVal(int step, int val, int i) {
    if ((step) && i % step == 0) { // If step is non-zero and its time to change a value,
        if (step > 0) {              //   increment the value if step is positive...
            val += 1;
        }
        else if (step < 0) {         //   ...or decrement it if step is negative
            val -= 1;
        }
    }

    // Defensive driving: make sure val stays in the range 0-255
    if (val > 255) {
        val = 255;
    }
    else if (val < 0) {
        val = 0;
    }

    return val;
}
