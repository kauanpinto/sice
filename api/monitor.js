import { ALERT_CONFIG, createAlertMonitor } from "./alert-rules.js";
import { sendTelegramMessage } from "./telegram.js";

const POLL_INTERVAL_MS = 10_000;
const TOKEN_TTL_MS = 50 * 60 * 1000;
const REQUEST_TIMEOUT_MS = 10_000;
const HEARTBEAT_TICKS = 6;
const TIMEZONE = "America/Bahia";

const REQUIRED_ENV = [
  "FIREBASE_API_KEY",
  "FIREBASE_DATABASE_URL",
  "MONITOR_EMAIL",
  "MONITOR_PASSWORD",
  "TELEGRAM_BOT_TOKEN",
  "TELEGRAM_CHAT_ID",
];

export function createMonitorService({
  env,
  fetchImpl = fetch,
  now = Date.now,
  send = sendTelegramMessage,
  config = ALERT_CONFIG,
  log = console,
}) {
  const databaseUrl = env.FIREBASE_DATABASE_URL.replace(/\/+$/, "");
  const alertMonitor = createAlertMonitor(config);

  let idToken = null;
  let tokenObtainedAt = 0;
  let pendingMessage = null;
  let loginInFlight = null;
  let lastState = null;
  let ticks = 0;

  function ensureToken() {
    if (idToken && now() - tokenObtainedAt <= TOKEN_TTL_MS) return;

    loginInFlight ??= login().finally(() => {
      loginInFlight = null;
    });

    return loginInFlight;
  }

  async function login() {
    const response = await fetchImpl(
      `https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=${env.FIREBASE_API_KEY}`,
      {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          email: env.MONITOR_EMAIL,
          password: env.MONITOR_PASSWORD,
          returnSecureToken: true,
        }),
        signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
      },
    );

    if (!response.ok) {
      throw new Error(`Login do monitor falhou (HTTP ${response.status})`);
    }

    const data = await response.json();
    if (!data.idToken) throw new Error("Login do monitor sem idToken");

    idToken = data.idToken;
    tokenObtainedAt = now();
  }

  async function databaseGet(path, params = {}) {
    await ensureToken();

    const query = new URLSearchParams({ ...params, auth: idToken });
    const response = await fetchImpl(`${databaseUrl}/${path}.json?${query}`, {
      signal: AbortSignal.timeout(REQUEST_TIMEOUT_MS),
    });

    if (response.status === 401) {
      idToken = null;
      throw new Error("Token do Firebase recusado (401)");
    }
    if (!response.ok) {
      throw new Error(`Firebase HTTP ${response.status} em /${path}`);
    }

    return response.json();
  }

  async function fetchInputs() {
    const [relay, readings] = await Promise.all([
      databaseGet("control/relayState"),
      databaseGet("readings", { orderBy: '"measuredAt"', limitToLast: "1" }),
    ]);

    return {
      relayOn: typeof relay === "boolean" ? relay : null,
      reading: readings ? Object.values(readings)[0] : null,
    };
  }

  function buildMessage(since) {
    const time = new Date(since).toLocaleTimeString("pt-BR", {
      hour: "2-digit",
      minute: "2-digit",
      timeZone: TIMEZONE,
    });

    return `SICE: equipamento ligado sem presença e sem iluminação desde ${time}.`;
  }

  async function tick() {
    let inputs;

    try {
      inputs = await fetchInputs();
    } catch (error) {
      log.error("[monitor] Falha ao ler o Firebase:", error.message);
      return;
    }

    const result = alertMonitor.update(inputs, now());

    if (result.state !== lastState || ticks % HEARTBEAT_TICKS === 0) {
      const reading = inputs.reading;
      log.log(
        `[monitor] estado=${result.state}`,
        JSON.stringify({
          relayOn: inputs.relayOn,
          presence: reading?.presenceDetected ?? null,
          light: reading?.lightLevel ?? null,
          readingAgeS: reading ? Math.round((now() - reading.measuredAt) / 1000) : null,
        }),
      );
    }
    lastState = result.state;
    ticks++;

    if (result.notify) pendingMessage = buildMessage(result.since);
    if (result.state !== "alert") pendingMessage = null;
    if (!pendingMessage) return;

    try {
      await send({
        botToken: env.TELEGRAM_BOT_TOKEN,
        chatId: env.TELEGRAM_CHAT_ID,
        text: pendingMessage,
      });
      pendingMessage = null;
      log.log("[monitor] Mensagem enviada ao Telegram.");
    } catch (error) {
      log.error("[monitor] Falha ao enviar ao Telegram:", error.message);
    }
  }

  async function announceStart() {
    try {
      await send({
        botToken: env.TELEGRAM_BOT_TOKEN,
        chatId: env.TELEGRAM_CHAT_ID,
        text: "SICE: monitor de alertas online.",
      });
      log.log("[monitor] Telegram OK: mensagem de inicialização enviada.");
    } catch (error) {
      log.error("[monitor] Telegram FALHOU na inicialização:", error.message);
    }
  }

  function start() {
    let stopped = false;
    let timer = null;

    announceStart();

    async function loop() {
      await tick();
      if (!stopped) timer = setTimeout(loop, POLL_INTERVAL_MS);
    }

    loop();

    return () => {
      stopped = true;
      clearTimeout(timer);
    };
  }

  return { tick, start };
}

export function startMonitorFromEnv(env = process.env, log = console) {
  const missing = REQUIRED_ENV.filter((name) => !env[name]);

  if (missing.length > 0) {
    log.warn(`[monitor] Desativado. Variáveis ausentes: ${missing.join(", ")}`);
    return null;
  }

  log.log("[monitor] Monitor de alertas (Telegram) iniciado.");
  return createMonitorService({ env }).start();
}