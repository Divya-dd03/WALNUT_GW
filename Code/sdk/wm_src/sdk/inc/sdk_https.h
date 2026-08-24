/**
 ******************************************************************************
 * @file    sdk_https.h
 * @author  Walnut Medical
 * @brief   Common Gateway SDK - HTTP/HTTPS client API.
 *
 *          A request is built up on a session, addressed by an "SSL index", and
 *          then run:
 *
 *              sdk_https_init()           choose sync or async delivery
 *              sdk_https_configure_ssl()  root CA / client certificate + key
 *              sdk_https_set_params()     URL and timeout
 *              sdk_https_set_header()     custom request headers   (optional)
 *              sdk_https_set_data()       request body             (POST/PUT)
 *              sdk_https_action()         run the request
 *              sdk_https_read()           drain the response body
 *              sdk_https_terminate()      close the session, free its buffers
 *
 *          Sessions 0..SDK_HTTPS_SESSION_MAX-1 are general purpose. The
 *          sdk_https_download_* calls take no index of their own and work on
 *          the reserved SDK_HTTPS_DOWNLOAD_INDEX session, which is pointed at
 *          the file with sdk_https_set_params() like any other.
 *
 *          A PDP context must be up before a request can succeed (see
 *          sdk_network.h). Both http:// and https:// URLs are accepted; TLS is
 *          selected by the scheme, and the server certificate is verified only
 *          when a root CA has been supplied to sdk_https_configure_ssl().
 *
 *          Response bodies are streamed: sdk_https_read() returns the body in
 *          successive pieces of at most SDK_HTTPS_RESP_BUF_SIZE bytes and
 *          reports 0 at the end, so a body need never be held whole in RAM.
 *          Bodies that fit in one piece always arrive intact, as do larger ones
 *          sent with a Content-Length - which includes every ranged response,
 *          and so the whole sdk_https_download_* path. The exception is a
 *          chunked body larger than one piece: it can fail part-way through with
 *          SDK_RESULT_ERROR. Fetch anything large by ranges instead.
 *
 *          Stack: a task that calls sdk_https_action() in SDK_HTTPS_MODE_SYNC
 *          needs roughly 16 KB of stack, and one that calls sdk_https_read()
 *          roughly 8 KB. SDK_HTTPS_MODE_ASYNC runs the request on an SDK task
 *          that is already sized for it.
 *
 *          Concurrency: sessions are independent of one another, but a single
 *          session must be driven from one task at a time. In async mode a
 *          session with a request in flight rejects further calls with
 *          SDK_RESULT_BUSY.
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Walnut Medical
 * All rights reserved.
 *
 ******************************************************************************
 */

#ifndef __SDK_HTTPS_H__
#define __SDK_HTTPS_H__

#include "sdk_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*******************************************************************************
** Action selectors for sdk_https_action()
******************************************************************************/
#define SDK_HTTPS_ACTION_GET    (0)
#define SDK_HTTPS_ACTION_POST   (1)
#define SDK_HTTPS_ACTION_PUT    (2)
#define SDK_HTTPS_ACTION_DELETE (3)
#define SDK_HTTPS_ACTION_HEAD   (4)

/*******************************************************************************
** Functions
******************************************************************************/
/**
 * @brief  Initialise the HTTPS client and choose how results are delivered.
 *         Idempotent; the mode may be changed later, but not while a request is
 *         in flight.
 * @param  tx_mode  SDK_HTTPS_MODE_SYNC or SDK_HTTPS_MODE_ASYNC (SdkHttpsTxMode).
 * @param  msgq     queue receiving one SdkHttpsEvent per completed request;
 *                  required in async mode, ignored in sync mode. Create it with
 *                  msg_size == sizeof(SdkHttpsEvent).
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on an unknown mode or
 *                     a NULL queue in async mode; SDK_RESULT_BUSY if a request
 *                     is still in flight.
 */
SdkResult sdk_https_init(UINT32 tx_mode, void *msgq);

/**
 * @brief  Configure the TLS credentials for a session. Supplying a root CA is
 *         what turns on server-certificate verification; without one an https://
 *         request still negotiates TLS but does not authenticate the server.
 *         A client certificate and key select mutual TLS - both are needed, and
 *         they are ignored unless a CA is present as well.
 *
 *         The PEM blobs are NOT copied: they stay CALLER-OWNED and must remain
 *         valid until the session is terminated. Each must be NUL-terminated.
 *
 * @param  ssl_index    session index, 0..SDK_HTTPS_SESSION_MAX-1 or
 *                      SDK_HTTPS_DOWNLOAD_INDEX.
 * @param  ca_cert      root CA in PEM, or NULL for no server verification.
 * @param  client_cert  client certificate in PEM, or NULL.
 * @param  client_key   client private key in PEM, or NULL.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a bad index;
 *                     SDK_RESULT_BUSY if a request is in flight on the session.
 */
