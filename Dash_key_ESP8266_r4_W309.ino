/*
 * Dash key
 * Using ESP8266
 * By Henry Chan Feb2022
 * Send email by IFTTT
 * Send MQTT messages via Hivemq.com
 * Then power off and sleep 
 * R2 changes:
 * - Light up RED as the confirmation of task finish
 * - Correct AD value for low battery
 * - WiFi retrial time changed from 60s to 30s
 * - HW can sustain over RESET cycle
 * R3 changes:
 * - Add RTC memory to count the bootcount
 * - if reset too many times, give error action
 * - error actions: fast flashing RED LED and then die
 * - if MQTT server cannot reconnect for 5 times, give error action
 * - IFTTT will do only after MQTT server is connected
 * - This is to avoid sending emails before MQTT connection is successful
 * 
 * R4 changes:
 * - IFTTT before MQTT connection
 * - MQTT connection with delay for publishing to ensure it is finished
 * - Remove all subscriptions
 */
 
 
#include <ESP8266WiFi.h>
#include <PubSubClient.h>       // MQTT server library
#include <ArduinoJson.h>        // JSON library
#include <RTCMemory.h>       //RTC memory

#define LED1 D5 //Green LED
#define LED2 D6 //Red LED
#define POWER D0

// Define a struct that maps what's inside the RTC memory
// Max size is 508 bytes.
typedef struct {
  byte bootcount;
} MyData; 

// Replace with your SSID and Password
//const char *ssid = "EIA-W311MESH";      // Your SSID             
//const char *password = "42004200";  // Your Wifi password
const char *ssid = "icw308";      // Your SSID             
const char *password = "icw3081a";  // Your Wifi password

//MQTT setting
const char *mqtt_server = "ia.ic.polyu.edu.hk"; // MQTT server name
//const char *mqtt_server = "broker.hivemq.com"; // MQTT server name
char *mqttTopic = "IC/request";

WiFiClient espClient;
PubSubClient client(espClient);
RTCMemory<MyData> rtcMemory;
byte reconnect_count = 0;
byte led = LED1;

// Replace with your unique IFTTT URL resource
const char* resource = "/trigger/W309_button/json/with/key/b2vLX-drrllYvioMpp_iWP";

// Maker Webhooks IFTTT
const char* server = "maker.ifttt.com";

long currentTime = 0;
String ipAddress;
String macAddr;
char msg[100];
StaticJsonDocument<50> Jsondata; // Create a JSON document of 100 characters max

void setup() {
  if (rtcMemory.begin()) {
    Serial.println("Initialization done!");
  } else {
    Serial.println("No previous data found. The memory is reset to zeros!");
  }
  
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(POWER, OUTPUT);
  digitalWrite(POWER, HIGH);
  digitalWrite(LED1, HIGH);
  digitalWrite(LED2, HIGH);
  Serial.begin(115200); 

  MyData *data = rtcMemory.getData();
  data->bootcount++;
  Serial.println(data->bootcount);
  rtcMemory.save(); //Save into RTC
  if (data->bootcount > 3){
    data->bootcount = 0; //reset bootcount
    rtcMemory.save(); //Save into RTC
    error_action(); //Give error LED and die
  }
  
  // Battery level check
  if (analogRead(A0)<574) led = LED2; //LED changed to red if VBAT < 3.7V
  else {
    led = LED1;
  }
  delay(1000);
  
  setup_wifi();

  //client.loop();
  
  //send mail
  makeIFTTTRequest();

  //Set MQTT server
  client.setServer(mqtt_server, 1883);  

  if (!client.connected()){
    reconnect();
  }
  //publish MQTT messages
  //Initalize Json message
  Jsondata["Request"] = "A4 Paper";
  Jsondata["Room"] = "W309"; 

  // Packing the JSON message into msg
  serializeJson(Jsondata, Serial);
  serializeJson(Jsondata, msg);
      
  //Publish msg to MQTT server
  client.publish(mqttTopic, msg);
  delay(3000); //delay for MQTT message completion
  
  //Delay for a while for MQTT message publishing before power off
  //Slow flash RED/GRN LED to indicate finish
  digitalWrite(led,HIGH); //turn off all LED
  for (int i = 0; i<3; i++){
    digitalWrite(LED1, HIGH); //turn on RED LED
    digitalWrite(LED2, LOW); 
    delay(500);
    digitalWrite(LED1, LOW); //turn on GRN LED
    digitalWrite(LED2, HIGH); 
    delay(500);
  }

  //Power off
  digitalWrite(POWER, LOW);

  // Deep sleep mode until RESET pin is connected to a LOW signal (pushbutton is pressed)
  ESP.deepSleep(0);  
}

