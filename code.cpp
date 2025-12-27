const int ANALOG_INPUT_PIN = A0;     // 0-5V analogisyöte
const int RELAY_PIN = 8;             // Releen ohjauspin

// Aikaparametrit (millisekunteina)
const unsigned long PWM_CYCLE_TIME = 2UL * 3600UL * 1000UL;  // 2 tuntia = 7,200,000 ms
const unsigned long MIN_ON_TIME = 10UL * 60UL * 1000UL;      // 10 minuuttia = 600,000 ms
const unsigned long MIN_OFF_BETWEEN_CYCLES = 10UL * 60UL * 1000UL; // 10 minuuttia = 600,000 ms

// Jänniteparametrit
const float THRESHOLD_VOLTAGE = 1.0;  // Kynnysjännite (1.0V)
const float MAX_INPUT_VOLTAGE = 5.0;  // Suurin syöttöjännite

// Laske minimijännite joka tuottaa 10 minuuttia päällä-aikaa
const float MIN_DUTY_CYCLE = (float)MIN_ON_TIME / (float)PWM_CYCLE_TIME;  // 10min / 120min = 0.0833
const float MIN_ON_VOLTAGE = THRESHOLD_VOLTAGE + (MIN_DUTY_CYCLE * (MAX_INPUT_VOLTAGE - THRESHOLD_VOLTAGE));

// Muuttujat
enum CycleState {
  CYCLE_RUNNING,      // Jakso on käynnissä
  CYCLE_FINISHED,     // Jakso loppui, odotetaan taukoa
  CYCLE_WAITING       // Odotetaan seuraavan jakson alkua
};

CycleState currentState = CYCLE_RUNNING;
unsigned long cycleStartTime = 0;      // Nykyisen jakson aloitusaika
unsigned long cycleEndTime = 0;        // Jakson loppumisaika
unsigned long onTimeThisCycle = 0;     // Päällä-aika tällä jaksolla (ms)
float currentDutyCycle = 0.0;          // Tehosuhde luettuna jakson alussa
bool relayState = false;               // Onko rele päällä

/**
 * @brief Alustaa järjestelmän
 */
void setup() {
  Serial.begin(115200);
  
  pinMode(ANALOG_INPUT_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  relayState = false; // Alustetaan tilamuuttuja
  
  // Aloita ensimmäinen jakso
  startNewCycle();
  
  Serial.println("=== PWM-OHJAIN KÄYNNISTETTY ===");
  Serial.println("2 tunnin jakso, PWM-arvo luetaan jakson alussa");
  Serial.print("Kynnysjännite: "); Serial.print(THRESHOLD_VOLTAGE); Serial.println("V");
  Serial.print("Minimijännite: "); Serial.print(MIN_ON_VOLTAGE, 2); Serial.println("V (10 min ajalle)");
  Serial.print("Minimitauko jaksovälissä: "); Serial.print(MIN_OFF_BETWEEN_CYCLES / 60000.0, 0); Serial.println(" min");
  Serial.println("Ohjausskaala: 1.0V - 5.0V");
  Serial.println("=================================");
}

/**
 * @brief Lukee ja suodattaa analogisen jännitteen
 * @return float Suodatettu jännite voltteina
 */
float readFilteredVoltage() {
  const int NUM_SAMPLES = 10;
  float sum = 0;
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    sum += analogRead(ANALOG_INPUT_PIN);
    delay(5);
  }
  
  float voltage = (sum / NUM_SAMPLES / 1023.0) * 5.0;
  return voltage;
}

/**
 * @brief Asettaa releen haluttuun tilaan VÄLTTÄEN TURHIA KYTKENTÖJÄ
 * @param shouldBeOn true = rele päälle, false = rele pois
 */
void setRelaySafely(bool shouldBeOn) {
  if (shouldBeOn != relayState) {
    relayState = shouldBeOn;
    digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
    
    // Tulosta tilanmuutos
    Serial.print("Rele ");
    Serial.print(relayState ? "PÄÄLLE " : "POIS ");
  }
}

/**
 * @brief Aloittaa uuden 2 tunnin jakson
 */
