#ifndef k42_display_h
#define k42_display_h

#include <WString.h>

// Types 'byte' und 'word' doesn't work!
typedef struct {
  int valid;                        // 0=no configuration, 2=valid configuration
  char ssid[33];                    // SSID of WiFi
  char password[33];                // Password of WiFi
  char ntpserver[65];               // NTP Server
  char tzinfo[65];                  // TZ Info
  char mqtt_clientId[33];
  char mqtt_server[33];
  char mqtt_user[33];
  char mqtt_password[33];
  char holiday[513];                // Feiertage
  char birthday[1025];              // Geburtstage
  char timerm[2];                   // Timer Mode 0=allways ON 1= Sleep Mode
  char interval[5];                 // Intervall in Min.
} configData_t;                      

#endif
