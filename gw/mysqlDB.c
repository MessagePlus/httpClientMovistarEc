#include "gwlib/gwlib.h"

#include "gwlib/dbpool.h"
#include <mysql/mysql.h>

#include "mysqlDB.h"

#define sql_update mysql_update
#define sql_select mysql_select

static Octstr *sqlbox_logtable;
static Octstr *sqlbox_insert_table;

/*
 * Our connection pool to mysql.
 */

static DBPool *pool = NULL;

void mysql_update(const Octstr *sql)
{
	int state;
	DBPoolConn *pc;


	debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));


	pc = dbpool_conn_consume(pool);
	if (pc == NULL)
	{
		error(0, "MYSQL: Database pool got no connection! DB update failed!");
		return;
	}

	state = mysql_query(pc->conn, octstr_get_cstr(sql));
	if (state != 0)
		error(0, "MYSQL: %s", mysql_error(pc->conn));

	dbpool_conn_produce(pc);
}

MYSQL_RES* mysql_select(const Octstr *sql)
{
	int state;
	MYSQL_RES *result = NULL;
	DBPoolConn *pc;


	debug("SQLBOX", 0, "sql: %s", octstr_get_cstr(sql));


	pc = dbpool_conn_consume(pool);
	if (pc == NULL)
	{
		error(0, "MYSQL: Database pool got no connection! DB update failed!");
		return NULL;
	}

	state = mysql_query(pc->conn, octstr_get_cstr(sql));
	if (state != 0)
	{
		error(0, "MYSQL: %s", mysql_error(pc->conn));
	}
	else
	{
		result = mysql_store_result(pc->conn);
	}

	dbpool_conn_produce(pc);

	return result;
}

void sqlbox_configure_mysql(Cfg* cfg)
{
	CfgGroup *grp;
	Octstr *sql;

//    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
//        panic(0, "SQLBOX: MySQL: group 'sqlbox' is not specified!");

//	sqlbox_logtable = octstr_imm("traficoLog");
//    if (sqlbox_logtable == NULL) {
//        panic(0, "No 'sql-log-table' not configured.");
//    }
//	sqlbox_insert_table = octstr_imm("trafico");
//    if (sqlbox_insert_table == NULL) {
//        panic(0, "No 'sql-insert-table' not configured.");
//    }

	/* create send_sms && sent_sms tables if they do not exist */
//	sql = octstr_format(TRAFFIC_MO, sqlbox_logtable);
//	sql_update(sql);
//	octstr_destroy(sql);
//	sql = octstr_format(SQLBOX_MYSQL_CREATE_INSERT_TABLE, sqlbox_insert_table);
//	sql_update(sql);
//	octstr_destroy(sql);
	/* end table creation */
}



static Octstr *get_numeric_value_or_return_null(long int num)
{
	if (num == -1)
	{
		return octstr_create("NULL");
	}
	return octstr_format("%ld", num);
}

static Octstr *get_string_value_or_return_null(Octstr *str)
{
	if (str == NULL)
	{
		return octstr_create("NULL");
	}
	if (octstr_compare(str, octstr_imm("")) == 0)
	{
		return octstr_create("NULL");
	}
	octstr_replace(str, octstr_imm("\\"), octstr_imm("\\\\"));
	octstr_replace(str, octstr_imm("\'"), octstr_imm("\\\'"));
	return octstr_format("\'%S\'", str);
}



void mysql_leave()
{
	dbpool_destroy(pool);
}

struct server_type *sqlbox_init_mysql(Cfg* cfg)
{
	CfgGroup *grp;
	List *grplist;
	Octstr *mysql_host, *mysql_user, *mysql_pass, *mysql_db;
	Octstr *p = NULL;
	long pool_size, mysql_port;
	int have_port;
	DBConf *db_conf = NULL;
	struct server_type *res = NULL;

//    /*
//     * check for all mandatory directives that specify the field names
//     * of the used MySQL table
//     */
//    if (!(grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"))))
//        panic(0, "SQLBOX: MySQL: group 'sqlbox' is not specified!");

//    if (!(mysql_id = cfg_get(grp, octstr_imm("id"))))
//        panic(0, "SQLBOX: MySQL: directive 'id' is not specified!");

	/*
	 * now grap the required information from the 'mysql-connection' group
	 * with the mysql-id we just obtained
	 *
	 * we have to loop through all available MySQL connection definitions
	 * and search for the one we are looking for
	 */

//     grplist = cfg_get_multi_group(cfg, octstr_imm("mysql-connection"));
//     while (grplist && (grp = (CfgGroup *)gwlist_extract_first(grplist)) != NULL) {
//         p = cfg_get(grp, octstr_imm("id"));
//         if (p != NULL && octstr_compare(p, mysql_id) == 0) {
//        	 info(0,"Found a connection");
//             goto found;
//         }
//         if (p != NULL) octstr_destroy(p);
//     }
//     panic(0, "SQLBOX: MySQL: connection settings for id '%s' are not specified!",
//         octstr_get_cstr(mysql_id));
	if (!(grp = cfg_get_single_group(cfg, octstr_imm("mysql-connection"))))
		panic(0, "SQLBOX: MySQL: group 'mysql-connection' is not specified!");
//    octstr_destroy(p);
//    gwlist_destroy(grplist, NULL);

	if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
		pool_size = 1;

	if (!(mysql_host = cfg_get(grp, octstr_imm("host"))))
		panic(0, "SQLBOX: MySQL: directive 'host' is not specified!");
	if (!(mysql_user = cfg_get(grp, octstr_imm("username"))))
		panic(0, "SQLBOX: MySQL: directive 'username' is not specified!");
	if (!(mysql_pass = cfg_get(grp, octstr_imm("password"))))
		panic(0, "SQLBOX: MySQL: directive 'password' is not specified!");
	if (!(mysql_db = cfg_get(grp, octstr_imm("database"))))
		panic(0, "SQLBOX: MySQL: directive 'database' is not specified!");
	have_port = (cfg_get_integer(&mysql_port, grp, octstr_imm("port")) != -1);

	/*
	 * ok, ready to connect to MySQL
	 */
	db_conf = gw_malloc(sizeof(DBConf));
	gw_assert(db_conf != NULL);

	db_conf->mysql = gw_malloc(sizeof(MySQLConf));
	gw_assert(db_conf->mysql != NULL);

	db_conf->mysql->host = mysql_host;
	db_conf->mysql->username = mysql_user;
	db_conf->mysql->password = mysql_pass;
	db_conf->mysql->database = mysql_db;
	if (have_port)
	{
		db_conf->mysql->port = mysql_port;
	}

	pool = dbpool_create(DBPOOL_MYSQL, db_conf, pool_size);
	gw_assert(pool != NULL);

	/*
	 * XXX should a failing connect throw panic?!
	 */
	if (dbpool_conn_count(pool) == 0)
		panic(0, "SQLBOX: MySQL: database pool has no connections!");


	res = gw_malloc(sizeof(struct server_type));
	gw_assert(res != NULL);

	res->type = octstr_create("MySQL");
	res->sql_enter = sqlbox_configure_mysql;
	res->sql_leave = mysql_leave;

	return res;
}

