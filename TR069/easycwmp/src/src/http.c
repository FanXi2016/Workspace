/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Copyright (C) 2012-2014 PIVA SOFTWARE (www.pivasoftware.com)
 *		Author: Mohamed Kallel <mohamed.kallel@pivasoftware.com>
 *		Author: Anis Ellouze <anis.ellouze@pivasoftware.com>
 *	Copyright (C) 2011-2012 Luka Perkov <freecwmp@lukaperkov.net>
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <curl/curl.h>

#include "http.h"
#include "config.h"
#include "cwmp.h"
#include "easycwmp.h"
#include "digestauth.h"
#include "log.h"

static struct http_client http_c;
static struct http_server http_s;
CURL *curl;

int
http_client_init(void)
{
	//Updated by Tian Yiqing, Remove username and password information from the URL, Start
	if (asprintf(&http_c.url, "%s://%s:%s%s",
			 config->acs->scheme,
//			 config->acs->username,
//			 config->acs->password,
			 config->acs->hostname,
			 config->acs->port,
			 config->acs->path) == -1)
		return -1;
	//Updated by Tian Yiqing, Remove username and password information from the URL, End

	DDF("+++ HTTP CLIENT CONFIGURATION +++\n");
	DD("url: %s\n", http_c.url);
	if (config->acs->ssl_cert)
		DD("ssl_cert: %s\n", config->acs->ssl_cert);
	if (config->acs->ssl_cacert)
		DD("ssl_cacert: %s\n", config->acs->ssl_cacert);
	if (!config->acs->ssl_verify)
		DD("ssl_verify: SSL certificate validation disabled.\n");
	DDF("--- HTTP CLIENT CONFIGURATION ---\n");

	http_c.header_list = NULL;
	http_c.header_list = curl_slist_append(http_c.header_list, "User-Agent: easycwmp");
	if (!http_c.header_list) return -1;
	http_c.header_list = curl_slist_append(http_c.header_list, "Content-Type: text/xml");
	if (!http_c.header_list) return -1;
# ifdef ACS_FUSION
	char *expect_header = "Expect:";
	http_c.header_list = curl_slist_append(http_c.header_list, expect_header);
	if (!http_c.header_list) return -1;
# endif /* ACS_FUSION */
	curl = curl_easy_init();
	if (!curl) return -1;
	curl_easy_setopt(curl, CURLOPT_URL, http_c.url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, http_c.header_list);
	curl_easy_setopt(curl, CURLOPT_USERNAME, config->acs->username ? config->acs->username : "");
	curl_easy_setopt(curl, CURLOPT_PASSWORD, config->acs->password ? config->acs->password : "");
	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC|CURLAUTH_DIGEST);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_get_response);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);
# ifdef DEVEL
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
# endif /* DEVEL */
	curl_easy_setopt(curl, CURLOPT_COOKIEFILE, fc_cookies);
	curl_easy_setopt(curl, CURLOPT_COOKIEJAR, fc_cookies);
	if (config->acs->ssl_cert)
		curl_easy_setopt(curl, CURLOPT_SSLCERT, config->acs->ssl_cert);
	if (config->acs->ssl_cacert)
		curl_easy_setopt(curl, CURLOPT_CAINFO, config->acs->ssl_cacert);
	if (!config->acs->ssl_verify)
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);

	log_message(NAME, L_NOTICE, "configured acs url %s", http_c.url);
	return 0;
}

void
http_client_exit(void)
{
	FREE(http_c.url);

	curl_easy_cleanup(curl);
	curl_global_cleanup();
	if (http_c.header_list) {
		curl_slist_free_all(http_c.header_list);
		http_c.header_list = NULL;
	}
	if (access(fc_cookies, W_OK) == 0)
		remove(fc_cookies);
}

static size_t
http_get_response(void *buffer, size_t size, size_t rxed, char **msg_in)
{
	char *c;

	if (asprintf(&c, "%s%.*s", *msg_in, size * rxed, buffer) == -1) {
		FREE(*msg_in);
		return -1;
	}

	free(*msg_in);
	*msg_in = c;

	DDF("+++ RECEIVED HTTP RESPONSE (PART) +++\n");
	DDF("%.*s", size * rxed, buffer);
	DDF("--- RECEIVED HTTP RESPONSE (PART) ---\n");

	return size * rxed;
}