void loop() {
  // sleeping so wont get here
}

// Establish a Wi-Fi connection with your router
void setup_wifi() {
  WiFi.disconnect();
  delay(100);
  // We start by connecting to a WiFi network
  Serial.printf("\nConnecting to %s\n", ssid);
  WiFi.begin(ssid, password); // start the Wifi connection with defined SSID and PW

  // Indicate "......" during connecting and flashing LED1
  // Restart if WiFi cannot be connected for 30sec
  currentTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(led,digitalRead(led)^1);
    if (millis()-currentTime > 30000){      //Error if wifi connect retrial is more than 3
      ESP.restart(); //restart if bootcount is <=3
    }
  }
  // Show "WiFi connected" once linked and light up LED1 or LED2
  Serial.printf("\nWiFi connected\n");
  digitalWrite(led,LOW); //turn on LED
  delay(1000);
  
  // Show IP address and MAC address
  ipAddress=WiFi.localIP().toString();
  Serial.printf("\nIP address: %s\n", ipAddress.c_str());
  macAddr=WiFi.macAddress();
  Serial.printf("MAC address: %s\n", macAddr.c_str());
}

// Make an HTTP request to the IFTTT web service
void makeIFTTTRequest() {
  Serial.print("Connecting to "); 
  Serial.print(server);
  
  int retries = 5;
  while(!!!espClient.connect(server, 80) && (retries-- > 0)) {
    Serial.print(".");
  }
  Serial.println();
  if(!!!espClient.connected()) {
     Serial.println("Failed to connect, going back to sleep");
  }
  
  Serial.print("Request resource: "); 
  Serial.println(resource);
  espClient.print(String("GET ") + resource + 
                  " HTTP/1.1\r\n" +
                  "Host: " + server + "\r\n" + 
                  "Connection: close\r\n\r\n");
                  
  int timeout = 5 * 10; // 5 seconds             
  while(!!!espClient.available() && (timeout-- > 0)){
    delay(100);
  }
  if(!!!espClient.available()) {
     Serial.println("No response, going back to sleep");
  }
  while(espClient.available()){
    Serial.write(espClient.read());
  }
  
  Serial.println("\nclosing connection");
  espClient.stop();
}

// Reconnect mechanism for MQTT Server
void reconnect() {
  
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.printf("Attempting MQTT connection...");
    // Attempt to connect
    //if (client.connect("ESP32Client")) {
    if (client.connect(macAddr.c_str())) {
      Serial.println("Connected");
      // Once connected, publish an announcement...
      snprintf(msg, 75, "IoT System (%s) is READY", ipAddress.c_str());
      //client.subscribe(mqttTopic);
      delay(1000);
      client.publish(mqttTopic, msg);
      reconnect_count = 0;
    } 
    else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      reconnect_count++;
      //Reconnect wifi by restart if retrial up to 5 times
      if (reconnect_count == 5){
      //ESP.restart(); // Reset if not connected to server 
      error_action();    
      }   
      // Wait 2 seconds before retrying
      delay(2000);
    }
  }
}

void error_action() {
  //Fast flash red LED to indicate error
  digitalWrite(led,HIGH); //turn off all LED
  for (int i = 0; i<8; i++){
    digitalWrite(LED2, LOW); //turn on RED LED
    delay(200);
    digitalWrite(LED2, HIGH); //turn on RED LED
    delay(200);
    }
  //Power off
  digitalWrite(POWER, LOW);
  // Deep sleep mode until RESET pin is connected to a LOW signal (pushbutton is pressed)
  ESP.deepSleep(0);  
}
