#define FASTLED_INTERNAL
#include <FastLED.h>
#include <ArduinoJson.h>
#include "EspMQTTClient.h"
#include "./config.h"
#define LED_PIN     4 
#define NUM_LEDS    12*12
#define startLedDistance 0.65/20
#define interLedDistance 1.7/20
#define messageLength 3000
# define BUTTON1_PIN 15


typedef struct {
  float x;
  float y;
  float z;
} point;



int button1State = 0;
uint8_t heartHue=0;

EspMQTTClient client(
  USER_SSID,
  USER_WIFI_PASSWORD,
  MQTT_BROKER,  // MQTT Broker server ip
  MQTT_USERNAME,   // Can be omitted if not needed
  MQTT_PASSWORD,   // Can be omitted if not needed
  MQTT_CLIENT_NAME,     // Client name that uniquely identify your device
  1883              // The MQTT port, default to 1883. this line can be omitted
);

String state="OFF";
int brightness=255;


//defines that somehow dont work with float division
float distanceMax = sqrtl(3.0);
float fps = 1.0/60.0*1000;
float speed =0.0001;

point ledPositions[NUM_LEDS] ;
CRGB leds[NUM_LEDS];
int averageTimeouts=0;

unsigned long lastTick;
unsigned long messageStartMillis;

//point reference{0.5,0.5,0.5};
//point moveDirection{0.1,0.24,0.3};


//up down
point reference{0.5,0,0.5};
point moveDirection{0,0.1,0};

//opposite corners
//point reference{0.5,0.5,0.5};
//point moveDirection{1,-1,1};



DEFINE_GRADIENT_PALETTE( heatmap_gp ) {
0,     193,  33,  240,   //center pruple
50,   255,  0,  232,   //glow
255,   0, 255,  248,}; //cyan

DEFINE_GRADIENT_PALETTE( rainbow_gp ) {
    0, 126,  1,142,
   25, 171,  1, 26,
   48, 224,  9,  1,
   71, 237,138,  1,
   94,  52,173,  1,
  117,   1,201,  1,
  140,   1,211, 54,
  163,   1,124,168,
  186,   1,  8,149,
  209,  12,  1,151,
  232,  12,  1,151,
  255, 171,  1,190};


// Gradient palette "titannightfall_gp", originally from
// http://soliton.vm.bytemark.co.uk/pub/cpt-city/rc/tn/titannightfall.png.index.html
// converted for FastLED with gammas (2.6, 2.2, 2.5)
// Size: 28 bytes of program space.

DEFINE_GRADIENT_PALETTE( titannightfall_gp ) {
    0, 173, 72,109,
   28, 112, 33, 68,
   51,  72, 19, 67,
  127,   2,  1, 33,
  204,  72, 19, 67,
  229, 112, 33, 68,
  255, 173, 72,109};

// Gradient palette "tubepreview_gp", originally from
// http://soliton.vm.bytemark.co.uk/pub/cpt-city/rc/tn/tubepreview.png.index.html
// converted for FastLED with gammas (2.6, 2.2, 2.5)
// Size: 32 bytes of program space.

DEFINE_GRADIENT_PALETTE( tubepreview_gp ) {
    0, 113, 99, 23,
   51, 255,255,255,
   76, 255,255,255,
  114,   0,  0,  0,
  140,   0,  0,  0,
  178, 255,255,255,
  204, 255,255,255,
  255, 113, 99, 23};





CRGBPalette16 myPal = rainbow_gp ;

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  calculatePositions();
  
  client.enableDebuggingMessages(); // Enable debugging messages sent to serial output
  client.enableHTTPWebUpdater(); // Enable the web updater. User and password default to values of MQTTUsername and MQTTPassword. These can be overrited with enableHTTPWebUpdater("user", "password").
  client.enableLastWillMessage(CHANNEL_AVAILABLITY, "offline");  // You can activate the retain flag by setting the third parameter to true
  
  normalize(moveDirection);
  lastTick=millis();
  
}

void onConnectionEstablished()
{
  client.subscribe(CHANNEL_MQTT_LIGHT_COMMANDS, &handleMessage);
  client.subscribe(CHANNEL_HEARTBEAT_RX, &handleHeart);
  client.publish(CHANNEL_AVAILABLITY, "online");
}

void loop() {
  int timeouts=0;
  while(millis()-lastTick<fps){timeouts++;}
  if(timeouts==0)
    Serial.printf("%fCant keep up, deltaT was %f\n",fps,millis()-lastTick);
  else if(((float)(abs(timeouts-averageTimeouts)))/timeouts>0.1){
    averageTimeouts=timeouts;
    Serial.printf("Next Frame Timeouts is deviating to %d\n",averageTimeouts);
  }
  lastTick=millis();
  if(state=="heart"){
    if(millis()-messageStartMillis>messageLength){
      state="OFF";  
      FastLED.setBrightness(0);
    }  
    float percentDone= ((float)(millis()-messageStartMillis))/((float)(messageLength));
    percentDone*=2;
    if(percentDone>1){
      percentDone=-percentDone+2;
    }
    uint8_t unscaled =(uint8_t)(percentDone*255);
    uint8_t scaled = scale8( unscaled, 200);
    CHSV color= CHSV(heartHue,200,scaled);
    FastLED.clear();
    for(int i=0;i<NUM_LEDS;i++){
      leds[i]=color;
    }
    FastLED.show();
    
  }
  else{
    movePoint(speed*fps);
    FastLED.clear();
    for(int i=0;i<NUM_LEDS;i++){
      calculateColor(i);
    }
    FastLED.show();
  }
  
  /*
  
  while(millis()-lastTick<fps){}
  lastTick=millis();
  for(int i=0;i<NUM_LEDS;i++){
    point current =ledPositions[i];
    point origin={0.5,0.5,0.5};
    float distance =sqrt(pow(origin.x-current.x,2)+pow(origin.y-current.y,2)+pow(origin.z-current.z,2));
    
    leds[i] = CRGB(0, 0, 0);
  }
  
  FastLED.show();
  delay(20);
  */
  client.loop();
}

