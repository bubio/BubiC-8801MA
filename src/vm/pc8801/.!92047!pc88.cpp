/*
	NEC PC-8001 Emulator 'ePC-8001'
	NEC PC-8001mkII Emulator 'ePC-8001mkII'
	NEC PC-8001mkIISR Emulator 'ePC-8001mkIISR'
	NEC PC-8801 Emulator 'ePC-8801'
	NEC PC-8801mkII Emulator 'ePC-8801mkII'
	NEC PC-8801MA Emulator 'ePC-8801MA'
	NEC PC-98DO Emulator 'ePC-98DO'

	Author : Takeda.Toshiya
	Date   : 2011.12.29-

	[ PC-8801 ]
*/

#include "pc88.h"
#include "../event.h"
#include "../i8251.h"
#include "../pcm1bit.h"
#include "../upd1990a.h"
#if defined(SUPPORT_PC88_OPN1) || defined(SUPPORT_PC88_OPN2)
#include "../ym2203.h"
#endif
#include "../z80.h"

#ifdef SUPPORT_PC88_FDD_8INCH
#include "../upd765a.h"
#endif

#ifdef SUPPORT_PC88_CDROM
#include "../scsi_cdrom.h"
#include "../scsi_host.h"
#endif

#ifdef SUPPORT_PC88_16BIT
#include "../i8255.h"
#endif

#define DEVICE_JOYSTICK	0
#define DEVICE_MOUSE	1
#define DEVICE_JOYMOUSE	2	// not supported yet

#define EVENT_TIMER	0
#define EVENT_BUSREQ	1
#define EVENT_CMT_SEND	2
#define EVENT_CMT_DCD	3
#define EVENT_BEEP	4
#ifdef SUPPORT_PC88_CDROM
#define EVENT_FADE_IN	5
#define EVENT_FADE_OUT	6
#endif

#define IRQ_USART	0
#define IRQ_VRTC	1
#define IRQ_TIMER	2
#define IRQ_INT4	3
#define IRQ_SOUND	4
#define IRQ_INT2	5
#define IRQ_FDINT1	6
#define IRQ_FDINT2	7

#define Port30_40	!(port[0x30] & 0x01)
#define Port30_COLOR	!(port[0x30] & 0x02)
#define Port30_MTON	(port[0x30] & 0x08)
#define Port30_CMT	!(port[0x30] & 0x20)
#define Port30_RS232C	(port[0x30] & 0x20)

#define Port31_MMODE	(port[0x31] & 0x02)
#ifdef PC8801_VARIANT
#define Port31_RMODE	(port[0x31] & 0x04)
#endif
#define Port31_GRAPH	(port[0x31] & 0x08)
#if defined(_PC8001SR) || defined(PC8801_VARIANT)
#define Port31_HCOLOR	(port[0x31] & 0x10)
#else
#define Port31_HCOLOR	false
#endif
#ifdef PC8801_VARIANT
#define Port31_400LINE	!(port[0x31] & 0x11)
#else
#define Port31_400LINE	false
#endif

#ifdef PC8001_VARIANT
#define Port31_V1_320x200	(port[0x31] & 0x10)	// PC-8001 (V1)
#define Port31_V1_MONO		(port[0x31] & 0x04)	// PC-8001 (V1)
#define Port31_320x200	(port[0x31] & 0x04)	// PC-8001
#endif

#if defined(_PC8001SR)
#define Port32_SINTM	(port[0x33] & 0x02)	// PC-8001SR
#define Port32_GVAM	(port[0x33] & 0x40)	// PC-8001SR
#elif defined(PC8801SR_VARIANT)
#define Port32_GVAM	(port[0x32] & 0x40)
#define Port32_SINTM	(port[0x32] & 0x80)
#endif
#if defined(PC8801SR_VARIANT)
#define Port32_EROMSL	(port[0x32] & 0x03)
#define Port32_TMODE	(port[0x32] & 0x10)
#define Port32_PMODE	(port[0x32] & 0x20)
#else
#define Port32_EROMSL	0
#define Port32_TMODE	true
#define Port32_PMODE	false
#endif

#ifdef _PC8001SR
#define Port33_PR1	(port[0x33] & 0x04)	// PC-8001SR
#define Port33_PR2	(port[0x33] & 0x08)	// PC-8001SR
//#define Port33_SINTM	(port[0x33] & 0x02)	// PC-8001SR -> Port32_SINTM
#define Port33_HIRA	(port[0x33] & 0x10)	// PC-8001SR
//#define Port33_GVAM	(port[0x33] & 0x40)	// PC-8001SR -> Port32_GVAM
#define Port33_N80SR	(port[0x33] & 0x80)	// PC-8001SR
#endif

#if defined(_PC8001SR) || defined(PC8801SR_VARIANT)
#define Port34_ALU	port[0x34]

#define Port35_PLN0	(port[0x35] & 0x01)
#define Port35_PLN1	(port[0x35] & 0x02)
#define Port35_PLN2	(port[0x35] & 0x04)
#define Port35_GDM	(port[0x35] & 0x30)
#define Port35_GAM	(port[0x35] & 0x80)
#endif

#if defined(_PC8001SR) || defined(PC8801SR_VARIANT)
#define Port40_GHSM	(port[0x40] & 0x10)
#else
#define Port40_GHSM	false
#endif
#define Port40_JOP1	(port[0x40] & 0x40)

#ifdef SUPPORT_PC88_OPN1
#define Port44_OPNCH	port[0x44]
#endif

