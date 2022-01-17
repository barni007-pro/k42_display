#include "Arduino.h"
#include <WebServer.h>
#include <WiFi.h>
//#include <MQTT.h>
#include <EEPROM.h>
#include <Update.h>
#include <time.h>
#include <TimeLib.h>
#include "EPD.h"
#include "GUI_Paint.h"
#include <stdlib.h>
#include "k42_display.h"

//GPIO
#define LED 2

//WEB Server
WebServer server(80);

// System definitions
configData_t cfg;
const int cfgStart = 0;                                                //EEPROM Startaddress
const int EEPROM_Version = 8;
const char *ssid = "IoT_Display";
const char WeekDays[][3] = {"Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"}; // Abkuerzungen der Wochentage
const String MonthName[] = {"Januar","Februar","Maerz","April","Mai","Juni","Juli","August","September","Oktober","Novembar","Dezember"};
const String html1 ="<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>Iot Display Configuration</title><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><link rel=\"stylesheet\" href=\"style.css\"></head><body><label for=\"show-menu\" class=\"show-menu\">Show Menu</label><input type=\"checkbox\" id=\"show-menu\"><ul id=\"menu\"><li><a href=\"index.html\">Home</a></li><li><a href=\"config.html\">Configuration</a></li><li><a href=\"wifi.html\">WIFI</a></li><li><a href=\"reboot.html\">Reboot</a></li></ul><br><br><br><fieldset>";
const String HW_Version = "Display 001";                               //Hardware Version old
const String SW_Version = "A1.00.03";                                  //Software Version

//System Variablen
UBYTE *BlackImage, *RYImage;                                           // Red or Yellow Image
bool wifi_sta_ready = true;
int sleep_mode = 0;

//Soft Timer1
long wait_millis_1 = 60000 * 1;                                        //60 Min.
long next_millis_1 = 0;                                                //Start sofort

void setup() 
{
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason)
  {
    case 1  : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case 2  : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case 3  : Serial.println("Wakeup caused by timer"); break;
    case 4  : Serial.println("Wakeup caused by touchpad"); break;
    case 5  : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.println("Wakeup was not caused by deep sleep"); break;
  }
  loadConfig();
  if (cfg.valid != EEPROM_Version)  //No valid data in the configuration therefore set the default parameters
  {
    default_setting();
  }
  sleep_mode = String(cfg.timerm).toInt();
  long zw = String(cfg.interval).toInt();
  wait_millis_1 = zw * 60000;
  
  if (wifi_start_STA() == false)
  {
    wifi_sta_ready = false;
    wifi_start_AP();
  }
  else
  {
    Serial.println("Time Sync ...");
    timesync();
    Serial.println("Time Sync Ready");
  }
  webserver_start();

  init_eink();
  clear_image_buff();

  if (wifi_sta_ready == false)
  {
    String ssid_mac_str = String(ssid) + "_" + WiFi.macAddress();
    const char *ssid_mac = ssid_mac_str.c_str();
    Paint_SelectImage(BlackImage);
    Paint_DrawString_EN(0, 60, "Connect to SSID:", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(0, 90, ssid_mac, &Font16, WHITE, BLACK);
    Paint_SelectImage(RYImage);
    Paint_DrawString_EN(0, 160, "Open this Link:", &Font16, WHITE, BLACK);
    Paint_DrawString_EN(0, 190, "http://192.168.4.1", &Font16, WHITE, BLACK);
    EPD_4IN2B_V2_Display(BlackImage, RYImage);
    DEV_Delay_ms(2000);
  }
  digitalWrite(LED, LOW);
}

void loop() 
{
  if (millis() > next_millis_1)
  {
    digitalWrite(LED, LOW);
    Serial.println("TIMER_1");
    next_millis_1 = millis() + wait_millis_1;
       
    if (wifi_sta_ready == true)
    {
      struct tm loctime;
      getLocalTime(&loctime);
      switch(sleep_mode)
      {
        case 0:
          Serial.println("timerm = 0");
          draw_cal(loctime.tm_year + 1900,loctime.tm_mon + 1,loctime.tm_mday);        
          break;
        case 1:
          Serial.println("timerm = 1");
          draw_cal(loctime.tm_year + 1900,loctime.tm_mon + 1,loctime.tm_mday);                
          reset_timer_1();
          sleep_mode = 2;
          break;
        case 2:
          Serial.println("timerm = 2");
          Serial.print("Sleep [ms] = ");
          Serial.println(wait_millis_1);
          delay(1000);
          Serial.flush(); 
          esp_sleep_enable_timer_wakeup(wait_millis_1 * 1000);    // Deep Sleep Zeit einstellen
          esp_deep_sleep_start();                                 // Starte Deep Sleep          
          break;
        default:
          break;            
      }      
    }
  }
  digitalWrite(LED, HIGH);
  server.handleClient();
}

