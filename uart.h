#ifndef _SDLOCKER_UART_
#define _SDLOCKER_UART_


#define BAUD 38400L


extern FILE uart_output;
extern FILE uart_input;


extern void uart_init(void);
extern void uart_putchar(char c, FILE *stream);
extern char uart_getchar(FILE *stream);
extern uint8_t uart_pending_data();

#endif /* _SDLOCKER_UART_ */
