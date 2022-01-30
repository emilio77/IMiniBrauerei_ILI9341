// iMiniBrauerei  für ESPs mit eingebautem ILI9341 320x240 + JSON von emilio  
// RX : DS18B20 Sensor
// D4 : Onboard LED
// D0 : Heizung
// D1 : Rührwerk
// D2 : Pumpe/Kühlung
// TX : Alarm
// D3 : TFT - DC 
// D5 : TFT - CLK
// D7 : TFT - MOSI
// RST : TFT - RES

#include <ESP8266WiFi.h>                             //https://github.com/esp8266/Arduino
#include <WiFiClient.h>                              //https://github.com/esp8266/Arduino
#include <EEPROM.h>                                  
#include <WiFiManager.h>                             //https://github.com/kentaylor/WiFiManager
#include <ESP8266WebServer.h>                        //http://www.wemos.cc/tutorial/get_started_in_arduino.html
#include <DoubleResetDetector.h>                     //https://github.com/datacute/DoubleResetDetector
#include <OneWire.h>                                 //http://www.pjrc.com/teensy/td_libs_OneWire.html
#include "SPI.h"
#include "TFT_eSPI.h"                                //https://github.com/Bodmer/TFT_eSPI
#include "TJpg_Decoder.h"                            //https://github.com/Bodmer/TJpg_Decoder
#include "icons.h"

#define Version "2.1.0"

#define deltaMeldungMillis 5000                      // Sendeintervall an die Brauerei in Millisekunden
#define DRD_TIMEOUT 10                               // Number of seconds after reset during which a subseqent reset will be considered a double reset.
#define DRD_ADDRESS 0                                // RTC Memory Address for the DoubleResetDetector to use

TFT_eSPI tft = TFT_eSPI();

DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

IPAddress UDPip(192,168,178,255);                     // IP-Adresse an welche UDP-Nachrichten geschickt werden xx.xx.xx.255 = Alle Netzwerkteilnehmer die am Port horchen.
IPAddress myIP(192,168,4,1);                          // IP-Adresse als HotSpot
unsigned int answerPort = 5003;                       // Port auf den Temperaturen geschickt werden
unsigned int localPort = 5010;                        // Port auf dem gelesen wird
ESP8266WebServer server(80);                          // Webserver initialisieren auf Port 80
WiFiUDP Udp;

OneWire ds(3); 

const char *ssid = "IMiniBrauerei";
const char *password = "IMiniBrauerei";

const int PIN_LED = D4;                                 // Controls the onboard LED.

const int Heizung = D0;                                 // Im folgenden sind die Pins der Sensoren und Aktoren festgelegt
const int Ruehrwerk = D1;
const int Pumpe = D2;
const int Summer = 1;

char charVal[8];
char packetBuffer[24];                                 
char temprec[24] = "";
char relais[5] = "";
char state[3] = "";
char relaisold[5] = "";
char stateold[3] = "";

unsigned long jetztMillis = 0, letzteInMillis = 0, letzteOfflineMillis = 0, letzteTempMillis = 0, displayMillis=0;

float Temp = 0.0;
float Tempold = 1.0;
int solltemp = 0;
int solltempold = 1;

String str = "";

bool HLowActive, RLowActive, PLowActive, ALowActive, HotSpot = true;
bool initialConfig = false;                           

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  if ( y >= tft.height() ) return 0;     // Stop further decoding as image is running off bottom of screen
  tft.pushImage(x, y, w, h, bitmap);     // This function will clip the image block rendering automatically at the TFT boundaries
  return 1;                              // Return 1 to decode next block
}

