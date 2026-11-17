export const ALERT_CONFIG = Object.freeze({
  graceMs: 2 * 60 * 1000,
  staleReadingMs: 2 * 60 * 1000,
  lightThreshold: 2048,
  darkWhen: "high",
});

export function isDark(lightLevel, config = ALERT_CONFIG) {
  return config.darkWhen === "high"
    ? lightLevel > config.lightThreshold
    : lightLevel < config.lightThreshold;
}

function isValidReading(reading) {
  return (
    reading != null &&
    typeof reading.presenceDetected === "boolean" &&
    Number.isFinite(reading.lightLevel) &&
    Number.isFinite(reading.measuredAt)
  );
}

export function evaluateCondition(
  { relayOn, reading },
  now,
  config = ALERT_CONFIG,
) {
  if (relayOn === false) return "clear";
  if (relayOn !== true) return "unknown";

  if (!isValidReading(reading)) return "unknown";
  if (now - reading.measuredAt > config.staleReadingMs) return "unknown";

  const noPresence = !reading.presenceDetected;
  const noLight = isDark(reading.lightLevel, config);

  return noPresence && noLight ? "triggered" : "clear";
}

export function createAlertMonitor(config = ALERT_CONFIG) {
  let since = null;
  let active = false;
  let dismissed = false;

  function reset() {
    since = null;
    active = false;
    dismissed = false;
  }

  return {
    update(inputs, now) {
      const verdict = evaluateCondition(inputs, now, config);

      if (verdict !== "triggered") {
        reset();
        return {
          state: verdict === "unknown" ? "unknown" : "ok",
          notify: false,
          since: null,
        };
      }

      if (since === null || now < since) since = now;

      if (now - since < config.graceMs) {
        return { state: "pending", notify: false, since };
      }

      const notify = !active;
      active = true;

      return { state: dismissed ? "dismissed" : "alert", notify, since };
    },

    dismiss() {
      if (active) dismissed = true;
    },
  };
}