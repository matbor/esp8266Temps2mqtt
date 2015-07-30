/* 
 by Matthew Bordignon, @bordignon on twitter.

 Using a ESP8266 with onewire temperature sensors and a couple of LED's for status.

 Publishes the current temperature's to mqtt broker and topic

 The board I am using; https://learn.adafruit.com/downloads/pdf/adafruit-huzzah-esp8266-breakout.pdf

*/

#include <OneWire.h> // using Arduino 1.65 built-in version
#include <PubSubClient.h> // https://github.com/Imroy/pubsubclient
#include <ESP8266WiFi.h> // http://arduino.esp8266.com/staging/package_esp8266com_index.json <- package manager or use Aradfruit one.

// SETTINGS
//
 // wifi network
const char* ssid = "foobarnetwork";     // wifi SSID
const char* pass = "ABCD1234";  // wifi network password

 // MQTT
IPAddress server(10,0,8,10);    // MQTT Broker IP, port defaults to 1883
 // TOPIC to publish to;
 //    This adds the Onewire ROM ID to where the XXXXXXXXXXXXXXXX is. 
 //    If you change the topic you need to change the 'BackslashStart' variable as well.
 //       eg if your topic was "/test1/hardware/arduino/weather1/XXXXXXXXXXXXXXXX/temperature/current"
 //       then you need to write 33 for the BackslashStart varible.
char charTopic[] = "/house/hardware/arduino/weather2/XXXXXXXXXXXXXXXX/temperature/current";
const int BackslashStart = 33;  // read above, this is important
 // base topic for LWT, it will automatically add mac address to the end as well
String lwtTopic = "/lwt/";      // ie. /lwt/esp8266-18:fe:34:a6:4b:af

 // OneWire Sensor
OneWire  ds[] = {14};           // GPIO pin 14

 // status LED's
const int REDled = 12;          // the GPIO of the RED LED pin
const int GRNled = 13;          // the GPIO of the GREEN LED pin

 // time between each check of the temperatures. NOTE; if enableDeepSleep is true, this won't work
int nextChk = 40;               // ie 30 = 30 seconds
boolean mqttKeepAlive = true;   // keep mqtt connection alive.  Mqtt library will disconnect after 15-seconds typically.
                                // you would normal disable this if your nextChk is time is large.

 // deep sleep, idea: read temperatures every half-hour, so goto sleep.
boolean enableDeepSleep = true;  // enable deep sleep, this will disable the nextChk and mqttKeepAlive
int deepsleepfor = 1800;         // 1800secs = 30mins.. how long to deep sleep for in seconds

//
// END of SETTINGS

void callback(const MQTT::Publish& pub) {
  // handle message arrived
  Serial.println("publish callback");
}

WiFiClient wclient;
PubSubClient client(wclient, server);

String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}


byte No_of_1Wire_Buses = sizeof(ds) / sizeof(ds[0]);  // Number of pins declared for OneWire = number of Buses
byte this_1Wire_Bus = 0;  // OneWire Bus number that is currently processed

char hexChars[] = "0123456789ABCDEF";
#define HEX_MSB(v) hexChars[(v & 0xf0) >> 4]
#define HEX_LSB(v) hexChars[v & 0x0f]
String clientName;

void setup(void) {
  Serial.begin(115200);
  Serial.println("starting...");
  // initialize digital pins for the LEDs.
  pinMode(GRNled, OUTPUT); // LED - GREEN - MQTT Connected
  pinMode(REDled, OUTPUT); // LED - RED - MQTT Not Connected & flashing no wifi
  digitalWrite(REDled, HIGH);   // turn the RED LED ON
  
  delay(10);
  Serial.println();
  Serial.println();

  client.set_callback(callback);
 
  Serial.print("Number of defined 1Wiew buses: ");
  Serial.println(No_of_1Wire_Buses);
  // Generate client name based on MAC address and last 8 bits of microsecond counter
  clientName += "esp8266-";
  uint8_t mac[6];
  WiFi.macAddress(mac);
  clientName += macToStr(mac);
  delay(5000);
  Serial.print("MQTT Broker: ");
  Serial.print(server);
  Serial.print(" / MQTT Client ID: ");
  Serial.println(clientName);

  lwtTopic += clientName; // add the client name to the lwt topic as well
}

