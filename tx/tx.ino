#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <DHT.h>

// ---------- PRIVACIDAD----------
const byte xorKey = 000;  

void xorEncrypt(char* data, byte key) {
  for (int i = 0; data[i]; i++) {
    data[i] ^= key;
  }
}

// ---------- CONFIGURACIÓN GENERAL ----------
#define id "000000000"
#define ZONAS 1

// LoRa
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 14
#define DIO0 26
#define BAND 434E6

// Relé para alertas
const int RELE_PINS[] = {21, 22};
const int NUM_RELES = sizeof(RELE_PINS) / sizeof(RELE_PINS[0]);

// Pines para sensores
const int dhtPins[] = {25, 0, 15, 12};
const int mqPins[]  = {4, 2, 13, 14};

// Sensores DHT
DHT dhts[] = {
  DHT(dhtPins[0], DHT11),
  DHT(dhtPins[1], DHT11),
  DHT(dhtPins[2], DHT11),
  DHT(dhtPins[3], DHT11)
};

#define UMBRAL_GAS 0

// Estado anterior por zona
bool last_atemp[4] = {false};
bool last_ahum[4] = {false};
bool last_error[4] = {false};
bool last_gas[4] = {false};

// ---------- FUNCIONES ----------

String armarPayloadPlano(int temps[], int hums[], bool errores[], bool atemp[], bool ahum[], bool gas[], int zonas) {
  String payload = String(id) + "," + String(zonas);
  for (int i = 0; i < zonas; i++) {
    payload += ",";
    payload += String(atemp[i] ? 1 : 0) + ",";
    payload += String(ahum[i] ? 1 : 0) + ",";
    payload += String(errores[i] ? 1 : 0) + ",";
    payload += String(gas[i] ? 1 : 0);
  }
  return payload;
}

void enviarDatosLoRa(int temps[], int hums[], bool errores[], bool atemp[], bool ahum[], bool gas[], int zonas) {
  String mensaje = armarPayloadPlano(temps, hums, errores, atemp, ahum, gas, zonas);
  char mensajeArray[128];
  mensaje.toCharArray(mensajeArray, sizeof(mensajeArray));

  xorEncrypt(mensajeArray, xorKey);  // Cifrar

  LoRa.beginPacket();
  LoRa.write((uint8_t*)mensajeArray, strlen(mensajeArray));
  LoRa.endPacket();

  Serial.print("Mensaje cifrado enviado: ");
  for (int i = 0; i < strlen(mensajeArray); i++) {
    Serial.print((byte)mensajeArray[i]); Serial.print(" ");
  }
  Serial.println();

  // Verificación para activar relés
  String mensajeNeutro = String(id) + "," + String(zonas);
  for (int i = 0; i < zonas; i++) {
    mensajeNeutro += ",0,0,0,0";
  }

  if (mensaje != mensajeNeutro) {
    for (int i = 0; i < NUM_RELES; i++) digitalWrite(RELE_PINS[i], LOW);
    Serial.println("Alerta detectada, relés ACTIVADOS.");
  } else {
    for (int i = 0; i < NUM_RELES; i++) digitalWrite(RELE_PINS[i], HIGH);
    Serial.println("Mensaje neutro, relés desactivados.");
  }
}

void enviarMensajeInicial() {
  int temps[4] = {0}, hums[4] = {0};
  bool errores[4] = {false}, atemp[4] = {false}, ahum[4] = {false}, gas[4] = {false};
  enviarDatosLoRa(temps, hums, errores, atemp, ahum, gas, ZONAS);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Iniciando Transmisor LoRa...");

  for (int i = 0; i < NUM_RELES; i++) {
    pinMode(RELE_PINS[i], OUTPUT);
    digitalWrite(RELE_PINS[i], LOW);
  }

  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(BAND)) {
    Serial.println("Error iniciando LoRa.");
    while (1);
  }

  LoRa.setSignalBandwidth(125000);
  LoRa.setTxPower(10);
  LoRa.setCodingRate4(8);
  LoRa.setSyncWord(0x34);
  LoRa.enableCrc();

  Serial.println("LoRa listo.");
  delay(2000);

  for (int i = 0; i < ZONAS; i++) {
    dhts[i].begin();
    pinMode(mqPins[i], INPUT);
  }

  enviarMensajeInicial();
}

void loop() {
  bool changeDetected = false;

  int temps[4], hums[4];
  bool current_error[4], current_atemp[4], current_ahum[4], current_gas[4];

  for (int i = 0; i < ZONAS; i++) {
    hums[i] = dhts[i].readHumidity();
    temps[i] = dhts[i].readTemperature();
    int gasRaw = digitalRead(mqPins[i]);
    current_gas[i] = (gasRaw == UMBRAL_GAS);

    if (isnan(temps[i]) || isnan(hums[i])) {
      temps[i] = 0;
      hums[i] = 0;
      current_error[i] = true;
    } else {
      current_error[i] = false;
    }

    current_atemp[i] = (temps[i] >= 30);
    current_ahum[i] = (hums[i] >= 90);

    if (current_error[i] != last_error[i] ||
        current_atemp[i] != last_atemp[i] ||
        current_ahum[i] != last_ahum[i] ||
        current_gas[i] != last_gas[i]) {
      changeDetected = true;
    }
  }

  if (changeDetected) {
    enviarDatosLoRa(temps, hums, current_error, current_atemp, current_ahum, current_gas, ZONAS);
    for (int i = 0; i < ZONAS; i++) {
      last_error[i] = current_error[i];
      last_atemp[i] = current_atemp[i];
      last_ahum[i] = current_ahum[i];
      last_gas[i] = current_gas[i];
    }
  }

  delay(1000);
}
