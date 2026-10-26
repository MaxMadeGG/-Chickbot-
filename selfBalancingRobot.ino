// Chickbot balance — auto-arm.
//
// On battery: power on, hold still for ~3 s (gyro bias), stand it up, and
// it engages itself. Knock it over and stand it up again and it re-arms.
//
// Serial (optional):
//   1 / 2   kp   - / +  0.1
//   3 / 4   kd   - / +  0.01
//   7 / 8   kv   - / +  0.05      wheel velocity -> lean
//   k / l   kpos - / +  0.05      wheel position -> lean   (L, not 1)
//   9 / 0   max volts - / +  0.5
//   5 / 6   balance point - / +  0.5 deg   (saved to flash)
//   b       set balance point to current tilt, and save
//   z       re-zero the hold position
//   t       toggle live telemetry
//   r       arm now (also re-enables auto-arm)
//   x       stop and stay stopped
//   c / s   crouch / stand

#include <Wire.h>
#include <Preferences.h>

Preferences prefs;

const float TWO_PI_F  = 6.28319f;
const float PI_F      = 3.14159f;
const float HALF_PI_F = 1.57080f;
const float PHASE_B   = 2.09440f;
const float PHASE_C   = 4.18879f;

const int   POLE_PAIRS  = 7;
const float SUPPLY      = 10.0f;
const float CAL_VOLTAGE = 8.0f;
const int   ELEC_REVS   = 20;
const float CAL_STEP    = 0.01f;

const uint32_t PERIOD_US = 1000;           // 1 kHz
const float    DT        = PERIOD_US * 1e-6f;
const int      MPU_EVERY = 5;              // 200 Hz
const float    MPU_DT    = MPU_EVERY * DT;

// ======== THE THINGS TO SET ========
const bool  MOTOR_ENABLED = true;
const bool  FORCE_RECAL   = false;
const bool  FALL_STOP     = true;
const bool  AUTO_ARM      = true;          // engage by itself when stood up

float BALANCE_POINT = -1.2f;               // overridden by the saved value
float kp   = 1.0f;
float kd   = 0.02f;
float kv   = 0.30f;
float kpos = 0.05f;
float MAX_VOLTS = 6.0f;

const float MOTOR_SIGN[2] = { -1.0f, +1.0f };
// ===================================

// ---- axes, measured on the robot ----
const int   GYRO_PITCH = 0;                // X, along the axle
const float GYRO_SIGN  = -1.0f;
const int   ACC_FWD    = 2;                // Z, forward
const int   ACC_UP     = 1;                // Y, vertical

const uint32_t RUN_MS   = 0;               // 0 = no limit
const float FALL_LIMIT  = 35.0f;
const float RAMP_MS     = 150.0f;
const float START_WINDOW= 5.0f;
const float MAX_LEAN    = 12.0f;
const float VEL_ALPHA   = 0.02f;

// auto-arm: must be upright AND still, and not have just fallen
const float    ARM_GYRO_MAX     = 15.0f;   // deg/s
const uint32_t ARM_STILL_MS     = 600;
const uint32_t FALL_COOLDOWN_MS = 2500;

const int   ENC_FAIL_LIMIT = 50;

// ---- servos ----
const int SV1 = 33, SV2 = 12;
const int SV1_STAND = 2300, SV1_CROUCH = 2050;
const int SV2_STAND = 701,  SV2_CROUCH = 950;

struct Motor {
  const char* name;
  int      pinA, pinB, pinC, pinEN;
  TwoWire* bus;
  float    offset;
  int      encDir;
  bool     ready;
  float    angle;
  float    lastRaw;
  bool     started;
  int      fails;
  uint32_t failTotal;
};

Motor motors[] = {
  { "M1", 16, 17, 18,  5, &Wire,  0.0f, +1, false, 0, 0, false, 0, 0 },
  { "M2", 25, 26, 14, 27, &Wire1, 0.0f, +1, false, 0, 0, false, 0, 0 },
};
const int N_MOTORS = 2;

