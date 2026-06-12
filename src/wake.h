#ifndef WAKE_DONGLE_WAKE_H
#define WAKE_DONGLE_WAKE_H

void wake_init(void);
// Request a host wake (no-op unless the host is suspended and the FSM is
// armed). Called when the configured BLE device is seen advertising.
void wake_trigger(void);
void wake_task(void);

extern volatile bool host_suspended;

#endif // WAKE_DONGLE_WAKE_H
