// === INCLUDES ===
#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>

// === XOR PRIVACY ===
const byte xorKey = 000;

void xorDecrypt(char* data, byte key) {
  for (int i = 0; data[i]; i++) {
    data[i] ^= key;
  }
}

// === LORA ===
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 23
#define DIO0 26
#define BAND 434E6

// === OLED ===
#define ANCHOPANTALLA 128
#define ALTOPANTALLA 64
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST 23
Adafruit_SSD1306 display(ANCHOPANTALLA, ALTOPANTALLA, &Wire, OLED_RST);

// === BUFFER DE MENSAJES ===
struct MensajeBuffer {
  String mensaje;
  int rssi;
  unsigned long timestamp;
};
#define BUFFER_SIZE 20
MensajeBuffer mensajeBuffer[BUFFER_SIZE];
int bufferIndex = 0;
int mensajesPendientes = 0;

// === WIFI MULTI ===
struct WiFiNetwork {
  const char* ssid;
  const char* password;
};
WiFiNetwork wifiNetworks[] = {
  {"wifi1", "wifi1"},
  {"wifi2", "wifi2"}
};
const int numNetworks = sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);

// === MQTT ===
const char* mqtt_server = "actlora.ddns.net";
const char* mqtt_user = "user";
const char* mqtt_password = "password";
WiFiClient espClient;
PubSubClient client(espClient);

String ultimoMensaje = "";
unsigned long ultimaRecepcion = 0;
bool wifiConectado = false;
int redActual = -1;
String nombreRedActual = "";
bool enviandoBuffer = false;
unsigned long ultimoIntentoReconexion = 0;
const unsigned long INTERVALO_RECONEXION_RAPIDA = 2000;
const unsigned long INTERVALO_RECONEXION_NORMAL = 10000;
bool reconexionRapida = true;
String ultimaRedConectada = "";

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Mensaje recibido en topic: ");
  Serial.println(topic);
}

void agregarMensajeBuffer(String mensaje, int rssi) {
  mensajeBuffer[bufferIndex] = {mensaje, rssi, millis()};
  bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
  if (mensajesPendientes < BUFFER_SIZE) mensajesPendientes++;
  Serial.println("Mensaje agregado al buffer");
}

void enviarMensajesBuffer() {
  if (!wifiConectado || !client.connected() || mensajesPendientes == 0) return;
  enviandoBuffer = true;
  int idxInicio = (bufferIndex - mensajesPendientes + BUFFER_SIZE) % BUFFER_SIZE;
  for (int i = 0; i < mensajesPendientes; i++) {
    int idx = (idxInicio + i) % BUFFER_SIZE;
    if (client.publish("test/topic", mensajeBuffer[idx].mensaje.c_str())) {
      Serial.print("MQTT Buffer enviado: ");
      Serial.println(mensajeBuffer[idx].mensaje);
    } else {
      Serial.println("Error MQTT buffer");
      break;
    }
    delay(100);
  }
  mensajesPendientes = 0;
  bufferIndex = 0;
  enviandoBuffer = false;
}

void connectWiFiFast();
void connectWiFiComplete();

void reconnectMQTT() {
  int intentos = 0;
  while (!client.connected() && intentos < 2) {
    String clientId = "LoRaReceiver_" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("MQTT conectado");
      enviarMensajesBuffer();
      return;
    }
    delay(1000);
    intentos++;
  }
}

void enviarMensajeMQTT(String mensaje) {
  if (wifiConectado) {
    if (!client.connected()) reconnectMQTT();
    if (client.connected()) client.publish("test/topic", mensaje.c_str());
  }
}

void onReceive(int packetSize) {
  if (packetSize == 0) return;
  char buffer[128];
  int i = 0;
  while (LoRa.available() && i < sizeof(buffer) - 1) buffer[i++] = (char)LoRa.read();
  buffer[i] = '\0';
  xorDecrypt(buffer, xorKey);
  String received = String(buffer);
  int rssi = LoRa.packetRssi();
  Serial.print("Mensaje LoRa recibido: ");
  Serial.println(received);
  ultimoMensaje = received;
  ultimaRecepcion = millis();
  if (wifiConectado && client.connected()) enviarMensajeMQTT(received);
  else agregarMensajeBuffer(received, rssi);
  actualizarPantalla(received, rssi);
}

