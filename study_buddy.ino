// ============================================================================
//  STUDY BUDDY v3  |  M5StickC Plus2 companion for Nasraa
//  Board: m5stack:esp32:m5stack_stickc_plus2
//  Files: study_buddy.ino (this file) + messages.h, both in ~/study_buddy/
// ----------------------------------------------------------------------------
//  BUTTONS
//   M5 (big front) : start a focus session (quick energy check first)
//                    during a session: tap = pause / resume, hold 2 s = stop
//   Right (side)   : pep talk card, new colour every time
//                    on the streak screen: hold 3 s = wipe stats + replay tour
//   Left (power)   : face -> streak -> breathing -> face
//                    (holding it about 6 s turns the stick off, that's built in)
//  MOTION
//   shake or hard knock : scared face -> message -> recovery face
//   gentle wiggle       : happy hearts, or "caught you!" during focus
// ============================================================================

#include <M5StickCPlus2.h>
#include <Preferences.h>
#include <math.h>
#include "messages.h"

// ============================================================================
//  SETTINGS (safe to tweak)
// ============================================================================
#define TEST_MODE     0     // 1 = every "minute" lasts 1 second, for testing
#define SOUND_ON      1     // 0 = silent buddy
#define SOUND_VOLUME  120   // 0 to 255
#define DEBUG_MOTION  0     // 1 = print motion numbers to Serial (never on screen)

// Energy check-in. Index 0 = Low, 1 = Okay (classic 25/5), 2 = Full
const uint16_t FOCUS_MIN[3]     = { 15, 25, 35 };
const uint16_t BREAK_MIN[3]     = { 10,  5,  5 };
const uint16_t LONG_BREAK_MIN   = 15;   // after every 4th focus session
const uint8_t  LONG_BREAK_EVERY = 4;

// Motion. d = how far total acceleration strays from 1 g (0 when resting).
// Your measurements: gentle < 1, set down hard ~0.94, jolt ~1.77, hard shake ~2
const float    SHAKE_PEAK      = 1.30f;  // one swing of a shake must pass this
const uint8_t  SHAKE_SWINGS    = 3;      // this many swings...
const uint32_t SHAKE_WINDOW_MS = 900;    // ...inside this window = a shake
const float    JOLT_PEAK       = 2.00f;  // a single spike this big = a hard knock
const float    PET_MIN         = 0.12f;  // gentle wiggle band (low end)
const float    PET_MAX         = 0.80f;  // gentle wiggle band (high end)
const uint32_t PET_HOLD_MS     = 900;    // wiggle this long to count as a cuddle

const uint32_t DROWSY_AFTER_MS = 90UL * 1000;   // idle: gets sleepy
const uint32_t SLEEP_AFTER_MS  = 180UL * 1000;  // idle: naps, screen dims
const uint32_t STOP_HOLD_MS    = 2000;
const uint32_t RESET_HOLD_MS   = 3000;
const uint8_t  BRIGHT_AWAKE    = 130;
const uint8_t  BRIGHT_ASLEEP   = 12;
const uint32_t XP_PER_SESSION  = 10;
const uint32_t XP_PER_BREATH   = 3;
const uint32_t XP_PER_LEVEL    = 50;

#if TEST_MODE
const uint32_t MINUTE_MS = 1000UL;
#else
const uint32_t MINUTE_MS = 60UL * 1000UL;
#endif

#define SCREEN_W 240
#define SCREEN_H 135
#define COUNT_OF(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// ============================================================================
//  COLOURS. The character itself is always C_EYE on black.
// ============================================================================
const uint16_t C_BG       = 0x0000;
const uint16_t C_EYE      = RGB565(90, 215, 255);
const uint16_t C_BLUSH    = RGB565(255, 125, 175);
const uint16_t C_WHITE    = RGB565(255, 255, 255);
const uint16_t C_GREY     = RGB565(140, 140, 160);
const uint16_t C_DARK     = RGB565(45, 45, 62);
const uint16_t C_INK      = RGB565(50, 32, 75);
const uint16_t C_FOCUS    = RGB565(255, 110, 100);
const uint16_t C_BREAK    = RGB565(100, 225, 160);
const uint16_t C_PAUSE    = RGB565(255, 200, 80);
const uint16_t C_SWEAT    = RGB565(185, 230, 255);

enum Pastel : uint8_t { P_PINK, P_LAVENDER, P_MINT, P_PEACH, P_SKY, P_BUTTER, P_COUNT };
const uint16_t PASTELS[P_COUNT] = {
  RGB565(255, 190, 215),  // pink
  RGB565(208, 188, 255),  // lavender
  RGB565(175, 240, 212),  // mint
  RGB565(255, 212, 178),  // peach
  RGB565(178, 220, 255),  // sky
  RGB565(255, 238, 160)   // butter
};

// ============================================================================
//  TYPES (all declared before any function, for Arduino's prototype builder)
// ============================================================================
enum Mood : uint8_t {
  MOOD_NEUTRAL, MOOD_HAPPY, MOOD_EXCITED, MOOD_LOVE, MOOD_SLEEPY, MOOD_ASLEEP,
  MOOD_SAD, MOOD_SCARED, MOOD_FOCUSED, MOOD_CURIOUS, MOOD_WINK, MOOD_POUTY,
  MOOD_SUSPICIOUS, MOOD_COUNT
};
enum Screen : uint8_t { SCR_FACE, SCR_ENERGY, SCR_STATS, SCR_BREATH };
enum Phase  : uint8_t { PH_IDLE, PH_FOCUS, PH_BREAK, PH_READY };
enum React  : uint8_t { RS_NONE, RS_SCARED, RS_MESSAGE, RS_AFTER };

// Every expression is the SAME pair of big rounded eyes, shaped by eyelids.
struct EyeParams {
  float w, h, r;        // eye size and corner roundness
  float tired;          // outer top corner droops (sad, sleepy)
  float angry;          // inner top corner slants (pouty, determined)
  float happy;          // bottom lid rises into a smile curve
  float lidTop;         // flat top lid (sleepy, focused)
  float sizeL, sizeR;   // per-eye scale (curious)
  float closeL, closeR; // per-eye closing (wink, asleep)
  float heart;          // heart eyes
  float shine;          // sparkly highlights (excited, sad puppy eyes, curious)
  float blush;          // pink cheeks
};

struct CardData {
  char     text[180];
  char     hint[48];
  uint16_t bg;
  Mood     mood;
  uint32_t duration;
  uint32_t notBefore;
  bool     important;
};

struct Bank {
  const char* const* lines;
  uint8_t count;
  uint8_t order[32];
  uint8_t pos;
  uint8_t last;
};

struct Note     { uint16_t freq; uint16_t ms; };
struct Particle { float x, y, vx, vy; uint16_t color; };
struct Stats    { uint32_t sessions, focusMin, xp, breaths; };

// ============================================================================
//  EXPRESSION TABLE
// ============================================================================
const EyeParams MOOD_EYES[MOOD_COUNT] = {
  //  w    h    r   tired angry happy lidTop sizeL sizeR closeL closeR heart shine blush
  { 62,  74,  22,  0.00, 0.00, 0.00, 0.00, 1.00, 1.00, 0,    0,     0,    0,    0 }, // NEUTRAL
  { 64,  62,  32,  0.00, 0.00, 0.58, 0.00, 1.00, 1.00, 0,    0,     0,    0,    1 }, // HAPPY
  { 66,  80,  28,  0.00, 0.00, 0.00, 0.00, 1.05, 1.05, 0,    0,     0,    1,    1 }, // EXCITED
  { 64,  66,  26,  0.00, 0.00, 0.00, 0.00, 1.00, 1.00, 0,    0,     1,    0,    1 }, // LOVE
  { 64,  70,  24,  0.14, 0.00, 0.00, 0.48, 1.00, 1.00, 0,    0,     0,    0,    0 }, // SLEEPY
  { 64,  70,  24,  0.00, 0.00, 0.00, 0.00, 1.00, 1.00, 1,    1,     0,    0,    0 }, // ASLEEP
  { 58,  66,  22,  0.42, 0.00, 0.00, 0.00, 0.96, 0.96, 0,    0,     0,    1,    0 }, // SAD
  { 44,  80,  22,  0.00, 0.00, 0.00, 0.00, 1.00, 1.00, 0,    0,     0,    0,    0 }, // SCARED
  { 64,  52,  20,  0.00, 0.10, 0.00, 0.12, 1.00, 1.00, 0,    0,     0,    0,    0 }, // FOCUSED
  { 62,  74,  22,  0.00, 0.00, 0.00, 0.00, 1.12, 0.80, 0,    0,     0,    1,    0 }, // CURIOUS
  { 64,  62,  32,  0.00, 0.00, 0.58, 0.00, 1.00, 1.00, 1,    0,     0,    0,    1 }, // WINK
  { 60,  62,  20,  0.10, 0.34, 0.00, 0.00, 1.00, 1.00, 0,    0,     0,    0,    0 }, // POUTY
  { 66,  26,  13,  0.00, 0.00, 0.00, 0.00, 1.00, 1.00, 0,    0,     0,    0,    0 }, // SUSPICIOUS
};