SdkResult sdk_https_configure_ssl(UINT32 ssl_index, const char *ca_cert,
                                  const char *client_cert, const char *client_key);

/**
 * @brief  Set the request URL and timeout. The URL is copied and may be
 *         http:// or https://; an explicit port is honoured, otherwise 80/443
 *         apply. Any response still buffered on this session is discarded.
 * @param  ssl_index  session index.
 * @param  url        request URL, at most SDK_HTTPS_URL_MAX-1 characters.
 * @param  timeout    transport send/receive timeout in seconds; 0 selects
 *                    SDK_HTTPS_DEFAULT_TIMEOUT. This bounds each transport
 *                    operation, not the request as a whole.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a bad index, a
 *                     NULL/empty URL or one that does not fit;
 *                     SDK_RESULT_BUSY if a request is in flight.
 */
SdkResult sdk_https_set_params(UINT32 ssl_index, const char *url, UINT32 timeout);

/**
 * @brief  Set the request body, sent by SDK_HTTPS_ACTION_POST and _PUT and
 *         ignored by the others. The bytes are NOT copied: the buffer stays
 *         CALLER-OWNED and must remain valid until the action completes (in
 *         async mode, until the completion event arrives). Pass NULL to clear a
 *         previously set body.
 * @param  ssl_index  session index.
 * @param  data       payload bytes; may be binary.
 * @param  data_len   payload length in bytes.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a bad index or a
 *                     non-NULL body of zero length; SDK_RESULT_BUSY if a
 *                     request is in flight.
 */
SdkResult sdk_https_set_data(UINT32 ssl_index, const char *data, UINT32 data_len);

/**
 * @brief  Run the request against the configured URL.
 *
 *         In SDK_HTTPS_MODE_SYNC this blocks until the response headers have
 *         been read and the first part of the body is available; success means
 *         the exchange completed, NOT that the server was happy - check
 *         sdk_https_get_status_code() for that. In SDK_HTTPS_MODE_ASYNC it
 *         queues the request and returns immediately; the body is only valid
 *         once the SdkHttpsEvent for the session has been received.
 *
 * @param  ssl_index  session index.
 * @param  action     one of the SDK_HTTPS_ACTION_* selectors.
 * @return SdkResult - 0 success (async: request accepted);
 *                     SDK_RESULT_INVALID_PARAM on a bad index, an unknown
 *                     action or an unparseable URL; SDK_RESULT_NOT_INITIALIZED
 *                     if no URL has been set; SDK_RESULT_BUSY if a request is
 *                     already in flight; SDK_RESULT_ERROR on any transport
 *                     failure - DNS, connect, TLS, protocol, or a peer that
 *                     went quiet past the timeout. sdk_https_get_last_error()
 *                     distinguishes them.
 */
SdkResult sdk_https_action(UINT32 ssl_index, UINT32 action);

/**
 * @brief  Copy the next bytes of the response body out of the session. Call it
 *         repeatedly until it reports 0 bytes, which marks the end of the body.
 *         The bytes are raw and may be binary.
 * @param  ssl_index   session index.
 * @param  buffer      [out] destination.
 * @param  size        size of @p buffer in bytes.
 * @param  bytes_read  [out] bytes actually copied; 0 at end of body.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a bad index or
 *                     NULL buffer; SDK_RESULT_NOT_INITIALIZED if no response is
 *                     available; SDK_RESULT_BUSY if a request is in flight;
 *                     SDK_RESULT_ERROR if the transfer failed part-way through
 *                     the body.
 */
SdkResult sdk_https_read(UINT32 ssl_index, void *buffer, UINT32 size, UINT32 *bytes_read);

/**
 * @brief  Get a pointer to the part of the response body currently available on
 *         the session that ran the most recent action. The bytes are also
 *         NUL-terminated, so a text response can be used as a string. Valid
 *         until the next sdk_https_read() / action / terminate on that session.
 * @return pointer to response bytes; NULL if none is available.
 */
const char *sdk_https_get_response(void);

/**
 * @brief  Get the number of bytes sdk_https_get_response() is pointing at. This
 *         is the part currently available, not the total body length, which for
 *         a streamed response is only known once it has been read to the end.
 * @return length in bytes; 0 if no response is available.
 */
UINT32 sdk_https_get_response_len(void);

/**
 * @brief  Close a session's connection, release its buffers and clear its URL,
 *         headers, body and TLS configuration. The index becomes free for
 *         reuse. Idempotent.
 * @param  ssl_index  session index.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a bad index;
 *                     SDK_RESULT_BUSY if a request is still in flight.
 */
