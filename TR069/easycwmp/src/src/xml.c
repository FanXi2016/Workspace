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
 *	Copyright (C) 2012 Jonas Gorski <jogo@openwrt.org>
 */

#include <stdbool.h>
#include <libubox/uloop.h>
#include <microxml.h>

#include "xml.h"
#include "backup.h"
#include "config.h"
#include "cwmp.h"
#include "external.h"
#include "easycwmp.h"
#include "messages.h"
#include "time.h"
#include "json.h"
#include "log.h"

struct fault_code fault_array[]=
{
	[FAULT_0]	 = {"0", "", ""},
	[FAULT_9000] = {"9000", "Server", "Method not supported"},
	[FAULT_9001] = {"9001", "Server", "Request denied"},
	[FAULT_9002] = {"9002", "Server", "Internal error"},
	[FAULT_9003] = {"9003", "Client", "Invalid arguments"},
	[FAULT_9004] = {"9004", "Server", "Resources exceeded"},
	[FAULT_9005] = {"9005", "Client", "Invalid parameter name"},
	[FAULT_9006] = {"9006", "Client", "Invalid parameter type"},
	[FAULT_9007] = {"9007", "Client", "Invalid parameter value"},
	[FAULT_9008] = {"9008", "Client", "Attempt to set a non-writable parameter"},
	[FAULT_9009] = {"9009", "Server", "Notification request rejected"},
	[FAULT_9010] = {"9010", "Server", "Download failure"},
	[FAULT_9011] = {"9011", "Server", "Upload failure"},
	[FAULT_9012] = {"9012", "Server", "File transfer server authentication failure"},
	[FAULT_9013] = {"9013", "Server", "Unsupported protocol for file transfer"},
	[FAULT_9014] = {"9014", "Server", "Download failure: unable to join multicast group"},
	[FAULT_9015] = {"9015", "Server", "Download failure: unable to contact file server"},
	[FAULT_9016] = {"9016", "Server", "Download failure: unable to access file"},
	[FAULT_9017] = {"9017", "Server", "Download failure: unable to complete download"},
	[FAULT_9018] = {"9018", "Server", "Download failure: file corrupted"},
	[FAULT_9019] = {"9019", "Server", "Download failure: file authentication failure"}
};

const static char *soap_env_url = "http://schemas.xmlsoap.org/soap/envelope/";
const static char *soap_enc_url = "http://schemas.xmlsoap.org/soap/encoding/";
const static char *xsd_url = "http://www.w3.org/2001/XMLSchema";
const static char *xsi_url = "http://www.w3.org/2001/XMLSchema-instance";
const static char *cwmp_urls[] = {
		"urn:dslforum-org:cwmp-1-0", 
		"urn:dslforum-org:cwmp-1-1", 
		"urn:dslforum-org:cwmp-1-2", 
		NULL };

static struct cwmp_namespaces ns;

const struct rpc_method rpc_methods[] = {
	{ "GetRPCMethods", xml_handle_get_rpc_methods },
	{ "SetParameterValues", xml_handle_set_parameter_values },
	{ "GetParameterValues", xml_handle_get_parameter_values },
	{ "GetParameterNames", xml_handle_get_parameter_names },
	{ "GetParameterAttributes", xml_handle_get_parameter_attributes },
	{ "SetParameterAttributes", xml_handle_set_parameter_attributes },
	{ "AddObject", xml_handle_AddObject },
	{ "DeleteObject", xml_handle_DeleteObject },
	{ "Download", xml_handle_download },
	{ "Reboot", xml_handle_reboot },
	{ "FactoryReset", xml_handle_factory_reset },
	{ "ScheduleInform", xml_handle_schedule_inform },
	//Added by Tian Yiqing, Implement upload rpc method, Start
	{"Upload", xml_handle_upload}
	//Added by Tian Yiqing, Implement upload rpc method, End
};

int xml_cut_LF_from_out_msg_head(char *msg_out)
{
    int i = 0, len = 0;

    if(msg_out == NULL || msg_out[0] == '\0')
    	return -1;
    for(i=0; i<strlen(msg_out); i++)
    {
        if('\n' == msg_out[i])
            msg_out[i] = ' ';
     }
     
     return 0;
}

const char *xml_format_cb(mxml_node_t *node, int pos)
{
	mxml_node_t *b = node;
	static char space_format[20];
	int i=0;

	switch (pos) {
		case  MXML_WS_BEFORE_CLOSE:
			if (node->child && node->child->type!=MXML_ELEMENT)
				return ("");
		case  MXML_WS_BEFORE_OPEN:
			while (b->parent != NULL) {
				space_format[i] = ' ';
				b=b->parent;
				i++;
			}
			space_format[i] = '\0';
			return (space_format);
		case  MXML_WS_AFTER_OPEN:
			if (node->child && node->child->type!=MXML_ELEMENT)
				return ("");
			else
				return ("\n");
		case  MXML_WS_AFTER_CLOSE:
			return ("\n");
		default:
			return ("");
	}
}

char *xml_get_value_with_whitespace(mxml_node_t *b)
{
	mxml_node_t *y = b->parent;
	char * value = calloc(1, sizeof(char));
	do {
		value = realloc(value, strlen(value) + strlen(b->value.text.string) + 2);
		if (value[0] != '\0')
			strcat(value, " ");

		strcat(value, b->value.text.string);
	 } while ((b = mxmlWalkNext(b, y, MXML_DESCEND)) &&
			(b->value.text.whitespace == true) &&
			(b->type == MXML_TEXT && b->value.text.string));
	return value;
}

void xml_exit(void)
{
	FREE(ns.soap_env);
	FREE(ns.soap_enc);
	FREE(ns.xsd);
	FREE(ns.xsi);
	FREE(ns.cwmp);
}