// ============================================================================
//  SOUNDS (non-blocking little melodies)
// ============================================================================
const Note MEL_START[]      = { {784, 70}, {1047, 120}, {0, 0} };
const Note MEL_DONE[]       = { {1047, 100}, {1319, 100}, {1568, 100}, {2093, 260}, {0, 0} };
const Note MEL_BREAK_OVER[] = { {1568, 90}, {1319, 90}, {1568, 90}, {2093, 200}, {0, 0} };
const Note MEL_SCARED[]     = { {2400, 40}, {3100, 70}, {2000, 60}, {0, 0} };
const Note MEL_PET[]        = { {1319, 60}, {1760, 100}, {0, 0} };
const Note MEL_BLIP[]       = { {1760, 35}, {0, 0} };
const Note MEL_LEVEL[]      = { {1047, 80}, {1319, 80}, {1568, 80}, {2093, 80}, {2637, 240}, {0, 0} };
const Note MEL_PAUSE[]      = { {1319, 60}, {988, 90}, {0, 0} };
const Note MEL_RESUME[]     = { {988, 60}, {1319, 90}, {0, 0} };

// ============================================================================
//  GLOBALS
// ============================================================================
M5Canvas canvas(&M5.Display);
Preferences prefs;
Stats stats = { 0, 0, 0, 0 };

Screen   screen = SCR_FACE;
uint32_t screenSince = 0;
Phase    phase = PH_IDLE;
bool     paused = false;
uint8_t  energy = 1, energySel = 1;
uint32_t phaseEnd = 0, phaseLenMs = 0, pausedRemain = 0, pausedAt = 0, readySince = 0;
uint8_t  cycleCount = 0;
bool     halfShown = false, lastMinShown = false, pausedNudged = false, readyNudged = false;

// face engine
EyeParams eyeCur;
Mood     shownMood = MOOD_NEUTRAL;
Mood     tempMood = MOOD_NEUTRAL;
uint32_t tempUntil = 0;
float    lookX = 0, lookY = 0, lookTX = 0, lookTY = 0;
float    layoutT = 0;
bool     blinking = false;
uint32_t blinkStart = 0, nextBlinkAt = 0, nextLookAt = 0, nextMicroAt = 0;
uint32_t lastFrameAt = 0;

// cards (full-screen speech cards)
CardData card;
bool     cardActive = false;
uint32_t cardStart = 0;
CardData cardQueue[6];
uint8_t  queueLen = 0;
uint8_t  pepColour = 0;
bool     welcomeActive = false;
uint32_t idleHintUntil = 0;

// reactions
React    reactStage = RS_NONE;
uint32_t reactUntil = 0;
bool     reactJolt = false, reactRepeat = false;
uint32_t shakeHistory[3] = { 0, 0, 0 };

// motion
uint32_t lastSampleAt = 0, motionIgnoreUntil = 0;
bool     swingArmed = true;
uint32_t swingTimes[4] = { 0, 0, 0, 0 };
uint8_t  swingIdx = 0;
uint32_t joltPendingAt = 0;
uint32_t petAccum = 0, petCooldownUntil = 0, lastCaughtAt = 0;

// buttons
uint32_t aDownAt = 0, bDownAt = 0;
bool     aConsumed = false, bConsumed = false, aHoldFired = false, bHoldFired = false;
bool     aHolding = false, bHolding = false;

// sleep and battery
bool     asleep = false;
uint32_t lastInteraction = 0, lastBattCheck = 0;
bool     lowBattWarned = false;

// breathing
uint32_t breathStart = 0;

// confetti
Particle confetti[30];
uint32_t confettiUntil = 0;

// sound
const Note* melody = nullptr;
uint8_t  melodyIdx = 0;
uint32_t melodyNextAt = 0;

// message banks
Bank bPep, bFocusPep, bSelfCare, bDone, bShake, bJolt, bShakeAgain, bCaught, bPet;
Bank bStartLow, bStartOkay, bStartFull, bHalf, bLastMin, bBreakOver, bWake;
Bank bBreathDone, bStopped, bStill, bHello;

// ============================================================================
//  PROTOTYPES
// ============================================================================
void tick(uint32_t now);
void render(uint32_t now);
void onAShort(uint32_t now);
void onBShort(uint32_t now);
void onLeftClick(uint32_t now);
void onShake(bool jolt, uint32_t now);
void onPet(uint32_t now);
void goFace(uint32_t now);
void exitBreath(uint32_t now, bool withCard);
void startFocus(uint32_t now);
void wakeUp(uint32_t now);

// ============================================================================
//  SMALL HELPERS
// ============================================================================
float lerpf(float a, float b, float t) { return a + (b - a) * t; }

