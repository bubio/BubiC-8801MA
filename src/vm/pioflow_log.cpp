/*
    Skelton for retropc emulator

    Author : Takeda.Toshiya
    Date   : 2026.04.11 -

    [ PIO flow JSONL logger ]
*/

#include "pioflow_log.h"
#include "i8255.h"
#include "z80.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Context populated once from pc8801.cpp after both I8255 and Z80
// instances exist. Used only for "side" labelling and PC lookup;
// the logger itself is otherwise stateless.
static const I8255* g_main_pio = NULL;
static const I8255* g_sub_pio  = NULL;
static Z80*         g_main_cpu = NULL;
static Z80*         g_sub_cpu  = NULL;

static FILE* g_log  = NULL;
static int   g_seq  = 0;
static bool  g_init = false;

// Depth counter for suppressing nested write_io8() calls fired as
// hand-shake side effects (Port C control writes triggered from
// read_io8 / write_io8 recursion inside i8255.cpp).
static int g_depth = 0;

static void pioflow_lazy_init()
{
    if(g_init) return;
    g_init = true;

    const char* enable = getenv("BUBIC_PIO_LOG");
    if(!enable || enable[0] == '\0' || enable[0] == '0') return;

    const char* path = getenv("BUBIC_PIO_LOG_FILE");
    if(!path || !*path) path = "bubic-pioflow.jsonl";

    g_log = fopen(path, "w");
    if(g_log != NULL) {
        // Line-buffered so a crash mid-investigation still leaves a
        // usable partial log.
        setvbuf(g_log, NULL, _IOLBF, 0);
        fprintf(stderr, "[pioflow_log] writing to %s\n", path);
    } else {
        fprintf(stderr, "[pioflow_log] failed to open %s\n", path);
    }
}

void pioflow_log_set_context(I8255* main_pio, I8255* sub_pio, Z80* main_cpu, Z80* sub_cpu)
{
    g_main_pio = main_pio;
    g_sub_pio  = sub_pio;
    g_main_cpu = main_cpu;
    g_sub_cpu  = sub_cpu;
    pioflow_lazy_init();
}

static void pioflow_emit(const I8255* pio, int ch, bool is_write, uint32_t val)
{
    if(!g_init) pioflow_lazy_init();
    if(g_log == NULL) return;
    if(ch < 0 || ch > 2) return;

    const char* side = "?";
    if(pio == g_main_pio)      side = "main";
    else if(pio == g_sub_pio)  side = "sub";

    const char* port = "?";
    switch(ch) {
    case 0: port = "A"; break;
    case 1: port = "B"; break;
    case 2: port = "C"; break;
    }

    uint32_t main_pc = g_main_cpu ? g_main_cpu->get_pc() : 0;
    uint32_t sub_pc  = g_sub_cpu  ? g_sub_cpu->get_pc()  : 0;

    fprintf(g_log,
        "{\"seq\":%d,\"mainPC\":\"%04X\",\"subPC\":\"%04X\",\"side\":\"%s\",\"port\":\"%s\",\"op\":\"%s\",\"val\":\"%02X\"}\n",
        g_seq++,
        main_pc & 0xFFFF,
        sub_pc  & 0xFFFF,
        side,
        port,
        is_write ? "W" : "R",
        val & 0xFF);
}

void pioflow_log_write(const I8255* pio, int ch, uint32_t data)
{
    pioflow_emit(pio, ch, true, data);
}

void pioflow_log_read(const I8255* pio, int ch, uint32_t data)
{
    pioflow_emit(pio, ch, false, data);
}

PIOFlowLogScope::PIOFlowLogScope()
{
    top_level = (g_depth == 0);
    g_depth++;
}

PIOFlowLogScope::~PIOFlowLogScope()
{
    g_depth--;
}
