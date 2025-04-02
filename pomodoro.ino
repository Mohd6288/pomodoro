#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Initialize the I2C LCD at address 0x27 with 16 columns and 2 rows.
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Pin definitions
const int encoderPinA = 2;   // Rotary encoder CLK (interrupt-capable)
const int encoderPinB = 3;   // Rotary encoder DT  (interrupt-capable)
const int buttonPin   = 8;   // Encoder push-button
const int buzzerPin   = 9;
const int redPin      = 10;
const int greenPin    = 11;
const int bluePin     = 12;

// Variables for rotary encoder handling
volatile long encoderValue = 0;
volatile int lastEncoded = 0;

// Pomodoro timer parameters (default values in minutes and sessions)
int workDuration   = 25;  // Work duration in minutes
int breakDuration  = 5;   // Normal break duration in minutes
int totalSessions  = 4;   // Total number of Pomodoro sessions
int currentSession = 1;   // Session counter (starts at 1)

// Timer countdown (in seconds) and update timing
unsigned long countdown = 0;             // Remaining seconds in the current phase
unsigned long lastUpdateMillis = 0;        // For tracking 1-second intervals
const unsigned long updateInterval = 1000; // 1000 ms update interval

// Debounce for button presses (for mode cycling)
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 500;

// Variables for skip functionality (active during running modes)
unsigned long buttonPressStart = 0;
const unsigned long skipPressDuration = 2000; // 2 seconds to skip

// Global variable for buzzer frequency (editable)
int buzzerFrequency = 1000; // in Hz

// Timer modes – first four modes are for editing settings,
// then waiting/working/break, followed by a long break and complete.
enum TimerMode { EDIT_WORK, EDIT_BREAK, EDIT_SESSION, EDIT_BUZZER, WAITING, WORK, BREAK, LONG_BREAK, COMPLETE };
TimerMode timerMode = EDIT_WORK;  // Start in editing work duration mode

// Forward declarations for helper functions
void updateDisplay();
void updateLED();
void fadeToColor(int targetRed, int targetGreen, int targetBlue, int steps = 50, int delayTime = 20);
void beep();
void playTone(int frequency, int duration);
void setRGBColor(int red, int green, int blue);
void congratsEffect();

// ----------------------- Setup -----------------------
void setup() {
  lcd.init();
  lcd.backlight();

  pinMode(encoderPinA, INPUT_PULLUP);
  pinMode(encoderPinB, INPUT_PULLUP);
  pinMode(buttonPin,   INPUT_PULLUP);
  pinMode(buzzerPin,   OUTPUT);
  pinMode(redPin,      OUTPUT);
  pinMode(greenPin,    OUTPUT);
  pinMode(bluePin,     OUTPUT);

  // Initialize rotary encoder state.
  int MSB = digitalRead(encoderPinA);
  int LSB = digitalRead(encoderPinB);
  lastEncoded = (MSB << 1) | LSB;

  attachInterrupt(digitalPinToInterrupt(encoderPinA), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(encoderPinB), updateEncoder, CHANGE);

  updateLED();
  updateDisplay();
}