int8_t
http_send_message(char *msg_out, char **msg_in)
{
	CURLcode res;

	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, msg_out);
	if (msg_out)
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long) strlen(msg_out));
	else
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);

	curl_easy_setopt(curl, CURLOPT_WRITEDATA, msg_in);

	*msg_in = (char *) calloc (1, sizeof(char));

	res = curl_easy_perform(curl);

	if (!strlen(*msg_in)) {
		FREE(*msg_in);
	}
	
	long httpCode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

	if (res || (httpCode != 200 && httpCode != 204)) {
		log_message(NAME, L_NOTICE, "sending http message failed\n");
		return -1;
	}


	if (*msg_in) {
		DDF("+++ RECEIVED HTTP RESPONSE +++\n");
		DDF("%s", *msg_in);
		DDF("--- RECEIVED HTTP RESPONSE ---\n");
	} else {
		DDF("+++ RECEIVED EMPTY HTTP RESPONSE +++\n");
	}

	return 0;
}

void
http_server_init(void)
{
	http_s.http_event.cb = http_new_client;

	http_s.http_event.fd = usock(USOCK_TCP | USOCK_SERVER, config->local->ip, config->local->port);
	uloop_fd_add(&http_s.http_event, ULOOP_READ | ULOOP_EDGE_TRIGGER);

	DDF("+++ HTTP SERVER CONFIGURATION +++\n");
	if (config->local->ip)
		DDF("ip: '%s'\n", config->local->ip);
	else
		DDF("NOT BOUND TO IP\n");
	DDF("port: '%s'\n", config->local->port);
	DDF("--- HTTP SERVER CONFIGURATION ---\n");

	log_message(NAME, L_NOTICE, "http server initialized\n");
}

static void
http_new_client(struct uloop_fd *ufd, unsigned events)
{
	int status;
	struct timeval t;

	t.tv_sec = 60;
	t.tv_usec = 0;

	for (;;) {
		int client = accept(ufd->fd, NULL, NULL);

		/* set one minute timeout */
		if (setsockopt(ufd->fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t, sizeof t)) {
			DD("setsockopt() failed\n");
		}

		if (client == -1)
			break;

		struct uloop_process *uproc = calloc(1, sizeof(*uproc));
		if (!uproc || (uproc->pid = fork()) == -1) {
			FREE(uproc);
			close(client);
		}

		if (uproc->pid != 0) {
			/* parent */
			/* register an event handler for when the child terminates */
			uproc->cb = http_del_client;
			uloop_process_add(uproc);
			close(client);
		} else {
			/* child */
			FILE *fp;
			char buffer[BUFSIZ];
			char *auth_digest;
			int8_t auth_status = 0;
			
			fp = fdopen(client, "r+");

			DDF("+++ RECEIVED HTTP REQUEST +++\n");
			while (fgets(buffer, sizeof(buffer), fp)) {
				char *username = config->local->username;
				char *password = config->local->password;
				if (!username || !password) {
					// if we dont have username or password configured proceed with connecting to ACS
					auth_status = 1;
				}
				else if (auth_digest = strstr(buffer, "Authorization: Digest ")) {
					if (http_digest_auth_check("GET", "/", auth_digest + strlen("Authorization: Digest "), REALM, username, password, 300) == MHD_YES)
						auth_status = 1;
					else
						auth_status = 0;
				}
				if (buffer[0] == '\r' || buffer[0] == '\n') {
					/* end of http request (empty line) */
					goto http_end_child;
				}
			}
error_child:
			/* here we are because of an error, e.g. timeout */
			status = ETIMEDOUT|ENOMEM;
			goto done_child;

http_end_child:
			fflush(fp);
			if (auth_status) {
				status = 0;
				fputs("HTTP/1.1 200 OK\r\n", fp);
			} else {
				status = EACCES;
				fputs("HTTP/1.1 401 Unauthorized\r\n", fp);
				fputs("Connection: close\r\n", fp);
				http_digest_auth_fail_response(fp, "GET", "/", REALM, OPAQUE);
			}
			fputs("\r\n", fp);
			goto done_child;

done_child:
			fclose(fp);
			FREE(uproc);
			DDF("--- RECEIVED HTTP REQUEST ---\n");
			exit(status);
		}
	}
}

static void
http_del_client(struct uloop_process *uproc, int ret)
{
	FREE(uproc);

	/* child terminated ; check return code */
	if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) {
		DDF("+++ HTTP SERVER CONNECTION SUCCESS +++\n");
		log_message(NAME, L_NOTICE, "acs initiated connection");
		cwmp_connection_request(EVENT_CONNECTION_REQUEST);
	} else {
		DDF("+++ HTTP SERVER CONNECTION FAILED +++\n");
	}
}