// add by Zhao zijun start
int xml_check_combination_nodes(mxml_node_t *tree)
{
	int count_min, count_max;
	int limit = 0;
	int min_flag = 0, max_flag = 0;
	int ips1, ips2, ips3, ips4;
	int ipe1, ipe2, ipe3, ipe4;
	char min_value[64] = {0};
	char max_value[64] = {0};
	char *parameter_name = NULL;
	char *parameter_value = NULL;
	mxml_node_t *n = tree;

	while (n)
	{
		if (n && n->type == MXML_TEXT &&
			n->value.text.string &&
			n->parent->type == MXML_ELEMENT &&
			!strcmp(n->parent->value.element.name, "Name"))
		{
			parameter_name = n->value.text.string;
		}

		if (n && n->type == MXML_ELEMENT &&
			!strcmp(n->value.element.name, "Name") &&
			!n->child) {
			parameter_name = "";
		}
		if (n && n->type == MXML_TEXT &&
			n->value.text.string &&
			n->parent->type == MXML_ELEMENT &&
			!strcmp(n->parent->value.element.name, "Value")) {
			free(parameter_value);
			parameter_value = xml_get_value_with_whitespace(n);
		}

		if (n && n->type == MXML_ELEMENT &&
			!strcmp(n->value.element.name, "Value") &&
			!n->child) {
			free(parameter_value);
			parameter_value = strdup("");
		}

		if (parameter_name && parameter_value)
		{
			if (0 == strcmp(parameter_name, "InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.MinAddress"))
			{
				strncpy(min_value, parameter_value, 63);
				min_flag = 1;
			}
			else if (0 == strcmp(parameter_name, "InternetGatewayDevice.LANDevice.1.LANHostConfigManagement.MaxAddress"))
			{
				strncpy(max_value, parameter_value, 63);
				max_flag = 1;
			}
			parameter_name = NULL;
			FREE(parameter_value);
		}
		n = mxmlWalkNext(n, tree, MXML_DESCEND);
	}

	if (min_flag && max_flag)
	{
		char command[128] = {0};

		count_min = sscanf(min_value, "%d.%d.%d.%d", &ips1, &ips2, &ips3, &ips4 );
		count_max = sscanf(max_value, "%d.%d.%d.%d", &ipe1, &ipe2, &ipe3, &ipe4 );
		if (4 != count_min || 4 != count_max)
			return 1;
		else
		{
			if (ips1 != ipe1 || ips2 != ipe2 || ips3 != ipe3)
				return 1;
			if (ips1 >= 255 || ips1 <= 0 || ipe1 >= 255 || ipe1 <= 0)
				return 1;
			if (ips2 > 255 || ips2 < 0 || ipe2 > 255 || ipe2 < 0)
				return 1;
			if (ips3 > 255 || ips3 < 0 || ipe3 > 255 || ipe3 < 0)
				return 1;
			if (ips4 >= 255 || ips4 <= 0 || ipe4 >= 255 || ipe4 <= 0)
				return 1;
		}

		if (ips4 > ipe4)
			return 1;

		limit = ipe4 - ips4 + 1;
		sprintf(command, "uci set dhcp.lan.start=%d", ips4);
		system(command);

		sprintf(command, "uci set dhcp.lan.limit=%d", limit);
		system(command);

		system("uci commit dhcp");
	}

	return 0;

}
// add by Zhao zijun end

int xml_check_duplicated_parameter(mxml_node_t *tree)
{
	mxml_node_t *b, *n = tree;
	while (n) {
		if (n && n->type == MXML_TEXT &&
			n->value.text.string &&
			n->parent->type == MXML_ELEMENT &&
			!strcmp(n->parent->value.element.name, "Name")) {
			b = n;
			while (b = mxmlWalkNext(b, tree, MXML_DESCEND)) {
				if (b && b->type == MXML_TEXT &&
					b->value.text.string &&
					b->parent->type == MXML_ELEMENT &&
					!strcmp(b->parent->value.element.name, "Name")) {
					if (strcmp(b->value.text.string, n->value.text.string) == 0) return 1;
				}
			}
		}
		n = mxmlWalkNext(n, tree, MXML_DESCEND);
	}
	return 0;
}

static int xml_recreate_namespace(mxml_node_t *tree)
{
	mxml_node_t *b = tree;
	const char *cwmp_urn;
	char *c;
	int i;

	do {
		FREE(ns.soap_env);
		FREE(ns.soap_enc);
		FREE(ns.xsd);
		FREE(ns.xsi);
		FREE(ns.cwmp);

		c = (char *) mxmlElementGetAttrName(b, soap_env_url);
		if (!c)
			continue;
		if (*(c + 5) == ':') {
			ns.soap_env = strdup((c + 6));
		} else {
			continue;
		}

		c = (char *) mxmlElementGetAttrName(b, soap_enc_url);
		if (!c)
			continue;
		if (*(c + 5) == ':') {
			ns.soap_enc = strdup((c + 6));
		} else {
			continue;
		}

		c = (char *) mxmlElementGetAttrName(b, xsd_url);
		if (!c)
			continue;
		if (*(c + 5) == ':') {
			ns.xsd = strdup((c + 6));
		} else {
			continue;
		}

		c = (char *) mxmlElementGetAttrName(b, xsi_url);
		if (!c)
			continue;
		if (*(c + 5) == ':') {
			ns.xsi = strdup((c + 6));
		} else {
			continue;
		}

		for (i = 0; cwmp_urls[i] != NULL; i++) {
			cwmp_urn = cwmp_urls[i];
			c = (char *) mxmlElementGetAttrName(b, cwmp_urn);
			if (c && *(c + 5) == ':') {
				ns.cwmp = strdup((c + 6));
				break;
			}
		}

		if (!ns.cwmp) return -1;
		return 0;
	} while (b = mxmlWalkNext(b, tree, MXML_DESCEND));

	return -1;
}

static void xml_get_hold_request(mxml_node_t *tree)
{
	mxml_node_t *b;
	char *c;

	cwmp->hold_requests = false;

	if (asprintf(&c, "%s:%s", ns.cwmp, "NoMoreRequests") == -1)
		return;
	b = mxmlFindElement(tree, tree, c, NULL, NULL, MXML_DESCEND);
	free(c);
	if (b) {
		b = mxmlWalkNext(b, tree, MXML_DESCEND_FIRST);

		if(b->value.text.string)
			cwmp->hold_requests = (atoi(b->value.text.string)) ? true : false;
	}

	if (asprintf(&c, "%s:%s", ns.cwmp, "HoldRequests") == -1)
		return;
	b = mxmlFindElement(tree, tree, c, NULL, NULL, MXML_DESCEND);
	free(c);
	if (b) {
		b = mxmlWalkNext(b, tree, MXML_DESCEND_FIRST);

		if(b->value.text.string)
			cwmp->hold_requests = (atoi(b->value.text.string)) ? true : false;
	}
}

