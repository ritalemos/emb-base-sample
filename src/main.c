#include <stdint.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/sntp.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socket_service.h>
#include <zephyr/sys/clock.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/toolchain.h>
#include <zephyr/zbus/zbus.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define SNTP_PORT_STR "123"
#define SNTP_RESPONSE_TIMEOUT K_MSEC(1000)
#define APP_START_DELAY K_SECONDS(2)
#define TIME_STR_BUFFER_SIZE 32
#define TIME_FORMAT_STR_SIZE 64

#define SNTP_REQUEST_INTERVAL K_SECONDS(10)

#define SNTP_THREAD_PRIO 4
#define LOGGER_THREAD_PRIO 3
#define MONITOR_THREAD_PRIO 3

extern int app_auto_init(void);
extern bool is_wifi_connected;

struct time_update_msg {
    struct tm timestamp;
};

struct server_endpoint {
    struct sockaddr addr;
    socklen_t len;
};

ZBUS_CHAN_DEFINE(time_update_channel, struct time_update_msg, NULL, NULL,
         ZBUS_OBSERVERS(logging_subscriber, interval_monitor_subscriber),
         ZBUS_MSG_INIT(.timestamp = 0));

ZBUS_SUBSCRIBER_DEFINE(interval_monitor_subscriber, 4);
ZBUS_SUBSCRIBER_DEFINE(logging_subscriber, 4);

