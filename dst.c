/*
 * Dynamic Split Tunneling (DST) — parse + stable C API + platform routing.
 *
 * Compatible with ocserv-tunnel / AnyConnect X-CSTP-Post-Auth-XML.
 *
 * Copyright (C) 2026 openconnect-tunnel contributors
 * Based on OpenConnect (LGPL-2.1-or-later).
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#endif

#include "openconnect-internal.h"

#ifndef DST_DEFAULT_POLL_SECS
#define DST_DEFAULT_POLL_SECS 5
#endif

struct dst_installed_route {
	char *ip;
	int exclude;
	struct dst_installed_route *next;
};

static void append_dst_domain(struct oc_split_include **head, const char *domain)
{
	struct oc_split_include *inc;
	char *dup;
	size_t len;

	if (!domain)
		return;
	while (*domain && isspace((unsigned char)*domain))
		domain++;
	len = strlen(domain);
	while (len > 0 && isspace((unsigned char)domain[len - 1]))
		len--;
	if (len == 0)
		return;

	dup = strndup(domain, len);
	if (!dup)
		return;

	inc = malloc(sizeof(*inc));
	if (!inc) {
		free(dup);
		return;
	}
	inc->route = dup;
	inc->next = *head;
	*head = inc;
}

static void append_dst_csv(struct oc_split_include **head, const char *csv)
{
	char *buf, *p, *tok;

	if (!csv || !*csv)
		return;
	buf = strdup(csv);
	if (!buf)
		return;
	p = buf;
	while (p && *p) {
		tok = p;
		p = strchr(p, ',');
		if (p)
			*p++ = 0;
		append_dst_domain(head, tok);
	}
	free(buf);
}

static void walk_dst_nodes(xmlNodePtr node, struct oc_ip_info *ip)
{
	for (; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE) {
			if (!xmlStrcmp(node->name, BAD_CAST "dynamic-split-include-domains")) {
				xmlChar *content = xmlNodeGetContent(node);
				if (content) {
					append_dst_csv(&ip->dynamic_split_includes,
						       (const char *)content);
					xmlFree(content);
				}
			} else if (!xmlStrcmp(node->name,
					      BAD_CAST "dynamic-split-exclude-domains")) {
				xmlChar *content = xmlNodeGetContent(node);
				if (content) {
					append_dst_csv(&ip->dynamic_split_excludes,
						       (const char *)content);
					xmlFree(content);
				}
			}
		}
		if (node->children)
			walk_dst_nodes(node->children, ip);
	}
}

void free_dynamic_split_domains(struct oc_ip_info *ip_info)
{
	struct oc_split_include *inc;

	for (inc = ip_info->dynamic_split_includes; inc; ) {
		struct oc_split_include *next = inc->next;
		free((char *)inc->route);
		free(inc);
		inc = next;
	}
	for (inc = ip_info->dynamic_split_excludes; inc; ) {
		struct oc_split_include *next = inc->next;
		free((char *)inc->route);
		free(inc);
		inc = next;
	}
	ip_info->dynamic_split_includes = NULL;
	ip_info->dynamic_split_excludes = NULL;
}

static void free_dst_arrays(struct openconnect_info *vpninfo)
{
	free(vpninfo->dst_inc_array);
	free(vpninfo->dst_exc_array);
	vpninfo->dst_inc_array = NULL;
	vpninfo->dst_exc_array = NULL;
	vpninfo->dst_inc_n = 0;
	vpninfo->dst_exc_n = 0;
}

static int rebuild_dst_arrays(struct openconnect_info *vpninfo)
{
	struct oc_split_include *p;
	int n, i;

	free_dst_arrays(vpninfo);

	n = 0;
	for (p = vpninfo->ip_info.dynamic_split_includes; p; p = p->next)
		n++;
	if (n) {
		vpninfo->dst_inc_array = calloc(n, sizeof(char *));
		if (!vpninfo->dst_inc_array)
			return -ENOMEM;
		i = 0;
		for (p = vpninfo->ip_info.dynamic_split_includes; p; p = p->next)
			vpninfo->dst_inc_array[i++] = (char *)p->route;
		vpninfo->dst_inc_n = n;
	}

	n = 0;
	for (p = vpninfo->ip_info.dynamic_split_excludes; p; p = p->next)
		n++;
	if (n) {
		vpninfo->dst_exc_array = calloc(n, sizeof(char *));
		if (!vpninfo->dst_exc_array)
			return -ENOMEM;
		i = 0;
		for (p = vpninfo->ip_info.dynamic_split_excludes; p; p = p->next)
			vpninfo->dst_exc_array[i++] = (char *)p->route;
		vpninfo->dst_exc_n = n;
	}
	return 0;
}

int parse_post_auth_dst(struct openconnect_info *vpninfo, const char *xml,
			struct oc_ip_info *ip_info)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	int n_inc = 0, n_exc = 0;
	struct oc_split_include *p;

	if (!xml || !ip_info)
		return -EINVAL;

	free_dynamic_split_domains(ip_info);

	doc = xmlReadMemory(xml, strlen(xml), "post-auth.xml", NULL,
			    XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
	if (!doc) {
		vpn_progress(vpninfo, PRG_DEBUG,
			     _("Failed to parse X-CSTP-Post-Auth-XML for DST\n"));
		return -EINVAL;
	}

	root = xmlDocGetRootElement(doc);
	if (root)
		walk_dst_nodes(root, ip_info);
	xmlFreeDoc(doc);

	for (p = ip_info->dynamic_split_includes; p; p = p->next)
		n_inc++;
	for (p = ip_info->dynamic_split_excludes; p; p = p->next)
		n_exc++;

	if (n_inc || n_exc)
		vpn_progress(vpninfo, PRG_INFO,
			     _("DST domains: %d include, %d exclude (from Post-Auth XML)\n"),
			     n_inc, n_exc);

	return 0;
}

void openconnect_dst_notify(struct openconnect_info *vpninfo)
{
	if (rebuild_dst_arrays(vpninfo) < 0)
		return;
	if (vpninfo->dst_domains_cb)
		vpninfo->dst_domains_cb(vpninfo->cbdata,
					(const char **)vpninfo->dst_inc_array,
					vpninfo->dst_inc_n,
					(const char **)vpninfo->dst_exc_array,
					vpninfo->dst_exc_n);
}

int openconnect_get_dynamic_split_domains(struct openconnect_info *vpninfo,
					 const char ***includes, int *n_include,
					 const char ***excludes, int *n_exclude)
{
	if (!vpninfo)
		return -EINVAL;
	if (rebuild_dst_arrays(vpninfo) < 0)
		return -ENOMEM;
	if (includes)
		*includes = (const char **)vpninfo->dst_inc_array;
	if (n_include)
		*n_include = vpninfo->dst_inc_n;
	if (excludes)
		*excludes = (const char **)vpninfo->dst_exc_array;
	if (n_exclude)
		*n_exclude = vpninfo->dst_exc_n;
	return 0;
}

void openconnect_set_dst_domains_handler(struct openconnect_info *vpninfo,
					openconnect_dst_domains_vfn cb)
{
	if (vpninfo)
		vpninfo->dst_domains_cb = cb;
}

void openconnect_set_dst_routing(struct openconnect_info *vpninfo, int enable)
{
	if (!vpninfo)
		return;
	vpninfo->dst_routing = enable ? 1 : 0;
	if (!vpninfo->dst_poll_secs)
		vpninfo->dst_poll_secs = DST_DEFAULT_POLL_SECS;
}

void openconnect_set_dst_poll_interval(struct openconnect_info *vpninfo, int seconds)
{
	if (!vpninfo)
		return;
	if (seconds < 1)
		seconds = 1;
	vpninfo->dst_poll_secs = seconds;
}

/* ---- platform host-route helpers ---- */

