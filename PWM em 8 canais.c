

   ifdef __16F88

   #include <p16f88.inc>

      ;Program Configuration Register 1
      __CONFIG    _CONFIG1, _CP_OFF & _CCP1_RB0 & _DEBUG_OFF & _WRT_PROTECT_OFF & _CPD_OFF & _LVP_OFF & _BODEN_ON & _MCLR_ON & _PWRTE_ON & _WDT_OFF & _INTRC_IO

      ;Program Configuration Register 2
      __CONFIG    _CONFIG2, _IESO_OFF & _FCMEN_OFF

   #define T0IF   TMR0IF
   #define T0IE   TMR0IE

   endif

              ifdef __16F628A

               #include <p16f628A.inc>      ; Standard include file
                                            ; defines processor specific variable definitions
                                            ; See MPASM UG Section 4.41

              ;------------------------------------------------------------------------------------
              ; '__CONFIG' directive is used to embed configuration data within .asm file.
              ; The lables following the directive are located in the respective .inc file.
              ; See respective data sheet for additional information on configuration word.
              ; and also see MPASM UG Section 4.12

               __CONFIG   _CP_OFF & _WDT_ON & _PWRTE_ON & _INTRC_OSC_NOCLKOUT & _LVP_OFF

              endif

              ifdef __16F628

               #include <p16f628.inc>
               __CONFIG   _CP_OFF & _WDT_ON & _PWRTE_ON & _INTRC_OSC_NOCLKOUT & _LVP_OFF

              endif

              ifdef __16F84A

   ; **** 16F84A only has 1K of program memory - the default pwm_SeqData.inc file ****
   ; **** will not fit into the available memory, you must remove some of the     ****
   ; **** the sequence data from the file before assembling the code              ****
               #include <p16f84a.inc>
                __CONFIG   _CP_OFF & _WDT_ON & _PWRTE_ON & _HS_OSC
              ; Code protect off
              ; Watch Dog timer off
              ; Power-up timer delay on
              ;   change to _RC_OSC for resistor/capacitor
              ;   change to _XT_OSC for crystal/resonator <3.5Mhz
              ;   change to _HS_OSC for high speed crystal/resonator >3.5Mhz

              endif

              ;------------------------------------------------------------------------------------
              ; Suppress specific warning messages
              ; see MPASM UG Section 4.29 for errorlevel directive
              ; see MPASM UG Section 8.4 for assembler message descriptions
              ;


              errorlevel -302     ; suppress banksel warning messages during assembly


              ;------------------------------------------------------------------------------------
              ; define variables in General Purpose Register (GPR) memory
              ; See MPASM UG Section 4.8
              ; note: 16F84A GPR start at 0x0C but most other mid-range PICs start at 0x20
              ; We use 0x20 here for compatibilty with newer PICs

              cblock 0x20
                firstGPR:0                ; mark start of GPR memory used
                state                     ; used by function state selector
                copyPORTB                 ; working variable, holds copy of PORTB
                vc0,vc1,vc2,vc3,vc4       ; vertical counter bits
                hiReload                  ; vertical counter reload hi
                loReload                  ; vertical counter reload lo
                pwm                       ; pwm counter
                holdTime                  ; sequence line hold time
                repeatCount               ; repeat seqeunce count
                RandMask                  ; mask to AND random number with
                seqIdxLo                  ; Lo byte address index pointer to start of selected sequence
                seqIdxHi                  ; Hi byte address index pointer to start of selected sequence
                seqTotal                  ; Total number of sequences in data
                seqMatch                  ; working variable
                seqCount                  ; working variable
                forward                   ; working regsiter for mirroring data
                reverse                   ; working regsiter for mirroring data
                reloadTemp                ; temporary holding variable for loReload
                tick                      ; timer0 ticks
                swTimer                   ; switch hold down timer
                flags                     ; function flags register
     mode      ; mode flags register
                indexLo                   ; Lo byte sequence line data address pointer
                indexHi                   ; Hi byte sequence line data address pointer
                save_W                    ; save W during interupt
                save_Status               ; save STATUS during interupt
                save_PCLATH               ; save PCLATH
     saveModeTimerL   ; holdoff timer for saving opmode to EEPROM high
     saveModeTimerH   ; holdoff timer for saving opmode to EEPROM low
     eesave_W      ; temp register used in EEPROM routings
                LFSRH                     ; Random number shift regsiter high byte
                LFSRL:0                   ; Random number shift register low byte
                lastGPR                   ; mark last GPR register used
                                          ; (required by GPR initialisation code)

              endc

