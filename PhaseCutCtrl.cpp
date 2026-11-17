#include "PhaseCutCtrl.h"
#include "Arduino.h"


//turn debug on. this gives some signals to pin13
//#define DEBUG_PCC 1

#define MAX_POWER 2048

#define AGGREGATE_COUNT 255 //  use 2^8, because a >>8 is a very fast operation
#define TCNT1_NOISE_OFFSET 200

PhaseCutCtrl PCCtrl; // pre-instatiate the class here

// define the interupt routines and redirect them to member methods

void ISR_acIsAcZero()
{
    // method for attached external interrupt for the zero phase signal
    PCCtrl.isr_AcZeroCallback();
}


ISR(TIMER1_COMPA_vect)
{
	// this is the prepared interrupt method for the OCR1
    PCCtrl.isr_OciCallback();
}

// Mapping function (costs about 8 mySeconds)

// <START OF GENERATED CODE> by map_generator.py --- 2019-01-16 14:14:28 -----
// Interpolation for 8 steps between x=0 and x=2048 with 256 x-values between each step
// Delta values are shiftet by 10  bits left for better accuracacy.
// ---------------------------------------------------------------------------

// arraydefinition for point_array ...
int point_array[8] = {17300, 14602, 12314, 10786, 10250, 9713, 8185, 5897};

// arraydefinition for delta_array ...
int delta_array[8] = {10791, 9148, 6109, 2144, 2146, 6111, 9148, 10787};
// ---------------------------------------------------------------------------


int xPartMask = (1 << 8) -1;


int mapFunction(int x)
{
    byte segment = x >> 8;
    int x_part = x & xPartMask;
    int delta = ((long)delta_array[segment] * x_part)  >> 10;
    int y = point_array[segment] - delta;
    return y;
}



// <END OF GENERATED CODE> ----------------------------------------------//


void PhaseCutCtrl::initialize(byte signal_pin, byte output_pin)
{
    this->output_pin = output_pin;
    this->signal_pin = signal_pin;
    pcc_power_last = 0;

    noInterrupts();
    pinMode(signal_pin, INPUT);
    attachInterrupt(digitalPinToInterrupt(signal_pin), ISR_acIsAcZero, RISING);

    pinMode(output_pin, OUTPUT);

    pcc_is_on = false;

    // set up timer interrupt for Timer1
    TCCR1A = 0;
    TCCR1B = 0;

    bitSet(TCCR1B, CS11);    // specify 8 als Prescaler for timer 1
    bitSet(TIMSK1, OCIE1A);  // activate Timer output compare Interrupt A at timer 1

    interrupts();             // activate all interrupts
    OCR1A = 0;

    samples_counter = 0;
    tcnt1_average = 20000; // assuming 50Hz
    tcnt1_noise_limit = tcnt1_average - TCNT1_NOISE_OFFSET;
#ifdef DEBUG_PCC
    pinMode(13, OUTPUT); // can use pin 13 for timing mesurement with an osziloscope
#endif // DEBUG_SPEEDCONTROL

}

void PhaseCutCtrl::isr_AcZeroCallback()
{
    // at zero phase _always_ reset timercounter. Other function waits till
    // OCR1A is reached.
	// function costs 4 mySec, with ifstatement: 6mySec.

    // save counter for solid state controller and measurement of net frequency


    if (TCNT1 < tcnt1_noise_limit)
    {
        // ignore all events that are much earlier then the expected event.
        // those are probably initiated by noise on the input wire
        return;
    }

#ifdef DEBUG_PCC
        PORTB |=  B00100000; //set pin13 back to HIGH for timemeasurement
#endif // DEBUG_PCC


    tcnt1_last_event = TCNT1; //this costs 400ns

    // reset counter
    TCNT1 = 0;

#ifdef DEBUG_PCC
    PORTB |=  B00100000; //set pin13 back to HIGH for timemeasurement
#endif // DEBUG_PCC

    tcnt1_aggregate += (tcnt1_last_event - 8); // add the missed cycle count here
    lastAcZeroMillis = millis(); // for acNetIsAlive()


    if (samples_counter++ == AGGREGATE_COUNT)
    {
         samples_counter = 0;
         tcnt1_average = int(tcnt1_aggregate >> 8); // divide by 256
         tcnt1_noise_limit = tcnt1_average - TCNT1_NOISE_OFFSET;
         tcnt1_aggregate = 0;
    }

#ifdef DEBUG_PCC
    PORTB &= ~B00100000; //set pin13 to LOW for timemeasurement
#endif // DEBUG_PCC

}

void PhaseCutCtrl::isr_OciCallback()
{
    // fires the pcm output pin if the OCR1A delay after phase zero is reached
    if (pcc_is_on)
    {
        digitalWrite(output_pin, 1); // pin on
        digitalWrite(output_pin, 0); // pin off
    }
}


void PhaseCutCtrl::waitUntilAcZero(int offsetMys)
// waits in a loop until the AC has passed the zero value. this method can be
// used to switch AC load with relais in the moment of solid state.
// offsetMys: fire xx mycroseconds before solid state

{
    int lowerLimit = tcnt1_average - (offsetMys * 2) - 1100;
    int upperLimit = lowerLimit + 200;

    while(true)
    {
         if ((TCNT1 > lowerLimit) and (TCNT1 < upperLimit))
         {
             return;
         }

    }

}


void PhaseCutCtrl::set_pcc(int pcc_power)
{
    // this calculates  and sets the delta for OCR1A register from timer interrupt

    if (pcc_power == 0)
    {
        // switched off
        pcc_is_on = false;
        digitalWrite(output_pin, LOW); // pin permanently LOW
    }
    else
    {
        if (pcc_power != pcc_power_last)
        {
            if (pcc_power == MAX_POWER)
            {
                // swiched on to 100% so no interrupt control but straight on
                pcc_is_on = false;
                digitalWrite(output_pin, HIGH); // pin permanently HIGH
            }
            else
            {
                // Let the speed be controlled by the interrupt methods
                pcc_is_on = true;
    #ifdef DEBUG_PCC
                PORTB |=  B00100000; //set pin13 back to HIGH for timemeasurement
    #endif // DEBUG_PCC

                // set the Output Compare Register 1 A
                int oc_value = mapFunction(pcc_power);
                OCR1A = oc_value;

    #ifdef DEBUG_PCC
                PORTB &= ~B00100000; //set pin13 to LOW for timemeasurement
                Serial.print("PCC:\t" + String(pcc_power) + "\t" + String(oc_value) + "\n");
    #endif // DEBUG_PCC

            }
        }
    }
    pcc_power_last = pcc_power;


}


bool PhaseCutCtrl::acNetIsAlive()
// return false if the last acZero event passed longer than 300ms
{
    return (millis() - lastAcZeroMillis < 300);

}

int PhaseCutCtrl::getNetFrequency()
// measures micros of 100 zero interrupt and calculates then
// the average netFrequency in centiHz (100 --> 1Hz )
// prescaler of timer1 is 8, cpu works on 16MHz
{
    return int(100000000 / tcnt1_average);

}

