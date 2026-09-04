/*
  Heltec WiFi LoRa 32 V4 (ESP32-S3 + SX1262)
  Test de mensajería punto a punto con botón PRG y pantalla OLED.

  Mismo firmware para AMBOS nodos: cualquiera puede enviar o recibir.

  Comportamiento:
    - Estado normal:      "No hay nuevos mensajes"
    - Pulsa PRG -> envía mensaje -> muestra "Mensaje enviado"
    - Pulsa PRG otra vez -> vuelve a "No hay nuevos mensajes"
    - Al recibir algo -> muestra "Nuevo mensaje"
    - Pulsa PRG -> muestra el contenido del mensaje recibido
    - Pulsa PRG otra vez -> vuelve a "No hay nuevos mensajes"

  Librerías necesarias (Gestor de Librerías de Arduino):
    - RadioLib (jgromes)
    - U8g2 (olikraus)
*/

#include <RadioLib.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <SPI.h>

// ---------- Pines LoRa (SX1262) - Heltec V4 ----------
#define LORA_NSS   8
#define LORA_SCK   9
#define LORA_MOSI  10
#define LORA_MISO  11
#define LORA_RST   12
#define LORA_BUSY  13
#define LORA_DIO1  14

// Habilitación del front-end de RF (GC1109). Sin esto, en la V4 el TX
// puede "parecer" correcto pero el otro nodo nunca recibe nada.
#define FEM_CTRL   2

// ---------- Pines OLED ----------
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21

// ---------- Botón PRG ----------
#define BUTTON_PIN 0   // GPIO0, activo en LOW, con pull-up interno

// ---------- Parámetros de radio (ajusta según tu región) ----------
#define LORA_FREQ       868.0   // MHz (EU868). Cambia a 915.0 si estás en US915, etc.
#define LORA_BW         125.0   // kHz
#define LORA_SF         7       // Spreading factor
#define LORA_CR         5       // Coding rate 4/5
#define LORA_SYNCWORD   0x12    // Privado (no usar 0x34, es el de LoRaWAN)
#define LORA_POWER      14      // dBm, bajo para pruebas de mesa (sube luego si hace falta)
#define LORA_PREAMBLE   8

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, OLED_RST);

// ---------- Máquina de estados ----------
enum AppState { ST_IDLE, ST_SENT, ST_RECEIVED, ST_VIEWING };
volatile AppState state = ST_IDLE;

String lastMessage = "";
uint16_t msgCounter = 0;

// Flags que se activan desde la interrupción del radio (deben ser volatile)
volatile bool operationDone = false;
volatile bool isTransmitting = false;

// Debounce del botón
unsigned long lastButtonMs = 0;
const unsigned long DEBOUNCE_MS = 250;

bool needsRedraw = true;

// ---------- Interrupción del radio (DIO1) ----------
void ICACHE_RAM_ATTR onRadioAction() {
  operationDone = true;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // Habilita el front-end de RF antes de tocar el radio
  pinMode(FEM_CTRL, OUTPUT);
  digitalWrite(FEM_CTRL, HIGH);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // OLED
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);

  // SPI del radio con pines manuales (no son los de VSPI/HSPI por defecto)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  int state_init = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                                LORA_SYNCWORD, LORA_POWER, LORA_PREAMBLE);
  if (state_init != RADIOLIB_ERR_NONE) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 20, "Error radio:");
    u8g2.drawStr(0, 35, String(state_init).c_str());
    u8g2.sendBuffer();
    Serial.print("Fallo al iniciar el radio, codigo: ");
    Serial.println(state_init);
    while (true) { delay(1000); }
  }

  radio.setDio1Action(onRadioAction);
  startListening();

  Serial.println("Radio listo. Esperando mensajes...");
  needsRedraw = true;
}

void startListening() {
  operationDone = false;
  isTransmitting = false;
  radio.startReceive();
}

void sendMessage() {
  msgCounter++;
  String payload = "Ping #" + String(msgCounter);

  operationDone = false;
  isTransmitting = true;

  int result = radio.transmit(payload);

  if (result == RADIOLIB_ERR_NONE) {
    Serial.println("Mensaje enviado: " + payload);
    state = ST_SENT;
  } else {
    Serial.print("Error al enviar, codigo: ");
    Serial.println(result);
    state = ST_IDLE; // si falla, no cambiamos de pantalla a "enviado"
  }

  // El transmit() deja el radio en modo TX; hay que volver a modo escucha
  startListening();
  needsRedraw = true;
}

void checkIncoming() {
  if (!operationDone || isTransmitting) return;
  operationDone = false;

  String received;
  int result = radio.readData(received);

  if (result == RADIOLIB_ERR_NONE) {
    lastMessage = received;
    state = ST_RECEIVED;
    needsRedraw = true;
    Serial.println("Mensaje recibido: " + received);
  }
  // Volvemos a poner el radio en escucha para el siguiente paquete
  radio.startReceive();
}

bool buttonPressed() {
  static bool lastReading = HIGH;
  bool reading = digitalRead(BUTTON_PIN);
  bool pressed = false;

  if (reading == LOW && lastReading == HIGH &&
      (millis() - lastButtonMs) > DEBOUNCE_MS) {
    pressed = true;
    lastButtonMs = millis();
  }
  lastReading = reading;
  return pressed;
}

void handleButton() {
  if (!buttonPressed()) return;

  switch (state) {
    case ST_IDLE:
      sendMessage();
      break;

    case ST_SENT:
      state = ST_IDLE;
      needsRedraw = true;
      break;

    case ST_RECEIVED:
      state = ST_VIEWING;
      needsRedraw = true;
      break;

    case ST_VIEWING:
      state = ST_IDLE;
      needsRedraw = true;
      break;
  }
}

void updateDisplay() {
  if (!needsRedraw) return;
  needsRedraw = false;

  u8g2.clearBuffer();

  switch (state) {
    case ST_IDLE:
      u8g2.drawStr(0, 35, "No hay nuevos");
      u8g2.drawStr(0, 50, "mensajes");
      break;

    case ST_SENT:
      u8g2.drawStr(0, 35, "Mensaje enviado");
      break;

    case ST_RECEIVED:
      u8g2.drawStr(0, 35, "Nuevo mensaje");
      break;

    case ST_VIEWING:
      u8g2.drawStr(0, 15, "Mensaje recibido:");
      u8g2.drawStr(0, 35, lastMessage.c_str());
      break;
  }

  u8g2.sendBuffer();
}

void loop() {
  checkIncoming();
  handleButton();
  updateDisplay();
}
