# Sistema de Radar Eletrônico

Um radar de velocidade com classificação automática de veículos, detecção de infrações e captura simulada de placas Mercosul, implementado com **Zephyr RTOS** na plataforma **mps2_an385**.

---

## Visão Geral

Este sistema simula um radar eletrônico de controle de velocidade que combina múltiplos conceitos:

- **Detecção física** através de sensores magnéticos simulados (GPIO)
- **Classificação inteligente** de veículos baseada em contagem de eixos
- **Cálculo de velocidade** por diferença temporal entre dois sensores
- **Interface visual colorida** com feedback instantâneo baseado em status
- **Sistema de captura automatizado** acionado apenas em infrações
- **Validação de placas Mercosul**
- **Arquitetura multi-thread**

---

## Arquitetura do Sistema

### Estrutura Multi-Thread

O sistema é composto por 4 threads principais que trabalham de forma independente e coordenada:

**SENSOR THREAD**
- Monitora interrupções dos sensores GPIO
- Implementa máquina de estados para contagem de eixos
- Calcula velocidade baseado em timestamps
- Classifica tipo de veículo (leve/pesado)
- Envia dados processados via message queue

**CONTROL THREAD**
- Recebe dados de veículos detectados
- Aplica lógica de negócio (limites de velocidade)
- Detecta infrações comparando com limites específicos
- Aciona câmera via ZBUS quando necessário
- Coordena fluxo entre sensores e display

**DISPLAY THREAD**
- Atualiza interface visual no console
- Aplica cores ANSI baseado no status (verde/amarelo/vermelho)

**CAMERA SERVICE THREAD**
- Aguarda trigger via ZBUS
- Simula captura de imagem
- Gera placa no formato Mercosul
- Calcula hash SHA-256 da placa
- Publica resultado via ZBUS

### Comunicação Inter-Thread

**Message Queues (K_MSGQ):**
- `sensor_queue`: ISRs GPIO → Sensor Thread (eventos de sensores)
- `vehicle_queue`: Sensor Thread → Control Thread (dados do veículo)
- `display_queue`: Control Thread → Display Thread (dados para visualização)

**ZBUS (Publish/Subscribe):**
- `chan_camera_evt`: Control Thread ↔ Camera Service (captura e resposta)

**Timers com Callbacks:**
- `vehicle_timeout`: Timeout adaptativo para finalizar detecção
- Callbacks executam em contexto de interrupção
- Enviam eventos para `sensor_queue`

---

## Fluxo de Detecção de Veículos

### Fase 1: Detecção de Eixos

1. Usuário pressiona '1' (simula sensor magnético)
2. GPIO gera interrupção → ISR envia evento para `sensor_queue`
3. Sensor Thread processa no estado IDLE:
   - Conta primeiro eixo
   - Guarda timestamp inicial
   - Muda estado para READING
   - Inicia timer de 800ms
   - Incrementa estatística de detecções

4. Eixos adicionais (pressionar '1' novamente):
   - Incrementa contador de eixos
   - Reinicia timer de 800ms
   - Permanece em estado READING

### Fase 2: Medição de Velocidade

5. Usuário pressiona '2' (simula segundo sensor)
6. Sensor Thread calcula velocidade

### Fase 3: Finalização e Classificação

7. Após 200ms sem novos eixos, timer expira
8. Sensor Thread entra em estado DONE:
   - Valida se velocidade foi calculada
   - Classifica veículo: 2 eixos = LEVE, 3+ eixos = PESADO
   - Envia `vehicle_data` para Control Thread via fila
   - Reseta contexto e volta para estado IDLE

### Fase 4: Processamento e Ação

9. Control Thread recebe dados do veículo
10. Prepara mensagem para Display Thread
11. Verifica infração:
    - Aplica limite específico
    - Calcula threshold de alerta (90% do limite)
    - Determina status: NORMAL, WARNING ou VIOLATION

12. Se VIOLATION detectada:
    - Incrementa contador de infrações
    - Publica requisição no ZBUS
    - Camera Service acorda e processa

13. Display Thread renderiza resultado:
    - Recebe dados via fila
    - Escolhe cor baseado no status
    - Formata e imprime no console com códigos ANSI

### Fase 5: Captura de Placa (apenas infrações)

14. Camera Service simula captura:
    - Gera placa aleatória formato Mercosul
    - Calcula hash SHA-256
    - Publica resposta no ZBUS

15. Control Thread recebe resposta:
    - Valida formato da placa (6 padrões Mercosul)
    - Incrementa contador se inválida
    - Imprime registro de infração formatado

---

## Máquina de Estados do Sensor Thread

**ESTADO: IDLE**
- Aguardando primeira detecção
- Timeout: Aguarda indefinidamente
- Transição: Sensor 0 → READING

**ESTADO: READING**
- Contando eixos do veículo
- Timeout: 800ms entre eixos (antes da velocidade)
- Transição: Sensor 0 → continua em READING (reinicia timer)
- Transição: Sensor 1 → continua em READING (calcula velocidade, timer 200ms)
- Transição: Timeout → DONE

**ESTADO: DONE**
- Processa detecção completa
- Valida dados (velocidade + classificação)
- Envia para Control Thread
- Transição: Automática → IDLE

---

## Como Executar

### Pré-requisitos

- Zephyr RTOS
- Python 3.8+
- West configurado

### Build e Execução

```bash
# Navegue até o workspace do Zephyr
cd ~/zephyrproject/zephyr

# Build para mps2_an385 (ARM Cortex-M3)
west build -b mps2/an385 -p always emb-base-sample

# Executar no emulador QEMU
west build -t run
```

---

## Estrutura de Arquivos

```
emb-base-sample/
│
├── src/
│   ├── main.c                  # Thread principal e orquestração
│   ├── camera_service.c        # Captura simulada via ZBUS
│   └── util/
│       ├── display.c           # Interface visual com cores ANSI
│       ├── radar.c             # Lógica de limites e validação
│       └── mercosul_plate.c    # Validador de placas Mercosul
│
├── include/
│   ├── display.h
│   ├── radar.h
│   ├── mercosul_plate.h
│   └── camera_service.h
│
├── Kconfig                     # Opções de configuração
├── prj.conf                    # Configuração do projeto
├── CMakeLists.txt              # Build
└── README.md                   # Esta documentação
```

---

## Autor

Projeto desenvolvido como parte do curso de Sistemas Embarcados.

**Autores:** Rita de Kassia e Pedro Henrique Sátiro

**Ano:** 2025