void startNewCycle() {
  // 1. Lue jännite ja laske tehosuhde
  float voltage = readFilteredVoltage();
  
  // 2. Tulosta luettu jännite
  Serial.print("Uusi jakso: ");
  Serial.print(voltage, 2);
  Serial.print("V -> ");
  
  // 3. Tarkista kynnysjännite ja laske uusi tehosuhde
  bool shouldRelayStartOn = false;
  
  if (voltage < THRESHOLD_VOLTAGE) {
    // Alle 1.0V: rele pois, tehosuhde 0%
    currentDutyCycle = 0.0;
    onTimeThisCycle = 0;
    shouldRelayStartOn = false;
    Serial.println("alle 1.0V, rele pois koko jaksosta");
  } 
  else if (voltage < MIN_ON_VOLTAGE) {
    // 1.0V - 1.33V: liian pieni 10 min ajalle
    currentDutyCycle = 0.0;
    onTimeThisCycle = 0;
    shouldRelayStartOn = false;
    Serial.print("alle ");
    Serial.print(MIN_ON_VOLTAGE, 2);
    Serial.println("V, rele pois (liian pieni 10 min ajalle)");
  }
  else {
    // Yli 1.33V: laske tehosuhde
    currentDutyCycle = (voltage - THRESHOLD_VOLTAGE) / (MAX_INPUT_VOLTAGE - THRESHOLD_VOLTAGE);
    currentDutyCycle = constrain(currentDutyCycle, 0.0, 1.0);
    
    // Laske päällä-aika
    onTimeThisCycle = (unsigned long)(currentDutyCycle * PWM_CYCLE_TIME);
    
    // Varmista minimiaika
    if (onTimeThisCycle > 0 && onTimeThisCycle < MIN_ON_TIME) {
      onTimeThisCycle = MIN_ON_TIME;
      Serial.print("pakotettu minimiaika 10 min, ");
    }
    
    // Rele KÄYNNISTYY jakson alussa (jos on päällä-aikaa)
    shouldRelayStartOn = (onTimeThisCycle > 0);
    
    Serial.print("tehosuhde ");
    Serial.print(currentDutyCycle * 100, 1);
    Serial.print("%, päällä ");
    Serial.print(onTimeThisCycle / 60000.0, 1);
    Serial.println(" min");
  }
  
  // 4. Nollaa ajastimet ja tilamuuttujat
  cycleStartTime = millis();
  currentState = CYCLE_RUNNING;
  
  // 5. Aseta rele turvallisesti (vain jos tilanmuutos TARVITAAN)
  setRelaySafely(shouldRelayStartOn);
  
  if (shouldRelayStartOn) {
    Serial.print("(@ 0.0 min)");
  }
  Serial.println();
}

/**
 * @brief Tarkistaa onko vähintään 10 min taukoa kulunut
 * @return true jos taukoa on kulunut tarpeeksi, false jos ei
 */
bool isMinimumOffTimeMet() {
  if (cycleEndTime == 0) return false;
  
  unsigned long currentTime = millis();
  unsigned long offTimeElapsed;
  
  // Käsittele millis() ylivuoto
  if (currentTime < cycleEndTime) {
    offTimeElapsed = (4294967295UL - cycleEndTime) + currentTime;
  } else {
    offTimeElapsed = currentTime - cycleEndTime;
  }
  
  return offTimeElapsed >= MIN_OFF_BETWEEN_CYCLES;
}

/**
 * @brief Päivittää järjestelmän tilan
 * @return true jos uusi jakso pitää aloittaa, false jos ei
 */
bool updateSystemState() {
  unsigned long currentTime = millis();
  unsigned long elapsedTime;
  
  // Laske kulunut aika (käsittele millis() ylivuoto)
  if (currentTime < cycleStartTime) {
    elapsedTime = (4294967295UL - cycleStartTime) + currentTime;
  } else {
    elapsedTime = currentTime - cycleStartTime;
  }
  
  switch (currentState) {
    case CYCLE_RUNNING:
      // Tarkista onko jakso loppunut
      if (elapsedTime >= PWM_CYCLE_TIME) {
        // Jakso loppui - RELE PYSYY NYKYISESSÄ TILASSA
        cycleEndTime = currentTime;
        currentState = CYCLE_FINISHED;
        
        // Tarkista onko rele päällä jakson lopussa
        if (relayState) {
          Serial.println("Jakso päättyi, rele PÄÄLLÄ -> pakollinen 10 min tauko");
        } else {
          Serial.println("Jakso päättyi, rele POIS -> ei tarvita taukoa");
          // Jos rele on jo pois, siirry suoraan odotustilaan
          currentState = CYCLE_WAITING;
        }
        return false;
      }
      break;
      
    case CYCLE_FINISHED:
      // Odotetaan minimitaukoa (vain jos rele oli päällä jakson lopussa)
      if (isMinimumOffTimeMet()) {
        // Ennen uutta jaksoa varmistetaan että rele on POIS
        if (relayState) {
          setRelaySafely(false);
          Serial.println("Tauko täynnä, rele sammutetaan ennen uutta jaksoa");
        }
        currentState = CYCLE_WAITING;
        Serial.println("10 min tauko täynnä, voidaan aloittaa uusi jakso");
      }
      break;
      
    case CYCLE_WAITING:
      // Voidaan aloittaa uusi jakso milloin tahansa
      return true;
  }
  
  return false;
}

