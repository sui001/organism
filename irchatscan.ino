#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <ESP32Servo.h>

// ----------------- PINS -----------------
#define LED_PIN     14
#define WIDTH       8
#define HEIGHT      8
#define LED_COUNT   (WIDTH * HEIGHT)

#define IR_LED_PIN  5
#define IR_RECV_PIN 6

#define SERVO_PIN   1
              
// ----------------- IR PWM -----------------
#define IR_CHANNEL  7      // Use channel 7 (servos typically use 0-5)
#define IR_FREQ     38000  // 38kHz carrier frequency
#define IR_RESOLUTION 8
#define IR_DUTY_CYCLE 128  // 50% duty cycle

// ----------------- NEOPIXEL -----------------
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ800);

// ----------------- TIMINGS -----------------
const unsigned long blinkDuration   = 1000;   // 1 second response blink
const unsigned long minGap         = 200;    // min gap between blinks
const unsigned long bootDelayMax   = 3000;   // random boot delay
const unsigned long pauseAfterIR   = 30000;  // 30 seconds silence before servo resumes
const unsigned long probeAfterIdle = 20000;  // 20 seconds idle, send probe
const int servoStepInterval        = 335;    // ~60s full sweep with 180 steps
const unsigned long pixelDanceInterval = 500; // Dance pixel moves every 500ms

// Random response delay to prevent sync - wider range
const unsigned long minResponseDelay = 200;   // minimum 200ms
const unsigned long maxResponseDelay = 1500;  // maximum 1500ms

// Conversation mode
bool inConversation = false;  // Track if actively talking

// ----------------- VARIABLES -----------------
unsigned long lastBlinkTime = 0;
unsigned long bootStartTime = 0;
unsigned long bootDelay = 0;
bool initialPulseDone = false;
bool ledOn = false;
unsigned long lastActionTime = 0;
unsigned long lastServoUpdate = 0;

unsigned long irDetectedTime = 0;  // When IR was detected
unsigned long randomResponseDelay = 0;  // Random delay before responding
bool waitingToRespond = false;  // Flag for delayed response

unsigned long lastPixelDance = 0;  // Last time waiting pixel moved
unsigned long lastIdleProbe = 0;   // Last time we sent an idle probe
bool idleProbeSent = false;        // Track if 20s probe already sent

// Conversation twinkle effect
unsigned long lastTwinkle = 0;
const unsigned long twinkleInterval = 1000;  // 1 second fade
const unsigned long waitingTwinkleInterval = 3000;  // 3 seconds for waiting mode (slower)
int twinklePixel = 0;
uint32_t twinkleColor = 0;
uint32_t nextTwinkleColor = 0;
unsigned long twinkleStartTime = 0;

// Servo smoothing
int targetServoAngle = 0;  // Where servo wants to go
int actualServoAngle = 0;  // Where servo currently is

int currentPixel = 0;
int purplePixel  = 0;

// Servo
Servo myServo;
int currentAngle = 0;
int targetAngle = 180;
bool sweepForward = true;

// Probe blink
bool probeBlinkOn = false;
int nextProbeAngle = 0;

// ----------------- HELPER FUNCTIONS -----------------
int XYtoIndex(int x, int y) { return y * WIDTH + x; }

uint32_t randomColor() {
  // Generate random bright colors
  int choice = random(0, 6);
  switch(choice) {
    case 0: return strip.Color(255, 0, 0);     // Red
    case 1: return strip.Color(0, 255, 0);     // Green
    case 2: return strip.Color(0, 0, 255);     // Blue
    case 3: return strip.Color(255, 255, 0);   // Yellow
    case 4: return strip.Color(0, 255, 255);   // Cyan
    case 5: return strip.Color(255, 0, 255);   // Magenta
    default: return strip.Color(255, 255, 255); // White
  }
}

uint32_t fadeColor(uint32_t color, float brightness) {
  uint8_t r = (uint8_t)((color >> 16) & 0xFF);
  uint8_t g = (uint8_t)((color >> 8) & 0xFF);
  uint8_t b = (uint8_t)(color & 0xFF);
  return strip.Color(r * brightness, g * brightness, b * brightness);
}

void showStatePixel(uint32_t color, bool withPurple) {
  strip.clear();
  strip.setPixelColor(currentPixel, color);
  if (withPurple) strip.setPixelColor(purplePixel, strip.Color(128,0,128));
  strip.show();
}