#if !defined(_WIN32)
static int dst_run_cmd(struct openconnect_info *vpninfo, char *const argv[])
{
	pid_t pid;
	int status;

	(void)vpninfo;
	pid = fork();
	if (pid < 0)
		return -errno;
	if (pid == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -errno;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	return -EIO;
}
#endif

static int dst_add_include_route(struct openconnect_info *vpninfo, const char *ip)
{
	const char *ifname = vpninfo->ifname;
	char host[64];

	if (!ifname || !ip)
		return -EINVAL;
#if defined(__linux__)
	snprintf(host, sizeof(host), "%s/32", ip);
	{
		char *argv[8];
		argv[0] = (char *)"ip";
		argv[1] = (char *)"-4";
		argv[2] = (char *)"route";
		argv[3] = (char *)"replace";
		argv[4] = host;
		argv[5] = (char *)"dev";
		argv[6] = (char *)ifname;
		argv[7] = NULL;
		return dst_run_cmd(vpninfo, argv);
	}
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
	{
		char *argv[8];
		argv[0] = (char *)"route";
		argv[1] = (char *)"-n";
		argv[2] = (char *)"change";
		argv[3] = (char *)"-host";
		argv[4] = (char *)ip;
		argv[5] = (char *)"-interface";
		argv[6] = (char *)ifname;
		argv[7] = NULL;
		if (dst_run_cmd(vpninfo, argv) == 0)
			return 0;
		argv[2] = (char *)"add";
		return dst_run_cmd(vpninfo, argv);
	}
#elif defined(_WIN32)
	{
		char cmd[256];
		snprintf(cmd, sizeof(cmd),
			 "route ADD %s MASK 255.255.255.255 0.0.0.0 IF %s",
			 ip, ifname);
		return system(cmd) == 0 ? 0 : -EIO;
	}
#else
	(void)host;
	return -ENOSYS;
#endif
}

static int dst_del_include_route(struct openconnect_info *vpninfo, const char *ip)
{
	const char *ifname = vpninfo->ifname;
	char host[64];

	if (!ip)
		return -EINVAL;
#if defined(__linux__)
	snprintf(host, sizeof(host), "%s/32", ip);
	{
		char *argv[8];
		argv[0] = (char *)"ip";
		argv[1] = (char *)"-4";
		argv[2] = (char *)"route";
		argv[3] = (char *)"del";
		argv[4] = host;
		if (ifname) {
			argv[5] = (char *)"dev";
			argv[6] = (char *)ifname;
			argv[7] = NULL;
		} else {
			argv[5] = NULL;
		}
		return dst_run_cmd(vpninfo, argv);
	}
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
	{
		char *argv[6];
		argv[0] = (char *)"route";
		argv[1] = (char *)"-n";
		argv[2] = (char *)"delete";
		argv[3] = (char *)"-host";
		argv[4] = (char *)ip;
		argv[5] = NULL;
		return dst_run_cmd(vpninfo, argv);
	}
#elif defined(_WIN32)
	{
		char cmd[128];
		snprintf(cmd, sizeof(cmd), "route DELETE %s", ip);
		return system(cmd) == 0 ? 0 : -EIO;
	}
#else
	(void)vpninfo;
	(void)ifname;
	(void)host;
	return -ENOSYS;
#endif
}

static int dst_add_exclude_route(struct openconnect_info *vpninfo, const char *ip)
{
	const char *gw = vpninfo->dst_orig_gw;
	const char *dev = vpninfo->dst_orig_dev;
	char host[64];

	if (!ip || !gw)
		return -EINVAL;
#if defined(__linux__)
	snprintf(host, sizeof(host), "%s/32", ip);
	{
		char *argv[10];
		int i = 0;
		argv[i++] = (char *)"ip";
		argv[i++] = (char *)"-4";
		argv[i++] = (char *)"route";
		argv[i++] = (char *)"replace";
		argv[i++] = host;
		argv[i++] = (char *)"via";
		argv[i++] = (char *)gw;
		if (dev) {
			argv[i++] = (char *)"dev";
			argv[i++] = (char *)dev;
		}
		argv[i] = NULL;
		return dst_run_cmd(vpninfo, argv);
	}
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
	{
		char *argv[7];
		argv[0] = (char *)"route";
		argv[1] = (char *)"-n";
		argv[2] = (char *)"change";
		argv[3] = (char *)"-host";
		argv[4] = (char *)ip;
		argv[5] = (char *)gw;
		argv[6] = NULL;
		if (dst_run_cmd(vpninfo, argv) == 0)
			return 0;
		argv[2] = (char *)"add";
		return dst_run_cmd(vpninfo, argv);
	}
#elif defined(_WIN32)
	{
		char cmd[256];
		snprintf(cmd, sizeof(cmd),
			 "route ADD %s MASK 255.255.255.255 %s", ip, gw);
		return system(cmd) == 0 ? 0 : -EIO;
	}
#else
	(void)dev;
	(void)host;
	return -ENOSYS;
#endif
}

static int dst_del_exclude_route(struct openconnect_info *vpninfo, const char *ip)
{
	return dst_del_include_route(vpninfo, ip);
}

static void dst_remember_route(struct openconnect_info *vpninfo, const char *ip, int exclude)
{
	struct dst_installed_route *r = malloc(sizeof(*r));
	if (!r)
		return;
	r->ip = strdup(ip);
	if (!r->ip) {
		free(r);
		return;
	}
	r->exclude = exclude;
	r->next = vpninfo->dst_installed;
	vpninfo->dst_installed = r;
}

static int dst_route_known(struct openconnect_info *vpninfo, const char *ip, int exclude)
{
	struct dst_installed_route *r;
	for (r = vpninfo->dst_installed; r; r = r->next) {
		if (r->exclude == exclude && !strcmp(r->ip, ip))
			return 1;
	}
	return 0;
}

static void dst_detect_orig_gateway(struct openconnect_info *vpninfo)
{
#if defined(__linux__)
	FILE *fp;
	char line[256], gw[64], dev[64];

	if (vpninfo->dst_orig_gw)
		return;
	fp = popen("ip -4 route show default 2>/dev/null", "r");
	if (!fp)
		return;
	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, "default via %63s dev %63s", gw, dev) == 2) {
			if (vpninfo->ifname && !strcmp(dev, vpninfo->ifname))
				continue;
			vpninfo->dst_orig_gw = strdup(gw);
			vpninfo->dst_orig_dev = strdup(dev);
			break;
		}
	}
	pclose(fp);