float gyroBias = 0, tilt = 0;
float wheelVel = 0;
float wheelPos = 0, posTarget = 0;
bool  posInit = false;
float idleGyro = 0;

uint32_t runStart = 0, fellAt = 0, stillSince = 0;
bool fallen = true;
bool armEnabled = AUTO_ARM;
bool crouched = false;
bool telemetry = false;

float sumErr = 0, sumVel = 0;
float maxDrift = 0;
uint32_t nStats = 0;
float bestSecs = 0;
uint32_t satTicks = 0;

// ---------------- servos ----------------

void servoUs(int pin, int us) {
  ledcWrite(pin, (uint32_t)(us * 65536.0f / 20000.0f));
}

void servoRamp(int pin, int from, int to, int ms) {
  const int steps = ms / 20;
  for (int i = 0; i <= steps; i++) {
    servoUs(pin, from + (to - from) * i / steps);
    delay(20);
  }
}

// One leg at a time — both together browns out the 5 V rail.
void goCrouch() {
  servoRamp(SV1, SV1_STAND, SV1_CROUCH, 400);
  delay(150);
  servoRamp(SV2, SV2_STAND, SV2_CROUCH, 400);
  crouched = true;
  Serial.println("crouched");
}

void goStand() {
  servoRamp(SV1, SV1_CROUCH, SV1_STAND, 700);
  delay(200);
  servoRamp(SV2, SV2_CROUCH, SV2_STAND, 700);
  crouched = false;
  Serial.println("standing");
}

// ---------------- motors ----------------

float normAngle(float a) {
  a = fmodf(a, TWO_PI_F);
  return (a >= 0) ? a : a + TWO_PI_F;
}

float readAngle(Motor& m) {
  m.bus->beginTransmission(0x36);
  m.bus->write(0x0C);
  if (m.bus->endTransmission(false) != 0) return -1.0f;
  if (m.bus->requestFrom((uint8_t)0x36, (uint8_t)2) != 2) return -1.0f;
  int raw = ((m.bus->read() << 8) | m.bus->read()) & 0x0FFF;
  float a = TWO_PI_F - (raw * TWO_PI_F / 4096.0f);
  return normAngle(m.encDir > 0 ? a : -a);
}

float updateAngle(Motor& m) {
  float raw = readAngle(m);
  if (raw < 0) return -1.0f;
  if (!m.started) { m.lastRaw = raw; m.angle = raw; m.started = true; }
  float d = raw - m.lastRaw;
  if (d >  PI_F) d -= TWO_PI_F;
  if (d < -PI_F) d += TWO_PI_F;
  m.angle  += d;
  m.lastRaw = raw;
  return raw;
}

void setPhase(Motor& m, float a, float volts) {
  float amp = (volts / (SUPPLY * 0.5f)) * 127.0f;
  ledcWrite(m.pinA, constrain(128 + (int)(amp * sinf(a)),           0, 255));
  ledcWrite(m.pinB, constrain(128 + (int)(amp * sinf(a + PHASE_B)), 0, 255));
  ledcWrite(m.pinC, constrain(128 + (int)(amp * sinf(a + PHASE_C)), 0, 255));
}

void setTorqueAt(Motor& m, float rawAngle, float volts) {
  float e = normAngle(rawAngle * POLE_PAIRS - m.offset);
  float lead = (volts >= 0) ? HALF_PI_F : -HALF_PI_F;
  setPhase(m, normAngle(e + lead), fabsf(volts));
}

void motorOff(Motor& m) { setPhase(m, 0, 0); digitalWrite(m.pinEN, LOW); }
void allOff() { for (int i = 0; i < N_MOTORS; i++) motorOff(motors[i]); }
void allOn()  { for (int i = 0; i < N_MOTORS; i++) if (motors[i].ready) digitalWrite(motors[i].pinEN, HIGH); }