;--------------------------------------------------------------------------------------------------------------------
; EEPROM data
;    ; program parameters
   org 0x2100
   de .0
   de .1
   de .0

   ; embed firmware release info
   de " PWM LED Chaser "
   de " V1.0.7 "
   de " 03/04/2009 "
   de " picprojects.org.uk "
;--------------------------------------------------------------------------------------------------------------------

; Define bank select pseudo instructions to make code more readable
#define   bank0     bcf       STATUS,RP0          ; Sel Bank 0
#define   bank1     bsf       STATUS,RP0          ; Sel Bank 1

;--------------------------------------------------------------------------------------------------------------------
;
; See MPASM UG Secdtion 4.27 for definition of equ directive

switch        equ 4         ; Port bit switch connected to

cSAVETIME EQU     d'4'   ; holdoff timer constant for saving EEPROM data
cTIMER    EQU   d'157'
cTICKS    EQU   d'50'
; 4Mhz oscillator clock, gives TMR0 clock = 1Mhz.
; Write to timer0 inhibits clock for 2 clock cycles
; TMR0 prescaler of 1:2 gives
; 1Mhz / ((256-157+2) x 2) = 5Khz = 0.2mS
; 50 x 0.2mS = 10mS
; So the sequence hold time will be 10mS x holdTime, giving a range of
; 1 = 10mS thru 254 = 2.54S
; remember that a holdtime value of 255 indicates the end of the sequence so DON'T USE IT

; define flag bits
fHoldTimeout  EQU           0             ; flag set by ISR, cleared by Table Lookup function
fSwitch       EQU           1             ; flag
fsetupRun     EQU           2             ; 0=run mode / 1=setup mode
fTick         EQU           3             ; timer tick flag
fSaveMode   EQU   4   ; set indicates opmode needs writing to EEPROM, clear indicates no data to write
;      5
fMirrorData   EQU           6             ; flag set, Table read reverses bits in data word [** This flag must be Bit 6 **]
fMirrorNext   EQU           7             ; flag tells code current sequence should be mirrored next time

#define   canMirror 1<<fMirrorData        ; Don't change the position of this bit, sequence data expects it here

; define mode bits
modeSeq   EQU   0
modeRan   EQU   1
modeMan   EQU   2
modeSleep   EQU   3

;--------------------------------------------------------------------------------------------------------------------
; Program code starts here
;
; See MPASM UG Section 1.7.1, and Example 1-1
;
; Labels      Mnemonics     Operands                     Comments
;             Directives
;             Macros
;
RESET_VECTOR  org           0x000                       ; see MPASM UG Section 4.50
              goto          START                       ; START label is in pwmc_start.inc

INTERRUPT_V   org           0x004                       ; Interrupt handler code must follow this origin statement
              #include pwmc_inth.inc                    ; include Interrupt Handler code block

              #include pwmc_statef.inc                  ; include main state function code block (must reside in page 0)
              #include pwmc_start.inc                   ; include reset and initialisation code
              #include pwmc_functions.inc               ; include all program functions
              #include pwmc_SeqMacro.inc                ; include MACRO definitions for sequence data
                                                        ; used for creating sequence data tables

; ------------------------------------------------------------------------------------------------------

