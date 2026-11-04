#include <WiFi.h>
#include <Wire.h>
#include <RTClib.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <WebServer.h>
#include <Keypad.h>

// ---------------- WiFi credentials ----------------
const char* sta_ssid = "Adithyan's iQOO Neo 10";
const char* sta_password = "justgoanddie";

const char* ap_ssid = "ESP32-SOS";
const char* ap_password = "12345678";

// ---------------- GSM ----------------
#define RXD2 16
#define TXD2 17
#define SOS_BUTTON 4
#define BUZZER 27
#define PIR_PIN 23
#define TRIG_PIN 5
#define ECHO_PIN 18

// ---------------- EEPROM ----------------
#define EEPROM_BASE 0x50

// ---------------- Timezone ----------------
#define GMT_OFFSET_SEC      19800
#define DAYLIGHT_OFFSET_SEC 0

RTC_DS3231 rtc;
hd44780_I2Cexp lcd;
WebServer server(80);

// ---------------- Stored data ----------------
String startTime = "";
String duration  = "";
String contact1  = "";
String contact2  = "";

// ---------------- States ----------------
bool wifiConnected = false;
bool wifiPrevConnected = false;
bool rtcValid = true;

bool showDataMessage = false;
bool showSOSMessage = false;
bool monitoringFinishedToday = false;

long us_duration;
float distance;
int movementCount = 0;
float prevDistance = 0;

// -------- KEYPAD EDIT MODE --------

int editMode = 0;
String inputBuffer = "";
bool editingActive = false;

// -------- KEYPAD --------

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[ROWS] = {32,33,25,26};
byte colPins[COLS] = {14,12,13,15};

Keypad keypad = Keypad(
  makeKeymap(keys),
  rowPins,
  colPins,
  ROWS,
  COLS
);

unsigned long dataMsgStart = 0;
unsigned long lastWifiAttempt = 0;
unsigned long lastTimeUpdate  = 0;
unsigned long lastRTCSync     = 0;

const unsigned long DATA_MSG_DURATION = 3000;
const unsigned long WIFI_RETRY_INTERVAL = 8000;
const unsigned long TIME_REFRESH_MS = 1000;
const unsigned long RTC_SYNC_INTERVAL = 3600000UL;
const unsigned long CALL_DURATION = 20000;


// =================================================
// EEPROM FUNCTIONS
// =================================================

void eepromWriteByte(int addr, byte data)
{
  byte block = addr / 256;
  byte mem = addr % 256;

  Wire.beginTransmission(EEPROM_BASE + block);
  Wire.write(mem);
  Wire.write(data);
  Wire.endTransmission();
  delay(5);
}

byte eepromReadByte(int addr)
{
  byte block = addr / 256;
  byte mem = addr % 256;

  Wire.beginTransmission(EEPROM_BASE + block);
  Wire.write(mem);
  Wire.endTransmission();

  Wire.requestFrom(EEPROM_BASE + block, 1);

  if (Wire.available())
    return Wire.read();

  return 0;
}

void writeString(int addr, String data)
{
  int len = data.length();
  if(len > 40) len = 40;

  eepromWriteByte(addr, len);

  for(int i=0;i<len;i++)
    eepromWriteByte(addr+1+i, data[i]);
}

String readString(int addr)
{
  int len = eepromReadByte(addr);
  if(len > 40) len = 40;

  char data[41];

  for(int i=0;i<len;i++)
    data[i] = eepromReadByte(addr+1+i);

  data[len] = '\0';
  return String(data);
}


// =================================================
// CUSTOM CHARACTERS
// =================================================

byte wifi_signalLeft[8] = {
  B00000,B00000,B00000,B00000,
  B00011,B00011,B11011,B11011
};

byte wifi_signalRight[8] = {
  B00011,B00011,B11011,B11011,
  B11011,B11011,B11011,B11011
};