uint16_t blend565(uint16_t a, uint16_t b, float t) {
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + (int)((br - ar) * t), g = ag + (int)((bg - ag) * t), bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

void initBank(Bank& b, const char* const* lines, uint8_t count) {
  b.lines = lines;
  b.count = count > 32 ? 32 : count;
  b.pos = b.count;   // forces a shuffle on first use
  b.last = 255;
}

// Shuffle-bag: every line is shown once before any line repeats.
const char* nextLine(Bank& b) {
  if (b.pos >= b.count) {
    for (uint8_t i = 0; i < b.count; i++) b.order[i] = i;
    for (int i = b.count - 1; i > 0; i--) {
      int j = random(i + 1);
      uint8_t t = b.order[i]; b.order[i] = b.order[j]; b.order[j] = t;
    }
    if (b.count > 1 && b.order[0] == b.last) {
      uint8_t t = b.order[0]; b.order[0] = b.order[b.count - 1]; b.order[b.count - 1] = t;
    }
    b.pos = 0;
  }
  b.last = b.order[b.pos++];
  return b.lines[b.last];
}

void initBanks() {
  initBank(bPep, MSG_PEP, COUNT_OF(MSG_PEP));
  initBank(bFocusPep, MSG_FOCUS_PEP, COUNT_OF(MSG_FOCUS_PEP));
  initBank(bSelfCare, MSG_SELF_CARE, COUNT_OF(MSG_SELF_CARE));
  initBank(bDone, MSG_DONE, COUNT_OF(MSG_DONE));
  initBank(bShake, MSG_SHAKE, COUNT_OF(MSG_SHAKE));
  initBank(bJolt, MSG_JOLT, COUNT_OF(MSG_JOLT));
  initBank(bShakeAgain, MSG_SHAKE_AGAIN, COUNT_OF(MSG_SHAKE_AGAIN));
  initBank(bCaught, MSG_CAUGHT, COUNT_OF(MSG_CAUGHT));
  initBank(bPet, MSG_PET, COUNT_OF(MSG_PET));
  initBank(bStartLow, MSG_START_LOW, COUNT_OF(MSG_START_LOW));
  initBank(bStartOkay, MSG_START_OKAY, COUNT_OF(MSG_START_OKAY));
  initBank(bStartFull, MSG_START_FULL, COUNT_OF(MSG_START_FULL));
  initBank(bHalf, MSG_HALF, COUNT_OF(MSG_HALF));
  initBank(bLastMin, MSG_LAST_MIN, COUNT_OF(MSG_LAST_MIN));
  initBank(bBreakOver, MSG_BREAK_OVER, COUNT_OF(MSG_BREAK_OVER));
  initBank(bWake, MSG_WAKE, COUNT_OF(MSG_WAKE));
  initBank(bBreathDone, MSG_BREATH_DONE, COUNT_OF(MSG_BREATH_DONE));
  initBank(bStopped, MSG_STOPPED, COUNT_OF(MSG_STOPPED));
  initBank(bStill, MSG_STILL_THERE, COUNT_OF(MSG_STILL_THERE));
  initBank(bHello, MSG_HELLO, COUNT_OF(MSG_HELLO));
}

// ============================================================================
//  SOUND
// ============================================================================
void playMelody(const Note* m) {
#if SOUND_ON
  melody = m;
  melodyIdx = 0;
  melodyNextAt = 0;
#endif
}

void updateSound(uint32_t now) {
  if (!melody || now < melodyNextAt) return;
  Note n = melody[melodyIdx];
  if (n.ms == 0) { melody = nullptr; return; }
  if (n.freq) M5.Speaker.tone(n.freq, n.ms);
  melodyNextAt = now + n.ms + 25;
  melodyIdx++;
}

// ============================================================================
//  STATS
// ============================================================================
void loadStats() {
  stats.sessions = prefs.getUInt("sessions", 0);
  stats.focusMin = prefs.getUInt("focusMin", 0);
  stats.xp       = prefs.getUInt("xp", 0);
  stats.breaths  = prefs.getUInt("breaths", 0);
  energy = prefs.getUChar("energy", 1);
  if (energy > 2) energy = 1;
}

void saveStats() {
  prefs.putUInt("sessions", stats.sessions);
  prefs.putUInt("focusMin", stats.focusMin);
  prefs.putUInt("xp", stats.xp);
  prefs.putUInt("breaths", stats.breaths);
}

uint32_t levelFor(uint32_t xp) { return 1 + xp / XP_PER_LEVEL; }

const char* levelTitle(uint32_t level) {
  uint8_t n = COUNT_OF(LEVEL_TITLES);
  uint32_t i = level - 1;
  if (i >= n) i = n - 1;
  return LEVEL_TITLES[i];
}

// returns true if this pushed her up a level
bool addXP(uint32_t amount) {
  uint32_t before = levelFor(stats.xp);
  stats.xp += amount;
  return levelFor(stats.xp) > before;
}

// ============================================================================
//  CARDS: full-screen pastel speech cards with a tiny version of the face
// ============================================================================
uint32_t cardDurationFor(const char* text) {
  uint32_t ms = 2600 + 55 * (uint32_t)strlen(text);
  if (ms < 3200) ms = 3200;
  if (ms > 7500) ms = 7500;
  return ms;
}

void fillCard(CardData& c, const char* text, uint16_t bg, Mood mood, bool important, const char* hint) {
  strncpy(c.text, text, sizeof(c.text) - 1);
  c.text[sizeof(c.text) - 1] = 0;
  strncpy(c.hint, hint ? hint : "", sizeof(c.hint) - 1);
  c.hint[sizeof(c.hint) - 1] = 0;
  c.bg = bg;
  c.mood = mood;
  c.important = important;
  c.duration = cardDurationFor(text);
  c.notBefore = 0;
}

void queueCard(const char* text, uint16_t bg, Mood mood, uint32_t delayMs, bool important, const char* hint) {
  if (queueLen >= COUNT_OF(cardQueue)) return;
  CardData& c = cardQueue[queueLen++];
  fillCard(c, text, bg, mood, important, hint);
  c.notBefore = millis() + delayMs;
}

void requeueFront(const CardData& c) {
  if (queueLen >= COUNT_OF(cardQueue)) return;
  for (int i = queueLen; i > 0; i--) cardQueue[i] = cardQueue[i - 1];
  cardQueue[0] = c;
  cardQueue[0].notBefore = 0;
  queueLen++;
}

void showCardNow(const char* text, uint16_t bg, Mood mood, bool important, const char* hint, uint32_t now) {
  if (cardActive && card.important) requeueFront(card);
  fillCard(card, text, bg, mood, important, hint);
  cardActive = true;
  cardStart = now;
}

// Leaving the face screen: keep important cards for later, drop chatter.
void parkCard() {
  if (cardActive && card.important) requeueFront(card);
  cardActive = false;
}

void updateCards(uint32_t now) {
  if (cardActive && now - cardStart >= card.duration) {
    cardActive = false;
    if (phase == PH_IDLE && !welcomeActive) idleHintUntil = now + 7000;
  }
  if (!cardActive && queueLen > 0 && screen == SCR_FACE && reactStage == RS_NONE &&
      !asleep && (int32_t)(now - cardQueue[0].notBefore) >= 0) {
    card = cardQueue[0];
    for (uint8_t i = 1; i < queueLen; i++) cardQueue[i - 1] = cardQueue[i];
    queueLen--;
    cardActive = true;
    cardStart = now;
  }
  if (welcomeActive && !cardActive && queueLen == 0) {
    welcomeActive = false;
    prefs.putBool("welcomed", true);
    idleHintUntil = now + 9000;
  }
}

void startWelcome(uint32_t now) {
  welcomeActive = true;
  queueLen = 0;
  const Mood moods[] = { MOOD_EXCITED, MOOD_FOCUSED, MOOD_LOVE, MOOD_HAPPY, MOOD_SCARED, MOOD_WINK };
  const uint8_t cols[] = { P_PINK, P_SKY, P_LAVENDER, P_MINT, P_PEACH, P_BUTTER };
  for (uint8_t i = 0; i < COUNT_OF(MSG_WELCOME) && i < 6; i++) {
    queueCard(MSG_WELCOME[i], PASTELS[cols[i]], moods[i], i == 0 ? 1600 : 0, true, "any button: next");
    cardQueue[queueLen - 1].duration = 7000;
  }
}

void emote(Mood m, uint32_t ms, uint32_t now) {
  tempMood = m;
  tempUntil = now + ms;
}

// ============================================================================
//  POMODORO
// ============================================================================
bool sessionActive() { return phase != PH_IDLE; }

uint32_t remainingMs(uint32_t now) {
  if (phase != PH_FOCUS && phase != PH_BREAK) return 0;
  if (paused) return pausedRemain;
  int32_t r = (int32_t)(phaseEnd - now);
  return r > 0 ? (uint32_t)r : 0;
}

void formatTime(uint32_t ms, char* out, size_t len) {
  uint32_t secs = (ms + 999) / 1000;   // round up: shows 25:00 at the start, 00:00 only at the end
  snprintf(out, len, "%02lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
}

void startFocus(uint32_t now) {
  phase = PH_FOCUS;
  paused = false;
  phaseLenMs = (uint32_t)FOCUS_MIN[energy] * MINUTE_MS;
  phaseEnd = now + phaseLenMs;
  halfShown = lastMinShown = false;
  screen = SCR_FACE;
  screenSince = now;
  asleep = false;
  queueLen = 0;
  Bank& b = (energy == 0) ? bStartLow : (energy == 2 ? bStartFull : bStartOkay);
  char buf[160];
  snprintf(buf, sizeof(buf), nextLine(b), (unsigned)FOCUS_MIN[energy]);
  showCardNow(buf, PASTELS[energy == 0 ? P_LAVENDER : P_SKY], MOOD_EXCITED, false,
              "tap M5: pause   hold M5: stop", now);
  playMelody(MEL_START);
  nextMicroAt = now + 30000;
}

void startBreak(uint32_t now) {
  bool longBreak = (cycleCount % LONG_BREAK_EVERY) == 0;
  uint16_t mins = longBreak ? LONG_BREAK_MIN : BREAK_MIN[energy];
  phase = PH_BREAK;
  paused = false;
  phaseLenMs = (uint32_t)mins * MINUTE_MS;
  phaseEnd = now + phaseLenMs;
  char buf[160];
  if (longBreak)
    snprintf(buf, sizeof(buf), "%u sessions! Long %u min break.\n%s", (unsigned)LONG_BREAK_EVERY, (unsigned)mins, nextLine(bSelfCare));
  else
    snprintf(buf, sizeof(buf), "%u min break!\n%s", (unsigned)mins, nextLine(bSelfCare));
  queueCard(buf, PASTELS[P_SKY], MOOD_HAPPY, 0, true, nullptr);
}

void onFocusEnd(uint32_t now) {
  if (screen == SCR_BREATH) exitBreath(now, false);
  goFace(now);
  cycleCount++;
  stats.sessions++;
  stats.focusMin += FOCUS_MIN[energy];
  bool levelled = addXP(XP_PER_SESSION);
  saveStats();

  // celebration first, cards after
  emote(MOOD_EXCITED, 2200, now);
  confettiUntil = now + 3200;
  for (uint8_t i = 0; i < COUNT_OF(confetti); i++) {
    confetti[i].x = random(0, SCREEN_W);
    confetti[i].y = random(-60, 0);
    confetti[i].vx = random(-10, 11) / 10.0f;
    confetti[i].vy = random(10, 30) / 10.0f;
    confetti[i].color = (i % 4 == 0) ? C_EYE : PASTELS[i % P_COUNT];
  }
  playMelody(levelled ? MEL_LEVEL : MEL_DONE);
  cardActive = false;
  queueLen = 0;

  char buf[160];
  snprintf(buf, sizeof(buf), "%s\n%lu session%s so far!", nextLine(bDone),
           (unsigned long)stats.sessions, stats.sessions == 1 ? "" : "s");
  queueCard(buf, PASTELS[P_MINT], MOOD_EXCITED, 2000, true, nullptr);
  if (levelled) {
    uint32_t lv = levelFor(stats.xp);
    snprintf(buf, sizeof(buf), "LEVEL UP!\n" BUDDY_NAME " is now a %s (Lv %lu)", levelTitle(lv), (unsigned long)lv);
    queueCard(buf, PASTELS[P_BUTTER], MOOD_LOVE, 0, true, nullptr);
  }
  startBreak(now);   // the break clock starts right away, so timing stays exact
}

void onBreakEnd(uint32_t now) {
  if (screen == SCR_BREATH) exitBreath(now, false);
  goFace(now);
  phase = PH_READY;
  paused = false;
  readySince = now;
  readyNudged = false;
  playMelody(MEL_BREAK_OVER);
  queueCard(nextLine(bBreakOver), PASTELS[P_LAVENDER], MOOD_EXCITED, 0, true, "press M5 to go again");
}

void togglePause(uint32_t now) {
  if (phase != PH_FOCUS && phase != PH_BREAK) return;
  if (paused) {
    phaseEnd = now + pausedRemain;
    paused = false;
    emote(MOOD_HAPPY, 1200, now);
    playMelody(MEL_RESUME);
  } else {
    pausedRemain = remainingMs(now);
    paused = true;
    pausedAt = now;
    pausedNudged = false;
    emote(MOOD_CURIOUS, 1200, now);
    playMelody(MEL_PAUSE);
  }
  cardActive = false;
}

void stopSession(uint32_t now) {
  phase = PH_IDLE;
  paused = false;
  cycleCount = 0;
  queueLen = 0;
  confettiUntil = 0;
  showCardNow(nextLine(bStopped), PASTELS[P_PEACH], MOOD_HAPPY, false, nullptr, now);
  playMelody(MEL_PAUSE);
}

void updatePhase(uint32_t now) {
  if ((phase == PH_FOCUS || phase == PH_BREAK) && !paused) {
    if ((int32_t)(now - phaseEnd) >= 0) {
      if (phase == PH_FOCUS) onFocusEnd(now);
      else onBreakEnd(now);
      return;
    }
    if (phase == PH_FOCUS) {
      uint32_t rem = phaseEnd - now;
      if (!halfShown && phaseLenMs >= 10 * MINUTE_MS && rem <= phaseLenMs / 2) {
        halfShown = true;
        queueCard(nextLine(bHalf), PASTELS[P_MINT], MOOD_HAPPY, 0, false, nullptr);
      }
      if (!lastMinShown && phaseLenMs > 3 * MINUTE_MS && rem <= MINUTE_MS) {
        lastMinShown = true;
        queueCard(nextLine(bLastMin), PASTELS[P_BUTTER], MOOD_EXCITED, 0, false, nullptr);
      }
    }
  }
  if (paused && !pausedNudged && now - pausedAt > 10 * 60000UL) {
    pausedNudged = true;
    queueCard(nextLine(bStill), PASTELS[P_LAVENDER], MOOD_SAD, 0, false, nullptr);
  }
  if (phase == PH_READY) {
    if (!readyNudged && now - readySince > 10 * 60000UL) {
      readyNudged = true;
      queueCard(nextLine(bStill), PASTELS[P_LAVENDER], MOOD_CURIOUS, 0, false, nullptr);
    }
    if (now - readySince > 30 * 60000UL) {   // wandered off: end the sitting quietly
      phase = PH_IDLE;
      cycleCount = 0;
      lastInteraction = now;
    }
  }
}

// ============================================================================
//  SCREENS
// ============================================================================
void goFace(uint32_t now) {
  screen = SCR_FACE;
  screenSince = now;
  if (phase == PH_IDLE) idleHintUntil = now + 7000;
}

void openEnergy(uint32_t now) {
  parkCard();
  screen = SCR_ENERGY;
  screenSince = now;
  energySel = energy;
}

void openStats(uint32_t now) {
  parkCard();
  screen = SCR_STATS;
  screenSince = now;
}

void openBreath(uint32_t now) {
  screen = SCR_BREATH;
  screenSince = now;
  breathStart = now;
}

void exitBreath(uint32_t now, bool withCard) {
  uint32_t cycles = (now - breathStart) / 8000;
  if (cycles >= 3) {
    stats.breaths += cycles;
    bool levelled = addXP(XP_PER_BREATH);
    saveStats();
    if (withCard) {
      queueCard(nextLine(bBreathDone), PASTELS[P_LAVENDER], MOOD_LOVE, 0, false, nullptr);
      if (levelled) {
        char buf[120];
        uint32_t lv = levelFor(stats.xp);
        snprintf(buf, sizeof(buf), "LEVEL UP!\n" BUDDY_NAME " is now a %s (Lv %lu)", levelTitle(lv), (unsigned long)lv);
        queueCard(buf, PASTELS[P_BUTTER], MOOD_LOVE, 0, true, nullptr);
        playMelody(MEL_LEVEL);
      }
    }
  }
  goFace(now);
}

void resetAll(uint32_t now) {
  stats.sessions = stats.focusMin = stats.xp = stats.breaths = 0;
  saveStats();
  prefs.putBool("welcomed", false);
  goFace(now);
  showCardNow("All fresh! The welcome tour plays next time I start.", PASTELS[P_MINT], MOOD_HAPPY, false, nullptr, now);
  playMelody(MEL_LEVEL);
}

void updateScreens(uint32_t now) {
  if (screen == SCR_ENERGY && now - screenSince > 25000) goFace(now);
  if (screen == SCR_STATS && now - screenSince > 15000 && !bHolding) goFace(now);
  if (screen == SCR_BREATH && now - screenSince > 5 * 60000UL) exitBreath(now, true);
}

// ============================================================================
//  SLEEP, WAKE, BATTERY
// ============================================================================
void goToSleep(uint32_t now) {
  asleep = true;
  cardActive = false;
  M5.Display.setBrightness(BRIGHT_ASLEEP);
}

void wakeUp(uint32_t now) {
  asleep = false;
  M5.Display.setBrightness(BRIGHT_AWAKE);
  lastInteraction = now;
  emote(MOOD_EXCITED, 1400, now);
  queueCard(nextLine(bWake), PASTELS[P_SKY], MOOD_HAPPY, 1100, false, nullptr);
  playMelody(MEL_PET);
}

void updateSleep(uint32_t now) {
  if (asleep) return;
  bool calm = (phase == PH_IDLE) && screen == SCR_FACE && !cardActive && queueLen == 0 &&
              reactStage == RS_NONE && !welcomeActive;
  if (calm && now - lastInteraction > SLEEP_AFTER_MS) goToSleep(now);
}

void updateBattery(uint32_t now) {
  if (now - lastBattCheck < 60000 && lastBattCheck != 0) return;
  lastBattCheck = now;
  int32_t lvl = M5.Power.getBatteryLevel();
  if (lvl > 0 && lvl <= 15 && !lowBattWarned) {
    lowBattWarned = true;
    queueCard(MSG_LOW_BATTERY, PASTELS[P_PEACH], MOOD_SLEEPY, 0, false, nullptr);
  }
  if (lvl > 30) lowBattWarned = false;
}

// ============================================================================
//  REACTIONS: scared -> message -> recovery face
// ============================================================================
void onShake(bool jolt, uint32_t now) {
  if (welcomeActive || screen == SCR_ENERGY || reactStage != RS_NONE) return;
  if (asleep) { asleep = false; M5.Display.setBrightness(BRIGHT_AWAKE); }
  if (screen == SCR_BREATH) exitBreath(now, false);
  if (screen != SCR_FACE) goFace(now);
  parkCard();

  shakeHistory[0] = shakeHistory[1];
  shakeHistory[1] = shakeHistory[2];
  shakeHistory[2] = now;
  reactRepeat = shakeHistory[0] != 0 && now - shakeHistory[0] < 30000;
  reactJolt = jolt;
  reactStage = RS_SCARED;
  reactUntil = now + 1500;
  tempUntil = 0;
  lastInteraction = now;
  playMelody(MEL_SCARED);
}

void updateReaction(uint32_t now) {
  if (reactStage == RS_NONE || now < reactUntil) return;
  if (reactStage == RS_SCARED) {
    const char* line = reactRepeat ? nextLine(bShakeAgain) : (reactJolt ? nextLine(bJolt) : nextLine(bShake));
    showCardNow(line, PASTELS[reactRepeat ? P_LAVENDER : P_PEACH],
                reactRepeat ? MOOD_POUTY : (reactJolt ? MOOD_SCARED : MOOD_SAD), false, nullptr, now);
    card.duration = 3600;
    reactStage = RS_MESSAGE;
    reactUntil = now + card.duration;
  } else if (reactStage == RS_MESSAGE) {
    cardActive = false;
    reactStage = RS_AFTER;
    reactUntil = now + 2600;
    if (reactRepeat) shakeHistory[0] = shakeHistory[1] = shakeHistory[2] = 0;
  } else {
    reactStage = RS_NONE;
  }
}

Mood reactAfterMood() { return reactRepeat ? MOOD_POUTY : MOOD_HAPPY; }

void onPet(uint32_t now) {
  if (welcomeActive || screen != SCR_FACE || reactStage != RS_NONE || now < petCooldownUntil) return;
  petCooldownUntil = now + 6000;
  lastInteraction = now;
  if (asleep) { wakeUp(now); return; }
  if (phase == PH_FOCUS && !paused) {
    if (now - lastCaughtAt > 180000UL || lastCaughtAt == 0) {
      lastCaughtAt = now;
      showCardNow(nextLine(bCaught), PASTELS[P_BUTTER], MOOD_SUSPICIOUS, false, nullptr, now);
    } else {
      emote(MOOD_SUSPICIOUS, 1600, now);
    }
    return;
  }
  emote(MOOD_LOVE, 2400, now);
  playMelody(MEL_PET);
  if (!cardActive && random(3) == 0) queueCard(nextLine(bPet), PASTELS[P_PINK], MOOD_LOVE, 1800, false, nullptr);
}

// ============================================================================
//  INPUT: MOTION
// ============================================================================
void readMotion(uint32_t now) {
  float ax, ay, az;
  if (!M5.Imu.getAccelData(&ax, &ay, &az)) return;
  float d = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
  uint32_t dt = now - lastSampleAt;
  lastSampleAt = now;
  if (dt > 200) dt = 200;
#if DEBUG_MOTION
  if (d > 0.3f) Serial.printf("d=%.2f\n", d);
#endif
  if (now < 2500 || now < motionIgnoreUntil) { petAccum = 0; joltPendingAt = 0; return; }

  // shake = several big swings close together; jolt = one very big spike
  if (!swingArmed && d < SHAKE_PEAK * 0.6f) swingArmed = true;
  if (swingArmed && d > SHAKE_PEAK) {
    swingArmed = false;
    swingTimes[swingIdx] = now;
    swingIdx = (swingIdx + 1) % 4;
    if (d > JOLT_PEAK && joltPendingAt == 0) joltPendingAt = now;
    uint8_t recent = 0;
    for (uint8_t i = 0; i < 4; i++)
      if (swingTimes[i] && now - swingTimes[i] <= SHAKE_WINDOW_MS) recent++;
    if (recent >= SHAKE_SWINGS) {
      for (uint8_t i = 0; i < 4; i++) swingTimes[i] = 0;
      joltPendingAt = 0;
      petAccum = 0;
      onShake(false, now);
      return;
    }
  }
  if (joltPendingAt && now - joltPendingAt > 350) {   // no shake followed: it was a knock
    joltPendingAt = 0;
    for (uint8_t i = 0; i < 4; i++) swingTimes[i] = 0;
    onShake(true, now);
    return;
  }

  // cuddle = sustained gentle movement
  if (d > PET_MAX) petAccum = 0;
  else if (d > PET_MIN) petAccum += dt;
  else petAccum = petAccum > dt / 2 ? petAccum - dt / 2 : 0;
  if (petAccum > PET_HOLD_MS) {
    petAccum = 0;
    onPet(now);
  }
}

// ============================================================================
//  INPUT: BUTTONS
// ============================================================================
// A press that wakes her up or advances the welcome tour does nothing else.
bool consumePress(uint32_t now) {
  if (reactStage != RS_NONE) { reactStage = RS_NONE; cardActive = false; }
  if (asleep) { wakeUp(now); return true; }
  if (welcomeActive) { cardActive = false; return true; }
  return false;
}

void readButtons(uint32_t now) {
  bool any = false;

  // ---- M5 button (A)
  if (M5.BtnA.wasPressed()) {
    any = true;
    aDownAt = now;
    aHoldFired = false;
    aConsumed = consumePress(now);
  }
  aHolding = M5.BtnA.isPressed() && !aConsumed && !aHoldFired &&
             screen == SCR_FACE && sessionActive();
  if (aHolding && now - aDownAt >= STOP_HOLD_MS) {
    aHoldFired = true;
    aHolding = false;
    stopSession(now);
  }
  if (M5.BtnA.wasReleased()) {
    if (!aConsumed && !aHoldFired && now - aDownAt < 600) onAShort(now);
    aConsumed = false;
    aHolding = false;
  }

  // ---- Right button (B)
  if (M5.BtnB.wasPressed()) {
    any = true;
    bDownAt = now;
    bHoldFired = false;
    bConsumed = consumePress(now);
  }
  bHolding = M5.BtnB.isPressed() && !bConsumed && !bHoldFired && screen == SCR_STATS;
  if (bHolding && now - bDownAt >= RESET_HOLD_MS) {
    bHoldFired = true;
    bHolding = false;
    resetAll(now);
  }
  if (M5.BtnB.wasReleased()) {
    if (!bConsumed && !bHoldFired && now - bDownAt < 600) onBShort(now);
    bConsumed = false;
    bHolding = false;
  }

  // ---- Left button (power)
  if (M5.BtnPWR.wasClicked()) {
    any = true;
    if (!consumePress(now)) onLeftClick(now);
  }

  if (any) {
    lastInteraction = now;
    motionIgnoreUntil = now + 1200;
    petAccum = 0;
  }
}

void onAShort(uint32_t now) {
  switch (screen) {
    case SCR_ENERGY:
      energy = energySel;
      prefs.putUChar("energy", energy);
      startFocus(now);
      break;
    case SCR_STATS:
      goFace(now);
      break;
    case SCR_BREATH:
      exitBreath(now, true);
      break;
    case SCR_FACE:
      if (phase == PH_IDLE || phase == PH_READY) openEnergy(now);
      else togglePause(now);
      break;
  }
}

void onBShort(uint32_t now) {
  switch (screen) {
    case SCR_ENERGY:
      energySel = (energySel + 1) % 3;
      screenSince = now;
      playMelody(MEL_BLIP);
      break;
    case SCR_STATS:
      goFace(now);
      break;
    case SCR_BREATH:
      exitBreath(now, true);
      break;
    case SCR_FACE: {
      bool focusing = (phase == PH_FOCUS && !paused);
      const char* line = focusing ? nextLine(bFocusPep) : nextLine(bPep);
      const Mood moods[] = { MOOD_HAPPY, MOOD_LOVE, MOOD_WINK, MOOD_EXCITED };
      Mood m = focusing ? MOOD_FOCUSED : moods[random(4)];
      showCardNow(line, PASTELS[pepColour], m, false, nullptr, now);
      pepColour = (pepColour + 1) % P_COUNT;
      playMelody(MEL_BLIP);
      break;
    }
  }
}

void onLeftClick(uint32_t now) {
  switch (screen) {
    case SCR_FACE:   openStats(now); break;
    case SCR_STATS:  openBreath(now); break;
    case SCR_BREATH: exitBreath(now, true); break;
    case SCR_ENERGY: goFace(now); break;
  }
}

// ============================================================================
//  FACE ENGINE: mood choice, blinking, glancing, tweening
// ============================================================================
Mood effectiveMood(uint32_t now) {
  if (reactStage == RS_SCARED) return MOOD_SCARED;
  if (reactStage == RS_AFTER) return reactAfterMood();
  if (cardActive) return card.mood;
  if ((int32_t)(tempUntil - now) > 0) return tempMood;
  if (asleep) return MOOD_ASLEEP;
  if (phase == PH_FOCUS) return paused ? MOOD_NEUTRAL : MOOD_FOCUSED;
  if (phase == PH_READY) return MOOD_CURIOUS;
  if (phase == PH_IDLE && now - lastInteraction > DROWSY_AFTER_MS) return MOOD_SLEEPY;
  return MOOD_NEUTRAL;
}

// Little spontaneous expressions so she always feels alive.
void scheduleMicro(uint32_t now) {
  if (now < nextMicroAt || cardActive || reactStage != RS_NONE || asleep ||
      (int32_t)(tempUntil - now) > 0 || screen != SCR_FACE) return;
  int roll = random(100);
  Mood m = MOOD_HAPPY;
  uint32_t dur = random(1600, 2600);
  if (phase == PH_FOCUS && !paused) {
    m = roll < 50 ? MOOD_HAPPY : MOOD_CURIOUS;
    dur = 1500;
    nextMicroAt = now + random(25000, 45000);
  } else if (phase == PH_BREAK) {
    m = roll < 35 ? MOOD_HAPPY : roll < 55 ? MOOD_LOVE : roll < 75 ? MOOD_WINK : MOOD_EXCITED;
    nextMicroAt = now + random(5000, 9000);
  } else if (phase == PH_READY) {
    m = roll < 50 ? MOOD_EXCITED : MOOD_HAPPY;
    nextMicroAt = now + random(4000, 7000);
  } else if (paused) {
    m = roll < 50 ? MOOD_SLEEPY : MOOD_CURIOUS;
    nextMicroAt = now + random(10000, 18000);
  } else {
    bool drowsy = now - lastInteraction > DROWSY_AFTER_MS;
    if (drowsy) { m = roll < 70 ? MOOD_SLEEPY : MOOD_CURIOUS; }
    else m = roll < 30 ? MOOD_HAPPY : roll < 50 ? MOOD_CURIOUS : roll < 65 ? MOOD_WINK :
             roll < 77 ? MOOD_LOVE : roll < 90 ? MOOD_EXCITED : MOOD_SLEEPY;
    nextMicroAt = now + random(6000, 12000);
  }
  emote(m, dur, now);
}

float blinkFactor(uint32_t now) {
  if (!blinking) return 1.0f;
  uint32_t t = now - blinkStart;
  const uint32_t len = (shownMood == MOOD_SLEEPY) ? 320 : 150;
  if (t >= len) { blinking = false; return 1.0f; }
  return 1.0f - 0.92f * sinf(3.14159f * (float)t / (float)len);
}

void lerpEyes(EyeParams& c, const EyeParams& t, float k) {
  c.w = lerpf(c.w, t.w, k);           c.h = lerpf(c.h, t.h, k);
  c.r = lerpf(c.r, t.r, k);           c.tired = lerpf(c.tired, t.tired, k);
  c.angry = lerpf(c.angry, t.angry, k); c.happy = lerpf(c.happy, t.happy, k);
  c.lidTop = lerpf(c.lidTop, t.lidTop, k);
  c.sizeL = lerpf(c.sizeL, t.sizeL, k); c.sizeR = lerpf(c.sizeR, t.sizeR, k);
  c.closeL = lerpf(c.closeL, t.closeL, k); c.closeR = lerpf(c.closeR, t.closeR, k);
  c.heart = lerpf(c.heart, t.heart, k); c.blush = lerpf(c.blush, t.blush, k);
  c.shine = lerpf(c.shine, t.shine, k);
}

void updateFace(uint32_t now) {
  scheduleMicro(now);
  shownMood = effectiveMood(now);

  // blinking
  bool canBlink = shownMood != MOOD_ASLEEP && shownMood != MOOD_LOVE && shownMood != MOOD_SCARED;
  if (!blinking && canBlink && now >= nextBlinkAt) {
    blinking = true;
    blinkStart = now;
    nextBlinkAt = (random(100) < 20) ? now + 280 : now + random(2200, 5500);  // sometimes a double blink
  }

  // glancing around
  if (shownMood == MOOD_SCARED) {
    lookTX = ((now / 240) % 2) ? 9 : -9;
    lookTY = -3;
  } else if (shownMood == MOOD_ASLEEP) {
    lookTX = 0; lookTY = 5;
  } else if (now >= nextLookAt) {
    int roll = random(100);
    if (shownMood == MOOD_FOCUSED) {
      if (roll < 25) { lookTX = 0; lookTY = -2; }            // glances up at her
      else { lookTX = random(-7, 8); lookTY = random(3, 8); } // reading along
    } else if (shownMood == MOOD_SUSPICIOUS) {
      lookTX = (roll < 50) ? -12 : 12; lookTY = 0;
    } else if (roll < 35) {
      lookTX = 0; lookTY = 0;
    } else {
      lookTX = random(-14, 15); lookTY = random(-8, 7);
    }
    nextLookAt = now + random(1200, 4200);
  }
  lookX = lerpf(lookX, lookTX, 0.35f);
  lookY = lerpf(lookY, lookTY, 0.35f);

  float k = (shownMood == MOOD_SCARED) ? 0.5f : 0.28f;
  lerpEyes(eyeCur, MOOD_EYES[shownMood], k);

  float layoutTarget = (screen == SCR_FACE && sessionActive()) ? 1.0f : 0.0f;
  layoutT = lerpf(layoutT, layoutTarget, 0.12f);
}

void updateConfetti(uint32_t now) {
  if (now >= confettiUntil) return;
  for (uint8_t i = 0; i < COUNT_OF(confetti); i++) {
    confetti[i].x += confetti[i].vx;
    confetti[i].y += confetti[i].vy;
    confetti[i].vy += 0.04f;
  }
}

// ============================================================================
//  DRAWING PRIMITIVES
// ============================================================================
void drawHeart(float cx, float cy, float s, uint16_t col) {
  float r = s * 0.52f;
  canvas.fillCircle((int)(cx - s * 0.48f), (int)(cy - s * 0.22f), (int)r, col);
  canvas.fillCircle((int)(cx + s * 0.48f), (int)(cy - s * 0.22f), (int)r, col);
  canvas.fillTriangle((int)(cx - s * 0.98f), (int)(cy - s * 0.06f), (int)(cx + s * 0.98f), (int)(cy - s * 0.06f),
                      (int)cx, (int)(cy + s * 0.95f), col);
}

void drawSparkle(float cx, float cy, float s, uint16_t col) {
  float t = s * 0.28f;
  canvas.fillTriangle((int)cx, (int)(cy - s), (int)(cx - t), (int)cy, (int)(cx + t), (int)cy, col);
  canvas.fillTriangle((int)cx, (int)(cy + s), (int)(cx - t), (int)cy, (int)(cx + t), (int)cy, col);
  canvas.fillTriangle((int)(cx - s), (int)cy, (int)cx, (int)(cy - t), (int)cx, (int)(cy + t), col);
  canvas.fillTriangle((int)(cx + s), (int)cy, (int)cx, (int)(cy - t), (int)cx, (int)(cy + t), col);
}

void drawDrop(float cx, float cy, float s, uint16_t col) {
  canvas.fillCircle((int)cx, (int)(cy + s * 0.5f), (int)(s * 0.55f), col);
  canvas.fillTriangle((int)(cx - s * 0.52f), (int)(cy + s * 0.4f), (int)(cx + s * 0.52f), (int)(cy + s * 0.4f),
                      (int)cx, (int)(cy - s * 0.7f), col);
}

// A closed eye: a thick curve. up = smiling arch, down = peaceful sleep curve.
void drawClosedEye(float cx, float cy, float w, bool up, float thick, uint16_t col) {
  float bend = w * 0.17f;
  const int n = 16;
  for (int i = 0; i <= n; i++) {
    float t = -1.0f + 2.0f * i / n;
    float x = cx + t * w * 0.5f;
    float y = up ? cy + bend * t * t - bend * 0.5f : cy - bend * t * t + bend * 0.5f;
    canvas.fillCircle((int)x, (int)y, (int)(thick * 0.5f), col);
  }
}

void drawEye(float cx, float cy, float w, float h, float r, const EyeParams& p,
             float close, bool isLeft, float scale, uint16_t col) {
  if (close > 0.7f) {
    drawClosedEye(cx, cy, w * 0.9f, p.happy > 0.15f, 7.0f * scale + 1.0f, col);
    return;
  }
  h *= (1.0f - close);
  if (h < 9.0f * scale) {   // mid-blink: a soft flat line
    int th = (int)(6 * scale) < 3 ? 3 : (int)(6 * scale);
    canvas.fillRoundRect((int)(cx - w / 2), (int)(cy - th / 2), (int)w, th, th / 2, col);
    return;
  }
  int iw = (int)(w + 0.5f), ih = (int)(h + 0.5f);
  int x = (int)(cx - iw / 2.0f), y = (int)(cy - ih / 2.0f);
  int rr = (int)r;
  if (rr > iw / 2) rr = iw / 2;
  if (rr > ih / 2) rr = ih / 2;
  canvas.fillRoundRect(x, y, iw, ih, rr, col);

  if (p.shine > 0.5f && p.happy < 0.1f && ih > 30 * scale) {   // anime sparkle, drawn before the lids cut in
    float sy = 0.32f + p.tired * 0.30f;
    canvas.fillCircle((int)(x + iw * 0.66f), (int)(y + ih * sy), (int)(iw * 0.12f) + 1, C_WHITE);
    canvas.fillCircle((int)(x + iw * 0.34f), (int)(y + ih * (sy + 0.34f)), (int)(iw * 0.065f) + 1, C_WHITE);
  }
  if (p.lidTop > 0.02f) canvas.fillRect(x - 2, y - 2, iw + 4, (int)(ih * p.lidTop) + 2, C_BG);
  if (p.tired > 0.02f) {
    int th = (int)(ih * p.tired);
    if (isLeft) canvas.fillTriangle(x - 2, y - 2, x + iw + 2, y - 2, x - 2, y + th, C_BG);
    else        canvas.fillTriangle(x - 2, y - 2, x + iw + 2, y - 2, x + iw + 2, y + th, C_BG);
  }
  if (p.angry > 0.02f) {
    int ah = (int)(ih * p.angry);
    if (isLeft) canvas.fillTriangle(x - 2, y - 2, x + iw + 2, y - 2, x + iw + 2, y + ah, C_BG);
    else        canvas.fillTriangle(x - 2, y - 2, x + iw + 2, y - 2, x - 2, y + ah, C_BG);
  }
  if (p.happy > 0.02f) {   // bottom lid rises in a curve: smiling crescent eyes
    float top = y + ih - ih * p.happy;
    float ry = ih * 0.52f;
    canvas.setClipRect(x - 1, y - 1, iw + 2, ih + 2);   // keep the cut inside this eye (matters on card badges)
    canvas.fillEllipse((int)cx, (int)(top + ry), (int)(iw * 0.54f), (int)ry, C_BG);
    canvas.fillRect(x - 2, (int)(top + ry), iw + 4, ih, C_BG);
    canvas.clearClipRect();
  }
}

// The whole character. Used big on the face screen and tiny on cards.
void drawEyes(float cx, float cy, float scale, bool extras, uint32_t now) {
  const EyeParams& p = eyeCur;
  float sep = 52.0f * scale;
  float bf = blinkFactor(now);
  float jx = 0, jy = 0;
  if (shownMood == MOOD_SCARED) { jx = random(-2, 3) * scale; jy = random(-2, 3) * scale; }

  float ex[2], ey[2], ew[2], eh[2];
  for (int side = 0; side < 2; side++) {
    bool isLeft = (side == 0);
    float s = isLeft ? p.sizeL : p.sizeR;
    float cl = isLeft ? p.closeL : p.closeR;
    ew[side] = p.w * scale * s;
    eh[side] = p.h * scale * s;
    ex[side] = cx + (isLeft ? -sep : sep) + lookX * scale + jx;
    ey[side] = cy + lookY * scale + jy;
    if (p.heart > 0.5f) {
      float pulse = 1.0f + 0.09f * sinf(now * 0.009f);
      drawHeart(ex[side], ey[side], ew[side] * 0.5f * pulse, C_EYE);
    } else {
      drawEye(ex[side], ey[side], ew[side], eh[side] * bf, p.r * scale * s, p, cl, isLeft, scale, C_EYE);
    }
    if (p.blush > 0.5f && shownMood != MOOD_ASLEEP) {
      float bx = ex[side] + (isLeft ? -1.0f : 1.0f) * ew[side] * 0.32f;
      float by = ey[side] + eh[side] * 0.5f + 7.0f * scale;
      canvas.fillEllipse((int)bx, (int)by, (int)(10 * scale) + 1, (int)(4.5f * scale) + 1, C_BLUSH);
    }
  }
  if (!extras) return;

  if (shownMood == MOOD_SCARED) {
    float slide = (now % 1100) / 1100.0f;
    drawDrop(ex[1] + ew[1] * 0.5f + 9 * scale, ey[1] - eh[1] * 0.35f + slide * 12 * scale, 7 * scale, C_SWEAT);
  }
  if (shownMood == MOOD_SAD) {
    float slide = (now % 1700) / 1700.0f;
    drawDrop(ex[0] - ew[0] * 0.28f, ey[0] + eh[0] * 0.45f + slide * 16 * scale, 5 * scale, C_SWEAT);
  }
  if (shownMood == MOOD_SLEEPY || shownMood == MOOD_ASLEEP) {
    canvas.setTextDatum(middle_center);
    for (int k = 0; k < 3; k++) {
      float ph = fmodf(now / 1600.0f + k / 3.0f, 1.0f);
      canvas.setFont(ph < 0.4f ? &fonts::FreeSansBold9pt7b : &fonts::FreeSansBold12pt7b);
      canvas.setTextColor(blend565(C_EYE, C_BG, ph));
      canvas.drawString("z", (int)(ex[1] + ew[1] * 0.5f + (8 + ph * 16) * scale), (int)(ey[1] - eh[1] * 0.4f - ph * 30 * scale));
    }
  }
  if (shownMood == MOOD_EXCITED || shownMood == MOOD_LOVE) {
    const float px[3] = { -1.0f, 1.0f, 1.0f };
    const float py[3] = { -0.5f, -0.6f, 0.45f };
    for (int k = 0; k < 3; k++) {
      float tw = fabsf(sinf(now * 0.006f + k * 2.1f));
      float x = cx + px[k] * (sep + 42 * scale);
      float y = cy + py[k] * 58 * scale;
      drawSparkle(x, y, (3 + 5 * tw) * scale, C_WHITE);
    }
  }
}

// ---------------------------------------------------------------------------
//  TEXT: word-wraps, picks the biggest font that fits, never overflows.
// ---------------------------------------------------------------------------
void setFontIdx(int i) {
  if (i == 0) canvas.setFont(&fonts::FreeSansBold12pt7b);
  else if (i == 1) canvas.setFont(&fonts::FreeSansBold9pt7b);
  else canvas.setFont(&fonts::Font2);
}

int wrapLines(const char* text, int maxW, char lines[][80], int maxLines) {
  int n = 0;
  char cur[80] = "";
  const char* p = text;
  while (*p) {
    if (*p == '\n') {
      if (n < maxLines) { strcpy(lines[n], cur); n++; }
      cur[0] = 0;
      p++;
      continue;
    }
    if (*p == ' ') { p++; continue; }
    char word[40];
    int wl = 0;
    while (*p && *p != ' ' && *p != '\n' && wl < 39) word[wl++] = *p++;
    word[wl] = 0;
    char trial[80];
    if (cur[0]) snprintf(trial, sizeof(trial), "%s %s", cur, word);
    else snprintf(trial, sizeof(trial), "%s", word);
    if (canvas.textWidth(trial) <= maxW || cur[0] == 0) {
      strcpy(cur, trial);
    } else {
      if (n < maxLines) { strcpy(lines[n], cur); n++; }
      snprintf(cur, sizeof(cur), "%s", word);
    }
  }
  if (cur[0] && n < maxLines) { strcpy(lines[n], cur); n++; }
  return n;
}

void drawWrapped(const char* text, int x, int y, int w, int h, uint16_t col) {
  static char lines[7][80];
  int n = 0, lineH = 0, font = 0;
  for (font = 0; font < 3; font++) {
    setFontIdx(font);
    lineH = (font == 0) ? 25 : (font == 1) ? 19 : 16;
    n = wrapLines(text, w, lines, 7);
    if (n * lineH <= h) break;
  }
  if (font > 2) font = 2;
  setFontIdx(font);
  canvas.setTextColor(col);
  canvas.setTextDatum(middle_center);
  canvas.setClipRect(x, y, w, h);
  int top = y + (h - n * lineH) / 2;
  if (top < y) top = y;
  for (int i = 0; i < n; i++) canvas.drawString(lines[i], x + w / 2, top + lineH * i + lineH / 2);
  canvas.clearClipRect();
}

// Single-line label that shrinks if it would not fit.
void drawLabel(const char* text, int x, int y, int maxW, uint16_t col, int firstFont) {
  int f = firstFont;
  for (; f < 3; f++) {
    setFontIdx(f);
    if (canvas.textWidth(text) <= maxW) break;
  }
  if (f > 2) f = 2;
  setFontIdx(f);
  canvas.setTextColor(col);
  canvas.drawString(text, x, y);
}

// ============================================================================
//  SCREENS: DRAWING
// ============================================================================
uint16_t phaseColour() {
  if (paused) return C_PAUSE;
  if (phase == PH_FOCUS) return C_FOCUS;
  if (phase == PH_BREAK) return C_BREAK;
  return C_EYE;
}

void drawSessionStrip(uint32_t now) {
  uint16_t pc = phaseColour();
  const char* label = paused ? "PAUSED" : phase == PH_FOCUS ? "FOCUS" : phase == PH_BREAK ? "BREAK" : "READY?";
  canvas.fillRoundRect(4, 99, 90, 24, 12, pc);
  canvas.setTextDatum(middle_center);
  drawLabel(label, 49, 111, 84, C_INK, 1);

  // tomatoes collected in this set of four
  uint8_t done = cycleCount % LONG_BREAK_EVERY;
  if (phase == PH_BREAK && done == 0 && cycleCount > 0) done = LONG_BREAK_EVERY;
  for (uint8_t i = 0; i < LONG_BREAK_EVERY; i++) {
    int x = 101 + i * 10;
    if (i < done) canvas.fillCircle(x, 111, 3, C_FOCUS);
    else canvas.drawCircle(x, 111, 3, C_GREY);
  }

  if (phase == PH_READY) {
    canvas.setTextDatum(middle_right);
    drawLabel("press M5", 234, 111, 100, C_WHITE, 0);
    return;
  }
  char tbuf[8];
  formatTime(remainingMs(now), tbuf, sizeof(tbuf));
  bool show = !paused || (now % 1000) < 650;
  if (show) {
    canvas.setTextDatum(middle_right);
    canvas.setFont(&fonts::FreeSansBold18pt7b);
    canvas.setTextColor(C_WHITE);
    canvas.drawString(tbuf, 234, 112);
  }
  float pct = phaseLenMs ? 1.0f - (float)remainingMs(now) / (float)phaseLenMs : 0;
  if (pct < 0) pct = 0;
  if (pct > 1) pct = 1;
  canvas.fillRoundRect(6, 128, 228, 6, 3, C_DARK);
  int fw = (int)(228 * pct);
  if (fw > 3) canvas.fillRoundRect(6, 128, fw, 6, 3, pc);
}

void drawHoldBar(const char* text, float pct, uint16_t col) {
  canvas.fillRect(0, 94, SCREEN_W, SCREEN_H - 94, C_BG);
  canvas.setTextDatum(middle_center);
  drawLabel(text, 120, 106, 228, C_WHITE, 1);
  canvas.drawRoundRect(10, 120, 220, 10, 5, C_GREY);
  int fw = (int)(216 * (pct > 1 ? 1 : pct));
  if (fw > 4) canvas.fillRoundRect(12, 122, fw, 6, 3, col);
}

void drawFaceScreen(uint32_t now) {
  float scale = lerpf(1.0f, 0.80f, layoutT);
  float cy = lerpf(60.0f, 47.0f, layoutT);
  if (shownMood == MOOD_ASLEEP) cy += sinf(now * 0.0016f) * 2.0f;
  else cy += sinf(now * 0.0039f) * 1.6f;                                  // gentle breathing bob
  if (shownMood == MOOD_EXCITED) cy -= fabsf(sinf(now * 0.012f)) * 5.0f * scale; // happy bounce

  drawEyes(120, cy, scale, true, now);

  if (now < confettiUntil) {
    for (uint8_t i = 0; i < COUNT_OF(confetti); i++)
      canvas.fillRect((int)confetti[i].x, (int)confetti[i].y, 4, 3, confetti[i].color);
  }

  if (layoutT > 0.5f) {
    if (aHolding && now - aDownAt > 400)
      drawHoldBar("Keep holding to stop", (float)(now - aDownAt - 400) / (STOP_HOLD_MS - 400), C_FOCUS);
    else
      drawSessionStrip(now);
  } else if (now < idleHintUntil && !asleep && !welcomeActive) {
    const char* hints[3] = { "Press M5 to start focusing", "Right button: pep talk", "Left button: streak + breathing" };
    canvas.setTextDatum(middle_center);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(C_GREY);
    canvas.drawString(hints[(now / 2400) % 3], 120, 126);
  }
}

void drawCard(uint32_t now) {
  canvas.fillScreen(card.bg);
  uint16_t deco = blend565(card.bg, C_WHITE, 0.55f);
  drawHeart(16, 16, 6, deco);
  drawSparkle(224, 15, 6, deco);
  drawSparkle(15, 121, 5, deco);
  drawHeart(225, 122, 5, deco);

  // the buddy's face, tiny, on its own little black screen
  canvas.fillRoundRect(90, 4, 60, 30, 12, C_BG);
  drawEyes(120, 19, 0.24f, false, now);

  bool session = (phase == PH_FOCUS || phase == PH_BREAK);
  bool hasHint = card.hint[0] != 0;
  int bottom = (session || hasHint) ? 114 : 131;
  drawWrapped(card.text, 8, 38, 224, bottom - 38, C_INK);

  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(blend565(C_INK, card.bg, 0.25f));
  if (session) {
    char tbuf[8], line[40];
    formatTime(remainingMs(now), tbuf, sizeof(tbuf));
    snprintf(line, sizeof(line), "%s %s%s", phase == PH_FOCUS ? "focus" : "break", tbuf, paused ? " (paused)" : "");
    canvas.drawString(hasHint && now - cardStart < 2500 ? card.hint : line, 120, 125);
  } else if (hasHint) {
    canvas.drawString(card.hint, 120, 125);
  }
}

void drawBattery(int cx, int cy, int bars, uint16_t col, bool selected) {
  uint16_t outline = selected ? C_WHITE : C_GREY;
  canvas.drawRoundRect(cx - 22, cy - 11, 42, 22, 4, outline);
  canvas.drawRoundRect(cx - 21, cy - 10, 40, 20, 3, outline);
  canvas.fillRoundRect(cx + 20, cy - 4, 4, 8, 1, outline);
  for (int k = 0; k < 3; k++) {
    uint16_t c = k < bars ? col : C_DARK;
    canvas.fillRoundRect(cx - 18 + k * 12, cy - 7, 10, 14, 2, c);
  }
}

void drawEnergyScreen(uint32_t now) {
  canvas.setTextDatum(middle_center);
  drawLabel("How's your energy, " BUDDY_NAME "?", 120, 12, 232, C_WHITE, 1);
  const char* names[3] = { "Low", "Okay", "Full" };
  const uint16_t cols[3] = { C_FOCUS, C_PAUSE, C_BREAK };
  for (int i = 0; i < 3; i++) {
    int cx = 40 + i * 80;
    bool sel = (i == energySel);
    int bob = sel ? (int)(sinf(now * 0.008f) * 2.0f) : 0;
    if (sel) {
      canvas.drawRoundRect(cx - 37, 26, 74, 72, 12, C_EYE);
      canvas.drawRoundRect(cx - 36, 27, 72, 70, 11, C_EYE);
    }
    drawBattery(cx, 46 + bob, i + 1, cols[i], sel);
    canvas.setTextDatum(middle_center);
    canvas.setFont(&fonts::FreeSansBold9pt7b);
    canvas.setTextColor(sel ? C_EYE : C_GREY);
    canvas.drawString(names[i], cx, 72);
    char buf[16];
    snprintf(buf, sizeof(buf), "%u min", (unsigned)FOCUS_MIN[i]);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(sel ? C_WHITE : C_GREY);
    canvas.drawString(buf, cx, 88);
  }
  char detail[48];
  snprintf(detail, sizeof(detail), "%u min focus + %u min break", (unsigned)FOCUS_MIN[energySel], (unsigned)BREAK_MIN[energySel]);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_EYE);
  canvas.drawString(detail, 120, 110);
  canvas.setTextColor(C_GREY);
  canvas.drawString("Right: change    M5: start", 120, 127);
}

void drawTomato(int cx, int cy, int r) {
  canvas.fillCircle(cx, cy, r, C_FOCUS);
  canvas.fillCircle(cx - r / 3, cy - r / 3, r / 4, blend565(C_FOCUS, C_WHITE, 0.5f));
  uint16_t leaf = RGB565(90, 200, 110);
  canvas.fillTriangle(cx - r / 2, cy - r + 2, cx + r / 2, cy - r + 2, cx, cy - r / 2, leaf);
  canvas.fillRect(cx - 1, cy - r - 4, 3, 6, leaf);
}

void drawStatsScreen(uint32_t now) {
  canvas.setTextDatum(middle_center);
  if (sessionActive() && phase != PH_READY) {
    char tbuf[8], line[40];
    formatTime(remainingMs(now), tbuf, sizeof(tbuf));
    snprintf(line, sizeof(line), "%s %s left", phase == PH_FOCUS ? "Focus" : "Break", tbuf);
    drawLabel(line, 120, 11, 232, phaseColour(), 1);
  } else {
    drawLabel(BUDDY_NAME "'s streak", 120, 11, 232, C_WHITE, 1);
  }

  drawTomato(30, 50, 17);
  char buf[40];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats.sessions);
  canvas.setTextDatum(middle_left);
  canvas.setFont(&fonts::FreeSansBold24pt7b);
  if (canvas.textWidth(buf) > 100) canvas.setFont(&fonts::FreeSansBold18pt7b);
  canvas.setTextColor(C_WHITE);
  canvas.drawString(buf, 54, 50);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_GREY);
  canvas.drawString(stats.sessions == 1 ? "session" : "sessions", 56, 76);

  canvas.setTextDatum(middle_right);
  canvas.drawString("focus time", 234, 36);
  if (stats.focusMin < 60) snprintf(buf, sizeof(buf), "%lu min", (unsigned long)stats.focusMin);
  else snprintf(buf, sizeof(buf), "%luh %02lum", (unsigned long)(stats.focusMin / 60), (unsigned long)(stats.focusMin % 60));
  drawLabel(buf, 234, 56, 84, C_WHITE, 0);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_GREY);
  snprintf(buf, sizeof(buf), "%lu calm breaths", (unsigned long)stats.breaths);
  canvas.drawString(buf, 234, 76);

  if (bHolding && now - bDownAt > 500) {
    drawHoldBar("Keep holding to wipe all stats", (float)(now - bDownAt - 500) / (RESET_HOLD_MS - 500), C_FOCUS);
    return;
  }
  uint32_t lv = levelFor(stats.xp);
  snprintf(buf, sizeof(buf), "Lv %lu  %s", (unsigned long)lv, levelTitle(lv));
  canvas.setTextDatum(middle_left);
  drawLabel(buf, 8, 97, 170, C_EYE, 1);
  snprintf(buf, sizeof(buf), "%lu/%lu xp", (unsigned long)(stats.xp % XP_PER_LEVEL), (unsigned long)XP_PER_LEVEL);
  canvas.setTextDatum(middle_right);
  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_GREY);
  canvas.drawString(buf, 234, 97);
  canvas.fillRoundRect(8, 108, 226, 7, 3, C_DARK);
  int fw = (int)(226.0f * (stats.xp % XP_PER_LEVEL) / XP_PER_LEVEL);
  if (fw > 4) canvas.fillRoundRect(8, 108, fw, 7, 3, C_EYE);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(C_GREY);
  canvas.drawString("Left: breathing    M5: back", 120, 127);
}