void init_eink()
{
  DEV_Module_Init();
  EPD_4IN2B_V2_Init();
  EPD_4IN2B_V2_Clear();
  DEV_Delay_ms(500);

  //Create a new image cache named IMAGE_BW and fill it with white
  
  UWORD Imagesize = ((EPD_4IN2B_V2_WIDTH % 8 == 0) ? (EPD_4IN2B_V2_WIDTH / 8 ) : (EPD_4IN2B_V2_WIDTH / 8 + 1)) * EPD_4IN2B_V2_HEIGHT;
  if ((BlackImage = (UBYTE *)malloc(Imagesize)) == NULL) {
    Serial.println("Failed to apply for black memory...");
    while(1);
  }
  if ((RYImage = (UBYTE *)malloc(Imagesize)) == NULL) {
    Serial.println("Failed to apply for red memory...");
    while(1);
  }
  Paint_NewImage(BlackImage, EPD_4IN2B_V2_WIDTH, EPD_4IN2B_V2_HEIGHT, 0, WHITE);
  Paint_NewImage(RYImage, EPD_4IN2B_V2_WIDTH, EPD_4IN2B_V2_HEIGHT, 0, WHITE);
}

void clear_image_buff()
{
  // clear Image Buffer
  Paint_SelectImage(BlackImage);
  Paint_Clear(WHITE);
  Paint_SelectImage(RYImage);
  Paint_Clear(WHITE);   
}

void test_eink()
{
  Paint_SelectImage(BlackImage);
  Paint_DrawString_EN(0, 20, "Reimar Barnstorf", &Font24, WHITE, BLACK);
  Paint_SelectImage(RYImage);
  Paint_DrawString_EN(0, 50, "Reimar Barnstorf", &Font24, WHITE, BLACK);
  EPD_4IN2B_V2_Display(BlackImage, RYImage);
  DEV_Delay_ms(2000);
}

