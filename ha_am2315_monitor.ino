#include <SimpleTimer.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_AM2315.h>

#define MQTT_CLIENT_ID_PREFIX           "SNU_PCB-NINANO-"
#define HA_TOPIC_TEMP_HUM               "/snu_pcb/ninano/temp_hum"
#define HA_TOPIC_CONTROL                "/snu_pcb/ninano/control"
#define HA_TOPIC_ALARM                  "/snu_pcb/ninano/alarm"


#define DEFAULT_AM2315_READ_INTERVAL    30000  // 30 seconds
#define DEFAULT_MQTT_BROKER             "broker.hivemq.com"
#define CONF_READ_TIME_INTERVAL         "ConfReadTimeInterval"
#define CONF_FILENAME                   "/config.cfg"

/* Macros for logging */
#define DEBUG_SERIAL                Serial
#define DEBUG                       DEBUG_SERIAL.printf
#define LOG_DBG(x,...)              DEBUG("[DBG](%d) " x, __LINE__, ##__VA_ARGS__)
#define LOG_INFO(x,...)             DEBUG("[INFO](%d) " x, __LINE__, ##__VA_ARGS__)
#define LOG_WARN(x,...)             DEBUG("[WARN](%d) " x, __LINE__, ##__VA_ARGS__)
#define LOG_ERR(x,...)              DEBUG("[ERR](%d) " x, __LINE__, ##__VA_ARGS__)
#define LOG_NO_HEADER(x,...)        DEBUG(x, ##__VA_ARGS__)

/********************************* Global variables *********************************/
/* WiFi Client instance */
WiFiClient espClient;

/* MQTT Client instance */
PubSubClient client(espClient);

/* AM2315 instance */
Adafruit_AM2315 am2315;

/* Timer stuff */
SimpleTimer timer;
int AM2315ReadTimerID = -1;
void AM2315ReadTimerCallback() ;

/* Configuration stuff */
uint32_t ConfReadTimeInterval = DEFAULT_AM2315_READ_INTERVAL;
String ConfDeviceName;
String ConfMQTTBroker = DEFAULT_MQTT_BROKER;
String ConfWiFiSSID;
String ConfWiFiAUTH;
/************************************************************************************/

int setup_wifi(int reconnect) 
{
  int count = 0;
  
  delay(10);

  while (1)
  {
    // We start by connecting to a WiFi network
    LOG_INFO("Connecting to %s", ConfWiFiSSID.c_str());

    if (!reconnect && ConfWiFiSSID.length() > 0)
      WiFi.begin(ConfWiFiSSID.c_str(), ConfWiFiAUTH.c_str());
    else
      WiFi.reconnect();

    count = 0;
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      LOG_NO_HEADER(".");
      if (++count == 10) break;
    }
    if (count < 10)
    {
       break;
    }
    else
    {
      LOG_ERR("fail to connect AP\n");
    }

    
    LOG_INFO("Start SmartConfig ... \n");
    WiFi.beginSmartConfig();

    count = 0;
    while (!WiFi.smartConfigDone()) 
    {
      delay(500);
      LOG_NO_HEADER(".");
      if (++count == 40) {
        LOG_NO_HEADER("Stop\n");
        WiFi.stopSmartConfig();
        break;
      }
    }
    
  }
    
  LOG_NO_HEADER("\n");
  LOG_INFO("WiFi connected(%d)\n", WiFi.status());
  LOG_INFO("IP address: %s\n", WiFi.localIP().toString().c_str());

  return 1;
}

void callback(char* topic, byte* payload, unsigned int length) 
{
  LOG_INFO("Message arrived [%s] ", topic);
  for (int i = 0; i < length; i++) {
    LOG_NO_HEADER("%c", (char)payload[i]);
  }
  LOG_NO_HEADER("\n");
}