void setupPins(Motor& m) {
  pinMode(m.pinEN, OUTPUT);
  digitalWrite(m.pinEN, LOW);
  ledcAttach(m.pinA, 20000, 8);
  ledcAttach(m.pinB, 20000, 8);
  ledcAttach(m.pinC, 20000, 8);
  setPhase(m, 0, 0);
}

bool calibrate(Motor& m) {
  Serial.printf("%s calibrating (encDir %+d)\n", m.name, m.encDir);
  digitalWrite(m.pinEN, HIGH);
  setPhase(m, 0, CAL_VOLTAGE);
  delay(1000);

  float prev = readAngle(m);
  if (prev < 0) { motorOff(m); return false; }

  float total = 0;
  const int steps = (int)(TWO_PI_F * ELEC_REVS / CAL_STEP);
  for (int i = 0; i < steps; i++) {
    setPhase(m, normAngle(i * CAL_STEP), CAL_VOLTAGE);
    delayMicroseconds(500);
    float cur = readAngle(m);
    if (cur < 0) { motorOff(m); return false; }
    float d = cur - prev;
    if (d >  PI_F) d -= TWO_PI_F;
    if (d < -PI_F) d += TWO_PI_F;
    total += d;
    prev = cur;
  }

  float mechRevs = total / TWO_PI_F;
  float implied  = ELEC_REVS / fabsf(mechRevs);
  Serial.printf("  mech revs %.4f   implied poles %.2f\n", mechRevs, implied);

  if (mechRevs < 0 && m.encDir > 0) {
    m.encDir = -1;
    setPhase(m, 0, 0);
    digitalWrite(m.pinEN, LOW);
    delay(500);
    return calibrate(m);
  }
  if (fabsf(implied - POLE_PAIRS) > 0.5f) {
    Serial.println("  BAD — rotor not turning");
    motorOff(m);
    return false;
  }

  setPhase(m, 0, CAL_VOLTAGE);
  delay(1500);
  float a = readAngle(m);
  if (a < 0) { motorOff(m); return false; }
  m.offset = normAngle(a * POLE_PAIRS);
  setPhase(m, 0, 0);
  digitalWrite(m.pinEN, LOW);
  Serial.printf("  offset %.4f\n", m.offset);
  m.ready = true;
  return true;
}

bool readMPU(float acc[3], float gyro[3]) {
  Wire.beginTransmission(0x68);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)0x68, (uint8_t)14) != 14) return false;
  if (Wire.available() < 14) return false;
  int16_t raw[7];
  for (int i = 0; i < 7; i++) raw[i] = (Wire.read() << 8) | Wire.read();
  for (int i = 0; i < 3; i++) {
    acc[i]  = raw[i]     / 16384.0f;
    gyro[i] = raw[i + 4] / 131.0f;
  }
  return true;
}

// Forward axis over vertical axis. Upright ~0, nose-down negative.
float accTilt(float acc[3]) {
  return atan2f(acc[ACC_FWD], acc[ACC_UP]) * 57.2958f;
}

void saveBalance() {
  prefs.begin("chickbot", false);
  prefs.putFloat("bal2", BALANCE_POINT);
  prefs.end();
}

void report(const char* why) {
  float secs = (millis() - runStart) / 1000.0f;
  bool best = secs > bestSecs;
  if (best) bestSecs = secs;
  Serial.printf("\n%s after %.1f s%s   mean err %+.2f   mean vel %+.2f\n",
                why, secs, best ? "   <-- BEST" : "",
                nStats ? sumErr / nStats : 0.0f,
                nStats ? sumVel / nStats : 0.0f);
  Serial.printf("  kp %.2f  kd %.3f  kv %.2f  kpos %.2f  maxV %.1f  balance %.1f   (best %.1f s)\n",
                kp, kd, kv, kpos, MAX_VOLTS, BALANCE_POINT, bestSecs);
  Serial.printf("  max drift %.1f rad   saturated %.1f%%   enc glitches M1 %lu  M2 %lu\n\n",
                maxDrift,
                nStats ? 100.0f * satTicks / nStats : 0.0f,
                motors[0].failTotal, motors[1].failTotal);
}