int xml_handle_message(char *msg_in, char **msg_out)
{
	mxml_node_t *tree_in, *tree_out = NULL, *b, *body_out;
	const struct rpc_method *method;
	int i, code = FAULT_9002;
	char *c;

	tree_out = mxmlLoadString(NULL, CWMP_RESPONSE_MESSAGE, MXML_NO_CALLBACK);
	if (!tree_out) goto error;

	tree_in = mxmlLoadString(NULL, msg_in, MXML_NO_CALLBACK);
	if (!tree_in) goto error;

	if(xml_recreate_namespace(tree_in)) {
		code = FAULT_9003;
		goto fault_out;
	}
	/* handle cwmp:ID */
	if (asprintf(&c, "%s:%s", ns.cwmp, "ID") == -1)
		goto error;

	b = mxmlFindElement(tree_in, tree_in, c, NULL, NULL, MXML_DESCEND);
	FREE(c);
	/* ACS did not send ID parameter, we are continuing without it */
	if (!b) goto find_method;

	b = mxmlWalkNext(b, tree_in, MXML_DESCEND_FIRST);
	if (!b || !b->value.text.string) goto find_method;
	c = strdup(b->value.text.string);

	b = mxmlFindElement(tree_out, tree_out, "cwmp:ID", NULL, NULL, MXML_DESCEND);
	if (!b) goto error;

	b = mxmlNewText(b, 0, c);
	FREE(c);
	if (!b) goto error;

find_method:
	if (asprintf(&c, "%s:%s", ns.soap_env, "Body") == -1)
		goto error;

	b = mxmlFindElement(tree_in, tree_in, c, NULL, NULL, MXML_DESCEND);
	FREE(c);
	if (!b) {
		code = FAULT_9003;
		goto fault_out;
	}
	while (1) {
		b = mxmlWalkNext(b, tree_in, MXML_DESCEND_FIRST);
		if (!b) {
			code = FAULT_9003;
			goto fault_out;
		}
		if (b->type == MXML_ELEMENT) break;
	}

	c = b->value.element.name;
	if (strchr(c, ':')) {
		char *tmp = strchr(c, ':');
		size_t ns_len = tmp - c;

		if (strlen(ns.cwmp) != ns_len) {
			code = FAULT_9003;
			goto fault_out;
		}

		if (strncmp(ns.cwmp, c, ns_len)) {
			code = FAULT_9003;
			goto fault_out;
		}

		c = tmp + 1;
	} else {
		code = FAULT_9003;
		goto fault_out;
	}
	method = NULL;
	log_message(NAME, L_NOTICE, "received %s method from the ACS\n", c);
	for (i = 0; i < ARRAY_SIZE(rpc_methods); i++) {
		if (!strcmp(c, rpc_methods[i].name)) {
			method = &rpc_methods[i];
			break;
		}
	}
	if (method) {
		if (method->handler(b, tree_in, tree_out)) goto error;
	}
	else {
		code = FAULT_9000;
		goto fault_out;
	}
	//*msg_out = mxmlSaveAllocString(tree_out, xml_format_cb);
	*msg_out = mxmlSaveAllocString(tree_out, NULL);
	xml_cut_LF_from_out_msg_head(*msg_out);

	mxmlDelete(tree_in);
	mxmlDelete(tree_out);
	return 0;

fault_out:
	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
	if (!body_out) goto error;
	xml_create_generic_fault_message(body_out, code);
	mxmlDelete(tree_in);
	mxmlDelete(tree_out);
	return 0;

error:
	mxmlDelete(tree_in);
	mxmlDelete(tree_out);
	return -1;
}

int xml_get_index_fault(char *fault_code)
{
	int i;

	for (i = 0; i < __FAULT_MAX; i++) {
		if (strcmp(fault_array[i].code, fault_code) == 0)
			return i;
	}
	return FAULT_9002;
}

/* Inform */

static int xml_prepare_events_inform(mxml_node_t *tree)
{
	mxml_node_t *node, *b1, *b2;
	char *c;
	int n = 0;
	struct list_head *p;
	struct event *event;

	b1 = mxmlFindElement(tree, tree, "Event", NULL, NULL, MXML_DESCEND);
	if (!b1) return -1;

	list_for_each(p, &cwmp->events) {
		event = list_entry (p, struct event, list);
		node = mxmlNewElement (b1, "EventStruct");
		if (!node) goto error;

		b2 = mxmlNewElement (node, "EventCode");
		if (!b2) goto error;

		b2 = mxmlNewText(b2, 0, event_code_array[event->code].code);
		if (!b2) goto error;

		b2 = mxmlNewElement (node, "CommandKey");
		if (!b2) goto error;

		if (event->key) {
			b2 = mxmlNewText(b2, 0, event->key);
			if (!b2) goto error;
		}

		mxmlAdd(b1, MXML_ADD_AFTER, MXML_ADD_TO_PARENT, node);
		n++;
	}

	if (n) {
		if (asprintf(&c, "cwmp:EventStruct[%u]", n) == -1)
			return -1;

		mxmlElementSetAttr(b1, "soap_enc:arrayType", c);
		FREE(c);
	}

	return 0;

error:
	return -1;
}

static int xml_prepare_notifications_inform(mxml_node_t *parameter_list, int *counter)
{
	/* notifications */
	mxml_node_t *b, *n;

	struct list_head *p;
	struct notification *notification;

	list_for_each(p, &cwmp->notifications) {
		notification = list_entry(p, struct notification, list);

		b = mxmlFindElementText(parameter_list, parameter_list, notification->parameter, MXML_DESCEND);
		if (b) continue;
		
		n = mxmlNewElement(parameter_list, "ParameterValueStruct");
		if (!n) goto error;

		b = mxmlNewElement(n, "Name");
		if (!b) goto error;

		b = mxmlNewText(b, 0, notification->parameter);
		if (!b) goto error;

		b = b->parent->parent;
		b = mxmlNewElement(n, "Value");
		if (!b) goto error;

		b = mxmlNewText(b, 0, notification->value);
		if (!b) goto error;

		(*counter)++;
	}

	return 0;

error:
	return -1;
}

int xml_prepare_inform_message(char **msg_out)
{
	mxml_node_t *tree, *b, *n, *parameter_list;
	struct external_parameter *external_parameter;
	char *c;
	int counter = 0;

	//Added by Tian Yiqing, Implement cwmp ID in the inform request. Start
	static int id_counter = 2;
	//Added by Tian Yiqing, Implement cwmp ID in the inform request. End

	tree = mxmlLoadString(NULL, CWMP_INFORM_MESSAGE, MXML_NO_CALLBACK);

	if (!tree) goto error;

	//Added by Tian Yiqing, Implement cwmp ID in the inform request. Start
	b = mxmlFindElement(tree, tree, "cwmp:ID", NULL, NULL, MXML_DESCEND);
	if (!b) goto error;

	b = mxmlNewInteger(b, id_counter);
	if(!b) goto error;
	//Added by Tian Yiqing, Implement cwmp ID in the inform request. End

	b = mxmlFindElement(tree, tree, "RetryCount", NULL, NULL, MXML_DESCEND);
	if (!b) goto error;

	b = mxmlNewInteger(b, cwmp->retry_count);
	if (!b) goto error;

	b = mxmlFindElement(tree, tree, "Manufacturer", NULL, NULL, MXML_DESCEND);
	if (!b) goto error;

	b = mxmlNewText(b, 0, cwmp->deviceid.manufacturer);
	if (!b) goto error;

	b = mxmlFindElement(tree, tree, "OUI", NULL, NULL, MXML_DESCEND);
	if (!b) goto error;

	b = mxmlNewText(b, 0, cwmp->deviceid.oui);
	if (!b) goto error;

	b = mxmlFindElement(tree, tree, "ProductClass", NULL, NULL, MXML_DESCEND);
	if (!b) goto error;

	b = mxmlNewText(b, 0, cwmp->deviceid.product_class);
	if (!b) goto error;

	b = mxmlFindElement(tree, tree, "SerialNumber", NULL, NULL, MXML_DESCEND);
	if (!b) goto error;

	b = mxmlNewText(b, 0, cwmp->deviceid.serial_number);
	if (!b) goto error;
   
	if (xml_prepare_events_inform(tree))
		goto error;

	b = mxmlFindElement(tree, tree, "CurrentTime", NULL, NULL, MXML_DESCEND);
	if (!b) goto error;

	b = mxmlNewText(b, 0, mix_get_time());
	if (!b) goto error;

	external_action_simple_execute("inform", "parameter", NULL);
	if (external_action_handle(json_handle_get_parameter_value))
		goto error;

	parameter_list = mxmlFindElement(tree, tree, "ParameterList", NULL, NULL, MXML_DESCEND);
	if (!parameter_list) goto error;

	while (external_list_parameter.next != &external_list_parameter) {

		external_parameter = list_entry(external_list_parameter.next, struct external_parameter, list);

		n = mxmlNewElement(parameter_list, "ParameterValueStruct");
		if (!n) goto error;

		b = mxmlNewElement(n, "Name");
		if (!b) goto error;

		b = mxmlNewText(b, 0, external_parameter->name);
		if (!b) goto error;

		b = mxmlNewElement(n, "Value");
		if (!b) goto error;

#ifdef ACS_MULTI
		//Updated by Tian Yiqing, the data type attribute should be added in Value tag, Start
		mxmlElementSetAttr(b, "xsi:type", external_parameter->type);
		//Updated by Tian Yiqing, the data type attribute should be added in Value tag, End
#endif
		b = mxmlNewText(b, 0, external_parameter->data ? external_parameter->data : "");
		if (!b) goto error;

		counter++;

		external_parameter_delete(external_parameter);
	}

	if (xml_prepare_notifications_inform(parameter_list, &counter))
		goto error;

#ifdef ACS_MULTI
	if (asprintf(&c, "cwmp:ParameterValueStruct[%d]", counter) == -1)
		goto error;

	mxmlElementSetAttr(parameter_list, "soap_enc:arrayType", c);
	FREE(c);
#endif

	//*msg_out = mxmlSaveAllocString(tree, xml_format_cb);
	*msg_out = mxmlSaveAllocString(tree, NULL);
	xml_cut_LF_from_out_msg_head(*msg_out);
	
	mxmlDelete(tree);

	//Added by Tian Yiqing, Implement cwmp ID in the inform request. Start
	++id_counter;
	//Added by Tian Yiqing, Implement cwmp ID in the inform request. End

	return 0;

error:
	external_free_list_parameter();
	mxmlDelete(tree);
	return -1;
}

