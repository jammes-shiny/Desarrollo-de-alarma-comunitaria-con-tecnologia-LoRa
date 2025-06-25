#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Parámetros
#define NODO "casa1"

// LoRa
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 23
#define DIO0 26
#define BAND 451E6 

// Pantalla OLED
#define ANCHOPANTALLA 128
#define ALTOPANTALLA 64
#define OLED_SDA 21
#define OLED_SCL 22 
#define OLED_RST 23
Adafruit_SSD1306 display(ANCHOPANTALLA, ALTOPANTALLA, &Wire, OLED_RST);

// Sensor DHT
#define dhtpin 25
#define DHTTYPE DHT11
DHT dht(dhtpin, DHTTYPE);

int temp = 0;
int hum = 0;

// WiFi
const char* ssid = "WiFi_Mesh-108090";
const char* password = "DTQbuYSH";

// MQTT
const char* mqtt_server = "192.168.1.36";
const char* mqtt_user = "root";
const char* mqtt_password = "123";
WiFiClient espClient;
PubSubClient client(espClient);

// Variables para estado anterior
bool last_atemp = false;
bool last_ahum = false;
bool last_error = false;

void callback(char* topic, byte* message, unsigned int length) {
  // Vacío por ahora
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  unsigned long startAttemptTime = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000) {
    delay(200);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado.");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nError al conectar WiFi");
  }
}

void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi desconectado para ahorrar energía");
}

void reconnectMQTT() {
  if (!client.connected()) {
    Serial.print("Intentando conectar MQTT...");
    if (client.connect(NODO, mqtt_user, mqtt_password)) {
      Serial.println("¡Conectado al broker MQTT!");
    } else {
      Serial.print("Error, rc=");
      Serial.print(client.state());
      Serial.println();
    }
  }
}

void setup() {
  Serial.begin(115200);

  // OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(20); digitalWrite(OLED_RST, HIGH);
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) {
    Serial.println(F("Fallo iniciando SSD1306"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("TRANSMISOR LORA");
  display.display();

  Serial.println("Prueba de envío LoRa");

  // LoRa
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(BAND)) {
    Serial.println("Error iniciando LoRa");
    while (1);
  }
  LoRa.setSignalBandwidth(7800);
  LoRa.setTxPower(20);
  LoRa.setCodingRate4(8);
  LoRa.setSyncWord(0x34);
  LoRa.enableCrc();

  Serial.println("Inicio exitoso de LoRa!");
  display.setCursor(0,10);
  display.print("Inicio exitoso de LoRa!");
  display.display();
  delay(2000);

  // Sensor
  dht.begin();

  // MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  hum = dht.readHumidity();
  temp = dht.readTemperature();

  if (isnan(temp) || isnan(hum)) {
    Serial.println("Error al leer sensor DHT!");
    temp = 2147483647;
  }

  bool current_error = (temp == 2147483647);
  bool current_atemp = (temp >= 30);
  bool current_ahum = (hum >= 90);

  if (current_error != last_error || current_atemp != last_atemp || current_ahum != last_ahum) {
    Serial.println("Cambio detectado, conectando a WiFi...");

    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      reconnectMQTT();

      String topic = "test/topic";
      String payload;

      if (current_error) {
        payload = "{\"domicilio\": \"" + String(NODO) + "\", \"alerta temperatura\": 0, \"alerta humedad\": 0, \"alerta gas\": 0, \"alerta temp error\": 1}";
      } else if (current_atemp || current_ahum) {
        payload = "{\"domicilio\": \"" + String(NODO) + "\", \"alerta temperatura\": " + String(current_atemp ? 1 : 0) + ", \"alerta humedad\": " + String(current_ahum ? 1 : 0) + ", \"alerta gas\": 0, \"alerta temp error\": 0}";
      } else {
        payload = "{\"domicilio\": \"" + String(NODO) + "\", \"alerta temperatura\": 0, \"alerta humedad\": 0, \"alerta gas\": 0, \"alerta temp error\": 0}";
      }

      bool published = false;
      int retries = 0;
      const int maxRetries = 3;

      while (!published && retries < maxRetries) {
        if (!client.connected()) {
          reconnectMQTT();
        }

        if (client.connected()) {
          published = client.publish(topic.c_str(), payload.c_str());
          if (published) {
            Serial.print("MQTT enviado a ");
            Serial.print(topic);
            Serial.print(": ");
            Serial.println(payload);
          } else {
            Serial.println("Error al publicar MQTT, reintentando...");
          }
        } else {
          Serial.println("No conectado al broker MQTT, intentando reconectar...");
        }

        client.loop();
        delay(1000);
        retries++;
      }

      if (published) {
        client.disconnect();
        Serial.println("Desconectado MQTT después de publicar");
      } else {
        Serial.println("Fallo al enviar MQTT tras varios intentos");
      }
    }

    disconnectWiFi();

    last_error = current_error;
    last_atemp = current_atemp;
    last_ahum = current_ahum;
  } else {
    Serial.println("Sin cambios, WiFi desconectado");
  }

  // Mostrar en OLED
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.println("Transmisor LoRa");
  display.setCursor(0,15);
  display.print("Nodo: ");
  display.println(NODO);
  display.setCursor(0,30);
  display.print("Temp: ");
  if (current_error) {
    display.println("Error sensor");
  } else {
    display.print(temp);
    display.println(" C");
  }
  display.setCursor(0,45);
  display.print("Humedad: ");
  if (current_error) {
    display.println("Error sensor");
  } else {
    display.print(hum);
    display.println(" %");
  }
  display.display();

  // Enviar paquete LoRa
  LoRa.beginPacket();
  LoRa.print(NODO);
  LoRa.print(":");
  LoRa.print(current_error ? 0 : temp);
  LoRa.print("C ");
  LoRa.print(current_error ? 0 : hum);
  LoRa.print("%");
  LoRa.endPacket();

  delay(1000);
}