// Engage now. The caller has already checked it is upright.
void engage() {
  for (int i = 0; i < N_MOTORS; i++) {
    motors[i].started   = false;
    motors[i].fails     = 0;
    motors[i].failTotal = 0;
  }
  wheelVel = 0;
  posInit  = false;
  sumErr = sumVel = 0;
  maxDrift = 0;
  nStats = 0;
  satTicks = 0;
  stillSince = 0;
  fallen = false;
  runStart = millis();
  allOn();
  Serial.println("armed");
}

void stopBalancing(const char* why) {
  report(why);
  allOff();
  fallen = true;
  fellAt = millis();
  stillSince = 0;
}

// Manual 'r': wait for upright, then engage.
void restart() {
  armEnabled = true;
  Serial.println("stand it up...");
  uint32_t t0 = millis();
  while (millis() - t0 < 15000) {
    float a[3], g[3];
    if (readMPU(a, g)) {
      tilt = accTilt(a);
      if (fabsf(tilt - BALANCE_POINT) < START_WINDOW) { engage(); return; }
    }
    delay(20);
  }
  Serial.printf("gave up — tilt %.1f (balance %.1f)\n", tilt, BALANCE_POINT);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\nChickbot balance — auto-arm");

  for (int i = 0; i < N_MOTORS; i++) setupPins(motors[i]);

  ledcAttach(SV1, 50, 16);
  ledcAttach(SV2, 50, 16);
  servoUs(SV1, SV1_STAND);
  delay(400);
  servoUs(SV2, SV2_STAND);
  delay(600);

  Wire.begin(21, 19, 400000);   Wire.setTimeOut(10);
  Wire1.begin(22, 23, 400000);  Wire1.setTimeOut(10);
  delay(200);

  bool mpuOk = false;
  for (int i = 0; i < 10 && !mpuOk; i++) {
    Wire.beginTransmission(0x68);
    Wire.write(0x6B);
    Wire.write(0x00);
    mpuOk = (Wire.endTransmission() == 0);
    if (!mpuOk) delay(100);
  }
  if (!mpuOk) {
    Serial.println("MPU not found — stopping");
    while (1) delay(1000);
  }
  delay(100);

  prefs.begin("chickbot", false);
  BALANCE_POINT = prefs.getFloat("bal2", BALANCE_POINT);

  if (MOTOR_ENABLED) {
    bool haveCal = prefs.getBool("cal", false) && !FORCE_RECAL;

    if (haveCal) {
      for (int i = 0; i < N_MOTORS; i++) {
        char k1[8], k2[8];
        sprintf(k1, "off%d", i);
        sprintf(k2, "dir%d", i);
        motors[i].offset = prefs.getFloat(k1, 0.0f);
        motors[i].encDir = prefs.getInt(k2, 1);
        motors[i].ready  = true;
      }
      prefs.end();
      Serial.println("calibration loaded");
    } else {
      prefs.end();
      for (int i = 0; i < N_MOTORS; i++) calibrate(motors[i]);
      bool allOk = true;
      for (int i = 0; i < N_MOTORS; i++) if (!motors[i].ready) allOk = false;
      if (allOk) {
        prefs.begin("chickbot", false);
        for (int i = 0; i < N_MOTORS; i++) {
          char k1[8], k2[8];
          sprintf(k1, "off%d", i);
          sprintf(k2, "dir%d", i);
          prefs.putFloat(k1, motors[i].offset);
          prefs.putInt(k2, motors[i].encDir);
        }
        prefs.putBool("cal", true);
        prefs.end();
        Serial.println("calibration saved");
      }
    }

    int ready = 0;
    for (int i = 0; i < N_MOTORS; i++) if (motors[i].ready) ready++;
    if (ready < 2) { Serial.println("need both motors — stopping"); while (1) delay(1000); }
  } else {
    prefs.end();
  }

  Serial.println("gyro bias — hold still");
  float sum = 0; int n = 0;
  for (int i = 0; i < 400; i++) {
    float a[3], g[3];
    if (readMPU(a, g)) { sum += g[GYRO_PITCH]; n++; }
    delay(3);
  }
  if (n < 300) { Serial.println("MPU unreliable — stopping"); while (1) delay(1000); }
  gyroBias = sum / n;

  float a[3], g[3];
  readMPU(a, g);
  tilt = accTilt(a);
  fellAt = millis() - FALL_COOLDOWN_MS;       // ready to arm immediately

  Serial.printf("\ntilt %.1f   balance %.1f   kp %.2f  kd %.3f  kv %.2f  kpos %.2f  maxV %.1f\n",
                tilt, BALANCE_POINT, kp, kd, kv, kpos, MAX_VOLTS);
  Serial.printf("auto-arm %s — stand it up\n", armEnabled ? "on" : "off");
  Serial.println("1/2 kp   3/4 kd   7/8 kv   k/l kpos   9/0 maxV   5/6 balance");
  Serial.println("b set balance   z re-zero hold   t telemetry   r arm   x stop\n");
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case '1': kp   -= 0.1f;  break;
      case '2': kp   += 0.1f;  break;
      case '3': kd   -= 0.01f; break;
      case '4': kd   += 0.01f; break;
      case '7': kv   -= 0.05f; break;
      case '8': kv   += 0.05f; break;
      case 'k': kpos -= 0.05f; break;
      case 'l': kpos += 0.05f; break;
      case '9': MAX_VOLTS -= 0.5f; break;
      case '0': MAX_VOLTS += 0.5f; break;
      case '5': BALANCE_POINT -= 0.5f; saveBalance(); break;
      case '6': BALANCE_POINT += 0.5f; saveBalance(); break;
      case 'b': BALANCE_POINT = tilt;  saveBalance(); break;
      case 'z': posTarget = wheelPos;
                Serial.println("hold position re-zeroed");
                continue;
      case 't': telemetry = !telemetry;
                Serial.printf("telemetry %s\n", telemetry ? "on" : "off");
                continue;
      case 'r': restart(); continue;
      case 'x': armEnabled = false;             // stays stopped
                if (!fallen) stopBalancing("stopped");
                else { allOff(); Serial.println("stopped"); }
                continue;
      case 'c': if (!crouched) { allOff(); fallen = true; goCrouch(); } continue;
      case 's': if ( crouched) { goStand(); } continue;
      default: continue;
    }
    if (kp   < 0) kp   = 0;
    if (kd   < 0) kd   = 0;
    if (kv   < 0) kv   = 0;
    if (kpos < 0) kpos = 0;
    MAX_VOLTS = constrain(MAX_VOLTS, 1.0f, 8.0f);
    Serial.printf("kp %.2f  kd %.3f  kv %.2f  kpos %.2f  maxV %.1f  balance %.1f\n",
                  kp, kd, kv, kpos, MAX_VOLTS, BALANCE_POINT);
  }

  if (!fallen && RUN_MS && millis() - runStart > RUN_MS) stopBalancing("time up");

  // ---- stopped: keep the filter alive, and auto-arm when stood up ----
  if (fallen) {
    static uint32_t idle = 0, idlePrint = 0;
    if (millis() - idle > 5) {
      idle = millis();
      float acc[3], gyr[3];
      if (readMPU(acc, gyr)) {
        idleGyro = GYRO_SIGN * (gyr[GYRO_PITCH] - gyroBias);
        tilt = 0.99f * (tilt + idleGyro * 0.005f) + 0.01f * accTilt(acc);
      }
    }
    if (telemetry && millis() - idlePrint > 250) {
      idlePrint = millis();
      Serial.printf("[idle] tilt %+6.2f\n", tilt);
    }

    // Upright AND still AND not just fallen. The stillness test is what
    // stops it engaging while it is being carried around.
    if (armEnabled && !crouched && millis() - fellAt > FALL_COOLDOWN_MS) {
      bool upright = fabsf(tilt - BALANCE_POINT) < START_WINDOW;
      bool quiet   = fabsf(idleGyro) < ARM_GYRO_MAX;
      if (upright && quiet) {
        if (stillSince == 0) stillSince = millis();
        if (millis() - stillSince > ARM_STILL_MS) engage();
      } else {
        stillSince = 0;
      }
    }
    delay(2);
    return;
  }

  static uint32_t tNext = 0;
  uint32_t now = micros();
  if (tNext == 0) tNext = now;
  if ((int32_t)(now - tNext) < 0) return;
  tNext += PERIOD_US;
  if ((int32_t)(now - tNext) > (int32_t)PERIOD_US) tNext = now + PERIOD_US;

  // --- encoders ---
  float rawAngle[2], prevAngle[2];
  bool encDead = false;
  for (int i = 0; i < N_MOTORS; i++) {
    prevAngle[i] = motors[i].angle;
    float r = updateAngle(motors[i]);
    if (r < 0) {
      motors[i].fails++;
      motors[i].failTotal++;
      rawAngle[i] = motors[i].lastRaw;
      if (motors[i].fails > ENC_FAIL_LIMIT) encDead = true;
    } else {
      motors[i].fails = 0;
      rawAngle[i] = r;
    }
  }
  if (encDead) { stopBalancing("encoder dead"); return; }

  float vSum = 0, pSum = 0;
  for (int i = 0; i < N_MOTORS; i++) {
    vSum += ((motors[i].angle - prevAngle[i]) / DT) * MOTOR_SIGN[i];
    pSum += motors[i].angle * MOTOR_SIGN[i];
  }
  wheelVel += ((vSum / N_MOTORS) - wheelVel) * VEL_ALPHA;
  wheelPos  = pSum / N_MOTORS;

  if (!posInit) { posTarget = wheelPos; posInit = true; }
  float posErr = wheelPos - posTarget;
  if (fabsf(posErr) > maxDrift) maxDrift = fabsf(posErr);

  // --- MPU at 200 Hz ---
  static float gyroRate = 0;
  static int div = 0, mpuFails = 0;
  if (++div >= MPU_EVERY) {
    div = 0;
    float acc[3], gyr[3];
    if (readMPU(acc, gyr)) {
      mpuFails = 0;
      gyroRate = GYRO_SIGN * (gyr[GYRO_PITCH] - gyroBias);
      tilt = 0.99f * (tilt + gyroRate * MPU_DT) + 0.01f * accTilt(acc);
    } else if (++mpuFails > 20) {
      stopBalancing("MPU fail");
      return;
    }
  }

  // Outer loop. kv brings the wheels to a stop; kpos decides WHERE they
  // stop. Without the position term the robot random-walks until it hits
  // something.
  float lean   = constrain(kv * wheelVel + kpos * posErr, -MAX_LEAN, MAX_LEAN);
  float target = BALANCE_POINT + lean;

  float ramp  = constrain((millis() - runStart) / RAMP_MS, 0.0f, 1.0f);
  float error = target - tilt;
  float raw   = kp * error - kd * gyroRate;
  float out   = constrain(raw, -MAX_VOLTS, MAX_VOLTS) * ramp;

  if (fabsf(raw) >= MAX_VOLTS) satTicks++;

  sumErr += (tilt - BALANCE_POINT);
  sumVel += wheelVel;
  nStats++;

  if (FALL_STOP && fabsf(tilt - BALANCE_POINT) > FALL_LIMIT) {
    stopBalancing("fallen");
    return;
  }

  if (!crouched)
    for (int i = 0; i < N_MOTORS; i++)
      if (motors[i].ready) setTorqueAt(motors[i], rawAngle[i], out * MOTOR_SIGN[i]);

  if (telemetry) {
    static uint32_t last = 0;
    if (millis() - last > 200) {
      last = millis();
      Serial.printf("tilt %+6.2f  vel %+6.2f  pos %+7.2f  lean %+5.1f  out %+5.2f\n",
                    tilt, wheelVel, posErr, lean, out);
    }
  }
}