int xml_parse_inform_response_message(char *msg_in)
{
	mxml_node_t *tree, *b;
	char *c;
	int fault = 0;

	tree = mxmlLoadString(NULL, msg_in, MXML_NO_CALLBACK);
	if (!tree) goto error;
	if(xml_recreate_namespace(tree)) goto error;

	if (asprintf(&c, "%s:%s", ns.soap_env, "Fault") == -1)
		goto error;
	b = mxmlFindElement(tree, tree, c, NULL, NULL, MXML_DESCEND);
	FREE(c);
	if (b) {
		b = mxmlFindElementText(b, b, "8005", MXML_DESCEND);
		if (b) {
			fault = FAULT_ACS_8005;
			goto out;
		}
		goto error;
	}

	xml_get_hold_request(tree);
	b = mxmlFindElement(tree, tree, "MaxEnvelopes", NULL, NULL, MXML_DESCEND);
	if (!b) goto error;

	b = mxmlWalkNext(b, tree, MXML_DESCEND_FIRST);
	if (!b || !b->value.text.string)
		goto error;


out:
	mxmlDelete(tree);
	return fault;

error:
	mxmlDelete(tree);
	return -1;
}

/* ACS GetRPCMethods */
int xml_prepare_get_rpc_methods_message(char **msg_out)
{
	mxml_node_t *tree;

	tree = mxmlLoadString(NULL, CWMP_GET_RPC_METHOD_MESSAGE, MXML_NO_CALLBACK);

	if (!tree) return -1;
	*msg_out = mxmlSaveAllocString(tree, NULL);
	xml_cut_LF_from_out_msg_head(*msg_out);
	mxmlDelete(tree);
	return 0;
}

int xml_parse_get_rpc_methods_response_message(char *msg_in)
{
	mxml_node_t *tree, *b;
	char *c;
	int fault = 0;

	tree = mxmlLoadString(NULL, msg_in, MXML_NO_CALLBACK);
	if (!tree) goto error;
	if(xml_recreate_namespace(tree)) goto error;

	if (asprintf(&c, "%s:%s", ns.soap_env, "Fault") == -1)
		goto error;

	b = mxmlFindElement(tree, tree, c, NULL, NULL, MXML_DESCEND);
	FREE(c);
	if (b) {
		b = mxmlFindElementText(b, b, "8005", MXML_DESCEND);
		if (b) {
			fault = FAULT_ACS_8005;
			goto out;
		}
		goto out;
	}

	xml_get_hold_request(tree);

out:
	mxmlDelete(tree);
	return fault;

error:
	mxmlDelete(tree);
	return -1;
}

/* ACS TransferComplete */

int xml_parse_transfer_complete_response_message(char *msg_in)
{
	mxml_node_t *tree, *b;
	char *c;
	int fault = 0;

	tree = mxmlLoadString(NULL, msg_in, MXML_NO_CALLBACK);
	if (!tree) goto error;
	if(xml_recreate_namespace(tree)) goto error;

	if (asprintf(&c, "%s:%s", ns.soap_env, "Fault") == -1)
		goto error;

	b = mxmlFindElement(tree, tree, c, NULL, NULL, MXML_DESCEND);
	FREE(c);
	if (b) {
		b = mxmlFindElementText(b, b, "8005", MXML_DESCEND);
		if (b) {
			fault = FAULT_ACS_8005;
			goto out;
		}
		goto out;
	}

	xml_get_hold_request(tree);

out:
	mxmlDelete(tree);
	return fault;

error:
	mxmlDelete(tree);
	return -1;
}

/* CPE GetRPCMethods */

static int xml_handle_get_rpc_methods(mxml_node_t *body_in,
					mxml_node_t *tree_in,
					mxml_node_t *tree_out)
{
		mxml_node_t *b1, *b2, *method_list;
		int i = 0;

		b1 = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
		if (!b1) return -1;

		b1 = mxmlNewElement(b1, "cwmp:GetRPCMethodsResponse");
		if (!b1) return -1;

		method_list = mxmlNewElement(b1, "MethodList");
		if (!method_list) return -1;

		for (i = 0; i < ARRAY_SIZE(rpc_methods); i++) {
			b2 = mxmlNewElement(method_list, "string");
			if (!b2) return -1;

			b2 = mxmlNewText(b2, 0, rpc_methods[i].name);
			if (!b2) return -1;
		}
		char *attr_value;
		if (asprintf(&attr_value, "xsd:string[%d]", ARRAY_SIZE(rpc_methods)) == -1)
			return -1;

		mxmlElementSetAttr(method_list, "soap_enc:arrayType", attr_value);
		free(attr_value);

		log_message(NAME, L_NOTICE, "send GetRPCMethodsResponse to the ACS\n");
		return 0;
}

/* SetParameterValues */

