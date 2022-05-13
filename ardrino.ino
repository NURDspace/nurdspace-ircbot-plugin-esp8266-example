#include <ArduinoJson.h>

#include <LittleFS.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager

#include <PubSubClient.h>

WiFiManager   wifiManager;

WiFiClient   wclient;
PubSubClient client(wclient);

char         mqttServer[32] = "10.208.30.11";  // default mqtt server
WiFiManagerParameter customMqttServer("server", "mqtt server", mqttServer, sizeof mqttServer);

char         botMqttPrefix[32] = "GHBot/";  // prefix for mqtt topics
WiFiManagerParameter customBotMqttPrefix("prefix", "bot MQTT prefix", botMqttPrefix, sizeof botMqttPrefix);

char         msgsInTopic[128] { 0 };

// assuming here that callback() is never called concurrently
char         msgsOutTopic[128] { 0 };

char         registerRequestTopic[128] { 0 };

char         registerTopic[128] { 0 };

void announce() {
	client.publish(registerTopic, "cmd=ardrino|descr=An ESP8266 as a bot plugin");
}

void callback(char *topic, byte *payload, unsigned int length) {
	Serial.print(F("Received MQTT msg for topic: "));
	Serial.println(topic);

	if (strcmp(topic, registerRequestTopic) == 0 && strncmp((const char *)payload, "register", 8) == 0) {
		announce();

		return;
	}

	if (strncmp(topic, msgsInTopic, strlen(msgsInTopic) - 1 /* ignore '#' */) == 0 && length >= 8) {
		if (strncmp(reinterpret_cast<const char *>(payload), "~ardrino", 8) == 0) {
			const char *p_topic = &topic[strlen(botMqttPrefix)];

			const char *p_channel = &p_topic[9]; // "from/irc/"
			const char *p_channel_end = strchr(p_channel, '/');

			if (p_channel_end) {
				char temp[32] { 0 };

				strncpy(temp, p_channel, std::min(31, p_channel_end - p_channel));

				snprintf(msgsOutTopic, sizeof msgsOutTopic, "%sto/irc/%s/privmsg", botMqttPrefix, temp);

				Serial.print(F("Publishing to: "));
				Serial.println(msgsOutTopic);

				// TODO: do something instead of:
				client.publish(msgsOutTopic, "Hello, this is Ardrino!");
			}
		}
	}
}

void pollMqtt() {
	client.loop();

	while (!client.connected()) {
		Serial.println("Attempting MQTT connection... ");

		if (client.connect("ardrino")) {
			Serial.println(F("Connected"));

			snprintf(msgsInTopic, sizeof msgsInTopic, "%sfrom/irc/#", botMqttPrefix);

			client.subscribe(msgsInTopic);

			snprintf(registerRequestTopic, sizeof registerRequestTopic, "%sfrom/bot/command", botMqttPrefix);

			client.subscribe(registerRequestTopic);

			snprintf(registerTopic, sizeof registerTopic, "%sto/bot/register", botMqttPrefix);

			// in case this is the first connect since up
			announce();

			break;
		}

		delay(1000);
	}
}

void reboot() {
	Serial.println(F("Reboot"));
	Serial.flush();

	delay(100);

	ESP.restart();

	delay(1000);
}

void loadConfiguration() {
	if (LittleFS.begin()) {
		Serial.println(F("mounted file system"));

		if (LittleFS.exists("/config.json")) {
			// file exists, reading and loading
			Serial.println(F("reading config file"));

			File configFile = LittleFS.open("/config.json", "r");
			if (configFile) {
				size_t size = configFile.size();

				std::unique_ptr<char[]> buf(new char[size]);

				configFile.readBytes(buf.get(), size);

				DynamicJsonDocument  jsonBuffer(128);
				DeserializationError error = deserializeJson(jsonBuffer, buf.get());

				if (error == 0) {
					serializeJson(jsonBuffer, Serial);

					strcpy(mqttServer,    jsonBuffer["mqttServer"]);
					strcpy(botMqttPrefix, jsonBuffer["botMqttPrefix"]);

					return;
				}

				Serial.println(F("failed to load json config"));
			}
		}
	}
	else {
		Serial.println(F("failed to mount FS"));
	}

	return;
}

void saveConfigCallback() {
	strcpy(mqttServer,    customMqttServer.getValue());

	strcpy(botMqttPrefix, customBotMqttPrefix.getValue());

	Serial.println(F("saving config"));

	DynamicJsonDocument jsonBuffer(128);

	jsonBuffer["mqttServer"]    = mqttServer;
	jsonBuffer["botMqttPrefix"] = botMqttPrefix;

	File configFile = LittleFS.open("/config.json", "w");
	if (!configFile) {
		Serial.println(F("failed to open config file for writing"));

		return;
	}

	serializeJson(jsonBuffer, Serial);

	serializeJson(jsonBuffer, configFile);

	configFile.close();
}

void setupWifi() {
	WiFi.begin();

	wifiManager.setTimeout(300);

	wifiManager.setSaveConfigCallback(saveConfigCallback);

	wifiManager.addParameter(&customMqttServer);

	wifiManager.addParameter(&customBotMqttPrefix);

	if (!wifiManager.autoConnect("ardrino-demo"))
		reboot();
}

void setup() {
	Serial.begin(115200);
	Serial.println(F("Init"));

	loadConfiguration();

	setupWifi();

	Serial.print(F("MQTT server: "));
	Serial.println(mqttServer);

	client.setServer(mqttServer, 1883);

	client.setCallback(callback);

	Serial.println(F("Go!"));
}

void loop() {
	pollMqtt();

	// TODO: do something
}
