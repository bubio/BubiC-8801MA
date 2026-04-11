/*
    Skelton for retropc emulator

    Author : Takeda.Toshiya
    Date   : 2026.04.11 -

    [ PIO flow JSONL logger ]

    Optional cross-emulator PIO 8255 data-flow trace.  When enabled
    via the environment variable BUBIC_PIO_LOG (any non-zero value),
    every top-level Port A/B/C access from either I8255 instance is
    recorded to a newline-delimited JSON file in the format used by
    Bubilator88's `PIOFlowJSONL.render(_:)` so that the two logs can
    be `diff`ed directly when investigating cross-CPU hand-shake
    discrepancies (e.g. RIGLAS.D88).

    Output format (one line per event):

        {"seq":42,"mainPC":"1C5A","subPC":"6830","side":"sub","port":"B","op":"W","val":"3B"}

    Environment variables:
        BUBIC_PIO_LOG        — enable when set to anything but "0"
        BUBIC_PIO_LOG_FILE   — output path (default: bubic-pioflow.jsonl)
*/

#ifndef _PIOFLOW_LOG_H_
#define _PIOFLOW_LOG_H_

#include <stdint.h>

class I8255;
class Z80;

// Install the main/sub I8255 and Z80 pointers so log events can be
// tagged with the correct "side" ("main"/"sub") and PC values.
// Safe to call more than once; subsequent calls overwrite.
void pioflow_log_set_context(I8255* main_pio, I8255* sub_pio, Z80* main_cpu, Z80* sub_cpu);

// Record one top-level port access. No-op when BUBIC_PIO_LOG is unset
// or empty, so the overhead when disabled is a single getenv + one
// NULL check on the file pointer.
void pioflow_log_write(const I8255* pio, int ch, uint32_t data);
void pioflow_log_read (const I8255* pio, int ch, uint32_t data);

// Record a control-register write (mode set or BSR) at port 0xFF.
// Emitted as a synthetic port "FF" write in the JSONL stream so it
// appears inline with A/B/C events for handshake-sequence debugging.
void pioflow_log_control(const I8255* pio, uint32_t data);

// RAII helper: skip nested calls in the i8255.cpp recursion. Used
// inside write_io8 / read_io8 to suppress logging of control-driven
// side-effect writes to Port C.
class PIOFlowLogScope {
public:
    PIOFlowLogScope();
    ~PIOFlowLogScope();
    bool is_top_level() const { return top_level; }
private:
    bool top_level;
};

#endif  // _PIOFLOW_LOG_H_