void handleGetValueJSON() {
  char dummy[8];
  String message = "{ ";
  message += "\"temperature\":\"";
  dtostrf(Temp, 1, 1, dummy);  
  message += dummy;
  message += "\",";
  message += "\"heating\":\"";
  if (relais[1] == 'H') { message +="Ein"; } else { message +="Aus"; }
  message += "\",";
  message += "\"agitator\":\"";
  if (relais[1] == 'R') { message +="Ein"; } else { message +="Aus"; }
  message += "\",";
  message += "\"pump\":\"";
  if (relais[1] == 'P') { message +="Ein"; } else { message +="Aus"; }
  message += "\",";
  message += "\"alarm\":\"";
  if (relais[1] == 'A') { message +="Ein"; } else { message +="Aus"; }
  message += "\",";  
  message += "\"state\":\"";
  if (state[1]=='o') { message +="OFFLINE "; }        
  else if (state[1]=='x') { message +="INAKTIV"; }
  else if (state[1]=='y') { message +="AKTIV"; }
  else if (state[1]=='z') { message +="PAUSIERT"; }
  message += "\",";  
  message +="\"IP\":\"";
  IPAddress IP = WiFi.localIP();
  message += IP[0];
  message += ".";
  message += IP[1];
  message += ".";
  message += IP[2];
  message += ".";
  message += IP[3];  
  message += "\",";  
  message +="\"PortIn\":\"";
  message +=localPort; 
  message += "\",";  
  message +="\"PortOut\":\"";
  message +=answerPort; 
  message += "\" }";
  server.send(200, "application/json", message);
}

void Hauptseite()
{
  char dummy[8];
  String Antwort = "";
  Antwort += "<meta http-equiv='refresh' content='5'/>";
  Antwort += "<font face=";
  Antwort += char(34);
  Antwort += "Courier New";
  Antwort += char(34);
  Antwort += ">";
   
  Antwort += "<b>Aktuelle Temperatur: </b>\n</br>";
  
  dtostrf(Temp, 5, 1, dummy);  
  Antwort += dummy;
  Antwort += " ";
  Antwort += char(176);
  Antwort += "C\n</br>";

  Antwort += "\n</br><b>Schaltstatus: </b>\n</br>Heizung:&nbsp;&nbsp;";
  if (relais[1] == 'H') { Antwort +="Ein\n</br>"; } else { Antwort +="Aus\n</br>"; }
  Antwort +="R"; Antwort +=char(252); Antwort +="hrwerk:&nbsp;";
  if (relais[2] == 'R') { Antwort +="Ein\n</br>"; } else { Antwort +="Aus\n</br>"; }
  Antwort += "Pumpe:&nbsp;&nbsp;&nbsp;&nbsp;";
  if (relais[3] == 'P') { Antwort +="Ein\n</br>"; } else { Antwort +="Aus\n</br>"; }
  Antwort += "Alarm:&nbsp;&nbsp;&nbsp;&nbsp;";
  if (relais[4] == 'A') { Antwort +="Ein\n</br>"; } else { Antwort +="Aus\n</br>"; }
  Antwort +="\n</br><b>Brauereistatus: </b>\n</br>";
  if (state[1]=='o') { Antwort +="OFFLINE "; }        
  else if (state[1]=='x') { Antwort +="INAKTIV"; }
  else if (state[1]=='y') { Antwort +="AKTIV"; }
  else if (state[1]=='z') { Antwort +="PAUSIERT"; }
  Antwort +="\n</br>";      
  Antwort +="</br>Verbunden mit: ";
  Antwort +=WiFi.SSID(); 
  Antwort +="</br>Signalstaerke: ";
  Antwort +=WiFi.RSSI(); 
  Antwort +="dBm  </br>";
  Antwort +="</br>IP-Adresse: ";
  IPAddress IP = WiFi.localIP();
  Antwort += IP[0];
  Antwort += ".";
  Antwort += IP[1];
  Antwort += ".";
  Antwort += IP[2];
  Antwort += ".";
  Antwort += IP[3];
  Antwort +="</br>";
  Antwort +="</br>UDP-IN port: ";
  Antwort +=localPort; 
  Antwort +="</br>UDP-OUT port: ";
  Antwort +=answerPort; 
  Antwort +="</br></br>";
  Antwort += "</font>";
  server.send ( 300, "text/html", Antwort );
}

