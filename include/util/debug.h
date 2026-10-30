// SPDX-FileCopyrightText: 2024 KylinSoft Co., Ltd.
//
// SPDX-License-Identifier: GPL-1.0-or-later

#ifndef _UTIL_DEBUG_H_
#define _UTIL_DEBUG_H_

void debug_backtrace(void);

/* Unwind the faulting stack from a signal handler ucontext_t. */
void debug_backtrace_from(void *ucontext);

#ifdef TRACY_ENABLE
#include <tracy/TracyC.h>

#define KY_PROFILE_FRAME() TracyCFrameMark
#define KY_PROFILE_FRAME_NAME(name) TracyCFrameMarkNamed(name)
#define KY_PROFILE_ZONE(ctx, name) TracyCZoneN(ctx, name, true)
#define KY_PROFILE_ZONE_END(ctx) TracyCZoneEnd(ctx)

#else

#define KY_PROFILE_FRAME()
#define KY_PROFILE_FRAME_NAME(name)
#define KY_PROFILE_ZONE(ctx, name)
#define KY_PROFILE_ZONE_END(ctx)

#endif

#endif /* _UTIL_DEBUG_H_ */