int xml_handle_set_parameter_values(mxml_node_t *body_in,
					mxml_node_t *tree_in,
					mxml_node_t *tree_out)
{
	mxml_node_t *b = body_in, *body_out;
	struct external_parameter *external_parameter;
	struct list_head *ilist;
	char *parameter_name = NULL, *parameter_value = NULL, *status = NULL, *param_key = NULL;
	int code = FAULT_9002;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
	if (!body_out) goto error;

	if (xml_check_duplicated_parameter(body_in)) {
		code = FAULT_9003;
		goto fault_out;
	}
// add by Zhao Zijun Start
	if (xml_check_combination_nodes(body_in))
	{
		code = FAULT_9007;
		goto fault_out;
	}
// add by Zhao Zijun End
	while (b) {
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "Name")) {
			parameter_name = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "Name") &&
			!b->child) {
			parameter_name = "";
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "Value")) {
			free(parameter_value);
			parameter_value = xml_get_value_with_whitespace(b);
		}

		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "Value") &&
			!b->child) {
			free(parameter_value);
			parameter_value = strdup("");
		}

		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "ParameterKey")) {
			param_key = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "ParameterKey") &&
			!b->child) {
			param_key = "";
		}

		if (parameter_name && parameter_value) {
			external_action_parameter_execute("set", "value", parameter_name, parameter_value);
			parameter_name = NULL;
			FREE(parameter_value);
		}
		b = mxmlWalkNext(b, body_in, MXML_DESCEND);
	}

	external_action_simple_execute("apply", "value", param_key);

	if (external_action_handle(json_handle_set_parameter_value))
		goto fault_out;

	list_for_each(ilist, &external_list_parameter) {
		external_parameter = list_entry(ilist, struct external_parameter, list);
		if (external_parameter->fault_code && external_parameter->fault_code[0]=='9') {
			code = FAULT_9003;
			goto fault_out;
		}
	}
	external_fetch_set_param_val_resp_status(&status);
	if(!status)
		goto fault_out;

	b = mxmlNewElement(body_out, "cwmp:SetParameterValuesResponse");
	if (!b) goto error;

	b = mxmlNewElement(b, "Status");
	if (!b) goto error;

	b = mxmlNewText(b, 0, status);
	if (!b) goto error;

	free(status);
	free(parameter_value);

	log_message(NAME, L_NOTICE, "send SetParameterValuesResponse to the ACS\n");
	return 0;

fault_out:
	free(parameter_value);
	xml_create_set_parameter_value_fault_message(body_out, code);
	free(status);
	external_free_list_parameter();
	return 0;
error:
	free(parameter_value);
	free(status);
	external_free_list_parameter();
	return -1;
}

/* GetParameterValues */

int xml_handle_get_parameter_values(mxml_node_t *body_in,
					mxml_node_t *tree_in,
					mxml_node_t *tree_out)
{
	mxml_node_t *n, *parameter_list, *b = body_in, *body_out, *t;
	struct external_parameter *external_parameter;
	char *parameter_name = NULL;
	int counter = 0, code = FAULT_9002;
	struct list_head *ilist;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body",
				NULL, NULL, MXML_DESCEND);
	if (!body_out) return -1;

	while (b) {
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "string")) {
			parameter_name = b->value.text.string;
		}

		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "string") &&
			!b->child) {
			parameter_name = "";
		}

		if (parameter_name) {
			external_action_parameter_execute("get", "value", parameter_name, NULL);
		}

		b = mxmlWalkNext(b, body_in, MXML_DESCEND);
		parameter_name = NULL;
	}
	if (external_action_handle(json_handle_get_parameter_value))
		goto fault_out;

	list_for_each(ilist, &external_list_parameter) {
		external_parameter = list_entry(ilist, struct external_parameter, list);
		if (external_parameter->fault_code && external_parameter->fault_code[0]=='9') {
			code =	xml_get_index_fault(external_parameter->fault_code);
			goto fault_out;
		}
	}
	n = mxmlNewElement(body_out, "cwmp:GetParameterValuesResponse");
	if (!n) return -1;
	parameter_list = mxmlNewElement(n, "ParameterList");
	if (!parameter_list) return -1;

	while (external_list_parameter.next != &external_list_parameter) {

		external_parameter = list_entry(external_list_parameter.next, struct external_parameter, list);

		n = mxmlNewElement(parameter_list, "ParameterValueStruct");
		if (!n) goto out;

		t = mxmlNewElement(n, "Name");
		if (!t) goto out;

		t = mxmlNewText(t, 0, external_parameter->name);
		if (!t) goto out;

		t = mxmlNewElement(n, "Value");
		if (!t) goto out;

#ifdef ACS_MULTI
		mxmlElementSetAttr(t, "xsi:type", external_parameter->type);
#endif
		t = mxmlNewText(t, 0, external_parameter->data ? external_parameter->data : "");
		if (!t) goto out;

		counter++;
		external_parameter_delete(external_parameter);
	}
#ifdef ACS_MULTI
	char *c;
	if (asprintf(&c, "cwmp:ParameterValueStruct[%d]", counter) == -1)
		goto out;

	mxmlElementSetAttr(parameter_list, "soap_enc:arrayType", c);
	FREE(c);
#endif

	log_message(NAME, L_NOTICE, "send GetParameterValuesResponse to the ACS\n");
	return 0;
fault_out:
	xml_create_generic_fault_message(body_out, code);
	external_free_list_parameter();
	return 0;
out:
	external_free_list_parameter();
	return -1;
}

/* GetParameterNames */

int xml_handle_get_parameter_names(mxml_node_t *body_in,
					mxml_node_t *tree_in,
					mxml_node_t *tree_out)
{
	mxml_node_t *n, *parameter_list, *b = body_in, *body_out, *t;
	struct external_parameter *external_parameter;
	char *parameter_name = NULL;
	char *next_level = NULL;
	int counter = 0, code = FAULT_9002;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body",
					NULL, NULL, MXML_DESCEND);
	if (!body_out) return -1;
	while (b) {
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "ParameterPath")) {
			parameter_name = b->value.text.string;
		}

		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "ParameterPath") &&
			!b->child) {
			parameter_name = "";
		}

		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "NextLevel")) {
			next_level = b->value.text.string;
		}

		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "NextLevel") &&
			!b->child) {
			next_level = "";
		}

		b = mxmlWalkNext(b, body_in, MXML_DESCEND);
	}
	if (parameter_name && next_level) {
		external_action_parameter_execute("get", "name", parameter_name, next_level);
		if (external_action_handle(json_handle_get_parameter_name))
			goto fault_out;
	}
	external_parameter = list_entry(external_list_parameter.next, struct external_parameter, list);

	if (external_parameter->fault_code && external_parameter->fault_code[0]=='9') {
		code = xml_get_index_fault(external_parameter->fault_code) ;
		goto fault_out;
	}

	n = mxmlNewElement(body_out, "cwmp:GetParameterNamesResponse");
	if (!n) return -1;

	parameter_list = mxmlNewElement(n, "ParameterList");
	if (!parameter_list) return -1;

	while (external_list_parameter.next != &external_list_parameter) {
		external_parameter = list_entry(external_list_parameter.next, struct external_parameter, list);

		n = mxmlNewElement(parameter_list, "ParameterInfoStruct");
		if (!n) goto out;

		t = mxmlNewElement(n, "Name");
		if (!t) goto out;

		t = mxmlNewText(t, 0, external_parameter->name);
		if (!t) goto out;

		t = mxmlNewElement(n, "Writable");
		if (!t) goto out;

		t = mxmlNewText(t, 0, external_parameter->data);
		if (!t) goto out;

		counter++;

		external_parameter_delete(external_parameter);
	}