void packetAuswertung()
{
  int temp = 0;
  int temp2 = 0;
  if ((temprec[0]=='C') && (temprec[18]=='c'))             // Begin der Decodierung des seriellen Strings  
  { 
    temp=(int)temprec[1];
    if ( temp < 0 ) { temp = 256 + temp; }
    if ( temp > 7) {relais[4]='A';temp=temp-8;} else {relais[4]='a';} 
    if ( temp > 3) {relais[3]='P';temp=temp-4;} else {relais[3]='p';} 
    if ( temp > 1) {relais[2]='R';temp=temp-2;} else {relais[2]='r';}
    if ( temp > 0) {relais[1]='H';temp=temp-1;} else {relais[1]='h';}   

    temp=(int)temprec[2];
    if ( temp < 0 ) { temp = 256 + temp; }
    if ( temp > 127) {temp=temp-128;}  
    if ( temp > 63) {temp=temp-64;}
    if ( temp > 31) {temp=temp-32;}    
    if ( temp > 15) {temp=temp-16;}  
    if ( temp > 7) {temp=temp-8;}  
    if ( temp > 3) {state[1]='x';temp=temp-4;} 
    else if ( temp > 1) {state[1]='z';temp=temp-2;}  
    else if ( temp > 0) {state[1]='y';temp=temp-1;}    

    temp=(int)temprec[3];
    if ( temp < 0 ) { temp = 256 + temp; }
    solltemp=temp;
  }
}