#elif defined(__APPLE__) || defined(__FreeBSD__)
	FILE *fp;
	char line[256], gw[64];

	if (vpninfo->dst_orig_gw)
		return;
	fp = popen("route -n get default 2>/dev/null", "r");
	if (!fp)
		return;
	while (fgets(line, sizeof(line), fp)) {
		if (sscanf(line, " gateway: %63s", gw) == 1) {
			vpninfo->dst_orig_gw = strdup(gw);
			break;
		}
	}
	pclose(fp);
#else
	(void)vpninfo;
#endif
}

static void dst_resolve_and_apply(struct openconnect_info *vpninfo, const char *domain,
				  int exclude)
{
	struct addrinfo hints, *res, *rp;
	int ret;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (vpninfo->getaddrinfo_override)
		ret = vpninfo->getaddrinfo_override(vpninfo->cbdata, domain, NULL, &hints, &res);
	else
		ret = getaddrinfo(domain, NULL, &hints, &res);
	if (ret)
		return;

	for (rp = res; rp; rp = rp->ai_next) {
		char ip[INET_ADDRSTRLEN];
		struct sockaddr_in *sin = (struct sockaddr_in *)rp->ai_addr;

		if (!inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip)))
			continue;
		if (vpninfo->ip_info.gateway_addr &&
		    !strcmp(ip, vpninfo->ip_info.gateway_addr))
			continue;
		if (dst_route_known(vpninfo, ip, exclude))
			continue;
		if (exclude) {
			if (dst_add_exclude_route(vpninfo, ip) == 0)
				dst_remember_route(vpninfo, ip, 1);
		} else {
			if (dst_add_include_route(vpninfo, ip) == 0)
				dst_remember_route(vpninfo, ip, 0);
		}
	}
	freeaddrinfo(res);
}

