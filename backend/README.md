## Como configurar

1. Crie um projeto no [Firebase](https://console.firebase.google.com).

2. Crie um **Realtime Database** para o projeto.

3. Em **Realtime Database → Regras**, substitua o conteúdo pelo de `database.rules.json` e clique em **Publicar**.

4. Em **Authentication → Sign-in method**, habilite o provedor **E-mail/Senha**.

5. Em **Authentication → Users**, crie 2 contas e copie o **UID** de cada uma:

- uma para o ESP32
- uma para o usuário

6. Abra `database.json` e preencha o bloco `config` com os UIDs copiados:

```json
"config": {
  "deviceUid": "UID_DA_CONTA_DO_ESP32",
  "ownerUid": "UID_DA_CONTA_DO_USUARIO"
}
```

7. Em **Realtime Database → Dados**, clique em ⋮ → **Importar JSON** e selecione o `database.json`.
