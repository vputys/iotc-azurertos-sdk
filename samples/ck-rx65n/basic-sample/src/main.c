/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/

#include <stdbool.h>
#include "nx_api.h"
#undef SAMPLE_DHCP_DISABLE
#ifndef SAMPLE_DHCP_DISABLE
#include "nxd_dhcp_client.h"
#endif /* SAMPLE_DHCP_DISABLE */
#include "nxd_dns.h"
#include "nxd_sntp_client.h"
#include "nx_secure_tls_api.h"
#include "azrtos_time.h"
#include "nx_driver_rx_fit.h"
#include "r_tsip_rx_if.h"
#include "secure_boot.h"

#include "demo_printf.h"
#include "demo_scanf.h"
#include "app_config.h"

#include "hardware_setup.h"

extern bool app_startup_interactive(NX_IP *ip_ptr, NX_PACKET_POOL *pool_ptr, NX_DNS *dns_ptr);

/* Define the helper thread for running Azure SDK on ThreadX (THREADX IoT Platform).  */
#ifndef SAMPLE_HELPER_STACK_SIZE
#define SAMPLE_HELPER_STACK_SIZE      (10 * 1024)
#endif /* SAMPLE_HELPER_STACK_SIZE  */

#ifndef SAMPLE_HELPER_THREAD_PRIORITY
#define SAMPLE_HELPER_THREAD_PRIORITY (4)
#endif /* SAMPLE_HELPER_THREAD_PRIORITY  */

/* Define user configurable symbols. */
#ifndef SAMPLE_IP_STACK_SIZE
#define SAMPLE_IP_STACK_SIZE          (2048)
#endif /* SAMPLE_IP_STACK_SIZE  */

#ifndef SAMPLE_PACKET_COUNT
#define SAMPLE_PACKET_COUNT           (60)
#endif /* SAMPLE_PACKET_COUNT  */

#ifndef SAMPLE_PACKET_SIZE
#define SAMPLE_PACKET_SIZE            (1536)
#endif /* SAMPLE_PACKET_SIZE  */

#define SAMPLE_POOL_SIZE              ((SAMPLE_PACKET_SIZE + sizeof(NX_PACKET)) * SAMPLE_PACKET_COUNT)

#ifndef SAMPLE_ARP_CACHE_SIZE
#define SAMPLE_ARP_CACHE_SIZE         (512)
#endif /* SAMPLE_ARP_CACHE_SIZE  */

#ifndef SAMPLE_IP_THREAD_PRIORITY
#define SAMPLE_IP_THREAD_PRIORITY     (1)
#endif /* SAMPLE_IP_THREAD_PRIORITY */

#if __STDC_VERSION__ >= 201112UL

#endif

#if defined(SAMPLE_DHCP_DISABLE) && !defined(SAMPLE_NETWORK_CONFIGURE)
#ifndef SAMPLE_IPV4_ADDRESS
/*#define SAMPLE_IPV4_ADDRESS           IP_ADDRESS(192, 168, 100, 33)*/
#error "SYMBOL SAMPLE_IPV4_ADDRESS must be defined. This symbol specifies the IP address of device. "
#endif /* SAMPLE_IPV4_ADDRESS */

#ifndef SAMPLE_IPV4_MASK
/*#define SAMPLE_IPV4_MASK              0xFFFFFF00UL*/
#error "SYMBOL SAMPLE_IPV4_MASK must be defined. This symbol specifies the IP address mask of device. "
#endif /* SAMPLE_IPV4_MASK */

#ifndef SAMPLE_GATEWAY_ADDRESS
/*#define SAMPLE_GATEWAY_ADDRESS        IP_ADDRESS(192, 168, 100, 1)*/
#error "SYMBOL SAMPLE_GATEWAY_ADDRESS must be defined. This symbol specifies the gateway address for routing. "
#endif /* SAMPLE_GATEWAY_ADDRESS */