void draw_cal(uint16_t y, uint8_t m, uint8_t d)
{
  int h_pos = 0;
  
  Paint_SelectImage(BlackImage);
  for (int i = 0; i <= 5; i++) 
  {
    Paint_DrawString_EN(i * 55 + 20, 42, WeekDays[i], &Font24, BLACK, WHITE);
  }  
  Paint_SelectImage(RYImage);
  Paint_DrawString_EN(6 * 55 + 20, 42, WeekDays[6], &Font24, BLACK, WHITE);

  
  int j = GetWeekday(y, m, 1) - 1;
  for (int i = 0; i < GetDaysOfMonth(y, m) ; i++)
  {
    int k = (j + i) % 7;
    int l = (j + i) / 7;
    bool feiertag = false;
    
    if (k == 6) //dieser Tag ist ein Sonntag
    {
      Paint_SelectImage(RYImage);
    }
    else
    {
      Paint_SelectImage(BlackImage);
    }

    String SearchStr  = "," + String(m) + "." + String(i + 1) + "-";
    String HolidayStr = "," + String(cfg.holiday) + ",";
    String BirthdayStr = "," + String(cfg.birthday) + ",";

    if (strstr(HolidayStr.c_str(), SearchStr.c_str()) != NULL) //dieser Tag ist ein Feiertag
    {
      feiertag = true;
      Paint_SelectImage(RYImage);        
      Paint_DrawPoint(k * 55 + 20, l * 37 + 78, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
      if ((h_pos <= 2) && ((i + 1) >= d))
      {
        char* str1 = strstr(HolidayStr.c_str(), SearchStr.c_str());
        char* str2 = strstr(str1, "-");
        String str3 = String(str2);
        int pos = str3.indexOf(",");
        String str4 = String(i + 1) + "." + String(m) + ". " + str3.substring(1, pos);        
        
        Paint_DrawPoint(200 ,  h_pos * 12 + 6, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);        
        Paint_SelectImage(BlackImage);
        Paint_DrawString_EN(206, h_pos * 12, str4.c_str(), &Font12, WHITE, BLACK);
        Paint_SelectImage(RYImage);
        ++h_pos;
      }
    }
    
    if (i <= 8) //etwas weiter rechts schreiben wenn Tag 1-9
    {
      Paint_DrawNum(k * 55 + 28, l * 37 + 80, i + 1, &Font24, BLACK, WHITE);
    }
    else
    {
      Paint_DrawNum(k * 55 + 20, l * 37 + 80, i + 1, &Font24, BLACK, WHITE);  
    }
    
    if ((i + 1) == d) //aktuellen Tag umranden
    {
      Paint_DrawRectangle(k * 55 + 16, l * 37 + 74, k * 55 + 16 + 40, l * 37 + 74 + 32, BLACK, DOT_PIXEL_2X2, DRAW_FILL_EMPTY);
    }

    if (strstr(BirthdayStr.c_str(), SearchStr.c_str()) != NULL) //dieser Tag ist ein Geburtstag
    {
      Paint_SelectImage(BlackImage);        
      if (feiertag == true)
      {
        Paint_DrawPoint(k * 55 + 26, l * 37 + 78, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
      }
      else
      {
        Paint_DrawPoint(k * 55 + 20, l * 37 + 78, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
      }
      //Paint_DrawPoint(k * 55 + 52, l * 37 + 78, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
      if ((h_pos <= 2) && ((i + 1) >= d))
      {
        char* str1 = strstr(BirthdayStr.c_str(), SearchStr.c_str());
        char* str2 = strstr(str1, "-");
        String str3 = String(str2);
        int pos = str3.indexOf(",");
        String str4 = String(i + 1) + "." + String(m) + ". " + str3.substring(1, pos);        

        Paint_DrawPoint(200 ,  h_pos * 12 + 6, BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
        Paint_DrawString_EN(206, h_pos * 12, str4.c_str(), &Font12, WHITE, BLACK);

        ++h_pos;
      }
    }    
  } 
  Paint_SelectImage(RYImage);
  //String str1 = MonthName[m - 1] + " " + String(y) + " W" + String(GetWeekNumber(y, m, d));
  //const char* str2 = str1.c_str();
  //Paint_DrawString_EN(20, 0, str2, &Font24, WHITE, BLACK);
  Paint_DrawString_EN(20, 0, MonthName[m - 1].c_str(), &Font24, WHITE, BLACK);
  
  Paint_SelectImage(BlackImage);
  String str1 = String(y) + " Woche-" + String(GetWeekNumber(y, m, d));
  Paint_DrawString_EN(20, 24, str1.c_str(), &Font12, WHITE, BLACK);
  
  //for (int i = 0; i <= 1 ; i++)
  //{
  //  Paint_DrawLine(1, i * 37 + 34, 400, i * 37 + 34, BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);  
  //}
  Paint_DrawRectangle(1,38,400,38+32, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawRectangle(285,38,400,300, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  Paint_DrawRectangle(1,38,285,300, BLACK, DOT_PIXEL_1X1, DRAW_FILL_EMPTY);
  EPD_4IN2B_V2_Display(BlackImage, RYImage);
  DEV_Delay_ms(2000);
  if (String(cfg.timerm) == "1")
  {
    Serial.println("Display Sleep...");
    EPD_4IN2B_V2_Sleep();      
  }
}


/***** Den Wochentag nach ISO 8601 (1 = Mo, 2 = Di, 3 = Mi, 4 = Do, 5 = Fr, 6 = Sa, 7 = So) berechnen *****/
uint8_t GetWeekday(uint16_t y, uint8_t m, uint8_t d) 
{
  static int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  y -= m < 3;
  uint8_t wd = (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
  return (wd == 0 ? 7 : wd);
}

/***** Die Wochennummer nach ISO 8601 berechnen *****/
uint8_t GetWeekNumber(uint16_t y, uint8_t m, uint8_t d) 
{
  bool LeapYear;
  uint16_t doy = GetDayOfYear(y, m, d);  // Anzahl der Tage im Jahr ermitteln
  uint8_t wd = GetWeekday(y, m, d);      // Wochentag ermitteln
  uint8_t wnr = (doy - wd + 7) / 7;     // die Wochennummer berechnen
  switch (wnr) {
    case 0:                              // wenn die Wochennummer Null ergibt, dann liegt der Tag am Anfang des Jahres (1. Sonderfall)
      wd = GetWeekday(y - 1, 12, 31);    // den letzten Wochentag aus dem Vorjahr ermitteln
      LeapYear = IsLeapYear(y - 1);      // ermitteln, ob es sich beim Vorjahr um ein Schaltjahr handelt
      break;                             // und nach dem Switch weitermachen...
    case 52:                             // wenn die Wochennummer 52 ergibt, dann liegt der Tag am Ende des Jahres (2. Sonderfall)
      wd = GetWeekday(y, 12, 31);        // den letzten Wochentag aus diesem Jahr ermitteln
      LeapYear = IsLeapYear(y);          // ermitteln, ob es sich bei diesem Jahr um ein Schaltjahr handelt
      break;                             // und nach dem Switch weitermachen...
    default:                             // in den anderen Faellen kann die Funktion
      return wnr;                        // hier verlassen und die Wochennummer zurueckgegeben werden
  }
  if (wd < 4) {                          // wenn der 31.12. vor dem Donnerstag liegt, dann...
    wnr = 1;                             // ist das die erste Woche des Jahres
  } else {                               // anderenfalls muss ermittelt werden, ob es eine 53. Woche gibt (3. Sonderfall)
    /* wenn der letzte Wochentag auf einen Donnerstag oder,          */
    /* in einem Schaltjahr, auf einen Donnerstag oder Freitag fällt, */
    /* dann ist das die 53. Woche, ansonsten die 52. Woche.          */
    wnr = ((wd == 4) || (LeapYear && wd == 5)) ? 53 : 52;
  }
  return wnr;
}

/***** die Anzahl der Tage (Tag des Jahres) berechnen *****/
uint16_t GetDayOfYear(uint16_t y, uint8_t m, uint8_t d) 
{
  static const uint16_t mdays[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  return d + mdays[m - 1] + (m >= 2 && IsLeapYear(y));
}

/***** Testen, ob das Jahr ein Schaltjahr ist *****/
bool IsLeapYear(uint16_t y) 
{
  return  !(y % 4) && ((y % 100) || !(y % 400)); // Schaltjahrberechnung (true = Schaltjahr, false = kein Schaltjahr)
}

/***** Anzahl der Tage in dem Monat *****/
uint8_t GetDaysOfMonth(uint16_t y, uint8_t m) 
{
  static const uint8_t mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return  mdays[m - 1] + (m = 2 && IsLeapYear(y));
}

void default_setting() //all EEPROM to default
{
    eraseConfig();
    String empty = "";
    String cu_ssid = "SSID";
    String cu_password = "password";
    String cu_ntpserver = "de.pool.ntp.org";
    String cu_tzinfo = "WEST-1DWEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00";
    String cu_mqtt_clientId = "DISP_002";
    String cu_mqtt_server = "192.168.1.3";
    String cu_mqtt_user = "Try";
    String cu_mqtt_password = "Try";
    String cu_holiday = "1.1-Neujahr,4.15-Karfreitag,4.17-Ostern,4.18-Ostermontag,5.1-Tag der Arbeit,5.26-Christi Himmelfahrt,6.6-Pfingstmontag,10.3-Tag der D. Einheit,10.31-Reformationstag,12.25-1.Weihnachtsfeiertag,12.26-2.Weihnachtsfeiertag";
    String cu_birthday = "4.7-Reimar,6.27-Hilke,4.2-Joana,8.5-Leon";
    String cu_timerm = "0";
    String cu_interval = "60";

    cfg.valid = EEPROM_Version;
    strcpy(cfg.ssid, cu_ssid.c_str());
    strcpy(cfg.password, cu_password.c_str());
    strcpy(cfg.ntpserver, cu_ntpserver.c_str());
    strcpy(cfg.tzinfo, cu_tzinfo.c_str());
    strcpy(cfg.mqtt_clientId, cu_mqtt_clientId.c_str());
    strcpy(cfg.mqtt_server, cu_mqtt_server.c_str());
    strcpy(cfg.mqtt_user, cu_mqtt_user.c_str());
    strcpy(cfg.mqtt_password, cu_mqtt_password.c_str());
    strcpy(cfg.holiday, cu_holiday.c_str());
    strcpy(cfg.birthday, cu_birthday.c_str());
    strcpy(cfg.timerm, cu_timerm.c_str());
    strcpy(cfg.interval, cu_interval.c_str());
    
    saveConfig();
} 

void saveConfig() //Save configuration from RAM into EEPROM 
{  
  Serial.println(F("Save Config"));
  Serial.println (String(sizeof(cfg)));
  EEPROM.begin(sizeof(cfg) + 3);
  EEPROM.put( cfgStart, cfg );
  delay(200);
  EEPROM.commit();                      // Only needed for ESP8266 to get data written
  EEPROM.end();                         // Free RAM copy of structure
}

void loadConfig() //Loads configuration from EEPROM into RAM 
{  
  Serial.println(F("Load Config"));
  Serial.println(String(sizeof(cfg)));
  EEPROM.begin(sizeof(cfg) + 3);
  EEPROM.get( cfgStart, cfg );
  EEPROM.end();
}

void eraseConfig()   //Reset EEPROM bytes to '0' for the length of the data structure 
{
  Serial.println(F("Erase Config"));
  Serial.println(String(sizeof(cfg)));
  EEPROM.begin(sizeof(cfg) + 3);
  for (int i = cfgStart ; i < sizeof(cfg) ; i++) {
    EEPROM.write(i, 0);
  }
  delay(200);
  EEPROM.commit();
  EEPROM.end();
}

void wifi_start_AP() //Start WiFi Mode AP
{
  String ssid_mac_str = String(ssid) + "_" + WiFi.macAddress();
  const char *ssid_mac = ssid_mac_str.c_str();
  Serial.println(F("Start WIFI AP..."));
  WiFi.mode(WIFI_OFF);
  delay(500);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid_mac);
  IPAddress myIP = WiFi.softAPIP();
  Serial.println();
  Serial.print(F("IP address AP: "));
  Serial.println(WiFi.softAPIP());
  Serial.print(F("SSID: "));
  Serial.println(ssid_mac);
}

bool wifi_start_STA() //Start WiFi Mode STA
{
  int sync_count = 0;
  WiFi.mode(WIFI_STA);
  if (WiFi.status() != WL_CONNECTED)  
  {
    Serial.println("WiFi Start");
    WiFi.begin(cfg.ssid, cfg.password);
    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
      Serial.print(".");
      sync_count = ++sync_count;
      if (sync_count == 20)
      {
        Serial.println();
        return 0;
        break;     
      }
      if (sync_count == 6)
      {
        WiFi.begin(cfg.ssid, cfg.password); //second try
      }      
    }    
  }
  
  Serial.println();
  Serial.print(F("IP address STA: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("SSID: "));
  Serial.println(WiFi.SSID());
  Serial.printf("BSSID: %s\n", WiFi.BSSIDstr().c_str());
  Serial.print(F("PW: "));
  Serial.println(WiFi.psk());
  return 1;
}

bool timesync()
{
  bool exit_status = 1;
  Serial.println("Get NTP Time");
  if (WiFi.status() == WL_CONNECTED)
  {
    struct tm local;
    configTzTime(cfg.tzinfo, cfg.ntpserver); // ESP32 Systemzeit mit NTP Synchronisieren
    if (!getLocalTime(&local, 10000)) // Versuche 10 s zu Synchronisieren
    {
      Serial.println("Timeserver cannot be reached !!!");
      exit_status = 0;
    }
    else
    {
      Serial.print("Timeserver: ");
      Serial.println(&local, "Datum: %d.%m.%y  Zeit: %H:%M:%S Test: %a,%B");
      Serial.flush();
    }
  }
  else
  {
    Serial.println("WiFi not connected !!!");
    exit_status = 0;    
  }
  
  return exit_status;  
}

void webserver_start() //WEB-SERVER configure and start
{
  server.on("/", HTTP_GET, handleIndex);
  server.on("/index.html", HTTP_GET, handleIndex);
  server.on("/index.html", HTTP_POST, handleIndexPost);
  server.on("/config.html", HTTP_GET, handleConfig);
//  server.on("/746558586d6f.html", HTTP_GET, handleAdmin);
  server.on("/wifi.html", HTTP_GET, handleWifi);
  server.on("/reboot.html", HTTP_GET, handleReboot);
  server.on("/style.css", HTTP_GET, handleCss);
  server.on("/favicon.ico", HTTP_GET, handleNotFound);
//API GET Variablen
//  server.on("/api", HTTP_GET, handleApi);
//  server.on("/api", HTTP_POST, handleApiPost);
//OTA Upload ESP32
  server.on("/ota", HTTP_GET, handleOta);
  server.on("/update", HTTP_POST, handleUpdate, handleProgress);
  server.onNotFound(handleNotFound);
  server.begin();
}

void handleIndex() // send index.html Web-Page
{
  reset_timer_1();
  Serial.println(F("index.html")); 
  const String html2 ="<H1 style=\"text-align:center;\"><span style=\"color:#A4A4A4\">Io</span><span style='color:#EF7F30'>TDI</span><span style=\"color:#A4A4A4\">splay</span></H1><div style=\"color:#A4A4A4; font-weight: bold; text-align: center;\"><span>HW Version: <b>";
  const String html3 ="</b><br><br></span> <span>SW Version: <b>";
  const String html4 ="</b><br><br><a href=\"ota\">Software Update</a><b>";
  const String html6 ="</b><br><br></span> <span>STA_IP: <b>"; 
  const String html7 ="</b><br><br></span> <span>MAC: <b>";
  const String html8 ="</b><br><br></span> </div> </fieldset> </body> </html>";
  String html = html1 + html2 + HW_Version + html3 + SW_Version + html4 + html6 +WiFi.localIP().toString() + html7 + WiFi.macAddress() + html8;
  server.send(200, "text/html", html); 
}

void handleCss() // send css Web-Page
{
  reset_timer_1();
  Serial.println(F("style.css"));
    const char html[] = "ul {list-style-type:none;padding:0;position:static;}"
      "li {display:inline-block;float:left;margin-bottom:1px;}"
      "li a {display:block;min-width:270px;height:50px;text-align:center;line-height:50px;font-family:\"Arial\",sans-serif;color:#fff;background:#646464;text-decoration:none;font-size:16px;}"
      "li:hover a {background:#EF7F30;color:#fff;}"
      "li ul {display:none;}"
      "li ul li {display:block;float:none;}"
      "li ul li a {width:auto;padding:0 220px;}"
      "ul li a:hover + .hidden, .hidden:hover {display:block;}"
      ".show-menu {font-family:\"Arial\",sans-serif;text-decoration:none;color:#fff;background:#848484;text-align:center;padding:10px 0;display:none;}"
      "input[type=checkbox]{display:none;}"
      "input[type=checkbox]:checked ~ #menu{display:block;}"
      "@media screen and (max-width:1098px){ul {position:static;display:none;}li {margin-bottom:1px;}ul li, li a {width:100%;}.show-menu {display:block;}}"
      "fieldset {font-family:\"Arial\",sans-serif;width:auto;min-height:150px;max-width:1043px;border:5px solid #EF7F30;}"
      "button {background-color:#646464;color:#fff;font-family:\"Arial\",sans-serif;font-size:16px;text-decoration:none;border:none;width:30%;height:50px;display:block;margin-left:auto;margin-right:auto;}"
      "button:hover {border:none;background:#EF7F30;color:#fff;}"
      "#file-input, input{font-family:\"Arial\",sans-serif;width:99%;}"
      "#file-input {text-align:center;border:1px solid #000;display:block;cursor:pointer;}"
      "#prgbar {border:0px solid #fff;position:relative;padding:0px;left:35%;height:10px;background-color:#646464;border-radius:10px;width:30%;}"
      "#bar {background-color:#58db34;border-radius:10px;width:0%;height:10px;}"
      "#percent {position:absolute;left:50%;}"
      "#prg {text-align:center;}";
  server.send(200, "text/css", html);    
}    

void handleWifi() // send wifi.html Web-Page
{
  reset_timer_1();
  Serial.println(F("wifi.html"));
    const String html2 ="<form action=\"index.html\" method=\"POST\"> <br><label>SSID:<input type=\"text\" maxlength=\"32\" name=\"ssid\" value=\"";    
    const String html3 ="\"><br><br></label> <label>Password:<input type=\"password\" maxlength=\"32\" name=\"password\" value=\"";    
    const String html4 ="\"><br><br><br></label><p style=\"text-align:center;\"><button type=\"submit\">Save</button></p><br><br></form> </fieldset> </body> </html>";
    String html = html1 + html2 + cfg.ssid + html3 + cfg.password + html4;    
    server.send(200, "text/html", html);
}

void handleConfig() // send wifi.html Web-Page
{
  reset_timer_1();
  Serial.println(F("config.html"));
    const String html2 ="<form action=\"index.html\" method=\"POST\"> <br><label>NTP_Server:<input type=\"text\" maxlength=\"64\" name=\"ntpserver\" value=\"";    
    const String html3 ="\"><br><br></label> <label>TZ_Info:<input type=\"text\" maxlength=\"64\" name=\"tzinfo\" value=\"";
    //const String html4 ="\"><br><br></label> <label>MQTT_ClientId:<input type=\"text\" maxlength=\"32\" name=\"mqtt_clientId\" value=\"";
    //const String html5 ="\"><br><br></label> <label>MQTT_Server:<input type=\"text\" maxlength=\"32\" name=\"mqtt_server\" value=\"";
    //const String html6 ="\"><br><br></label> <label>MQTT_User:<input type=\"text\" maxlength=\"32\" name=\"mqtt_user\" value=\"";
    //const String html7 ="\"><br><br></label> <label>MQTT_Password:<input type=\"password\" maxlength=\"32\" name=\"mqtt_password\" value=\"";
    const String html8 ="\"><br><br></label> <label>Holiday:<input type=\"text\" maxlength=\"513\" name=\"holiday\" value=\"";
    const String html9 ="\"><br><br></label> <label>Birthday:<input type=\"text\" maxlength=\"513\" name=\"birthday\" value=\"";
    const String html10 ="\"><br><br></label> <label>Timer_Mode:<input type=\"text\" maxlength=\"1\" name=\"timerm\" value=\"";
    const String html11 ="\"><br><br></label> <label>Interval:<input type=\"text\" maxlength=\"4\" name=\"interval\" value=\"";
    const String html12 ="\"><br><br><br></label><p style=\"text-align:center;\"><button type=\"submit\">Save</button></p><br><br></form> </fieldset> </body> </html>";
    //String html = html1 + html2 + cfg.ntpserver + html3 + cfg.tzinfo + html4 + cfg.mqtt_clientId + html5 + cfg.mqtt_server + html6 + cfg.mqtt_user + html7 + cfg.mqtt_password + html8 + cfg.holiday + html9 + cfg.birthday + html10 + cfg.timerm + html11 + cfg.interval + html12;    
    String html = html1 + html2 + cfg.ntpserver + html3 + cfg.tzinfo + html8 + cfg.holiday + html9 + cfg.birthday + html10 + cfg.timerm + html11 + cfg.interval + html12;    
    server.send(200, "text/html", html);
}

void handleIndexPost() // evaluate POST message and send index.html Web-Page
{
   reset_timer_1();
   Serial.println(F("INDEX POST"));
   ApiSave();
   handleIndex();
}

void ApiSave()
{
  reset_timer_1();
  boolean save_cfg = false;
  for (uint8_t i = 0; i < server.args(); i++) 
  {
    if (server.argName(i) == "ssid")
    {
      strcpy(cfg.ssid, server.arg(i).c_str());
      save_cfg = true;
    }   
    if (server.argName(i) == "password")
    {
      strcpy(cfg.password, server.arg(i).c_str());
      save_cfg = true;
    }
    if (server.argName(i) == "ntpserver")
    {
      strcpy(cfg.ntpserver, server.arg(i).c_str());
      save_cfg = true;
    }
    if (server.argName(i) == "tzinfo")
    {
      strcpy(cfg.tzinfo, server.arg(i).c_str());
      save_cfg = true;
    }        
    if (server.argName(i) == "mqtt_clientId")
    {
      strcpy(cfg.mqtt_clientId, server.arg(i).c_str());
      save_cfg = true;
    }
    if (server.argName(i) == "mqtt_server")
    {
      strcpy(cfg.mqtt_server, server.arg(i).c_str());
      save_cfg = true;
    }
    if (server.argName(i) == "mqtt_user")
    {
      strcpy(cfg.mqtt_user, server.arg(i).c_str());
      save_cfg = true;
    }
    if (server.argName(i) == "mqtt_password")
    {
      strcpy(cfg.mqtt_password, server.arg(i).c_str());
      save_cfg = true;
    }
    if (server.argName(i) == "holiday")
    {
      strcpy(cfg.holiday, server.arg(i).c_str());
      save_cfg = true;
    }
    if (server.argName(i) == "birthday")
    {
      strcpy(cfg.birthday, server.arg(i).c_str());
      save_cfg = true;
    }
    if (server.argName(i) == "timerm")
    {
      strcpy(cfg.timerm, server.arg(i).c_str());
      save_cfg = true;
    }
    if (server.argName(i) == "interval")
    {
      strcpy(cfg.interval, server.arg(i).c_str());
      save_cfg = true;
    }
        
            
    if (server.argName(i) == "action" && server.arg(i) == "reboot") 
    {            
      Serial.println("Reboot...");
      esp_sleep_enable_timer_wakeup(3 * 1E6);     // Sekunden in Deep Sleep
      esp_deep_sleep_start();                     // Starte Deep Sleep 
    }

    Serial.print(F(" "));
    Serial.print(server.argName(i));
    Serial.print(F(": "));
    Serial.println(server.arg(i));    
  }
  if (save_cfg == true)
  {
    saveConfig();   
  }  
}  

void handleReboot() // send shutdown.html Web-Page
{
  reset_timer_1();
  Serial.println(F("shutdown.html"));
  const String html2 ="<form action=\"index.html\" method=\"POST\"><br><br><br><p style=\"text-align:center;\"><button name=\"action\" value=\"reboot\" type=\"submit\">Reboot</button></p><br><br></form></fieldset></body></html>";
  String html = html1 + html2;
  server.send(200, "text/html", html);
}

void handleOta()
{
  reset_timer_1();
  Serial.println(F("ota"));
  const String html2 ="<br><br>"
                        "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
                        "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"                        
                        "<input type='file' name='update' id='file' onchange='sub(this)' style=display:none>"
                        "<label id='file-input' for='file'>   Choose file...</label>"
                        "<br><br>"
                        "<button type='submit' class='button'>Update</button>" 
                        "<br><br>"
                        "<div id='prg'></div>"
                        "<br><div id='prgbar'><div id='bar'></div></div><br></form>"
                        "<script>"
                        "function sub(obj){"
                        "var fileName = obj.value.split('\\\\');"
                        "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
                        "};"
                        "$('form').submit(function(e){"
                        "e.preventDefault();"
                        "var form = $('#upload_form')[0];"
                        "var data = new FormData(form);"
                        "$.ajax({"
                        "url: '/update',"
                        "type: 'POST',"
                        "data: data,"
                        "contentType: false,"
                        "processData:false,"
                        "xhr: function() {"
                        "var xhr = new window.XMLHttpRequest();"
                        "xhr.upload.addEventListener('progress', function(evt) {"
                        "if (evt.lengthComputable) {"
                        "var per = evt.loaded / evt.total;"
                        "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
                        "$('#bar').css('width',Math.round(per*100) + '%');"
                        "}"
                        "}, false);"
                        "return xhr;"
                        "},"
                        "success:function(d, s) {"
                        "console.log('success!') "
                        "},"
                        "error: function (a, b, c) {"
                        "}"
                        "});"
                        "});"
                        "</script>"
                        "</fieldset>"
                        "</body>"
                        "</html>";
                        
  String html = html1 + html2;
  server.send(200, "text/html", html);    
}

void handleUpdate()
{
  reset_timer_1();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
  //ESP.restart();
  esp_sleep_enable_timer_wakeup(3 * 1E6);     // Sekunden in Deep Sleep
  esp_deep_sleep_start();                     // Starte Deep Sleep            
}

void handleProgress()
{
  reset_timer_1();
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) 
  {
    Serial.printf("Update: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) 
    { //start with max available size
      Update.printError(Serial);
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) 
  {
    /* flashing firmware to ESP*/
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) 
    {
      Update.printError(Serial);
    }
  } 
  else if (upload.status == UPLOAD_FILE_END) 
  {
    if (Update.end(true)) 
    { //true to set the size to the current progress
       Serial.printf("Update Success: %u\nReboot...\n", upload.totalSize);
    } 
    else 
    {
      Update.printError(Serial);
    }
  }    
}  

void handleNotFound() // send page not found
{
  reset_timer_1();
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++) 
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void reset_timer_1()
{
  if (sleep_mode == 1)
  {
    next_millis_1 = millis() + 60000;
  }  
}