byte nowifi_signalLeft[8] = {
  B10100,B01000,B10100,B00000,
  B00011,B00011,B11011,B11011
};

byte nowifi_signalRight[8] = {
  B00011,B00011,B11011,B11011,
  B11011,B11011,B11011,B11011
};


// =================================================
// LCD HELPERS
// =================================================

void showRangeMeter() {
  lcd.setCursor(14,0);
  lcd.write(byte(0));
  lcd.write(byte(1));
}

void hideRangeMeter() {
  lcd.setCursor(14,0);
  lcd.print("  ");
}

void blinkRangeMeter(int times){
  Serial.println("LCD: WiFi Lost");
  for(int i=0;i<times;i++){
    hideRangeMeter(); delay(300);
    showRangeMeter(); delay(300);
  }
}

void showMonitoringScreen(){
  lcd.clear();
  lcd.setCursor(0,1);
  lcd.print("System:Active");
  showRangeMeter();
}

void showDataReceivedMessage(){
  lcd.clear();
  lcd.setCursor(1,0);
  lcd.print("Data Received");
  Serial.println("LCD: Data Received");
}

void showSOSScreen(String l1,String l2){
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);

  Serial.println("LCD:");
  Serial.println(l1);
  Serial.println(l2);
}


// =================================================
// EEPROM LOADED SCREEN
// =================================================

void showEEPROMLoaded(){

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("DATA LOADED");

  lcd.setCursor(0,1);
  lcd.print("FROM MEMORY");

  Serial.println("LCD: DATA LOADED FROM EEPROM");

  delay(3000);

  lcd.clear();
}


// =================================================
// STARTUP BANNER
// =================================================

void showStartupBanner(){

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("SMART MONITORING");
  lcd.setCursor(0,1);
  lcd.print("SYSTEM");

  Serial.println("LCD: SMART MONITORING SYSTEM");

  delay(1500);

  const char* l1="FOR SOLITARY";
  const char* l2="& ELDERLY USERS";

  for(int pos=16; pos>=0; pos--){
    lcd.clear();
    lcd.setCursor(pos,0); lcd.print(l1);
    lcd.setCursor(pos,1); lcd.print(l2);
    delay(200);
  }

  Serial.println("LCD: FOR SOLITARY & ELDERLY USERS");

  delay(1500);
  lcd.clear();
}
// =================================================
// GSM FUNCTIONS
// =================================================

bool waitForResponse(String target, unsigned long timeout){
  unsigned long start = millis();
  String response="";

  while(millis()-start < timeout){
    while(Serial2.available()){
      char c = Serial2.read();
      response += c;
      Serial.write(c);

      if(response.indexOf(target)!=-1) return true;
    }
  }
  return false;
}

void gsmWake(){
  Serial2.println("AT");
  waitForResponse("OK",2000);

  Serial2.println("ATE0");
  waitForResponse("OK",2000);

  Serial2.println("ATH");
  delay(1000);
}

bool sendSMS(String num){

  if(num=="") return false;

  Serial.println("Sending SMS to "+num);
  showSOSScreen("Sending SMS",num);

  Serial2.println("AT+CMGF=1");
  if(!waitForResponse("OK",3000)) return false;

  Serial2.println("AT+CMGS=\"" + num + "\"");
  if(!waitForResponse(">",5000)) return false;

  Serial2.print("SOS ALERT\r\n");
  Serial2.write(26);

  bool sent = waitForResponse("OK",10000);
  delay(2000);

  return sent;
}

