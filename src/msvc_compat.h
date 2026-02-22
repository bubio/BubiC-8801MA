#ifndef _MSVC_COMPAT_H_
#define _MSVC_COMPAT_H_

#if defined(_MSC_VER)

#if defined(_M_IX86)
#define __CSP_MACHINE_X86 1
#elif defined(_M_X64) || defined(_M_AMD64)
#define __CSP_MACHINE_X64 1
#elif defined(_M_ARM)
#define __CSP_MACHINE_ARM 1
#elif defined(_M_ARM64) || defined(_M_ARM64EC)
#define __CSP_MACHINE_ARM64 1
#endif

#if !defined(__CSP_MACHINE_ARM64) && !defined(__CSP_MACHINE_X64) && defined(_WIN64)
#define __CSP_MACHINE_X64 1
#endif

#ifndef __MACHINE
#define __MACHINE(X) X;
#endif

#ifndef __MACHINEI
#define __MACHINEI(X) X;
#endif

#ifndef __MACHINEX86
#if defined(__CSP_MACHINE_X86)
#define __MACHINEX86(X) X;
#else
#define __MACHINEX86(X)
#endif
#endif

#ifndef __MACHINEX64
#if defined(__CSP_MACHINE_X64)
#define __MACHINEX64(X) X;
#else
#define __MACHINEX64(X)
#endif
#endif

#ifndef __MACHINEX86_X64
#if defined(__CSP_MACHINE_X86) || defined(__CSP_MACHINE_X64)
#define __MACHINEX86_X64(X) X;
#else
#define __MACHINEX86_X64(X)
#endif
#endif

#ifndef __MACHINEARM
#if defined(__CSP_MACHINE_ARM)
#define __MACHINEARM(X) X;
#else
#define __MACHINEARM(X)
#endif
#endif

#ifndef __MACHINEARM64
#if defined(__CSP_MACHINE_ARM64)
#define __MACHINEARM64(X) X;
#else
#define __MACHINEARM64(X)
#endif
#endif

#ifndef __MACHINEARM_ARM64
#if defined(__CSP_MACHINE_ARM) || defined(__CSP_MACHINE_ARM64)
#define __MACHINEARM_ARM64(X) X;
#else
#define __MACHINEARM_ARM64(X)
#endif
#endif

#endif

#endif
