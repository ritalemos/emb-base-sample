#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/wifi_mgmt.h>

#define DHCP_TIMEOUT K_SECONDS(30)
#define CONNECTIVITY_TEST_HOSTNAME "google.com"
#define CONNECTIVITY_TEST_PORT "80"

static struct k_sem g_ip_acquired_sem;
static struct net_mgmt_event_callback g_ipv4_event_callback;

bool is_wifi_connected = false;
int app_auto_init(void);

LOG_MODULE_REGISTER(wifi_manager, LOG_LEVEL_INF);

static void on_net_event_ipv4_addr_add(struct net_mgmt_event_callback *cb,
                       uint64_t mgmt_event, struct net_if *iface);
static int request_wifi_connection(const char *ssid, const char *psk);
static int test_dns_connectivity(void);


static void on_net_event_ipv4_addr_add(struct net_mgmt_event_callback *cb,
                       uint64_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        char ip[NET_IPV4_ADDR_LEN];
        const struct net_if_config *cfg = net_if_get_config(iface);

        if (cfg && cfg->ip.ipv4) {
            net_addr_ntop(AF_INET, &cfg->ip.ipv4->unicast[0].ipv4.address.in_addr, ip,
                      sizeof(ip));
            LOG_INF("DHCP OK: %s", ip);
            k_sem_give(&g_ip_acquired_sem);
        }
    }
}

static int request_wifi_connection(const char *ssid, const char *psk)
{
    struct net_if *iface = net_if_get_default();
    struct wifi_connect_req_params p = {0};

    p.ssid = ssid;
    p.ssid_length = strlen(ssid);
    p.psk = psk;
    p.psk_length = strlen(psk);
    p.security = WIFI_SECURITY_TYPE_PSK;
    p.channel = WIFI_CHANNEL_ANY;

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &p, sizeof(p));
    if (ret) {
        LOG_ERR("Falha NET_REQUEST_WIFI_CONNECT (%d)", ret);
        return ret;
    }

    LOG_INF("Conectando ao AP \"%s\" ...", ssid);
    return 0;
}

static int test_dns_connectivity(void)
{
    struct zsock_addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int ret = zsock_getaddrinfo(CONNECTIVITY_TEST_HOSTNAME, CONNECTIVITY_TEST_PORT, &hints, &res);
    if (ret) {
        LOG_ERR("DNS failed (%d)", ret);
        return -1;
    }

    char ipbuf[NET_IPV4_ADDR_LEN];
    struct sockaddr_in *a = (struct sockaddr_in *)res->ai_addr;
    net_addr_ntop(AF_INET, &a->sin_addr, ipbuf, sizeof(ipbuf));
    LOG_INF("DNS OK: %s -> %s", ipbuf, CONNECTIVITY_TEST_HOSTNAME);

    zsock_freeaddrinfo(res);
    return 0;
}

int app_auto_init(void)
{
    LOG_INF("Iniciando gerenciador de Wi-Fi");

    k_sem_init(&g_ip_acquired_sem, 0, 1);
    net_mgmt_init_event_callback(&g_ipv4_event_callback, on_net_event_ipv4_addr_add,
                   NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&g_ipv4_event_callback);

    if (request_wifi_connection(CONFIG_WIFI_SSID, CONFIG_WIFI_PASSWD) != 0) {
        return -EIO;
    }

    if (k_sem_take(&g_ip_acquired_sem, DHCP_TIMEOUT) != 0) {
        LOG_ERR("DHCP Timeout");
        return -ETIMEDOUT;
    }

    if (test_dns_connectivity() != 0) {
        LOG_ERR("Falha no teste de conectividade DNS");
        return -EHOSTUNREACH;
    }

    is_wifi_connected = true;

    return 0;
}