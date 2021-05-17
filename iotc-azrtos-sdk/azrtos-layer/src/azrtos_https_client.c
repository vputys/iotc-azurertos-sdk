//
// Copyright: Avnet 2021
// Created by Shu Liu <shu.liu@avnet.com> on 4/19/21.
// Modified by Nik Markovic <nikola.markovic@avnet.com> on 4/19/21.
//

#include "nx_secure_tls.h"
#include "nx_secure_tls_api.h"
#include "nx_web_http_client.h"
#include "iotconnect_certs.h"
#include "iotconnect.h"
#include "azrtos_https_client.h"

#ifndef IOTCONNECT_HTTP_RECEIVE_BUFFER_SIZE
#define IOTCONNECT_HTTP_RECEIVE_BUFFER_SIZE 12000
#endif

#ifndef NX_WEB_HTTP_TCP_WINDOW_SIZE
#define NX_WEB_HTTP_TCP_WINDOW_SIZE     1536
#endif

#ifndef IOTCONNECT_HTTPS_TLS_BUFFERSIZE
#define IOTCONNECT_HTTPS_TLS_BUFFERSIZE  3000
#endif

#define HDR_CT_NAME "Content-Type"
#define HDR_CT_VALUE "application/json" // for content type


#define NX_WEB_HTTP_SERVER_SESSION_MAX  2
static UCHAR tls_packet_buffer[16500];
static CHAR crypto_client_metadata[8928 * NX_WEB_HTTP_SERVER_SESSION_MAX];

//static UCHAR tls_packet_buffer[IOTCONNECT_HTTPS_TLS_BUFFERSIZE];
//static CHAR crypto_client_metadata[IOTCONNECT_HTTPS_TLS_BUFFERSIZE];
static NX_SECURE_X509_CERT trusted_certificate;
static NX_SECURE_X509_CERT remote_certificate, remote_issuer;
static UCHAR remote_cert_buffer[2000];
static UCHAR remote_issuer_buffer[2000];
extern const NX_SECURE_TLS_CRYPTO nx_crypto_tls_ciphers;

static UINT tls_setup_callback(NX_WEB_HTTP_CLIENT *client_ptr, NX_SECURE_TLS_SESSION *tls_session);

