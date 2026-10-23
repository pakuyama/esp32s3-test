// Credentials - copy to secrets.h and fill in
#pragma once
#define WIFI_SSID "YOUR-WIFI-SSID"
#define WIFI_PSWD "YOUR-WIFI-PASSWORD"

#define SUPABASE_URL "https://YOUR-PROJECT.supabase.co"
#define SUPABASE_KEY "YOUR-PUBLISHABLE-KEY"
#define DEVICE_ID    "xiao-s3"

// MJPEG relay on the PC (relay/relay.mjs); public IP of the PC, inbound TCP 8090 allowed in the firewall
#define RELAY_URL    "http://YOUR-PC-PUBLIC-IP:8090/push"
#define RELAY_TOKEN  "SAME-VALUE-AS-relay/token.txt"