void handleHeart(const String & payload){
  if(state=="OFF"){
    state="heart";  
    messageStartMillis=millis();
    FastLED.setBrightness(255);
    
  }
}

void handleMessage(const String & payload){
  Serial.println("incoming: " + payload);
  DynamicJsonDocument json(1024);
  deserializeJson(json, payload);
  String deviceOn = json["state"];
  int rec_brightness = json["brightness"];
  long rec_spectrumRgb = json["spectrumRgb"];
  long my_spectrumRgb;

  if (deviceOn == "ON")
  {
    Serial.println("ON");
    state="ON";
    FastLED.setBrightness(brightness);
  }
  
  if (deviceOn == "OFF")
  {
    Serial.println("OFF");
    state="OFF";
    FastLED.setBrightness(0);
  }

  if (!json["brightness"].isNull())
  {
    FastLED.setBrightness(rec_brightness);
    brightness=rec_brightness;
    Serial.println("brightness");
    Serial.println(rec_brightness);
  }
  if (!json["color"].isNull())
  {
    changeColorPalette(json["color"]["r"],json["color"]["g"],json["color"]["b"]);
  }
  publishStateChange();
}


void publishStateChange(){
    String json = "{\"state\":\""+state+"\",\"brightness\":"+brightness+"}";
    client.publish(CHANNEL_MQTT_LIGHT_STATUS, json);
}

void calculateColor(int number){
  point x= ledPositions[number];
  double distance = sqrt((x.x-reference.x)*(x.x-reference.x)+(x.y-reference.y)*(x.y-reference.y)+(x.z-reference.z)*(x.z-reference.z));
  uint8_t normalised = distance/distanceMax*255;
  CHSV hsv(normalised,255,255-normalised);
  CRGB rgb;
  hsv2rgb_rainbow( hsv, rgb);
  uint8_t heatindex = sin8((millis()%5100)/20-normalised);
  leds[number] = ColorFromPalette( myPal, heatindex); 
  //leds[number]=rgb;
}

void changeColorPalette(uint8_t r,uint8_t g,uint8_t b) {
  CRGB color{r,g,b};
  CRGB color2 = color*0.8;
  CRGB color3 = color*0.5;
  Serial.printf("R:%d G:%d B:%d\n",color.r,color.g,color.b);
  byte colorArr[]{
    0, color.r, color.g, color.b,
   64, color2.r, color2.g, color2.b,
  128, color3.r, color3.g, color3.b,
  192, color2.r, color2.g, color2.b,
  255, color.r, color.g, color.b
  };
  myPal.loadDynamicGradientPalette( colorArr);
}

void movePoint(float factor){
  reference.x+=moveDirection.x*factor;
  if(reference.x<0){
    moveDirection.x*=-1;
    reference.x*=-1;
  }
  if(reference.x>1){
    moveDirection.x*=-1;
    reference.x=2-reference.x;
  }
  reference.y+=moveDirection.y*factor;
  if(reference.y<0){
    moveDirection.y*=-1;
    reference.y*=-1;
  }
  if(reference.y>1){
    moveDirection.y*=-1;
    reference.y=2-reference.y;
  }
  reference.z+=moveDirection.z*factor;
  if(reference.z<0){
    moveDirection.z*=-1;
    reference.z*=-1;
  }
  if(reference.z>1){
    moveDirection.z*=-1;
    reference.z=2-reference.z;
  }
}

void normalize(point& x){
  if(x.x==0&&x.y==0&&x.z==0)
    return;
  float length = sqrt(sq(x.x)+sq(x.y)+sq(x.z));
  x.x/=length;
  x.y/=length;
  x.z/=length;
}
void calculatePositions(){
  point rowDirections[12]={
    { 0, 0, 1 },
    { 0, 1, 0 },
    { 1, 0, 0 },
    { 0, 1, 0 },
    { 0, 0, -1},
    { 0, 1, 0 },
    { -1, 0, 0 }, 
    { 0, 1, 0 },
    { 1, 0, 0 },
    { 0, 0, 1 },
    { -1, 0, 0 },
    { 0, 0, -1 }
  };
  point rowBeginPositions[12]={
    { 0, 0, 0 },
    { 0, 0, 1 },
    { 0, 0, 1 },
    { 1, 0, 1 },
    { 1, 0, 1 },
    { 1, 0, 0 },
    { 1, 0, 0 },
    { 0, 0, 0 },
    { 0, 1, 0 },
    { 1, 1, 0 },
    { 1, 1, 1 },
    { 0, 1, 1 }
  };
  for(int i=0;i<12;i++){
    for(int j=0;j<NUM_LEDS/12;j++){
      ledPositions[i*12+j].x=rowBeginPositions[i].x+rowDirections[i].x*(startLedDistance+j*interLedDistance);
      ledPositions[i*12+j].y=rowBeginPositions[i].y+rowDirections[i].y*(startLedDistance+j*interLedDistance);
      ledPositions[i*12+j].z=rowBeginPositions[i].z+rowDirections[i].z*(startLedDistance+j*interLedDistance);
    }
  }
}

void checkbuttons(){
  if(digitalRead(BUTTON1_PIN)==HIGH){
    if(button1State==0){
      button1State=1;  
      client.publish(CHANNEL_HEARTBEAT_TX, CHANNEL_HEARTBEAT_MESSAGE);
    }
  }
}
