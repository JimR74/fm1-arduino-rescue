/*
  FM-1 UBOOT key sender - Arduino Uno / Nano (ATmega328P @ 16 MHz)  *** EXPERIMENTAL ***

  Teste avec succes une fois (2026-11-14) : Uno clone CH340 + FM-1 flash 0x856014.
  Port AVR de la logique du projet Flipper "flipper-fm1-recovery"
  (fm1_boot_key.c) : cle JieLi 0x16EF, MSB en premier, D+ = horloge, D- = donnees,
  8 us bas / 12 us haut par bit (10 us pour le dernier), puis emulation des
  impulsions SOF (1 ms) sur D+.

  Ce sketch N'ECRIT RIEN dans le FM-1 : il essaie seulement de mettre sa puce
  en mode de telechargement ROM (WL82 UBOOT1.00). La sauvegarde et l'ecriture
  se font ensuite sur le PC avec FM1 Recovery / MvaveFM1Unbricker / M-Flasher.

  CABLAGE (voir aussi docs/cablage.html) :

    Nano D2 --[2.2k]--+------------------ D+ du cable FM-1
                      +--[3.3k]-- GND
                      +------------------ A0 (lecture, fil direct)  [v2 : A0, plus D7]
    Nano D4 --[470R]--+   (meme noeud D+, sert seulement a tirer a la masse)

    Nano D3 --[2.2k]--+------------------ D- du cable FM-1
                      +--[3.3k]-- GND

    Nano GND ----------------------------- GND du cable FM-1
    VBUS (5 V) du cable : NON BRANCHE, isole au ruban.

  Niveaux : D2/D3 haut -> environ 3.0 V sur la ligne (pont 2.2k/3.3k).
  D4 n'est JAMAIS mis a l'etat haut par ce code (seulement sortie basse ou entree).

  Carte dans l'IDE : "Arduino Uno", ou "Arduino Nano" + processeur
  "ATmega328P (Old Bootloader)" pour la plupart des clones Nano CH340. Moniteur serie : 115200 bauds.
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// ---- Polarite : 0 = celle du Flipper/Transporter (D+ horloge, D- donnees).
//      Mettre 1 seulement si rien ne repond avec 0.
#define SWAP_POLARITY 0

#define DP_BIT    2   // PD2 = Nano D2 -> D+ (via 2.2k)
#define DM_BIT    3   // PD3 = Nano D3 -> D- (via 2.2k)
#define DPLOW_BIT 4   // PD4 = Nano D4 -> D+ (via 470R), tirage a la masse seulement
// A0 = lecture ADC, relie directement au noeud D+ (v2 ; D7 n'est plus utilise)

#if SWAP_POLARITY
  #define CLK_BIT DM_BIT
  #define DAT_BIT DP_BIT
#else
  #define CLK_BIT DP_BIT
  #define DAT_BIT DM_BIT
#endif

#define LED_PIN 13

static const uint16_t KEY = 0x16EF;
static const unsigned long KEY_WINDOW_MS = 15000;  // comme le Flipper

// ---------- bas niveau ----------

static inline void releaseKeyPins() {
  PORTD &= ~(_BV(DP_BIT) | _BV(DM_BIT));   // pas de pull-up interne
  DDRD  &= ~(_BV(DP_BIT) | _BV(DM_BIT));   // haute impedance
}

static inline void releaseLowPin() {
  DDRD  &= ~_BV(DPLOW_BIT);
  PORTD &= ~_BV(DPLOW_BIT);                // jamais d'etat haut / pull-up sur D4
}

// Lecture ADC rapide de A0 (noeud D+), reference AVcc (5 V), prescaler 16
// -> ~13 us par conversion. Seuil ~1.1 V = 225/1023.
static inline uint16_t dpRaw() {
  ADCSRA |= _BV(ADSC);
  while (ADCSRA & _BV(ADSC)) { }
  return ADC;
}
static inline bool dpHigh() {
  return dpRaw() > 225;
}

static void sendPacket() {
  // entree : les deux lignes basses 3 us
  PORTD &= ~(_BV(CLK_BIT) | _BV(DAT_BIT));
  DDRD  |=  (_BV(CLK_BIT) | _BV(DAT_BIT));
  _delay_us(3);

  uint16_t mask = 0x8000;
  for (uint8_t i = 0; i < 16; i++) {
    if (KEY & mask) PORTD |= _BV(DAT_BIT); else PORTD &= ~_BV(DAT_BIT);
    PORTD &= ~_BV(CLK_BIT);              // horloge basse 8 us
    _delay_us(8);
    PORTD |= _BV(CLK_BIT);               // front montant = echantillonnage
    if (i < 15) _delay_us(12); else _delay_us(10);
    mask >>= 1;
  }
  PORTD &= ~_BV(CLK_BIT);                // dernier front descendant
  releaseKeyPins();
}

// Comme le Flipper : 4 lectures a 35 us, accepte si D+ haut 2 fois de suite.
static bool checkAck() {
  uint8_t consec = 0;
  for (uint8_t i = 0; i < 4; i++) {
    _delay_us(35);
    if (dpHigh()) { if (++consec >= 2) return true; }
    else consec = 0;
  }
  return false;
}

// Emulation des SOF : impulsion basse de 4 us sur D+ toutes les 1 ms (Timer1),
// au moins 1 s, au plus 6 s ; sortie apres 3 lectures D+ basses consecutives.
static uint16_t calibrate() {
  releaseKeyPins();
  releaseLowPin();

  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS11);       // CTC, prescaler 8 -> 0.5 us/tick
  OCR1A  = 1999;                         // 1000 us
  TCNT1  = 0;
  TIFR1  = _BV(OCF1A);

  uint16_t ms = 0;
  uint8_t lows = 0;
  cli();
  while (ms < 6000) {
    while (!(TIFR1 & _BV(OCF1A))) { }
    TIFR1 = _BV(OCF1A);
    DDRD |= _BV(DPLOW_BIT);              // PORTD bit deja a 0 -> tire a la masse
    _delay_us(4);
    DDRD &= ~_BV(DPLOW_BIT);
    ms++;
    _delay_us(20);
    if (!dpHigh()) {
      if (++lows >= 3 && ms >= 1000) break;
    } else {
      lows = 0;
    }
  }
  sei();
  TCCR1B = 0;
  releaseLowPin();
  return ms;
}

// ---------- programme ----------

static void printHelp() {
  Serial.println(F(""));
  Serial.println(F("=== FM-1 UBOOT key (Uno/Nano) - EXPERIMENTAL ==="));
  Serial.println(F("Si ce texte est lisible, l'horloge est bien a 16 MHz."));
  Serial.print  (F("Polarite : "));
  Serial.println(SWAP_POLARITY ? F("B (D- horloge, D+ donnees)") : F("A (D+ horloge, D- donnees)"));
  Serial.println(F("1. FM-1 ETEINT, cable coupe branche au FM-1."));
  Serial.println(F("2. Envoie 'g' ici, puis ALLUME le FM-1 dans les 15 s."));
  Serial.println(F("   'c' = lancer seulement la calibration SOF."));
  Serial.println(F("   't' = test de lecture de la ligne D+."));
}

void setup() {
  releaseKeyPins();
  releaseLowPin();
  DDRD &= ~_BV(7); PORTD &= ~_BV(7);     // D7 (ancien fil) laisse en entree
  DDRC &= ~_BV(0); PORTC &= ~_BV(0);     // A0 en entree, sans pull-up
  DIDR0 = _BV(ADC0D);                    // coupe le buffer numerique de A0
  ADMUX  = _BV(REFS0);                   // ref AVcc, canal ADC0 (A0)
  ADCSRA = _BV(ADEN) | _BV(ADPS2);       // ADC actif, prescaler 16 (1 MHz)
  dpRaw();                               // 1re conversion jetee
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(100);
  printHelp();
}

static void finish(uint16_t ms) {
  Serial.print(F("Calibration terminee apres "));
  Serial.print(ms);
  Serial.println(F(" ms."));
  Serial.println(F(">>> NE PAS eteindre le FM-1."));
  Serial.println(F(">>> Debranche le cable coupe du FM-1, branche un cable USB-C"));
  Serial.println(F(">>> intact FM-1 -> PC, puis verifie 'WL82 UBOOT1.00' (Get-Disk)."));
  digitalWrite(LED_PIN, HIGH);
}

static void runKey() {
  Serial.println(F("Envoi de la cle... ALLUME le FM-1 maintenant."));
  unsigned long t0 = millis();
  unsigned long packets = 0;
  bool acked = false;

  while (millis() - t0 < KEY_WINDOW_MS) {
    uint8_t s = SREG;
    cli();
    sendPacket();
    acked = checkAck();
    SREG = s;
    packets++;
    if (acked) break;
    if ((packets & 0x3FF) == 0) digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }
  digitalWrite(LED_PIN, LOW);

  Serial.print(F("Paquets envoyes : "));
  Serial.println(packets);
  if (!acked) {
    Serial.println(F("Aucune reponse (D+ jamais haut)."));
    Serial.println(F("Verifie le cablage et D+/D-. Ensuite, essaie 'c' quand meme,"));
    Serial.println(F("ou SWAP_POLARITY 1. Eteins le FM-1 avant chaque nouvel essai."));
    return;
  }
  Serial.println(F("REPONSE detectee : D+ tire vers le haut par la puce."));
  finish(calibrate());
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'g') runKey();
  else if (c == 'c') { Serial.println(F("Calibration SOF...")); finish(calibrate()); }
  else if (c == 't') {
    uint16_t v = dpRaw();
    Serial.print(F("D+ : "));
    Serial.print(v * 5.0 / 1023.0, 2);
    Serial.print(F(" V -> "));
    Serial.println(v > 225 ? F("HAUT (> 1.1 V)") : F("BAS"));
  }
  else if (c == 'h' || c == '?') printHelp();
}
