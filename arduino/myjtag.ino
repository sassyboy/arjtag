/** 
 *  Arduino's Pin Assignment
 */
#define PIN_TDI 2
#define PIN_TMS 3
#define PIN_TCK 4
#define PIN_TDO 5

/** 
 *  Arduino's JTAG Software Configuration
 */
bool PULLUP   = false;
bool DELAY    = false;
long DELAYUS  = 50;
#define MAX_DEV_NR 4
#define IDCODE_LEN 32
// Target specific, check your documentation or guess
#define SCAN_LEN                 1890 // used for IR enum. bigger the better
#define IR_LEN                   5
// IR registers must be IR_LEN wide:
#define IR_IDCODE                "01100" // always 011
#define IR_SAMPLE                "10100" // always 101
#define IR_PRELOAD               IR_SAMPLE

/** 
 *  LOW-LEVEL JTAG SIGNALLING
 */
void pulse_tms(int s_tms) {
  digitalWrite(PIN_TCK, LOW);
  digitalWrite(PIN_TMS, s_tms);
  digitalWrite(PIN_TCK, HIGH);
}
void pulse_tdi(int s_tdi) {
  if (DELAY) delayMicroseconds(DELAYUS);
  digitalWrite(PIN_TCK, LOW);
  digitalWrite(PIN_TDI, s_tdi);
  digitalWrite(PIN_TCK, HIGH);
}
byte pulse_tdo(){
  byte tdo_read;
  if (DELAY) delayMicroseconds(DELAYUS);
  digitalWrite(PIN_TCK, LOW); // read in TDO on falling edge
  tdo_read = digitalRead(PIN_TDO);
  digitalWrite(PIN_TCK, HIGH);
  return tdo_read;
}


/** TAP TMS states we care to use.
 *  NOTE: MSB sent first Meaning ALL TAP and IR codes
 *  have their leftmost bit sent first. This might be
 *  the reverse of what documentation for your target(s) show.
 */ 
#define TAP_RESET                "11111"       // looping 1 will return IDCODE if reg available
#define TAP_SHIFTDR              "111110100"
#define TAP_SHIFTIR              "1111101100" // -11111> Reset -0> Idle -1> SelectDR
                                              // -1> SelectIR -0> CaptureIR -0> ShiftIR
/*
 * Set the JTAG TAP state machine
 */
void tap_state(String tap_state){
  int tap_state_length = tap_state.length();
  for (int i=0; i < tap_state_length; i++) {
    if (DELAY) delayMicroseconds(DELAYUS);
    digitalWrite(PIN_TCK, LOW);
    digitalWrite(PIN_TMS, tap_state[i] - '0'); // conv from ascii pattern
    digitalWrite(PIN_TCK, HIGH); // rising edge shifts in TMS
  }
}

/* ir_state()
 * Set TAP to Reset then ShiftIR.
 * Shift in state[] as IR value.
 * Switch to ShiftDR state and end.
 */
void ir_state(String state) {

  tap_state(TAP_SHIFTIR);
  for (int i = 0; i < IR_LEN; i++) {
    if (DELAY) delayMicroseconds(DELAYUS);
    // TAP/TMS changes to Exit IR state (1) must be executed
    // at same time that the last TDI bit is sent:
    if (i == IR_LEN-1) {
      digitalWrite(PIN_TMS, HIGH); // ExitIR
    }
    pulse_tdi(state[i] - '0');
    // TMS already set to 0 "shiftir" state to shift in bit to IR
  }
  // a reset would cause IDCODE instruction to be selected again
  tap_state("1100"); // -1> UpdateIR -1> SelectDR -0> CaptureDR -0> ShiftDR
}

/**
 * High-level JTAG Commands
 */

