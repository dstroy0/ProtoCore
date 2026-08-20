// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_clock.c
 * @brief Which clock the HTTP Date header reads - implementation. See http_clock.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP_CLOCK

#include "mmgr/plaintext/plaintext.h" // the end this module's bytes come from
#include "server/io/http_clock/http_clock.h"
#include "shared/http_date/http_date.h" // HttpDate.format: the rendering this chooses an instant for

#if PROTOCORE_ENABLE_TIME_SOURCE
#include "services/timing_position/time_source/time_source.h" // protocore_time_now: the registry's best source
#elif PROTOCORE_ENABLE_NTP
#include "network_drivers/application/ntp_service/ntp_service.h" // NtpService.epoch: the only clock here
#endif

PROTOCORE_BEGIN_DECLS

// PROTOCORE_HTTP_CLOCK_BORROW - the bytes this module runs out of - is stated in protocore_config.h,
// which sums it into the plaintext arena. One region: the rendered date, held from the render until
// the caller has copied it out.
#define HTTP_CLOCK_OFF_IMF 0u
static_assert(HTTP_CLOCK_OFF_IMF + PROTOCORE_HTTP_DATE_MAX <= PROTOCORE_HTTP_CLOCK_BORROW,
              "PROTOCORE_HTTP_CLOCK_BORROW is smaller than the IMF-fixdate it has to hold - raise it to at least "
              "PROTOCORE_HTTP_DATE_MAX");

// char, so alignment is 1 by the standard and the offset needs no alignment assert.
#define HTTP_CLOCK_IMF(w) ((char *)(void *)((w) + HTTP_CLOCK_OFF_IMF))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_HTTP_CLOCK_BORROW persistent bytes
} HttpClockOwnCtx;
static HttpClockOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_http_clock_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_HTTP_CLOCK_BORROW).buf;
    }
    return s_own.span;
}

void protocore_http_clock_date(uint8_t *restrict work)
{
    // Zero is what both arms below produce before a sync, and it is what HttpDate.format renders as
    // an empty string - so a clock-less boot emits no Date rather than a wrong one.
    time_t epoch = 0;

#if PROTOCORE_ENABLE_TIME_SOURCE
    // The registry answers from whichever of NTP / GPS / RTC / ... is registered and has valid
    // time, by priority. It subsumes the NTP-only arm, so it is asked first.
    epoch = (time_t)protocore_time_now();
#elif PROTOCORE_ENABLE_NTP
    // NtpService.epoch DEREFERENCES the borrow it is handed - it reads the sync instant out of
    // NTP_SERVICE_CTX - so it gets that module's own span, not this one's.
    NtpService.epoch(protocore_ntp_service_span());
    epoch = NtpServiceV.value;
#endif

    HttpDateV.args.epoch = epoch;
    HttpDateV.args.out = HTTP_CLOCK_IMF(work);
    HttpDateV.args.out_cap = PROTOCORE_HTTP_DATE_MAX;
    HttpDate.format(work);

    HttpClockV.n = HttpDateV.n;
    HttpClockV.imf = HTTP_CLOCK_IMF(work);
}

/** @brief The operands and the outcome. */
HttpClockVars HttpClockV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_CLOCK