#ifdef ACS_MULTI
	char *c;
	if (asprintf(&c, "cwmp:ParameterInfoStruct[%d]", counter) == -1)
		goto out;

	mxmlElementSetAttr(parameter_list, "soap_enc:arrayType", c);
	FREE(c);
#endif

	log_message(NAME, L_NOTICE, "send GetParameterNamesResponse to the ACS\n");
	return 0;
fault_out:
	xml_create_generic_fault_message(body_out, code);
	external_free_list_parameter();
	return 0;

out:
	external_free_list_parameter();
	return -1;
}

/* GetParameterAttributes */

static int xml_handle_get_parameter_attributes(mxml_node_t *body_in,
					mxml_node_t *tree_in,
					mxml_node_t *tree_out)
{
	mxml_node_t *n, *parameter_list, *b = body_in, *body_out, *t;
	struct external_parameter *external_parameter;
	char *parameter_name = NULL;
	int counter = 0, code = FAULT_9002;
	struct list_head *ilist;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body",
				NULL, NULL, MXML_DESCEND);
	if (!body_out) return -1;

	while (b) {
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "string")) {
			parameter_name = b->value.text.string;
		}

		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "string") &&
			!b->child) {
			parameter_name = "";
		}
		if (parameter_name) {
			external_action_parameter_execute("get", "notification", parameter_name, NULL);
		}
		b = mxmlWalkNext(b, body_in, MXML_DESCEND);
		parameter_name = NULL;
	}

	if (external_action_handle(json_handle_get_parameter_attribute))
		goto fault_out;
	list_for_each(ilist, &external_list_parameter) {
		external_parameter = list_entry(ilist, struct external_parameter, list);
		if (external_parameter->fault_code && external_parameter->fault_code[0]=='9') {
			code =	xml_get_index_fault(external_parameter->fault_code);
			goto fault_out;
		}
	}
	n = mxmlNewElement(body_out, "cwmp:GetParameterAttributesResponse");
	if (!n) return -1;

	parameter_list = mxmlNewElement(n, "ParameterList");
	if (!parameter_list) return -1;

	while (external_list_parameter.next != &external_list_parameter) {

		external_parameter = list_entry(external_list_parameter.next, struct external_parameter, list);

		n = mxmlNewElement(parameter_list, "ParameterAttributeStruct");
			if (!n) goto out;

		t = mxmlNewElement(n, "Name");
		if (!t) goto out;

		t = mxmlNewText(t, 0, external_parameter->name);
		if (!t) goto out;

		t = mxmlNewElement(n, "Notification");
		if (!t) goto out;
		t = mxmlNewText(t, 0, external_parameter->data ? external_parameter->data : "");
		if (!t) goto out;

		t = mxmlNewElement(n, "AccessList");
		if (!t) goto out;

		counter++;

		external_parameter_delete(external_parameter);
	}
#ifdef ACS_MULTI
	char *c;
	if (asprintf(&c, "cwmp:ParameterAttributeStruct[%d]", counter) == -1)
		goto out;

	mxmlElementSetAttr(parameter_list, "soap_enc:arrayType", c);
	FREE(c);
#endif

	log_message(NAME, L_NOTICE, "send GetParameterAttributesResponse to the ACS\n");
	return 0;
fault_out:
	xml_create_generic_fault_message(body_out, code);
	external_free_list_parameter();
	return 0;
out:
	external_free_list_parameter();
	return -1;
}

/* SetParameterAttributes */

static int xml_handle_set_parameter_attributes(mxml_node_t *body_in,
						   mxml_node_t *tree_in,
						   mxml_node_t *tree_out) {

	mxml_node_t *b = body_in, *body_out;
	char *c, *parameter_name, *parameter_notification, *success = NULL, *fault = NULL;
	uint8_t attr_notification_update = 0;
	int code = FAULT_9002 ;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
	if (!body_out) goto error;

	while (b != NULL) {
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "SetParameterAttributesStruct")) {
			attr_notification_update = 0;
			parameter_name = NULL;
			parameter_notification = NULL;
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "Name")) {
			parameter_name = b->value.text.string;
		}

		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "Name") &&
			!b->child) {
			parameter_name = "";
		}

		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "NotificationChange")) {
			if (strcasecmp(b->value.text.string, "true") == 0) {
				attr_notification_update = 1;
			} else if (strcasecmp(b->value.text.string, "false") == 0) {
				attr_notification_update = 0;
			} else {
				attr_notification_update = (uint8_t) atoi(b->value.text.string);
			}
		}

		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "Notification")) {
			parameter_notification = b->value.text.string;
		}

		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "Notification") &&
			!b->child) {
			parameter_notification = "";
		}

		if (attr_notification_update && parameter_name && parameter_notification) {
			external_action_parameter_execute("set", "notification", parameter_name, parameter_notification);
			attr_notification_update = 0;
			parameter_name = NULL;
			parameter_notification = NULL;
		}
		b = mxmlWalkNext(b, body_in, MXML_DESCEND);
	}

	external_action_simple_execute("apply", "notification", NULL);

	if (external_action_handle(json_handle_method_status))
		goto fault_out;


	external_fetch_method_resp_status(&success, &fault);

	if (fault && fault[0]=='9') {
		code = xml_get_index_fault(fault);
		goto fault_out;
	}

	if(!success || success[0] == '\0') {
		goto fault_out;
	}


	b = mxmlNewElement(body_out, "cwmp:SetParameterAttributesResponse");
	if (!b) goto error;

	free(success);
	free(fault);

	log_message(NAME, L_NOTICE, "send SetParameterAttributesResponse to the ACS\n");
	return 0;

fault_out:
	xml_create_generic_fault_message(body_out, code);
	free(success);
	free(fault);
	return 0;
error:
	free(success);
	free(fault);
	return -1;
}

/* Download */