#ifndef SAMPLE_DNS_SERVER_ADDRESS
/*#define SAMPLE_DNS_SERVER_ADDRESS     IP_ADDRESS(192, 168, 100, 1)*/
#error "SYMBOL SAMPLE_DNS_SERVER_ADDRESS must be defined. This symbol specifies the dns server address for routing. "
#endif /* SAMPLE_DNS_SERVER_ADDRESS */
#else
#define SAMPLE_IPV4_ADDRESS             IP_ADDRESS(0, 0, 0, 0)
#define SAMPLE_IPV4_MASK                IP_ADDRESS(0, 0, 0, 0)

#define SAMPLE_DNS_SERVER_ADDRESS       IP_ADDRESS(8, 8, 8, 8)

#ifndef SAMPLE_DHCP_WAIT_OPTION
#define SAMPLE_DHCP_WAIT_OPTION         (20 * NX_IP_PERIODIC_RATE)
#endif /* SAMPLE_DHCP_WAIT_OPTION */

#endif /* SAMPLE_DHCP_DISABLE */

/* Using SNTP to get unix time.  */
/* Define the address of SNTP Server. If not defined, use DNS module to resolve the host name SAMPLE_SNTP_SERVER_NAME.  */
/*
#define SAMPLE_SNTP_SERVER_ADDRESS     IP_ADDRESS(118, 190, 21, 209)
*/

#ifndef SAMPLE_SNTP_SERVER_NAME
#define SAMPLE_SNTP_SERVER_NAME           "0.pool.ntp.org"    /* SNTP Server.  */
#endif /* SAMPLE_SNTP_SERVER_NAME */

#ifndef SAMPLE_SNTP_SYNC_MAX
#define SAMPLE_SNTP_SYNC_MAX              5
#endif /* SAMPLE_SNTP_SYNC_MAX */

#ifndef SAMPLE_SNTP_UPDATE_MAX
#define SAMPLE_SNTP_UPDATE_MAX            10
#endif /* SAMPLE_SNTP_UPDATE_MAX */

#ifndef SAMPLE_SNTP_UPDATE_INTERVAL
#define SAMPLE_SNTP_UPDATE_INTERVAL       (NX_IP_PERIODIC_RATE / 2)
#endif /* SAMPLE_SNTP_UPDATE_INTERVAL */

/* Seconds between Unix Epoch (1/1/1970) and NTP Epoch (1/1/1999) */
#define SAMPLE_UNIX_TO_NTP_EPOCH_SECOND   0x83AA7E80

static TX_THREAD        sample_helper_thread;
static NX_PACKET_POOL   pool_0;
static NX_IP            ip_0;
static NX_DNS           dns_0;
#ifndef SAMPLE_DHCP_DISABLE
static NX_DHCP          dhcp_0;
#endif /* SAMPLE_DHCP_DISABLE  */

/* Define the stack/cache for ThreadX.  */
static ULONG sample_ip_stack[SAMPLE_IP_STACK_SIZE / sizeof(ULONG)];
#ifndef SAMPLE_POOL_STACK_USER
static ULONG sample_pool_stack[SAMPLE_POOL_SIZE / sizeof(ULONG)];
static ULONG sample_pool_stack_size = sizeof(sample_pool_stack);
#else
extern ULONG sample_pool_stack[];
extern ULONG sample_pool_stack_size;
#endif
static ULONG sample_arp_cache_area[SAMPLE_ARP_CACHE_SIZE / sizeof(ULONG)];
static ULONG sample_helper_thread_stack[SAMPLE_HELPER_STACK_SIZE / sizeof(ULONG)];

/* Define the prototypes for sample thread.  */
static void sample_helper_thread_entry(ULONG parameter);

#ifndef SAMPLE_DHCP_DISABLE
static void dhcp_wait();
#endif /* SAMPLE_DHCP_DISABLE */

VOID sample_network_configure(NX_IP *ip_ptr, ULONG *dns_server_address);

void thread_0_entry(ULONG thread_input);

static UINT dns_create();

#ifdef SAMPLE_BOARD_SETUP
void SAMPLE_BOARD_SETUP();
#endif /* SAMPLE_BOARD_SETUP */

/* Define main entry point.  */
int main(void)
{
#ifdef SAMPLE_BOARD_SETUP
    SAMPLE_BOARD_SETUP();
#endif /* SAMPLE_BOARD_SETUP */
    /* Enter the ThreadX kernel.  */
    tx_kernel_enter();
}

