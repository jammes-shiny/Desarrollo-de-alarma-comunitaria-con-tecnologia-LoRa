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

//alertas bool
bool atemp = false;
bool ahum = false;

// WiFi
const char* ssid = "WiFi_Mesh-108090";
const char* password = "DTQbuYSH";

// MQTT
const char* mqtt_server = "192.168.1.36";
const char* mqtt_user = "root";
const char* mqtt_password = "123";
WiFiClient espClient;
PubSubClient client(espClient);

void callback(char* topic, byte* message, unsigned int length) {
  // Callback vacío por ahora
}

void setup_wifi() {
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); 
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado.");
  Serial.print("IP: "); Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conectar MQTT...");
    if (client.connect(NODO, mqtt_user, mqtt_password)) {
      Serial.println("¡Conectado al broker MQTT!");
    } else {
      Serial.print("Error, rc=");
      Serial.print(client.state());
      Serial.println(" Intentando de nuevo en 5 segundos");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Inicializar pantalla OLED
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

  // Configurar LoRa
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

  // Inicializar sensor DHT
  dht.begin();

  // Conectar WiFi y MQTT
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  // Leer sensor DHT
  hum = dht.readHumidity();
  temp = dht.readTemperature();

  if (isnan(temp) || isnan(hum)) {
    Serial.println("Error al leer sensor DHT!");
    return;
  }

  Serial.print("Temperatura: "); Serial.print(temp); Serial.print(" °C");
  Serial.print("  Humedad: "); Serial.print(hum); Serial.println(" %");

  // Enviar paquete LoRa
  LoRa.beginPacket();
  LoRa.print(NODO);
  LoRa.print(":");
  LoRa.print(temp);
  LoRa.print("C ");
  LoRa.print(hum);
  LoRa.print("%");
  LoRa.endPacket();

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
  display.print(temp);
  display.println(" C");
  display.setCursor(0,45);
  display.print("Humedad: ");
  display.print(hum);
  display.println(" %");
  display.display();



  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Error sensor DHT
  if(temp == 2147483647){
    String topic = String("test/topic");
    String payload = "{\"domicilio\": \"" + String(NODO) + "\", \"alerta temperatura\": 0, \"alerta humedad\": 0, \"alerta gas\": 0, \"alerta temp error\": 1}";

    client.publish(topic.c_str(), payload.c_str());
    Serial.print("MQTT enviado a ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(payload);
    return;
  }

  // Evaluar alertas
  bool atemp = (temp >= 30);
  bool ahum = (hum >= 300);

  if(atemp || ahum){
    String topic = String("test/topic");
    String payload = "{\"domicilio\": \"" + String(NODO) + "\", \"alerta temperatura\": " + String(atemp) + ", \"alerta humedad\": " + String(ahum) + ", \"alerta gas\": 0, \"alerta temp error\": 0}";

    client.publish(topic.c_str(), payload.c_str());

    Serial.print("MQTT enviado a ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(payload);
  } else {
    // No hay alertas, enviar todo en 0
    String topic = String("test/topic");
    String payload = "{\"domicilio\": \"" + String(NODO) + "\", \"alerta temperatura\": 0, \"alerta humedad\": 0, \"alerta gas\": 0, \"alerta temp error\": 0}";

    client.publish(topic.c_str(), payload.c_str());

    Serial.print("MQTT enviado a ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(payload);
  }

  delay(1000);
}