void scan_idcode(){
  int i, j;
  int tdo_read;
  uint32_t idcodes[MAX_DEV_NR];
  uint32_t idcode;

  /* we hope that IDCODE is the default DR after reset */
  tap_state(TAP_RESET);
  tap_state(TAP_SHIFTDR);

  /* j is the number of bits we pulse into TDI and read from TDO */
  for(i = 0; i < MAX_DEV_NR; i++) {
    idcodes[i] = 0;
    for(j = 0; j < IDCODE_LEN;j++) {
      /* we send '0' in */
      pulse_tdi(0);
      tdo_read = digitalRead(PIN_TDO);
      if (tdo_read)
        idcodes[i] |= ( (uint32_t) 1 ) << j;
    } /* for(j=0; ... ) */
    /* save time: break at the first idcode with bit0 != 1 */
    if (!(idcodes[i] & 1) || idcodes[i] == 0xffffffff)
      break;
  } /* for(i=0; ...) */

  if (i > 0) {
    Serial.print("JTAG Devices: ");
    Serial.print(i,DEC);
    for(j = 0; j < i; j++) {
      Serial.print(" 0x");
      Serial.print(idcodes[j],HEX);
    }
    Serial.println("");
  } else {
    Serial.println("No JTAG Devices");
  }
  
}



static void sample(int iterations){
  // send instruction and go to ShiftDR
  ir_state(IR_SAMPLE);

  // Tell TAP to go to shiftout of selected data register (DR)
  // is determined by the instruction we sent, in our case
  // SAMPLE/boundary scan
  for (int i = 0; i < iterations; i++) {
    // no need to set TMS. It's set to the '0' state to
    // force a Shift DR by the TAP
    Serial.print(pulse_tdo(),DEC);
    if (i % 32  == 31 ) Serial.print(" ");
    if (i % 128 == 127) Serial.println();
  }
}

void boundary_scan(){
      sample(SCAN_LEN+100);
      Serial.println("");
}


////////////////
#define CMDLEN 20
char command[CMDLEN+1];
int cmd_indx = 0;

void exec(){
  Serial.print("Executing command:");
  Serial.println(command);
  if (strcmp(command, "idcode") == 0){
    scan_idcode();
  } else if (strcmp(command, "bscan") == 0){
    boundary_scan();
  } else {
    Serial.println("Commands: idcode, bscan");
  }
}

byte exec_svf_cmd(char tms, char tdi){
  byte tdo_read;
  if (DELAY) delayMicroseconds(DELAYUS);
  digitalWrite(PIN_TCK, LOW);
  //////////////////////////////////////
  // PUT TMS
  if (tms == '0')
    digitalWrite(PIN_TMS, LOW);
  else if (tms == '1')
    digitalWrite(PIN_TMS, HIGH);
  // PUT TDI
    if (tdi == '0')
      digitalWrite(PIN_TDI, LOW);
  else if (tdi == '1')
    digitalWrite(PIN_TDI, HIGH);
  // READ TDO
  tdo_read = digitalRead(PIN_TDO);
  ///////////////////////////////////
  digitalWrite(PIN_TCK, HIGH);
  return tdo_read;
}

void setup() {
  // Serial
  Serial.begin(115200);
  // Initialize JTAG PINS
  pinMode(PIN_TCK, OUTPUT);
  pinMode(PIN_TMS, OUTPUT);
  pinMode(PIN_TDI, OUTPUT);
  // Internal pullups default to logic 1
  pinMode(PIN_TDO, INPUT);
  digitalWrite(PIN_TDO, HIGH);
  scan_idcode();
}

void loop() {
  char inp;
  char tms, tdi;
  byte tdo;
  if (Serial.available() && cmd_indx < CMDLEN - 1){
    inp = Serial.read();
    if (inp == '\n' || inp == '\r'){
      if (!strncmp(command, "$RST", 4)){
        // Command: Reset the JTAG Programming.
        // We send the JTAG.IDCODE as response
        scan_idcode();
      } else if (cmd_indx == 4 && command[0]=='$'){
        // It's a valid command sent by our svf player
        tms = command[1];
        tdi = command[2];
        tdo = exec_svf_cmd(tms, tdi);
        if (tdo == LOW)
          Serial.println("TDO: 0");
        else if (tdo == HIGH)
          Serial.println("TDO: 1");
        else
          Serial.println("TDO: X");          
      } 
      // Prepare the buffer for the next command
      command[cmd_indx] = 0;
      cmd_indx = 0;
      Serial.flush();
    } else {
//      Serial.println(inp); // Disabled echo back
      command[cmd_indx++] = inp;
    }
  }
}