bool callNumber(String num){

  if(num=="") return false;

  Serial.println("Calling "+num);
  showSOSScreen("Calling...",num);

  Serial2.println("ATD"+num+";");

  unsigned long start=millis();
  String resp="";

  while(millis()-start < CALL_DURATION){

    while(Serial2.available()){

      char c=Serial2.read();
      resp+=c;
      Serial.write(c);

      if(resp.indexOf("CONNECT")!=-1){
        showSOSScreen("Call Answered",num);
        delay(5000);
        Serial2.println("ATH");
        return true;
      }

      if(resp.indexOf("BUSY")!=-1 ||
         resp.indexOf("NO CARRIER")!=-1 ||
         resp.indexOf("NO ANSWER")!=-1){

        Serial2.println("ATH");
        return false;
      }
    }
  }

  Serial2.println("ATH");
  delay(2000);
  return false;
}
// =========================
// BUZZER FUNCTION
// =========================

void beepSOS3()
{
  for(int i=0;i<3;i++)
  {
    digitalWrite(BUZZER, LOW);
    delay(150);
    digitalWrite(BUZZER, HIGH);
    delay(150);
  }
}
float getDistance()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  us_duration = pulseIn(ECHO_PIN, HIGH);

  return us_duration * 0.034 / 2;
}
// =================================================
// SETUP
// =================================================