void checkLoRaMessages() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) onReceive(packetSize);
}

void actualizarPantalla(String mensaje, int rssi) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("RECEPTOR LORA");
  display.setCursor(0, 10);
  display.print("WiFi: ");
  display.print(wifiConectado ? "OK" : "NO");
  display.setCursor(0, 20);
  display.print("MQTT: ");
  display.print(client.connected() ? "OK" : "NO");
  display.setCursor(0, 30);
  display.print("RSSI LoRa: ");
  display.print(rssi);
  display.setCursor(0, 40);
  display.print("Msg: ");
  display.setCursor(0, 50);
  display.print(mensaje.substring(0, 21));
  display.display();
}

void connectWiFiFast() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (ultimaRedConectada != "" && reconexionRapida) {
    WiFi.reconnect();
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 3000) {
      delay(100);
      checkLoRaMessages();
    }
    if (WiFi.status() == WL_CONNECTED) {
      wifiConectado = true;
      nombreRedActual = ultimaRedConectada;
      return;
    }
  }
  if (redActual >= 0 && redActual < numNetworks) {
    WiFi.begin(wifiNetworks[redActual].ssid, wifiNetworks[redActual].password);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 4000) {
      delay(100);
      checkLoRaMessages();
    }
    if (WiFi.status() == WL_CONNECTED) {
      wifiConectado = true;
      nombreRedActual = String(wifiNetworks[redActual].ssid);
      ultimaRedConectada = nombreRedActual;
      return;
    }
  }
  connectWiFiComplete();
}

void connectWiFiComplete() {
  for (int i = 0; i < numNetworks; i++) {
    WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 6000) {
      delay(100);
      checkLoRaMessages();
    }
    if (WiFi.status() == WL_CONNECTED) {
      wifiConectado = true;
      redActual = i;
      nombreRedActual = String(wifiNetworks[i].ssid);
      ultimaRedConectada = nombreRedActual;
      reconexionRapida = true;
      return;
    }
  }
  wifiConectado = false;
  redActual = -1;
  nombreRedActual = "";
  ultimaRedConectada = "";
  reconexionRapida = false;
}

void setup() {
  Serial.begin(115200);
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(20); digitalWrite(OLED_RST, HIGH);
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) {
    Serial.println("Fallo SSD1306");
    while (1);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("RECEPTOR LORA");
  display.display();

  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(BAND)) {
    Serial.println("Fallo LoRa");
    while (1);
  }
  LoRa.setSignalBandwidth(125000);
  LoRa.setCodingRate4(8);
  LoRa.setSyncWord(0x34);
  LoRa.enableCrc();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  connectWiFiComplete();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  if (wifiConectado) reconnectMQTT();
  actualizarPantalla("Iniciando...", 0);
}

void loop() {
  checkLoRaMessages();
  if (wifiConectado && client.connected()) client.loop();
  if (wifiConectado && client.connected() && mensajesPendientes > 0 && !enviandoBuffer) enviarMensajesBuffer();
  static unsigned long ultimaVerWiFi = 0;
  if (millis() - ultimaVerWiFi > (reconexionRapida ? INTERVALO_RECONEXION_RAPIDA : INTERVALO_RECONEXION_NORMAL)) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiConectado = false;
      ultimoIntentoReconexion = millis();
      connectWiFiFast();
    }
    ultimaVerWiFi = millis();
  }
  static unsigned long ultimaActPantalla = 0;
  if (millis() - ultimaActPantalla > 3000) {
    actualizarPantalla(ultimoMensaje, LoRa.packetRssi());
    ultimaActPantalla = millis();
  }
  delay(5);
}
