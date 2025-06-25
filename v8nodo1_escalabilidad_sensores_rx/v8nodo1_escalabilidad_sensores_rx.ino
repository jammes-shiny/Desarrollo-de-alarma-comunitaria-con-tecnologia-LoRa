#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>

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

// Estructura para mensajes almacenados
struct MensajeBuffer {
  String mensaje;
  int rssi;
  unsigned long timestamp;
};

// Buffer circular para mensajes pendientes
#define BUFFER_SIZE 20
MensajeBuffer mensajeBuffer[BUFFER_SIZE];
int bufferIndex = 0;
int mensajesPendientes = 0;

// Estructura para redes WiFi
struct WiFiNetwork {
  const char* ssid;
  const char* password;
};

// Lista de redes WiFi disponibles (añade todas las que necesites)
WiFiNetwork wifiNetworks[] = {
  {"2011", "12345678"},           // Red principal
  {"WiFi_Mesh-108090", "DTQbuYSH"} // Red de respaldo 1
};
const int numNetworks = sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);

// MQTT
const char* mqtt_server = "jammes.ddns.net";
const char* mqtt_user = "mqttalarma";
const char* mqtt_password = "mqttRa.CD3JDZ2002";
WiFiClient espClient;
PubSubClient client(espClient);

// Variables para el estado del receptor
String ultimoMensaje = "";
unsigned long ultimaRecepcion = 0;
bool wifiConectado = false;
int redActual = -1;
String nombreRedActual = "";

// Variables para optimización de reconexión
unsigned long ultimoIntentoReconexion = 0;
const unsigned long INTERVALO_RECONEXION_RAPIDA = 2000; // 2 segundos para reconexión rápida
const unsigned long INTERVALO_RECONEXION_NORMAL = 10000; // 10 segundos para búsqueda normal
bool reconexionRapida = true;
String ultimaRedConectada = "";

// Variables para el buffer de mensajes
bool enviandoBuffer = false;

void callback(char* topic, byte* message, unsigned int length) {
  // Callback para mensajes MQTT recibidos (si es necesario)
  Serial.print("Mensaje recibido en topic: ");
  Serial.print(topic);
  Serial.print(". Mensaje: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    messageTemp += (char)message[i];
  }
  Serial.println(messageTemp);
}

// Función para agregar mensaje al buffer
void agregarMensajeBuffer(String mensaje, int rssi) {
  if (mensajesPendientes < BUFFER_SIZE) {
    mensajeBuffer[bufferIndex].mensaje = mensaje;
    mensajeBuffer[bufferIndex].rssi = rssi;
    mensajeBuffer[bufferIndex].timestamp = millis();
    
    bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
    mensajesPendientes++;
    
    Serial.print("Mensaje almacenado en buffer. Pendientes: ");
    Serial.println(mensajesPendientes);
  } else {
    // Buffer lleno, sobrescribir el más antiguo
    Serial.println("Buffer lleno, sobrescribiendo mensaje más antiguo");
    mensajeBuffer[bufferIndex].mensaje = mensaje;
    mensajeBuffer[bufferIndex].rssi = rssi;
    mensajeBuffer[bufferIndex].timestamp = millis();
    
    bufferIndex = (bufferIndex + 1) % BUFFER_SIZE;
  }
}

// Función para enviar mensajes del buffer
void enviarMensajesBuffer() {
  if (mensajesPendientes == 0 || !wifiConectado || !client.connected()) {
    return;
  }
  
  if (enviandoBuffer) {
    return; // Ya se está enviando
  }
  
  enviandoBuffer = true;
  Serial.print("Enviando ");
  Serial.print(mensajesPendientes);
  Serial.println(" mensajes del buffer...");
  
  int indiceEnvio = (bufferIndex - mensajesPendientes + BUFFER_SIZE) % BUFFER_SIZE;
  int mensajesEnviados = 0;
  
  for (int i = 0; i < mensajesPendientes; i++) {
    int indiceActual = (indiceEnvio + i) % BUFFER_SIZE;
    
    // Enviar mensaje original sin modificaciones
    String mensajeOriginal = mensajeBuffer[indiceActual].mensaje;
    String topic = "test/topic";
    
    if (client.publish(topic.c_str(), mensajeOriginal.c_str())) {
      Serial.print("Buffer enviado: ");
      Serial.println(mensajeOriginal);
      mensajesEnviados++;
    } else {
      Serial.println("Error enviando mensaje del buffer");
      break; // Parar si hay error
    }
    
    delay(100); // Pequeña pausa entre envíos
  }
  
  // Actualizar contador de mensajes pendientes
  mensajesPendientes -= mensajesEnviados;
  
  if (mensajesPendientes == 0) {
    bufferIndex = 0; // Reiniciar buffer
    Serial.println("Todos los mensajes del buffer enviados correctamente");
  } else {
    Serial.print("Quedan ");
    Serial.print(mensajesPendientes);
    Serial.println(" mensajes pendientes");
  }
  
  enviandoBuffer = false;
}

