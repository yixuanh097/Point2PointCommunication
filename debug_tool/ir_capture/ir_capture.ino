/* ---------------------------------------------------------------------------
   IR FRAME CAPTURE / TIMING ANALYZER  --  diagnostic tool, not the receiver
   ---------------------------------------------------------------------------
   Purpose: record the ACTUAL timing of what the far transmitter emits, as seen
   through the VS1838B, so the receiver's assumptions can be checked against
   measured reality instead of nominal design values.

   Deliberately minimal:
     - no LCD, no Wire, no tone()  -> nothing perturbs the timing
     - this unit NEVER transmits   -> every edge logged is the far transmitter
     - ISR only stores micros() + level; all printing happens after the frame

   HOW TO USE
     1. Upload to the RECEIVER Arduino. Open Serial Monitor at 115200.
     2. Send ONE full message from the transmitter (e.g. 1 2 3 4 ...).
     3. When the line goes quiet the frame is dumped as a MARK/SPACE table.
     4. Read off: header width, bit slot width, digit period, inter-digit gap.
--------------------------------------------------------------------------- */

const int rxPin = 2;

const int MAXEDGES = 300;              // ~300 edges covers a 10-digit message
volatile unsigned long edgeUs[MAXEDGES];
volatile uint8_t       edgeLvl[MAXEDGES];
volatile int           nEdges = 0;
volatile unsigned long lastEdgeUs = 0;

// Silence longer than this means the message is over and we can dump.
const unsigned long IDLE_US = 400000UL;   // 400 ms

void setup() {
  pinMode(rxPin, INPUT_PULLUP);
  Serial.begin(115200);                   // fast: keeps print time small
  Serial.println(F("IR capture ready. Send one message from the transmitter."));
  attachInterrupt(digitalPinToInterrupt(rxPin), onEdge, CHANGE);
}

void loop() {
  if (nEdges > 0 && (micros() - lastEdgeUs) > IDLE_US) {
    dumpFrame();
    noInterrupts();
    nEdges = 0;                            // re-arm for the next message
    interrupts();
    Serial.println(F("\n--- ready for next message ---\n"));
  }
}

void onEdge() {
  // Keep this tiny. digitalRead + micros only; no Serial, no math.
  unsigned long t = micros();
  if (nEdges < MAXEDGES) {
    edgeUs[nEdges]  = t;
    edgeLvl[nEdges] = digitalRead(rxPin);
    nEdges++;
  }
  lastEdgeUs = t;
}

void dumpFrame() {
  noInterrupts();
  int n = nEdges;
  interrupts();

  Serial.println(F("========== CAPTURED FRAME =========="));
  Serial.print(F("edges: "));
  Serial.println(n);
  if (n >= MAXEDGES) Serial.println(F("*** BUFFER FULL - frame may be truncated ***"));
  Serial.println(F("idx  type    ms     (start ms from first edge)"));
  Serial.println(F("---------------------------------------------"));

  unsigned long t0 = edgeUs[0];

  // Each logged edge starts a segment that ends at the NEXT edge.
  // VS1838B is active-low: level LOW after the edge  => carrier ON  => MARK
  //                        level HIGH after the edge => carrier OFF => SPACE
  for (int i = 0; i < n - 1; i++) {
    unsigned long durUs   = edgeUs[i + 1] - edgeUs[i];
    unsigned long startMs = (edgeUs[i] - t0) / 1000UL;

    Serial.print(i);
    Serial.print(F("\t"));
    Serial.print(edgeLvl[i] == LOW ? F("MARK ") : F("space"));
    Serial.print(F("\t"));
    Serial.print(durUs / 1000.0, 1);       // duration in ms, 0.1 ms resolution
    Serial.print(F("\t("));
    Serial.print(startMs);
    Serial.println(F(")"));
  }

  Serial.println(F("---------------------------------------------"));
  Serial.print(F("total frame length (ms): "));
  Serial.println((edgeUs[n - 1] - t0) / 1000UL);

  // Longest mark is almost always the header - report it explicitly.
  unsigned long widest = 0;
  int widestIdx = -1;
  for (int i = 0; i < n - 1; i++) {
    if (edgeLvl[i] == LOW) {
      unsigned long d = edgeUs[i + 1] - edgeUs[i];
      if (d > widest) { widest = d; widestIdx = i; }
    }
  }
  if (widestIdx >= 0) {
    Serial.print(F("widest MARK (likely header): "));
    Serial.print(widest / 1000.0, 1);
    Serial.print(F(" ms at idx "));
    Serial.println(widestIdx);
  }
  Serial.println(F("===================================="));
}
