#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <esp32cam.h>

#define I2C_SDA_PIN 15
#define I2C_SCL_PIN 14
#define SCREEN_WIDTH 128 // Lebar layar OLED
#define SCREEN_HEIGHT 64 // Tinggi layar OLED
#define OLED_RESET    -1 // Pin reset, -1 jika tidak digunakan
#define SCREEN_ADDRESS 0x3C // Alamat I2C layar OLED

const int sensorPin = 13; // Pin analog sensor pulsa terhubung ke pin 13
int nilaiSensor;
int detakJantung;
const int numReadings = 10; // Jumlah pembacaan untuk filter sederhana
int readings[numReadings]; // Array untuk menyimpan pembacaan
int readIndex = 0; // Indeks pembacaan saat ini
int total = 0; // Total semua pembacaan
int average = 0; // Nilai rata-rata

// Membuat objek untuk tampilan OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Konfigurasi WiFi dan NTPClient
const char* ssid = "R-201-LAB";
const char* password = "smkhebat";
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // Offset GMT 0, update setiap 60 detik

// WebServer untuk ESP32 CAM
WebServer server(80);

static auto loRes = esp32cam::Resolution::find(320, 240);
static auto midRes = esp32cam::Resolution::find(350, 530);
static auto hiRes = esp32cam::Resolution::find(800, 600);

unsigned long previousMillisTime = 0;
unsigned long previousMillisSensor = 0;
unsigned long previousMillisPost = 0;
const long intervalTime = 1000; // Interval untuk update waktu (1 detik)
const long intervalSensor = 60000; // Interval untuk update sensor (30 detik)
const long intervalPost = 60000; // Interval untuk mengirim data ke API (30 detik)

void serveJpg()
{
  auto frame = esp32cam::capture();
  if (frame == nullptr) {
    Serial.println("CAPTURE FAIL");
    server.send(503, "", "");
    return;
  }
  Serial.printf("CAPTURE OK %dx%d %db\n", frame->getWidth(), frame->getHeight(),
                static_cast<int>(frame->size()));

  server.setContentLength(frame->size());
  server.send(200, "image/jpeg");
  WiFiClient client = server.client();
  frame->writeTo(client);
}

void handleJpgLo()
{
  if (!esp32cam::Camera.changeResolution(loRes)) {
    Serial.println("SET-LO-RES FAIL");
  }
  serveJpg();
}

void handleJpgHi()
{
  if (!esp32cam::Camera.changeResolution(hiRes)) {
    Serial.println("SET-HI-RES FAIL");
  }
  serveJpg();
}

void handleJpgMid()
{
  if (!esp32cam::Camera.changeResolution(midRes)) {
    Serial.println("SET-MID-RES FAIL");
  }
  serveJpg();
}

void setup() {
  Serial.begin(115200);

  // Inisialisasi tampilan OLED
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("Gagal memulai tampilan OLED"));
    for (;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setRotation(3);

  // Inisialisasi array pembacaan
  for (int i = 0; i < numReadings; i++) {
    readings[i] = 0;
  }

  // Inisialisasi ESP32 CAM
  using namespace esp32cam;
  Config cfg;
  cfg.setPins(pins::AiThinker);
  cfg.setResolution(hiRes);
  cfg.setBufferCount(2);
  cfg.setJpeg(80);

  bool ok = Camera.begin(cfg);
  Serial.println(ok ? "CAMERA OK" : "CAMERA FAIL");

  // Koneksi ke WiFi
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  Serial.print("http://");
  Serial.println(WiFi.localIP());
  Serial.println("  /cam-lo.jpg");
  Serial.println("  /cam-hi.jpg");
  Serial.println("  /cam-mid.jpg");

  server.on("/cam-lo.jpg", handleJpgLo);
  server.on("/cam-hi.jpg", handleJpgHi);
  server.on("/cam-mid.jpg", handleJpgMid);

  server.begin();

  // Inisialisasi NTPClient
  timeClient.begin();
}

void loop() {
  unsigned long currentMillis = millis();

  // Update waktu setiap 1 detik
  if (currentMillis - previousMillisTime >= intervalTime) {
    previousMillisTime = currentMillis;

    // Mendapatkan waktu dari NTPClient
    timeClient.update();
    String formattedTime = timeClient.getFormattedTime();

    // Memisahkan jam dan menit
    String hours = formattedTime.substring(0, 2);
    String minutes = formattedTime.substring(3, 5);

    // Menampilkan jam di OLED
    display.clearDisplay();
    display.setTextSize(4);

    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(hours, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_HEIGHT - h) / 3, (SCREEN_WIDTH - w) / 3); // Tengah atas layar
    display.print(hours);

    int verticalSpacing = 34;

    display.setTextSize(4);
    display.getTextBounds(minutes, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_HEIGHT - h) / 3, (SCREEN_WIDTH - w) / 3 + verticalSpacing);
    display.print(minutes);

    // Menampilkan nilai sensor dan detak jantung di pojok kiri bawah
    display.setTextSize(1); // Ukuran teks untuk nilai sensor
    display.setCursor((SCREEN_HEIGHT - h) / 5, SCREEN_WIDTH - 30); // Pojok kiri bawah
    display.print("Detak:");
    display.println(detakJantung);
    display.display(); // Tampilkan perubahan
  }

  // Update sensor setiap 30 detik
  if (currentMillis - previousMillisSensor >= intervalSensor) {
    previousMillisSensor = currentMillis;

    total = total - readings[readIndex];
    readings[readIndex] = analogRead(sensorPin);
    total = total + readings[readIndex];
    readIndex = readIndex + 1;

    if (readIndex >= numReadings) {
      readIndex = 0;
    }

    average = total / numReadings;
    detakJantung = map(average, 0, 1023, 80, 150);

    // Tampilkan detak jantung di Serial Monitor
    Serial.print("Detak Jantung: ");
    Serial.println(detakJantung);
  }

  // Kirim data ke API setiap 30 detik
  if (currentMillis - previousMillisPost >= intervalPost) {
    previousMillisPost = currentMillis;

    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin("https://besthy-be.vercel.app/detak");
      http.addHeader("Content-Type", "application/json");

      String jsonData = "{\"detak\": " + String(detakJantung) + ", \"userId\": \"clz195fts0000prvu1vk73scx\"}";
      int httpResponseCode = http.POST(jsonData);

      if (httpResponseCode > 0) {
        Serial.print("HTTP Response code: ");
        Serial.println(httpResponseCode);
      } else {
        Serial.print("Error code: ");
        Serial.println(httpResponseCode);
      }

      http.end();
    } else {
      Serial.println("WiFi Disconnected");
    }
  }

  server.handleClient(); // Menangani permintaan HTTP dari klien
}
