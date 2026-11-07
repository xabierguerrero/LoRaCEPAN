/*
  Heltec WiFi LoRa 32 V4 (ESP32-S3 + SX1262 + front-end GC1109)
  Test de mensajeria punto a punto con boton PRG y pantalla OLED.

  Mismo firmware para AMBOS nodos: cualquiera puede enviar o recibir.

  Comportamiento:
    - Estado normal:      "No hay nuevos mensajes"
    - Pulsa PRG -> envia mensaje -> muestra "Mensaje enviado"
    - Pulsa PRG otra vez -> vuelve a "No hay nuevos mensajes"
    - Al recibir algo -> muestra "Nuevo mensaje"
    - Pulsa PRG -> muestra el contenido y el RSSI/SNR del mensaje recibido
    - Pulsa PRG otra vez -> vuelve a "No hay nuevos mensajes"

  Librerias necesarias (Gestor de Librerias de Arduino):
    - RadioLib (jgromes)
    - U8g2 (olikraus)

  IMPORTANTE:
    - Conecta SIEMPRE la antena antes de transmitir.
    - Los pines del front-end (7, 2, 46) y el TCXO a 1.8 V estan puestos de
      memoria segun el variant de la V4 en Meshtastic: contrastalos con el
      pinout oficial de Heltec.
    - Con el PA del GC1109 la potencia real puede ser mayor que LORA_POWER.
      Respeta el limite legal de tu region (EU868: 14 dBm PIRE, 1 % duty cycle).
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

// ---------- Front-end de RF (GC1109) ----------
#define FEM_POWER  7    // Alimentacion del front-end (VFEM)
#define FEM_CSD    2    // Habilita el chip
#define FEM_CPS    46   // Modo PA (alta potencia) -> solo durante TX

// ---------- Pines OLED ----------
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21

// ---------- Boton PRG ----------
#define BUTTON_PIN 0   // GPIO0, activo en LOW, con pull-up interno

// ---------- Parametros de radio (ajusta segun tu region) ----------
#define LORA_FREQ       868.0   // MHz (EU868). Cambia a 915.0 si estas en US915, etc.
#define LORA_BW         125.0   // kHz
#define LORA_SF         8       // Spreading factor (sube a 10-12 para mas alcance)
#define LORA_CR         8       // Coding rate 4/8
#define LORA_SYNCWORD   0x12    // Privado (no usar 0x34, es el de LoRaWAN)
#define LORA_POWER      14      // dBm
#define LORA_PREAMBLE   12
#define LORA_TCXO_V     1.8     // Tension del TCXO controlado por DIO3

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, OLED_RST);

// ---------- Maquina de estados ----------
enum AppState { ST_IDLE, ST_SENT, ST_RECEIVED, ST_VIEWING };
volatile AppState state = ST_IDLE;

String lastMessage = "";
float lastRssi = 0;
float lastSnr = 0;
uint16_t msgCounter = 0;

// Flags que se activan desde la interrupcion del radio (deben ser volatile)
volatile bool operationDone = false;
volatile bool isTransmitting = false;

// Debounce del boton
unsigned long lastButtonMs = 0;
const unsigned long DEBOUNCE_MS = 250;

bool needsRedraw = true;

// ---------- Interrupcion del radio (DIO1) ----------
void IRAM_ATTR onRadioAction() {
  operationDone = true;
}

// ---------- Control del front-end ----------
void femInit() {
  pinMode(FEM_POWER, OUTPUT);
  digitalWrite(FEM_POWER, HIGH);
  pinMode(FEM_CSD, OUTPUT);
  digitalWrite(FEM_CSD, HIGH);
  pinMode(FEM_CPS, OUTPUT);
  digitalWrite(FEM_CPS, LOW);   // modo RX por defecto
  delay(10);
}

void femTx(bool tx) {
  digitalWrite(FEM_CPS, tx ? HIGH : LOW);
}

void showFatal(const char* msg, int code) {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 20, msg);
  u8g2.drawStr(0, 35, String(code).c_str());
  u8g2.sendBuffer();
  Serial.printf("%s codigo: %d\n", msg, code);
  while (true) { delay(1000); }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // OLED
  u8g2.begin();
  u8g2.setFont(u8g2_font_ncenB08_tr);

  // Front-end de RF encendido ANTES de iniciar el radio
  femInit();

  // SPI del radio con pines manuales
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  int st = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                       LORA_SYNCWORD, LORA_POWER, LORA_PREAMBLE, LORA_TCXO_V);
  if (st != RADIOLIB_ERR_NONE) showFatal("Error radio:", st);

  // DIO2 controla el conmutador TX/RX del GC1109
  st = radio.setDio2AsRfSwitch(true);
  if (st != RADIOLIB_ERR_NONE) showFatal("Error DIO2:", st);

  // Mas sensibilidad en recepcion (~+3 dB)
  radio.setRxBoostedGainMode(true);

  radio.setDio1Action(onRadioAction);
  startListening();

  Serial.println("Radio listo. Esperando mensajes...");
  needsRedraw = true;
}

void startListening() {
  femTx(false);
  operationDone = false;
  isTransmitting = false;
  radio.startReceive();
}

void sendMessage() {
  msgCounter++;
  String payload = "Ping #" + String(msgCounter);

  operationDone = false;
  isTransmitting = true;

  femTx(true);
  int result = radio.transmit(payload);
  femTx(false);

  if (result == RADIOLIB_ERR_NONE) {
    Serial.println("Mensaje enviado: " + payload);
    state = ST_SENT;
  } else {
    Serial.print("Error al enviar, codigo: ");
    Serial.println(result);
    state = ST_IDLE;
  }

  // Tras transmit() hay que volver a modo escucha
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
    lastRssi = radio.getRSSI();
    lastSnr = radio.getSNR();
    state = ST_RECEIVED;
    needsRedraw = true;
    Serial.println("Mensaje recibido: " + received);
    Serial.printf("RSSI: %.1f dBm  SNR: %.1f dB\n", lastRssi, lastSnr);
  } else {
    // CRC_MISMATCH indica que llega senal, pero demasiado debil o con ruido
    Serial.printf("Error readData: %d\n", result);
  }

  // Volvemos a escuchar para el siguiente paquete
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

    case ST_VIEWING: {
      char buf[32];
      u8g2.drawStr(0, 12, "Mensaje recibido:");
      u8g2.drawStr(0, 28, lastMessage.c_str());
      snprintf(buf, sizeof(buf), "RSSI: %.0f dBm", lastRssi);
      u8g2.drawStr(0, 44, buf);
      snprintf(buf, sizeof(buf), "SNR: %.1f dB", lastSnr);
      u8g2.drawStr(0, 58, buf);
      break;
    }
  }

  u8g2.sendBuffer();
}

void loop() {
  checkIncoming();
  handleButton();
  updateDisplay();
}