void DisplayInitOut() {
  tft.fillScreen(TFT_WHITE);  
  tft.fillRect(0, 0, 320, 30, TFT_LIGHTGREY);
  tft.setCursor(6, 6);
  tft.print("Isttemp.");
  tft.setCursor(167, 6);
  tft.print("Solltemp.");
  tft.setCursor(3, 177);
  tft.print("IP-Adr.: ");
  tft.println(str);  
  tft.setCursor(3, 200);
  tft.print("Out: ");
  tft.println(answerPort);
  tft.setCursor(165, 200);  
  tft.print("In: ");
  tft.println(localPort); 
  tft.setCursor(3, 222);  
  tft.print("IMiniBraurei Version ");
  tft.print(Version);
  tft.drawLine(0, 100, 320, 100, TFT_BLACK);
  tft.drawLine(0, 101, 320, 101, TFT_BLACK);
  tft.drawLine(0, 30, 320, 30, TFT_BLACK);
  tft.drawLine(0, 31, 320, 31, TFT_BLACK);
  tft.drawLine(0, 169, 320, 169, TFT_BLACK);
  tft.drawLine(0, 170, 320, 170, TFT_BLACK);  
  tft.drawLine(159, 0, 159, 100, TFT_BLACK);
  tft.drawLine(160, 0, 160, 100, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(120, 3);
  tft.write(247);
  tft.print("C");  
  tft.setCursor(282, 3);
  tft.write(247);
  tft.print("C");
  tft.setTextSize(2);
}
 
void DisplayOut() {
  if (stateold[1]!=state[1]) {    
    tft.fillRect(257, 106, 16, 16, TFT_WHITE);
    if (state[1]=='o') { TJpgDec.drawJpg(257, 106, offline, sizeof(offline));}  
    else if (state[1]=='x') { TJpgDec.drawJpg(257, 106, stopp, sizeof(stopp));}
    else if (state[1]=='y') { TJpgDec.drawJpg(257, 106, play, sizeof(play));}
    else if (state[1]=='z') { TJpgDec.drawJpg(257, 106, pause, sizeof(pause));}
    stateold[1]=state[1];
  }
  if (relaisold[1]!=relais[1]) {
    if (relais[1] == 'H') { TJpgDec.drawJpg(5, 106, feuer_an, sizeof(feuer_an));} 
    else { TJpgDec.drawJpg(5, 106, feuer_aus, sizeof(feuer_aus));}
    relaisold[1]!=relais[1];
  }
  if (relaisold[2]!=relais[2]) {
    if (relais[2] == 'R') { TJpgDec.drawJpg(68, 106, ruehrer_an, sizeof(ruehrer_an));} 
    else { TJpgDec.drawJpg(68, 106, ruehrer_aus, sizeof(ruehrer_aus));}
    relaisold[2]!=relais[2];
  }
  if (relaisold[3]!=relais[3]) {
    if (relais[3] == 'P') { TJpgDec.drawJpg(131, 106, kuehlung_an, sizeof(kuehlung_an));} 
    else { TJpgDec.drawJpg(131, 106, kuehlung_aus, sizeof(kuehlung_aus));}
    relaisold[3]!=relais[3];
  }
  if (relaisold[4]!=relais[4]) {
    if (relais[4] == 'A') { TJpgDec.drawJpg(194, 106, alarm_an, sizeof(alarm_an));} 
    else { TJpgDec.drawJpg(194, 106, alarm_aus, sizeof(alarm_aus));}
    relaisold[4]!=relais[4];
  }
  if (Temp!=Tempold) {
    tft.fillRect(18, 50, 140, 40, TFT_WHITE);
    dtostrf(Temp, 3, 1, charVal); 
    if (Temp<100) {str=charVal;} else {str="100";};  
    if (Temp<10) {tft.setCursor(38, 50);} else {tft.setCursor(18, 50);}
    tft.setTextSize(5);
    tft.print(str);
    tft.setTextSize(2);
  }      
  if (solltemp!=solltempold) {
    tft.fillRect(180, 50, 140, 40, TFT_WHITE);
    dtostrf(solltemp, 3, 1, charVal); 
    if (solltemp<100) {str=charVal;} else {str="100";};  
    if (solltemp<10) {tft.setCursor(200, 50);} else {tft.setCursor(180, 50);}
    tft.setTextSize(5);
    tft.print(str);
    tft.setTextSize(2);
  }
  Tempold=Temp;
  solltempold=solltemp;
}

void UDPOut() {
  dtostrf(Temp, 3, 1, charVal);
  Udp.beginPacket(UDPip, answerPort);
  Udp.write('T');
  if (Temp<10) {Udp.write(' ');}
  Udp.write(charVal);
  Udp.write('t');
  Udp.println();
  Udp.endPacket();
}

void UDPRead()
{
  int packetSize = Udp.parsePacket();
  if (packetSize)
  {
    for (int schleife = 0; schleife < 23; schleife++) { temprec[schleife] = ' '; }
    // read the packet into packetBufffer
    Udp.read(packetBuffer, packetSize);
    for (int schleife = 0; schleife < 23; schleife++) { temprec[schleife] = packetBuffer[schleife]; }
    letzteInMillis = millis();
    packetAuswertung();
  }
}    

void OfflineCheck()
{
  if (jetztMillis > letzteInMillis+10000) 
  {
    if (jetztMillis > letzteOfflineMillis+1000) {letzteOfflineMillis=jetztMillis; } 
    relais[1]='h';      
    relais[2]='r';               
    relais[3]='p'; 
    relais[4]='a';       
    state[1]='o';
  }
}

void RelaisOut()
{
  if (relais[1] == 'H') { digitalWrite(Heizung,!HLowActive); } else { digitalWrite(Heizung,HLowActive); }
  if (relais[2] == 'R') { digitalWrite(Ruehrwerk,!RLowActive); } else { digitalWrite(Ruehrwerk,RLowActive); }
  if (relais[3] == 'P') { digitalWrite(Pumpe,!PLowActive); } else { digitalWrite(Pumpe,PLowActive); }
  if (relais[4] == 'A') { digitalWrite(Summer,!ALowActive); } else { digitalWrite(Summer,ALowActive); }
}

float DS18B20lesen()
{
  int TReading, SignBit;
  byte i, present = 0, data[12], addr[8];
  if ( !ds.search(addr))  { ds.search(addr); }        // Wenn keine weitere Adresse vorhanden, von vorne anfangen
  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);                                  // start Konvertierung, mit power-on am Ende
  delay(750);                                         // 750ms sollten ausreichen
  present = ds.reset();
  ds.select(addr);
  ds.write(0xBE);                                     // Wert lesen
  for ( i = 0; i < 9; i++) { data[i] = ds.read(); }
  TReading = (data[1] << 8) + data[0];
  SignBit = TReading & 0x8000;                        // test most sig bit
  if (SignBit) {TReading = (TReading ^ 0xffff) + 1;}  // 2's comp
  Temp = TReading * 0.0625;                           // Für DS18S20  temperatur = TReading*0.5 / DS18B20 temperatur = TReading*0.0625
  if (SignBit) {Temp = Temp * -1;}
  return Temp;
}

