import { initializeApp } from "https://www.gstatic.com/firebasejs/12.5.0/firebase-app.js";
import {
  getAuth,
  signInWithEmailAndPassword,
  onAuthStateChanged,
  signOut,
} from "https://www.gstatic.com/firebasejs/12.5.0/firebase-auth.js";
import {
  getDatabase,
  ref,
  onValue,
  query,
  orderByChild,
  limitToLast,
  set,
} from "https://www.gstatic.com/firebasejs/12.5.0/firebase-database.js";

const firebaseConfig = {
  apiKey: "AIzaSyDOdLahYbPKJXQssgXX2wxDtsXOHxGkB8w",
  authDomain: "sice-48e9c.firebaseapp.com",
  databaseURL: "https://sice-48e9c-default-rtdb.firebaseio.com",
  projectId: "sice-48e9c",
  appId: "1:162447557403:web:0ba7f522c263db06d949be",
};

const app = initializeApp(firebaseConfig);
const auth = getAuth(app);
const database = getDatabase(app);

const currentPage = window.location.pathname.split("/").pop();
const isLoginPage = currentPage === "login.html";
const isDashboardPage = currentPage === "dashboard.html";
const loginForm = document.getElementById("login-form");
const loginMessage = document.getElementById("login-message");
const loginButton = document.getElementById("login-button");
const logoutButton = document.getElementById("logout-button");

onAuthStateChanged(auth, (user) => {
  if (user && isLoginPage) {
    window.location.replace("dashboard.html");
    return;
  }

  if (!user && isDashboardPage) {
    window.location.replace("login.html");
    return;
  }

  if (user) {
    console.log("[SICE] Usuário autenticado.");
  }
});

if (loginForm) {
  loginForm.addEventListener("submit", async (event) => {
    event.preventDefault();

    const email = document.querySelector("#email").value.trim();
    const password = document.querySelector("#password").value;

    loginMessage.textContent = "";
    loginButton.disabled = true;
    loginButton.textContent = "Entrando...";

    try {
      await signInWithEmailAndPassword(auth, email, password);

      window.location.replace("dashboard.html");
    } catch (error) {
      console.error("[SICE] Erro no login:", error.code);

      if (
        error.code === "auth/invalid-credential" ||
        error.code === "auth/user-not-found" ||
        error.code === "auth/wrong-password"
      ) {
        loginMessage.textContent = "E-mail ou senha inválidos.";
      } else if (error.code === "auth/too-many-requests") {
        loginMessage.textContent =
          "Muitas tentativas. Tente novamente mais tarde.";
      } else {
        loginMessage.textContent = "Não foi possível entrar. Tente novamente.";
      }
    } finally {
      loginButton.disabled = false;
      loginButton.textContent = "Entrar";
    }
  });
}

if (logoutButton) {
  logoutButton.addEventListener("click", async () => {
    logoutButton.disabled = true;

    try {
      await signOut(auth);
      window.location.replace("login.html");
    } catch (error) {
      console.error("[SICE] Erro ao sair:", error.code);
      logoutButton.disabled = false;
    }
  });
}

let actualRelayState = false;

if (isDashboardPage) {
  const readingsQuery = query(
    ref(database, "readings"),
    orderByChild("measuredAt"),
    limitToLast(1),
  );

  onValue(
    readingsQuery,
    (snapshot) => {
      if (!snapshot.exists()) {
        console.warn("[SICE] Nenhuma leitura encontrada.");
        return;
      }

      const readings = snapshot.val();
      const latestReading = Object.values(readings)[0];

      document.getElementById("current-value").textContent = Number(
        latestReading.currentAmps,
      ).toFixed(2);

      document.getElementById("power-value").textContent = Number(
        latestReading.estimatedPowerW,
      ).toFixed(2);

      document.getElementById("light-value").textContent =
        latestReading.lightLevel;

      const presenceStatus = document.getElementById("presence-status");

      presenceStatus.textContent = latestReading.presenceDetected
        ? "Presença detectada"
        : "Sem presença";

      presenceStatus.classList.toggle(
        "status--online",
        latestReading.presenceDetected,
      );

      presenceStatus.classList.toggle(
        "status--offline",
        !latestReading.presenceDetected,
      );
    },
    (error) => {
      console.error("[SICE] Erro ao carregar leituras:", error);
    },
  );
}