void loop(void) {
  if (WiFi.status() != WL_CONNECTED) {
    //blink the RED LED, to show we are in process of connecting to wifi
    digitalWrite(REDled, HIGH);    // turn the LED off by making the voltage LOW
    delay(250);
    digitalWrite(REDled, LOW);   // turn the LED on (HIGH is the voltage level)
        
    Serial.print("Connecting to ");
    Serial.print(ssid);
    Serial.println("...");
    WiFi.begin(ssid, pass);

    if (WiFi.waitForConnectResult() != WL_CONNECTED)
      return;
    Serial.println("WiFi connected");
  }

  if (WiFi.status() == WL_CONNECTED) {
    while (!client.connected()) {
      Serial.println("Connecting to MQTT Broker");
      digitalWrite(GRNled, LOW);   // turn the GREEN LED OFF
      digitalWrite(REDled, HIGH);   // turn the RED LED ON
     
      if (client.connect(MQTT::Connect((char*) clientName.c_str()) // generated client name
                                    .set_will(lwtTopic, "offline", 2, true))) // topic, message, qos, retain
      {
        client.publish(MQTT::Publish(lwtTopic,"online")
                         .set_qos(2)
                         .set_retain(true));
        //client.subscribe("inTopic");
        digitalWrite(REDled, LOW);   // turn the RED LED OFF
        digitalWrite(GRNled, HIGH);   // turn the GREEN LED ON
        Serial.println("MQTT Broker connected");
      }
    }

    if (client.connected())
      client.loop();
  }
  Serial.println("starting onewire search");
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius, fahrenheit;
 
  if ( !ds[this_1Wire_Bus].search(addr)) {
    Serial.print("No more addresses for array element number: ");
    Serial.println(this_1Wire_Bus);
    // If all buses done, start all over again
    if (this_1Wire_Bus >= (No_of_1Wire_Buses - 1)) {
      this_1Wire_Bus = 0;
    } else {
      this_1Wire_Bus++;
    }
    ds[this_1Wire_Bus].reset_search();

    if (enableDeepSleep)
    {
      // deep sleep time
      // delay(2000); // 2 second delay to make sure all messages have been sent
      Serial.print("Having a catnap for: ");
      Serial.print(deepsleepfor);
      Serial.println(" seconds");
      ESP.deepSleep((deepsleepfor * 1000000), WAKE_RF_DEFAULT); // Sleep for x seconds.
    }
    else
    {
      // delay between checking for the next temperature. 
      // loop every 10 seconds so we don't get disconnected from broker.
      Serial.print("Pausing between search's... ");
      int nextChk_noof = int (nextChk / 10); // working out how many times we need to run the loop as we need to call client.loop every 10 seconds
      for (int i=1; i <= nextChk_noof; i++){
        if (client.connected() && mqttKeepAlive) {client.loop(); }
        if (!client.connected()){
          // toggle LED status
          digitalWrite(GRNled, LOW);   // turn the GREEN LED OFF
          digitalWrite(REDled, HIGH);   // turn the RED LED ON
        }
        Serial.print(i); 
        delay(10000);
        }
       Serial.println(""); 
       return;
    }
  }
  
  Serial.print("ROM =");
  for( i = 0; i < 8; i++) {
    Serial.write(' ');
    Serial.print(addr[i], HEX);
  }

  if (OneWire::crc8(addr, 7) != addr[7]) {
      Serial.println("CRC is not valid!");
      return;
  }
  Serial.println();
 
  // the first ROM byte indicates which chip
  switch (addr[0]) {
    case 0x10:
      Serial.println("  Chip = DS18S20");  // or old DS1820
      type_s = 1;
      break;
    case 0x28:
      Serial.println("  Chip = DS18B20");
      type_s = 0;
      break;
    case 0x22:
      Serial.println("  Chip = DS1822");
      type_s = 0;
      break;
    default:
      Serial.println("Device is not a DS18x20 family device.");
      return;
  } 

  ds[this_1Wire_Bus].reset();
  ds[this_1Wire_Bus].select(addr);
  ds[this_1Wire_Bus].write(0x44, 1);        // start conversion, with parasite power on at the end
  
  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.
  
  present = ds[this_1Wire_Bus].reset();
  ds[this_1Wire_Bus].select(addr);    
  ds[this_1Wire_Bus].write(0xBE);         // Read Scratchpad

  Serial.print("  Data = ");
  Serial.print(present, HEX);
  Serial.print(" ");
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds[this_1Wire_Bus].read();
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.print(" CRC=");
  Serial.print(OneWire::crc8(data, 8), HEX);
  Serial.println();

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
    //// default is 12 bit resolution, 750 ms conversion time
  }
  celsius = (float)raw / 16.0;
  fahrenheit = celsius * 1.8 + 32.0;
  Serial.print("  Temperature = ");
  Serial.print(celsius);
  Serial.print(" Celsius, ");
  Serial.print(fahrenheit);
  Serial.println(" Fahrenheit");
//publish the temp now
  for (i = 0; i < 8; i++) {
    charTopic[BackslashStart+i*2] = HEX_MSB(addr[i]); //33 is where the backlash before XXX starts
    charTopic[BackslashStart+1+i*2] = HEX_LSB(addr[i]); //34 is plus one on the above
  }
  char charMsg[10];
  memset(charMsg,'\0',10);
  dtostrf(celsius, 4, 2, charMsg);
  client.publish(MQTT::Publish(charTopic,charMsg)
                .set_qos(2));
  delay(1000); // just adding a small delay between publishing just incase
}
