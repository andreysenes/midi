#pragma once

#include <string.h>
#include <stdio.h>
#include <Arduino.h>

static const char *const CHORD_PC[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

static bool chordHeld[128];
static uint8_t chordCount = 0;
// Press order of currently held notes (for live typing display).
static uint8_t chordOrder[16];
static uint8_t chordOrderLen = 0;
// Live text shown on OLED (updates on every note-on/off).
static char chordLive[14] = "";
// Stack of completed chord labels (G → G/D) while notes remain held.
static char chordStack[14] = "";
static char chordLastStable[8] = "";

static void chordClear() {
  memset(chordHeld, 0, sizeof(chordHeld));
  chordCount = 0;
  chordOrderLen = 0;
  chordLive[0] = '\0';
  chordStack[0] = '\0';
  chordLastStable[0] = '\0';
}

static bool chordMatch(uint16_t mask, uint8_t root, const uint8_t *iv, uint8_t n) {
  uint16_t want = 0;
  for (uint8_t i = 0; i < n; i++) {
    want |= static_cast<uint16_t>(1u << ((root + iv[i]) % 12));
  }
  return (mask & want) == want;
}

// Returns score (0 = only pitch list / single note).
static uint8_t chordDetect(char *out, size_t n, uint8_t *bassPcOut) {
  out[0] = '\0';
  uint16_t mask = 0;
  int bass = -1;
  for (uint8_t note = 0; note < 128; note++) {
    if (!chordHeld[note]) {
      continue;
    }
    mask |= static_cast<uint16_t>(1u << (note % 12));
    if (bass < 0 || note < bass) {
      bass = note;
    }
  }
  if (bass < 0 || mask == 0) {
    return 0;
  }
  const uint8_t bassPc = static_cast<uint8_t>(bass % 12);
  if (bassPcOut) {
    *bassPcOut = bassPc;
  }

  static const uint8_t maj7[] = {0, 4, 7, 11};
  static const uint8_t min7[] = {0, 3, 7, 10};
  static const uint8_t dom7[] = {0, 4, 7, 10};
  static const uint8_t maj[] = {0, 4, 7};
  static const uint8_t minn[] = {0, 3, 7};
  static const uint8_t dim[] = {0, 3, 6};
  static const uint8_t aug[] = {0, 4, 8};
  static const uint8_t sus2[] = {0, 2, 7};
  static const uint8_t sus4[] = {0, 5, 7};

  uint8_t bestRoot = 0;
  const char *bestSuf = "";
  uint8_t bestScore = 0;
  bool found = false;

  for (uint8_t root = 0; root < 12; root++) {
    if (!(mask & (1u << root))) {
      continue;
    }
    struct Trial {
      const uint8_t *iv;
      uint8_t nn;
      const char *suf;
      uint8_t score;
    };
    const Trial trials[] = {
        {maj7, 4, "maj7", 40},
        {min7, 4, "m7", 39},
        {dom7, 4, "7", 38},
        {maj, 3, "", 30},
        {minn, 3, "m", 30},
        {dim, 3, "dim", 28},
        {aug, 3, "aug", 28},
        {sus4, 3, "sus4", 26},
        {sus2, 3, "sus2", 26},
    };
    for (uint8_t t = 0; t < sizeof(trials) / sizeof(trials[0]); t++) {
      if (!chordMatch(mask, root, trials[t].iv, trials[t].nn)) {
        continue;
      }
      if (!found || trials[t].score > bestScore ||
          (trials[t].score == bestScore && root == bassPc)) {
        bestRoot = root;
        bestSuf = trials[t].suf;
        bestScore = trials[t].score;
        found = true;
      }
    }
  }

  if (!found) {
    // Live note list in press order (unique pitch classes).
    char list[14] = "";
    uint16_t seen = 0;
    for (uint8_t i = 0; i < chordOrderLen; i++) {
      const uint8_t pc = static_cast<uint8_t>(chordOrder[i] % 12);
      if (seen & (1u << pc)) {
        continue;
      }
      seen |= static_cast<uint16_t>(1u << pc);
      if (list[0]) {
        strncat(list, " ", sizeof(list) - strlen(list) - 1);
      }
      strncat(list, CHORD_PC[pc], sizeof(list) - strlen(list) - 1);
      if (strlen(list) >= sizeof(list) - 3) {
        break;
      }
    }
    if (!list[0]) {
      snprintf(out, n, "%s", CHORD_PC[bassPc]);
    } else {
      snprintf(out, n, "%s", list);
    }
    return 1;
  }

  char label[8];
  snprintf(label, sizeof(label), "%s%s", CHORD_PC[bestRoot], bestSuf);
  if (bassPc != bestRoot) {
    snprintf(out, n, "%s/%s", label, CHORD_PC[bassPc]);
  } else {
    snprintf(out, n, "%s", label);
  }
  return bestScore;
}

static void chordPushStable(const char *label) {
  if (!label || !label[0]) {
    return;
  }
  if (strcmp(chordLastStable, label) == 0) {
    return;
  }
  if (chordStack[0] == '\0') {
    strncpy(chordStack, label, sizeof(chordStack) - 1);
    chordStack[sizeof(chordStack) - 1] = '\0';
  } else {
    const char *add = label;
    const char *slash = strrchr(label, '/');
    if (slash && slash[1]) {
      add = slash + 1;
    }
    const char *tail = strrchr(chordStack, '/');
    const char *last = tail ? tail + 1 : chordStack;
    if (strcmp(last, add) == 0) {
      strncpy(chordLastStable, label, sizeof(chordLastStable) - 1);
      chordLastStable[sizeof(chordLastStable) - 1] = '\0';
      return;
    }
    char next[14];
    snprintf(next, sizeof(next), "%s/%s", chordStack, add);
    strncpy(chordStack, next, sizeof(chordStack) - 1);
    chordStack[sizeof(chordStack) - 1] = '\0';
  }
  strncpy(chordLastStable, label, sizeof(chordLastStable) - 1);
  chordLastStable[sizeof(chordLastStable) - 1] = '\0';
}

static void chordRebuildLive() {
  if (chordCount == 0) {
    chordLive[0] = '\0';
    chordStack[0] = '\0';
    chordLastStable[0] = '\0';
    return;
  }

  char label[12];
  uint8_t bassPc = 0;
  const uint8_t score = chordDetect(label, sizeof(label), &bassPc);
  if (!label[0]) {
    return;
  }

  // Always refresh live text immediately (every press / release).
  strncpy(chordLive, label, sizeof(chordLive) - 1);
  chordLive[sizeof(chordLive) - 1] = '\0';

  // Stable triad+ (or slash chord) feeds the history stack.
  if (score >= 26) {
    chordPushStable(label);
    // Prefer stacked history when it is longer / richer.
    if (chordStack[0] && strlen(chordStack) >= strlen(chordLive)) {
      strncpy(chordLive, chordStack, sizeof(chordLive) - 1);
      chordLive[sizeof(chordLive) - 1] = '\0';
    }
  }
}

static void chordNote(uint8_t note, bool on) {
  if (note > 127) {
    return;
  }
  if (on) {
    if (!chordHeld[note]) {
      chordHeld[note] = true;
      chordCount++;
      if (chordOrderLen < sizeof(chordOrder)) {
        chordOrder[chordOrderLen++] = note;
      }
    }
  } else if (chordHeld[note]) {
    chordHeld[note] = false;
    if (chordCount) {
      chordCount--;
    }
    for (uint8_t i = 0; i < chordOrderLen; i++) {
      if (chordOrder[i] != note) {
        continue;
      }
      memmove(&chordOrder[i], &chordOrder[i + 1],
              static_cast<size_t>(chordOrderLen - i - 1));
      chordOrderLen--;
      break;
    }
  }

  chordRebuildLive();
}

static const char *chordText() {
  return chordLive;
}