if (isDashboardPage) {
  const connectionStatus = document.getElementById("connection-status");
  const relayStatus = document.getElementById("relay-status");
  const relayDescription = document.getElementById("relay-description");
  const relayButton = document.getElementById("relay-button");

  onValue(
    ref(database, "control/deviceConnected"),
    (snapshot) => {
      const isConnected = snapshot.val() === true;

      connectionStatus.textContent = isConnected ? "Conectado" : "Desconectado";

      connectionStatus.classList.toggle("status--online", isConnected);
      connectionStatus.classList.toggle("status--offline", !isConnected);
    },
    (error) => {
      console.error("[SICE] Erro ao consultar conexão:", error);
      connectionStatus.textContent = "Indisponível";
    },
  );

  onValue(
    ref(database, "control/relayState"),
    (snapshot) => {
      const isOn = snapshot.val() === true;

      actualRelayState = isOn;

      relayStatus.textContent = isOn ? "Ligado" : "Desligado";

      relayDescription.textContent = isOn
        ? "O equipamento está ligado."
        : "O equipamento está desligado.";

      relayButton.textContent = isOn
        ? "Desligar equipamento"
        : "Ligar equipamento";

      relayButton.setAttribute("aria-pressed", String(isOn));

      relayStatus.classList.toggle("status--online", isOn);
      relayStatus.classList.toggle("status--offline", !isOn);
    },
    (error) => {
      console.error("[SICE] Erro ao consultar relé:", error);
      relayStatus.textContent = "Indisponível";
      relayDescription.textContent =
        "Não foi possível consultar o estado do equipamento.";
    },
  );

  if (isDashboardPage && relayButton) {
    relayButton.addEventListener("click", async () => {
      const requestedState = !actualRelayState;

      relayButton.disabled = true;
      relayDescription.textContent = "Enviando solicitação...";

      try {
        await set(ref(database, "control/requestedState"), requestedState);

        console.log(
          "[SICE] Solicitação enviada:",
          requestedState ? "Ligar" : "Desligar",
        );

        relayDescription.textContent =
          "Solicitação enviada. Aguardando confirmação do ESP32.";
      } catch (error) {
        console.error("[SICE] Erro ao solicitar alteração:", error);

        relayDescription.textContent =
          "Não foi possível enviar o comando. Verifique sua conexão e permissões.";
      } finally {
        relayButton.disabled = false;
      }
    });
  }
}

if (isDashboardPage) {
  const historyList = document.getElementById("history-list");
  const historyMessage = document.getElementById("history-message");

  const recentHistoryQuery = query(
    ref(database, "history"),
    orderByChild("startedAt"),
    limitToLast(5),
  );

  onValue(
    recentHistoryQuery,
    (snapshot) => {
      historyList.replaceChildren();

      if (!snapshot.exists()) {
        historyMessage.textContent = "Nenhum evento registrado.";
        historyList.appendChild(historyMessage);
        return;
      }

      const records = Object.entries(snapshot.val())
        .map(([id, record]) => ({ id, ...record }))
        .sort((a, b) => b.startedAt - a.startedAt);

      records.forEach((record) => {
        const item = document.createElement("article");
        item.className = "history__item";

        const event = document.createElement("h3");
        event.className = "history__event";
        event.textContent =
          record.event === "ligado"
            ? "Equipamento ligado"
            : "Equipamento desligado";

        const date = document.createElement("p");
        date.className = "history__date";
        date.textContent = record.startedAt
          ? new Date(record.startedAt).toLocaleString("pt-BR")
          : "Data indisponível";

        const duration = document.createElement("p");
        duration.className = "history__duration";

        if (record.endedAt && record.startedAt) {
          const durationSeconds = Math.max(
            0,
            Math.floor((record.endedAt - record.startedAt) / 1000),
          );

          const hours = Math.floor(durationSeconds / 3600);
          const minutes = Math.floor((durationSeconds % 3600) / 60);
          const seconds = durationSeconds % 60;

          duration.textContent = `Duração: ${hours}h ${minutes}min ${seconds}s`;
        } else {
          duration.textContent = "Sessão em andamento";
        }

        const energy = document.createElement("p");
        energy.className = "history__energy";
        energy.textContent = `Energia: ${Number(record.energyWh ?? 0).toFixed(2)} Wh`;

        item.append(event, date, duration, energy);
        historyList.appendChild(item);
      });
    },
    (error) => {
      console.error("[SICE] Erro ao carregar histórico:", error);

      historyMessage.textContent = "Não foi possível carregar o histórico.";

      historyList.replaceChildren(historyMessage);
    },
  );
}
