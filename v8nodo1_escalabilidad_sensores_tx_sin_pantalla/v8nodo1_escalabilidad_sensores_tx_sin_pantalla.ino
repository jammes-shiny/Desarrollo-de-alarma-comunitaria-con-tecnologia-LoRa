#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <DHT.h>

// ID único del dispositivo
#define id "000000000"

// Configura cuántas zonas vas a usar (1 a 4)
#define ZONAS 2

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
bool estadorele = true;

// Pines para cada DHT y MQ135 (por zona)
const int dhtPins[] = {25, 0, 15, 12};     // Pines digitales para DHT11
const int mqPins[]  = {4, 2, 13, 14};      // Pines para MQ135

// Crear objetos DHT para cada zona
DHT dhts[] = {
  DHT(dhtPins[0], DHT11),
  DHT(dhtPins[1], DHT11),
  DHT(dhtPins[2], DHT11),
  DHT(dhtPins[3], DHT11)
};

#define UMBRAL_GAS 0  // Cambia si tu sensor devuelve LOW cuando detecta gas

// Estado anterior por zona
bool last_atemp[4] = {false, false, false, false};
bool last_ahum[4] = {false, false, false, false};
bool last_error[4] = {false, false, false, false};
bool last_gas[4] = {false, false, false, false};

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
  
  LoRa.beginPacket();
  LoRa.print(mensaje);
  LoRa.endPacket();
  
  Serial.print("LoRa enviado: ");
  Serial.println(mensaje);

  // 🔁 Conmutar el estado del relé
  estadorele = !estadorele;
  for (int i = 0; i < NUM_RELES; i++) {
  digitalWrite(RELE_PINS[i], estadorele ? HIGH : LOW);
}
Serial.print("Relés (21 y 22) ahora están: ");
Serial.println(estadorele ? "ACTIVADOS" : "DESACTIVADOS");
}

void enviarMensajeInicial() {
  int temps[4] = {0};
  int hums[4] = {0};
  bool errores[4] = {false};
  bool atemp[4] = {false};
  bool ahum[4] = {false};
  bool gas[4] = {false};

  enviarDatosLoRa(temps, hums, errores, atemp, ahum, gas, ZONAS);
}

void setup() {
  Serial.begin(115200);

  Serial.println("Iniciando Transmisor LoRa");

  // Configurar pin del relé
  for (int i = 0; i < NUM_RELES; i++) {
  pinMode(RELE_PINS[i], OUTPUT);
  digitalWrite(RELE_PINS[i], LOW);  // Inicialmente desactivados
}

  // LoRa
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(BAND)) {
    Serial.println("Error iniciando LoRa");
    while (1);
  }
  LoRa.setSignalBandwidth(125000);
  LoRa.setTxPower(10);
  LoRa.setCodingRate4(8);
  LoRa.setSyncWord(0x34);
  LoRa.enableCrc();

  Serial.println("Inicio exitoso de LoRa!");
  delay(2000);

  // Inicializar sensores DHT y MQ135
  for (int i = 0; i < ZONAS; i++) {
    dhts[i].begin();
    pinMode(mqPins[i], INPUT);
  }

  enviarMensajeInicial();  // Envía datos iniciales en cero
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
      temps[i] = 0; // Para evitar valores NAN en el envío plano
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

    // Actualizar estados previos
    for (int i = 0; i < ZONAS; i++) {
      last_error[i] = current_error[i];
      last_atemp[i] = current_atemp[i];
      last_ahum[i] = current_ahum[i];
      last_gas[i] = current_gas[i];
    }
  }

  

  delay(1000);
}