static int xml_handle_download(mxml_node_t *body_in,
				   mxml_node_t *tree_in,
				   mxml_node_t *tree_out)
{
	mxml_node_t *n, *t, *b = body_in, *body_out;
	char *download_url = NULL, *file_size = NULL,
		*command_key = NULL, *file_type = NULL, *username = NULL,
		*password = NULL, r;
	int delay = -1, code = FAULT_9002;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
	if (!body_out) return -1;

	while (b != NULL) {
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "CommandKey")) {
			command_key = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "CommandKey") &&
			!b->child) {
			command_key = "";
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "FileType")) {
			file_type = xml_get_value_with_whitespace(b);
			b = b->parent->last_child;
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "URL")) {
			download_url = b->value.text.string;
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "Username")) {
			username = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "Username") &&
			!b->child) {
			username = "";
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "Password")) {
			password = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "Password") &&
			!b->child) {
			password = "";
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "FileSize")) {
			file_size = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "FileSize") &&
			!b->child) {
			file_size = "0";
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "DelaySeconds")) {
			delay = atoi(b->value.text.string);
		}
		b = mxmlWalkNext(b, body_in, MXML_DESCEND);
	}
	if (!download_url || !file_size || !command_key || !file_type || !username || !password || delay < 0) {
		code = FAULT_9003;
		goto fault_out;
	}
	if (sscanf(download_url,"%*[a-zA-Z_0-9]://%c",&r) < 1 ||
		sscanf(download_url,"%*[^:]://%*[^:]:%*[^@]@%c",&r) == 1) {
		code = FAULT_9003;
		goto fault_out;
	}
	if (cwmp->download_count >= MAX_DOWNLOAD) {
		code = FAULT_9004;
		goto fault_out;
	}
	n = backup_add_download(command_key, delay, file_size, download_url, file_type, username, password);
	cwmp_add_download(command_key, delay, file_size, download_url, file_type, username, password, n);
	FREE(file_type);

	t = mxmlNewElement(body_out, "cwmp:DownloadResponse");
	if (!t) return -1;

	b = mxmlNewElement(t, "Status");
	if (!b) return -1;

	b = mxmlNewElement(t, "StartTime");
	if (!b) return -1;

	b = mxmlNewText(b, 0, UNKNOWN_TIME);
	if (!b) return -1;

	b = mxmlFindElement(t, tree_out, "Status", NULL, NULL, MXML_DESCEND);
	if (!b) return -1;

	b = mxmlNewText(b, 0, "1");

	b = mxmlNewElement(t, "CompleteTime");
	if (!b) return -1;

	b = mxmlNewText(b, 0, UNKNOWN_TIME);
	if (!b) return -1;

	log_message(NAME, L_NOTICE, "send DownloadResponse to the ACS\n");
	return 0;

fault_out:
	xml_create_generic_fault_message(body_out, code);
	FREE(file_type);
	return 0;
}

/* FactoryReset */

static int xml_handle_factory_reset(mxml_node_t *node,
					mxml_node_t *tree_in,
					mxml_node_t *tree_out)
{
	mxml_node_t *body_out, *b;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
	if (!body_out) return -1;

	b = mxmlNewElement(body_out, "cwmp:FactoryResetResponse");
	if (!b) return -1;

	cwmp_add_handler_end_session(ENDS_FACTORY_RESET);

	log_message(NAME, L_NOTICE, "send FactoryResetResponse to the ACS\n");
	return 0;
}

 /* Reboot */

static int xml_handle_reboot(mxml_node_t *node,
				 mxml_node_t *tree_in,
				 mxml_node_t *tree_out)
{
	mxml_node_t *b = node, *body_out;
	char *command_key;
	int code = FAULT_9002;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
	if (!body_out) return -1;

	while (b) {
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "CommandKey")) {
			command_key = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "CommandKey") &&
			!b->child) {
			command_key = "";
		}
		b = mxmlWalkNext(b, node, MXML_DESCEND);
	}

	if (!command_key) {
		code = FAULT_9003;
		goto fault_out;
	}

	b = mxmlNewElement(body_out, "cwmp:RebootResponse");
	if (!b) return -1;

	backup_add_event(EVENT_M_REBOOT, command_key, 0);
	cwmp_add_handler_end_session(ENDS_REBOOT);

	log_message(NAME, L_NOTICE, "send RebootResponse to the ACS\n");
	return 0;

fault_out:
	xml_create_generic_fault_message(body_out, code);
	return 0;
}

/* ScheduleInform */

static int xml_handle_schedule_inform(mxml_node_t *body_in,
				 mxml_node_t *tree_in,
				 mxml_node_t *tree_out)
{
	mxml_node_t *b = body_in, *body_out;
	char *command_key = NULL;
	char *delay_seconds = NULL;
	int  delay = 0, code = FAULT_9002;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
	if (!body_out) return -1;

	while (b) {
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "CommandKey")) {
			command_key = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "CommandKey") &&
			!b->child) {
			command_key = "";
		}

		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "DelaySeconds")) {
			delay_seconds = b->value.text.string;
		}
		b = mxmlWalkNext(b, body_in, MXML_DESCEND);
	}
	if (delay_seconds) delay = atoi(delay_seconds);

	if (command_key && (delay > 0)) {
		cwmp_add_scheduled_inform(command_key, delay);
		b = mxmlNewElement(body_out, "cwmp:ScheduleInformResponse");
		if (!b) return -1;
	}
	else {
		code = FAULT_9003;
		goto fault_out;
	}

	log_message(NAME, L_NOTICE, "send ScheduleInformResponse to the ACS\n");
	return 0;
fault_out:
	xml_create_generic_fault_message(body_out, code);
	return 0;
}

/* AddObject */

static int xml_handle_AddObject(mxml_node_t *body_in,
		 mxml_node_t *tree_in,
		 mxml_node_t *tree_out)
{
	mxml_node_t *b = body_in, *t, *body_out;
	char *object_name = NULL, *param_key = NULL;
	char *status = NULL, *fault = NULL, *instance = NULL;
	int code = FAULT_9002;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
	if (!body_out) return -1;

	while (b) {
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "ObjectName")) {
			object_name = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "ObjectName") &&
			!b->child) {
			object_name = "";
		}

		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "ParameterKey")) {
			param_key = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "ParameterKey") &&
			!b->child) {
			param_key = "";
		}
		b = mxmlWalkNext(b, body_in, MXML_DESCEND);
	}

	if (!param_key) {
		code = FAULT_9003;
		goto fault_out;
	}

	if (object_name) {
		external_action_parameter_execute("add", "object", object_name, NULL);
		if (external_action_handle(json_handle_add_object)) goto fault_out;
	} else {
		code = FAULT_9003;
		goto fault_out;
	}

	external_fetch_add_obj_resp(&status, &instance, &fault);

	if (fault && fault[0] == '9') {
		code = xml_get_index_fault(fault);
		goto fault_out;
	}
	if (!status || !instance) {
		code = FAULT_9002;
		goto fault_out;
	}

	external_action_simple_execute("apply", "object", param_key);

	t = mxmlNewElement(body_out, "cwmp:AddObjectResponse");
	if (!t) goto error;

	b = mxmlNewElement(t, "InstanceNumber");
	if (!b) goto error;
	b = mxmlNewText(b, 0, instance);
	if (!b) goto error;

	b = mxmlNewElement(t, "Status");
	if (!b) goto error;
	b = mxmlNewText(b, 0, status);
	if (!b) goto error;

	free(instance);
	free(status);
	free(fault);

	log_message(NAME, L_NOTICE, "send AddObjectResponse to the ACS\n");
	return 0;

fault_out:
	xml_create_generic_fault_message(body_out, code);
	free(instance);
	free(status);
	free(fault);
	return 0;

error:
	free(instance);
	free(status);
	free(fault);
	return -1;
}

/* DeleteObject */