void ReadSettings() {
  EEPROM.begin(512);
  EEPROM.get(0, localPort);
  EEPROM.get(20, answerPort);
  EEPROM.get(40, HLowActive);
  EEPROM.get(50, RLowActive);
  EEPROM.get(60, PLowActive);
  EEPROM.get(70, ALowActive);
  EEPROM.get(90, HotSpot);
  EEPROM.commit();
  EEPROM.end();
}  

void WriteSettings() {
  EEPROM.begin(512);
  EEPROM.put(0, localPort);
  EEPROM.put(20, answerPort);
  EEPROM.put(40, HLowActive);
  EEPROM.put(50, RLowActive);
  EEPROM.put(60, PLowActive);
  EEPROM.put(70, ALowActive);
  EEPROM.put(90, HotSpot);
  EEPROM.end();    
}

void setup() {
  pinMode(PIN_LED, OUTPUT);       // Im folgenden werden die Pins als I/O definiert
  pinMode(Heizung, OUTPUT);
  pinMode(Summer, OUTPUT);
  pinMode(Ruehrwerk, OUTPUT);
  pinMode(Pumpe, OUTPUT);
  pinMode(0, FUNCTION_3); 
  pinMode(3, FUNCTION_3); 

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK);  
  tft.setTextSize(2);
  TJpgDec.setJpgScale(1);                // The jpeg image can be scaled by a factor of 1, 2, 4, or 8
  TJpgDec.setSwapBytes(true);            // The byte order can be swapped (set true for TFT_eSPI)
  TJpgDec.setCallback(tft_output);       // The decoder must be given the exact name of the rendering function above
     
  ReadSettings();

  if (drd.detectDoubleReset()) {
    initialConfig = true;
  }
  
  if (initialConfig) {
    digitalWrite(PIN_LED, LOW); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.
    tft.setCursor(95, 112);
    tft.print("Config-Mode");
    char convertedValue[5];

    char hotspot[24] = "type=\"checkbox\"";
    if (HotSpot) {strcat(hotspot, " checked");}
    WiFiManagerParameter p_hotspot("HotSpot", "Create HotSpot", "T", 2, hotspot, WFM_LABEL_AFTER);
    sprintf(convertedValue, "%d", answerPort);
    WiFiManagerParameter p_answerPort("answerPort", "send temperature on UDP Port", convertedValue, 5);
    sprintf(convertedValue, "%d", localPort);
    WiFiManagerParameter p_localPort("localPort", "receive relais state on UDP Port", convertedValue, 5);
    char hlowactive[24] = "type=\"checkbox\"";
    if (HLowActive) {strcat(hlowactive, " checked");}
    WiFiManagerParameter p_hlowactive("HLowActive", "Heizung (D0) Low Active", "T", 2, hlowactive, WFM_LABEL_AFTER);
    char rlowactive[24] = "type=\"checkbox\"";
    if (RLowActive) {strcat(rlowactive, " checked");}
    WiFiManagerParameter p_rlowactive("RLowAactive", "Ruehrwerk (D1) Low Active", "T", 2, rlowactive, WFM_LABEL_AFTER);
    char plowactive[24] = "type=\"checkbox\"";
    if (PLowActive) {strcat(plowactive, " checked");}
    WiFiManagerParameter p_plowactive("PLowActive", "Pumpe/Kuehlung (D2) Low Active", "T", 2, plowactive, WFM_LABEL_AFTER);
    char alowactive[24] = "type=\"checkbox\"";
    if (ALowActive) {strcat(alowactive, " checked");}
    WiFiManagerParameter p_alowactive("ALowActive", "Alarm (D8) Low Active", "T", 2, alowactive, WFM_LABEL_AFTER);

    WiFiManager wifiManager;
    wifiManager.setBreakAfterConfig(true);
    wifiManager.addParameter(&p_hotspot);
    wifiManager.addParameter(&p_answerPort);
    wifiManager.addParameter(&p_localPort);
    wifiManager.addParameter(&p_hlowactive);
    wifiManager.addParameter(&p_rlowactive);
    wifiManager.addParameter(&p_plowactive);
    wifiManager.addParameter(&p_alowactive);
    wifiManager.setConfigPortalTimeout(300);
    wifiManager.startConfigPortal();
    
    HotSpot = (strncmp(p_hotspot.getValue(), "T", 1) == 0);
    answerPort = atoi(p_answerPort.getValue());
    localPort = atoi(p_localPort.getValue());
    HLowActive = (strncmp(p_hlowactive.getValue(), "T", 1) == 0);
    RLowActive = (strncmp(p_rlowactive.getValue(), "T", 1) == 0);
    PLowActive = (strncmp(p_plowactive.getValue(), "T", 1) == 0);
    ALowActive = (strncmp(p_alowactive.getValue(), "T", 1) == 0);

    tft.fillScreen(TFT_WHITE);
    WriteSettings();
  }

  if (!HotSpot) {
    WiFi.setOutputPower(20.5);
    WiFi.mode(WIFI_STA); // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
    unsigned long startedAt = millis();
    int connRes = WiFi.waitForConnectResult();
    float waited = (millis()- startedAt);  
    if (WiFi.status()!=WL_CONNECTED){
    } else{
      Udp.begin(localPort);
      UDPip=WiFi.localIP();
      str=String(UDPip[0])+"."+String(UDPip[1])+"."+String(UDPip[2])+"."+String(UDPip[3]);
      DisplayInitOut();
      UDPip[3]=255;
      delay(500);  
      server.on("/", Hauptseite);
      server.on("/json", handleGetValueJSON);
      server.begin();                          // HTTP-Server starten
    }
  } else { 
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(false);
    WiFi.mode(WIFI_AP); // Force to acess point mode.
    WiFi.softAP(ssid, password);
    IPAddress myIP = WiFi.softAPIP();
    digitalWrite(PIN_LED, HIGH); // Turn led off as we are not in configuration mode.
    Udp.begin(localPort);
    str=String(myIP[0])+"."+String(myIP[1])+"."+String(myIP[2])+"."+String(myIP[3]);
    DisplayInitOut();
    UDPip = myIP;
    UDPip[3]=255;
    delay(500);  
    server.on("/", Hauptseite);
    server.on("/json", handleGetValueJSON);
    server.begin();                          // HTTP-Server starten
  } 
}

void loop() {
  drd.loop();
  jetztMillis = millis();
  server.handleClient(); // auf HTTP-Anfragen warten
  if ((WiFi.status()!=WL_CONNECTED) and (!HotSpot)) {
    WiFi.reconnect();
    delay(5000);
    Udp.begin(localPort);
    server.on("/", Hauptseite);    
    server.on("/json", handleGetValueJSON);
    server.begin();            // HTTP-Server starten
  } else{
    if(!deltaMeldungMillis == 0 && jetztMillis - letzteTempMillis > deltaMeldungMillis)
    {
      digitalWrite(PIN_LED, LOW);
      Temp = DS18B20lesen();
      UDPOut();
      letzteTempMillis = jetztMillis;
      digitalWrite(PIN_LED, HIGH);
    }
    UDPRead();
    OfflineCheck();
    RelaisOut();
    DisplayOut();
    Hauptseite();
    handleGetValueJSON();
    if (jetztMillis < 100000000) {wdt_reset();}             // WatchDog Reset  
  }  
}
