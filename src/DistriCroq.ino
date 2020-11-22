
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AccelStepper.h>
#include <LittleFS.h>
#include <EEPROM.h>
#include <Thread.h>
#include <ThreadController.h>
#include <time.h>
#include <sys/time.h>
#include <CronAlarms.h>
#include <sntp.h>

#define STASSID "GuiEtJew"
#define STAPSK  "lithium est notre chat."

#define RESET_PIN 2
#define SLEEP_PIN 4
#define MS1_PIN 5
#define ENABLE_PIN 16
#define STEP_PIN 12
#define DIR_PIN 14

#define INIT_VALUE 56
#define DEFAULT_CRON "0 0 2/7-23 * * *"
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

bool distribute = false;

WiFiUDP ntpUDP;
AsyncWebServer server(80);
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);
ThreadController controller = ThreadController();
CronId id = dtINVALID_ALARM_ID;

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
  while( stepper.run() == true )
    delay(50);
}

void do_distribute(){
  Serial.println("RUN - Start distribute");
  
  int setBackBefore = 0;
  int setForward = 0;
  int setBackAfter = 0;

  EEPROM.get(ADDR_STEP_BACK_BEFORE, setBackBefore);
  EEPROM.get(ADDR_STEP_FORWARD, setForward);
  EEPROM.get(ADDR_STEP_BACK_AFTER, setBackAfter);  

  Serial.println("RUN - Enable motors");
  digitalWrite(ENABLE_PIN, LOW);
  digitalWrite(SLEEP_PIN, HIGH);
  stepper.enableOutputs();

  Serial.print("RUN - go back before : ");
  Serial.println(setBackBefore);
  stepper.move(setBackBefore);
  stepper.runToPosition();

  Serial.print("RUN - go set forward : ");
  Serial.println(setForward);
  stepper.move(setForward);
  stepper.runToPosition();

  Serial.print("RUN - go back after : ");
  Serial.println(setBackAfter);
  stepper.move(setBackAfter);
  stepper.runToPosition();
  
  Serial.println("RUN - Disable motors");
  stepper.disableOutputs();
  digitalWrite(SLEEP_PIN, LOW);
  digitalWrite(ENABLE_PIN, HIGH);
  Serial.println("RUN - End distribute");
}

String processor(const String& var){
  if(var == "TIME"){
    time_t now = time(nullptr);
    return ctime(&now);
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

void updateCron(){
  Serial.println("RUN - updateCron");
  Cron.free(id);
  String cronExpression = readStringFromEEPROM(ADDR_CRON);
  Serial.println(cronExpression);
  id = Cron.create(cronExpression.c_str(), [](){
    distribute = true;
  }, false);
  Serial.println("RUN - updateCron - OK");
}

void updateVars(const AsyncWebServerRequest *request){
  Serial.println("RUN - updateVars");
  if (request->hasParam("cron")){
    writeStringToEEPROM(ADDR_CRON, request->getParam("cron")->value());
    EEPROM.commit();
    updateCron();
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
  Serial.println("RUN - updateVars - OK");

}


Thread motorThread = Thread();
Thread cronThread = Thread();


void setup(void) {

  Serial.begin(115200);
  Serial.println("INIT - Start");

  // Init EEPREOM
  EEPROM.begin(ADDR_STEP_BACK_AFTER + sizeof(int));

  // Init EEPROM vars :
  if( EEPROM.read(ADDR_INIT) != INIT_VALUE){
    Serial.println("INIT - Reset EEPROM value");
    EEPROM.write(ADDR_INIT, INIT_VALUE);
    EEPROM.put(ADDR_STEP_BACK_BEFORE, (int)DEFAULT_STEP_BACK_BEFORE);
    EEPROM.put(ADDR_STEP_FORWARD, (int)DEFAULT_STEP_FORWARD);
    EEPROM.put(ADDR_STEP_BACK_AFTER, (int)DEFAULT_STEP_BACK_AFTER);
    writeStringToEEPROM(ADDR_CRON, DEFAULT_CRON);
    EEPROM.commit();
  }
  


  // Init stepper motor
  Serial.println("INIT - Init Stepper Motor");
  pinMode(RESET_PIN, OUTPUT);
  pinMode(SLEEP_PIN, OUTPUT);
  pinMode(MS1_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  stepper.setMaxSpeed(300);
  stepper.setAcceleration(100); 
  digitalWrite(RESET_PIN, LOW);
  digitalWrite(MS1_PIN, LOW);
  digitalWrite(ENABLE_PIN, HIGH);
  digitalWrite(SLEEP_PIN, LOW);
  delay(50);  
  digitalWrite(RESET_PIN, HIGH);
  stepper.disableOutputs();
  Serial.println("INIT - Init Stepper Motor - OK");

  // Init Wifi
  Serial.println("INIT - Init Wifi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  Serial.print("INIT - Wifi Connected : ");
  Serial.println(WiFi.localIP());
  
  // Init mDNS
  if (MDNS.begin("districroq")) {
    Serial.println("INIT - mDNS OK");
  }

  // Init FS
  Serial.println("INIT - LittleFS");
  LittleFS.begin();
  

  Serial.println("INIT - Route init");
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    updateVars(request);
    if (request->hasParam("distribute")) {
      distribute = true;
    }
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
  
  server.on("/distribute", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "distribute started");
    distribute = true;
  });
  
  server.on("/date", HTTP_GET, [](AsyncWebServerRequest *request){
    time_t now = time(nullptr);
    request->send(200, "text/plain", ctime(&now));
  });


  server.onNotFound( [](AsyncWebServerRequest *request){
    request->send(404, "text/plain", "Not found");
  });
  Serial.println("INIT - Route init - OK");

  // Start server
  server.begin();
  Serial.println("INIT - HTTP Server started");

  // Start NTP
  Serial.println("INIT - NTP start");
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  ip4_addr ipaddr;
  IP4_ADDR(&ipaddr, 176,31,251,158);
	sntp_setserver(0, &ipaddr);
	sntp_init();
  Serial.println("INIT - NTP start - OK");

  cronThread.setInterval(100);
  motorThread.onRun( []() {
    //stepper.run();
    if(distribute){
      distribute = false;
      do_distribute();
    }
      
  });

  cronThread.setInterval(100);
  cronThread.onRun( []() {
    Cron.delay();
  });
  
  
  controller.add(&motorThread); 
  controller.add(&cronThread); 

  updateCron();
  
}

void loop(void) {
  controller.run();
}