/* Define what the initial system looks like.  */
void    tx_application_define(void *first_unused_memory)
{

    UINT  status;

    /* Initialize the demo printf implementation. */
    demo_printf_init();
    /* Initialize the demo scanf implementation. */
    demo_scanf_init();
    /* Initialize the NetX system.  */
    nx_system_initialize();

    /* Create a packet pool.  */
    status = nx_packet_pool_create(&pool_0, "NetX Main Packet Pool", SAMPLE_PACKET_SIZE,
                                   (UCHAR *)sample_pool_stack , sample_pool_stack_size);

    /* Check for pool creation error.  */
    if (status)
    {
        printf("nx_packet_pool_create fail: 0x%x\r\n", status);
        return;
    }

    /* Create an IP instance.  */
    status = nx_ip_create(&ip_0, "NetX IP Instance 0",
                          SAMPLE_IPV4_ADDRESS, SAMPLE_IPV4_MASK,
                          &pool_0, nx_driver_rx_fit,
                          (UCHAR*)sample_ip_stack, sizeof(sample_ip_stack),
                          SAMPLE_IP_THREAD_PRIORITY);

    /* Check for IP create errors.  */
    if (status)
    {
        printf("nx_ip_create fail: 0x%x\r\n", status);
        return;
    }

    /* Enable ARP and supply ARP cache memory for IP Instance 0.  */
    status = nx_arp_enable(&ip_0, (VOID *)sample_arp_cache_area, sizeof(sample_arp_cache_area));

    /* Check for ARP enable errors.  */
    if (status)
    {
        printf("nx_arp_enable fail: 0x%x\r\n", status);
        return;
    }

    /* Enable ICMP traffic.  */
    status = nx_icmp_enable(&ip_0);

    /* Check for ICMP enable errors.  */
    if (status)
    {
        printf("nx_icmp_enable fail: 0x%x\r\n", status);
        return;
    }

    /* Enable TCP traffic.  */
    status = nx_tcp_enable(&ip_0);

    /* Check for TCP enable errors.  */
    if (status)
    {
        printf("nx_tcp_enable fail: 0x%x\r\n", status);
        return;
    }

    /* Enable UDP traffic.  */
    status = nx_udp_enable(&ip_0);

    /* Check for UDP enable errors.  */
    if (status)
    {
        printf("nx_udp_enable fail: 0x%x\r\n", status);
        return;
    }

    /* Initialize TLS.  */
    nx_secure_tls_initialize();

    /* Create sample helper thread. */
    status = tx_thread_create(&sample_helper_thread, "Demo Thread",
                              sample_helper_thread_entry, 0,
                              sample_helper_thread_stack, SAMPLE_HELPER_STACK_SIZE,
                              SAMPLE_HELPER_THREAD_PRIORITY, SAMPLE_HELPER_THREAD_PRIORITY,
                              TX_NO_TIME_SLICE, TX_AUTO_START);

    /* Check status.  */
    if (status)
    {
        printf("Demo helper thread creation fail: 0x%x\r\n", status);
        return;
    }
}

