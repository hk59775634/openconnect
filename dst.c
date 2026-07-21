/*
 * Dynamic Split Tunneling (DST) for ocserv-tunnel / AnyConnect Post-Auth XML.
 *
 * Parses X-CSTP-Post-Auth-XML for:
 *   <dynamic-split-include-domains><![CDATA[a.com,b.com]]></dynamic-split-include-domains>
 *   <dynamic-split-exclude-domains><![CDATA[c.com]]></dynamic-split-exclude-domains>
 *
 * Domains are exported to the vpnc-script environment; a helper can poll DNS
 * and install host routes (AnyConnect-compatible behaviour on Linux).
 *
 * Copyright (C) 2026 ocserv-tunnel / openconnect-tunnel contributors
 * Based on OpenConnect (LGPL-2.1-or-later).
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "openconnect-internal.h"

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
	char *buf, *tok, *save = NULL;

	if (!csv || !*csv)
		return;
	buf = strdup(csv);
	if (!buf)
		return;
	for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save))
		append_dst_domain(head, tok);
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
