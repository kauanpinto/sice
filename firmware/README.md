## Como configurar

> Pré-requisito: ter concluído a [configuração do backend](./backend/README.md).

### Componentes

| Componente | Função |
|---|---|
| **ESP32** | Microcontrolador principal|
| **Relé** | Liga e desliga o equipamento |
| **ACS712-20A** | Sensor de corrente (até 20 A) |
| **PIR** | Sensor de movimento |
| **LDR** | Sensor de luminosidade |

Os pinos usados por cada componente estão definidos no início do `sketch.ino`.

### Configuração

1. No `sketch.ino`, preencha as credenciais do Firebase e da conta do dispositivo:

```cpp
const char* FIREBASE_API_KEY = "SUA_FIREBASE_API_KEY";
const char* FIREBASE_DATABASE_URL = "https://SEU-PROJETO-default-rtdb.firebaseio.com";

const char* DEVICE_EMAIL = "EMAIL_DA_CONTA_DO_DISPOSITIVO";
const char* DEVICE_PASSWORD = "SENHA_DA_CONTA_DO_DISPOSITIVO";
```

- `FIREBASE_API_KEY`: em **Configurações do projeto → Geral → Chave de API da Web**.
- `FIREBASE_DATABASE_URL`: no topo da aba **Realtime Database → Dados**.
- `DEVICE_EMAIL` e `DEVICE_PASSWORD`: e-mail e senha da conta criada para o ESP32.

2. Adicione os arquivos do **ACS712-20A** dentro da pasta `chips/`, pois o `sketch.ino` depende dele.