int LoadConfigFile(void)
{
  File f;
  String cfg_str;
  String comp_str = "=";
    
  SPIFFS.begin();

  LOG_NO_HEADER("\n");
  LOG_INFO("Loading configuration...\n");
  
  f = SPIFFS.open(CONF_FILENAME, "r");
  if (!f)
  {
    LOG_ERR("file open failed\n");
    return 0;
  }
  else
  {
    LOG_INFO("Success to open configuration file\n");
  }

  LOG_NO_HEADER("==================== Config ====================\n");
  cfg_str = f.readStringUntil('\0');
  LOG_NO_HEADER("%s", cfg_str.c_str());
  LOG_NO_HEADER("================================================\n\n");
  
  LOG_INFO("Parsing configuration file...\n");
  f.seek(0, SeekSet);
  while (f.available())
  {
    String token, value;
    const String conf_devicename = "devicename";
    const String conf_intervaltime = "intervaltime";
    const String conf_broker = "mqtt-broker";
    const String conf_wifi_ssid = "wifi-ssid";
    const String conf_wifi_auth = "wifi-auth";
    int pos;

    // parsing line one by one
    cfg_str = f.readStringUntil('\n');
    LOG_DBG("\tparsing line [%s]\n", cfg_str.c_str());
    pos = cfg_str.indexOf(comp_str);
    token = cfg_str.substring(0, pos);
    LOG_DBG("\t\ttoken [%s]\n", token.c_str());
    value = cfg_str.substring(pos+1);
    LOG_DBG("\t\tvalue [%s]\n", value.c_str());

    if (token == conf_devicename)
    {
      ConfDeviceName = value;
      LOG_DBG("\t\tSet value to [%s]\n", ConfDeviceName.c_str());
    }
    else if (token == conf_intervaltime)
    {
      ConfReadTimeInterval = value.toInt();
      LOG_DBG("\t\tSet value to %d\n", ConfReadTimeInterval);
    }
    else if (token == conf_broker)
    {
      ConfMQTTBroker = value;
      LOG_DBG("\t\tSet value to [%s]\n", ConfMQTTBroker.c_str());
    }
    else if (token == conf_wifi_ssid)
    {
      ConfWiFiSSID = value;
      LOG_DBG("\t\tSet value to [%s]\n", ConfWiFiSSID.c_str());
    }
    else if (token == conf_wifi_auth)
    {
      ConfWiFiAUTH = value;
      LOG_DBG("\t\tSet value to [%s]\n", ConfWiFiAUTH.c_str());
    }
    else
    {
      LOG_WARN("Unknown config [%s]\n", token.c_str());
    }
  }

  f.close();
  
  LOG_INFO("Complete to parsing configuration file ...\n\n");
  
  return 1;
}

void setup() {
  int ret;
  
  DEBUG_SERIAL.begin(115200);

  LoadConfigFile();
  
  ret = setup_wifi(0);

  client.setServer(ConfMQTTBroker.c_str(), 1883);
  client.setCallback(callback);

  if (!am2315.begin()) {
     LOG_ERR("Sensor not found, check wiring & pullups!\n");
     while (1);
  }
  else
  {
    LOG_INFO("Success to initialize AM2315 instance\n");
  }
  
  AM2315ReadTimerID = timer.setInterval(ConfReadTimeInterval, AM2315ReadTimerCallback);
  LOG_DBG("TimerID: %d\n", AM2315ReadTimerID);
}

void reconnect() 
{
  String topic_str = HA_TOPIC_CONTROL;
  String clientid_str = MQTT_CLIENT_ID_PREFIX + ConfDeviceName;

  // Loop until we're reconnected
  while (!client.connected()) {
    LOG_INFO("Attempting MQTT connection with ClientID [%s] ... ", clientid_str.c_str());
    // Attempt to connect
    if (client.connect(clientid_str.c_str(), "hjung200x", "123456")) {
      LOG_NO_HEADER("connected\n");
      
      // ... and resubscribe
      //topic_str.concat(ConfDeviceName);
      topic_str += ("/" + ConfDeviceName);
      LOG_INFO("Subscribe topic [%s] to broker ...\n", topic_str.c_str());
      client.subscribe(topic_str.c_str());
    } else {
      LOG_NO_HEADER("failed, rc=%d\n", client.state());
      LOG_ERR(" try again in 5 seconds\n");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void loop() {
  static uint32_t timerLast = 0;
  int count = 0;

  while (WiFi.status() != WL_CONNECTED)
  {
    LOG_WARN("WiFi diconnected!!\n");
    delay(500);
    LOG_NO_HEADER(".");
    if (++count == 4) break;
  }
  
  if (count == 4)
  {
    if (AM2315ReadTimerID != -1)
    {
      timer.deleteTimer(AM2315ReadTimerID);
      AM2315ReadTimerID = -1;
    }
    
    setup_wifi(1);
  }
  
  if (!client.connected()) 
  {
    LOG_WARN("No connection with MQTT broker!!\n");

    /*
    if (AM2315ReadTimerID != -1)
    {
      timer.deleteTimer(AM2315ReadTimerID);
      AM2315ReadTimerID = -1;
    }
    */
    
    reconnect();
  }

  if (AM2315ReadTimerID == -1)
  {
    AM2315ReadTimerID = timer.setInterval(ConfReadTimeInterval, AM2315ReadTimerCallback);
  }

  client.loop();

  timer.run();
}

void AM2315ReadTimerCallback() 
{
  float temp, hum;
  char clientMsg[64] = {0, };
  int ret;

  temp = am2315.readTemperature();
  //LOG_DBG("Temp: "); LOG_NO_HEADER(temp);
  delay(500);
  hum = am2315.readHumidity();
  //LOG_DBGt("Hum: "); LOG_NO_HEADER(hum);

  sprintf(clientMsg,  
          "{"\
            "\"device\": \"%s\", "\
            "\"temp\": %3.2f, "\
            "\"hum\": %3.2f"\
          "}", 
          ConfDeviceName.c_str(), temp, hum);

  client.publish(HA_TOPIC_TEMP_HUM, clientMsg, 0);

  LOG_DBG("Publish topic [%s] to MQTT broker ...\n", HA_TOPIC_TEMP_HUM);
  LOG_DBG("\t%s\n", clientMsg);

  LOG_DBG("Sleep %d seconds ...\n", ConfReadTimeInterval / 1000);
}
