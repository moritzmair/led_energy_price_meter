#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include "config.h"
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#define LED_PIN 5           // GPIO 5 (D5 on NodeMCU)
#define NUM_LEDS 72         // Number of LEDs in the strip

uint32_t colors[NUM_LEDS]; // Store colors for each LED

// Initialize NeoPixel strip
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600 * 1); // Berlin time zone (UTC +1)

void setup() {
  Serial.begin(115200);
  delay(10);

  strip.begin();
  strip.setBrightness(10);
  strip.show(); // Initialize all pixels to 'off'

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    rainbowCycle(10); // Show rainbow animation
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected!");

  // Initialize time client
  timeClient.begin();
  timeClient.update();
  adjustTimeForDST(); // Adjust for Daylight Saving Time
  fetchAndDisplayData(); // Initial data fetch
}

void loop() {
  static int lastHour = -1;
  static int lastDay = -1;
  static uint32_t lastColor = 0;

  timeClient.update();
  adjustTimeForDST(); // Adjust for Daylight Saving Time
  int currentHour = timeClient.getHours();
  int currentDay = timeClient.getDay();

  // Check if a new day has started
  if (currentDay != lastDay) {
    fetchAndDisplayData();
    lastDay = currentDay;
  }

  // Check if the hour has changed
  if (currentHour != lastHour) {
    lastHour = currentHour;
    lastColor = colors[currentHour]; // Get the color for the current hour
  }

  // Blink the LED for the current hour
  blinkCurrentHourLED(currentHour, lastColor);
}

void fetchAndDisplayData() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;  // Create a secure WiFi client
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(15000);
    Serial.print("making request to: ");
    Serial.println(api_url);
    http.begin(client, api_url);  // Pass both the WiFi client and the API URL

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println(payload);

      DynamicJsonDocument doc(4096);
      DeserializationError error = deserializeJson(doc, payload);

      if (error) {
        Serial.print("JSON deserialization failed: ");
        Serial.println(error.c_str());
        return;
      }

      JsonArray array = doc.as<JsonArray>();
      float minPrice = array[0];
      float maxPrice = array[0];

      // Find min and max prices
      for (float price : array) {
        if (price < minPrice) minPrice = price;
        if (price > maxPrice) maxPrice = price;
      }

      int index = 0;
      for (float price : array) {
        if (index >= NUM_LEDS) break;  // Only display the first 72 values

        Serial.print("Price for LED ");
        Serial.print(index);
        Serial.print(": ");
        Serial.println(price);

        // Map the price to a color using min and max prices
        uint32_t color = priceToColor(price, minPrice, maxPrice);
        strip.setPixelColor(index, color);
        colors[index] = color; // Store the color
        index++;
      }

      strip.show(); // Display all pixels
    } else {
      Serial.print("HTTP request failed with code: ");
      Serial.println(httpCode);
    }

    http.end();
  } else {
    Serial.println("WiFi not connected");
  }
}

uint32_t priceToColor(float price, float minPrice, float maxPrice) {
  // Define colors for thresholds
  uint8_t red, green, blue;

  if (price <= minPrice) {
    // Green for min price
    red = 0;
    green = 255;
    blue = 0;
  } else if (price >= maxPrice) {
    // Red for max price
    red = 255;
    green = 0;
    blue = 0;
  } else {
    // Gradient between green and red for in-between prices
    float ratio = (price - minPrice) / (maxPrice - minPrice);
    red = ratio * 255;
    green = (1 - ratio) * 255;
    blue = 0;
  }

  return strip.Color(red, green, blue);
}

void blinkCurrentHourLED(int hour, uint32_t color) {
  static bool isFadingIn = true;
  static unsigned long lastUpdateTime = 0;
  static uint8_t brightness = 0;
  unsigned long currentTime = millis();

  if (currentTime - lastUpdateTime >= 20) { // Update every 20ms for smooth fading
    if (isFadingIn) {
      brightness += 5;
      if (brightness >= 255) {
        brightness = 255;
        isFadingIn = false;
      }
    } else {
      brightness -= 5;
      if (brightness <= 0) {
        brightness = 0;
        isFadingIn = true;
      }
    }

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    strip.setPixelColor(hour, strip.Color(r * brightness / 255, g * brightness / 255, b * brightness / 255));
    strip.show();
    lastUpdateTime = currentTime;
  }
}

void adjustTimeForDST() {
  // Calculate if DST is in effect
  time_t now = timeClient.getEpochTime();
  struct tm * timeinfo = localtime(&now);

  // DST starts last Sunday in March at 2:00 AM
  if (timeinfo->tm_mon > 2 && timeinfo->tm_mon < 9) {
    timeClient.setTimeOffset(3600 * 2); // DST in effect
  } else if (timeinfo->tm_mon == 2) {
    int lastSunday = (31 - (5 + timeinfo->tm_year * 5 / 4) % 7);
    if (timeinfo->tm_mday > lastSunday || (timeinfo->tm_mday == lastSunday && timeinfo->tm_hour >= 2)) {
      timeClient.setTimeOffset(3600 * 2); // DST in effect
    } else {
      timeClient.setTimeOffset(3600 * 1); // Standard time
    }
  } else if (timeinfo->tm_mon == 9) {
    int lastSunday = (31 - (5 + timeinfo->tm_year * 5 / 4) % 7);
    if (timeinfo->tm_mday < lastSunday || (timeinfo->tm_mday == lastSunday && timeinfo->tm_hour < 3)) {
      timeClient.setTimeOffset(3600 * 2); // DST in effect
    } else {
      timeClient.setTimeOffset(3600 * 1); // Standard time
    }
  } else {
    timeClient.setTimeOffset(3600 * 1); // Standard time
  }
}

void rainbowCycle(int wait) {
  uint16_t i, j;

  for (j = 0; j < 256 * 5; j++) { // 5 cycles of all colors on wheel
    for (i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
    }
    strip.show();
    delay(wait);
  }
}

uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}