void connectWiFiFast() {
  if (WiFi.status() == WL_CONNECTED) {
    return; // Ya está conectado
  }

  Serial.println("Reconexión WiFi rápida...");
  
  // Configuración para reconexión rápida
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  
  // Intentar reconexión automática primero (más rápido)
  if (ultimaRedConectada != "" && reconexionRapida) {
    Serial.print("Usando WiFi.reconnect() para: ");
    Serial.println(ultimaRedConectada);
    
    WiFi.reconnect();
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 3000) {
      delay(100); // Delay más corto para verificación más frecuente
      // Continuar recibiendo mensajes LoRa durante la reconexión
      checkLoRaMessages();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Reconexión rápida exitosa!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      wifiConectado = true;
      nombreRedActual = ultimaRedConectada;
      return;
    }
  }
  
  // Si la reconexión rápida falló, intentar con la red anterior conocida
  if (redActual >= 0 && redActual < numNetworks) {
    Serial.print("Intentando red anterior: ");
    Serial.println(wifiNetworks[redActual].ssid);
    
    WiFi.begin(wifiNetworks[redActual].ssid, wifiNetworks[redActual].password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 4000) {
      delay(100);
      // Continuar recibiendo mensajes LoRa durante la reconexión
      checkLoRaMessages();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Reconectado a red anterior!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      wifiConectado = true;
      nombreRedActual = String(wifiNetworks[redActual].ssid);
      ultimaRedConectada = nombreRedActual;
      return;
    }
  }
  
  // Búsqueda completa solo si falló la reconexión rápida
  Serial.println("Búsqueda completa de redes...");
  connectWiFiComplete();
}

void connectWiFiComplete() {
  // Probar todas las redes disponibles
  for (int i = 0; i < numNetworks; i++) {
    Serial.print("Probando red: ");
    Serial.println(wifiNetworks[i].ssid);
    
    WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 6000) {
      delay(100);
      // Continuar recibiendo mensajes LoRa durante la búsqueda
      checkLoRaMessages();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Conectado!");
      Serial.print("Red: ");
      Serial.println(wifiNetworks[i].ssid);
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("RSSI: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      
      wifiConectado = true;
      redActual = i;
      nombreRedActual = String(wifiNetworks[i].ssid);
      ultimaRedConectada = nombreRedActual;
      reconexionRapida = true; // Habilitar reconexión rápida para la próxima vez
      return;
    } else {
      Serial.print("Error conectando a: ");
      Serial.println(wifiNetworks[i].ssid);
    }
  }
  
  // Si llegamos aquí, no se pudo conectar a ninguna red
  Serial.println("No se pudo conectar a ninguna red WiFi");
  wifiConectado = false;
  redActual = -1;
  nombreRedActual = "Sin conexion";
  ultimaRedConectada = "";
  reconexionRapida = false;
}

void reconnectMQTT() {
  int intentos = 0;
  while (!client.connected() && intentos < 2) { // Reducido a 2 intentos
    Serial.print("Conectando MQTT... (");
    Serial.print(intentos + 1);
    Serial.println(")");
    
    String clientId = "LoRaReceiver_" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("MQTT conectado!");
      // Enviar mensajes del buffer cuando se reconecte MQTT
      enviarMensajesBuffer();
      break;
    } else {
      Serial.print("Error MQTT, rc=");
      Serial.println(client.state());
      delay(1000); // Reducido de 3000 a 1000ms
    }
    intentos++;
  }
}

void enviarMensajeMQTT(String mensaje) {
  if (wifiConectado) {
    if (!client.connected()) {
      reconnectMQTT();
    }
    
    if (client.connected()) {
      String topic = "test/topic";
      
      // Formato fijo: mensaje original sin modificaciones
      if (client.publish(topic.c_str(), mensaje.c_str())) {
        Serial.print("MQTT enviado: ");
        Serial.println(mensaje);
      } else {
        Serial.println("Error al publicar MQTT");
      }
    }
  }
}

