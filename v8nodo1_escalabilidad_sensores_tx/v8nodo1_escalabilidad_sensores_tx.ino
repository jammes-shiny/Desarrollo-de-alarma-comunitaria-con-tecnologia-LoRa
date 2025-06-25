#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ID único del dispositivo
#define id "A00000001"

// Configura cuántas zonas vas a usar (1 a 4)
#define ZONAS 2

// LoRa
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 23
#define DIO0 26
#define BAND 434E6

// Pantalla OLED
#define ANCHOPANTALLA 128
#define ALTOPANTALLA 64
#define OLED_SDA 21
#define OLED_SCL 22 
#define OLED_RST 23
Adafruit_SSD1306 display(ANCHOPANTALLA, ALTOPANTALLA, &Wire, OLED_RST);

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

#define UMBRAL_GAS 1  // Cambia si tu sensor devuelve LOW cuando detecta gas

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

  // OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW); delay(20); digitalWrite(OLED_RST, HIGH);
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) {
    Serial.println(F("Fallo iniciando SSD1306"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("TRANSMISOR LORA");
  display.display();

  Serial.println("Iniciando Transmisor LoRa");

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
  display.setCursor(0, 10);
  display.print("Inicio exitoso de LoRa!");
  display.display();
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

  // Mostrar en OLED todas las zonas
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("TRANSMISOR LORA");
  display.setCursor(0, 10);
  display.print("ID: ");
  display.print(id);
  display.print(" Zonas:");
  display.println(ZONAS);

  for (int i = 0; i < ZONAS; i++) {
    display.setCursor(0, 20 + 10*i);
    display.printf("Z%d T:%2dC H:%2d%% G:%d E:%d", i+1, temps[i], hums[i], current_gas[i], current_error[i]);
  }
  display.display();

  delay(1000);
}