const TELEGRAM_TIMEOUT_MS = 10_000;

export async function sendTelegramMessage(
  { botToken, chatId, text },
  fetchImpl = fetch,
) {
  const response = await fetchImpl(
    `https://api.telegram.org/bot${botToken}/sendMessage`,
    {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        chat_id: chatId,
        text,
        disable_web_page_preview: true,
      }),
      signal: AbortSignal.timeout(TELEGRAM_TIMEOUT_MS),
    },
  );

  if (!response.ok) {
    const data = await response.json().catch(() => ({}));
    throw new Error(
      `Telegram HTTP ${response.status}: ${data.description ?? "sem detalhes"}`,
    );
  }
}