void onReceive(int packetSize) {
  if (packetSize == 0) return;

  String received = "";
  while (LoRa.available()) {
    received += (char)LoRa.read();
  }

  // Obtener RSSI
  int rssi = LoRa.packetRssi();
  
  Serial.print("Mensaje LoRa recibido: ");
  Serial.print(received);
  Serial.print(" con RSSI: ");
  Serial.println(rssi);

  ultimoMensaje = received;
  ultimaRecepcion = millis();

  // Intentar enviar a MQTT o almacenar en buffer
  if (wifiConectado && client.connected()) {
    enviarMensajeMQTT(received);
  } else {
    Serial.println("WiFi/MQTT no disponible. Almacenando en buffer...");
    agregarMensajeBuffer(received, rssi);
  }
  
  // Actualizar pantalla
  actualizarPantalla(received, rssi);
}

// Función alternativa para recepción manual
void checkLoRaMessages() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String received = "";
    while (LoRa.available()) {
      received += (char)LoRa.read();
    }

    int rssi = LoRa.packetRssi();
    
    Serial.print("Mensaje LoRa recibido (manual): ");
    Serial.print(received);
    Serial.print(" con RSSI: ");
    Serial.println(rssi);

    ultimoMensaje = received;
    ultimaRecepcion = millis();

    // Intentar enviar a MQTT o almacenar en buffer
    if (wifiConectado && client.connected()) {
      enviarMensajeMQTT(received);
    } else {
      Serial.println("WiFi/MQTT no disponible. Almacenando en buffer...");
      agregarMensajeBuffer(received, rssi);
    }
    
    actualizarPantalla(received, rssi);
  }
}

void actualizarPantalla(String mensaje, int rssi) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("RECEPTOR LORA");
  
  display.setCursor(0, 10);
  display.print("WiFi: ");
  if (wifiConectado) {
    display.print("OK (");
    // Mostrar nombre de red abreviado
    String redAbreviada = nombreRedActual;
    if (redAbreviada.length() > 8) {
      redAbreviada = redAbreviada.substring(0, 8) + "..";
    }
    display.print(redAbreviada);
    display.print(")");
  } else {
    display.print("RECONECT..");
  }
  
  display.setCursor(0, 20);
  display.print("MQTT: ");
  display.print(client.connected() ? "OK" : "ERROR");
  
  // Mostrar mensajes en buffer
  if (mensajesPendientes > 0) {
    display.print(" B:");
    display.print(mensajesPendientes);
  }
  
  display.setCursor(0, 30);
  display.print("RSSI LoRa: ");
  display.print(rssi);
  display.print("dBm");
  
  display.setCursor(0, 40);
  display.print("Ultimo msg:");
  display.setCursor(0, 50);
  
  // Mostrar solo los primeros caracteres del mensaje
  String mensajeCortado = mensaje;
  if (mensaje.length() > 21) {
    mensajeCortado = mensaje.substring(0, 21);
  }
  display.print(mensajeCortado);
  
  display.display();
}

void mostrarPantallaEstado() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("RECEPTOR LORA");
  
  display.setCursor(0, 10);
  display.print("WiFi: ");
  if (wifiConectado) {
    display.print("OK");
    display.setCursor(0, 18);
    String redMostrar = nombreRedActual;
    if (redMostrar.length() > 21) {
      redMostrar = redMostrar.substring(0, 18) + "...";
    }
    display.print(redMostrar);
    
    display.setCursor(0, 26);
    display.print("RSSI WiFi: ");
    display.print(WiFi.RSSI());
    display.print("dBm");
  } else {
    display.print("RECONECTANDO...");
    display.setCursor(0, 18);
    if (reconexionRapida) {
      display.print("Modo rapido");
    } else {
      display.print("Buscando redes...");
    }
  }
  
  display.setCursor(0, 34);
  display.print("MQTT: ");
  display.print(client.connected() ? "OK" : "ERROR");
  
  // Mostrar mensajes en buffer
  if (mensajesPendientes > 0) {
    display.print(" Buffer:");
    display.print(mensajesPendientes);
  }
  
  display.setCursor(0, 42);
  display.print("Esperando datos...");
  
  display.setCursor(0, 50);
  display.print("Uptime: ");
  display.print(millis() / 1000);
  display.print("s");
  
  if (ultimaRecepcion > 0) {
    display.setCursor(0, 58);
    display.print("Ultimo: ");
    display.print((millis() - ultimaRecepcion) / 1000);
    display.print("s");
  }
  
  display.display();
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== RECEPTOR LORA - BUFFER DE MENSAJES ===");

  // Inicializar buffer
  mensajesPendientes = 0;
  bufferIndex = 0;

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
  display.print("RECEPTOR LORA");
  display.setCursor(0, 10);
  display.print("Buffer Mensajes");
  display.setCursor(0, 20);
  display.print("Iniciando...");
  display.display();

  Serial.println("Iniciando Receptor LoRa con Buffer");
  Serial.print("Redes WiFi configuradas: ");
  Serial.println(numNetworks);
  for (int i = 0; i < numNetworks; i++) {
    Serial.print("  - ");
    Serial.println(wifiNetworks[i].ssid);
  }
  Serial.print("Tamaño del buffer: ");
  Serial.println(BUFFER_SIZE);

  // LoRa
  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(BAND)) {
    Serial.println("Error iniciando LoRa");
    display.setCursor(0, 30);
    display.print("Error LoRa!");
    display.display();
    while (1);
  }
  
  // Configuración LoRa (debe coincidir con el transmisor)
  LoRa.setSignalBandwidth(125000);
  LoRa.setCodingRate4(8);
  LoRa.setSyncWord(0x34);
  LoRa.enableCrc();

  Serial.println("LoRa iniciado correctamente!");
  
  // Configuración WiFi para reconexión rápida
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  
  // Conectar WiFi inicial
  connectWiFiComplete(); // Primera conexión completa
  
  // Configurar MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  
  if (wifiConectado) {
    reconnectMQTT();
  }

  Serial.println("Usando recepción manual de LoRa");
  mostrarPantallaEstado();
  Serial.println("Receptor LoRa con buffer listo. Esperando mensajes...");
}

