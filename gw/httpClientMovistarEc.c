#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <math.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "gwlib/gwlib.h"
#include "gw/msg.h"
#include "gw/shared.h"
#include "gw/bb.h"
#include "gw/smsc/smpp_pdu.h"
#include "gw/sms.h"
#include "gw/heartbeat.h"
#include "gw/meta_data.h"
#include "mysqlDB.h"
#undef GW_NAME
#undef GW_VERSION
#include "../sb-config.h"


#define SMPP_DEAD 0
#define SMPP_SHUTDOWN 1
#define SMPP_RUNNING 2

CURL *curl;
CURLcode res;
struct MemoryStruct chunk;
struct curl_slist * headers = NULL;
char errorBuffer[CURL_ERROR_SIZE + 1];
/* our config */
static Cfg *cfg;
/* have we received restart cmd from bearerbox? */
static volatile sig_atomic_t restart_httpbox = 0;
static volatile sig_atomic_t httpbox_status;
static Dict *list_dict;
static Dict *list_dict;
static Octstr *httpBoxId;
static Octstr *url;
static time_t timeOut;
#define TIMEOUT_SECONDS 300

struct MemoryStruct {
	char *memory;
	size_t size;
};

typedef char msgSMS[255 + 1];

typedef struct {
	char id[11 + 1];
	char phone[20 + 1];
	msgSMS *multiText;
	char text[255 + 1];
	char shortNumber[20 + 1];
	char status[20 + 1];
	char msgId[20 + 1];
	int errCode;
	char errText[255 + 1];
	char serviceName[200 + 1];
} SMSmsg;



size_t WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data) {
	register int realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *) data;

	mem->memory = (char *) gw_realloc(mem->memory, mem->size + realsize + 1);
	if (mem->memory) {
		memcpy(&(mem->memory[mem->size]), ptr, realsize);
		mem->size += realsize;
		mem->memory[mem->size] = 0;
	}
	return realsize;
}


void escapeXML(char *outText, char *source) {
	int i, j;
	for (i = 0, j = 0; i < strlen(source); i++, j++) {
		switch (source[i]) {
		case '<':
			outText[j++] = '&';
			outText[j++] = 'l';
			outText[j++] = 't';
			outText[j] = ';';
			break;
		case '>':
			outText[j++] = '&';
			outText[j++] = 'g';
			outText[j++] = 't';
			outText[j] = ';';
			break;
		case '&':
			outText[j++] = '&';
			outText[j++] = 'a';
			outText[j++] = 'm';
			outText[j++] = 'p';
			outText[j] = ';';
			break;
		case '"':
			outText[j++] = '&';
			outText[j++] = 'q';
			outText[j++] = 'u';
			outText[j++] = 'o';
			outText[j++] = 't';
			outText[j] = ';';
			break;
			//case '/':
			//    break;
		default:
			outText[j] = source[i];
		}
	}
	outText[j] = '\0';
}

int buildXML(SMSmsg *msg, char* postdata) {
	char phone[20 + 1];
	char textOut[500 + 1];
	char serviceName[200 + 1];
	char id[11 + 1];
	strcpy(phone, msg->phone);
	strcpy(serviceName, msg->serviceName);
	strcpy(id, msg->id);
	//getFormatedPhone(procConfig.prefix, phone);

	escapeXML(textOut, msg->text);

	sprintf(postdata,
			"<?xml version=\"1.0\" encoding=\"ISO-8859-1\" standalone=\"yes\" ?><Submit><MessageGroup ExtTxnID=\"%s\" SubscriptionServiceName=\"%s\"><Message ContentType=\"Content\" MarkingServiceName=\"%s\"><Text>%s</Text></Message></MessageGroup><Users><User>%s</User></Users></Submit>",
			id,serviceName, serviceName, textOut, phone);

	debug("httpClient", 0, "********** DMT XML ********");
	debug("httpClient", 0, "%s", postdata);
	debug("httpClient", 0, "***************************");

	return 0;
}


int ltrim(char *txt) {
	int i;
	if (txt == NULL) {
		return 0;
	}

	i = 0;
	while (i < strlen(txt)) {
		if (txt[i] == ' ' || txt[i] == '\n' || txt[i] == '\r' || txt[i] == '\t') {
			i++;
		} else {
			break;
		}
	}
	if (i > 0) {
		int j;
		for (j = 0; j < strlen(txt) - i; j++) {
			txt[j] = txt[i + j];
		}
		txt[j] = '\0';
	}

	return 0;
}

