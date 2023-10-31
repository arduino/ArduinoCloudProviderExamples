#include <ArduinoBearSSL.h>
#include <ArduinoECCX08.h>
#include <ArduinoMqttClient.h>
#include <Arduino_ConnectionHandler.h>
#include <Arduino_JSON.h>
#include <NTPClient.h>
#include <mbed_mktime.h>

#include "arduino_secrets.h"

// Enter your sensitive data in arduino_secrets.h
constexpr char broker[] { SECRET_BROKER };
constexpr unsigned port { SECRET_PORT };
const char* certificate { SECRET_CERTIFICATE };

#include <WiFi.h>
#include <WiFiUdp.h>
// Enter your sensitive data in arduino_secrets.h
constexpr char ssid[] { SECRET_SSID };
constexpr char pass[] { SECRET_PASS };
WiFiConnectionHandler conMan(SECRET_SSID, SECRET_PASS);
WiFiClient tcpClient;
WiFiUDP NTPUdp;

NTPClient timeClient(NTPUdp);
BearSSLClient sslClient(tcpClient);
MqttClient mqttClient(sslClient);

unsigned long lastMillis { 0 };

void setup()
{
    Serial.begin(115200);

    // Wait for Serial Monitor or start after 2.5s
    for (const auto startNow = millis() + 2500; !Serial && millis() < startNow; delay(250));

    // Set the callbacks for connectivity management
    conMan.addCallback(NetworkConnectionEvent::CONNECTED, onNetworkConnect);
    conMan.addCallback(NetworkConnectionEvent::DISCONNECTED, onNetworkDisconnect);
    conMan.addCallback(NetworkConnectionEvent::ERROR, onNetworkError);

    // Check for HSM
    if (!ECCX08.begin()) {
        Serial.println("No ECCX08 present!");
        while (1)
            ;
    }

    // Configure TLS to use HSM and the key/certificate pair
    ArduinoBearSSL.onGetTime(getTime);
    sslClient.setEccSlot(0, certificate);
    // mqttClient.setId("Your Thing ID");
    mqttClient.onMessage(onMessageReceived);

    timeClient.begin();
}

void loop()
{
    // Automatically manage connectivity
    const auto conStatus = conMan.check();

    if (conStatus != NetworkConnectionState::CONNECTED)
        return;

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

void setNtpTime()
{
    timeClient.forceUpdate();
    const auto epoch = timeClient.getEpochTime();
    set_time(epoch);
}

unsigned long getTime()
{
    const auto now = time(NULL);
    return now;
}

void connectMQTT()
{
    Serial.print("Attempting to MQTT broker: ");
    Serial.print(broker);
    Serial.print(":");
    Serial.print(port);
    Serial.println();

    int status;
    while ((status = mqttClient.connect(broker, port)) == 0) {
        // failed, retry
        Serial.println(status);
        delay(1000);
    }
    Serial.println();

    Serial.println("You're connected to the MQTT broker");
    Serial.println();

    // subscribe to a topic with QoS 1
    constexpr char incomingTopic[] { "arduino/incoming" };
    constexpr int incomingQoS { 1 };
    Serial.print("Subscribing to topic: ");
    Serial.print(incomingTopic);
    Serial.print(" with QoS ");
    Serial.println(incomingQoS);
    mqttClient.subscribe(incomingTopic, incomingQoS);   
}

void publishMessage()
{
    Serial.println("Publishing message");

    JSONVar payload;
    String msg = "Hello, World! ";
    msg += millis();
    payload["message"] = msg;
    payload["rssi"] = WiFi.RSSI();

    JSONVar message;
    message["ts"] = static_cast<unsigned long>(time(nullptr));
    message["payload"] = payload;

    String messageString = JSON.stringify(message);
    Serial.println(messageString);

    // send message, the Print interface can be used to set the message contents
    constexpr char outgoingTopic[] { "arduino/outgoing" };

    mqttClient.beginMessage(outgoingTopic);
    mqttClient.print(messageString);
    mqttClient.endMessage();
}

void onMessageReceived(int messageSize)
{
    // we received a message, print out the topic and contents
    Serial.println();
    Serial.print("Received a message with topic '");
    Serial.print(mqttClient.messageTopic());
    Serial.print("', length ");
    Serial.print(messageSize);
    Serial.println(" bytes:");

    /*
    // Message from AWS MQTT Test Client
    {
      "message": "Hello from AWS IoT console"
    }
    */

    char bytes[messageSize] {};
    for (int i = 0; i < messageSize; i++)
        bytes[i] = mqttClient.read();

    JSONVar jsonMessage = JSON.parse(bytes);
    auto text = jsonMessage["message"];

    Serial.print("[");
    Serial.print(time(nullptr));
    Serial.print("] ");
    Serial.print("Message: ");
    Serial.println(text);

    Serial.println();
}

void onNetworkConnect()
{
    Serial.println(">>>> CONNECTED to network");

    printWifiStatus();
    setNtpTime();
    connectMQTT();
}

void onNetworkDisconnect()
{
    Serial.println(">>>> DISCONNECTED from network");
}

void onNetworkError()
{
    Serial.println(">>>> ERROR");
}

void printWifiStatus()
{
    // print the SSID of the network you're attached to:
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    // print the received signal strength:
    Serial.print("signal strength (RSSI):");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    Serial.println();

    // print your board's IP address:
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Local GW: ");
    Serial.println(WiFi.gatewayIP());
    Serial.println();
}