void openconnect_dst_clear_routes(struct openconnect_info *vpninfo)
{
	struct dst_installed_route *r, *next;

	if (!vpninfo)
		return;
	for (r = vpninfo->dst_installed; r; r = next) {
		next = r->next;
		if (r->exclude)
			dst_del_exclude_route(vpninfo, r->ip);
		else
			dst_del_include_route(vpninfo, r->ip);
		free(r->ip);
		free(r);
	}
	vpninfo->dst_installed = NULL;
	free(vpninfo->dst_orig_gw);
	free(vpninfo->dst_orig_dev);
	vpninfo->dst_orig_gw = NULL;
	vpninfo->dst_orig_dev = NULL;
	free_dst_arrays(vpninfo);
}

int openconnect_dst_sync_routes(struct openconnect_info *vpninfo)
{
	struct oc_split_include *p;

	if (!vpninfo)
		return -EINVAL;
	if (!vpninfo->ip_info.dynamic_split_includes &&
	    !vpninfo->ip_info.dynamic_split_excludes)
		return 0;

	dst_detect_orig_gateway(vpninfo);

	for (p = vpninfo->ip_info.dynamic_split_includes; p; p = p->next)
		dst_resolve_and_apply(vpninfo, p->route, 0);
	for (p = vpninfo->ip_info.dynamic_split_excludes; p; p = p->next)
		dst_resolve_and_apply(vpninfo, p->route, 1);

	vpninfo->dst_last_poll = time(NULL);
	return 0;
}

int openconnect_dst_poll(struct openconnect_info *vpninfo, int *timeout_ms)
{
	time_t now, due;
	int secs;

	if (!vpninfo || !vpninfo->dst_routing)
		return 0;
	if (!vpninfo->ip_info.dynamic_split_includes &&
	    !vpninfo->ip_info.dynamic_split_excludes)
		return 0;

	secs = vpninfo->dst_poll_secs > 0 ? vpninfo->dst_poll_secs : DST_DEFAULT_POLL_SECS;
	now = time(NULL);
	due = vpninfo->dst_last_poll + secs;
	if (now >= due) {
		openconnect_dst_sync_routes(vpninfo);
		return 1;
	}
	if (timeout_ms) {
		int remain = (int)((due - now) * 1000);
		if (remain < *timeout_ms)
			*timeout_ms = remain > 0 ? remain : 0;
	}
	return 0;
}
