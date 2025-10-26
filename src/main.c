#include <avr/interrupt.h>

void state_machine(void);

int main(void)
{
    cli();
    // Call your initialisation functions here
    sei();

    state_machine();

    // The program should not reach this point
    while (1)
        ;
}

void state_machine(void)
{
    while (1)
    {
        // Implement your main loop here
    }
}