// ----------------------- Main Loop -----------------------
void loop() {
  // Mode cycling button (only active in non-running modes)
  if (!(timerMode == WORK || timerMode == BREAK || timerMode == LONG_BREAK)) {
    if (digitalRead(buttonPin) == LOW && (millis() - lastButtonPress) > debounceDelay) {
      lastButtonPress = millis();
      // Cycle through modes.
      if (timerMode == EDIT_WORK) {
        timerMode = EDIT_BREAK;
      } else if (timerMode == EDIT_BREAK) {
        timerMode = EDIT_SESSION;
      } else if (timerMode == EDIT_SESSION) {
        timerMode = EDIT_BUZZER;
      } else if (timerMode == EDIT_BUZZER) {
        timerMode = WAITING;
      } else if (timerMode == WAITING) {
        // Start the Pomodoro timer.
        currentSession = 1;
        countdown = workDuration * 60;  // Convert work minutes to seconds.
        timerMode = WORK;
      } else if (timerMode == COMPLETE) {
        // After completion, return to settings.
        timerMode = EDIT_WORK;
      }
      updateLED();
      updateDisplay();
      delay(200); // Prevent multiple triggers.
    }
  }

  // Skip functionality during active timer modes (WORK, BREAK, LONG_BREAK)
  if (timerMode == WORK || timerMode == BREAK || timerMode == LONG_BREAK) {
    if (digitalRead(buttonPin) == LOW) {
      if (buttonPressStart == 0) {
        buttonPressStart = millis();
      } else if (millis() - buttonPressStart >= skipPressDuration) {
        // Skip current phase by setting countdown to zero.
        countdown = 0;
      }
    } else {
      buttonPressStart = 0;
    }
  }

  // In editing modes, use the rotary encoder to adjust values.
  if (timerMode == EDIT_WORK) {
    long change = encoderValue;
    if (change != 0) {
      encoderValue = 0;
      adjustTimerValue(workDuration, change);
      updateDisplay();
    }
  } else if (timerMode == EDIT_BREAK) {
    long change = encoderValue;
    if (change != 0) {
      encoderValue = 0;
      adjustTimerValue(breakDuration, change);
      updateDisplay();
    }
  } else if (timerMode == EDIT_SESSION) {
    long change = encoderValue;
    if (change != 0) {
      encoderValue = 0;
      adjustTimerValue(totalSessions, change);
      updateDisplay();
    }
  } else if (timerMode == EDIT_BUZZER) {
    long change = encoderValue;
    if (change != 0) {
      encoderValue = 0;
      // Adjust buzzer frequency in steps of 50 Hz.
      buzzerFrequency += change * 50;
      if (buzzerFrequency < 100) buzzerFrequency = 100;
      updateDisplay();
    }
  }

  // Timer running: WORK, BREAK, or LONG_BREAK modes.
  if (timerMode == WORK || timerMode == BREAK || timerMode == LONG_BREAK) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastUpdateMillis >= updateInterval) {
      lastUpdateMillis = currentMillis;
      
      if (countdown > 0) {
        countdown--;
        updateDisplay();
      } else {
        // When countdown reaches zero or skip was triggered, signal phase end.
        beep();
        if (timerMode == WORK) {
          if (currentSession < totalSessions) {
            // Normal break between sessions.
            countdown = breakDuration * 60;
            timerMode = BREAK;
          } else {
            // Final work session complete:
            // Play congratulatory effect, then start a long break of 15 minutes.
            congratsEffect();
            countdown = 15 * 60; // 15 minute long break.
            timerMode = LONG_BREAK;
          }
        } else if (timerMode == BREAK) {
          // After break, move to next work session.
          currentSession++;
          countdown = workDuration * 60;
          timerMode = WORK;
        } else if (timerMode == LONG_BREAK) {
          // Long break finished, complete the Pomodoro cycle.
          timerMode = COMPLETE;
        }
        updateLED();
        updateDisplay();
      }
    }
  }
}

// ----------------------- Encoder ISR -----------------------
void updateEncoder() {
  int MSB = digitalRead(encoderPinA);
  int LSB = digitalRead(encoderPinB);
  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
    encoderValue++;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
    encoderValue--;

  lastEncoded = encoded;
}

// ----------------------- Timer Adjustment Method -----------------------
void adjustTimerValue(int &timerValue, long encoderChange) {
  // Change the value by encoderChange (step of 1)
  timerValue += encoderChange;
  
  // Ensure the value is a multiple of 5
  if (timerValue % 5 != 0) {
    timerValue = (timerValue / 5) * 5;
  }

  // Ensure the value is within the range of 1 to 250
  if (timerValue < 1) {
    timerValue = 1;
  } else if (timerValue > 250) {
    timerValue = 250;
  }
}