int rtrim(char *txt) {
	int i = 0;

	if (txt == NULL) {
		return 0;
	}

	i = strlen(txt) - 1;
	while (i >= 0) {
		if (txt[i] == ' ' || txt[i] == '\n' || txt[i] == '\r' || txt[i] == '\t') {
			i--;
		} else {
			break;
		}
	}

	txt[i + 1] = '\0';

	return 0;
}

int trim(char *txt) {
	ltrim(txt);
	rtrim(txt);
	return 0;
}

xmlChar* getNodeValue(xmlDocPtr doc, xmlNodePtr current, const char * tag)
{
   while (current != NULL)
   {
       if ( ! xmlStrcmp(current->name, (const xmlChar*) tag) )
       {
           return xmlNodeListGetString(doc, current->xmlChildrenNode, 1);
       }
       current = current->next;
   }
   return NULL;
}

int parseXML(char *buf, char *status, char *ReturnMessage, char *errText) {
	xmlDocPtr document;
	xmlNodePtr root;
	xmlChar *clave;

	status[0] = '\0';
	errText[0] = '\0';

	while (buf[0] != '\0' && buf[0] != '<') {
		buf++;
	}

	if ((document = xmlParseMemory(buf, strlen(buf))) == NULL) {
		strcpy(errText, "ERROR: Bad document.");
		return -1;
	}

	if ((root = xmlDocGetRootElement(document)) == NULL) {
		xmlFreeDoc(document);
		strcpy(errText, "ERROR: Bad document. Root element not found.");
		return -1;
	}

    if (xmlStrcmp(root->name, (const xmlChar*) "SubmitContentResponse"))
    {
        xmlFreeDoc (document);
        strcpy(errText, "ERROR: Bad document. Node 'SubmitContentResponse' not found.");
        return -1;
    }

    clave = getNodeValue (document, root->xmlChildrenNode, "Status");
    if (clave == NULL)
    {
        xmlFreeDoc (document);
        strcpy(errText, "ERROR: Bad document. Node 'Status' not found.");
        return -1;
    }
    else
    {
        snprintf(status, 11, "%s", clave);
        trim(status);
    }

    clave = getNodeValue (document, root->xmlChildrenNode, "ReturnMessage");
    if (clave == NULL)
    {
        xmlFreeDoc (document);
        strcpy(errText, "ERROR: Bad document. Node 'ReturnMessage' not found.");
        return -1;
    }
    else
    {
        snprintf(ReturnMessage, 256, "%s", clave);
        trim(ReturnMessage);
    }

	xmlFree(clave);
	xmlFreeDoc(document);

	strcpy(errText, "");
	return 0;
}


int sendMT(SMSmsg *msg, char *moURL) {
	int res;
	char postthis[4096];

	chunk.size = 0;
	buildXML(msg, postthis);
	curl_easy_setopt(curl, CURLOPT_URL, moURL);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postthis);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(postthis));
	curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, 10);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);
	res = curl_easy_perform(curl);
	debug("httpClient", 0, "curl params setted...");

	if (res == 0) {
		long httpReturnCode;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpReturnCode);

		if (httpReturnCode == 200) {
			int retParse = 0;
			char statusParse[11], ReturnMessage[256], errTextParse[256];
			debug("httpClient", 0, "********************");
			debug("httpClient", 0, "http return code: %ld", httpReturnCode);
			debug("httpClient", 0, "%s", (char *) chunk.memory);
			retParse = parseXML(chunk.memory, statusParse, ReturnMessage, errTextParse);
			//retParse = 0;
			msg->errCode = atoi(statusParse);
			sprintf(msg->errText, ReturnMessage);

			if (retParse != 0) {
				debug("httpClient", 0, "XML ERROR: %s", errTextParse);
				res = retParse;
			} else {
				if (strcmp(statusParse, "0") != 0) {
					debug("httpClient", 0, "Status ERROR: %s", statusParse);
					res = -1;
				} else {
					debug("httpClient", 0, "Status OK: %s", statusParse);
					res = 0;
				}
			}
			debug("httpClient", 0, "********************");
		} else {
			res = httpReturnCode;
			debug("httpClient", 0, "********************");
			debug("httpClient", 0, "HTTP ERROR");
			debug("httpClient", 0, "http return code: %ld", httpReturnCode);
			debug("httpClient", 0, "%s", (char *) chunk.memory);
			debug("httpClient", 0, "********************");
		}
	} else {
		// agregar un reinicio
		debug("httpClient", 0, "********************");
		debug("httpClient", 0, "ERROR DMT: %s", errorBuffer);
		debug("httpClient", 0, "moURL: %s", moURL);
		debug("httpClient", 0, "phone: %s", msg->phone);
		debug("httpClient", 0, "text : %s", msg->text);
		debug("httpClient", 0, "********************");
		msg->errCode=1050;
		sprintf(msg->errText, "Connection failed: Could not send the message");
	}
	return res;
}



