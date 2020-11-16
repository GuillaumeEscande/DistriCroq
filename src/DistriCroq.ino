
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AccelStepper.h>
#include <NTPClient.h>
#include <LittleFS.h>
#include <EEPROM.h>
#include <Thread.h>
#include <ThreadController.h>
#include <time.h>
#include <sys/time.h>
#include <CronAlarms.h>

#define STASSID "GuiEtJew"
#define STAPSK  "lithium est notre chat."


#define ENABLE_PIN 0
#define STEP_PIN 1
#define DIR_PIN 2

#define INIT_VALUE 56
#define DEFAULT_CRON "0 0 2/7-23 ? * * *"
#define DEFAULT_STEP_BACK_BEFORE 150
#define DEFAULT_STEP_FORWARD -800
#define DEFAULT_STEP_BACK_AFTER 10

#define ADDR_INIT 0
#define ADDR_CRON ADDR_INIT + sizeof(int)
#define ADDR_STEP_BACK_BEFORE ADDR_CRON + 128
#define ADDR_STEP_FORWARD ADDR_STEP_BACK_BEFORE + sizeof(int)
#define ADDR_STEP_BACK_AFTER ADDR_STEP_FORWARD + sizeof(int)

const char *ssid = STASSID;
const char *password = STAPSK;


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
AsyncWebServer server(80);
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
ThreadController controller = ThreadController();
CronId id;



String readStringFromEEPROM(int addrOffset)
{
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++)
  {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = 0;
  return String(data);
}

void writeStringToEEPROM(int addrOffset, const String &strToWrite)
{
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++)
  {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
}

void wait_motor_end(){
  while( stepper.run() == true );
}

void distribute(){
  
  int setBackBefore = 0;
  int setForward = 0;
  int setBackAfter = 0;

  EEPROM.get(ADDR_STEP_BACK_BEFORE, setBackBefore);
  EEPROM.get(ADDR_STEP_FORWARD, setForward);
  EEPROM.get(ADDR_STEP_BACK_AFTER, setBackAfter);  
  
  digitalWrite(ENABLE_PIN, LOW);
  stepper.enableOutputs();
  
  stepper.move(setBackBefore);
  wait_motor_end();
  stepper.move(setForward);
  wait_motor_end();
  stepper.move(setBackAfter);
  wait_motor_end();
  
  digitalWrite(ENABLE_PIN, HIGH);
  stepper.disableOutputs();
}

String processor(const String& var){
  Serial.println(var);
  if(var == "TIME"){
    return timeClient.getFormattedTime();
  } else if(var == "CRON"){
    return readStringFromEEPROM(ADDR_CRON);
  } else if(var == "BEFORE"){
    int setBackBefore = 0;
    EEPROM.get(ADDR_STEP_BACK_BEFORE, setBackBefore);
    return String(setBackBefore);
  } else if(var == "FORWARD"){
    int setForward = 0;
    EEPROM.get(ADDR_STEP_FORWARD, setForward);
    return String(setForward);
  } else  if(var == "AFTER"){
    int setBackAfter = 0;
    EEPROM.get(ADDR_STEP_BACK_AFTER, setBackAfter); 
    return String(setBackAfter);
  }
  return "Unknown";
}

void updateVars(const AsyncWebServerRequest *request){

  if (request->hasParam("cron")) {
    updateCron(request->getParam("cron")->value());
  }
  if (request->hasParam("before")) {
    EEPROM.put(ADDR_STEP_BACK_BEFORE, (int)request->getParam("before")->value().toInt());
  }
  if (request->hasParam("forward")) {
    EEPROM.put(ADDR_STEP_FORWARD, (int)request->getParam("forward")->value().toInt());
  }
  if (request->hasParam("after")) {
    EEPROM.put(ADDR_STEP_BACK_AFTER, (int)request->getParam("after")->value().toInt());
  }
    
  EEPROM.commit();

}

void updateCron(String cronexpr){
  writeStringToEEPROM(ADDR_CRON, cronexpr);
  Cron.free(id);
  id = Cron.create(cronexpr.c_str(), distribute, false);
}


Thread ntpThread = Thread();
Thread motorThread = Thread();


void setup(void) {
  // Init EEPREOM
  EEPROM.begin(ADDR_STEP_BACK_AFTER + sizeof(int));

  // Init EEPROM vars :
  if( EEPROM.read(ADDR_INIT) != INIT_VALUE){
    EEPROM.write(ADDR_INIT, INIT_VALUE);
    EEPROM.put(ADDR_STEP_BACK_BEFORE, (int)DEFAULT_STEP_BACK_BEFORE);
    EEPROM.put(ADDR_STEP_FORWARD, (int)DEFAULT_STEP_FORWARD);
    EEPROM.put(ADDR_STEP_BACK_AFTER, (int)DEFAULT_STEP_BACK_AFTER);
    updateCron(DEFAULT_CRON);
    EEPROM.commit();
  }
  


  // Init stepper motor
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  stepper.setMaxSpeed(3000);
  stepper.setAcceleration(1000);
  
  digitalWrite(ENABLE_PIN, HIGH);
  stepper.disableOutputs();

  // Init Wifi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Init FS
  LittleFS.begin();

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    updateVars(request);
    request->send(LittleFS, "/index.html", String(), false, processor);
  });
  
  // Route to load style.css file
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/favicon.ico", "image/x-icon");
  });
  
  // Route to load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/main.css", "text/css");
  });
  
  // Route to set GPIO to HIGH
  server.on("/distribute", HTTP_GET, [](AsyncWebServerRequest *request){
    distribute();
    request->send(200, "text/plain", "distribute done");
  });


  server.onNotFound( [](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "Not found");
  });

  // Start server
  server.begin();

  // Start NTP
  timeClient.begin();
  timeClient.update();


  ntpThread.setInterval(60000);
  ntpThread.onRun( []() {
    timeClient.update();
  });
  
  motorThread.onRun( []() {
    stepper.run();
  });
  
  
  controller.add(&ntpThread); 
  controller.add(&motorThread); 
  
}

void loop(void) {
  
  controller.run();
}