UINT iotconnect_https_request(IotConnectAzrtosConfig *azrtos_config,
        IotConnectHttpResponse *r,
        char *host_name,
        char *resource,
        char *post_data
        ) {
    UINT status;
    NX_WEB_HTTP_CLIENT http_client;
    NXD_ADDRESS server_ip_address;

    if (!azrtos_config || !azrtos_config || !host_name || !resource) {
        printf("HTTP: Invalid arguments");
        return NX_INVALID_PARAMETERS;
    }

    r->response = malloc(IOTCONNECT_HTTP_RECEIVE_BUFFER_SIZE);
    if (!r->response) {
        printf("HTTP: Failed to allocate response buffer!\r\n");
        return NX_POOL_ERROR;
    }
    r->response[0] = 0; // null terminate

    status = nx_web_http_client_create(&http_client, "HTTP Client", azrtos_config->ip_ptr, azrtos_config->pool_ptr,
            NX_WEB_HTTP_TCP_WINDOW_SIZE);
    if (status) {
        printf("HTTP: Client create failed: 0x%x\r\n", status);
        return status;
    }

    status = nx_dns_host_by_name_get(azrtos_config->dns_ptr, (UCHAR*) host_name, &server_ip_address.nxd_ip_address.v4, 5 * NX_IP_PERIODIC_RATE); // give it 5 seconds to resolve
    if (status) {
        printf("HTTP: Host DNS resolution failed 0x%x\r\n", status);
        nx_web_http_client_delete(&http_client);
        return status;
    }

    // Set the header callback routine.
    // nx_web_http_client_response_header_callback_set(http_client_ptr, http_header_response_callback);


    server_ip_address.nxd_ip_version = NX_IP_VERSION_V4;
    status = nx_web_http_client_secure_connect(
            &http_client, //
            &server_ip_address,//
            NX_WEB_HTTPS_SERVER_PORT,//
            tls_setup_callback,//
            NX_WAIT_FOREVER);
    if (status) {
        printf("HTTP: Error in HTTP Connect: 0x%x\r\n", status);
        nx_web_http_client_delete(&http_client);
        return status;
    }

    if (post_data) {
        status = nx_web_http_client_request_initialize(&http_client,
                NX_WEB_HTTP_METHOD_POST,
                resource, host_name,
                strlen(post_data),          //  POST input size needed here
                NX_FALSE,
                NULL,
                NULL,
                NX_WAIT_FOREVER);


        if (status != NX_SUCCESS) {
            printf("HTTP: Error in HTTP PUT request initialization: 0x%x\r\n", status);
            nx_web_http_client_delete(&http_client);
            return status;
        }

        status = nx_web_http_client_request_header_add(&http_client,
                HDR_CT_NAME, strlen(HDR_CT_NAME),
                HDR_CT_VALUE, strlen(HDR_CT_VALUE),
                NX_WAIT_FOREVER);

        if (status != NX_SUCCESS) {
            printf("HTTP: Error in HTTP request headers setup: 0x%x\r\n", status);
            nx_web_http_client_delete(&http_client);
            return status;
        }
    } else {
        status = nx_web_http_client_request_initialize(&http_client,
                NX_WEB_HTTP_METHOD_GET, /* GET, PUT, DELETE, POST, HEAD */
                resource, host_name, 0, /* PUT and POST need an input size. */
                NX_FALSE, /* If true, input_size is ignored. */
                NULL,
                NULL,
                NX_WAIT_FOREVER);
        if (status != NX_SUCCESS) {
            printf("HTTP: Error in HTTP GET request initialization: 0x%x\r\n", status);
            nx_web_http_client_delete(&http_client);
            return status;
        }
    }

    // common for both GET and POST
    status = nx_web_http_client_request_send(&http_client, NX_WAIT_FOREVER);
    if (status) {
        printf("HTTP: Error in HTTP request send: 0x%x\r\n", status);
        nx_web_http_client_delete(&http_client);
        return status;
    }

    if (post_data) {
        NX_PACKET *packet_ptr;
        /* Create a new data packet request on the HTTP(S) client instance. */
        nx_web_http_client_request_packet_allocate(&http_client, &packet_ptr, NX_WAIT_FOREVER);
        if (status != NX_SUCCESS) {
            printf("HTTP: Error while allocating packet: 0x%x\r\n", status);
            nx_web_http_client_delete(&http_client);
            return status;
        }

        status = nx_packet_data_append(packet_ptr, (VOID *)post_data, strlen(post_data),
                                       packet_ptr -> nx_packet_pool_owner,
                                       NX_WAIT_FOREVER);
        if (status) {
            printf("HTTP: Error while appending packet data: 0x%x\r\n", status);
            nx_web_http_client_delete(&http_client);
            return(status);
        }

         /* Send data packet request to server. */
        status = nx_web_http_client_request_packet_send(&http_client, packet_ptr, 0, NX_WAIT_FOREVER);
        if (status) {
            printf("HTTP: Error sending packet: 0x%x\r\n", status);
            nx_web_http_client_delete(&http_client);
            return(status);
        }

    }

    /* Receive response data from the server. Loop until all data is received. */
    NX_PACKET *receive_packet = NULL;
    UINT get_status = NX_SUCCESS;
    size_t data_length = 0;
    while (get_status != NX_WEB_HTTP_GET_DONE) {
    	get_status = nx_web_http_client_response_body_get(&http_client, &receive_packet, NX_WAIT_FOREVER);

        /* Check for error.  */
        if (get_status != NX_SUCCESS && get_status != NX_WEB_HTTP_GET_DONE) {
            printf("HTTP get packet failed, error: 0x%x\r\n", get_status);
            status = get_status;
            break;
        }

		UINT packet_len = receive_packet->nx_packet_length;
		if (packet_len == 0) {
			if (get_status == NX_WEB_HTTP_GET_DONE) {
				status = NX_SUCCESS;
			} else {
				// not sure how to handle this case. I guess break and send last status.
				status = get_status;
				printf("HTTP packet length was zero. Aborting the receive loop!");
			}
			break; // in both cases
		}

		ULONG bytes_received = 0;
		// TODO: remove this debug
		if (data_length > 0) {
			printf("DEBUG: HTTP: Packet data break here\r\n");
		}
		status = nx_packet_data_extract_offset( //
				receive_packet, // packet
				0, // offset in the packet
				&(r->response[data_length]), // where to put data
				IOTCONNECT_HTTP_RECEIVE_BUFFER_SIZE - data_length - 1 /* 1 for null */, // bytes left in buffer
				&bytes_received);
		if (status) {
			printf("HTTP: Packet data extraction error: 0x%x\r\n", status);
			break;
		}
		if (bytes_received >= IOTCONNECT_HTTP_RECEIVE_BUFFER_SIZE - data_length) {
			printf("HTTP Receive buffer empty! Increase IOTCONNECT_HTTP_RECEIVE_BUFFER_SIZE\r\n");
			// just return bad status so that the user can print what we have so far with += bytes_received below
			status = NX_OVERFLOW;
		}

    	nx_packet_release(receive_packet);
    	receive_packet = NULL;
		data_length += bytes_received;
		printf("HTTP: %lu bytes received.\r\n", bytes_received);
    }
    r->response[data_length] = 0; // terminate the string
    if (receive_packet) {
    	nx_packet_release(receive_packet);
    }
    status = nx_web_http_client_delete(&http_client);
    return NX_SUCCESS;
}

