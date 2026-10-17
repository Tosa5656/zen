#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>

void apic_init(void);
void apic_send_eoi(void);
bool apic_is_enabled(void);

#endif