void drawBreathScreen(uint32_t now) {
  uint32_t el = now - breathStart;
  uint32_t t = el % 8000;
  bool inhale = t < 4000;
  float p = inhale ? t / 4000.0f : 1.0f - (t - 4000) / 4000.0f;
  float ease = (1.0f - cosf(3.14159f * p)) * 0.5f;
  float r = 16 + 30 * ease;
  int cx = 120, cy = 55;

  // soft glow, then a shaded sphere
  canvas.fillCircle(cx, cy, (int)(r + 8), blend565(C_BG, C_EYE, 0.12f));
  canvas.fillCircle(cx, cy, (int)(r + 4), blend565(C_BG, C_EYE, 0.25f));
  uint16_t deep = blend565(C_EYE, RGB565(120, 90, 255), 0.45f);
  for (int k = 0; k < 6; k++) {
    float f = k / 5.0f;
    float rr = r * (1.0f - f * 0.72f);
    canvas.fillCircle((int)(cx - f * r * 0.22f), (int)(cy - f * r * 0.26f), (int)rr, blend565(deep, C_WHITE, f * 0.6f));
  }
  // the buddy lives in the sphere: calm closed eyes and blush
  uint16_t ink = RGB565(30, 40, 90);
  drawClosedEye(cx - r * 0.33f, cy + r * 0.05f, r * 0.38f, false, 2.0f + r * 0.07f, ink);
  drawClosedEye(cx + r * 0.33f, cy + r * 0.05f, r * 0.38f, false, 2.0f + r * 0.07f, ink);
  canvas.fillEllipse((int)(cx - r * 0.5f), (int)(cy + r * 0.32f), (int)(r * 0.13f) + 1, (int)(r * 0.07f) + 1, C_BLUSH);
  canvas.fillEllipse((int)(cx + r * 0.5f), (int)(cy + r * 0.32f), (int)(r * 0.13f) + 1, (int)(r * 0.07f) + 1, C_BLUSH);

  char buf[32];
  snprintf(buf, sizeof(buf), "%s  %lu", inhale ? "Breathe in" : "Breathe out", (unsigned long)(4 - (t % 4000) / 1000));
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(C_WHITE);
  canvas.drawString(buf, 120, 119);

  canvas.setFont(&fonts::Font2);
  canvas.setTextColor(C_GREY);
  canvas.setTextDatum(top_left);
  snprintf(buf, sizeof(buf), "%lu breath%s", (unsigned long)(el / 8000), (el / 8000) == 1 ? "" : "s");
  canvas.drawString(buf, 4, 2);
  canvas.setTextDatum(top_right);
  canvas.drawString(el < 8000 ? "any button: stop" : "", 236, 2);
}