void setup(){

  Serial.begin(115200);
  Serial2.begin(9600,SERIAL_8N1,RXD2,TXD2);

  Wire.begin(21,22);

  pinMode(SOS_BUTTON, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, HIGH);
  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  lcd.begin(16,2);
  lcd.backlight();

  lcd.createChar(0, nowifi_signalLeft);
  lcd.createChar(1, nowifi_signalRight);

  showStartupBanner();


  // -------- EEPROM LOAD --------

  startTime = readString(0);
  duration  = readString(50);
  contact1  = readString(100);
  contact2  = readString(150);

  Serial.println("======== EEPROM DATA ========");
  Serial.println("StartTime: " + startTime);
  Serial.println("Duration : " + duration);
  Serial.println("Contact1 : " + contact1);
  Serial.println("Contact2 : " + contact2);
  Serial.println("=============================");

  showEEPROMLoaded();


  if(!rtc.begin()){
    rtcValid=false;
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(sta_ssid, sta_password);
  WiFi.softAP(ap_ssid, ap_password);

  server.on("/",handleRoot);
  server.on("/update",handleUpdate);
  server.on("/sos",handleSOS);

  server.begin();

  showMonitoringScreen();
  showRangeMeter();
}

// =================================================
// WEB SERVER
// =================================================

void handleRoot(){
  server.send(200,"text/plain","ESP32 is running");
}

void handleUpdate(){

 if(server.hasArg("TIME")){

    startTime = server.arg("TIME");

    // REMOVE :
    startTime.replace(":", "");

    writeString(0,startTime);
}

  if(server.hasArg("DUR")){
    duration = server.arg("DUR");
    writeString(50,duration);
  }

  if(server.hasArg("C1")){
    contact1 = server.arg("C1");
    writeString(100,contact1);
  }

  if(server.hasArg("C2")){
    contact2 = server.arg("C2");
    writeString(150,contact2);
  }

  Serial.println("---- SETTINGS RECEIVED ----");
  Serial.println("StartTime: " + startTime);
  Serial.println("Duration : " + duration);
  Serial.println("Contact1 : " + contact1);
  Serial.println("Contact2 : " + contact2);

  showDataMessage=true;
  dataMsgStart=millis();
  showDataReceivedMessage();

  server.send(200,"text/plain","Settings updated successfully");
}

void handleSOS(){
  beepSOS3();
  Serial.println("SOS RECEIVED");

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("SOS RECEIVED");

  delay(1000);

  showSOSMessage=true;

  gsmWake();

  // SMS 1
  Serial.println("Sending SMS 1");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Sending SMS 1");
  delay(500);
  sendSMS(contact1);

  // SMS 2
  Serial.println("Sending SMS 2");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Sending SMS 2");
  delay(500);
  sendSMS(contact2);

  // CALL 1
  Serial.println("Calling 1");
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Calling 1");

  bool answered = callNumber(contact1);

  // CALL 2
  if(!answered){
    Serial.println("Calling 2");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Calling 2");

    callNumber(contact2);
  }

  showSOSMessage=false;
  showMonitoringScreen();

  server.send(200,"text/plain","SOS completed");
}

// =================================================
// LOOP
// =================================================

void loop(){

  server.handleClient();

  // -------- KEYPAD CONTROL --------

  char key = keypad.getKey();

  if(key){

    Serial.print("Key Pressed: ");
    Serial.println(key);

    // NORMAL MODE
    if(editMode == 0){

      if(key == 'A'){
        editMode = 1;
        editingActive = true;
        inputBuffer = "";
        Serial.println("Edit Time");
      }

      else if(key == 'B'){
        editMode = 2;
        editingActive = true;
        inputBuffer = "";
        Serial.println("Edit Duration");
      }

      else if(key == 'C'){
        editMode = 3;
        editingActive = true;
        inputBuffer = "";
        Serial.println("Edit Contact1");
      }

      else if(key == 'D'){
        editMode = 4;
        editingActive = true;
        inputBuffer = "";
        Serial.println("Edit Contact2");
      }

    }

    // EDIT MODE
    else{

      if(key >= '0' && key <= '9'){
        inputBuffer += key;
        Serial.print("Buffer: ");
        Serial.println(inputBuffer);
      }

      else if(key == '*'){
        inputBuffer = "";
        Serial.println("Buffer cleared");
      }

      else if(key == '#'){

        Serial.println("Save pressed");

        if(editMode == 1){
          startTime = inputBuffer;
          writeString(0, startTime);
        }

        else if(editMode == 2){
          duration = inputBuffer;
          writeString(50, duration);
        }

        else if(editMode == 3){
          contact1 = inputBuffer;
          writeString(100, contact1);
        }

        else if(editMode == 4){
          contact2 = inputBuffer;
          writeString(150, contact2);
        }

        inputBuffer = "";
        editMode = 0;
        editingActive = false;

        lcd.clear();
        lcd.print("Saved");

        delay(1000);
        showMonitoringScreen();
      }

    }

  }


if(digitalRead(SOS_BUTTON) == LOW){
  Serial.println("Manual SOS Button Pressed");
  handleSOS();
  delay(500);
}

  if(showDataMessage){
    if(millis()-dataMsgStart > DATA_MSG_DURATION){
      showDataMessage=false;
      showMonitoringScreen();
    }
    return;
  }

  wifiConnected = (WiFi.status()==WL_CONNECTED);

  if(wifiConnected && !wifiPrevConnected){

  lcd.createChar(0,wifi_signalLeft);
  lcd.createChar(1,wifi_signalRight);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("WiFi Detected");

  showRangeMeter();

  // ===== SYNC TIME NOW =====
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org");

  struct tm timeinfo;

  if (getLocalTime(&timeinfo))
  {
    DateTime netTime(
      timeinfo.tm_year + 1900,
      timeinfo.tm_mon + 1,
      timeinfo.tm_mday,
      timeinfo.tm_hour,
      timeinfo.tm_min,
      timeinfo.tm_sec
    );

    rtc.adjust(netTime);

    Serial.println("RTC SYNCED AT WIFI CONNECT");
  }

  delay(2000);

  showMonitoringScreen();
}
  if(!wifiConnected && wifiPrevConnected){

    blinkRangeMeter(5);

    lcd.createChar(0,nowifi_signalLeft);
    lcd.createChar(1,nowifi_signalRight);

    showMonitoringScreen();
  }

  wifiPrevConnected = wifiConnected;
// -------- EDIT LCD --------

if(editingActive){

  lcd.clear();

  if(editMode == 1){
    lcd.setCursor(0,0);
    lcd.print("SET TIME");
  }
  else if(editMode == 2){
    lcd.setCursor(0,0);
    lcd.print("SET MIN");
  }
  else if(editMode == 3){
    lcd.setCursor(0,0);
    lcd.print("SET NUM1");
  }
  else if(editMode == 4){
    lcd.setCursor(0,0);
    lcd.print("SET NUM2");
  }

  lcd.setCursor(0,1);
  lcd.print(inputBuffer);

  delay(200);

  return;
}

  // TIME DISPLAY (no serial print)

  if(rtcValid && millis()-lastTimeUpdate > TIME_REFRESH_MS){

    lastTimeUpdate = millis();

    DateTime now = rtc.now();

    lcd.setCursor(0,0);

    if(now.hour()<10) lcd.print("0");
    lcd.print(now.hour()); lcd.print(":");

    if(now.minute()<10) lcd.print("0");
    lcd.print(now.minute()); lcd.print(":");

    if(now.second()<10) lcd.print("0");
    lcd.print(now.second());
  }
// =============================
// AUTO WAKEUP MONITOR SYSTEM
// =============================

static bool monitoringStarted = false;
static bool startTriggeredToday = false;
static unsigned long monitorStartMillis = 0;

if(startTime != "" && duration != "")
{
  DateTime now = rtc.now();

  int nowMin = now.hour() * 60 + now.minute();

  int setHour = startTime.substring(0,2).toInt();
  int setMin  = startTime.substring(2,4).toInt();

  int startMin = setHour * 60 + setMin;

  int durMin = duration.toInt();


  // START MONITOR

if(nowMin == startMin && !startTriggeredToday)
 if(nowMin >= startMin && !startTriggeredToday)
{
  monitoringStarted = true;
  monitoringFinishedToday = false;
  startTriggeredToday = true;

  monitorStartMillis = millis();

  Serial.println("===== MONITOR START =====");

  lcd.clear();
  lcd.print("MONITOR START");
  delay(1000);

  showMonitoringScreen();
}


  // MONITOR ACTIVE

  if(monitoringStarted)
  {
    Serial.println("Monitoring running...");
distance = getDistance();

Serial.print("Distance: ");
Serial.println(distance);

// =========================
// INVALID READING FILTER
// =========================

if(distance <= 0 || distance > 400)
{
    Serial.println("Invalid reading");

    delay(300);
    return;
}

// =========================
// PERSON DETECTION
// =========================

if(distance < 200)
{
    int motion = digitalRead(PIR_PIN);

    float diff = abs(distance - prevDistance);

    Serial.print("Change: ");
    Serial.println(diff);

    // =========================
    // REAL MOVEMENT DETECTION
    // =========================

// =========================
// REAL MOVEMENT FILTER
// =========================

if(motion == HIGH && diff > 10)
{
    movementCount++;

    Serial.print("Movement Count: ");
    Serial.println(movementCount);
}
else
{
    movementCount = 0;

    Serial.println("No Movement (Sleeping)");
}

// =========================
// CONFIRMED MOVEMENT
// =========================

if(movementCount >= 2)
{
    Serial.println("REAL MOVEMENT DETECTED");

    lcd.clear();
    lcd.print("PERSON WOKE");
    lcd.setCursor(0,1);
    lcd.print("UP");

    monitoringStarted = false;
    monitoringFinishedToday = true;

    movementCount = 0;

    delay(2000);

    showMonitoringScreen();
}
}
else
{
    Serial.println("No Person Detected");
}

prevDistance = distance;

// SENSOR STABILIZATION
delay(300);

    // DURATION CHECK

   Serial.print("Elapsed: ");
Serial.println((millis() - monitorStartMillis)/1000);

if(millis() - monitorStartMillis > durMin * 60000)
    {
      Serial.println("AUTO SOS");

      lcd.clear();
      lcd.print("NO MOVEMENT");
      delay(1000);

      handleSOS();

      monitoringStarted = false;
      monitoringFinishedToday = true;
    }
  }


  // RESET FOR NEXT DAY

  if(nowMin < startMin)
  {
    startTriggeredToday = false;
    monitoringFinishedToday = false;
  }
}} make it into .c file