SdkResult sdk_https_terminate(UINT32 ssl_index);

/*******************************************************************************
** File download (SDK_HTTPS_DOWNLOAD_INDEX session)
**
** For fetching a file by ranges rather than streaming it in one pass: useful
** when each part is written straight to flash. Point the session at the file
** with sdk_https_set_params(SDK_HTTPS_DOWNLOAD_INDEX, url, timeout) first.
** These three return an int (0 / < 0) rather than SdkResult, per the API spec.
******************************************************************************/
/**
 * @brief  Report whether the download session has TLS credentials in place.
 *
 *         The credentials themselves are supplied the same way as for any other
 *         session, with sdk_https_configure_ssl(SDK_HTTPS_DOWNLOAD_INDEX, ...).
 *         Read them from wherever the application keeps them first -
 *         sdk_storage_cred_read() for the provisioned set - and note that they
 *         stay caller-owned until the session is terminated.
 * @return 0 when a root CA is configured; < 0 when none is, meaning an https://
 *         download would run without authenticating the server.
 */
int sdk_https_download_configure_ssl(void);

/**
 * @brief  Query the size of the remote file.
 * @param  file_size  [out] file size in bytes.
 * @return 0 on success; < 0 if the size could not be determined, which means
 *         the server will not support ranged reads either.
 */
int sdk_https_download_get_file_size(UINT32 *file_size);

/**
 * @brief  Read one chunk of the remote file. Each call is a self-contained
 *         request, so chunks may be fetched in any order and a failed one can
 *         simply be retried.
 * @param  offset      byte offset to read from.
 * @param  size        number of bytes wanted; the tail chunk returns fewer.
 * @param  buffer      [out] chunk buffer, at least @p size bytes.
 * @param  bytes_read  [out] bytes actually placed in @p buffer.
 * @return 0 on success; < 0 on a bad argument, a transport failure, or a server
 *         that does not honour ranged requests.
 */
int sdk_https_download_read_chunk(UINT32 offset, UINT32 size, char *buffer, UINT32 *bytes_read);

/*******************************************************************************
** Extensions beyond WEGW_API_REQUIREMENTS_V0
**
** The specified set above cannot express request headers, and cannot report
** whether the server answered 200 or 404, so these fill those gaps.
******************************************************************************/
/**
 * @brief  Set the custom request headers sent with the next action. The block
 *         is copied and is one or more "Name: value" lines; a missing trailing
 *         CRLF is added. Pass NULL to clear.
 *
 *         Host is always sent, as are Content-Length and Content-Type when
 *         there is a body, so do not repeat those here. Requests are HTTP/1.1
 *         without "Connection: close": add that header for a server that ends
 *         its response by closing the connection rather than sending a
 *         Content-Length or chunking it.
 * @param  ssl_index  session index.
 * @param  headers    header block, at most SDK_HTTPS_HEADER_MAX-3 characters.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a bad index or an
 *                     oversized block; SDK_RESULT_BUSY if a request is in
 *                     flight.
 */
SdkResult sdk_https_set_header(UINT32 ssl_index, const char *headers);

/**
 * @brief  Set the Content-Type sent with a body. Copied; defaults to
 *         "application/json" when never set. Ignored when there is no body.
 * @param  ssl_index     session index.
 * @param  content_type  MIME type, at most SDK_HTTPS_CONTENT_TYPE_MAX-1
 *                       characters; NULL restores the default.
 * @return SdkResult - 0 success; SDK_RESULT_INVALID_PARAM on a bad index or an
 *                     oversized type; SDK_RESULT_BUSY if a request is in flight.
 */
SdkResult sdk_https_set_content_type(UINT32 ssl_index, const char *content_type);

/**
 * @brief  Get the HTTP status code from the most recent action on a session,
 *         e.g. 200, 404 or 500. This is the server's verdict; sdk_https_action()
 *         reports only whether the exchange itself worked.
 * @param  ssl_index  session index.
 * @return the status code; 0 if the session has not completed a request or a
 *         bad index was given.
 */
INT32 sdk_https_get_status_code(UINT32 ssl_index);

/**
 * @brief  Get the raw client error code from the most recent action on a
 *         session, for diagnostics beyond SdkResult (-1 connection failed,
 *         -2 closed by peer, -3 unknown, -4 protocol, -5 DNS, -6 URL parse,
 *         -7 out of memory).
 * @param  ssl_index  session index.
 * @return the last underlying code; 0 if the last action succeeded.
 */
INT32 sdk_https_get_last_error(UINT32 ssl_index);

#ifdef __cplusplus
}
#endif

#endif /* __SDK_HTTPS_H__ */
