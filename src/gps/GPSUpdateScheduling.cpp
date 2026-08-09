#include "GPSUpdateScheduling.h"

#include "Default.h"

// Mark the time when searching for GPS position begins
void GPSUpdateScheduling::informSearching()
{
    searching = true;
    searchStartedMs = millis();
}

// Mark the time when searching for GPS is complete,
// then update the predicted lock-time
void GPSUpdateScheduling::informGotLock()
{
    searching = false;
    searchEndedMs = millis();
    LOG_DEBUG("Took %us to get lock", (searchEndedMs - searchStartedMs) / 1000);
    updateLockTimePrediction();
    consecutiveFailures = 0; // Drop back to fast cadence as soon as we acquire any fix
}

// Search finished without obtaining a fix. We still need to mark the end time so
// the next sleep is timed correctly, but we must not feed the timeout duration
// into predictedMsToGetLock - doing so poisons msUntilNextSearch() and causes
// down() to fall into GPS_IDLE, leaving the chip awake on subsequent indoor cycles.
void GPSUpdateScheduling::informSearchFailed()
{
    searching = false;
    searchEndedMs = millis();
    consecutiveFailures++;
    LOG_DEBUG("GPS search ended without fix after %us (consecutive failures: %u)", (searchEndedMs - searchStartedMs) / 1000,
              consecutiveFailures);
}

// Clear old lock-time prediction data.
// When re-enabling GPS with user button.
void GPSUpdateScheduling::reset()
{
    searching = false;
    searchStartedMs = 0;
    searchEndedMs = 0;
    searchCount = 0;
    predictedMsToGetLock = 0;
    consecutiveFailures = 0;
}

// How many milliseconds before we should next search for GPS position
// Used by GPS hardware directly, to enter timed hardware sleep
uint32_t GPSUpdateScheduling::msUntilNextSearch()
{
    uint32_t now = millis();

    // Target interval (seconds), between GPS updates
    uint32_t updateInterval = Default::getConfiguredOrDefaultMs(config.position.gps_update_interval, default_gps_update_interval);

    // After a failed search, back off: indoors / no-sky environments will keep failing,
    // so wake at most once per broadcast interval rather than once per gps_update_interval.
    // Capped at 1 hour so a user-configured very-long broadcast interval still retries
    // periodically (in case conditions change). Reset on any successful lock.
    if (consecutiveFailures > 0) {
        constexpr uint32_t failureRetryCapMs = 60UL * 60UL * 1000UL; // 1 hour cap
        uint32_t failureSleepMs =
            Default::getConfiguredOrDefaultMs(config.position.position_broadcast_secs, default_broadcast_interval_secs);
        if (failureSleepMs > failureRetryCapMs)
            failureSleepMs = failureRetryCapMs;
        if (updateInterval < failureSleepMs)
            updateInterval = failureSleepMs;
    }

    // Check how long until we should start searching, to hopefully hit our target interval
    uint32_t dueAtMs = searchEndedMs + updateInterval;
    uint32_t compensatedStart = dueAtMs - predictedMsToGetLock;
    int32_t remainingMs = compensatedStart - now;

    // If we should have already started (negative value), start ASAP
    if (remainingMs < 0)
        remainingMs = 0;

    return (uint32_t)remainingMs;
}

// How long have we already been searching?
// Used to abort a search in progress, if it runs unacceptably long
uint32_t GPSUpdateScheduling::elapsedSearchMs()
{
    // An explicit flag, NOT (searchStartedMs > searchEndedMs): that inference compares two
    // absolute millis() stamps, so once millis() wraps (day 49.7) a search started after the
    // wrap reads as "not searching", elapsed stays 0, searchedTooLong() can never end the
    // window, and the GPS pins itself GPS_ACTIVE forever. The subtraction below is modular
    // arithmetic and wrap-safe on its own.
    if (searching)
        return millis() - searchStartedMs;

    // If not searching - 0ms. We shouldn't really consume this value
    else
        return 0;
}

// Is it now time to begin searching for a GPS position?
bool GPSUpdateScheduling::isUpdateDue()
{
    return (msUntilNextSearch() == 0);
}

