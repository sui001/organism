#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN     14
#define WIDTH       8
#define HEIGHT      8
#define LED_COUNT   (WIDTH * HEIGHT)
#define PHOTO_PIN   1          // photoresistor input

// Adjust these to match your sensor readings
int minLight = 2200;   // darkest reading
int maxLight = 4050;   // brightest reading
int THRESHOLD = 2500;  // blink starts when below this

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

unsigned long lastToggle = 0;
unsigned long interval = 1000; // current ON/OFF duration
bool ledOn = false;
int currentPixel = 0;
uint32_t currentColor = 0;

// Convert x,y to strip index (assuming straight rows)
int XYtoIndex(int x, int y) {
  return y * WIDTH + x;
}

void setup() {
  strip.begin();
  strip.setBrightness(127); // 50% brightness
  strip.show();
  randomSeed(analogRead(0));
  Serial.begin(115200);
}

void loop() {
  int lightLevel = analogRead(PHOTO_PIN); // 0–4095
  Serial.print("LDR: ");
  Serial.println(lightLevel);

  // Map light level to 0–8 LEDs for the top row as visual debug
  int litCount = map(lightLevel, minLight, maxLight, 0, 8);
  litCount = constrain(litCount, 0, 8);

  // Clear display first
  strip.clear();

  // Show brightness on the top row (blue)
  for (int x = 0; x < WIDTH; x++) {
    if (x < litCount) {
      strip.setPixelColor(XYtoIndex(x, 0), strip.Color(0, 0, 255));
    }
  }

  unsigned long now = millis();
  if (lightLevel < THRESHOLD) { // dark enough → blinking allowed
    if (now - lastToggle >= interval) {
      lastToggle = now;

      if (ledOn) {
        // Switch OFF phase
        ledOn = false;
        interval = random(1000, 5001); // 1–5 sec OFF
      } else {
        // Switch ON phase
        ledOn = true;
        interval = random(500, 2001); // 0.5–2 sec ON

        // Pick a random vivid colour
        int mode = random(6);
        int r = 0, g = 0, b = 0;
        switch (mode) {
          case 0: r = random(150, 256); g = random(20, 100); b = 0; break;   // warm red-orange
          case 1: r = random(20, 100); g = random(150, 256); b = 0; break;   // green
          case 2: r = 0; g = random(20, 100); b = random(150, 256); break;   // blue-cyan
          case 3: r = random(150, 256); g = 0; b = random(20, 100); break;   // magenta
          case 4: r = random(20, 100); g = random(150, 256); b = random(20, 100); break; // green with tint
          case 5: r = random(150, 256); g = random(150, 256); b = 0; break;  // yellow-gold
        }
        currentColor = strip.Color(r, g, b);

        // Pick a random pixel (avoid row 0, so y >= 1)
        int y = random(1, HEIGHT);  // 1–7
        int x = random(0, WIDTH);   // 0–7
        currentPixel = XYtoIndex(x, y);
      }
    } 
    // During ON phase → keep showing the lit pixel
    if (ledOn) {
      strip.setPixelColor(currentPixel, currentColor);
    }
  } else {
    // Too bright → force all random LEDs off
    ledOn = false;
  }

  strip.show();
}
