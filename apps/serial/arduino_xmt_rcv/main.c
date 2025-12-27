#define BAUD 38400


#include <util/setbaud.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#if defined(__AVR_ATmega2560__)
#define SERIAL_RX_ISR USART0_RX_vect
#elif defined(__AVR_ATmega328__)
#define SERIAL_RX_ISR USART_RX_vect
#else
#error Unknown target processor
#endif

void uart_putchar(char c);

void on_error( const char* msg, uint8_t code )
{
  uart_putchar('0' + code );
  uart_putchar(':');
  for(; *msg; ++msg )
    uart_putchar(*msg);
        
}

// Basic UART setup code from here:
// https://appelsiini.net/2011/simple-usart-with-avr-libc/


void uart_init(void)
{
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;

#if USE_2X
    UCSR0A |= _BV(U2X0);
#else
    UCSR0A &= ~(_BV(U2X0));
#endif

    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00); // 8-bit data 
    UCSR0B = _BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0);   // Enable RX and TX and RX intertupt enable
}

void uart_putchar(char c)
{
  loop_until_bit_is_set(UCSR0A, UDRE0); // Wait until data register empty.
  UDR0 = c;
}

void uart_putchar_alt(char c)
{
  UDR0 = c;
  loop_until_bit_is_set(UCSR0A, TXC0); // Wait until transmission ready. 
}

char uart_getchar(void)
{
  loop_until_bit_is_set(UCSR0A, RXC0); // Wait until data exists. 
  return UDR0;
}


#define BUF_N (64)  // size of receive buffer

// Note that 'buf_i_idx' must be declared volatile or the
// the compare in the main loop will not work.
volatile int  buf_i_idx = 0;  // receive buffer input index
int           buf_o_idx = 0;  // receive buffer output index

// Receive buffer
char buf[ BUF_N ];


ISR(SERIAL_RX_ISR)
{
  // receive the incoming byte
  buf[ buf_i_idx ] = uart_getchar();

  // advance the buffer input index
  buf_i_idx = (buf_i_idx + 1) % BUF_N;  
}


int main (void)
{
  char c;

  cli();        // mask all interupts
  uart_init();  // setup UART data format and baud rate
  sei();        // re-enable interrupts

  // set pin 5 of PORTB for output
  DDRB |= _BV(DDB5);

  
  uart_putchar('a');
  
  for(;;)
  {
    // if there are bytes waiting in the receive buffer
    if( buf_o_idx != buf_i_idx )
    {
      // get the waiting byte
      c = buf[buf_o_idx];

      // advance the buffer output index
      buf_o_idx = (buf_o_idx+1) % BUF_N;

      // transmit the buffer input index as an asii char.      
      c += 1;

      PORTB ^= _BV(PORTB5);// toggle LED

      // transmit
      uart_putchar(c);
      
    }
        
  }
    
}