K_THREAD_STACK_DEFINE(sntp_task_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(logging_task_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(interval_monitor_stack, STACK_SIZE);

static struct k_thread sntp_task_data;
static struct k_thread logging_task_data;
static struct k_thread interval_monitor_data;

static struct server_endpoint g_sntp_server_ep;
static struct sntp_time g_last_sntp_result;
static K_SEM_DEFINE(g_sntp_response_received_sem, 0, 1);

static void on_sntp_socket_event(struct net_socket_service_event *pev);
static int setup_sntp_server(void);
static void start_application_threads(void);
static void handle_sntp_time_update(const struct sntp_time *sntp_result,
                    struct tm *local_tm_out);

void time_logging_task(void *arg1, void *arg2, void *arg3);
void update_interval_monitor_task(void *arg1, void *arg2, void *arg3);
void sntp_client_task(void *arg1, void *arg2, void *arg3);

NET_SOCKET_SERVICE_SYNC_DEFINE_STATIC(sntp_socket_service, on_sntp_socket_event, 1);

SYS_INIT(app_auto_init, APPLICATION, 50);

int main(void)
{
    if (!is_wifi_connected) {
        LOG_INF("Incapaz de conectar ao WIFI. Abortando.");
        return -1;
    }

    LOG_INF("Iniciando sistema... Aguardando estabilização da rede.");
    k_sleep(APP_START_DELAY);

    if (setup_sntp_server() != 0) {
        LOG_ERR("Falha ao configurar o servidor SNTP.");
        return -1;
    }

    start_application_threads();

    LOG_INF("Sistema iniciado com sucesso");
    return 0;
}

static int setup_sntp_server(void)
{
    int status;
    char ip_str_buffer[NET_IPV4_ADDR_LEN];

    struct zsock_addrinfo addr_hints;
    struct zsock_addrinfo *addr_result;

    memset(&addr_hints, 0, sizeof(addr_hints));
    addr_hints.ai_family = AF_INET;
    addr_hints.ai_socktype = SOCK_DGRAM;

    status = zsock_getaddrinfo(CONFIG_SNTP_HOSTNAME, SNTP_PORT_STR, &addr_hints, &addr_result);
    if (status) {
        LOG_ERR("Falha ao obter informações do hostname (erro %d)", status);
        return -1;
    }

    net_addr_ntop(AF_INET, addr_result->ai_addr, ip_str_buffer, sizeof(ip_str_buffer));
    LOG_INF("DNS SNTP OK: %s -> %s", ip_str_buffer, CONFIG_SNTP_HOSTNAME);

    g_sntp_server_ep.len = addr_result->ai_addrlen;
    memcpy(&g_sntp_server_ep.addr, addr_result->ai_addr, addr_result->ai_addrlen);

    zsock_freeaddrinfo(addr_result);
    return 0;
}

static void start_application_threads(void)
{
    k_thread_create(&sntp_task_data, sntp_task_stack,
            K_THREAD_STACK_SIZEOF(sntp_task_stack), sntp_client_task,
            &g_sntp_server_ep, NULL, NULL, K_PRIO_PREEMPT(SNTP_THREAD_PRIO), 0,
            K_NO_WAIT);

    k_thread_create(&logging_task_data, logging_task_stack,
            K_THREAD_STACK_SIZEOF(logging_task_stack), time_logging_task, NULL,
            NULL, NULL, K_PRIO_PREEMPT(LOGGER_THREAD_PRIO), 0, K_NO_WAIT);

    k_thread_create(&interval_monitor_data, interval_monitor_stack,
            K_THREAD_STACK_SIZEOF(interval_monitor_stack),
            update_interval_monitor_task, NULL, NULL, NULL,
            K_PRIO_PREEMPT(MONITOR_THREAD_PRIO), 0, K_NO_WAIT);
}

static void on_sntp_socket_event(struct net_socket_service_event *pev)
{
    int ret = sntp_read_async(pev, &g_last_sntp_result);
    if (ret) {
        LOG_ERR("[SNTP_CB] Erro ao ler resposta SNTP (%d)", ret);
        return;
    }
    k_sem_give(&g_sntp_response_received_sem);
}

static void handle_sntp_time_update(const struct sntp_time *sntp_result,
                    struct tm *local_tm_out)
{
    const struct timespec ts = {
        .tv_sec = sntp_result->seconds,
        .tv_nsec = (long)((((uint64_t)sntp_result->fraction) * 1000000000ULL) >> 32)};

    sys_clock_settime(CLOCK_REALTIME, &ts);

    uint64_t local_time_seconds = sntp_result->seconds + (CONFIG_LOCAL_TIME * 3600);
    gmtime_r(&local_time_seconds, local_tm_out);

    char time_format_str[TIME_FORMAT_STR_SIZE];
    
    snprintf(time_format_str, sizeof(time_format_str), "%%a %%Y-%%b-%%d %%H:%%M:%%S %%Z%+d",
         CONFIG_LOCAL_TIME);

    char time_buffer[TIME_STR_BUFFER_SIZE];
    strftime(time_buffer, sizeof(time_buffer) - 1, time_format_str, local_tm_out);

    LOG_INF("[SNTP] Hora local atualizada: %s", time_buffer);
}

void time_logging_task(void *arg1, void *arg2, void *arg3)
{
    char time_buffer[TIME_STR_BUFFER_SIZE] = {0};
    const struct zbus_channel *channel;
    struct time_update_msg received_msg;

    LOG_INF("[LOGGER] Iniciando serviço de log de tempo");

    while (true) {
        if (zbus_sub_wait(&logging_subscriber, &channel, K_FOREVER)) {
            LOG_WRN("[LOGGER] Erro ao esperar notificação do canal");
            continue;
        }

        if (zbus_chan_read(channel, &received_msg, K_MSEC(100))) {
            LOG_WRN("[LOGGER] Erro ao ler mensagem do canal");
            continue;
        }

        strftime(time_buffer, sizeof(time_buffer) - 1, "%a %Y-%b-%d %H:%M:%S %Z",
             &received_msg.timestamp);
        LOG_INF("[LOGGER] Relógio interno atualizado: %s", time_buffer);
    }
}

void update_interval_monitor_task(void *arg1, void *arg2, void *arg3)
{
    char time_buffer[TIME_STR_BUFFER_SIZE] = {0};
    const struct zbus_channel *channel;
    static struct tm previous_timestamp = {0};
    struct time_update_msg current_msg;

    LOG_INF("[MONITOR] Iniciando serviço de monitoramento de intervalo");

    while (true) {
        if (zbus_sub_wait(&interval_monitor_subscriber, &channel, K_FOREVER)) {
            LOG_WRN("[MONITOR] Erro ao esperar notificação do canal");
            continue;
        }

        if (zbus_chan_read(channel, &current_msg, K_MSEC(100))) {
            LOG_WRN("[MONITOR] Erro ao ler mensagem do canal");
            continue;
        }

        strftime(time_buffer, sizeof(time_buffer) - 1, "%a %Y-%b-%d %H:%M:%S %Z",
             &previous_timestamp);
        LOG_DBG("[MONITOR] Hora da última execução: %s", time_buffer);
        strftime(time_buffer, sizeof(time_buffer) - 1, "%a %Y-%b-%d %H:%M:%S %Z",
             &current_msg.timestamp);
        LOG_DBG("[MONITOR] Hora da execução atual: %s", time_buffer);

        int64_t t_previous = timeutil_timegm64(&previous_timestamp);
        int64_t t_current = timeutil_timegm64(&current_msg.timestamp);

        if (t_previous == 0) {
            previous_timestamp = current_msg.timestamp;
            continue;
        }

        int64_t delta_seconds = t_current - t_previous;
        LOG_INF("[MONITOR] Intervalo de execução: %" PRId64 "s", delta_seconds);

        previous_timestamp = current_msg.timestamp;
    }
}

void sntp_client_task(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    LOG_INF("Iniciando Serviço SNTP");

    struct server_endpoint *server_conf = (struct server_endpoint *)arg1;
    struct sntp_ctx sntp_context;
    int status;

    status = sntp_init_async(&sntp_context, &server_conf->addr, server_conf->len,
                   &sntp_socket_service);
    if (status) {
        LOG_ERR("Falha ao iniciar SNTP, ctx: %d", status);
        sntp_close(&sntp_context);
        return;
    }

    while (true) {
        struct tm local_tm_result;

        k_sem_reset(&g_sntp_response_received_sem);
        status = sntp_send_async(&sntp_context);
        if (status) {
            LOG_WRN("[SNTP] Falha ao enviar consulta SNTP (%d)", status);
            goto sleep;
        }

        status = k_sem_take(&g_sntp_response_received_sem, SNTP_RESPONSE_TIMEOUT);
        if (status) {
            LOG_WRN("[SNTP] Timeout na resposta SNTP (%d)", status);
            goto sleep;
        }

        handle_sntp_time_update(&g_last_sntp_result, &local_tm_result);

        struct time_update_msg msg = {.timestamp = local_tm_result};
        status = zbus_chan_pub(&time_update_channel, &msg, K_NO_WAIT);
        if (status) {
            LOG_WRN("[SNTP] Falha ao publicar no canal zbus");
        }

sleep:
        k_sleep(SNTP_REQUEST_INTERVAL);
    }

    sntp_close_async(&sntp_socket_service);
    sntp_close(&sntp_context);
}