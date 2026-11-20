# Tanaman Disiram Sendiri — ESP32 + Sensor Kelembapan Tanah + Pompa Air

Pernah pulang kerja dan tanamanmu layu karena lupa disiram? Alat kecil ini
menyiram tanamanmu sendiri: sensor kelembapan tanah membaca tanah, begitu
kering, ESP32 menyalakan pompa air lewat relay — dan kamu dapat kabar di
Telegram.

Video penjelasan (60 detik):
https://youtube.com/shorts/UFZ7Zlu3q0I

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat |
| Sensor kelembapan tanah | Keluaran analog (AO), probe ke tanah |
| Modul relay 5V | Saklar pompa air (kontak NO-COM) |
| Pompa air mini 5V / 12V | Disuplai catu luar lewat relay |
| LED hijau | Menyala saat tanah lembap |
| LED merah | Menyala saat tanah kering / pompa menyala |
| Tombol | Siram manual 5 detik (INPUT_PULLUP) |

## Wiring

| Dari | Ke |
|---|---|
| Sensor AO | ESP32 GPIO34 |
| Sensor VCC | ESP32 3V3 |
| Relay IN | ESP32 GPIO26 |
| Relay VCC | 5V (bukan dari pin ESP32) |
| Pompa | Catu luar lewat kontak relay NO-COM |
| LED hijau | ESP32 GPIO32 (seri 220 Ω ke GND) |
| LED merah | ESP32 GPIO33 (seri 220 Ω ke GND) |
| Tombol | ESP32 GPIO14 → GND |
| Semua GND | Digabung |

Skema lengkap: [schematic.png](schematic.png)

## Cara kerja

1. ESP32 membaca sensor tiap 5 detik; tiap pembacaan adalah rata-rata
   dari 5 sampel ADC (0–4095; nilai tinggi = tanah kering).
2. Rata-rata di atas ambang kering (2500) → pompa menyala lewat relay,
   maksimal 10 detik, lalu mati sendiri; notifikasi Telegram terkirim
   (bila `USE_TELEGRAM` 1 dan WiFi tersambung).
3. Tanah sudah lembap → pompa mati, LED hijau menyala, LED merah padam.
4. Tekan tombol → siram manual 5 detik (tanpa menunggu sensor).
   Tekan lagi saat pompa manual menyala → pompa berhenti.
5. Pompa yang menyala lebih dari 10 detik dimatikan paksa (pengaman).

## Konfigurasi (di awal sketch)

- `USE_TELEGRAM` (1) — kirim notifikasi Telegram. Set 0 untuk tanpa Telegram.
- `WIFI_SSID` / `WIFI_PASS` — WiFi rumahmu.
- `BOT_TOKEN` / `CHAT_ID` — token bot dan chat ID Telegram.
- `AMBANG_KERING` (2500) — ADC di atas ini dianggap kering. Sesuaikan
  dengan tanah dan sensormu: baca nilai ADC di Serial Monitor saat tanah
  basah dan saat kering, lalu ambil nilai di antaranya.
- `INTERVAL_BACA_MS` (5000) — baca sensor tiap 5 detik.
- `POMPA_MAKS_MS` (10000) — pompa otomatis maks 10 detik.
- `SIRAM_MANUAL_MS` (5000) — siram manual 5 detik.
- `JUMLAH_SAMPEL` (5) — sampel ADC yang dirata-rata per pembacaan.

## Library yang dibutuhkan

| Library | Versi | Keterangan |
|---|---|---|
| UniversalTelegramBot | 1.3.0 | Notifikasi Telegram |
| ArduinoJson | 6.21.5 | Dibutuhkan UniversalTelegramBot (WAJIB v6 — v7 tidak dipakai di sini) |

Kedua library di atas juga tersedia sebagai ZIP di folder [`libraries/`](libraries/)
— install lewat Arduino IDE: Sketch → Include Library → Add .ZIP Library.

## Compile

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 TanamanDisiram
```

Terverifikasi 20 Nov 2026: sketch 1.071.741 byte (81% flash),
RAM 50.608 byte (15%), ESP32 core 4.0.0.

## Lisensi

MIT — lihat [LICENSE](LICENSE).