/* Define sample helper thread entry.  */
void sample_helper_thread_entry(ULONG parameter)
{
UINT    status;
ULONG   ip_address = 0;
ULONG   network_mask = 0;
ULONG   gateway_address = 0;

	printf("sntp server: %s\r\n", SAMPLE_SNTP_SERVER_NAME);

    /* Initialize Flash - dataflash used by TSIP */
    flash_err_t flash_error_code = FLASH_SUCCESS;
    flash_error_code = R_FLASH_Open();
    if (FLASH_SUCCESS != flash_error_code)
    {
        printf("Failed to initialise flash: %u\r\n", flash_error_code);
        return;
    }

    /* Initialise the TSIP, load keyindexes from dataflash */
    int32_t result_secure_boot = R_SECURE_BOOT_SUCCESS;
    tsip_keyring_restore();
    e_tsip_err_t tsip_status = TSIP_ERR_FAIL;
    if ((tsip_status = R_TSIP_Open(NULL, NULL)) != TSIP_SUCCESS)
    {
        printf("Failed to initialise TSIP: %u\r\n", tsip_status);
        return;
    }
    /* If keyindexes haven't been generated for all of the encrypted keys in
     *  flash then generate them and store them in the dataflash for future use.
     * Keyindex generation is a one-time operation for a device (unless the
     *  dataflash section is erased and re-populated with the encrypted keys or
     *  the UpdateKey family of APIs are used in combination with the UpdateKeyring
     *  KeyIndex)
     */
    result_secure_boot = secure_boot();
    if (R_SECURE_BOOT_FAIL == result_secure_boot)
    {
        printf("secure boot sequence: fail.\r\n");
        R_BSP_SoftwareDelay(1000, BSP_DELAY_MILLISECS);
        while (1)
        {
            R_BSP_NOP(); /* infinite loop */
        }
    }
    /* Print all of the installed key indexes (which are also saved in dataflash) */
    //tsip_print_installed_key_index();

#ifndef SAMPLE_DHCP_DISABLE
    dhcp_wait();
#else
    nx_ip_gateway_address_set(&ip_0, SAMPLE_GATEWAY_ADDRESS);
#endif /* SAMPLE_DHCP_DISABLE  */

    /* Get IP address and gateway address. */
    nx_ip_address_get(&ip_0, &ip_address, &network_mask);
    nx_ip_gateway_address_get(&ip_0, &gateway_address);

    /* Output IP address and gateway address. */
    printf("IP address: %lu.%lu.%lu.%lu\r\n",
           (ip_address >> 24),
           (ip_address >> 16 & 0xFF),
           (ip_address >> 8 & 0xFF),
           (ip_address & 0xFF));
    printf("Mask: %lu.%lu.%lu.%lu\r\n",
           (network_mask >> 24),
           (network_mask >> 16 & 0xFF),
           (network_mask >> 8 & 0xFF),
           (network_mask & 0xFF));
    printf("Gateway: %lu.%lu.%lu.%lu\r\n",
           (gateway_address >> 24),
           (gateway_address >> 16 & 0xFF),
           (gateway_address >> 8 & 0xFF),
           (gateway_address & 0xFF));

    /* Create DNS.  */
    status = dns_create();

    /* Check for DNS create errors.  */
    if (status)
    {
        printf("dns_create fail: 0x%x\r\n", status);
        printf("Rebooting...\r\n");
        software_reset();
    }

    /* Sync up time by SNTP at start up.  */
    for (UINT i = 0; i < SAMPLE_SNTP_SYNC_MAX; i++)
    {

        /* Start SNTP to sync the local time.  */
        status = sntp_time_sync(&ip_0, &pool_0, &dns_0, SAMPLE_SNTP_SERVER_NAME);

        /* Check status.  */
        if(status == NX_SUCCESS)
            break;
    }

    /* Check status.  */
    if (status)
    {
        printf("SNTP Time Sync failed. Rebooting...\r\n");
        software_reset();
    }
    else
    {
        printf("SNTP Time Sync successfully.\r\n");
    }

    /* Start sample.  */
    bool app_status = app_startup_interactive(&ip_0, &pool_0, &dns_0);
    printf("App exited with status %s.\r\n", app_status ? "true" : "false");
    if (app_status){
    	printf("Status true\r\n");
		while (true) {
			tx_thread_sleep(10 * NX_IP_PERIODIC_RATE);
		}
    } else {
    	printf("App startup failed. Rebooting...\r\n");
    	software_reset();
    }
}

#ifndef SAMPLE_DHCP_DISABLE
static void dhcp_error()
{
    printf("...DHCP Error\r\n");

    while(1) {
        tx_thread_sleep((ULONG) -1);
    }

    /* no return */
}