// Have we been searching for a GPS position for too long?
bool GPSUpdateScheduling::searchedTooLong()
{
    constexpr uint32_t oneMinuteMs = 60UL * 1000UL;
    constexpr uint32_t baseSearchClampMs = 15UL * oneMinuteMs;     // upstream default (4ccdd8009 / #10293)
    constexpr uint32_t absoluteSearchClampMs = 45UL * oneMinuteMs; // never search longer than this, ever
    constexpr uint32_t postFailureSearchMs = 5UL * oneMinuteMs;    // Tighter dwell once we know the environment is hostile
    uint32_t elapsed = elapsedSearchMs();

    // Keep upstream's flat cap as the default, but never cap below twice this node's OWN measured lock
    // time: a cap that sits under the real TTFF turns every search into a guaranteed failure, and the
    // node can then never lock to clear consecutiveFailures. Raising the BASE instead would cost every
    // never-locking node on every board double the GPS-active time (the later position_broadcast_secs
    // test can never bind below 1h, so this cap is the only real limit), for nodes that have shown no
    // evidence of needing it. Keying off predictedMsToGetLock self-limits the exception to nodes that
    // have actually measured a slow lock here; it is only populated once we have measured one (from the
    // second - the first is discarded), so a node with no measurement keeps upstream's behaviour
    // exactly. This costs nothing on a healthy node: a successful search ends at informGotLock(), long
    // before any cap.
    uint32_t clampMs = baseSearchClampMs;
    if (predictedMsToGetLock > 0 && predictedMsToGetLock < absoluteSearchClampMs) {
        const uint32_t headroom = predictedMsToGetLock * 2; // < 90 min, cannot overflow
        if (headroom > clampMs)
            clampMs = headroom;
    }
    if (clampMs > absoluteSearchClampMs)
        clampMs = absoluteSearchClampMs;

    if (elapsed > clampMs)
        return true;

    // After a prior failed search, shorten the dwell - but ONLY when we have MEASURED how long a lock
    // takes here and that measurement comfortably fits the shortened dwell. Otherwise the shrink is
    // self-defeating: 5 minutes is below the cold-start TTFF of a module with no soft-sleep
    // (GPS_HARDSLEEP cuts the EN rail, so every wake loses the ephemeris and starts cold), so the
    // first failure pins the dwell at 5 minutes, every later 5-minute cold start also fails, and
    // consecutiveFailures can never reset - a permanent lock-out.
    //
    // Gate on predictedMsToGetLock, NOT on searchCount: updateLockTimePrediction() deliberately
    // discards the first lock as unrepresentative, so predictedMsToGetLock is only populated from the
    // SECOND lock onward. A node that has locked exactly once has searchCount == 1 but still tells us
    // nothing about its lock time - gating on searchCount would re-arm the lock-out on the very next
    // failure. Nodes that lock quickly and repeatedly still get the power saving; nodes whose lock is
    // slow, rare, or never keep the full dwell (bounded by the cap above), and msUntilNextSearch()'s
    // failure backoff remains the power lever that holds the retry cadence down.
    //
    // And bound how long we trust that measurement: it dates from the last SUCCESSFUL lock, so once
    // the environment degrades (module fault, antenna, desense) a historic "locks in 90s" reading
    // would pin every retry at a 5-minute dwell that a now-cold-and-slow module can never meet -
    // the same permanent lock-out the measurement gate exists to prevent. After three consecutive
    // failed shrunk dwells, treat the prediction as stale and return to the full dwell.
    if (consecutiveFailures > 0 && consecutiveFailures <= 3 && predictedMsToGetLock > 0 &&
        predictedMsToGetLock < postFailureSearchMs && elapsed > postFailureSearchMs)
        return true;

    uint32_t minimumOrConfiguredSecs =
        Default::getConfiguredOrMinimumValue(config.position.position_broadcast_secs, default_broadcast_interval_secs);
    uint32_t maxSearchMs = Default::getConfiguredOrDefaultMs(minimumOrConfiguredSecs, default_broadcast_interval_secs);

    // If we've been searching longer than our position broadcast interval: that's too long
    if (elapsed > maxSearchMs)
        return true;

    // Otherwise, not too long yet!
    return false;
}

// Updates the predicted time-to-get-lock, by exponentially smoothing the latest observation
void GPSUpdateScheduling::updateLockTimePrediction()
{

    // How long did it take to get GPS lock this time?
    // Duration between down() calls
    int32_t lockTime = searchEndedMs - searchStartedMs;
    if (lockTime < 0)
        lockTime = 0;

    // Ignore the first lock-time: likely to be long, will skew data

    // Second locktime: likely stable. Use to initialize the smoothing filter
    if (searchCount == 1)
        predictedMsToGetLock = lockTime;

    // Third locktime and after: predict using exponential smoothing. Respond slowly to changes
    else if (searchCount > 1)
        predictedMsToGetLock = (lockTime * weighting) + (predictedMsToGetLock * (1 - weighting));

    searchCount++; // Only tracked so we can disregard initial lock-times

    LOG_DEBUG("Predict %us to get next lock", predictedMsToGetLock / 1000);
}

// How long do we expect to spend searching for a lock?
uint32_t GPSUpdateScheduling::predictedSearchDurationMs()
{
    return GPSUpdateScheduling::predictedMsToGetLock;
}