void sendBlink() {
  ledcWrite(IR_LED_PIN, IR_DUTY_CYCLE);  // Start 38kHz PWM (new API writes to pin)
  ledOn = true;
  lastBlinkTime = millis();
  lastActionTime = millis();  // Update action time when SENDING too
  
  // Show PURPLE when actually transmitting IR
  strip.clear();
  strip.setPixelColor(currentPixel, strip.Color(128, 0, 128)); // purple
  strip.show();
  
  // Randomize blink duration slightly (900-1100ms) to add more variation
  unsigned long variation = random(900, 1100);
  lastBlinkTime = millis() - (1000 - variation);  // Adjust for variable duration
  
  Serial.println("Responding with IR blink...");
}

void stopBlink() {
  ledcWrite(IR_LED_PIN, 0);  // Stop PWM
  ledOn = false;
  lastBlinkTime = millis();
  idleProbeSent = false;  // Reset idle probe flag when activity happens
  // DON'T update lastActionTime here - it should only update when IR is exchanged
  Serial.println("Waiting IR...");
  // Don't set a color here - let the twinkle animation handle it
}

void moveServoTo(int angle) {
  myServo.write(angle);
  Serial.print("Servo sweep pos: "); Serial.println(angle);
}

// ----------------- SETUP -----------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Attach servo FIRST so it claims its PWM channels
  myServo.attach(SERVO_PIN);
  moveServoTo(0);
  currentAngle = 0;
  targetServoAngle = 0;
  actualServoAngle = 0;
  
  // Now setup IR LED with 38kHz PWM (should auto-select free channel)
  if (!ledcAttach(IR_LED_PIN, IR_FREQ, IR_RESOLUTION)) {
    Serial.println("ERROR: Failed to attach IR LED PWM!");
  } else {
    Serial.println("IR LED PWM attached successfully");
  }
  ledcWrite(IR_LED_PIN, 0);  // Start off
  
  pinMode(IR_RECV_PIN, INPUT);

  // NeoPixel setup
  strip.begin();
  strip.setBrightness(76);
  strip.show();

  randomSeed(analogRead(0));
  bootDelay = random(500, bootDelayMax);
  bootStartTime = millis();

  currentPixel = XYtoIndex(random(0, WIDTH), random(0, HEIGHT));
  purplePixel  = XYtoIndex(random(0, WIDTH), random(0, HEIGHT));

  Serial.println("NeoPixel IR ping-pong ready. Waiting IR...");
  // Let the twinkle animation handle the display
  lastTwinkle = millis();
  twinkleStartTime = millis();
  twinklePixel = random(0, LED_COUNT);
}