#define Port53_TEXTDS	(port[0x53] & 0x01)
#define Port53_G0DS	(port[0x53] & 0x02)
#define Port53_G1DS	(port[0x53] & 0x04)
#define Port53_G2DS	(port[0x53] & 0x08)
#if defined(PC8001_VARIANT)
#define Port53_G3DS	(port[0x53] & 0x10)	// PC-8001
#define Port53_G4DS	(port[0x53] & 0x20)	// PC-8001
#define Port53_G5DS	(port[0x53] & 0x40)	// PC-8001
#endif

#ifdef SUPPORT_PC88_16BIT
#define Port82_BOOT16	(!(port[0x82] & 0x01))
#endif

#if defined(PC8801_VARIANT)
#define Port70_TEXTWND	port[0x70]
#endif

#if defined(_PC8001SR) || defined(PC8801_VARIANT)
#define Port71_EROM	port[0x71]
#endif

#ifdef SUPPORT_PC88_CDROM
#define Port99_CDREN	(port[0x99] & 0x10)
#endif

// XM8 version 1.20
#ifdef SUPPORT_PC88_OPN2
#define PortA8_OPNCH	port[0xa8]
#define PortAA_S2INTM	(port[0xaa] & 0x80)
#endif

#ifdef PC88_EXRAM_BANKS
#define PortE2_RDEN	(port[0xe2] & 0x01)
#define PortE2_WREN	(port[0xe2] & 0x10)

#if !defined(PC8001_VARIANT)
#define PortE3_ERAMSL	(port[0xe3] & 0x0f)
#endif
#endif

#ifdef SUPPORT_PC88_KANJI1
#define PortE8E9_KANJI1	(port[0xe8] | (port[0xe9] << 8))
#endif
#ifdef SUPPORT_PC88_KANJI2
#define PortECED_KANJI2	(port[0xec] | (port[0xed] << 8))
#endif

#ifdef SUPPORT_PC88_DICTIONARY
#define PortF0_DICROMSL	(port[0xf0] & 0x1f)
#define PortF1_DICROM	!(port[0xf1] & 0x01)
#endif

#ifdef SUPPORT_PC88_VAB
// X88000
#define PortB4_VAB_DISP	((port[0xb4] & 0x41) == 0x41)
#define PortE3_VAB_SEL	(((port[0xe3] >> 2) & 3) == PC88_VAB_PAGE)
#endif

#define SET_BANK(s, e, w, r) { \
	int sb = (s) >> 12, eb = (e) >> 12; \
	for(int i = sb; i <= eb; i++) { \
		if((w) == wdmy) { \
			wbank[i] = wdmy; \
		} else { \
			wbank[i] = (w) + 0x1000 * (i - sb); \
		} \
		if((r) == rdmy) { \
			rbank[i] = rdmy; \
		} else { \
			rbank[i] = (r) + 0x1000 * (i - sb); \
		} \
	} \
}

#define SET_BANK_W(s, e, w) { \
	int sb = (s) >> 12, eb = (e) >> 12; \
	for(int i = sb; i <= eb; i++) { \
		if((w) == wdmy) { \
			wbank[i] = wdmy; \
		} else { \
			wbank[i] = (w) + 0x1000 * (i - sb); \
		} \
	} \
}

#define SET_BANK_R(s, e, r) { \
	int sb = (s) >> 12, eb = (e) >> 12; \
	for(int i = sb; i <= eb; i++) { \
		if((r) == rdmy) { \
			rbank[i] = rdmy; \
		} else { \
			rbank[i] = (r) + 0x1000 * (i - sb); \
		} \
	} \
}

static const int key_table[15][8] = {
	{ 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67 },
	{ 0x68, 0x69, 0x6a, 0x6b, 0x92, 0x6c, 0x6e, 0x0d },	// 0x92 = VK_OEM_NEC_EQUAL
	{ 0xc0, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47 },
	{ 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f },
	{ 0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57 },
	{ 0x58, 0x59, 0x5a, 0xdb, 0xdc, 0xdd, 0xde, 0xbd },
	{ 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37 },
	{ 0x38, 0x39, 0xba, 0xbb, 0xbc, 0xbe, 0xbf, 0xe2 },
//	{ 0x24, 0x26, 0x27, 0x2e, 0x12, 0x15, 0x10, 0x11 },
	{ 0x24, 0x26, 0x27, 0x08, 0x12, 0x15, 0x10, 0x11 },
	{ 0x13, 0x70, 0x71, 0x72, 0x73, 0x74, 0x20, 0x1b },
	{ 0x09, 0x28, 0x25, 0x23, 0x7b, 0x6d, 0x6f, 0x14 },
	{ 0x21, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
	{ 0x75, 0x76, 0x77, 0x78, 0x79, 0x08, 0x2d, 0x2e },
	{ 0x1c, 0x1d, 0x7a, 0x19, 0x00, 0x00, 0x00, 0x00 },
	{ 0x0d, 0x00, 0xa0, 0xa1, 0x00, 0x00, 0x00, 0x00 }
};

static const int key_conv_table[][3] = {
	{0x2d, 0x2e, 1}, // INS	-> SHIFT + DEL
	{0x75, 0x70, 1}, // F6	-> SHIFT + F1
	{0x76, 0x71, 1}, // F7	-> SHIFT + F2
	{0x77, 0x72, 1}, // F8	-> SHIFT + F3
	{0x78, 0x73, 1}, // F9	-> SHIFT + F4
	{0x79, 0x74, 1}, // F10	-> SHIFT + F5
//	{0x08, 0x2e, 0}, // BS	-> DEL
	{0x2e, 0x08, 0}, // DEL	-> BS