static int xml_handle_DeleteObject(mxml_node_t *body_in,
		 mxml_node_t *tree_in,
		 mxml_node_t *tree_out)
{
	mxml_node_t *b = body_in, *t, *body_out;
	char *object_name = NULL, *param_key = NULL;
	char *status = NULL, *fault = NULL;
	int code = FAULT_9002;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
	if (!body_out) return -1;

	while (b) {
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "ObjectName")) {
			object_name = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "ObjectName") &&
			!b->child) {
			object_name = "";
		}

		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "ParameterKey")) {
			param_key = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "ParameterKey") &&
			!b->child) {
			param_key = "";
		}
		b = mxmlWalkNext(b, body_in, MXML_DESCEND);
	}

	if (!param_key) {
		code = FAULT_9003;
		goto fault_out;
	}

	if (object_name) {
		external_action_parameter_execute("delete", "object", object_name, NULL);
		if (external_action_handle(json_handle_method_status)) goto fault_out;
	} else {
		code = FAULT_9003;
		goto fault_out;
	}

	external_fetch_method_resp_status(&status, &fault);

	if (fault && fault[0] == '9') {
		code = xml_get_index_fault(fault);
		goto fault_out;
	}
	if (!status ) {
		code = FAULT_9002;
		goto fault_out;
	}

	external_action_simple_execute("apply", "object", param_key);

	t = mxmlNewElement(body_out, "cwmp:DeleteObjectResponse");
	if (!t) goto error;

	b = mxmlNewElement(t, "Status");
	if (!b) goto error;
	b = mxmlNewText(b, 0, status);
	if (!b) goto error;
	free(status);
	free(fault);

	log_message(NAME, L_NOTICE, "send DeleteObjectResponse to the ACS\n");
	return 0;

fault_out:
	xml_create_generic_fault_message(body_out, code);
	free(status);
	free(fault);
	return 0;

error:
	free(status);
	free(fault);
	return -1;
}

/* Fault */

mxml_node_t *xml_create_generic_fault_message(mxml_node_t *body, int code)
{
	mxml_node_t *b, *t, *u;

	b = mxmlNewElement(body, "soap_env:Fault");
	if (!b) return NULL;

	t = mxmlNewElement(b, "faultcode");
	if (!t) return NULL;

	u = mxmlNewText(t, 0, fault_array[code].type);
	if (!u) return NULL;

	t = mxmlNewElement(b, "faultstring");
	if (!t) return NULL;

	u = mxmlNewText(t, 0, "CWMP fault");
	if (!u) return NULL;

	b = mxmlNewElement(b, "detail");
	if (!b) return NULL;

	b = mxmlNewElement(b, "cwmp:Fault");
	if (!b) return NULL;

	t = mxmlNewElement(b, "FaultCode");
	if (!t) return NULL;

	u = mxmlNewText(t, 0, fault_array[code].code);
	if (!u) return NULL;

	t = mxmlNewElement(b, "FaultString");
	if (!t) return NULL;

	u = mxmlNewText(t, 0, fault_array[code].string);
	if (!u) return NULL;

	log_message(NAME, L_NOTICE, "send Fault: %s: '%s'\n", fault_array[code].code, fault_array[code].string);
	return b;
}

int xml_create_set_parameter_value_fault_message(mxml_node_t *body, int code)
{
	struct external_parameter *external_parameter;
	mxml_node_t *b, *n, *t;
	int index;

	n = xml_create_generic_fault_message(body, code);
	if (!n)
		return -1;

	while (external_list_parameter.next != &external_list_parameter) {

		external_parameter = list_entry(external_list_parameter.next, struct external_parameter, list);

		if (external_parameter->fault_code && external_parameter->fault_code[0]=='9') {
			index = xml_get_index_fault(external_parameter->fault_code);

			b = mxmlNewElement(n, "SetParameterValuesFault");
			if (!b) return -1;

			t = mxmlNewElement(b, "ParameterName");
			if (!t) return -1;
			t = mxmlNewText(t, 0, external_parameter->name);
			if (!t) return -1;

			t = mxmlNewElement(b, "FaultCode");
			if (!t) return -1;
			t = mxmlNewText(t, 0, external_parameter->fault_code);
			if (!t) return -1;

			t = mxmlNewElement(b, "FaultString");
			if (!t) return -1;
			t = mxmlNewText(t, 0, fault_array[index].string);
			if (!t) return -1;
		}
		external_parameter_delete(external_parameter);
	}
	return 0;
}

//Added by Tian Yiqing, Implement upload rpc method, Start
static int xml_handle_upload(mxml_node_t *body_in,
				   mxml_node_t *tree_in,
				   mxml_node_t *tree_out)
{
	mxml_node_t *n, *t, *b = body_in, *body_out;
	char *upload_url = NULL,
		*command_key = NULL, *file_type = NULL, *username = NULL,
		*password = NULL, r;
	int delay = -1, code = FAULT_9002;

	body_out = mxmlFindElement(tree_out, tree_out, "soap_env:Body", NULL, NULL, MXML_DESCEND);
	if (!body_out) return -1;

	while (b != NULL) {
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "CommandKey")) {
			command_key = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "CommandKey") &&
			!b->child) {
			command_key = "";
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "FileType")) {
			file_type = xml_get_value_with_whitespace(b);
			b = b->parent->last_child;
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "URL")) {
			upload_url = b->value.text.string;
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "Username")) {
			username = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "Username") &&
			!b->child) {
			username = "";
		}
		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "Password")) {
			password = b->value.text.string;
		}
		if (b && b->type == MXML_ELEMENT &&
			!strcmp(b->value.element.name, "Password") &&
			!b->child) {
			password = "";
		}

		if (b && b->type == MXML_TEXT &&
			b->value.text.string &&
			b->parent->type == MXML_ELEMENT &&
			!strcmp(b->parent->value.element.name, "DelaySeconds")) {
			delay = atoi(b->value.text.string);
		}
		b = mxmlWalkNext(b, body_in, MXML_DESCEND);
	}
	if (!upload_url || !command_key || !file_type || !username || !password || delay < 0) {
		code = FAULT_9003;
		goto fault_out;
	}
	if (sscanf(upload_url,"%*[a-zA-Z_0-9]://%c",&r) < 1 ||
		sscanf(upload_url,"%*[^:]://%*[^:]:%*[^@]@%c",&r) == 1) {
		code = FAULT_9003;
		goto fault_out;
	}
	if (cwmp->upload_count >= MAX_UPLOAD) {
		code = FAULT_9004;
		goto fault_out;
	}
	n = backup_add_upload(command_key, delay, upload_url, file_type, username, password);
	cwmp_add_upload(command_key, delay, upload_url, file_type, username, password, n);
	FREE(file_type);

	t = mxmlNewElement(body_out, "cwmp:UploadResponse");
	if (!t) return -1;

	b = mxmlNewElement(t, "Status");
	if (!b) return -1;

	b = mxmlNewElement(t, "StartTime");
	if (!b) return -1;

	b = mxmlNewText(b, 0, UNKNOWN_TIME);
	if (!b) return -1;

	b = mxmlFindElement(t, tree_out, "Status", NULL, NULL, MXML_DESCEND);
	if (!b) return -1;

	b = mxmlNewText(b, 0, "1");

	b = mxmlNewElement(t, "CompleteTime");
	if (!b) return -1;

	b = mxmlNewText(b, 0, UNKNOWN_TIME);
	if (!b) return -1;

	log_message(NAME, L_NOTICE, "send UploadResponse to the ACS\n");
	return 0;

fault_out:
	xml_create_generic_fault_message(body_out, code);
	FREE(file_type);
	return 0;
}
//Added by Tian Yiqing, Implement upload rpc method, End