/**
 * @brief Pääsilmukka
 */
void loop() {
  // 1. Päivitä järjestelmän tila ja tarkista uusi jakso
  if (updateSystemState()) {
    // Aloita uusi jakso
    startNewCycle();
  }
  
  // 2. Jos ollaan jakson aikana, ohjaa relettä tarpeen mukaan
  if (currentState == CYCLE_RUNNING) {
    unsigned long currentTime = millis();
    unsigned long elapsedTime;
    
    // Laske kulunut aika
    if (currentTime < cycleStartTime) {
      elapsedTime = (4294967295UL - cycleStartTime) + currentTime;
    } else {
      elapsedTime = currentTime - cycleStartTime;
    }
    
    // Päätä releen tila
    bool shouldRelayBeOn = false;
    
    if (onTimeThisCycle > 0 && elapsedTime < onTimeThisCycle) {
      // Ollaankin vielä päällä-ajan sisällä
      shouldRelayBeOn = true;
    }
    
    // Ohjaa relettä turvallisesti (vain jos muutos TARVITAAN)
    setRelaySafely(shouldRelayBeOn);
  }
  
  // 3. Tulosta tilatietoja ajoittain
  static unsigned long lastPrintTime = 0;
  unsigned long currentTime = millis();
  
  if (currentTime - lastPrintTime > 60000) {  // 1 minuutin välein
    lastPrintTime = currentTime;
    
    switch (currentState) {
      case CYCLE_RUNNING: {
        unsigned long elapsedTime;
        if (currentTime < cycleStartTime) {
          elapsedTime = (4294967295UL - cycleStartTime) + currentTime;
        } else {
          elapsedTime = currentTime - cycleStartTime;
        }
        
        // Laske jäljellä oleva aika
        unsigned long timeLeft = 0;
        if (relayState) {
          timeLeft = onTimeThisCycle - elapsedTime;
          if (timeLeft > onTimeThisCycle) timeLeft = 0; // Overflow
        } else if (onTimeThisCycle > 0) {
          timeLeft = PWM_CYCLE_TIME - elapsedTime;
        }
        
        Serial.print("Tila: ");
        Serial.print(relayState ? "PÄÄLLÄ" : "POIS");
        Serial.print(", Jaksossa: ");
        Serial.print(elapsedTime / 60000.0, 1);
        Serial.print(" min / ");
        Serial.print(PWM_CYCLE_TIME / 60000.0, 1);
        Serial.print(" min");
        
        if (timeLeft > 0) {
          Serial.print(", Vaihtoon: ");
          Serial.print(timeLeft / 60000.0, 1);
          Serial.print(" min");
        }
        
        Serial.print(", Tehosuhde: ");
        Serial.print(currentDutyCycle * 100, 1);
        Serial.println("%");
        break;
      }
        
      case CYCLE_FINISHED: {
        unsigned long offTimeElapsed;
        if (currentTime < cycleEndTime) {
          offTimeElapsed = (4294967295UL - cycleEndTime) + currentTime;
        } else {
          offTimeElapsed = currentTime - cycleEndTime;
        }
        
        unsigned long timeLeft = MIN_OFF_BETWEEN_CYCLES - offTimeElapsed;
        if (timeLeft > MIN_OFF_BETWEEN_CYCLES) timeLeft = 0; // Overflow
        
        Serial.print("Tila: TAUKO");
        Serial.print(relayState ? " (rele PÄÄLLÄ)" : " (rele POIS)");
        Serial.print(", Kulunut: ");
        Serial.print(offTimeElapsed / 60000.0, 1);
        Serial.print(" min / ");
        Serial.print(MIN_OFF_BETWEEN_CYCLES / 60000.0, 0);
        Serial.print(" min, Uuteen jaksoon: ");
        Serial.print(timeLeft / 60000.0, 1);
        Serial.println(" min");
        break;
      }
        
      case CYCLE_WAITING:
        Serial.print("Tila: VALMIS (rele ");
        Serial.print(relayState ? "PÄÄLLÄ)" : "POIS)");
        Serial.println(", odotetaan seuraavaa jaksoa");
        break;
    }
  }
  
  // 4. Pieni viive säästää prosessointiaikaa
  delay(100);
}