static void signal_handler(int signum) {
	/* On some implementations (i.e. linuxthreads), signals are delivered
	 * to all threads.  We only want to handle each signal once for the
	 * entire box, and we let the gwthread wrapper take care of choosing
	 * one.
	 */
	if (!gwthread_shouldhandlesignal(signum))
		return;

	switch (signum) {
	case SIGINT:
	case SIGTERM:
		if (httpbox_status == SMPP_RUNNING) {
			error(0, "SIGINT received, aborting program...");
			httpbox_status = SMPP_SHUTDOWN;
			debug("httpClient", 0, "server is running with smppbox_status %d", httpbox_status);
			gwthread_wakeup_all();
		}
		break;

	case SIGHUP:
		warning(0, "SIGHUP received, catching and re-opening logs");
		log_reopen();
		alog_reopen();
		break;

		/*
		 * It would be more proper to use SIGUSR1 for this, but on some
		 * platforms that's reserved by the pthread support.
		 */
	case SIGQUIT:
		warning(0, "SIGQUIT received, reporting memory usage.");
		gw_check_leaks();
		break;
	}
}

static void setup_signal_handlers(void) {
	struct sigaction act;

	act.sa_handler = signal_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGQUIT, &act, NULL);
	sigaction(SIGHUP, &act, NULL);
	sigaction(SIGPIPE, &act, NULL);
	sigaction(SIGTERM, &act, NULL);
}

static void gw_smpp_enter(Cfg *cfg) {
}

static void gw_smpp_leave() {
}

static void msg_list_destroy(List *l) {
	long i, len;
	Msg *item;

	i = 0;
	len = gwlist_len(l);
	while (i < len) {
		item = gwlist_get(l, i);
		msg_destroy(item);
		item = NULL;
		++i;
	}
}

static void msg_list_destroy_item(void *item) {
	msg_list_destroy(item);
}

static int check_args(int i, int argc, char **argv) {
	if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--tryhttp") == 0) {
		//only_try_http = 1;
	} else
		return -1;

	return 0;
}

/*
 * Adding hooks to kannel check config
 *
 * Martin Conte.
 */

