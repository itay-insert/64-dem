#pragma once
#include "drivers/timers/timer.hpp"
#include "drivers/usb/xhci_events.h"

int xhci_init(TimerSource& timer);
void USB_init(void);

