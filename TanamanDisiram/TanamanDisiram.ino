// Tanaman Disiram Sendiri — ESP32 + Sensor Kelembapan Tanah + Pompa Air
//
// Cara kerja:
//  - Sensor kelembapan tanah (keluaran analog) dibaca tiap 5 detik;
//    tiap pembacaan adalah rata-rata dari 5 sampel ADC.
//  - Tanah kering (nilai ADC di atas ambang) -> pompa air menyala lewat
//    relay, maksimal 10 detik, lalu mati sendiri. Notifikasi dikirim
//    ke Telegram bila USE_TELEGRAM 1 dan WiFi tersambung.
//  - Tanah lembap -> pompa mati, LED hijau menyala, LED merah padam.
//  - Tombol siram manual (tekan) -> pompa menyala 5 detik, tanpa
//    menunggu sensor. Tekan lagi saat pompa manual menyala = berhenti.
//
// Wiring:
//  - Sensor kelembapan tanah : AO  -> GPIO34, VCC -> 3V3, GND -> GND
//    (sensor umum: nilai ADC TINGGI = tanah KERING, RENDAH = tanah BASAH;
//     kalau sensormu terbalik, tukar AMBANG_KERING dan logika di bawah)
//  - Relay pompa air         : IN  -> GPIO26, VCC -> 5V, GND -> GND
//    (modul relay aktif HIGH; pompa 5V/12V disuplai dari catu luar
//     lewat kontak relay NO-COM — JANGAN dari pin ESP32)
//  - LED hijau               : GPIO32 (seri resistor 220 ohm ke GND)
//  - LED merah                : GPIO33 (seri resistor 220 ohm ke GND)
//  - Tombol siram manual      : GPIO14 -> GND (INPUT_PULLUP, aktif LOW)
//
// Library yang dibutuhkan (semua tersedia di Library Manager):
//  - UniversalTelegramBot 1.3.0 (hanya bila USE_TELEGRAM 1)
//  - ArduinoJson 6.21.5 (dibutuhkan UniversalTelegramBot)
// Compile: arduino-cli compile --fqbn esp32:esp32:esp32 TanamanDisiram
// (terverifikasi 20 Nov 2026: 1.071.741 byte, 81% flash, RAM 50.608 byte,
//  15%, ESP32 core 4.0.0, COMPILE_RC=0)

// ---------- KONFIGURASI ----------
#define USE_TELEGRAM 1  // 1 = kirim notifikasi Telegram, 0 = tanpa Telegram

// Pin
#define PIN_SENSOR_TANAH 34   // ADC1, hanya input
#define PIN_RELAY_POMPA 26    // output ke modul relay (aktif HIGH)
#define PIN_LED_HIJAU 32      // tanah lembap
#define PIN_LED_MERAH 33      // tanah kering / pompa menyala
#define PIN_TOMBOL_SIRAM 14   // INPUT_PULLUP, aktif LOW

// Ambang dan waktu (semua bernama — tanpa magic number di logika)
const int AMBANG_KERING = 2500;        // ADC 0..4095; di atas ini = kering
const int JUMLAH_SAMPEL = 5;           // sampel ADC per pembacaan
const int JEDA_ANTAR_SAMPEL_MS = 100;  // jeda antar sampel ADC
const unsigned long INTERVAL_BACA_MS = 5000;      // baca sensor tiap 5 detik
const unsigned long POMPA_MAKS_MS = 10000;        // pompa otomatis maks 10 detik
const unsigned long SIRAM_MANUAL_MS = 5000;       // siram manual 5 detik
const unsigned long DEBOUNCE_MS = 50;             // debounce tombol
const unsigned long TIMEOUT_WIFI_MS = 20000;      // tunggu WiFi maks 20 detik

const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASS = "PASSWORD_WIFI";
const char *BOT_TOKEN = "TOKEN_BOT_TELEGRAM";
const char *CHAT_ID = "CHAT_ID_TELEGRAM";

#if USE_TELEGRAM
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

WiFiClientSecure telegramClient;
UniversalTelegramBot bot(BOT_TOKEN, telegramClient);

// Kirim satu pesan Telegram; diam bila WiFi putus.
void kirimTelegram(const String &pesan) {
  if (WiFi.status() != WL_CONNECTED) return;
  bot.sendMessage(CHAT_ID, pesan, "");
}
#else
void kirimTelegram(const String &) {}
#endif

// ---------- STATE ----------
bool pompaAktif = false;       // pompa sedang menyala
bool pompaManual = false;      // penyalaan berasal dari tombol (bukan sensor)
unsigned long pompaMulai = 0;  // millis() saat pompa dinyalakan
unsigned long bacaTerakhir = 0;
bool tombolDulu = HIGH;        // state tombol sebelumnya (INPUT_PULLUP)
unsigned long tombolBerubah = 0;

// ---------- FUNGSI ----------

// Baca kelembapan tanah: rata-rata dari JUMLAH_SAMPEL sampel ADC.
// Nilai ADC tinggi = tanah kering (sensor umum); rendah = tanah basah.
int bacaKelembapan() {
  long jumlah = 0;
  for (int i = 0; i < JUMLAH_SAMPEL; i++) {
    jumlah += analogRead(PIN_SENSOR_TANAH);
    delay(JEDA_ANTAR_SAMPEL_MS);
  }
  return (int)(jumlah / JUMLAH_SAMPEL);
}

