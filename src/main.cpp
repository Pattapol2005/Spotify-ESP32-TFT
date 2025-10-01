#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <JPEGDecoder.h>
#include <SPIFFS.h>
#include <mbedtls/base64.h>

TFT_eSPI tft = TFT_eSPI();

int progress = 0, duration = 0;
unsigned long lastTick = 0;
bool firstUpdate = true;
int lastFillW = 0;

String lastTrack = "";
String lastArtist = "";

void drawCenterText(const String &txt, int y, int size = 2) {
  tft.setTextSize(size);
  int16_t tw = tft.textWidth(txt);
  int16_t x = (tft.width() - tw) / 2;
  tft.setCursor(x, y);
  tft.print(txt);
}

int base64Decode(const String &b64, uint8_t *output, size_t outLen) {
  size_t olen = 0;
  int ret = mbedtls_base64_decode(output, outLen, &olen,
                                  (const unsigned char*)b64.c_str(),
                                  b64.length());
  if (ret == 0) return olen;
  else return -1;
}

void drawAlbumFromBase64(const String &b64) {
  int bufSize = b64.length() * 3 / 4;
  uint8_t *buf = (uint8_t*)malloc(bufSize);
  if (!buf) {
    Serial.println("malloc fail");
    return;
  }

  int decodedLen = base64Decode(b64, buf, bufSize);
  Serial.printf("Base64 length: %d, decodedLen: %d\n", b64.length(), decodedLen);

  if (decodedLen > 0) {
    File f = SPIFFS.open("/album.jpg", FILE_WRITE);
    if (!f) {
      Serial.println("SPIFFS open fail");
      free(buf);
      return;
    }
    f.write(buf, decodedLen);
    f.close();
    Serial.println("JPEG file written to SPIFFS");
  }
  free(buf);

  if (JpegDec.decodeFsFile("/album.jpg")) {
    Serial.printf("JPEG decode OK: %dx%d\n", JpegDec.width, JpegDec.height);

    int centerX = (tft.width() - JpegDec.width) / 2;
    int topY = 10;

    tft.setSwapBytes(true);
    while (JpegDec.read()) {
      int mcuX = JpegDec.MCUx * JpegDec.MCUWidth + centerX;
      int mcuY = JpegDec.MCUy * JpegDec.MCUHeight + topY;
      int winW = JpegDec.MCUWidth;
      int winH = JpegDec.MCUHeight;
      if ((mcuX + winW) <= tft.width() && (mcuY + winH) <= tft.height()) {
        tft.pushImage(mcuX, mcuY, winW, winH, JpegDec.pImage);
      }
    }
    tft.setSwapBytes(false);
  } else {
    Serial.println("JPEG decode fail");
  }
}

void setup() {
  Serial.setRxBufferSize(32768);
  Serial.begin(921600);
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
  }

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  drawCenterText("Waiting Spotify...", 150, 2);
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    size_t capacity = line.length() + 2048;
    DynamicJsonDocument doc(capacity);

    DeserializationError error = deserializeJson(doc, line);
    if (error) {
      Serial.print("JSON error: ");
      Serial.println(error.c_str());
      return;
    }

    String track = doc["track"] | "Unknown";
    String artist = doc["artist"] | "Unknown Artist";
    progress = doc["progress"] | 0;
    duration = doc["duration"] | 0;

    if (firstUpdate) {
      tft.fillScreen(TFT_BLACK); 
      firstUpdate = false;
      lastFillW = 0;  
    }

    if (doc.containsKey("albumArt_b64")) {
      String artB64 = doc["albumArt_b64"].as<String>();
      Serial.println("Got albumArt_b64, decoding...");
      drawAlbumFromBase64(artB64);
      doc.remove("albumArt_b64");
      lastFillW = 0;
    }

    if (track != lastTrack || artist != lastArtist) {
      tft.fillRect(0, 220, 480, 100, TFT_BLACK);
      drawCenterText(track, 220, 2);
      drawCenterText(artist, 240, 2);
      lastTrack = track;
      lastArtist = artist;
    }
  }

  if (millis() - lastTick > 1000) {
    lastTick = millis();
    if (duration > 0) {
      progress += 1000;
      if (progress > duration) progress = duration;

      int barX = 40, barY = 270, barW = 400, barH = 14;
      float frac = (float)progress / duration;
      int fillW = (int)(barW * frac);

      if (lastFillW == 0 || fillW < lastFillW) {
        tft.drawRect(barX, barY, barW, barH, TFT_WHITE);
        tft.fillRect(barX + 1, barY + 1, barW - 2, barH - 2, TFT_BLACK);
      }

      if (fillW > lastFillW) {
        tft.fillRect(barX + lastFillW + 1, barY + 1,
                     fillW - lastFillW, barH - 2, TFT_GREEN);
      }

      lastFillW = fillW;

      tft.fillRect(barX, barY + barH + 4, barW, 20, TFT_BLACK);

      char buf[16];
      int curSec = progress / 1000;
      int durSec = duration / 1000;

      sprintf(buf, "%02d:%02d", curSec / 60, curSec % 60);
      tft.setCursor(barX, barY + barH + 6);
      tft.print(buf);

      sprintf(buf, "%02d:%02d", durSec / 60, durSec % 60);
      int tw = tft.textWidth(buf);
      tft.setCursor(barX + barW - tw, barY + barH + 6);
      tft.print(buf);
    }
  }
}
