#pragma once
#include <Arduino.h>

struct RttlNote {
    int note;
    int octave;
    unsigned long durationMs;
};

class BuzzerManager {
public:
    BuzzerManager() {}

    void begin(int pin) {
        _pin = pin;
        pinMode(_pin, OUTPUT);
        digitalWrite(_pin, LOW);
    }

    void playRttl(const char* rttl) {
        _noteCount = 0;
        _currentNoteIndex = 0;
        _playing = false;

        const char* p = rttl;
        while (*p && *p != ':') p++;
        if (!*p) return;
        p++;

        int defaultDuration = 4;
        int defaultOctave = 5;
        int bpm = 125;

        while (*p && *p != ':') {
            if (*p == 'd' && *(p+1) == '=') { p += 2; defaultDuration = max(atoi(p), 1); while (*p >= '0' && *p <= '9') p++; }
            else if (*p == 'o' && *(p+1) == '=') { p += 2; defaultOctave = max(atoi(p), 1); while (*p >= '0' && *p <= '9') p++; }
            else if (*p == 'b' && *(p+1) == '=') { p += 2; bpm = max(atoi(p), 1); while (*p >= '0' && *p <= '9') p++; }
            else p++;
            if (*p == ',') p++;
        }
        if (!*p) return;
        p++;

        int wholeNoteMs = 60000 * 4 / bpm;

        while (*p && _noteCount < 64) {
            while (*p == ' ' || *p == ',') p++;
            if (!*p) break;

            int note = -1;
            if (*p == 'p' || *p == 'P') { note = 0; p++; }
            else {
                switch (*p) {
                    case 'c': case 'C': note = 1; break;
                    case 'd': case 'D': note = 3; break;
                    case 'e': case 'E': note = 5; break;
                    case 'f': case 'F': note = 6; break;
                    case 'g': case 'G': note = 8; break;
                    case 'a': case 'A': note = 10; break;
                    case 'b': case 'B': note = 12; break;
                }
                if (note > 0) {
                    p++;
                    if (*p == '#' || *p == 's' || *p == 'S') { note++; p++; }
                }
            }
            if (note < 0) { p++; continue; }

            int octave = defaultOctave;
            if (*p >= '4' && *p <= '7') { octave = *p - '0'; p++; }

            int duration = defaultDuration;
            int durVal = 0;
            while (*p >= '0' && *p <= '9') { durVal = durVal * 10 + (*p - '0'); p++; }
            if (durVal > 0) duration = max(durVal, 1);

            _notes[_noteCount].note = note;
            _notes[_noteCount].octave = octave;
            _notes[_noteCount].durationMs = wholeNoteMs / duration;

            while (*p == '.') {
                _notes[_noteCount].durationMs += _notes[_noteCount].durationMs / 2;
                p++;
            }
            _noteCount++;
        }

        if (_noteCount > 0) {
            _currentNoteIndex = 0;
            _playing = true;
            playCurrentNote();
        }
    }

    void loop() {
        if (!_playing) return;
        unsigned long now = millis();
        if (now - _noteStartTime >= _currentNoteTotalMs) {
            noTone(_pin);
            _currentNoteIndex++;
            if (_currentNoteIndex >= _noteCount) {
                _playing = false;
                return;
            }
            playCurrentNote();
        }
    }

    void stop() {
        _playing = false;
        noTone(_pin);
    }

    bool isPlaying() { return _playing; }

private:
    int _pin;
    bool _playing = false;
    RttlNote _notes[64];
    int _noteCount = 0;
    int _currentNoteIndex = 0;
    unsigned long _noteStartTime = 0;
    unsigned long _currentNoteTotalMs = 0;

    static const uint16_t _baseFreq[];

    void playCurrentNote() {
        if (_currentNoteIndex >= _noteCount) { _playing = false; return; }
        RttlNote& n = _notes[_currentNoteIndex];
        uint16_t freq = 0;
        if (n.note > 0) {
            freq = _baseFreq[n.note];
            if (n.octave > 4) freq *= (1 << (n.octave - 4));
            if (n.octave < 4) freq /= (1 << (4 - n.octave));
        }
        unsigned long pauseMs = (n.durationMs > 20) ? (n.durationMs / 8) : 2;
        _currentNoteTotalMs = n.durationMs + pauseMs;
        _noteStartTime = millis();
        unsigned long toneDur = (n.durationMs > 4) ? (n.durationMs - 2) : n.durationMs;
        if (freq > 0) tone(_pin, freq, toneDur);
    }
};

const uint16_t BuzzerManager::_baseFreq[] = {
    0, 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494
};
