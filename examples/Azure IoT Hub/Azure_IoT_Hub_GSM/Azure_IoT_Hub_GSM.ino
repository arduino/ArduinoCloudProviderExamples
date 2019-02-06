/*
  Azure IoT Hub GSM

  This sketch securely connects to an Azure IoT Hub using MQTT over GSM/3G.
  It uses a private key stored in the ATECC508A and a self signed public
  certificate for SSL/TLS authetication.

  It publishes a message every 5 seconds to "devices/{deviceId}/messages/events/" topic
  and subscribes to messages on the "devices/{deviceId}/messages/devicebound/#"
  topic.

  The circuit:
  - MKR GSM 1400 board
  - Antenna
  - SIM card with a data plan
  - LiPo battery

  This example code is in the public domain.
*/

#include <ArduinoBearSSL.h>
#include <ArduinoECCX08.h>
#include <utility/ECCX08SelfSignedCert.h>
#include <ArduinoMqttClient.h>
#include <MKRGSM.h>

#include "arduino_secrets.h"

/////// Enter your sensitive data in arduino_secrets.h
const char pinnumber[]     = SECRET_PINNUMBER;
const char gprs_apn[]      = SECRET_GPRS_APN;
const char gprs_login[]    = SECRET_GPRS_LOGIN;
const char gprs_password[] = SECRET_GPRS_PASSWORD;
const char broker[]        = SECRET_BROKER;
String     deviceId        = SECRET_DEVICE_ID;

GSM gsmAccess;
GPRS gprs;

GSMClient     gsmClient;            // Used for the TCP socket connection
BearSSLClient sslClient(gsmClient); // Used for SSL/TLS connection, integrates with ECC508
MqttClient    mqttClient(sslClient);

unsigned long lastMillis = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial);

  if (!ECCX08.begin()) {
    Serial.println("No ECCX08 present!");
    while (1);
  }

  // reconstruct the self signed cert
  ECCX08SelfSignedCert.beginReconstruction(0, 8);
  ECCX08SelfSignedCert.setCommonName(ECCX08.serialNumber());
  ECCX08SelfSignedCert.endReconstruction();

  // Set a callback to get the current time
  // used to validate the servers certificate
  ArduinoBearSSL.onGetTime(getTime);

  // Set the ECCX08 slot to use for the private key
  // and the accompanying public certificate for it
  sslClient.setEccSlot(0, ECCX08SelfSignedCert.bytes(), ECCX08SelfSignedCert.length());

  // Set the client id used for MQTT as the device id
  mqttClient.setId(deviceId);

  // Set the username to "<broker>/<device id>/api-version=2018-06-30" and empty password
  String username;

  username += broker;
  username += "/";
  username += deviceId;
  username += "/api-version=2018-06-30";

  mqttClient.setUsernamePassword(username, "");

  // Set the message callback, this function is
  // called when the MQTTClient receives a message
  mqttClient.onMessage(onMessageReceived);
}

void loop() {
  if (gsmAccess.status() != GSM_READY || gprs.status() != GPRS_READY) {
    connectGSM();
  }

  if (!mqttClient.connected()) {
    // MQTT client is disconnected, connect
    connectMQTT();
  }

  // poll for new MQTT messages and send keep alives
  mqttClient.poll();

  // publish a message roughly every 5 seconds.
  if (millis() - lastMillis > 5000) {
    lastMillis = millis();

    publishMessage();
  }
}

unsigned long getTime() {
  // get the current time from the cellular module
  return gsmAccess.getTime();
}

void connectGSM() {
  Serial.println("Attempting to connect to the cellular network");

  while ((gsmAccess.begin(pinnumber) != GSM_READY) ||
         (gprs.attachGPRS(gprs_apn, gprs_login, gprs_password) != GPRS_READY)) {
    // failed, retry
    Serial.print(".");
    delay(1000);
  }

  Serial.println("You're connected to the cellular network");
  Serial.println();
}

void connectMQTT() {
  Serial.print("Attempting to MQTT broker: ");
  Serial.print(broker);
  Serial.println(" ");

  while (!mqttClient.connect(broker, 8883)) {
    // failed, retry
    Serial.print(".");
    Serial.println(mqttClient.connectError());
    delay(5000);
  }
  Serial.println();

  Serial.println("You're connected to the MQTT broker");
  Serial.println();

  // subscribe to a topic
  mqttClient.subscribe("devices/" + deviceId + "/messages/devicebound/#");
}

void publishMessage() {
  Serial.println("Publishing message");

  // send message, the Print interface can be used to set the message contents
  mqttClient.beginMessage("devices/" + deviceId + "/messages/events/");
  mqttClient.print("hello ");
  mqttClient.print(millis());
  mqttClient.endMessage();
}

void onMessageReceived(int messageSize) {
  // we received a message, print out the topic and contents
  Serial.print("Received a message with topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', length ");
  Serial.print(messageSize);
  Serial.println(" bytes:");

  // use the Stream interface to print the contents
  while (mqttClient.available()) {
    Serial.print((char)mqttClient.read());
  }
  Serial.println();

  Serial.println();
}
