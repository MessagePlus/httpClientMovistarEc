#include "gwlib/gwlib.h"

#define SQL_SELECT_MT "SELECT id,phoneNumber,shortNumber,receivedTime,deliveryTime,dispatchTime,deliveryCount,msgText,carrierMsgId,integratorMsgId,status,errCode,errText,serviceId,serviceName,carrierId,integratorId,integratorQueueId \
FROM trafficMT \
WHERE status='QUEUED' \
AND deliveryTime<=now() \
AND connectionId='%s' \
LIMIT 1000 "

#define SQL_UPDATE_MT_STATUS "UPDATE trafficMT  \
SET status = '%s', \
dispatchTime = now() \
WHERE id= %s "

#define SQL_UPDATE_MT_STATUS_ERROR "UPDATE trafficMT  \
SET status = '%s',errCode=%d,errText='%s', \
	dispatchTime = now() \
WHERE id= %s "

#define SQL_UPDATE_MT_STATUS_ "UPDATE trafficMT  \
SET status = '%s', \
carrierMsgId ='%s', \
dispatchTime = now() \
WHERE id= %s "



#define SQL_SELECT_CARRIER_ROUTE "SELECT c.id AS id,c.name AS name,cast(group_concat(p.preffix separator ';') as char(5000) charset utf8) AS preffix \
FROM carrier c, preffixMapping p \
WHERE c.id = p.carrierId \
GROUP BY c.name"

#define SQL_SELECT_PORTED_NUMBER "SELECT carrierId FROM ported \
where portedNumber='%s' "

#define SQL_SELECT_BLACK_LIST "SELECT * FROM blackList \
where phoneNumber='%s' "



#include "gw/msg.h"
#include <mysql/mysql.h>

MYSQL_RES* mysql_select(const Octstr *sql);
void mysql_update(const Octstr *sql);
void sql_save_msg(Msg *msg, Octstr *momt);
Msg *mysql_fetch_msg();
void sql_shutdown();
struct server_type *sqlbox_init_mysql(Cfg* cfg);
Octstr *sqlbox_id;

struct server_type {
	Octstr *type;
	void (*sql_enter)(Cfg *);
	void (*sql_leave)();
	Msg *(*sql_fetch_msg)();
	void (*sql_save_msg)(Msg *, Octstr *);
};