static int smppbox_is_allowed_in_group(Octstr *group, Octstr *variable) {
	Octstr *groupstr;

	groupstr = octstr_imm("group");

#define OCTSTR(name) \
        if (octstr_compare(octstr_imm(#name), variable) == 0) \
        return 1;
#define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), group) == 0) { \
        if (octstr_compare(groupstr, variable) == 0) \
        return 1; \
        fields \
        return 0; \
    }
#define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), group) == 0) { \
        if (octstr_compare(groupstr, variable) == 0) \
        return 1; \
        fields \
        return 0; \
    }
#include "httpClientMovistarEc-cfg.def"

	return 0;
}

#undef OCTSTR
#undef SINGLE_GROUP
#undef MULTI_GROUP

static int smppbox_is_single_group(Octstr *query) {
#define OCTSTR(name)
#define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 1;
#define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 0;
#include "httpClientMovistarEc-cfg.def"
	return 0;
}

static void smpp_server_box_shutdown(void) {
//	octstr_destroy(our_system_id);
//	our_system_id = NULL;
//	octstr_destroy(smppbox_id);
//	smppbox_id = NULL;
	smpp_pdu_shutdown();
}

static void main_thread() {
	while (httpbox_status == SMPP_RUNNING) {
		readQueueMessages();
		gwthread_sleep(1.0);

	}
}

void initRowMO(SMSmsg *msg) {
	msg = gw_malloc( sizeof(SMSmsg));
}

#define octstr_null_create(x) ((x != NULL) ? octstr_create(x) : octstr_create(""))
int readQueueMessages() {

	int ret;
	MYSQL_RES *result = NULL;
	MYSQL_ROW row = NULL;
	int send;
	result = mysql_select(octstr_format(SQL_SELECT_MT, octstr_get_cstr(httpBoxId)));
	int num_rows = mysql_num_rows(result);
	debug("httpClient", 0, "%i messages in the queue", num_rows);
	if (num_rows == 0) {
		mysql_free_result(result);
		return 0;
	}

	while ((row = mysql_fetch_row(result))) {
		SMSmsg msg;
		//initRowMO(msg);
		int ret;
		strcpy(msg.id, row[0]);
		strcpy(msg.phone, row[1]);
		strcpy(msg.text, row[7]);
		strcpy(msg.serviceName, row[14]);
		debug("httpClient", 0, "processing MT msg id %s phone=%s text=%s", msg.id, msg.phone, msg.text);
		ret = sendMT(&msg, octstr_get_cstr(url));
		debug("httpClient", 0, "Return Processing code = %d - %d - %s", ret, msg.errCode, msg.errText);

		if(ret==0)
		{
			mysql_update(octstr_format(SQL_UPDATE_MT_STATUS, "DISPATCHED", row[0]));
		}else{
			mysql_update(octstr_format(SQL_UPDATE_MT_STATUS_ERROR, "NOTDISPATCHED",msg.errCode,msg.errText,row[0]));
		}

	}
	mysql_free_result(result);
	return 0;

}

void http_client_box_run() {
	debug("httpClient", 0, "Start method http_client_box_run");
	main_thread();
	debug("httpClient", 0, "End method http_client_box_run");
}

static void init_smpp_server_box(Cfg *cfg) {
	CfgGroup *cfg_group;
	Octstr *log_file;

	long log_level;

	log_file = NULL;
	log_level = 0;

	debug("httpClient", 0, "********** HTTP Client Box Configuration Initialization **********");

	/* initialize low level PDUs */
	if (smpp_pdu_init(cfg) == -1)
		panic(0, "Connot start with PDU init failed.");

	/*
	 * first we take the port number in bearerbox and other values from the
	 * httpClient group in configuration file
	 */

	cfg_group = cfg_get_single_group(cfg, octstr_imm("httpClientMovistarEc"));
	if (cfg_group == NULL)
		panic(0, "No 'httpClient' group in configuration");

	httpBoxId = cfg_get(cfg_group, octstr_imm("httpClientMovistarEc-id"));

	/* setup logfile stuff */
	log_file = cfg_get(cfg_group, octstr_imm("log-file"));
	url = cfg_get(cfg_group, octstr_imm("url"));
	cfg_get_integer(&log_level, cfg_group, octstr_imm("log-level"));

	if (log_file != NULL) {
		info(0, "Starting to log to file %s level %ld", octstr_get_cstr(log_file), log_level);
		log_open(octstr_get_cstr(log_file), log_level, GW_NON_EXCL);

	}

	if (cfg_get_integer(&timeOut, cfg_group, octstr_imm("time-out")) == -1)
		timeOut = TIMEOUT_SECONDS;



	debug("httpClient", 0, "==========Configuration Parameters============");
	debug("httpClient", 0, "===> httpClient-id:          %s ", octstr_get_cstr(httpBoxId));
	debug("httpClient", 0, "===> url:                    %s ", octstr_get_cstr(url));
	debug("httpClient", 0, "===> log-file:               %s ", octstr_get_cstr(log_file));
	debug("httpClient", 0, "===> log-level:              %ld", log_level);
	debug("httpClient", 0, "===> timeout:                %ld ", timeOut);
	debug("httpClient", 0, "==============================================");

	octstr_destroy(log_file);
	gw_smpp_enter(cfg);
	httpbox_status = SMPP_RUNNING;
	debug("httpClient", 0, "http_status: %d ", httpbox_status);
	debug("httpClient", 0, "********** HTTP Client Box Configuration End **********");
}

int main(int argc, char **argv) {
	int cf_index;
	Octstr *filename, *version;

	gwlib_init();
	list_dict = dict_create(32, msg_list_destroy_item);

	cf_index = get_and_set_debugs(argc, argv, check_args);
	setup_signal_handlers();

	if (argv[cf_index] == NULL)
		filename = octstr_create("httpClientMovistarEc.conf");
	else
		filename = octstr_create(argv[cf_index]);

	cfg = cfg_create(filename);

	/* Adding cfg-checks to core */
	cfg_add_hooks(smppbox_is_allowed_in_group, smppbox_is_single_group);

	if (cfg_read(cfg) == -1)
		panic(0, "Couldn't read configuration from `%s'.", octstr_get_cstr(filename));

	octstr_destroy(filename);

	version = octstr_format("httpClient version %s gwlib", GW_VERSION);
	report_versions(octstr_get_cstr(version));
	octstr_destroy(version);

	struct server_type *res = NULL;
	res = sqlbox_init_mysql(cfg);
	sqlbox_configure_mysql(cfg);

	init_smpp_server_box(cfg);
	http_client_box_run();

	//gwthread_join_every(sql_to_smpp);
	//gwthread_join_every(smpp_to_sql);
	smpp_server_box_shutdown();

	dict_destroy(list_dict);
	list_dict = NULL;

	cfg_destroy(cfg);

	if (restart_httpbox) {
		gwthread_sleep(1.0);
	}

	gw_smpp_leave();
	gwlib_shutdown();

	if (restart_httpbox)
		execvp(argv[0], argv);
	return 0;
}