void render(uint32_t now) {
  canvas.fillScreen(C_BG);
  switch (screen) {
    case SCR_FACE:
      if (cardActive) drawCard(now);
      else drawFaceScreen(now);
      break;
    case SCR_ENERGY: drawEnergyScreen(now); break;
    case SCR_STATS:  drawStatsScreen(now); break;
    case SCR_BREATH: drawBreathScreen(now); break;
  }
  canvas.pushSprite(0, 0);
}

// ============================================================================
//  SETUP / LOOP
// ============================================================================
void tick(uint32_t now) {
  updateSound(now);
  updatePhase(now);
  updateReaction(now);
  updateCards(now);
  updateScreens(now);
  updateSleep(now);
  updateBattery(now);
  uint32_t frameMs = asleep ? 100 : 33;
  if (now - lastFrameAt >= frameMs) {
    lastFrameAt = now;
    updateFace(now);
    updateConfetti(now);
    render(now);
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(BRIGHT_AWAKE);
  M5.Speaker.setVolume(SOUND_VOLUME);
  canvas.setColorDepth(16);
  canvas.createSprite(SCREEN_W, SCREEN_H);
  randomSeed(esp_random());

  prefs.begin("buddy3", false);
  loadStats();
  initBanks();

  uint32_t now = millis();
  eyeCur = MOOD_EYES[MOOD_ASLEEP];   // she wakes up when switched on
  emote(MOOD_SLEEPY, 900, now);
  lastInteraction = now;
  nextBlinkAt = now + 1500;
  nextMicroAt = now + 8000;
  if (!prefs.getBool("welcomed", false)) startWelcome(now);
  else queueCard(nextLine(bHello), PASTELS[P_PINK], MOOD_HAPPY, 1300, false, nullptr);
}

void loop() {
  M5.update();
  uint32_t now = millis();
  readButtons(now);
  readMotion(now);
  tick(now);
  delay(5);
}
