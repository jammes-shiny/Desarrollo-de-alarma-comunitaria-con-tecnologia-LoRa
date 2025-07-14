#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define TINY_GSM_MODEM_SIM7600
#include <TinyGsmClient.h>
#include <PubSubClient.h>

// XOR PRIVACY
const byte xorKey = 135;
void xorDecrypt(char* data, byte key) {
  for (int i = 0; data[i]; i++) data[i] ^= key;
}

// LORA
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 23
#define DIO0 26
#define BAND 434E6

// OLED
#define ANCHOPANTALLA 128
#define ALTOPANTALLA 64
#define OLED_SDA 21
#define OLED_SCL 22
#define OLED_RST 23
Adafruit_SSD1306 display(ANCHOPANTALLA, ALTOPANTALLA, &Wire, OLED_RST);

// BUFFER
struct MensajeBuffer {
  String mensaje;
  int rssi;
  unsigned long timestamp;
};
#define BUFFER_SIZE 20
MensajeBuffer mensajeBuffer[BUFFER_SIZE];
int bufferIndex = 0;
int mensajesPendientes = 0;

// A7670SA CONFIG
#define MODEM_RX 13
#define MODEM_TX 12
#define MODEM_PWRKEY 14
#define MODEM_BAUD 115200
HardwareSerial modemSerial(1);
TinyGsm modem(modemSerial);
TinyGsmClient espClient(modem);
PubSubClient client(espClient);

// MQTT CONFIG
const char* mqtt_server = "dominio";
const char* mqtt_user = "user";
const char* mqtt_password = "pass";

String ultimoMensaje = "";
unsigned long ultimaRecepcion = 0;
bool wifiConectado = false;
bool enviandoBuffer = false;

void encenderModem() {
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  delay(3000);
  digitalWrite(MODEM_PWRKEY, HIGH);
  delay(1000);
}

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
  display.print("4G: ");
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

void setup() {
  Serial.begin(115200);

  // OLED y LoRa
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

  // Encender módem y conectar
// Encender módem y conectar
encenderModem();
modemSerial.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
delay(5000);
Serial.println("🔄 Iniciando módem...");
modem.restart();
delay(5000);

// Forzar SIM1
Serial.println("⚙️ Forzando SIM 1...");
modem.sendAT("+CSIMSELECT=1");
modem.waitResponse(1000);

// IMEI
Serial.println("📱 IMEI:");
modem.sendAT("+CGSN");
modem.waitResponse(1000);

// Señal
Serial.println("📶 Consultando nivel de señal...");
modem.sendAT("+CSQ");
modem.waitResponse(1000);

// Registro red
Serial.println("📡 Esperando registro a red...");
unsigned long startReg = millis();
while (!modem.isNetworkConnected() && millis() - startReg < 30000) {
  Serial.print(".");
  delay(1000);
}
if (modem.isNetworkConnected()) {
  Serial.println("\n✅ Red detectada");
} else {
  Serial.println("\n❌ No se detectó red celular");
  return;
}

// Operador
Serial.println("📞 Operador detectado:");
modem.sendAT("+COPS?");
modem.waitResponse(1000);

// GPRS
Serial.println("🌐 Conectando a GPRS Entel...");
if (modem.gprsConnect("bam.clarochile.cl", "", "")) {
  Serial.println("✅ Conexión GPRS exitosa");
  wifiConectado = true;
  IPAddress ip = modem.localIP();
  Serial.print("🧠 IP pública: ");
  Serial.println(ip);
} else {
  Serial.println("❌ Error en conexión GPRS");
  wifiConectado = false;
  return;
}




  if (modem.isGprsConnected()) {
    wifiConectado = true;
    Serial.println("Conectado a red móvil");
  } else {
    wifiConectado = false;
    Serial.println("Error conectando a red móvil");
  }

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  if (wifiConectado) reconnectMQTT();
  actualizarPantalla("Iniciando...", 0);
}

void loop() {
  checkLoRaMessages();
  if (wifiConectado && client.connected()) client.loop();
  if (wifiConectado && client.connected() && mensajesPendientes > 0 && !enviandoBuffer)
    enviarMensajesBuffer();

  static unsigned long ultimaActPantalla = 0;
  if (millis() - ultimaActPantalla > 3000) {
    actualizarPantalla(ultimoMensaje, LoRa.packetRssi());
    ultimaActPantalla = millis();
  }

  delay(5);
}