// ----------------- LOOP -----------------
void loop() {
  unsigned long now = millis();
  int recv = digitalRead(IR_RECV_PIN);

  // ---- Initial random pulse if no IR ----
  if (!initialPulseDone && now - bootStartTime >= bootDelay) {
    Serial.println("No IR detected yet → sending initial pulse");
    sendBlink();
    initialPulseDone = true;
  }

  // ---- Detect IR and reply with random delay ----
  if (recv == LOW && !ledOn && !waitingToRespond && now - lastBlinkTime >= minGap) {
    Serial.println("IR detected!");
    irDetectedTime = now;
    randomResponseDelay = random(minResponseDelay, maxResponseDelay);
    waitingToRespond = true;
    lastActionTime = now;  // Reset idle timer
    idleProbeSent = false;  // Reset idle probe flag
    
    // Show ORANGE when IR detected (waiting to respond)
    strip.clear();
    strip.setPixelColor(currentPixel, strip.Color(255, 165, 0)); // orange
    strip.show();
    
    // Enter conversation mode
    if (!inConversation) {
      inConversation = true;
      Serial.println(">>> CONVERSATION STARTED <<<");
      // Initialize first twinkle
      twinklePixel = random(0, LED_COUNT);
      nextTwinkleColor = randomColor();
      lastTwinkle = now;
      twinkleStartTime = now;
    }
    
    Serial.print("Waiting ");
    Serial.print(randomResponseDelay);
    Serial.println("ms before responding...");
  }
  
  // ---- Send response after random delay ----
  if (waitingToRespond && now - irDetectedTime >= randomResponseDelay) {
    sendBlink();
    waitingToRespond = false;
  }
  
  // ---- Idle probe after 20 seconds of silence ----
  if (!ledOn && !inConversation && !idleProbeSent && now - lastActionTime >= probeAfterIdle) {
    Serial.println("20s idle → sending probe blink");
    ledcWrite(IR_LED_PIN, IR_DUTY_CYCLE);
    strip.setPixelColor(purplePixel, strip.Color(128, 0, 128));  // Purple flash
    strip.show();
    delay(800);  // Shorter probe
    ledcWrite(IR_LED_PIN, 0);
    idleProbeSent = true;  // Only send once per idle period
    lastPixelDance = now;  // Reset dance so it doesn't jump
    showStatePixel(strip.Color(255,255,0), false); // Back to yellow
  }
  
  // ---- Ultra slow red twinkle when waiting ----
  if (!ledOn && !inConversation) {
    // Start new twinkle cycle
    if (now - lastTwinkle >= waitingTwinkleInterval) {
      lastTwinkle = now;
      twinkleStartTime = now;
      twinklePixel = random(0, LED_COUNT);
    }
    
    // Calculate fade progress (0.0 to 1.0 and back)
    float progress = (float)(now - twinkleStartTime) / (float)waitingTwinkleInterval;
    float brightness;
    
    if (progress < 0.5) {
      // Fade in (0 to 0.1)
      brightness = progress * 0.2;  // Max 10% brightness
    } else {
      // Fade out (0.1 to 0)
      brightness = (1.0 - progress) * 0.2;
    }
    
    strip.clear();
    strip.setPixelColor(twinklePixel, strip.Color(255 * brightness, 0, 0)); // Red at 10%
    strip.show();
  }
  
  // ---- Twinkling rainbow during conversation ----
  if (inConversation && !ledOn && !waitingToRespond) {
    // Start new twinkle cycle
    if (now - lastTwinkle >= twinkleInterval) {
      lastTwinkle = now;
      twinkleStartTime = now;
      twinklePixel = random(0, LED_COUNT);
      twinkleColor = nextTwinkleColor;
      nextTwinkleColor = randomColor();
    }
    
    // Calculate fade progress (0.0 to 1.0 and back)
    float progress = (float)(now - twinkleStartTime) / (float)twinkleInterval;
    float brightness;
    
    if (progress < 0.5) {
      // Fade in (0 to 1)
      brightness = progress * 2.0;
    } else {
      // Fade out (1 to 0)
      brightness = (1.0 - progress) * 2.0;
    }
    
    strip.clear();
    strip.setPixelColor(twinklePixel, fadeColor(nextTwinkleColor, brightness));
    strip.show();
  }

  // ---- Handle blink duration ----
  if (ledOn && now - lastBlinkTime >= blinkDuration) {
    stopBlink();
  }

  // ---- Servo sweep if idle > pauseAfterIR ----
  // Only sweep if NOT in conversation OR conversation has been silent for 30 seconds
  if (!ledOn && now - lastActionTime >= pauseAfterIR) {
    // Exit conversation mode after long silence
    if (inConversation) {
      inConversation = false;
      Serial.println(">>> CONVERSATION ENDED (30s silence) <<<");
      Serial.println("Resuming servo sweep...");
    }
    
    if (now - lastServoUpdate >= servoStepInterval) {
      lastServoUpdate = now;

      // Probe blink every 45°
      if (currentAngle == nextProbeAngle) {
        Serial.println("Idle sweep → probe blink");
        strip.setPixelColor(purplePixel, strip.Color(128, 0, 128));  // Purple flash
        strip.show();
        ledcWrite(IR_LED_PIN, IR_DUTY_CYCLE);  // 38kHz PWM on
        delay(1000);  // full 1s probe blink
        ledcWrite(IR_LED_PIN, 0);  // PWM off
        strip.setPixelColor(purplePixel, strip.Color(0, 0, 0));  // Clear purple
        strip.show();
        nextProbeAngle += 45;
        if (nextProbeAngle > 180) nextProbeAngle = 0;
      }

      // Increment/decrement angle
      if (sweepForward) {
        targetServoAngle++;
        if (targetServoAngle >= 180) {
          targetServoAngle = 180;
          sweepForward = false;
        }
      } else {
        targetServoAngle--;
        if (targetServoAngle <= 0) {
          targetServoAngle = 0;
          sweepForward = true;
        }
      }

      // Smooth servo movement - gradually approach target
      if (actualServoAngle < targetServoAngle) {
        actualServoAngle++;
      } else if (actualServoAngle > targetServoAngle) {
        actualServoAngle--;
      }
      
      // Only write if angle changed
      if (currentAngle != actualServoAngle) {
        currentAngle = actualServoAngle;
        moveServoTo(currentAngle);
      }
    }
  }
}