// Nyalakan pompa; sumberManual = true bila dari tombol.
void nyalakanPompa(bool sumberManual) {
  if (pompaAktif) return;
  pompaAktif = true;
  pompaManual = sumberManual;
  pompaMulai = millis();
  digitalWrite(PIN_RELAY_POMPA, HIGH);
  digitalWrite(PIN_LED_MERAH, HIGH);
  digitalWrite(PIN_LED_HIJAU, LOW);
  Serial.print(sumberManual ? "Siram manual: pompa ON"
                            : "Tanah kering: pompa ON");
  Serial.println(" (maks 10 detik).");
}

// Matikan pompa dan kembalikan LED ke penanda kondisi tanah.
void matikanPompa(bool tanahKering) {
  if (!pompaAktif) return;
  pompaAktif = false;
  pompaManual = false;
  digitalWrite(PIN_RELAY_POMPA, LOW);
  digitalWrite(PIN_LED_MERAH, tanahKering ? HIGH : LOW);
  digitalWrite(PIN_LED_HIJAU, tanahKering ? LOW : HIGH);
  Serial.println("Pompa OFF.");
}

// Kendalikan pompa dari hasil baca sensor.
//  - Kering dan pompa belum nyala -> nyalakan + kabari Telegram.
//  - Lembap dan pompa otomatis nyala -> matikan.
//  - Pompa yang sudah menyala lebih dari POMPA_MAKS_MS -> matikan paksa.
void kendaliPompa(int rataRata) {
  bool tanahKering = rataRata > AMBANG_KERING;
  Serial.print("Kelembapan (ADC): ");
  Serial.print(rataRata);
  Serial.println(tanahKering ? " -> KERING" : " -> LEMBAP");

  if (pompaAktif) {
    // Pompa manual tidak diputus sensor; hanya timeout yang memutusnya.
    if (millis() - pompaMulai >= POMPA_MAKS_MS) {
      matikanPompa(tanahKering);
      kirimTelegram("Penyiraman selesai — pompa mati setelah 10 detik.");
    } else if (!pompaManual && !tanahKering) {
      matikanPompa(false);
      kirimTelegram("Tanah sudah lembap — pompa dimatikan.");
    }
    return;
  }

  if (tanahKering) {
    nyalakanPompa(false);
    kirimTelegram("Tanah kering (ADC " + String(rataRata) +
                  ") — pompa air menyala.");
  } else {
    digitalWrite(PIN_LED_HIJAU, HIGH);
    digitalWrite(PIN_LED_MERAH, LOW);
  }
}

// Tombol ditekan (aktif LOW, debounce): siram manual 5 detik,
// atau hentikan pompa bila sedang siram manual.
void cekTombol() {
  bool tombol = digitalRead(PIN_TOMBOL_SIRAM);
  if (tombol != tombolDulu) {
    tombolBerubah = millis();
    tombolDulu = tombol;
  }
  if (tombol == LOW && millis() - tombolBerubah >= DEBOUNCE_MS) {
    // Tunggu tombol dilepas supaya satu tekanan = satu perintah.
    static bool sudahDiproses = false;
    if (sudahDiproses) return;
    sudahDiproses = true;
    if (pompaAktif && pompaManual) {
      matikanPompa(true);
      kirimTelegram("Siram manual dihentikan dari tombol.");
    } else if (!pompaAktif) {
      nyalakanPompa(true);
      kirimTelegram("Siram manual dari tombol — pompa menyala 5 detik.");
    }
    while (digitalRead(PIN_TOMBOL_SIRAM) == LOW) delay(10);
    sudahDiproses = false;
  }
}

void sambungWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - mulai < TIMEOUT_WIFI_MS) {
    delay(250);
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "WiFi tersambung."
                                                : "WiFi gagal — lanjut tanpa Telegram.");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_RELAY_POMPA, OUTPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_TOMBOL_SIRAM, INPUT_PULLUP);
  digitalWrite(PIN_RELAY_POMPA, LOW);
  digitalWrite(PIN_LED_HIJAU, LOW);
  digitalWrite(PIN_LED_MERAH, LOW);

#if USE_TELEGRAM
  sambungWiFi();
  telegramClient.setInsecure();
#endif

  Serial.println("Tanaman Disiram Sendiri — siap.");
  Serial.print("Ambang kering (ADC): ");
  Serial.println(AMBANG_KERING);
}

void loop() {
  cekTombol();

  // Pompa manual berhenti sendiri setelah SIRAM_MANUAL_MS.
  if (pompaAktif && pompaManual &&
      millis() - pompaMulai >= SIRAM_MANUAL_MS) {
    int rataRata = bacaKelembapan();
    matikanPompa(rataRata > AMBANG_KERING);
    kirimTelegram("Siram manual selesai (5 detik).");
  }

  // Baca sensor tiap INTERVAL_BACA_MS.
  if (millis() - bacaTerakhir >= INTERVAL_BACA_MS) {
    bacaTerakhir = millis();
    int rataRata = bacaKelembapan();
    kendaliPompa(rataRata);
  }
}
