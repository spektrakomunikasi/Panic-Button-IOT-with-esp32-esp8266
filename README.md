# Panic Button IoT (ESP32 Master + ESP8266 Slave)

Sistem panic button nirkabel:
- **Master**: ESP32 + LCD 20x4 I2C + buzzer/siren
- **Slave**: ESP8266 + tombol panic
- Komunikasi via HTTP (`/api/event`) di jaringan WiFi yang sama.

## Fitur Utama

- Master punya **AP Setup** untuk isi WiFi + nama site.
- Slave punya **AP Setup** untuk isi WiFi + IP master + slave ID + lokasi.
- Event:
  - `PANIC` (tekan singkat tombol slave)
  - `CLEAR` (tahan 5 detik)
  - `HEARTBEAT` periodik
- Slave low-latency: retry event cepat agar respons tidak perlu pencet berulang.

---

## Struktur Folder

- `master_esp32/master_esp32.ino`
- `slave_esp8266/slave_esp8266_low_latency.ino`
- `docs/wiring-diagram.md`
- `docs/api.md`

---

## Wiring Ringkas

Lihat detail di: `docs/wiring-diagram.md`

### Master ESP32
- LCD SDA: GPIO21
- LCD SCL: GPIO22
- Buzzer driver: GPIO25
- ACK button: GPIO26 (opsional)
- Config button: GPIO27 (opsional)

### Slave ESP8266
- Panic button: D5 / GPIO14
- LED status: D6 / GPIO12 (opsional)
- Config button: D3 / GPIO0 (opsional, hold 5 detik reset config)

---

## Cara Upload

## 1) Master (ESP32)
Upload:
- `master_esp32/master_esp32.ino`

Jika belum ada config, master akan membuat AP:
- SSID: `PANIC-MASTER-xxxx`
- Password: `12345678`
- Setup page: `http://192.168.4.1`

Isi:
- SSID WiFi
- Password WiFi
- Nama Site

## 2) Slave (ESP8266)
Upload:
- `slave_esp8266/slave_esp8266_low_latency.ino`

Jika belum ada config / gagal konek WiFi:
- SSID: `PANIC-SLAVE-xxxx`
- Password: `12345678`
- Setup page: `http://192.168.4.1`

Isi:
- SSID WiFi
- Password WiFi
- IP Master (contoh `192.168.1.50`)
- Slave ID (1..3)
- Nama lokasi

---

## Komisioning Cepat

1. Nyalakan master, setup WiFi sampai dapat IP.
2. Catat IP master (LCD / serial / router DHCP list).
3. Setup setiap slave (IP master harus benar).
4. Uji tombol panic:
   - Tekan singkat -> alarm aktif di master
   - Tahan 5 detik -> clear

---

## Catatan

- Pastikan master dan slave di subnet yang sama.
- Router 2.4GHz yang lemah bisa menambah latency.
- Versi slave low-latency sudah menambah retry event cepat.