void loop() {
  // Verificar mensajes LoRa (recepción manual) - PRIORIDAD MÁXIMA
  checkLoRaMessages();
  
  // Mantener conexión MQTT
  if (wifiConectado && client.connected()) {
    client.loop();
  }
  
  // Enviar mensajes del buffer si hay WiFi y MQTT conectado
  if (wifiConectado && client.connected() && mensajesPendientes > 0 && !enviandoBuffer) {
    enviarMensajesBuffer();
  }
  
  // Verificación WiFi optimizada - más frecuente pero más inteligente
  static unsigned long ultimaVerificacionWiFi = 0;
  unsigned long intervaloVerificacion = reconexionRapida ? INTERVALO_RECONEXION_RAPIDA : INTERVALO_RECONEXION_NORMAL;
  
  if (millis() - ultimaVerificacionWiFi > intervaloVerificacion) {
    if (WiFi.status() != WL_CONNECTED) {
      wifiConectado = false;
      Serial.println("WiFi desconectado!");
      
      // Usar reconexión rápida solo si no han pasado más de 30 segundos desde la última desconexión
      if (millis() - ultimoIntentoReconexion > 30000) {
        reconexionRapida = false; // Forzar búsqueda completa después de mucho tiempo
      }
      
      ultimoIntentoReconexion = millis();
      connectWiFiFast();
      
    } else {
      // Verificar calidad de señal - umbral más bajo para evitar cambios innecesarios
      int rssiWiFi = WiFi.RSSI();
      if (rssiWiFi < -85) { // Señal muy débil (era -80)
        Serial.print("Señal WiFi muy débil (");
        Serial.print(rssiWiFi);
        Serial.println(" dBm). Buscando mejor red...");
        
        // Intentar encontrar una mejor red
        int redAnterior = redActual;
        redActual = -1;
        reconexionRapida = false; // Forzar búsqueda completa
        connectWiFiComplete();
        
        // Si no encontró una mejor red, volver a la anterior
        if (!wifiConectado && redAnterior >= 0) {
          redActual = redAnterior;
          connectWiFiFast();
        }
      }
    }
    ultimaVerificacionWiFi = millis();
  }
  
  // Reconectar MQTT si es necesario - más rápido
  if (wifiConectado && !client.connected()) {
    static unsigned long ultimoIntentoMQTT = 0;
    if (millis() - ultimoIntentoMQTT > 3000) { // Cada 3 segundos en lugar de inmediatamente
      Serial.println("MQTT desconectado. Reintentando...");
      reconnectMQTT();
      ultimoIntentoMQTT = millis();
    }
  }
  
  // Actualizar pantalla cada 3 segundos (más frecuente)
  static unsigned long ultimaActualizacion = 0;
  if (millis() - ultimaActualizacion > 3000) {
    if (ultimoMensaje == "" || millis() - ultimaRecepcion > 10000) {
      mostrarPantallaEstado();
    }
    ultimaActualizacion = millis();
  }
  
  delay(5); // Delay muy corto para máxima capacidad de respuesta
}