// ----------------------- Display Update -----------------------
void updateDisplay() {
  lcd.clear();
  if (timerMode == EDIT_WORK) {
    lcd.setCursor(0, 0);
    lcd.print("Set Work Time");
    lcd.setCursor(0, 1);
    lcd.print("Work: ");
    lcd.print(workDuration);
    lcd.print(" min");
  } else if (timerMode == EDIT_BREAK) {
    lcd.setCursor(0, 0);
    lcd.print("Set Break Time");
    lcd.setCursor(0, 1);
    lcd.print("Break: ");
    lcd.print(breakDuration);
    lcd.print(" min");
  } else if (timerMode == EDIT_SESSION) {
    lcd.setCursor(0, 0);
    lcd.print("Set Sessions");
    lcd.setCursor(0, 1);
    lcd.print("Sess: ");
    lcd.print(totalSessions);
  } else if (timerMode == EDIT_BUZZER) {
    lcd.setCursor(0, 0);
    lcd.print("Set Buzzer Tone");
    lcd.setCursor(0, 1);
    lcd.print("Freq: ");
    lcd.print(buzzerFrequency);
    lcd.print(" Hz");
  } else if (timerMode == WAITING) {
    lcd.setCursor(0, 0);
    lcd.print("Ready to Start");
    lcd.setCursor(0, 1);
    lcd.print("W:");
    lcd.print(workDuration);
    lcd.print(" B:");
    lcd.print(breakDuration);
    lcd.print(" S:");
    lcd.print(totalSessions);
  } else if (timerMode == WORK) {
    lcd.setCursor(0, 0);
    lcd.print("Work ");
    lcd.print(currentSession);
    lcd.print("/");
    lcd.print(totalSessions);
    lcd.setCursor(0, 1);
    lcd.print("Time: ");
    int minutesLeft = countdown / 60;
    int secondsLeft = countdown % 60;
    if (minutesLeft < 10) lcd.print("0");
    lcd.print(minutesLeft);
    lcd.print(":");
    if (secondsLeft < 10) lcd.print("0");
    lcd.print(secondsLeft);
  } else if (timerMode == BREAK) {
    lcd.setCursor(0, 0);
    lcd.print("Break ");
    lcd.print(currentSession);
    lcd.print("/");
    lcd.print(totalSessions);
    lcd.setCursor(0, 1);
    lcd.print("Time: ");
    int minutesLeft = countdown / 60;
    int secondsLeft = countdown % 60;
    if (minutesLeft < 10) lcd.print("0");
    lcd.print(minutesLeft);
    lcd.print(":");
    if (secondsLeft < 10) lcd.print("0");
    lcd.print(secondsLeft);
  } else if (timerMode == LONG_BREAK) {
    lcd.setCursor(0, 0);
    lcd.print("Long Break!");
    lcd.setCursor(0, 1);
    lcd.print("Time: ");
    int minutesLeft = countdown / 60;
    int secondsLeft = countdown % 60;
    if (minutesLeft < 10) lcd.print("0");
    lcd.print(minutesLeft);
    lcd.print(":");
    if (secondsLeft < 10) lcd.print("0");
    lcd.print(secondsLeft);
  } else if (timerMode == COMPLETE) {
    lcd.setCursor(0, 0);
    lcd.print("Pomodoro");
    lcd.setCursor(0, 1);
    lcd.print("Complete!");
  }
}

// ----------------------- LED Update -----------------------
void updateLED() {
  if (timerMode == EDIT_WORK) {
    fadeToColor(255, 100, 0); // Orange-ish.
  } else if (timerMode == EDIT_BREAK) {
    fadeToColor(0, 255, 255); // Cyan.
  } else if (timerMode == EDIT_SESSION) {
    fadeToColor(128, 0, 128); // Purple.
  } else if (timerMode == EDIT_BUZZER) {
    fadeToColor(255, 255, 0); // Yellow.
  } else if (timerMode == WAITING) {
    fadeToColor(0, 255, 0);   // Green.
  } else if (timerMode == WORK) {
    fadeToColor(255, 0, 0);   // Red.
  } else if (timerMode == BREAK) {
    fadeToColor(0, 0, 255);   // Blue.
  } else if (timerMode == LONG_BREAK) {
    fadeToColor(0, 128, 255); // Light blue.
  } else if (timerMode == COMPLETE) {
    fadeToColor(255, 255, 255); // White.
  }
}

// ----------------------- Fade to Color Function -----------------------
void fadeToColor(int targetRed, int targetGreen, int targetBlue, int steps, int delayTime) {
  static int currentRed = 0, currentGreen = 0, currentBlue = 0;
  int stepRed = (targetRed - currentRed) / steps;
  int stepGreen = (targetGreen - currentGreen) / steps;
  int stepBlue = (targetBlue - currentBlue) / steps;

  for (int i = 0; i < steps; i++) {
    currentRed += stepRed;
    currentGreen += stepGreen;
    currentBlue += stepBlue;
    analogWrite(redPin, currentRed);
    analogWrite(greenPin, currentGreen);
    analogWrite(bluePin, currentBlue);
    delay(delayTime);
  }
  analogWrite(redPin, targetRed);
  analogWrite(greenPin, targetGreen);
  analogWrite(bluePin, targetBlue);
  currentRed = targetRed;
  currentGreen = targetGreen;
  currentBlue = targetBlue;
}

// ----------------------- Buzzer Functions -----------------------
void playTone(int frequency, int duration) {
  tone(buzzerPin, frequency, duration);
  delay(duration);
  noTone(buzzerPin);
}

void beep() {
  playTone(buzzerFrequency, 150);
}

// ----------------------- Congratulatory Effect -----------------------
void congratsEffect() {
  // Play a congratulatory sequence of beeps.
  for (int i = 0; i < 3; i++) {
    tone(buzzerPin, 1500, 100);
    delay(150);
    noTone(buzzerPin);
    delay(100);
  }
}

// ----------------------- Direct RGB LED Set Function -----------------------
void setRGBColor(int red, int green, int blue) {
  analogWrite(redPin, red);
  analogWrite(greenPin, green);
  analogWrite(bluePin, blue);
} 