void iotconnect_free_https_response(IotConnectHttpResponse *response) {
    if (response) {
        free(response->response);
    }
}


VOID free_buffer(CHAR *buffer) {
    free(buffer);
}

/* Callback to setup TLS parameters for secure HTTPS. */
static UINT tls_setup_callback(NX_WEB_HTTP_CLIENT *client_ptr, NX_SECURE_TLS_SESSION *tls_session) {
    UINT status;

    NX_PARAMETER_NOT_USED(client_ptr);

    /* Initialize and create TLS session. */
    status = nx_secure_tls_session_create(tls_session, &nx_crypto_tls_ciphers, crypto_client_metadata,
            sizeof(crypto_client_metadata));

    /* Check status.  */
    if (status) {
        return (status);
    }

    /* Allocate space for packet reassembly. */
    status = nx_secure_tls_session_packet_buffer_set(tls_session, tls_packet_buffer, sizeof(tls_packet_buffer));

    /* Check status.  */
    if (status != NX_SUCCESS) {
        return (status);
    }

    /* Add a CA Certificate to our trusted store for verifying incoming server certificates. */
    nx_secure_x509_certificate_initialize(&trusted_certificate,
            (UCHAR*) IOTCONNECT_REST_API_CERT,
            (USHORT) IOTCONNECT_REST_API_CERT_SIZE,
            NX_NULL,
            0,
            NULL,
            0,
            NX_SECURE_X509_KEY_TYPE_NONE);
    nx_secure_tls_trusted_certificate_add(tls_session, &trusted_certificate);

    /* Need to allocate space for the certificate coming in from the remote host. */
    nx_secure_tls_remote_certificate_allocate(tls_session, &remote_certificate, remote_cert_buffer,
            sizeof(remote_cert_buffer));
    nx_secure_tls_remote_certificate_allocate(tls_session, &remote_issuer, remote_issuer_buffer,
            sizeof(remote_issuer_buffer));

    return (NX_SUCCESS);
}