static void dhcp_wait()
{
    ULONG   actual_status = 0;
    UINT status;

    printf("DHCP In Progress...\r\n");

    /* Create the DHCP instance.  */
    status = nx_dhcp_create(&dhcp_0, &ip_0, "DHCP Client");
    if(status != NX_SUCCESS) {
        printf("nx_dhcp_create failed: 0x%x\r\n", status);
        dhcp_error();
    }
    printf("nx_dhcp_create ok\r\n");

    /* Start the DHCP Client.  */
    status = nx_dhcp_start(&dhcp_0);
    if(status != NX_SUCCESS) {
        printf("nx_dhcp_start failed: 0x%x\r\n", status);
        nx_dhcp_delete(&dhcp_0);
        dhcp_error();
    }
    printf("nx_dhcp_start ok\r\n");

    /* Wait until address is resolved, or times out. */
    status = nx_ip_status_check(&ip_0, NX_IP_ADDRESS_RESOLVED, &actual_status, 1000);
    for(int i = 0; i < 10 && status != NX_SUCCESS && actual_status != NX_IP_ADDRESS_RESOLVED;i++)
    {
    	printf("nx_ip_status_check failed: 0x%x 0x%x\r\n", status, actual_status);
        tx_thread_sleep(NX_IP_PERIODIC_RATE * 10);
	
        printf("retry nx_ip_status_check %d\r\n", i);
        actual_status = 0;
        status = nx_ip_status_check(&ip_0, NX_IP_ADDRESS_RESOLVED, &actual_status, 1000);
    }
    if(status != NX_SUCCESS)
    {
        printf("nx_ip_status_check failed: 0x%x - 0x%x\r\n", status, actual_status);
        nx_dhcp_delete(&dhcp_0);
        return;
    }

    printf("...DHCP Finished\r\n");
}
#endif /* SAMPLE_DHCP_DISABLE  */

static UINT dns_create()
{

    UINT    status;
    ULONG   dns_server_address[3]; // IPv6 needs more than one ULONG
    UINT    dns_server_address_size = sizeof(dns_server_address);

    /* Create a DNS instance for the Client.  Note this function will create
       the DNS Client packet pool for creating DNS message packets intended
       for querying its DNS server. */
    status = nx_dns_create(&dns_0, &ip_0, (UCHAR *)"DNS Client");
    if (status != NX_SUCCESS)
    {
        printf("nx_dns_create failed: status = 0x%x\r\n", status);
        return(status);
    }

    /* Is the DNS client configured for the host application to create the pecket pool? */
#ifdef NX_DNS_CLIENT_USER_CREATE_PACKET_POOL

    /* Yes, use the packet pool created above which has appropriate payload size
       for DNS messages. */
    status = nx_dns_packet_pool_set(&dns_0, ip_0.nx_ip_default_packet_pool);
    if (status != NX_SUCCESS)
    {
        printf("nx_dns_packet_pool_set failed: status = 0x%x\r\n", status);
        nx_dns_delete(&dns_0);
        return(status);
    }
#endif /* NX_DNS_CLIENT_USER_CREATE_PACKET_POOL */

#ifndef SAMPLE_DHCP_DISABLE
    /* Retrieve DNS server address.  */
    status = nx_dhcp_interface_user_option_retrieve(&dhcp_0, 0, NX_DHCP_OPTION_DNS_SVR, (UCHAR *)(dns_server_address),
                                           &dns_server_address_size);
    if (status != NX_SUCCESS)
    {
        printf("nx_dhcp_interface_user_option_retrieve failed: status = 0x%x\r\n", status);
        nx_dns_delete(&dns_0);
        return(status);
    }
#else
    dns_server_address[0] = SAMPLE_DNS_SERVER_ADDRESS;
#endif /* SAMPLE_DHCP_DISABLE */

    /* Add an IPv4 server address to the Client list. */
    status = nx_dns_server_add(&dns_0, dns_server_address[0]);
    if (status)
    {
        printf("nx_dns_server_add failed: status = 0x%x\r\n", status);
        nx_dns_delete(&dns_0);
        return(status);
    }

    /* Output DNS Server address.  */
    printf("DNS Server address: %lu.%lu.%lu.%lu\r\n",
           (dns_server_address[0] >> 24),
           (dns_server_address[0] >> 16 & 0xFF),
           (dns_server_address[0] >> 8 & 0xFF),
           (dns_server_address[0] & 0xFF));

    return(NX_SUCCESS);
}
