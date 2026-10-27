## Como configurar

> Pré-requisito: ter concluído a configuração do [backend](../backend/README.md) e [firmware](../firmware/README.md)

### Arquivos

| Arquivo | Função |
|---|---|
| `login.html` | Tela de login |
| `dashboard.html` | Painel para ligar/desligar equipamentos e ver o consumo |
| `style.css` | Estilos das duas páginas |
| `script.js` | Lógica: autenticação, leitura e escrita no Firebase |

### Configuração

1. No `script.js`, preencha o objeto `firebaseConfig` com os dados do seu projeto:

```js
const firebaseConfig = {
  apiKey: "SUA_API_KEY",
  authDomain: "SEU_PROJETO.firebaseapp.com",
  databaseURL: "SUA_DATABASE_URL",
  projectId: "SEU_PROJECT_ID",
  appId: "SEU_APP_ID",
};
```

Todos esses valores ficam em **Configurações do projeto → Geral → Seus apps → Configuração do SDK** (se ainda não houver um app web, clique em **Adicionar app** e escolha o ícone `</>`).

2. Abra o `login.html` no navegador e entre com a conta criada para o **usuário** (a mesma do `ownerUid`).