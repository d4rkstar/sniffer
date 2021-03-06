#include <stdio.h>
#include <iostream>
#include <syslog.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sstream>
#include <stdarg.h>
#include <netdb.h>
#include <mysqld_error.h>
#include <errmsg.h>
#include "voipmonitor.h"

#include "tools.h"

#include "sql_db.h"
#include "fraud.h"


extern int verbosity;
extern int opt_mysql_port;
extern char opt_match_header[128];
extern int terminating;
extern int opt_ipaccount;
extern int opt_id_sensor;
extern bool opt_cdr_partition;
extern bool opt_cdr_sipport;
extern bool opt_last_rtp_from_end;
extern bool opt_cdr_rtpport;
extern int opt_create_old_partitions;
extern bool opt_disable_partition_operations;
extern vector<dstring> opt_custom_headers_cdr;
extern vector<dstring> opt_custom_headers_message;
extern char get_customers_pn_query[1024];
extern int opt_dscp;
extern int opt_enable_http_enum_tables;
extern int opt_enable_webrtc_table;
extern int opt_mysqlcompress;
extern pthread_mutex_t mysqlconnect_lock;      
extern int opt_mos_lqo;
extern int opt_read_from_file;
extern char opt_pb_read_from_file[256];
extern volatile int calls_cdr_save_counter;
extern volatile int calls_message_save_counter;
extern int opt_enable_fraud;

extern char sql_driver[256];

extern char mysql_host[256];
extern char mysql_database[256];
extern char mysql_user[256];
extern char mysql_password[256];
extern int opt_mysql_port;
extern char opt_mysql_timezone[256];
extern int opt_skiprtpdata;

extern char odbc_dsn[256];
extern char odbc_user[256];
extern char odbc_password[256];
extern char odbc_driver[256];

extern char cloud_host[256];
extern char cloud_token[256];

int sql_noerror = 0;
int sql_disable_next_attempt_if_error = 0;
bool opt_cdr_partition_oldver = false;
bool exists_column_message_content_length = false;


string SqlDb_row::operator [] (const char *fieldName) {
	int indexField = this->getIndexField(fieldName);
	if(indexField >= 0) {
		return(row[indexField].content);
	}
	return("");
}

string SqlDb_row::operator [] (string fieldName) {
	return((*this)[fieldName.c_str()]);
}

string SqlDb_row::operator [] (int indexField) {
	return(row[indexField].content);
}

SqlDb_row::operator int() {
	return(!this->isEmpty());
}

void SqlDb_row::add(const char *content, string fieldName) {
	if(fieldName != "") {
		for(size_t i = 0; i < row.size(); i++) {
			if(row[i].fieldName == fieldName) {
				row[i] = SqlDb_rowField(content, fieldName);
				return;
			}
		}
	}
	this->row.push_back(SqlDb_rowField(content, fieldName));
}

void SqlDb_row::add(string content, string fieldName) {
	if(fieldName != "") {
		for(size_t i = 0; i < row.size(); i++) {
			if(row[i].fieldName == fieldName) {
				row[i] = SqlDb_rowField(content, fieldName);
				return;
			}
		}
	}
	this->row.push_back(SqlDb_rowField(content, fieldName));
}

void SqlDb_row::add(int content, string fieldName, bool null) {
	if(!content && null) {
		this->add((const char*)NULL, fieldName);
	} else {
		char str_content[100];
		sprintf(str_content, "%i", content);
		this->add(str_content, fieldName);
	}
}

void SqlDb_row::add(unsigned int content, string fieldName, bool null) {
	if(!content && null) {
		this->add((const char*)NULL, fieldName);
	} else {
		char str_content[100];
		sprintf(str_content, "%u", content);
		this->add(str_content, fieldName);
	}
}

void SqlDb_row::add(long int content,  string fieldName, bool null) {
	if(!content && null) {
		this->add((const char*)NULL, fieldName);
	} else {
		char str_content[100];
		sprintf(str_content, "%li", content);
		this->add(str_content, fieldName);
	}
}

void SqlDb_row::add(double content,  string fieldName, bool null) {
	if(!content && null) {
		this->add((const char*)NULL, fieldName);
	} else {
		char str_content[100];
		sprintf(str_content, "%lf", content);
		this->add(str_content, fieldName);
	}
}

void SqlDb_row::add(u_int64_t content,  string fieldName, bool null) {
	if(!content && null) {
		this->add((const char*)NULL, fieldName);
	} else {
		char str_content[100];
		sprintf(str_content, "%llu", (unsigned long long)content);
		this->add(str_content, fieldName);
	}
}

int SqlDb_row::getIndexField(string fieldName) {
	for(size_t i = 0; i < row.size(); i++) {
		if(!strcasecmp(row[i].fieldName.c_str(), fieldName.c_str())) {
			return(i);
		}
	}
	if(this->sqlDb) {
		return(this->sqlDb->getIndexField(fieldName));
	}
	return(-1);
}

bool SqlDb_row::isEmpty() {
	return(!row.size());
}

bool SqlDb_row::isNull(string fieldName) {
	int indexField = this->getIndexField(fieldName);
	if(indexField >= 0) {
		return(row[indexField].null);
	}
	return(false);
}

string SqlDb_row::implodeFields(string separator, string border) {
	string rslt;
	for(size_t i = 0; i < this->row.size(); i++) {
		if(i) { rslt += separator; }
		rslt += border + /*'`' +*/ this->row[i].fieldName + /*'`' +*/ border;
	}
	return(rslt);
}

string SqlDb_row::implodeContent(string separator, string border, bool enableSqlString, bool escapeAll) {
	string rslt;
	for(size_t i = 0; i < this->row.size(); i++) {
		if(i) { rslt += separator; }
		if(this->row[i].null) {
			rslt += "NULL";
		} else if(enableSqlString && this->row[i].content.substr(0, 12) == "_\\_'SQL'_\\_:") {
			rslt += this->row[i].content.substr(12);
		} else {
			rslt += border + 
				(escapeAll ? sqlEscapeString(this->row[i].content) : this->row[i].content) + 
				border;
		}
	}
	return(rslt);
}

string SqlDb_row::keyvalList(string separator) {
	string rslt;
	for(size_t i = 0; i < this->row.size(); i++) {
		if(this->row[i].null) {
			rslt += this->row[i].fieldName + ":NULL\n";
		} else {
			rslt += this->row[i].fieldName + separator + this->row[i].content + "\n";
		}
	}
	return(rslt);
}


SqlDb::SqlDb() {
	this->clearLastError();
	this->maxQueryPass = UINT_MAX;
	this->loginTimeout = (ulong)NULL;
	this->enableSqlStringInContent = false;
	this->disableNextAttemptIfError = false;
	this->connecting = false;
	this->cloud_data_rows = 0;
	this->cloud_data_index = 0;
	this->maxAllowedPacket = 1024*1024;
	this->lastError = 0;
}

SqlDb::~SqlDb() {
}

void SqlDb::setConnectParameters(string server, string user, string password, string database, bool showversion) {
	this->conn_server = server;
	this->conn_user = user;
	this->conn_password = password;
	this->conn_database = database;
	this->conn_showversion = showversion;
}

void SqlDb::setCloudParameters(string cloud_host, string cloud_token) {
	this->cloud_host = cloud_host;
	this->cloud_token = cloud_token;
}

void SqlDb::setLoginTimeout(ulong loginTimeout) {
	this->loginTimeout = loginTimeout;
}

bool SqlDb::reconnect() {
	if(this->connecting) {
		if(verbosity > 1) {
			syslog(LOG_NOTICE, "prevent recursion of connect to db");
		}
		return(false);
	}
	if(verbosity > 1) {
		syslog(LOG_INFO, "start reconnect");
	}
	this->disconnect();
	bool rslt = this->connect();
	if(verbosity > 1) {
		syslog(LOG_INFO, "reconnect rslt: %s", rslt ? "OK" : "FAIL");
	}
	return(rslt);
}

bool SqlDb::queryByCurl(string query) {
	cloud_data_columns.clear();
	cloud_data.clear();
	cloud_data_rows = 0;
	cloud_data_index = 0;
	clearLastError();
	bool ok = false;
	vector<dstring> postData;
	postData.push_back(dstring("query", query.c_str()));
	postData.push_back(dstring("token", cloud_token));
	unsigned int attempt = 0;
	for(unsigned int pass = 0; pass < this->maxQueryPass; pass++, attempt++) {
		if(pass > 0) {
			sleep(1);
			syslog(LOG_INFO, "next attempt %u - query: %s", attempt, prepareQueryForPrintf(query).c_str());
		}
		SimpleBuffer responseBuffer;
		string error;
		get_url_response(cloud_redirect.empty() ? cloud_host.c_str() : cloud_redirect.c_str(),
				 &responseBuffer, &postData, &error);
		if(error.empty()) {
			if(!responseBuffer.empty()) {
				if(responseBuffer.isJsonObject()) {
					JsonItem jsonData;
					jsonData.parse((char*)responseBuffer);
					string result = jsonData.getValue("result");
					trim(result);
					if(!strcasecmp(result.c_str(), "OK")) {
						ok = true;
					} else if(!strncasecmp(result.c_str(), "REDIRECT TO", 11)) {
						cloud_redirect = result.substr(11);
						trim(cloud_redirect);
						if(cloud_redirect.empty()) {
							setLastError(0, "missing redirect ip / server", true);
						} else {
							pass = 0;
							continue;
						}
					} else {
						bool tryNext = true;
						unsigned int errorCode = atol(result.c_str());
						size_t posSeparator = result.find('|');
						string errorString;
						if(posSeparator != string::npos) {
							size_t posSeparator2 = result.find('|', posSeparator + 1);
							if(posSeparator2 != string::npos) {
								tryNext = atoi(result.substr(posSeparator + 1).c_str());
								errorString = result.substr(posSeparator2 + 1);
							} else {
								errorString = result.substr(posSeparator + 1);
							}
						} else {
							errorString = result;
						}
						if(!sql_noerror) {
							setLastError(errorCode, errorString.c_str(), true);
						}
						if(tryNext) {
							if(sql_noerror || sql_disable_next_attempt_if_error || this->disableNextAttemptIfError ||
							   errorCode == ER_PARSE_ERROR) {
								break;
							} else if(errorCode != CR_SERVER_GONE_ERROR &&
								  pass < this->maxQueryPass - 5) {
								pass = this->maxQueryPass - 5;
							}
						} else {
							break;
						}
					}
					if(ok) {
						JsonItem *dataJsonDataRows = jsonData.getItem("data_rows");
						if(dataJsonDataRows) {
							cloud_data_rows = atol(dataJsonDataRows->getLocalValue().c_str());
						}
						JsonItem *dataJsonItems = jsonData.getItem("data");
						if(dataJsonItems) {
							for(size_t i = 0; i < dataJsonItems->getLocalCount(); i++) {
								JsonItem *dataJsonItem = dataJsonItems->getLocalItem(i);
								for(size_t j = 0; j < dataJsonItem->getLocalCount(); j++) {
									string dataItem = dataJsonItem->getLocalItem(j)->getLocalValue();
									if(i == 0) {
										cloud_data_columns.push_back(dataItem);
									} else {
										if(cloud_data.size() < i) {
											vector<string> row;
											cloud_data.push_back(row);
										}
										cloud_data[i-1].push_back(dataItem);
									}
								}
							}
						}
						break;
					}
				} else {
					setLastError(0, "bad response - " + string(responseBuffer), true);
				}
			} else {
				setLastError(0, "response is empty", true);
			}
		} else {
			setLastError(0, error.c_str(), true);
		}
	}
	return(ok);
}

void SqlDb::prepareQuery(string *query) {
	size_t findPos;
	if(this->getSubtypeDb() == "mssql") {
		const char *substFce[][2] = { 
				{ "UNIX_TIMESTAMP", "dbo.unix_timestamp" },
				{ "NOW", "dbo.now" },
				{ "SUBTIME", "dbo.subtime" }
		};
		for(unsigned int i = 0; i < sizeof(substFce)/sizeof(substFce[0]); i++) {
			while((findPos  = query->find(substFce[i][0])) != string::npos) {
				query->replace(findPos, strlen(substFce[i][0]), substFce[i][1]);
			}
		}
	}
	while((findPos  = query->find("_LC_[")) != string::npos) {
		size_t findPosEnd = query->find("]", findPos);
		if(findPosEnd != string::npos) {
			string lc = query->substr(findPos + 5, findPosEnd - findPos - 5);
			if((this->getSubtypeDb() == "mssql")|(this->getSubtypeDb() == "pgsql")) {
				lc = "case when " + lc + " then 1 else 0 end";
			}
			query->replace(findPos, findPosEnd - findPos + 1, lc);
		}
	}
}

string SqlDb::insertQuery(string table, SqlDb_row row, bool enableSqlStringInContent, bool escapeAll, bool insertIgnore) {
	string query = 
		string("INSERT ") + (insertIgnore ? "IGNORE " : "") + "INTO " + table + " ( " + row.implodeFields(this->getFieldSeparator(), this->getFieldBorder()) + 
		" ) VALUES ( " + row.implodeContent(this->getContentSeparator(), this->getContentBorder(), enableSqlStringInContent || this->enableSqlStringInContent, escapeAll) + " )";
	return(query);
}

string SqlDb::insertQuery(string table, vector<SqlDb_row> *rows, bool enableSqlStringInContent, bool escapeAll, bool insertIgnore) {
	if(!rows->size()) {
		return("");
	}
	string values = "";
	for(size_t i = 0; i < rows->size(); i++) {
		values += "( " + (*rows)[i].implodeContent(this->getContentSeparator(), this->getContentBorder(), enableSqlStringInContent || this->enableSqlStringInContent, escapeAll) + " )";
		if(i < rows->size() - 1) {
			values += ",";
		}
	}
	string query = 
		string("INSERT ") + (insertIgnore ? "IGNORE " : "") + "INTO " + table + " ( " + (*rows)[0].implodeFields(this->getFieldSeparator(), this->getFieldBorder()) + 
		" ) VALUES " + values;
	return(query);
}

int SqlDb::insert(string table, SqlDb_row row) {
	string query = this->insertQuery(table, row);
	if(this->query(query)) {
		return(this->getInsertId());
	}
	return(-1);
}

int SqlDb::insert(string table, vector<SqlDb_row> *rows) {
	if(!rows->size()) {
		return(-1);
	}
	string query = this->insertQuery(table, rows);
	if(this->query(query)) {
		return(this->getInsertId());
	}
	return(-1);
}

int SqlDb::getIdOrInsert(string table, string idField, string uniqueField, SqlDb_row row) {
	string query = 
		"SELECT * FROM " + table + " WHERE " + uniqueField + " = " + 
		this->getContentBorder() + row[uniqueField] + this->getContentBorder();
	if(this->query(query)) {
		SqlDb_row rsltRow = this->fetchRow();
		if(rsltRow) {
			return(atoi(rsltRow[idField].c_str()));
		}
	}
	return(this->insert(table, row));
}

int SqlDb::getIndexField(string fieldName) {
	if(isCloud()) {
		for(size_t i = 0; i < this->cloud_data_columns.size(); i++) {
			if(this->cloud_data_columns[i] == fieldName) {
				return(i);
			}
		}
	} else {
		for(size_t i = 0; i < this->fields.size(); i++) {
			if(this->fields[i] == fieldName) {
				return(i);
			}
		}
	}
	return(-1);
}

void SqlDb::setLastErrorString(string lastErrorString, bool sysLog) {
	this->lastErrorString = lastErrorString;
	if(sysLog && lastErrorString != "") {
		syslog(LOG_ERR, "%s", lastErrorString.c_str());
	}
}

void SqlDb::setEnableSqlStringInContent(bool enableSqlStringInContent) {
	this->enableSqlStringInContent = enableSqlStringInContent;
}

void SqlDb::setDisableNextAttemptIfError() {
	this->disableNextAttemptIfError = true;
}

void SqlDb::setEnableNextAttemptIfError() {
	this->disableNextAttemptIfError = false;
}

void SqlDb::cleanFields() {
	this->fields.clear();
}


SqlDb_mysql::SqlDb_mysql() {
	this->hMysql = NULL;
	this->hMysqlConn = NULL;
	this->hMysqlRes = NULL;
	this->mysqlThreadId = 0;
}

SqlDb_mysql::~SqlDb_mysql() {
	this->clean();
}

bool SqlDb_mysql::connect(bool createDb, bool mainInit) {
	if(isCloud()) {
		return(true);
	}
	this->connecting = true;
	pthread_mutex_lock(&mysqlconnect_lock);
	this->hMysql = mysql_init(NULL);
	if(this->hMysql) {
		my_bool reconnect = 1;
		mysql_options(this->hMysql, MYSQL_OPT_RECONNECT, &reconnect);
		if(this->conn_server_ip.empty()) {
			if(reg_match(this->conn_server.c_str(), "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+")) {
				this->conn_server_ip = this->conn_server;
			} else {
				hostent *conn_server_record = gethostbyname(this->conn_server.c_str());
				if(conn_server_record == NULL) {
					this->setLastErrorString("mysql connect failed - " + this->conn_server + " is unavailable", true);
					pthread_mutex_unlock(&mysqlconnect_lock);
					this->connecting = false;
					return(false);
				}
				in_addr *conn_server_address = (in_addr*)conn_server_record->h_addr;
				this->conn_server_ip = inet_ntoa(*conn_server_address);
				syslog(LOG_NOTICE, "resolve mysql host %s to %s", this->conn_server.c_str(), this->conn_server_ip.c_str());
			}
		}
		for(int connectPass = 0; connectPass < 2; connectPass++) {
			if(connectPass) {
				if(this->hMysqlRes) {
					while(mysql_fetch_row(this->hMysqlRes));
					mysql_free_result(this->hMysqlRes);
					this->hMysqlRes = NULL;
				}
				mysql_close(this->hMysqlConn);
			}
			this->hMysql = mysql_init(NULL);
			this->hMysqlConn = mysql_real_connect(
						this->hMysql,
						this->conn_server_ip.c_str(), this->conn_user.c_str(), this->conn_password.c_str(), NULL,
						opt_mysql_port, NULL, CLIENT_MULTI_RESULTS);
			if(!this->hMysqlConn) {
				break;
			}
			sql_disable_next_attempt_if_error = 1;
			sql_noerror = !mainInit;
			if(this->query("SET GLOBAL max_allowed_packet=1024*1024*100") &&
			   this->query("show variables like 'max_allowed_packet'")) {
				sql_disable_next_attempt_if_error = 0;
				sql_noerror = 0;
				SqlDb_row row;
				if((row = this->fetchRow())) {
					this->maxAllowedPacket = atoll(row[1].c_str());
					if(this->maxAllowedPacket >= 1024*1024*100) {
						break;
					} else if(connectPass) {
						if(mainInit) {
							syslog(LOG_WARNING, "Max allowed packet size is only %lu. Concat query size is limited. "
									    "Please set max_allowed_packet to 100MB manually in your mysql configuration file.", 
							       this->maxAllowedPacket);
						}
					}
				} else {
					if(mainInit) {
						syslog(LOG_WARNING, "Unknown max allowed packet size. Concat query size is limited. "
								    "Please set max_allowed_packet to 100MB manually in your mysql configuration file.");
					}
					break;
				}
			} else {
				sql_disable_next_attempt_if_error = 0;
				sql_noerror = 0;
				if(mainInit) {
					syslog(LOG_WARNING, "Query for set / get max allowed packet size failed. Concat query size is limited. "
							    "Please set max_allowed_packet to 100MB manually in your mysql configuration file.");
				}
				break;
			}
		}
		if(this->hMysqlConn) {
			bool rslt = true;
			this->mysqlThreadId = mysql_thread_id(this->hMysql);
			sql_disable_next_attempt_if_error = 1;
			if(!this->query("SET NAMES UTF8")) {
				rslt = false;
			}
			sql_noerror = 1;
			this->query("SET GLOBAL innodb_stats_on_metadata=0"); // this will speedup "Slow query on information_schema.tables"
			if(opt_mysql_timezone[0]) {
				this->query(string("SET time_zone = '") + opt_mysql_timezone + "'");
			}
			sql_noerror = 0;
			if(!this->query("SET sql_mode = ''")) {
				rslt = false;
			}
			char tmp[1024];
			if(createDb) {
				sprintf(tmp, "CREATE DATABASE IF NOT EXISTS `%s`", this->conn_database.c_str());
				if(!this->query(tmp)) {
					rslt = false;
				}
			}
			sprintf(tmp, "USE `%s`", this->conn_database.c_str());
			if(!this->query(tmp)) {
				rslt = false;
			}
			if(mainInit && !cloud_host[0]) {
				this->query("SHOW VARIABLES LIKE \"version\"");
				SqlDb_row row;
				if((row = this->fetchRow())) {
					this->dbVersion = row[1];
				}
				while(this->fetchRow());
				if(this->conn_showversion) {
					syslog(LOG_INFO, "connect - db version %i.%i", this->getDbMajorVersion(), this->getDbMinorVersion());
				}
			}
			sql_disable_next_attempt_if_error = 0;
			pthread_mutex_unlock(&mysqlconnect_lock);
			if(this->hMysqlRes) {
				mysql_free_result(this->hMysqlRes);
				this->hMysqlRes = NULL;
				this->cleanFields();
			}
			this->connecting = false;
			return(rslt);
		} else {
			this->checkLastError("connect error", true);
		}
	} else {
		this->setLastErrorString("mysql_init failed - insufficient memory ?", true);
	}
	pthread_mutex_unlock(&mysqlconnect_lock);
	this->connecting = false;
	return(false);
}

int SqlDb_mysql::multi_on() {
	return isCloud() ? true : mysql_set_server_option(this->hMysql, MYSQL_OPTION_MULTI_STATEMENTS_ON);
}

int SqlDb_mysql::multi_off() {
	return isCloud() ? true : mysql_set_server_option(this->hMysql, MYSQL_OPTION_MULTI_STATEMENTS_OFF);
}

int SqlDb_mysql::getDbMajorVersion() {
	return(atoi(this->dbVersion.c_str()));
}

int SqlDb_mysql::getDbMinorVersion(int minorLevel) {
	const char *pointToVersion = this->dbVersion.c_str();
	for(int i = 0; i < minorLevel + 1 && pointToVersion; i++) {
		const char *pointToSeparator = strchr(pointToVersion, '.');
		if(pointToSeparator) {
			pointToVersion = pointToSeparator + 1;
		}
	}
	return(pointToVersion ? atoi(pointToVersion) : 0);
}

bool SqlDb_mysql::createRoutine(string routine, string routineName, string routineParamsAndReturn, eRoutineType routineType) {
	bool missing = false;
	bool diff = false;
	if(this->isCloud()) {
		missing = true;
		diff = true;
	} else {
		this->query(string("select routine_definition from information_schema.routines where routine_schema='") + this->conn_database + 
			    "' and routine_name='" + routineName + 
			    "' and routine_type='" + (routineType == procedure ? "PROCEDURE" : "FUNCTION") + "'");
		SqlDb_row row = this->fetchRow();
		if(!row) {
			missing = true;
		} else if(row["routine_definition"] != routine) {
			size_t i = 0, j = 0;
			while(i < routine.length() &&
			      j < row["routine_definition"].length()) {
				if(routine[i] == '\\' && i < routine.length() - 1) {
					++i;
				}
				if(routine[i] != row["routine_definition"][j]) {
					diff = true;
					break;
				}
				++i;
				++j;
			}
			if(!diff && 
			   (i < routine.length() || j < row["routine_definition"].length())) {
				diff = true;
			}
		}
	}
	if(missing || diff) {
		syslog(LOG_NOTICE, "create %s %s", (routineType == procedure ? "procedure" : "function"), routineName.c_str());
		this->query(string("drop ") + (routineType == procedure ? "PROCEDURE" : "FUNCTION") +
			    " if exists " + routineName);
		return(this->query(string("create ") + (routineType == procedure ? "PROCEDURE" : "FUNCTION") + " " +
				   routineName + routineParamsAndReturn + " " + routine));
	} else {
		return(true);
	}
}

void SqlDb_mysql::disconnect() {
	if(isCloud()) {
		return;
	}
	if(this->hMysqlRes) {
		while(mysql_fetch_row(this->hMysqlRes));
		mysql_free_result(this->hMysqlRes);
		this->hMysqlRes = NULL;
	}
	if(this->hMysqlConn) {
		mysql_close(this->hMysqlConn);
		this->hMysqlConn = NULL;
	}
	/* disable dealloc hMysql - is it shared variable ?
		this->hMysql = NULL;
	} 
	else if(this->hMysql) {
		mysql_close(this->hMysql);
		this->hMysql = NULL;
	}
	*/
}

bool SqlDb_mysql::connected() {
	return(isCloud() ? true : this->hMysqlConn != NULL);
}

bool SqlDb_mysql::query(string query) {
	if(isCloud()) {
		this->prepareQuery(&query);
		if(verbosity > 1) {
			syslog(LOG_INFO, prepareQueryForPrintf(query).c_str());
		}
		return(this->queryByCurl(query));
	}
	if(this->hMysqlRes) {
		while(mysql_fetch_row(this->hMysqlRes));
		mysql_free_result(this->hMysqlRes);
		this->hMysqlRes = NULL;
	}
	if(this->connected()) {
		if(mysql_ping(this->hMysql)) {
			if(verbosity > 1) {
				syslog(LOG_INFO, "mysql_ping failed -> force reconnect");
			}
			this->reconnect();
		} else if(this->mysqlThreadId &&
			  this->mysqlThreadId != mysql_thread_id(this->hMysql)) {
			if(verbosity > 1) {
				syslog(LOG_INFO, "diff thread_id -> force reconnect");
			}
			this->reconnect();
		}
	}
	this->prepareQuery(&query);
	if(verbosity > 1) {
		syslog(LOG_INFO, prepareQueryForPrintf(query).c_str());
	}
	bool rslt = false;
	this->cleanFields();
	unsigned int attempt = 1;
	for(unsigned int pass = 0; pass < this->maxQueryPass; pass++) {
		if(pass > 0) {
			sleep(1);
			syslog(LOG_INFO, "next attempt %u - query: %s", attempt - 1, prepareQueryForPrintf(query).c_str());
		}
		if(!this->connected()) {
			this->connect();
		}
		if(this->connected()) {
			if(mysql_query(this->hMysqlConn, query.c_str())) {
				if(verbosity > 1) {
					syslog(LOG_NOTICE, "query error - query: %s", prepareQueryForPrintf(query).c_str());
					syslog(LOG_NOTICE, "query error - error: %s", mysql_error(this->hMysql));
				}
				this->checkLastError("query error in [" + query.substr(0,200) + (query.size() > 200 ? "..." : "") + "]", !sql_noerror);
				if(this->connecting) {
					break;
				} else {
					if(this->getLastError() == CR_SERVER_GONE_ERROR) {
						if(pass < this->maxQueryPass - 1) {
							this->reconnect();
						}
					} else if(sql_noerror || sql_disable_next_attempt_if_error || this->disableNextAttemptIfError ||
						  this->getLastError() == ER_PARSE_ERROR) {
						break;
					} else {
						if(pass < this->maxQueryPass - 5) {
							pass = this->maxQueryPass - 5;
						}
						if(pass < this->maxQueryPass - 1) {
							this->reconnect();
						}
					}
				}
			} else {
				if(verbosity > 1) {
					syslog(LOG_NOTICE, "query ok - %s", prepareQueryForPrintf(query).c_str());
				}
				rslt = true;
				break;
			}
		}
		if(terminating) {
			break;
		}
		++attempt;
	}
	return(rslt);
}

SqlDb_row SqlDb_mysql::fetchRow(bool assoc) {
	SqlDb_row row(this);
	if(isCloud()) {
		if(cloud_data_index < cloud_data_rows &&
		   cloud_data_index < cloud_data.size()) {
			for(size_t i = 0; i < min(cloud_data[cloud_data_index].size(), cloud_data_columns.size()); i++) {
				row.add(cloud_data[cloud_data_index][i], assoc ? cloud_data_columns[i] : "");
			}
			++cloud_data_index;
		}
	} else if(this->hMysqlConn) {
		if(!this->hMysqlRes) {
			this->hMysqlRes = mysql_use_result(this->hMysqlConn);
			if(this->hMysqlRes) {
				MYSQL_FIELD *field;
				for(int i = 0; (field = mysql_fetch_field(this->hMysqlRes)); i++) {
					this->fields.push_back(field->name);
				}
			} else {
				this->checkLastError("fetch row error in function mysql_use_result", true);
			}
		}
		if(this->hMysqlRes) {
			MYSQL_ROW mysqlRow = mysql_fetch_row(hMysqlRes);
			if(mysqlRow) {
				unsigned int numFields = mysql_num_fields(this->hMysqlRes);
				for(unsigned int i = 0; i < numFields; i++) {
					row.add(mysqlRow[i], assoc ? this->fields[i] : "");
				}
			} else {
				this->checkLastError("fetch row error", true);
			}
		}
	}
	return(row);
}

int SqlDb_mysql::getInsertId() {
	if(this->hMysqlConn) {
		return(mysql_insert_id(this->hMysqlConn));
	}
	return(-1);
}

string SqlDb_mysql::escape(const char *inputString, int length) {
	return sqlEscapeString(inputString, length, this->getTypeDb().c_str(), this);
}

bool SqlDb_mysql::checkLastError(string prefixError, bool sysLog, bool clearLastError) {
	if(this->hMysql) {
		unsigned int errno = mysql_errno(this->hMysql);
		if(errno) {
			this->setLastError(errno, (prefixError + ":  " + mysql_error(this->hMysql)).c_str(), sysLog);
			return(true);
		} else if(clearLastError) {
			this->clearLastError();
		}
	}
	return(false);
}

void SqlDb_mysql::clean() {
	this->disconnect();
	this->cleanFields();
}


SqlDb_odbc_bindBufferItem::SqlDb_odbc_bindBufferItem(SQLUSMALLINT colNumber, string fieldName, SQLSMALLINT dataType, SQLULEN columnSize, SQLHSTMT hStatement) {
	this->colNumber = colNumber;
	this->fieldName = fieldName;
	this->dataType = dataType;
	this->columnSize = columnSize;
	this->buffer = new char[this->columnSize + 100]; // 100 - reserve for convert binary to text
	memset(this->buffer, 0, this->columnSize + 100);
	if(hStatement) {
		this->bindCol(hStatement);
	}
}

SqlDb_odbc_bindBufferItem::~SqlDb_odbc_bindBufferItem() {
	if(this->buffer) {
		delete [] this->buffer;
	}
}

void SqlDb_odbc_bindBufferItem::bindCol(SQLHSTMT hStatement) {
	SQLBindCol(hStatement, this->colNumber, SQL_CHAR, this->buffer, this->columnSize, &this->ind);
}

string SqlDb_odbc_bindBufferItem::getContent() {
	return(string(this->buffer));
}

char* SqlDb_odbc_bindBufferItem::getBuffer() {
	return(this->buffer);
}


void SqlDb_odbc_bindBuffer::addItem(SQLUSMALLINT colNumber, string fieldName, SQLSMALLINT dataType, SQLULEN columnSize, SQLHSTMT hStatement) {
	this->push_back(new SqlDb_odbc_bindBufferItem(colNumber, fieldName, dataType, columnSize, hStatement));
}

void SqlDb_odbc_bindBuffer::bindCols(SQLHSTMT hStatement) {
	SQLCHAR columnName[255];
	SQLSMALLINT nameLength;
	SQLSMALLINT dataType;
	SQLULEN columnSize;
	SQLSMALLINT decimalDigits;
	SQLSMALLINT nullable;
	unsigned int columnIndex = 0;
	while(!SQLDescribeCol(
			hStatement, columnIndex + 1, columnName, sizeof(columnName)/sizeof(SQLCHAR),
			&nameLength, &dataType, &columnSize, &decimalDigits, &nullable)) {
		this->addItem(columnIndex + 1, (const char*)columnName, dataType, columnSize + 1, hStatement);
		++columnIndex;
	}
}

string SqlDb_odbc_bindBuffer::getColContent(string fieldName) {
	int index = this->getIndexField(fieldName);
	if(index >= 0) {
		this->getColContent(index);
	}
	return("");
}

string SqlDb_odbc_bindBuffer::getColContent(unsigned int fieldIndex) {
	return(fieldIndex < this->size() ?
		(*this)[fieldIndex]->getContent() :
		"");
}

char* SqlDb_odbc_bindBuffer::getColBuffer(unsigned int fieldIndex) {
	return(fieldIndex < this->size() ?
		(*this)[fieldIndex]->getBuffer() :
		NULL);
}

int SqlDb_odbc_bindBuffer::getIndexField(string fieldName) {
	for(size_t i = 0; i < this->size(); i++) {
		if((*this)[i]->fieldName == fieldName) {
			return(i);
		}
	}
	return(-1);
}


SqlDb_odbc::SqlDb_odbc() {
	this->odbcVersion = (ulong)NULL;
	this->subtypeDb = "";
	this->hEnvironment = NULL;
	this->hConnection = NULL;
	this->hStatement = NULL;
}

SqlDb_odbc::~SqlDb_odbc() {
	this->clean();
}

void SqlDb_odbc::setOdbcVersion(ulong odbcVersion) {
	this->odbcVersion = odbcVersion;
}

void SqlDb_odbc::setSubtypeDb(string subtypeDb) {
	this->subtypeDb = subtypeDb;
}

bool SqlDb_odbc::connect(bool createDb, bool mainInit) {
	this->connecting = true;
	SQLRETURN rslt;
	this->clearLastError();
	if(!this->hEnvironment) {
		rslt = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &this->hEnvironment);
		if(!this->okRslt(rslt)) {
			this->setLastError(rslt, "odbc: error in allocate environment handle", true);
			this->disconnect();
			this->connecting = false;
			return(false);
		}
		if(this->odbcVersion) {
			rslt = SQLSetEnvAttr(this->hEnvironment, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)this->odbcVersion, 0); 
			if(!this->okRslt(rslt)) {
				this->setLastError(rslt, "odbc: error in set environment attributes");
				this->disconnect();
				this->connecting = false;
				return(false);
			}
		}
	}
	if(!this->hConnection) {
		rslt = SQLAllocHandle(SQL_HANDLE_DBC, this->hEnvironment, &this->hConnection); 
		if(!this->okRslt(rslt)) {
			this->setLastError(rslt, "odbc: error in allocate connection handle");
			this->disconnect();
			this->connecting = false;
			return(false);
		}
		if(this->loginTimeout) {
			SQLSetConnectAttr(this->hConnection, SQL_LOGIN_TIMEOUT, (SQLPOINTER *)this->loginTimeout, 0);
		}
		rslt = SQLConnect(this->hConnection, 
				  (SQLCHAR*)this->conn_server.c_str(), SQL_NTS,
				  (SQLCHAR*)this->conn_user.c_str(), SQL_NTS,
				  (SQLCHAR*)this->conn_password.c_str(), SQL_NTS);
		if(!this->okRslt(rslt)) {
			this->checkLastError("odbc: connect error", true);
			this->disconnect();
			this->connecting = false;
			return(false);
		}
	}
	this->connecting = false;
	return(true);
}

void SqlDb_odbc::disconnect() {
	if(this->hStatement) {
		SQLFreeHandle(SQL_HANDLE_STMT, this->hStatement);
		this->hStatement = NULL;
	}
	if(this->hConnection) {
		SQLDisconnect(this->hConnection);
		SQLFreeHandle(SQL_HANDLE_DBC, this->hConnection);
		this->hConnection = NULL;
	}
	if(this->hEnvironment) {
		SQLFreeHandle(SQL_HANDLE_ENV, this->hEnvironment);
		this->hEnvironment = NULL;
	}
}

bool SqlDb_odbc::connected() {
	return(this->hConnection != NULL);
}

bool SqlDb_odbc::query(string query) {
	this->prepareQuery(&query);
	if(verbosity > 1) { 
		syslog(LOG_INFO, prepareQueryForPrintf(query).c_str());
	}
	SQLRETURN rslt = SQL_NULL_DATA;
	if(this->hStatement) {
		SQLFreeHandle(SQL_HANDLE_STMT, this->hStatement);
		this->hStatement = NULL;
	}
	this->cleanFields();
	for(unsigned int pass = 0; pass < this->maxQueryPass; pass++) {
		if(pass > 0) {
			sleep(1);
		}
		if(!this->connected()) {
			this->connect();
		}
		if(this->connected()) {
			rslt = SQLAllocHandle(SQL_HANDLE_STMT, hConnection, &hStatement);
			if(!this->okRslt(rslt)) {
				this->checkLastError("odbc: error in allocate statement handle", true);
				if(terminating) {
					break;
				}
				this->reconnect();
				continue;
			}
			rslt = SQLExecDirect(this->hStatement, (SQLCHAR*)query.c_str(), SQL_NTS);   
			if(!this->okRslt(rslt) && rslt != SQL_NO_DATA) {
				if(!sql_noerror) {
					this->checkLastError("odbc query error", true);
					// cout << query.c_str() << endl;
				}
				if(sql_noerror || sql_disable_next_attempt_if_error || this->disableNextAttemptIfError) {
					break;
				}
				else if(rslt == SQL_ERROR || rslt == SQL_INVALID_HANDLE) {
					if(pass < this->maxQueryPass - 1) {
						this->reconnect();
					}
				} else {
					if(pass < this->maxQueryPass - 5) {
						pass = this->maxQueryPass - 5;
					}
					if(pass < this->maxQueryPass - 1) {
						this->reconnect();
					}
				}
			} else {
				break;
			}
		}
		if(terminating) {
			break;
		}
	}
	return(this->okRslt(rslt) || rslt == SQL_NO_DATA);
}

SqlDb_row SqlDb_odbc::fetchRow(bool assoc) {
	SqlDb_row row(this);
	if(this->hConnection && this->hStatement) {
		if(!this->bindBuffer.size()) {
			this->bindBuffer.bindCols(this->hStatement);
		}
		SQLRETURN rslt = SQLFetch(hStatement);
		if(this->okRslt(rslt) || rslt == SQL_NO_DATA) {
			if(rslt != SQL_NO_DATA) {
				for(unsigned int i = 0; i < this->bindBuffer.size(); i++) {
					row.add(this->bindBuffer.getColBuffer(i),
						assoc ? this->bindBuffer[i]->fieldName : "");
				}
			}
		} else {
			this->checkLastError("odbc fetch error", true);
		}
	}
	return(row);
}

int SqlDb_odbc::getInsertId() {
	SqlDb_row row;
	if (this->getSubtypeDb()=="mssql") {
		if(this->query("select @@identity as last_insert_id") &&
		   (row = this->fetchRow()) != 0) {
			return(atol(row["last_insert_id"].c_str()));
		}
	}
	else if (this->getSubtypeDb()=="pgsql") {
		if(this->query("select lastval() as last_insert_id") &&
		   (row = this->fetchRow()) != 0) {
			return(atol(row["last_insert_id"].c_str()));
		}
	}
	return(-1);
}

int SqlDb_odbc::getIndexField(string fieldName) {
	for(size_t i = 0; i < this->bindBuffer.size(); i++) {
		if(this->bindBuffer[i]->fieldName == fieldName) {
			return(i);
		}
	}
	return(-1);
}

string SqlDb_odbc::escape(const char *inputString, int length) {
	return sqlEscapeString(inputString, length, this->getTypeDb().c_str());
}

bool SqlDb_odbc::checkLastError(string prefixError, bool sysLog, bool clearLastError) {
	if(this->hConnection) {
		SQLCHAR sqlState[10];
		SQLINTEGER nativeError;
		SQLCHAR messageText[1000];
		SQLSMALLINT messageTextLength;
		SQLRETURN rslt = SQLGetDiagRec(
					this->hStatement ? SQL_HANDLE_STMT : SQL_HANDLE_DBC,
					this->hStatement ? this->hStatement : this->hConnection,
					1, sqlState, &nativeError, messageText, sizeof(messageText), &messageTextLength);
		if(this->okRslt(rslt)) {
			if(nativeError) {
				this->setLastError(nativeError, (prefixError + ":  " + string((char*)messageText)).c_str(), sysLog);
				return(true);
			} else {
				this->clearLastError();
			}
		}
	}
	return(false);
}

void SqlDb_odbc::cleanFields() {
	for(unsigned int i = 0; i < this->bindBuffer.size(); i++) {
		delete this->bindBuffer[i];
	}
	this->bindBuffer.clear();
}

void SqlDb_odbc::clean() {
	this->disconnect();
	this->cleanFields();
}

void *MySqlStore_process_storing(void *storeProcess_addr) {
	MySqlStore_process *storeProcess = (MySqlStore_process*)storeProcess_addr;
	storeProcess->store();
	return(NULL);
}
	
MySqlStore_process::MySqlStore_process(int id, const char *host, const char *user, const char *password, const char *database,
				       const char *cloud_host, const char *cloud_token,
				       int concatLimit) {
	this->id = id;
	this->terminated = false;
	this->ignoreTerminating = false;
	this->enableAutoDisconnect = false;
	this->concatLimit = concatLimit;
	this->sqlDb = new SqlDb_mysql();
	this->sqlDb->setConnectParameters(host, user, password, database);
	if(cloud_host && *cloud_host) {
		this->sqlDb->setCloudParameters(cloud_host, cloud_token);
	}
	pthread_mutex_init(&this->lock_mutex, NULL);
	this->thread = (pthread_t)NULL;
}

MySqlStore_process::~MySqlStore_process() {
	if(this->thread) {
		while(!this->terminated) {
			usleep(100000);
		}
		pthread_detach(this->thread);
	}
	if(this->sqlDb) {
		delete this->sqlDb;
	}
}

void MySqlStore_process::connect() {
	if(!this->sqlDb->connected()) {
		this->sqlDb->connect();
	}
}

void MySqlStore_process::disconnect() {
	if(this->sqlDb->connected()) {
		this->sqlDb->disconnect();
	}
}

void MySqlStore_process::query(const char *query_str) {
	if(sverb.store_process_query) {
		cout << "store_process_query_" << this->id << ": " << query_str << endl;
	}
	if(!this->thread) {
		pthread_create(&this->thread, NULL, MySqlStore_process_storing, this);
	}
	this->query_buff.push(query_str);
}

void MySqlStore_process::store() {
	char insert_funcname[20];
	sprintf(insert_funcname, "__insert_%i", this->id);
	if(opt_id_sensor > -1) {
		sprintf(insert_funcname + strlen(insert_funcname), "S%i", opt_id_sensor);
	}
	while(1) {
		int size = 0;
		string queryqueue = "";
		while(1) {
			this->lock();
			if(this->query_buff.size() == 0) {
				this->unlock();
				if(queryqueue != "") {
					for(int pass = 0; pass < 2; pass ++) {
						this->sqlDb->query(string("drop procedure if exists ") + insert_funcname);
						if(this->sqlDb->query(string("create procedure ") + insert_funcname + "()\nbegin\n" + queryqueue + "\nend")) {
							break;
						} else if(this->sqlDb->getLastError() == ER_SP_ALREADY_EXISTS) {
							this->sqlDb->query("repair table mysql.proc");
						} else {
							if(sverb.store_process_query) {
								cout << "store_process_query_" << this->id << ": " << "ERROR " << this->sqlDb->getLastErrorString() << endl;
							}
							break;
						}
					}
					if(!this->sqlDb->query(string("call ") + insert_funcname + "();") && sverb.store_process_query) {
						cout << "store_process_query_" << this->id << ": " << "ERROR " << this->sqlDb->getLastErrorString() << endl;
					}
					queryqueue = "";
					if(verbosity > 1) {
						syslog(LOG_INFO, "STORE id: %i", this->id);
					}
				}
				break;
			}
			string query = this->query_buff.front();
			bool maxAllowedPacketIsFull = false;
			if(queryqueue.size() + query.size() + 100 > this->sqlDb->maxAllowedPacket) {
				maxAllowedPacketIsFull = true;
				this->unlock();
			} else {
				this->query_buff.pop();
				this->unlock();
				queryqueue.append(query);
				size_t query_len = query.length();
				while(query_len && query[query_len - 1] == ' ') {
					--query_len;
				}
				if(query_len && query[query_len - 1] != ';') {
					queryqueue.append("; ");
				}
				if(this->id >= STORE_PROC_ID_CDR_1 && this->id < STORE_PROC_ID_CDR_1 + 10) {
					--calls_cdr_save_counter;
				}
				else if(this->id >= STORE_PROC_ID_MESSAGE_1 && this->id < STORE_PROC_ID_MESSAGE_1 + 10) {
					--calls_message_save_counter;
				}
			}
			if(size < this->concatLimit && !maxAllowedPacketIsFull) {
				size++;
			} else {
				for(int pass = 0; pass < 2; pass ++) {
					this->sqlDb->query(string("drop procedure if exists ") + insert_funcname);
					if(this->sqlDb->query(string("create procedure ") + insert_funcname + "()\nbegin\n" + queryqueue + "\nend")) {
						break;
					} else if(this->sqlDb->getLastError() == ER_SP_ALREADY_EXISTS) {
						this->sqlDb->query("repair table mysql.proc");
					} else {
						break;
					}
				}
				this->sqlDb->query(string("call ") + insert_funcname + "();");
				queryqueue = "";
				size = 0;
				if(verbosity > 1) {
					syslog(LOG_INFO, "STORE id: %i", this->id);
				}
			}
			
			if(!opt_read_from_file && !opt_pb_read_from_file[0] &&
			   terminating && !this->sqlDb->connected()) {
				break;
			}
		}
		if(terminating && !this->ignoreTerminating) {
			break;
		} else if(this->enableAutoDisconnect) {
			this->disconnect();
		}
		sleep(1);
	}
	this->terminated = true;
}

void MySqlStore_process::lock() {
	pthread_mutex_lock(&this->lock_mutex);
}

void MySqlStore_process::unlock() {
	pthread_mutex_unlock(&this->lock_mutex);
}

void MySqlStore_process::setIgnoreTerminating(bool ignoreTerminating) {
	this->ignoreTerminating = ignoreTerminating;
}

void MySqlStore_process::setEnableAutoDisconnect(bool enableAutoDisconnect) {
	this->enableAutoDisconnect = enableAutoDisconnect;
}

void MySqlStore_process::setConcatLimit(int concatLimit) {
	this->concatLimit = concatLimit;
}

MySqlStore::MySqlStore(const char *host, const char *user, const char *password, const char *database,
		       const char *cloud_host, const char *cloud_token) {
	this->host = host;
	this->user = user;
	this->password = password;
	this->database = database;
	if(cloud_host) {
		this->cloud_host = cloud_host;
	}
	if(cloud_token) {
		this->cloud_token = cloud_token;
	}
	this->defaultConcatLimit = 400;
}

MySqlStore::~MySqlStore() {
	map<int, MySqlStore_process*>::iterator iter;
	for(iter = this->processes.begin(); iter != this->processes.end(); ++iter) {
		delete iter->second;
	}
}

void MySqlStore::connect(int id) {
	MySqlStore_process* process = this->find(id);
	process->connect();
}

void MySqlStore::query(const char *query_str, int id) {
	MySqlStore_process* process = this->find(id);
	process->query(query_str);
}

void MySqlStore::query_lock(const char *query_str, int id) {
	MySqlStore_process* process = this->find(id);
	process->lock();
	process->query(query_str);
	process->unlock();
}

void MySqlStore::lock(int id) {
	MySqlStore_process* process = this->find(id);
	process->lock();
}

void MySqlStore::unlock(int id) {
	MySqlStore_process* process = this->find(id);
	process->unlock();
}

void MySqlStore::setIgnoreTerminating(int id, bool ignoreTerminating) {
	MySqlStore_process* process = this->find(id);
	process->setIgnoreTerminating(ignoreTerminating);
}

void MySqlStore::setEnableAutoDisconnect(int id, bool enableAutoDisconnect) {
	MySqlStore_process* process = this->find(id);
	process->setEnableAutoDisconnect(enableAutoDisconnect);
}

void MySqlStore::setConcatLimit(int id, int concatLimit) {
	MySqlStore_process* process = this->find(id);
	process->setConcatLimit(concatLimit);
}

void MySqlStore::setDefaultConcatLimit(int defaultConcatLimit) {
	this->defaultConcatLimit = defaultConcatLimit;
}

MySqlStore_process *MySqlStore::find(int id) {
	if(cloud_host[0]) {
		id = 1;
	}
	MySqlStore_process* process = this->processes[id];
	if(process) {
		return(process);
	}
	process = new MySqlStore_process(id, this->host.c_str(), this->user.c_str(), this->password.c_str(), this->database.c_str(),
					 this->cloud_host.c_str(), this->cloud_token.c_str(),
					 this->defaultConcatLimit);
	this->processes[id] = process;
	return(process);
}

MySqlStore_process *MySqlStore::check(int id) {
	if(cloud_host[0]) {
		id = 1;
	}
	map<int, MySqlStore_process*>::iterator iter = this->processes.find(id);
	if(iter == this->processes.end()) {
		return(NULL);
	} else {
		return(iter->second);
	}
}

size_t MySqlStore::getAllSize(bool lock) {
	size_t size = 0;
	map<int, MySqlStore_process*>::iterator iter;
	for(iter = this->processes.begin(); iter != this->processes.end(); ++iter) {
		if(lock) {
			iter->second->lock();
		}
		size += iter->second->getSize();
		if(lock) {
			iter->second->unlock();
		}
	}
	return(size);
}

int MySqlStore::getSize(int id, bool lock) {
	MySqlStore_process *process = this->check(id);
	if(process) {
		if(lock) {
			process->lock();
		}
		int size = process->getSize();
		if(lock) {
			process->unlock();
		}
		return(size);
	} else {
		return(-1);
	}
}

int MySqlStore::getSizeMult(int n, ...) {
	int size = -1;
	va_list vl;
	va_start(vl, n);
	for(int i = 0; i < n; i++) {
		int id = va_arg(vl, int);
		int _size = this->getSize(id);
		if(_size >= 0) {
			if(size < 0) {
				size = 0;
			}
			size += _size;
		}
	}
	va_end(vl);
	return(size);
}

int MySqlStore::getSizeVect(int id1, int id2, bool lock) {
	int size = -1;
	for(int id = id1; id <= id2; id++) {
		int _size = this->getSize(id);
		if(_size >= 0) {
			if(size < 0) {
				size = 0;
			}
			size += _size;
		}
	}
	return(size);
}


SqlDb *createSqlObject() {
	SqlDb *sqlDb = NULL;
	if(isSqlDriver("mysql")) {
		sqlDb = new SqlDb_mysql();
		sqlDb->setConnectParameters(mysql_host, mysql_user, mysql_password, mysql_database);
		if(cloud_host[0]) {
			sqlDb->setCloudParameters(cloud_host, cloud_token);
		}
	} else if(isSqlDriver("odbc")) {
		SqlDb_odbc *sqlDb_odbc = new SqlDb_odbc();
		sqlDb_odbc->setOdbcVersion(SQL_OV_ODBC3);
		sqlDb_odbc->setSubtypeDb(odbc_driver);
		sqlDb = sqlDb_odbc;
		sqlDb->setConnectParameters(odbc_dsn, odbc_user, odbc_password);
	}
	return(sqlDb);
}

string sqlDateTimeString(time_t unixTime) {
	struct tm * localTime = localtime(&unixTime);
	char dateTimeBuffer[50];
	strftime(dateTimeBuffer, sizeof(dateTimeBuffer), "%Y-%m-%d %H:%M:%S", localTime);
	return string(dateTimeBuffer);
}

string sqlDateString(time_t unixTime) {
	struct tm * localTime = localtime(&unixTime);
	char dateBuffer[50];
	strftime(dateBuffer, sizeof(dateBuffer), "%Y-%m-%d", localTime);
	return string(dateBuffer);
}

string sqlEscapeString(string inputStr, const char *typeDb, SqlDb_mysql *sqlDbMysql) {
	return sqlEscapeString(inputStr.c_str(), 0, typeDb, sqlDbMysql);
}

SqlDb_mysql *sqlDbEscape = NULL; 

string sqlEscapeString(const char *inputStr, int length, const char *typeDb, SqlDb_mysql *sqlDbMysql) {
	if(!length) {
		length = strlen(inputStr);
	}
	if(isTypeDb("mysql", sqlDbMysql ? sqlDbMysql->getTypeDb().c_str() : typeDb) && !cloud_host[0]) {
		bool okEscape = false;
		int sizeBuffer = length * 2 + 10;
		char *buffer = new char[sizeBuffer];
		if(sqlDbMysql && sqlDbMysql->getH_Mysql()) {
			if(mysql_real_escape_string(sqlDbMysql->getH_Mysql(), buffer, inputStr, length) >= 0) {
				okEscape = true;
			}
		} else {
			if(!sqlDbEscape) {
				sqlDbEscape = (SqlDb_mysql*)createSqlObject();
				sqlDbEscape->connect();
			}
			if(sqlDbEscape->connected() &&
			   mysql_real_escape_string(sqlDbEscape->getH_Mysql(), buffer, inputStr, length) >= 0) {
				okEscape = true;
			}
		}
		string rslt = buffer;
		delete [] buffer;
		if(okEscape) {
			return(rslt);
		}
	}
	return _sqlEscapeString(inputStr, length, sqlDbMysql ? sqlDbMysql->getTypeDb().c_str() : typeDb);
}

string _sqlEscapeString(const char *inputStr, int length, const char *typeDb) {
	string rsltString;
	struct escChar {
		char ch;
		const char* escStr;
	} 
	escCharsMysql[] = 
				{
					{ '\'', "\\'" },
					{ '"' , "\\\"" },
					{ '\\', "\\\\" },
					{ '\n', "\\n" }, 	// new line feed
					{ '\r', "\\r" }, 	// cariage return
					// remove after create function test_escape
					//{ '\t', "\\t" }, 	// tab
					//{ '\v', "\\v" }, 	// vertical tab
					//{ '\b', "\\b" }, 	// backspace
					//{ '\f', "\\f" }, 	// form feed
					//{ '\a', "\\a" }, 	// alert (bell)
					//{ '\e', "" }, 		// escape
					// add after create function test_escape
					{    0, "\\0" },
					{   26, "\\Z" }
				},
	escCharsOdbc[] = 
				{ 
					{ '\'', "\'\'" },
					{ '\v', "" }, 		// vertical tab
					{ '\b', "" }, 		// backspace
					{ '\f', "" }, 		// form feed
					{ '\a', "" }, 		// alert (bell)
					{ '\e', "" }, 		// escape
				};
	escChar *escChars = NULL;
	int countEscChars = 0;
	if(isTypeDb("mysql", typeDb)) {
		escChars = escCharsMysql;
		countEscChars = sizeof(escCharsMysql)/sizeof(escChar);
	} else if(isTypeDb("odbc", typeDb)) {
		escChars = escCharsOdbc;
		countEscChars = sizeof(escCharsOdbc)/sizeof(escChar);
	}
	if(!length) {
		length = strlen(inputStr);
	}
	for(int posInputString = 0; posInputString<length; posInputString++) {
		bool isEscChar = false;
		for(int i = 0; i<countEscChars; i++) {
			if(escChars[i].ch == inputStr[posInputString]) {
				rsltString += escChars[i].escStr;
				isEscChar = true;
				break;
			}
		}
		if(!isEscChar) {
			rsltString += inputStr[posInputString];
		}
	}
	return(rsltString);
}

string sqlEscapeStringBorder(string inputStr, char borderChar, const char *typeDb, SqlDb_mysql *sqlDbMysql) {
	return sqlEscapeStringBorder(inputStr.c_str(), borderChar, typeDb, sqlDbMysql);
}

string sqlEscapeStringBorder(const char *inputStr, char borderChar, const char *typeDb, SqlDb_mysql *sqlDbMysql) {
	string rsltString = sqlEscapeString(inputStr, 0, typeDb, sqlDbMysql);
	if(borderChar) {
		rsltString = borderChar + rsltString + borderChar;
	}
	return rsltString;
}

bool isSqlDriver(const char *sqlDriver, const char *checkSqlDriver) {
	return cmpStringIgnoreCase(checkSqlDriver ? checkSqlDriver : sql_driver, sqlDriver);
}

bool isTypeDb(const char *typeDb, const char *checkSqlDriver, const char *checkOdbcDriver) {
	return cmpStringIgnoreCase(checkSqlDriver ? checkSqlDriver : sql_driver, typeDb) ||
	       (cmpStringIgnoreCase(checkSqlDriver ? checkSqlDriver : sql_driver, "odbc") && 
	        cmpStringIgnoreCase(checkOdbcDriver ? checkOdbcDriver : odbc_driver, typeDb));
}

bool cmpStringIgnoreCase(const char* str1, const char* str2) {
	if(str1 == str2) {
		return true;
	}
	if(((str1 || str2) && !(str1 && str2)) ||
	   ((*str1 || *str2) && !(*str1 && *str2)) ||
	   strlen(str1) != strlen(str2)) {
		return false;
	}
	int length = strlen(str1);
	for(int i = 0; i < length; i++) {
		if(tolower(str1[i]) != tolower(str2[i])) {
			return false;
		}
	}
	return true;
}

string reverseString(const char *str) {
	string rslt;
	if(str) {
		int length = strlen(str);
		for(int i = length - 1; i >= 0; i--) {
			rslt += str[i];
		}
	}
	return rslt;
}

string prepareQueryForPrintf(const char *query) {
	string rslt;
	if(query) {
		int length = strlen(query);
		for(int i = 0; i < length; i++) {
			rslt += query[i];
			if(query[i] == '%') {
				rslt += '%';
			}
		}
	}
	return rslt;
}

string prepareQueryForPrintf(string &query) {
	return(prepareQueryForPrintf(query.c_str()));
}


void SqlDb_mysql::createSchema(const char *host, const char *database, const char *user, const char *password) {
 
	bool federated = host && database && user;
	string federatedSuffix =federated ? "_fed" : "";
	string federatedConnection = federated ? 
					"FEDERATED CONNECTION=" +
					string("'mysql://") + 
					user + 
					(password ? string(":") + password : "") + 
					"@" + host +
					"/" + database + "/" :
					"";

	if(federated) {
		bool okFederated = false;
		this->query("show engines");
		SqlDb_row row;
		while(row = this->fetchRow()) {
			if(row["Engine"] == "FEDERATED" && row["Support"] == "YES") {
				okFederated = true;
			}
		}
		if(!okFederated) {
			syslog(LOG_ERR, "federated engine disabled");
			return;
		}
	}
  
	const char *cdrMainTables[] = {
		 "cdr",
		 "cdr_next",
		 "cdr_proxy",
		 "cdr_rtp",
		 "cdr_dtmf"
	};

	string compress = "";

	if(opt_mysqlcompress) {
		compress = "ROW_FORMAT=COMPRESSED";
	}

	syslog(LOG_DEBUG, federated ? "created federated tables" :  "creating and upgrading MySQL schema...");
	sql_disable_next_attempt_if_error = 1;
	this->multi_off();

	if(!federated) {
#if 1
	this->query(
	"CREATE TABLE IF NOT EXISTS `sensor_conf` (\
			`id` int NOT NULL AUTO_INCREMENT,\
			`id_sensor` int unsigned DEFAULT NULL,\
			`interface` varchar(255),\
			`threading_mod` tinyint DEFAULT 1,\
			`mirror_destination_ip` int unsigned DEFAULT NULL,\
			`mirror_destination_port` smallint unsigned DEFAULT NULL,\
			`mirror_bind_ip` int unsigned DEFAULT NULL,\
			`mirror_bind_port` smallint unsigned DEFAULT 5030,\
			`mirror_bind_dlt` int unsigned DEFAULT 1,\
			`scanpcapdir` varchar(255) DEFAULT NULL,\
			`scanpcapmethod` varchar(255) DEFAULT 'newfile',\
			`natalias` text DEFAULT NULL,\
			`sdp_reverse_ipport` tinyint DEFAULT 0,\
			`managerip` varchar(255) DEFAULT '127.0.0.1',\
			`httpport` varchar(255) DEFAULT NULL,\
			`sipport` varchar(255) DEFAULT '5060',\
			`cdr_sipport` tinyint DEFAULT 1,\
			`destination_number_mode` tinyint DEFAULT 1,\
			`onowaytimeout` int DEFAULT 15,\
			`rtptimeout` int DEFAULT 30,\
			`ringbuffer` int DEFAULT 50,\
			`packetbuffer_enable` tinyint DEFAULT 1,\
			`packetbuffer_total_maxheap` int DEFAULT 2000,\
			`packetbuffer_compress` tinyint DEFAULT 1,\
			`packetbuffer_file_totalmaxsize` tinyint DEFAULT 0,\
			`packetbuffer_file_path` varchar(255) DEFAULT '/var/spool/voipmonitor/packetbuffer',\
			`rtpthreads` int DEFAULT NULL,\
			`jitterbuffer_f1` tinyint DEFAULT 1,\
			`jitterbuffer_f2` tinyint DEFAULT 1,\
			`jitterbuffer_adapt` tinyint DEFAULT 1,\
			`callslimit` int DEFAULT 0,\
			`cdrproxy` tinyint DEFAULT 1,\
			`cdr_ua_enable` tinyint DEFAULT 1,\
			`rtp-firstleg` tinyint DEFAULT 0,\
			`allow-zerossrc` tinyint DEFAULT 0,\
			`deduplicate` tinyint DEFAULT 0,\
			`deduplicate_ipheader` tinyint DEFAULT 1,\
			`sipoverlap` tinyint DEFAULT 1,\
			`sip-register` tinyint DEFAULT 0,\
			`sip-register-active-nologbin` tinyint DEFAULT 1,\
			`nocdr` tinyint DEFAULT 0,\
			`skipdefault` tinyint DEFAULT 0,\
			`cdronlyanswered` tinyint DEFAULT 0,\
			`cdronlyrtp` tinyint DEFAULT 0,\
			`maxpcapsize` int unsigned DEFAULT NULL,\
			`savesip` tinyint DEFAULT 1,\
			`savertp` tinyint DEFAULT 1,\
			`pcapsplit` tinyint DEFAULT 1,\
			`savertcp` tinyint DEFAULT 1,\
			`saveaudio` varchar(255) DEFAULT NULL,\
			`saveaudio_reversestereo` tinyint DEFAULT 0,\
			`keycheck` text DEFAULT NULL,\
			`saverfc2833` tinyint DEFAULT 0,\
			`dtmf2db` tinyint DEFAULT 0,\
			`savegraph` varchar(255) DEFAULT 'plain',\
			`norecord-header` tinyint DEFAULT 0,\
			`norecord-dtmf` tinyint DEFAULT 0,\
			`pauserecordingdtmf` varchar(255) DEFAULT NULL,\
			`convert_dlt_sll2en10` tinyint DEFAULT 0,\
			`mos_g729` tinyint DEFAULT 0,\
			`mos_lqo` tinyint DEFAULT 0,\
			`mos_lqo_bin` varchar(255) DEFAULT 'pesq',\
			`mos_lqo_ref` varchar(255) DEFAULT '/usr/local/share/voipmonitor/audio/mos_lqe_original.wav',\
			`mos_lqo_ref16` varchar(255) DEFAULT '/usr/local/share/voipmonitor/audio/mos_lqe_original_16khz.wav',\
			`dscp` tinyint DEFAULT 1,\
			`custom_headers` text DEFAULT NULL,\
			`custom_headers_message` text DEFAULT NULL,\
			`matchheader` text DEFAULT NULL,\
			`domainport` tinyint DEFAULT 0,\
			`pcapcommand` text DEFAULT NULL,\
			`filtercommand` text DEFAULT NULL,\
			`filter` text DEFAULT NULL,\
			`openfile_max` int DEFAULT NULL,\
			`convertchar` text DEFAULT NULL,\
			`spooldir` varchar(255) DEFAULT '/var/spool/voipmonitor',\
			`spooldiroldschema` tinyint DEFAULT 0,\
			`cleandatabase_cdr` int DEFAULT 0,\
			`cleandatabase_register_failed` int DEFAULT 0,\
			`cleandatabase` int DEFAULT 0,\
			`maxpoolsize` int unsigned DEFAULT 102400,\
			`maxpooldays` int DEFAULT 0,\
			`maxpoolsipsize` int unsigned DEFAULT 0,\
			`maxpoolsipdays` int DEFAULT 0,\
			`maxpoolrtpsize` int unsigned DEFAULT 0,\
			`maxpoolrtpdays` int DEFAULT 0,\
			`maxpoolgraphsize` int unsigned DEFAULT 0,\
			`maxpoolgraphdays` int DEFAULT 0,\
			`cachedir` text DEFAULT NULL,\
			`promisc` tinyint DEFAULT 1,\
			`sqlcallend` tinyint DEFAULT 1,\
			`cdr_partition` tinyint DEFAULT 1,\
			`disable_partition_operations` tinyint DEFAULT 0,\
			`upgrade_try_http_if_https_fail` tinyint DEFAULT 1,\
			`opt_saveaudio_reversestereo` tinyint DEFAULT 0,\
			`onewaytimeout` smallint DEFAULT 15,\
			`sip-register-timeout` tinyint DEFAULT 5,\
		PRIMARY KEY (`id`)\
	) ENGINE=InnoDB DEFAULT CHARSET=latin1;");
#endif

	this->query(
	"CREATE TABLE IF NOT EXISTS `filter_ip` (\
			`id` int NOT NULL AUTO_INCREMENT,\
			`ip` int unsigned DEFAULT NULL,\
			`mask` int DEFAULT NULL,\
			`direction` tinyint DEFAULT NULL,\
			`rtp` tinyint DEFAULT NULL,\
			`sip` tinyint DEFAULT NULL,\
			`register` tinyint DEFAULT NULL,\
			`graph` tinyint DEFAULT NULL,\
			`wav` tinyint DEFAULT NULL,\
			`skip` tinyint DEFAULT NULL,\
			`script` tinyint DEFAULT NULL,\
			`mos_lqo` tinyint DEFAULT NULL,\
			`hide_message` tinyint DEFAULT NULL,\
			`note` text,\
			`remove_at` date default NULL,\
		PRIMARY KEY (`id`)\
	) ENGINE=InnoDB DEFAULT CHARSET=latin1;");

	this->query(
	"CREATE TABLE IF NOT EXISTS `filter_telnum` (\
			`id` int NOT NULL AUTO_INCREMENT,\
			`prefix` bigint unsigned DEFAULT NULL,\
			`fixed_len` int unsigned DEFAULT '0',\
			`direction` tinyint DEFAULT NULL,\
			`rtp` tinyint DEFAULT NULL,\
			`sip` tinyint DEFAULT NULL,\
			`register` tinyint DEFAULT NULL,\
			`graph` tinyint DEFAULT NULL,\
			`wav` tinyint DEFAULT NULL,\
			`skip` tinyint DEFAULT NULL,\
			`script` tinyint DEFAULT NULL,\
			`mos_lqo` tinyint DEFAULT NULL,\
			`hide_message` tinyint DEFAULT NULL,\
			`note` text,\
			`remove_at` date default NULL,\
		PRIMARY KEY (`id`)\
	) ENGINE=InnoDB DEFAULT CHARSET=latin1;");
	}

	this->query(
	"CREATE TABLE IF NOT EXISTS `filter_domain` (\
			`id` int NOT NULL AUTO_INCREMENT,\
			`domain` char(128) DEFAULT NULL,\
			`direction` tinyint DEFAULT NULL,\
			`rtp` tinyint DEFAULT NULL,\
			`sip` tinyint DEFAULT NULL,\
			`register` tinyint DEFAULT NULL,\
			`graph` tinyint DEFAULT NULL,\
			`wav` tinyint DEFAULT NULL,\
			`skip` tinyint DEFAULT NULL,\
			`script` tinyint DEFAULT NULL,\
			`mos_lqo` tinyint DEFAULT NULL,\
			`hide_message` tinyint DEFAULT NULL,\
			`note` text,\
			`remove_at` date default NULL,\
		PRIMARY KEY (`id`)\
	) ENGINE=InnoDB DEFAULT CHARSET=latin1;");
	
	this->query(string(
	"CREATE TABLE IF NOT EXISTS `cdr_sip_response") + federatedSuffix + "` (\
			`id` mediumint unsigned NOT NULL AUTO_INCREMENT,\
			`lastSIPresponse` varchar(255) DEFAULT NULL,\
		PRIMARY KEY (`id`),\
		UNIQUE KEY `lastSIPresponse` (`lastSIPresponse`)\
	) ENGINE=" + (federated ? federatedConnection + "cdr_sip_response'" : "InnoDB") + " DEFAULT CHARSET=latin1;");

	this->query(string(
	"CREATE TABLE IF NOT EXISTS `cdr_ua") + federatedSuffix + "` (\
			`id` int unsigned NOT NULL AUTO_INCREMENT,\
			`ua` varchar(512) DEFAULT NULL,\
		PRIMARY KEY (`id`),\
		UNIQUE KEY `ua` (`ua`)\
	) ENGINE=" + (federated ? federatedConnection + "cdr_ua'" : "InnoDB") + " DEFAULT CHARSET=latin1 " + compress + ";");

	char partDayName[20] = "";
	char limitDay[20] = "";
	if(opt_cdr_partition && !federated) {
		time_t act_time = time(NULL);
		if(opt_create_old_partitions > 0) {
			act_time -= opt_create_old_partitions * 24 * 60 * 60;
		}
		struct tm *actTime = localtime(&act_time);
		strftime(partDayName, sizeof(partDayName), "p%y%m%d", actTime);
		time_t next_day_time = act_time + 24 * 60 * 60;
		struct tm *nextDayTime = localtime(&next_day_time);
		strftime(limitDay, sizeof(partDayName), "%Y-%m-%d", nextDayTime);
	}
	
	this->query("show tables like 'cdr'");
	int createdCdrTable = !this->fetchRow();
	
	this->query(string(
	"CREATE TABLE IF NOT EXISTS `cdr") + federatedSuffix + "` (\
			`ID` int unsigned NOT NULL AUTO_INCREMENT,\
			`calldate` datetime NOT NULL,\
			`callend` datetime NOT NULL,\
			`duration` mediumint unsigned DEFAULT NULL,\
			`connect_duration` mediumint unsigned DEFAULT NULL,\
			`progress_time` mediumint unsigned DEFAULT NULL,\
			`first_rtp_time` mediumint unsigned DEFAULT NULL,\
			`caller` varchar(255) DEFAULT NULL,\
			`caller_domain` varchar(255) DEFAULT NULL,\
			`caller_reverse` varchar(255) DEFAULT NULL,\
			`callername` varchar(255) DEFAULT NULL,\
			`callername_reverse` varchar(255) DEFAULT NULL,\
			`called` varchar(255) DEFAULT NULL,\
			`called_domain` varchar(255) DEFAULT NULL,\
			`called_reverse` varchar(255) DEFAULT NULL,\
			`sipcallerip` int unsigned DEFAULT NULL,\
			`sipcallerport` smallint unsigned DEFAULT NULL,\
			`sipcalledip` int unsigned DEFAULT NULL,\
			`sipcalledport` smallint unsigned DEFAULT NULL,\
			`whohanged` enum('caller','callee') DEFAULT NULL,\
			`bye` tinyint unsigned DEFAULT NULL,\
			`lastSIPresponse_id` mediumint unsigned DEFAULT NULL,\
			`lastSIPresponseNum` smallint unsigned DEFAULT NULL,\
			`sighup` tinyint DEFAULT NULL,\
			`dscp` int unsigned DEFAULT NULL,\
			`a_index` tinyint DEFAULT NULL,\
			`b_index` tinyint DEFAULT NULL,\
			`a_payload` int DEFAULT NULL,\
			`b_payload` int DEFAULT NULL,\
			`a_saddr` int unsigned DEFAULT NULL,\
			`b_saddr` int unsigned DEFAULT NULL,\
			`a_received` mediumint unsigned DEFAULT NULL,\
			`b_received` mediumint unsigned DEFAULT NULL,\
			`a_lost` mediumint unsigned DEFAULT NULL,\
			`b_lost` mediumint unsigned DEFAULT NULL,\
			`a_ua_id` int unsigned DEFAULT NULL,\
			`b_ua_id` int unsigned DEFAULT NULL,\
			`a_avgjitter_mult10` mediumint unsigned DEFAULT NULL,\
			`b_avgjitter_mult10` mediumint unsigned DEFAULT NULL,\
			`a_maxjitter` smallint unsigned DEFAULT NULL,\
			`b_maxjitter` smallint unsigned DEFAULT NULL,\
			`a_sl1` mediumint unsigned DEFAULT NULL,\
			`a_sl2` mediumint unsigned DEFAULT NULL,\
			`a_sl3` mediumint unsigned DEFAULT NULL,\
			`a_sl4` mediumint unsigned DEFAULT NULL,\
			`a_sl5` mediumint unsigned DEFAULT NULL,\
			`a_sl6` mediumint unsigned DEFAULT NULL,\
			`a_sl7` mediumint unsigned DEFAULT NULL,\
			`a_sl8` mediumint unsigned DEFAULT NULL,\
			`a_sl9` mediumint unsigned DEFAULT NULL,\
			`a_sl10` mediumint unsigned DEFAULT NULL,\
			`a_d50` mediumint unsigned DEFAULT NULL,\
			`a_d70` mediumint unsigned DEFAULT NULL,\
			`a_d90` mediumint unsigned DEFAULT NULL,\
			`a_d120` mediumint unsigned DEFAULT NULL,\
			`a_d150` mediumint unsigned DEFAULT NULL,\
			`a_d200` mediumint unsigned DEFAULT NULL,\
			`a_d300` mediumint unsigned DEFAULT NULL,\
			`b_sl1` mediumint unsigned DEFAULT NULL,\
			`b_sl2` mediumint unsigned DEFAULT NULL,\
			`b_sl3` mediumint unsigned DEFAULT NULL,\
			`b_sl4` mediumint unsigned DEFAULT NULL,\
			`b_sl5` mediumint unsigned DEFAULT NULL,\
			`b_sl6` mediumint unsigned DEFAULT NULL,\
			`b_sl7` mediumint unsigned DEFAULT NULL,\
			`b_sl8` mediumint unsigned DEFAULT NULL,\
			`b_sl9` mediumint unsigned DEFAULT NULL,\
			`b_sl10` mediumint unsigned DEFAULT NULL,\
			`b_d50` mediumint unsigned DEFAULT NULL,\
			`b_d70` mediumint unsigned DEFAULT NULL,\
			`b_d90` mediumint unsigned DEFAULT NULL,\
			`b_d120` mediumint unsigned DEFAULT NULL,\
			`b_d150` mediumint unsigned DEFAULT NULL,\
			`b_d200` mediumint unsigned DEFAULT NULL,\
			`b_d300` mediumint unsigned DEFAULT NULL,\
			`a_mos_lqo_mult10` tinyint unsigned DEFAULT NULL,\
			`b_mos_lqo_mult10` tinyint unsigned DEFAULT NULL,\
			`a_mos_f1_mult10` tinyint unsigned DEFAULT NULL,\
			`a_mos_f2_mult10` tinyint unsigned DEFAULT NULL,\
			`a_mos_adapt_mult10` tinyint unsigned DEFAULT NULL,\
			`b_mos_f1_mult10` tinyint unsigned DEFAULT NULL,\
			`b_mos_f2_mult10` tinyint unsigned DEFAULT NULL,\
			`b_mos_adapt_mult10` tinyint unsigned DEFAULT NULL,\
			`a_rtcp_loss` smallint unsigned DEFAULT NULL,\
			`a_rtcp_maxfr` smallint unsigned DEFAULT NULL,\
			`a_rtcp_avgfr_mult10` smallint unsigned DEFAULT NULL,\
			`a_rtcp_maxjitter` smallint unsigned DEFAULT NULL,\
			`a_rtcp_avgjitter_mult10` smallint unsigned DEFAULT NULL,\
			`b_rtcp_loss` smallint unsigned DEFAULT NULL,\
			`b_rtcp_maxfr` smallint unsigned DEFAULT NULL,\
			`b_rtcp_avgfr_mult10` smallint unsigned DEFAULT NULL,\
			`b_rtcp_maxjitter` smallint unsigned DEFAULT NULL,\
			`b_rtcp_avgjitter_mult10` smallint unsigned DEFAULT NULL,\
			`a_last_rtp_from_end` smallint unsigned DEFAULT NULL,\
			`b_last_rtp_from_end` smallint unsigned DEFAULT NULL,\
			`payload` int DEFAULT NULL,\
			`jitter_mult10` mediumint unsigned DEFAULT NULL,\
			`mos_min_mult10` tinyint unsigned DEFAULT NULL,\
			`a_mos_min_mult10` tinyint unsigned DEFAULT NULL,\
			`b_mos_min_mult10` tinyint unsigned DEFAULT NULL,\
			`packet_loss_perc_mult1000` mediumint unsigned DEFAULT NULL,\
			`a_packet_loss_perc_mult1000` mediumint unsigned DEFAULT NULL,\
			`b_packet_loss_perc_mult1000` mediumint unsigned DEFAULT NULL,\
			`delay_sum` mediumint unsigned DEFAULT NULL,\
			`a_delay_sum` mediumint unsigned DEFAULT NULL,\
			`b_delay_sum` mediumint unsigned DEFAULT NULL,\
			`delay_avg_mult100` mediumint unsigned DEFAULT NULL,\
			`a_delay_avg_mult100` mediumint unsigned DEFAULT NULL,\
			`b_delay_avg_mult100` mediumint unsigned DEFAULT NULL,\
			`delay_cnt` mediumint unsigned DEFAULT NULL,\
			`a_delay_cnt` mediumint unsigned DEFAULT NULL,\
			`b_delay_cnt` mediumint unsigned DEFAULT NULL,\
			`rtcp_avgfr_mult10` smallint unsigned DEFAULT NULL,\
			`rtcp_avgjitter_mult10` smallint unsigned DEFAULT NULL,\
			`lost` mediumint unsigned DEFAULT NULL,\
			`id_sensor` smallint unsigned DEFAULT NULL," + 
			(get_customers_pn_query[0] ?
				"`caller_customer_id` int DEFAULT NULL,\
				`caller_reseller_id` char(10) DEFAULT NULL,\
				`called_customer_id` int DEFAULT NULL,\
				`called_reseller_id` char(10) DEFAULT NULL," :
				"") +
		       "`price_operator_mult100` int unsigned DEFAULT NULL,\
			`price_operator_currency_id` tinyint unsigned DEFAULT NULL,\
			`price_customer_mult100` int unsigned DEFAULT NULL,\
			`price_customer_currency_id` tinyint unsigned DEFAULT NULL," + 
		(opt_cdr_partition ? 
			"PRIMARY KEY (`ID`, `calldate`)," :
			"PRIMARY KEY (`ID`),") + 
		"KEY `calldate` (`calldate`),\
		KEY `callend` (`callend`),\
		KEY `duration` (`duration`),\
		KEY `source` (`caller`),\
		KEY `source_reverse` (`caller_reverse`),\
		KEY `destination` (`called`),\
		KEY `destination_reverse` (`called_reverse`),\
		KEY `callername` (`callername`),\
		KEY `callername_reverse` (`callername_reverse`),\
		KEY `sipcallerip` (`sipcallerip`),\
		KEY `sipcalledip` (`sipcalledip`),\
		KEY `lastSIPresponseNum` (`lastSIPresponseNum`),\
		KEY `bye` (`bye`),\
		KEY `a_saddr` (`a_saddr`),\
		KEY `b_saddr` (`b_saddr`),\
		KEY `a_lost` (`a_lost`),\
		KEY `b_lost` (`b_lost`),\
		KEY `a_maxjitter` (`a_maxjitter`),\
		KEY `b_maxjitter` (`b_maxjitter`),\
		KEY `a_rtcp_loss` (`a_rtcp_loss`),\
		KEY `a_rtcp_maxfr` (`a_rtcp_maxfr`),\
		KEY `a_rtcp_maxjitter` (`a_rtcp_maxjitter`),\
		KEY `b_rtcp_loss` (`b_rtcp_loss`),\
		KEY `b_rtcp_maxfr` (`b_rtcp_maxfr`),\
		KEY `b_rtcp_maxjitter` (`b_rtcp_maxjitter`),\
		KEY `a_ua_id` (`a_ua_id`),\
		KEY `b_ua_id` (`b_ua_id`),\
		KEY `a_avgjitter_mult10` (`a_avgjitter_mult10`),\
		KEY `b_avgjitter_mult10` (`b_avgjitter_mult10`),\
		KEY `a_rtcp_avgjitter_mult10` (`a_rtcp_avgjitter_mult10`),\
		KEY `b_rtcp_avgjitter_mult10` (`b_rtcp_avgjitter_mult10`),\
		KEY `lastSIPresponse_id` (`lastSIPresponse_id`),\
		KEY `payload` (`payload`),\
		KEY `id_sensor` (`id_sensor`)" + 
		(get_customers_pn_query[0] ?
				",KEY `caller_customer_id` (`caller_customer_id`),\
				KEY `caller_reseller_id` (`caller_reseller_id`),\
				KEY `called_customer_id` (`called_customer_id`),\
				KEY `called_reseller_id` (`called_reseller_id`)" :
				"") +
		(opt_cdr_partition ?
			"" :
			",CONSTRAINT `cdr_ibfk_1` FOREIGN KEY (`lastSIPresponse_id`) REFERENCES `cdr_sip_response` (`id`) ON UPDATE CASCADE,\
			CONSTRAINT `cdr_ibfk_2` FOREIGN KEY (`a_ua_id`) REFERENCES `cdr_ua` (`id`) ON UPDATE CASCADE,\
			CONSTRAINT `cdr_ibfk_3` FOREIGN KEY (`b_ua_id`) REFERENCES `cdr_ua` (`id`) ON UPDATE CASCADE") +
	") ENGINE=" + (federated ? federatedConnection + "cdr'" : "InnoDB") + " DEFAULT CHARSET=latin1 " + compress +  
	(opt_cdr_partition && !federated ?
		(opt_cdr_partition_oldver ? 
			string(" PARTITION BY RANGE (to_days(calldate))(\
				 PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
			string(" PARTITION BY RANGE COLUMNS(calldate)(\
				 PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
		""));
	
	if(opt_cdr_partition && !federated) {
		bool okExplainPartition = false;
		bool existPartition = false;
		for(uint i = 0; i < sizeof(cdrMainTables)/sizeof(cdrMainTables[0]); i++) {
			this->query(string("EXPLAIN PARTITIONS SELECT * from ") + cdrMainTables[i] + " limit 1");
			SqlDb_row row;
			if((row = this->fetchRow())) {
				okExplainPartition = true;
				if(row["partitions"] != "") {
					existPartition = true;
					break;
				}
			}
		}
		if(okExplainPartition && !existPartition) {
			syslog(LOG_INFO, "disable opt_cdr_partition (tables cdr... does not have partitions)");
			opt_cdr_partition = 0;
		}
	}

	if(!federated && !opt_cdr_sipport) {
		this->query("show columns from cdr where Field='sipcallerport'");
		opt_cdr_sipport = this->fetchRow();
	}

	this->query("show columns from cdr where Field='a_last_rtp_from_end'");
	opt_last_rtp_from_end = this->fetchRow();
	if(!opt_last_rtp_from_end) {
		syslog(LOG_WARNING, "!!! Your database needs to be upgraded to support new features - ALTER TABLE cdr ADD a_last_rtp_from_end SMALLINT UNSIGNED DEFAULT NULL, ADD b_last_rtp_from_end SMALLINT UNSIGNED DEFAULT NULL;");
	}
	
	this->query("show columns from cdr where Field='price_operator_mult100'");
	if(!this->fetchRow()) {
		this->query("show tables like 'billing'");
		if(this->fetchRow()) {
			syslog(LOG_WARNING, "!!! You need to alter cdr database table and add new columns to support billing feature. "
					    "This operation can take hours based on ammount of data, CPU and I/O speed of your server. "
					    "The alter table will prevent the database to insert new rows and will probably block other operations. "
					    "It is recommended to alter the table in non working hours. "
					    "Login to the mysql voipmonitor database (mysql -uroot voipmonitor) and run on the CLI> "
					    "ALTER TABLE cdr ADD COLUMN price_operator_mult100 INT UNSIGNED, ADD COLUMN price_operator_currency_id TINYINT UNSIGNED, ADD COLUMN price_customer_mult100 INT UNSIGNED, ADD COLUMN price_customer_currency_id TINYINT UNSIGNED;");
		}
	}

	string cdrNextCustomFields;
	if(federated) {
		for(size_t iCustHeaders = 0; iCustHeaders < opt_custom_headers_cdr.size(); iCustHeaders++) {
			cdrNextCustomFields += "`" + opt_custom_headers_cdr[iCustHeaders][1] + "` VARCHAR(255)" + ",";
		}
	}
	this->query(string(
	"CREATE TABLE IF NOT EXISTS `cdr_next") + federatedSuffix + "` (\
			`cdr_ID` int unsigned NOT NULL," +
			(opt_cdr_partition ?
				"`calldate` datetime NOT NULL," :
				"") + 
			"`custom_header1` varchar(255) DEFAULT NULL,\
			`fbasename` varchar(255) DEFAULT NULL,\
			`match_header` VARCHAR(128) DEFAULT NULL,\
			`GeoPosition` varchar(255) DEFAULT NULL," +
			cdrNextCustomFields +
		(opt_cdr_partition ? 
			"PRIMARY KEY (`cdr_ID`, `calldate`)," :
			"PRIMARY KEY (`cdr_ID`),") +
		"KEY `fbasename` (`fbasename`)" + 
		(opt_cdr_partition ?
			"" :
			",CONSTRAINT `cdr_next_ibfk_1` FOREIGN KEY (`cdr_ID`) REFERENCES `cdr` (`ID`) ON DELETE CASCADE ON UPDATE CASCADE") +
	") ENGINE=" + (federated ? federatedConnection + "cdr_next'" : "InnoDB") + " DEFAULT CHARSET=latin1 " + compress +  
	(opt_cdr_partition && !federated ?
		(opt_cdr_partition_oldver ? 
			string(" PARTITION BY RANGE (to_days(calldate))(\
				 PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
			string(" PARTITION BY RANGE COLUMNS(calldate)(\
				 PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
		""));
	if(!federated) {
	sql_noerror = 1;
	for(size_t iCustHeaders = 0; iCustHeaders < opt_custom_headers_cdr.size(); iCustHeaders++) {
		this->query(string(
		"ALTER TABLE `cdr_next`\
			ADD COLUMN `") + opt_custom_headers_cdr[iCustHeaders][1] + "` VARCHAR(255);");
	}
	sql_noerror = 0;
	}

	this->query(string(
	"CREATE TABLE IF NOT EXISTS `cdr_proxy") + federatedSuffix + "` (\
			`ID` int unsigned NOT NULL AUTO_INCREMENT,\
			`cdr_ID` int unsigned NOT NULL,\
			`calldate` datetime NOT NULL,\
			`src` int unsigned DEFAULT NULL,\
			`dst` varchar(255) DEFAULT NULL," +
		(opt_cdr_partition ? 
			"PRIMARY KEY (`ID`, `calldate`)," :
			"PRIMARY KEY (`ID`),") +
		"KEY `cdr_ID` (`cdr_ID`)," + 
		"KEY `src` (`src`)," + 
		"KEY `dst` (`dst`)" + 
		(opt_cdr_partition ?
			"" :
			",CONSTRAINT `cdr_proxy_ibfk_1` FOREIGN KEY (`cdr_ID`) REFERENCES `cdr` (`ID`) ON DELETE CASCADE ON UPDATE CASCADE") +
	") ENGINE=" + (federated ? federatedConnection + "cdr_proxy'" : "InnoDB") + " DEFAULT CHARSET=latin1 " + compress  + 
	(opt_cdr_partition && !federated ?
		(opt_cdr_partition_oldver ? 
			string(" PARTITION BY RANGE (to_days(calldate))(\
				 PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
			string(" PARTITION BY RANGE COLUMNS(calldate)(\
				 PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
	""));

	this->query(string(
	"CREATE TABLE IF NOT EXISTS `cdr_rtp") + federatedSuffix + "` (\
			`ID` int unsigned NOT NULL AUTO_INCREMENT,\
			`cdr_ID` int unsigned NOT NULL," +
			(opt_cdr_partition ?
				"`calldate` datetime NOT NULL," :
				"") + 
			"`saddr` int unsigned DEFAULT NULL,\
			`daddr` int unsigned DEFAULT NULL,\
			`ssrc` int unsigned DEFAULT NULL,\
			`received` mediumint unsigned DEFAULT NULL,\
			`loss` mediumint unsigned DEFAULT NULL,\
			`firsttime` float DEFAULT NULL,\
			`payload` smallint unsigned DEFAULT NULL,\
			`maxjitter_mult10` smallint unsigned DEFAULT NULL," +
		(opt_cdr_partition ? 
			"PRIMARY KEY (`ID`, `calldate`)," :
			"PRIMARY KEY (`ID`),") +
		"KEY (`cdr_ID`)" + 
		(opt_cdr_partition ?
			"" :
			",CONSTRAINT `cdr_rtp_ibfk_1` FOREIGN KEY (`cdr_ID`) REFERENCES `cdr` (`ID`) ON DELETE CASCADE ON UPDATE CASCADE") +
	") ENGINE=" + (federated ? federatedConnection + "cdr_rtp'" : "InnoDB") + " DEFAULT CHARSET=latin1 " + compress +  
	(opt_cdr_partition && !federated ?
		(opt_cdr_partition_oldver ? 
			string(" PARTITION BY RANGE (to_days(calldate))(\
				 PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
			string(" PARTITION BY RANGE COLUMNS(calldate)(\
				 PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
		""));

	if(!federated && !opt_cdr_rtpport) {
		this->query("show columns from cdr_rtp where Field='dport'");
		opt_cdr_rtpport = this->fetchRow();
	}

	this->query(string(
	"CREATE TABLE IF NOT EXISTS `cdr_dtmf") + federatedSuffix + "` (\
			`ID` int unsigned NOT NULL AUTO_INCREMENT,\
			`cdr_ID` int unsigned NOT NULL," +
			(opt_cdr_partition ?
				"`calldate` datetime NOT NULL," :
				"") + 
			"`daddr` int unsigned DEFAULT NULL,\
			`saddr` int unsigned DEFAULT NULL,\
			`firsttime` float DEFAULT NULL,\
			`dtmf` char DEFAULT NULL," +
		(opt_cdr_partition ? 
			"PRIMARY KEY (`ID`, `calldate`)," :
			"PRIMARY KEY (`ID`),") +
		"KEY (`cdr_ID`)" + 
		(opt_cdr_partition ?
			"" :
			",CONSTRAINT `cdr_dtmf_ibfk_1` FOREIGN KEY (`cdr_ID`) REFERENCES `cdr` (`ID`) ON DELETE CASCADE ON UPDATE CASCADE") +
	") ENGINE=" + (federated ? federatedConnection + "cdr_dtmf'" : "InnoDB") + " DEFAULT CHARSET=latin1" + 
	(opt_cdr_partition && !federated ?
		(opt_cdr_partition_oldver ? 
			string(" PARTITION BY RANGE (to_days(calldate))(\
				 PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
			string(" PARTITION BY RANGE COLUMNS(calldate)(\
				 PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
		""));

	this->query(string(
	"CREATE TABLE IF NOT EXISTS `contenttype") + federatedSuffix + "` (\
			`id` int unsigned NOT NULL AUTO_INCREMENT,\
			`contenttype` varchar(255) DEFAULT NULL,\
		PRIMARY KEY (`id`),\
		KEY `contenttype` (`contenttype`)\
	) ENGINE=" + (federated ? federatedConnection + "contenttype'" : "InnoDB") + " DEFAULT CHARSET=utf8 " + compress + ";");

	string messageNextCustomFields;
	if(federated) {
		for(size_t iCustHeaders = 0; iCustHeaders < opt_custom_headers_message.size(); iCustHeaders++) {
			messageNextCustomFields += "`" + opt_custom_headers_message[iCustHeaders][1] + "` VARCHAR(255)" + ",";
		}
	}
	this->query(string(
	"CREATE TABLE IF NOT EXISTS `message") + federatedSuffix + "` (\
			`ID` int unsigned NOT NULL AUTO_INCREMENT,\
			`id_contenttype` int unsigned NOT NULL,\
			`calldate` datetime NOT NULL,\
			`caller` varchar(255) DEFAULT NULL,\
			`caller_domain` varchar(255) DEFAULT NULL,\
			`caller_reverse` varchar(255) DEFAULT NULL,\
			`callername` varchar(255) DEFAULT NULL,\
			`callername_reverse` varchar(255) DEFAULT NULL,\
			`called` varchar(255) DEFAULT NULL,\
			`called_domain` varchar(255) DEFAULT NULL,\
			`called_reverse` varchar(255) DEFAULT NULL,\
			`sipcallerip` int unsigned DEFAULT NULL,\
			`sipcalledip` int unsigned DEFAULT NULL,\
			`bye` tinyint unsigned DEFAULT NULL,\
			`lastSIPresponse_id` mediumint unsigned DEFAULT NULL,\
			`lastSIPresponseNum` smallint unsigned DEFAULT NULL,\
			`id_sensor` smallint unsigned DEFAULT NULL,\
			`a_ua_id` int unsigned DEFAULT NULL,\
			`b_ua_id` int unsigned DEFAULT NULL,\
			`fbasename` varchar(255) DEFAULT NULL,\
			`message` MEDIUMTEXT CHARACTER SET utf8,\
			`content_length` MEDIUMINT DEFAULT NULL," +
			messageNextCustomFields +
		(opt_cdr_partition ? 
			"PRIMARY KEY (`ID`, `calldate`)," :
			"PRIMARY KEY (`ID`),") + 
		"KEY `id_contenttype` (`id_contenttype`),\
		KEY `calldate` (`calldate`),\
		KEY `caller` (`caller`),\
		KEY `caller_domain` (`caller_domain`),\
		KEY `caller_reverse` (`caller_reverse`),\
		KEY `callername` (`callername`),\
		KEY `callername_reverse` (`callername_reverse`),\
		KEY `called` (`called`),\
		KEY `called_reverse` (`called_reverse`),\
		KEY `sipcallerip` (`sipcallerip`),\
		KEY `sipcalledip` (`sipcalledip`),\
		KEY `lastSIPresponseNum` (`lastSIPresponseNum`),\
		KEY `bye` (`bye`),\
		KEY `lastSIPresponse_id` (`lastSIPresponse_id`),\
		KEY `id_sensor` (`id_sensor`),\
		KEY `a_ua_id` (`a_ua_id`),\
		KEY `b_ua_id` (`b_ua_id`),\
		KEY `fbasename` (`fbasename`)" +
		(opt_cdr_partition ?
			"" :
			",CONSTRAINT `messages_ibfk_1` FOREIGN KEY (`lastSIPresponse_id`) REFERENCES `cdr_sip_response` (`id`) ON UPDATE CASCADE,\
			CONSTRAINT `messages_ibfk_2` FOREIGN KEY (`a_ua_id`) REFERENCES `cdr_ua` (`id`) ON UPDATE CASCADE,\
			CONSTRAINT `messages_ibfk_3` FOREIGN KEY (`b_ua_id`) REFERENCES `cdr_ua` (`id`) ON UPDATE CASCADE,\
			CONSTRAINT `messages_ibfk_4` FOREIGN KEY (`id_contenttype`) REFERENCES `contenttype` (`id`) ON UPDATE CASCADE") +
	") ENGINE=" + (federated ? federatedConnection + "message'" : "InnoDB") + " DEFAULT CHARSET=latin1 " + compress + 
	(opt_cdr_partition && !federated ?
		(opt_cdr_partition_oldver ? 
			string(" PARTITION BY RANGE (to_days(calldate))(\
				 PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
			string(" PARTITION BY RANGE COLUMNS(calldate)(\
				 PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
		""));
	if(!federated) {
	sql_noerror = 1;
	for(size_t iCustHeaders = 0; iCustHeaders < opt_custom_headers_message.size(); iCustHeaders++) {
		this->query(string(
		"ALTER TABLE `message`\
			ADD COLUMN `") + opt_custom_headers_message[iCustHeaders][1] + "` VARCHAR(255);");
	}
	sql_noerror = 0;
	}

	if(!federated) {
		this->query("show columns from message where Field='content_length'");
		exists_column_message_content_length = this->fetchRow();
	}

	if(!federated) {
	this->query(
	"CREATE TABLE IF NOT EXISTS `register` (\
			`ID` int unsigned NOT NULL AUTO_INCREMENT,\
			`id_sensor` int unsigned NOT NULL,\
			`fname` BIGINT NULL default NULL,\
			`calldate` datetime NOT NULL,\
			`sipcallerip` int unsigned NOT NULL,\
			`sipcalledip` int unsigned NOT NULL,\
			`from_num` varchar(255) NULL DEFAULT NULL,\
			`from_name` varchar(255) NULL DEFAULT NULL,\
			`from_domain` varchar(255) NULL DEFAULT NULL,\
			`to_num` varchar(255) NULL DEFAULT NULL,\
			`to_domain` varchar(255) NULL DEFAULT NULL,\
			`contact_num` varchar(255) NULL DEFAULT NULL,\
			`contact_domain` varchar(255) NULL DEFAULT NULL,\
			`digestusername` varchar(255) NULL DEFAULT NULL,\
			`digestrealm` varchar(255) NULL DEFAULT NULL,\
			`expires`     mediumint NULL DEFAULT NULL,\
			`expires_at`  datetime NULL DEFAULT NULL,\
			`state`  tinyint unsigned NULL DEFAULT NULL,\
			`ua_id` int unsigned DEFAULT NULL,\
		PRIMARY KEY (`ID`),\
		KEY `calldate` (`calldate`),\
		KEY `sipcallerip` (`sipcallerip`),\
		KEY `sipcalledip` (`sipcalledip`),\
		KEY `from_num` (`from_num`),\
		KEY `digestusername` (`digestusername`)\
	) ENGINE=MEMORY DEFAULT CHARSET=latin1 " + compress + ";");
	}

	this->query(string(
	"CREATE TABLE IF NOT EXISTS `register_state") + federatedSuffix + "` (\
			`ID` int unsigned NOT NULL AUTO_INCREMENT,\
			`id_sensor` int unsigned NOT NULL,\
			`fname` BIGINT NULL default NULL,\
			`created_at` datetime NOT NULL,\
			`sipcallerip` int unsigned NOT NULL,\
			`sipcalledip` int unsigned NOT NULL,\
			`from_num` varchar(255) NULL DEFAULT NULL,\
			`to_num` varchar(255) NULL DEFAULT NULL,\
			`contact_num` varchar(255) NULL DEFAULT NULL,\
			`contact_domain` varchar(255) NULL DEFAULT NULL,\
			`digestusername` varchar(255) NULL DEFAULT NULL,\
			`expires` mediumint NULL DEFAULT NULL,\
			`state` tinyint unsigned NULL DEFAULT NULL,\
			`ua_id` int unsigned DEFAULT NULL,\
			`to_domain` varchar(255) NULL DEFAULT NULL," +
		(opt_cdr_partition ? 
			"PRIMARY KEY (`ID`, `created_at`)," :
			"PRIMARY KEY (`ID`),") +
		"KEY `created_at` (`created_at`),\
		KEY `sipcallerip` (`sipcallerip`),\
		KEY `sipcalledip` (`sipcalledip`)\
	) ENGINE=" + (federated ? federatedConnection + "register_state'" : "InnoDB") + " DEFAULT CHARSET=latin1 " + compress +  
	(opt_cdr_partition && !federated ?
		(opt_cdr_partition_oldver ? 
			string(" PARTITION BY RANGE (to_days(created_at))(\
				 PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
			string(" PARTITION BY RANGE COLUMNS(created_at)(\
				 PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
	""));

	this->query(string(
	"CREATE TABLE IF NOT EXISTS `register_failed") + federatedSuffix + "` (\
			`ID` int unsigned NOT NULL AUTO_INCREMENT,\
			`id_sensor` int unsigned NOT NULL,\
			`fname` BIGINT NULL default NULL,\
			`counter` int DEFAULT 0,\
			`created_at` datetime NOT NULL,\
			`sipcallerip` int unsigned NOT NULL,\
			`sipcalledip` int unsigned NOT NULL,\
			`from_num` varchar(255) NULL DEFAULT NULL,\
			`to_num` varchar(255) NULL DEFAULT NULL,\
			`contact_num` varchar(255) NULL DEFAULT NULL,\
			`contact_domain` varchar(255) NULL DEFAULT NULL,\
			`digestusername` varchar(255) NULL DEFAULT NULL,\
			`ua_id` int unsigned DEFAULT NULL,\
			`to_domain` varchar(255) NULL DEFAULT NULL," +
		(opt_cdr_partition ? 
			"PRIMARY KEY (`ID`, `created_at`)," :
			"PRIMARY KEY (`ID`),") +
		"KEY `created_at` (`created_at`),\
		KEY `sipcallerip` (`sipcallerip`),\
		KEY `sipcalledip` (`sipcalledip`)\
	) ENGINE=" + (federated ? federatedConnection + "register_failed'" : "InnoDB") + " DEFAULT CHARSET=latin1 " + compress +  
	(opt_cdr_partition && !federated ?
		(opt_cdr_partition_oldver ? 
			string(" PARTITION BY RANGE (to_days(created_at))(\
				 PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
			string(" PARTITION BY RANGE COLUMNS(created_at)(\
				 PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
	""));

	if(!federated) {
	this->query("CREATE TABLE IF NOT EXISTS `sensors` (\
			`id_sensor` int unsigned NOT NULL,\
			`host` varchar(255) NULL DEFAULT NULL,\
			`port` int NULL DEFAULT NULL,\
		PRIMARY KEY (`id_sensor`)\
	) ENGINE=InnoDB DEFAULT CHARSET=latin1;");

	if(opt_ipaccount) {
	this->query(
	"CREATE TABLE IF NOT EXISTS `ipacc` (\
			`saddr` int unsigned NOT NULL,\
			`src_id_customer` int unsigned NOT NULL DEFAULT 0,\
			`daddr` int unsigned NOT NULL,\
			`dst_id_customer` int unsigned NOT NULL DEFAULT 0,\
			`port` smallint unsigned NOT NULL,\
			`proto` smallint unsigned NOT NULL,\
			`octects` int unsigned NOT NULL,\
			`numpackets` mediumint unsigned NOT NULL,\
			`interval_time` datetime NOT NULL,\
			`voip` tinyint unsigned NOT NULL DEFAULT 0,\
			`do_agr_trigger` tinyint NOT NULL DEFAULT 0,\
		KEY `saddr` (`saddr`),\
		KEY `src_id_customer` (`src_id_customer`),\
		KEY `daddr` (`daddr`),\
		KEY `dst_id_customer` (`dst_id_customer`),\
		KEY `port` (`port`),\
		KEY `proto` (`proto`),\
		KEY `interval_time` (`interval_time`)\
	) ENGINE=InnoDB DEFAULT CHARSET=latin1 " + compress +
			string(" PARTITION BY RANGE COLUMNS(interval_time)(\
				 PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)");
	}

	this->query(
	"CREATE TABLE IF NOT EXISTS `livepacket` (\
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT ,\
			`id_sensor` INT DEFAULT NULL,\
			`sipcallerip` INT UNSIGNED NOT NULL ,\
			`sipcalledip` INT UNSIGNED NOT NULL ,\
			`sport` SMALLINT UNSIGNED NOT NULL ,\
			`dport` SMALLINT UNSIGNED NOT NULL ,\
			`istcp` TINYINT UNSIGNED NOT NULL ,\
			`created_at` TIMESTAMP NOT NULL ,\
			`microseconds` INT UNSIGNED NOT NULL ,\
			`callid` VARCHAR(255) NOT NULL ,\
			`description` VARCHAR(1024),\
			`data` VARBINARY(10000) NOT NULL ,\
		PRIMARY KEY ( `id` ) ,\
		INDEX (`created_at` , `microseconds`)\
	) ENGINE=MEMORY DEFAULT CHARSET=latin1 " + compress + ";");
	
	this->query(
	"CREATE TABLE IF NOT EXISTS `files` (\
			`datehour` int NOT NULL,\
			`id_sensor` int unsigned DEFAULT NULL,\
			`sipsize` bigint unsigned DEFAULT 0,\
			`rtpsize` bigint unsigned DEFAULT 0,\
			`graphsize` bigint unsigned DEFAULT 0,\
			`audiosize` bigint unsigned DEFAULT 0,\
			`regsize` bigint unsigned DEFAULT 0,\
		PRIMARY KEY (`datehour`, `id_sensor`)\
	) ENGINE=InnoDB DEFAULT CHARSET=latin1;");
	}
	
	if(opt_enable_http_enum_tables) {
		this->query(string(
		"CREATE TABLE IF NOT EXISTS `http_jj") + federatedSuffix + "` (\
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,\
			`master_id` INT UNSIGNED,\
			`timestamp` DATETIME NOT NULL,\
			`usec` INT UNSIGNED NOT NULL,\
			`srcip` INT UNSIGNED NOT NULL,\
			`dstip` INT UNSIGNED NOT NULL,\
			`srcport` SMALLINT UNSIGNED DEFAULT NULL,\
			`dstport` SMALLINT UNSIGNED DEFAULT NULL,\
			`url` TEXT NOT NULL,\
			`type` ENUM('http_ok') DEFAULT NULL,\
			`http` TEXT CHARACTER SET utf8 COLLATE utf8_bin NOT NULL,\
			`body` TEXT CHARACTER SET utf8 COLLATE utf8_bin NOT NULL,\
			`callid` VARCHAR( 255 ) NOT NULL,\
			`sessid` VARCHAR( 255 ) NOT NULL,\
			`external_transaction_id` varchar( 255 ) NOT NULL,\
			`id_sensor` smallint DEFAULT NULL," +
		(opt_cdr_partition ? 
			"PRIMARY KEY (`id`, `timestamp`)," :
			"PRIMARY KEY (`id`),") + 
		"KEY `timestamp` (`timestamp`),\
		KEY `callid` (`callid`),\
		KEY `sessid` (`sessid`),\
		KEY `external_transaction_id` (`external_transaction_id`)," +
		(opt_cdr_partition ? 
			"KEY `master_id` (`master_id`)" :
			"CONSTRAINT fk__http_jj__master_id\
				FOREIGN KEY (`master_id`) REFERENCES `http_jj` (`id`)\
				ON DELETE CASCADE ON UPDATE CASCADE") +
		") ENGINE = " + (federated ? federatedConnection + "http_jj'" : "InnoDB") + " " + compress +
		(opt_cdr_partition && !federated ?
			(opt_cdr_partition_oldver ? 
				string(" PARTITION BY RANGE (to_days(timestamp))(\
					PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
				string(" PARTITION BY RANGE COLUMNS(timestamp)(\
					PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
			""));
		
		this->query(string(
		"CREATE TABLE IF NOT EXISTS `enum_jj") + federatedSuffix + "` (\
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,\
			`dnsid` INT UNSIGNED NOT NULL,\
			`timestamp` DATETIME NOT NULL,\
			`usec` INT UNSIGNED NOT NULL,\
			`srcip` INT UNSIGNED NOT NULL,\
			`dstip` INT UNSIGNED NOT NULL,\
			`isresponse` TINYINT NOT NULL,\
			`recordtype` SMALLINT NOT NULL,\
			`queryname` VARCHAR(255) NOT NULL,\
			`responsename` VARCHAR(255) NOT NULL,\
			`data` BLOB NOT NULL,\
			`id_sensor` smallint DEFAULT NULL," +
		(opt_cdr_partition ? 
			"PRIMARY KEY (`id`, `timestamp`)," :
			"PRIMARY KEY (`id`),") + 
		"KEY `timestamp` (`timestamp`),\
		KEY `dnsid` (`dnsid`),\
		KEY `queryname` (`queryname`),\
		KEY `responsename` (`responsename`)\
		) ENGINE = " + (federated ? federatedConnection + "enum_jj'" : "InnoDB") + " " + compress +
		(opt_cdr_partition && !federated ?
			(opt_cdr_partition_oldver ? 
				string(" PARTITION BY RANGE (to_days(timestamp))(\
					PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
				string(" PARTITION BY RANGE COLUMNS(timestamp)(\
					PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
			""));
	}

	if(opt_enable_webrtc_table) {
		this->query(string(
		"CREATE TABLE IF NOT EXISTS `webrtc") + federatedSuffix + "` (\
			`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,\
			`timestamp` DATETIME NOT NULL,\
			`usec` INT UNSIGNED NOT NULL,\
			`srcip` INT UNSIGNED NOT NULL,\
			`dstip` INT UNSIGNED NOT NULL,\
			`srcport` SMALLINT UNSIGNED DEFAULT NULL,\
			`dstport` SMALLINT UNSIGNED DEFAULT NULL,\
			`type` ENUM('http', 'http_resp', 'websocket', 'websocket_resp') DEFAULT NULL,\
			`method` VARCHAR(32) DEFAULT NULL,\
			`body` TEXT CHARACTER SET utf8 COLLATE utf8_bin NOT NULL,\
			`external_transaction_id` VARCHAR( 255 ) NOT NULL,\
			`id_sensor` smallint DEFAULT NULL," +
		(opt_cdr_partition ? 
			"PRIMARY KEY (`id`, `timestamp`)," :
			"PRIMARY KEY (`id`),") + 
		"KEY `timestamp` (`timestamp`),\
		KEY `external_transaction_id` (`external_transaction_id`)\
		) ENGINE = " + (federated ? federatedConnection + "http_jj'" : "InnoDB") + " " + compress +
		(opt_cdr_partition && !federated ?
			(opt_cdr_partition_oldver ? 
				string(" PARTITION BY RANGE (to_days(timestamp))(\
					PARTITION ") + partDayName + " VALUES LESS THAN (to_days('" + limitDay + "')) engine innodb)" :
				string(" PARTITION BY RANGE COLUMNS(timestamp)(\
					PARTITION ") + partDayName + " VALUES LESS THAN ('" + limitDay + "') engine innodb)") :
			""));
	}

	if(opt_enable_fraud) {
	this->query(
	"CREATE TABLE IF NOT EXISTS `cache_number_location` (\
			`number` varchar(30) NOT NULL,\
			`number_ip` int unsigned DEFAULT NULL,\
			`ip` int unsigned,\
			`country_code` char(5),\
			`continent_code` char(5),\
			`at` bigint unsigned,\
			`old_ip` int unsigned,\
			`old_country_code` char(5),\
			`old_continent_code` char(5),\
			`old_at` bigint unsigned,\
		PRIMARY KEY (`number`, `number_ip`)\
	) ENGINE=InnoDB DEFAULT CHARSET=latin1;");
	this->createTable("fraud_alert_info");
	}
	
	if(!federated) {
	//BEGIN ALTER TABLES
	char alter_funcname[20];
	sprintf(alter_funcname, "__alter");
	if(opt_id_sensor > -1) {
		sprintf(alter_funcname + strlen(alter_funcname), "_S%i", opt_id_sensor);
	}
	this->query(string("drop procedure if exists ") + alter_funcname);
	ostringstream outStrAlter;
	outStrAlter << "create procedure " << alter_funcname << "() begin" << endl
		    << "DECLARE CONTINUE HANDLER FOR SQLSTATE '42S21' BEGIN END;" << endl
		    << "DECLARE CONTINUE HANDLER FOR SQLSTATE '42000' BEGIN END;" << endl;
	
	//5.2 -> 5.3
	if(opt_match_header[0] != '\0') {
		outStrAlter << "ALTER TABLE cdr_next\
				ADD match_header VARCHAR(128),\
				ADD KEY `match_header` (`match_header`);" << endl;
	}
	//5.3 -> 5.4
	outStrAlter << "ALTER TABLE register\
			ADD KEY `to_domain` (`to_domain`),\
			ADD KEY `to_num` (`to_num`);" << endl;
	outStrAlter << "ALTER TABLE register_state\
			ADD `to_domain` varchar(255) NULL DEFAULT NULL;" << endl;
	outStrAlter << "ALTER TABLE register_failed\
			ADD `to_domain` varchar(255) NULL DEFAULT NULL;" << endl;
	//5.4 -> 5.5
	outStrAlter << "ALTER TABLE register_state\
			ADD `sipcalledip` int unsigned,\
			ADD KEY `sipcalledip` (`sipcalledip`);" << endl;
	outStrAlter << "ALTER TABLE register_failed\
			ADD `sipcalledip` int unsigned,\
			ADD KEY `sipcalledip` (`sipcalledip`);" << endl;
	//6.0 -> 6.1
	outStrAlter << "ALTER TABLE message\
			ADD id_contenttype INT AFTER ID,\
			ADD KEY `id_contenttype` (`id_contenttype`);" << endl;
	
	//6.5RC2 ->
	outStrAlter << "ALTER TABLE message ADD GeoPosition varchar(255);" << endl;
	outStrAlter << "ALTER TABLE cdr_next ADD GeoPosition varchar(255);" << endl;
	outStrAlter << "ALTER TABLE register\
			ADD `fname` BIGINT NULL DEFAULT NULL;" << endl;
	outStrAlter << "ALTER TABLE register_failed\
			ADD `fname` BIGINT NULL DEFAULT NULL;" << endl;
	outStrAlter << "ALTER TABLE register_state\
			ADD `fname` BIGINT NULL DEFAULT NULL;" << endl;
	outStrAlter << "ALTER TABLE register\
			ADD `id_sensor` INT NULL DEFAULT NULL;" << endl;
	outStrAlter << "ALTER TABLE register_failed\
			ADD `id_sensor` INT NULL DEFAULT NULL;" << endl;
	outStrAlter << "ALTER TABLE register_state\
			ADD `id_sensor` INT NULL DEFAULT NULL;" << endl;

	outStrAlter << "ALTER TABLE filter_ip\
			ADD `skip` tinyint NULL;" << endl;
	outStrAlter << "ALTER TABLE filter_telnum\
			ADD `skip` tinyint NULL;" << endl;
	//8.0
	if(opt_dscp) {
		outStrAlter << "ALTER TABLE cdr ADD dscp int unsigned DEFAULT NULL;" << endl;
	}
	
	if(opt_enable_http_enum_tables) {
		outStrAlter << "ALTER TABLE http_jj\
				ADD external_transaction_id varchar( 255 ) NOT NULL,\
				ADD KEY `external_transaction_id` (`external_transaction_id`);" << endl;
		outStrAlter << "ALTER TABLE http_jj ADD type ENUM('http_ok') DEFAULT NULL AFTER url;" << endl;
		outStrAlter << "ALTER TABLE http_jj ADD http TEXT CHARACTER SET utf8 COLLATE utf8_bin NOT NULL AFTER type;" << endl;
		outStrAlter << "ALTER TABLE http_jj ADD id_sensor SMALLINT DEFAULT NULL;" << endl;
		outStrAlter << "ALTER TABLE enum_jj ADD id_sensor SMALLINT DEFAULT NULL;" << endl;
	}

	//8.2
	outStrAlter << "ALTER TABLE filter_ip\
			ADD `script` tinyint NULL;" << endl;
	outStrAlter << "ALTER TABLE filter_telnum\
			ADD `script` tinyint NULL;" << endl;

	outStrAlter << "ALTER TABLE filter_ip\
			ADD `mos_lqo` tinyint NULL;" << endl;
	outStrAlter << "ALTER TABLE filter_telnum\
			ADD `mos_lqo` tinyint NULL;" << endl;
	
	outStrAlter << "ALTER TABLE files\
			ADD `regsize` bigint unsigned DEFAULT 0;" << endl;
	
	//8.4
	if(opt_cdr_sipport) {
		outStrAlter << "ALTER TABLE cdr\
				ADD `sipcallerport` smallint unsigned DEFAULT NULL AFTER `sipcallerip`,\
				ADD `sipcalledport` smallint unsigned DEFAULT NULL AFTER `sipcalledip`;" << endl;
	}

	if(opt_mos_lqo) {
		outStrAlter << "ALTER TABLE cdr\
				ADD `a_mos_lqo_mult10` tinyint unsigned DEFAULT NULL,\
				ADD `b_mos_lqo_mult10` tinyint unsigned DEFAULT NULL;" << endl;
	}

	if(opt_cdr_rtpport) {
		outStrAlter << "ALTER TABLE cdr_rtp\
				ADD `dport` smallint unsigned DEFAULT NULL AFTER `daddr`;" << endl;
	}


	//9.4
	outStrAlter << "ALTER TABLE sensor_conf ADD `sip-register-timeout` tinyint DEFAULT 5;" << endl;

	//
	outStrAlter << "ALTER TABLE filter_ip\
			ADD `hide_message` tinyint default NULL;" << endl;
	outStrAlter << "ALTER TABLE filter_telnum\
			ADD `hide_message` tinyint default NULL;" << endl;
	outStrAlter << "ALTER TABLE filter_ip\
			ADD `remove_at` date default NULL;" << endl;
	outStrAlter << "ALTER TABLE filter_telnum\
			ADD `remove_at` date default NULL;" << endl;

	//10.0.5
	if(opt_enable_fraud) {
	outStrAlter << "ALTER TABLE cache_number_location\
			ADD `number_ip` int unsigned DEFAULT NULL AFTER number,\
			DROP PRIMARY KEY,\
			ADD PRIMARY KEY (`number`, `number_ip`);" << endl;
	}
	
	// drop old cdr trigger
	outStrAlter << "drop trigger if exists cdr_bi;" << endl;

	outStrAlter << "end;" << endl;
	/*
	cout << "alter procedure" << endl
	     << outStrAlter.str() << endl
	     << "---" << endl;
	*/
	this->query(outStrAlter.str());
	this->query(string("call ") + alter_funcname);
	this->query(string("drop procedure if exists ") + alter_funcname);
	//END ALTER TABLES
	
	//BEGIN SQL SCRIPTS
	if((opt_cdr_partition || opt_ipaccount) && !opt_disable_partition_operations) {
		if(!cloud_host.empty()) {
			this->createProcedure(string(
			"begin\
			    declare part_date date;\
			    declare part_limit date;\
			    declare part_limit_int int;\
			    declare part_name char(100);\
			    declare create_part_query varchar(1000);\
			    set part_date =  date_add(date(now()), interval next_days day);\
			    if(type_part = 'month') then\
			       set part_date = date_add(part_date, interval -(day(part_date)-1) day);\
			       set part_limit = date_add(part_date, interval 1 month);\
			       set part_name = concat('p', date_format(part_date, '%y%m'));\
			    else\
			       set part_limit = date_add(part_date, interval 1 day);\
			       set part_name = concat('p', date_format(part_date, '%y%m%d'));\
			    end if;\
			    set part_limit_int = to_days(part_limit);\
			    set create_part_query = concat(\
			       'alter table `',\
			       table_name,\
			       '` add partition (partition ',\
			       part_name,") + 
			       (opt_cdr_partition_oldver ? 
				     "' VALUES LESS THAN (',\
				      part_limit_int,\
				      '))'" :
				     "' VALUES LESS THAN (\\'',\
				      part_limit,\
				      '\\'))'") + 
			       ");\
			    set @_create_part_query = create_part_query;\
			    prepare stmt FROM @_create_part_query;\
			    execute stmt;\
			    deallocate prepare stmt;\
			 end",
			"create_partition", "(table_name char(100), type_part char(10), next_days int)");
		} else {
			this->createProcedure(string(
			"begin\
			    declare part_date date;\
			    declare part_limit date;\
			    declare part_limit_int int;\
			    declare part_name char(100);\
			    declare test_exists_any_part_query varchar(1000);\
			    declare test_exists_part_query varchar(1000);\
			    declare create_part_query varchar(1000);\
			    set test_exists_any_part_query = concat(\
			       'set @_exists_any_part = exists (select * from information_schema.partitions where table_schema=\\'',\
			       database_name,\
			       '\\' and table_name = \\'',\
			       table_name,\
			       '\\' and partition_name is not null)');\
			    set @_test_exists_any_part_query = test_exists_any_part_query;\
			    prepare stmt FROM @_test_exists_any_part_query;\
			    execute stmt;\
			    deallocate prepare stmt;\
			    if(@_exists_any_part) then\
			       set part_date =  date_add(date(now()), interval next_days day);\
			       if(type_part = 'month') then\
				  set part_date = date_add(part_date, interval -(day(part_date)-1) day);\
				  set part_limit = date_add(part_date, interval 1 month);\
				  set part_name = concat('p', date_format(part_date, '%y%m'));\
			       else\
				  set part_limit = date_add(part_date, interval 1 day);\
				  set part_name = concat('p', date_format(part_date, '%y%m%d'));\
			       end if;\
			       set part_limit_int = to_days(part_limit);\
			       set test_exists_part_query = concat(\
				  'set @_exists_part = exists (select * from information_schema.partitions where table_schema=\\'',\
				  database_name,\
				  '\\' and table_name = \\'',\
				  table_name,\
				  '\\' and partition_name = \\'',\
				  part_name,\
				  '\\')');\
			       set @_test_exists_part_query = test_exists_part_query;\
			       prepare stmt FROM @_test_exists_part_query;\
			       execute stmt;\
			       deallocate prepare stmt;\
			       if(not @_exists_part) then\
				  set create_part_query = concat(\
				     'alter table ',\
				     if(database_name is not null, concat('`', database_name, '`.'), ''),\
				     '`',\
				     table_name,\
				     '` add partition (partition ',\
				     part_name,") + 
				     (opt_cdr_partition_oldver ? 
					   "' VALUES LESS THAN (',\
					    part_limit_int,\
					    '))'" :
					   "' VALUES LESS THAN (\\'',\
					    part_limit,\
					    '\\'))'") + 
				     ");\
				  set @_create_part_query = create_part_query;\
				  prepare stmt FROM @_create_part_query;\
				  execute stmt;\
				  deallocate prepare stmt;\
			       end if;\
			    end if;\
			 end",
			"create_partition", "(database_name char(100), table_name char(100), type_part char(10), next_days int)");
		}
	}
	if(opt_cdr_partition && !opt_disable_partition_operations) {
		if(!cloud_host.empty()) {
			this->createProcedure(
			"begin\
			    call create_partition('cdr', 'day', next_days);\
			    call create_partition('cdr_next', 'day', next_days);\
			    call create_partition('cdr_rtp', 'day', next_days);\
			    call create_partition('cdr_dtmf', 'day', next_days);\
			    call create_partition('cdr_proxy', 'day', next_days);\
			    call create_partition('http_jj', 'day', next_days);\
			    call create_partition('enum_jj', 'day', next_days);\
			    call create_partition('message', 'day', next_days);\
			    call create_partition('register_state', 'day', next_days);\
			    call create_partition('register_failed', 'day', next_days);\
			 end",
			"create_partitions_cdr", "(next_days int)");
			if(opt_create_old_partitions > 0 && createdCdrTable) {
				for(int i = opt_create_old_partitions - 1; i > 0; i--) {
					char i_str[10];
					sprintf(i_str, "%i", i);
					this->query(string(
					"call create_partitions_cdr('-") + i_str + ");");
				}
			}
			this->query(
			"call create_partitions_cdr(0);");
			this->query(
			"call create_partitions_cdr(1);");
			this->query(
			"drop event if exists cdr_add_partition");
			this->query(
			"create event if not exists cdr_add_partition\
			 on schedule every 1 hour do\
			 begin\
			    call create_partitions_cdr(1);\
			 end");
		} else {
			this->createProcedure(
			"begin\
			    call create_partition(database_name, 'cdr', 'day', next_days);\
			    call create_partition(database_name, 'cdr_next', 'day', next_days);\
			    call create_partition(database_name, 'cdr_rtp', 'day', next_days);\
			    call create_partition(database_name, 'cdr_dtmf', 'day', next_days);\
			    call create_partition(database_name, 'cdr_proxy', 'day', next_days);\
			    call create_partition(database_name, 'http_jj', 'day', next_days);\
			    call create_partition(database_name, 'enum_jj', 'day', next_days);\
			    call create_partition(database_name, 'webrtc', 'day', next_days);\
			    call create_partition(database_name, 'message', 'day', next_days);\
			    call create_partition(database_name, 'register_state', 'day', next_days);\
			    call create_partition(database_name, 'register_failed', 'day', next_days);\
			 end",
			"create_partitions_cdr", "(database_name char(100), next_days int)");
			if(opt_create_old_partitions > 0 && createdCdrTable) {
				for(int i = opt_create_old_partitions - 1; i > 0; i--) {
					char i_str[10];
					sprintf(i_str, "%i", i);
					this->query(string(
					"call `") + mysql_database + "`.create_partitions_cdr('" + mysql_database + "', -" + i_str + ");");
				}
			}
			this->query(string(
			"call `") + mysql_database + "`.create_partitions_cdr('" + mysql_database + "', 0);");
			this->query(string(
			"call `") + mysql_database + "`.create_partitions_cdr('" + mysql_database + "', 1);");
			this->query(
			"drop event if exists cdr_add_partition");
			this->query(string(
			"create event if not exists cdr_add_partition\
			 on schedule every 1 hour do\
			 begin\
			    call `") + mysql_database + "`.create_partitions_cdr('" + mysql_database + "', 1);\
			 end");
		}
	}
	if(opt_ipaccount && !opt_disable_partition_operations) {
		if(!cloud_host.empty()) {
			this->createProcedure(
			"begin\
			    call create_partition('ipacc', 'day', next_days);\
			    call create_partition('ipacc_agr_interval', 'day', next_days);\
			    call create_partition('ipacc_agr_hour', 'day', next_days);\
			    call create_partition('ipacc_agr2_hour', 'day', next_days);\
			    call create_partition('ipacc_agr_day', 'month', next_days);\
			 end",
			"create_partitions_ipacc", "(next_days int)");
			this->query(
			"call create_partitions_ipacc(0);");
			this->query(
			"call create_partitions_ipacc(1);");
			this->query(
			"drop event if exists ipacc_add_partition");
			this->query(
			"create event if not exists ipacc_add_partition\
			 on schedule every 1 hour do\
			 begin\
			    call create_partitions_ipacc(1);\
			 end");
		} else {
			this->createProcedure(
			"begin\
			    call create_partition(database_name, 'ipacc', 'day', next_days);\
			    call create_partition(database_name, 'ipacc_agr_interval', 'day', next_days);\
			    call create_partition(database_name, 'ipacc_agr_hour', 'day', next_days);\
			    call create_partition(database_name, 'ipacc_agr2_hour', 'day', next_days);\
			    call create_partition(database_name, 'ipacc_agr_day', 'month', next_days);\
			 end",
			"create_partitions_ipacc", "(database_name char(100), next_days int)");
			this->query(string(
			"call `") + mysql_database + "`.create_partitions_ipacc('" + mysql_database + "', 0);");
			this->query(string(
			"call `") + mysql_database + "`.create_partitions_ipacc('" + mysql_database + "', 1);");
			this->query(
			"drop event if exists ipacc_add_partition");
			this->query(string(
			"create event if not exists ipacc_add_partition\
			 on schedule every 1 hour do\
			 begin\
			    call `") + mysql_database + "`.create_partitions_ipacc('" + mysql_database + "', 1);\
			 end");
		}
	}

	this->createFunction( // double space after begin for invocation rebuild function if change parameter - createRoutine compare only body
			"BEGIN  \
				DECLARE _ID INT; \
				SET _ID = (SELECT id FROM cdr_ua WHERE ua = val); \
				IF ( _ID ) THEN \
					RETURN _ID; \
				ELSE  \
					INSERT INTO cdr_ua SET ua = val; \
					RETURN LAST_INSERT_ID(); \
				END IF; \
			END",
			"getIdOrInsertUA", "(val VARCHAR(255) CHARACTER SET latin1) RETURNS INT DETERMINISTIC");

	this->createFunction( // double space after begin for invocation rebuild function if change parameter - createRoutine compare only body
			"BEGIN  \
				DECLARE _ID INT; \
				SET _ID = (SELECT id FROM cdr_sip_response WHERE lastSIPresponse = val); \
				IF ( _ID ) THEN \
					RETURN _ID; \
				ELSE  \
					INSERT INTO cdr_sip_response SET lastSIPresponse = val; \
					RETURN LAST_INSERT_ID(); \
				END IF; \
			END",
			"getIdOrInsertSIPRES", "(val VARCHAR(255) CHARACTER SET latin1) RETURNS INT DETERMINISTIC");

	this->createFunction( // double space after begin for invocation rebuild function if change parameter - createRoutine compare only body
			"BEGIN  \
				DECLARE _ID INT; \
				SET _ID = (SELECT id FROM contenttype WHERE contenttype = val LIMIT 1); \
				IF ( _ID ) THEN \
					RETURN _ID; \
				ELSE  \
					INSERT INTO contenttype SET contenttype = val; \
					RETURN LAST_INSERT_ID(); \
				END IF; \
			END",
			"getIdOrInsertCONTENTTYPE", "(val VARCHAR(255) CHARACTER SET utf8) RETURNS INT DETERMINISTIC");

	this->createProcedure(
			"BEGIN \
				DECLARE _ID INT; \
				DECLARE _state INT; \
				DECLARE _expires_at DATETIME; \
				DECLARE _expired INT; \
				SELECT ID, \
				       state, \
				       expires_at, \
				       (UNIX_TIMESTAMP(expires_at) < UNIX_TIMESTAMP(calltime)) AS expired \
				INTO _ID, _state, _expires_at, _expired FROM register \
				WHERE to_num = called AND to_domain = called_domain AND \
				      contact_num = contact_num_param AND contact_domain = contact_domain_param \
				ORDER BY ID DESC LIMIT 1; \
				IF ( _ID ) THEN \
					DELETE FROM register WHERE ID = _ID; \
					IF ( _expired > 5 ) THEN \
						INSERT INTO `register_state` \
							SET `id_sensor` = id_sensor, \
							    `fname` = fname, \
							    `created_at` = _expires_at, \
							    `sipcallerip` = sipcallerip, \
							    `sipcalledip` = sipcalledip, \
							    `from_num` = caller, \
							    `to_num` = called, \
							    `to_domain` = called_domain, \
							    `contact_num` = contact_num_param, \
							    `contact_domain` = contact_domain_param, \
							    `digestusername` = digest_username, \
							    `expires` = register_expires, \
							    state = 5, \
							    ua_id = getIdOrInsertUA(cdr_ua); \
					END IF; \
					IF ( _state <> regstate OR register_expires = 0) THEN \
						INSERT INTO `register_state` \
							SET `id_sensor` = id_sensor, \
							    `fname` = fname, \
							    `created_at` = calltime, \
							    `sipcallerip` = sipcallerip, \
							    `sipcalledip` = sipcalledip, \
							    `from_num` = caller, \
							    `to_num` = called, \
							    `to_domain` = called_domain, \
							    `contact_num` = contact_num_param, \
							    `contact_domain` = contact_domain_param, \
							    `digestusername` = digest_username, \
							    `expires` = register_expires, \
							    state = regstate, \
							    ua_id = getIdOrInsertUA(cdr_ua); \
					END IF; \
				ELSE \
					INSERT INTO `register_state` \
						SET `id_sensor` = id_sensor, \
						    `fname` = fname, \
						    `created_at` = calltime, \
						    `sipcallerip` = sipcallerip, \
						    `sipcalledip` = sipcalledip, \
						    `from_num` = caller, \
						    `to_num` = called, \
						    `to_domain` = called_domain, \
						    `contact_num` = contact_num_param, \
						    `contact_domain` = contact_domain_param, \
						    `digestusername` = digest_username, \
						    `expires` = register_expires, \
						    state = regstate, \
						    ua_id = getIdOrInsertUA(cdr_ua);\
				END IF; \
				IF ( register_expires > 0 ) THEN \
					INSERT INTO `register` \
						SET `id_sensor` = id_sensor, \
						    `fname` = fname, \
						    `calldate` = calltime, \
						    `sipcallerip` = sipcallerip, \
						    `sipcalledip` = sipcalledip, \
						    `from_num` = caller, \
						    `from_name` = callername, \
						    `from_domain` = caller_domain, \
						    `to_num` = called, \
						    `to_domain` = called_domain, \
						    `contact_num` = contact_num_param, \
						    `contact_domain` = contact_domain_param, \
						    `digestusername` = digest_username, \
						    `digestrealm` = digest_realm, \
						    `expires` = register_expires, \
						    state = regstate, \
						    ua_id = getIdOrInsertUA(cdr_ua), \
						    `expires_at` = mexpires_at; \
				END IF; \
			END",
			"PROCESS_SIP_REGISTER", 
			"(IN calltime VARCHAR(32), \
			  IN caller VARCHAR(64), \
			  IN callername VARCHAR(64), \
			  IN caller_domain VARCHAR(64), \
			  IN called VARCHAR(64), \
			  IN called_domain VARCHAR(64), \
			  IN sipcallerip INT UNSIGNED, \
			  IN sipcalledip INT UNSIGNED, \
			  IN contact_num_param VARCHAR(64), \
			  IN contact_domain_param VARCHAR(64), \
			  IN digest_username VARCHAR(255), \
			  IN digest_realm VARCHAR(255), \
			  IN regstate INT, \
			  IN mexpires_at VARCHAR(128), \
			  IN register_expires INT, \
			  IN cdr_ua VARCHAR(255), \
			  IN fname BIGINT, \
			  IN id_sensor INT)");

	//END SQL SCRIPTS
	}

	//this->multi_on();
	sql_disable_next_attempt_if_error = 0;

	syslog(LOG_DEBUG, "done");
}

void SqlDb_mysql::checkDbMode() {
	sql_disable_next_attempt_if_error = 1;
	if(!opt_cdr_partition &&
	   (cloud_host[0] ||
	    this->getDbMajorVersion() * 100 + this->getDbMinorVersion() > 500)) {
		this->query("EXPLAIN PARTITIONS SELECT * from cdr limit 1");
		SqlDb_row row;
		if((row = this->fetchRow())) {
			if(row["partitions"] != "") {
				syslog(LOG_INFO, "enable opt_cdr_partition (table cdr has partitions)");
				opt_cdr_partition = true;
			}
		}
	}
	if(opt_cdr_partition && !cloud_host[0]) {
		if(this->getDbMajorVersion() * 100 + this->getDbMinorVersion() <= 500) {
			opt_cdr_partition = false;
			syslog(LOG_NOTICE, "mysql <= 5.0 does not know partitions - we recommend to upgrade mysql");
		} else { 
			if(this->getDbMajorVersion() * 100 + this->getDbMinorVersion() <= 501) {
				opt_cdr_partition_oldver = true;
				syslog(LOG_NOTICE, "mysql <= 5.1 - use old mode partitions");
			} else {
				this->query(string("select partition_description from information_schema.partitions where table_schema='") +mysql_database + 
					    "' and table_name like 'cdr%' and partition_description is not null and  partition_description regexp '^[0-9]+$' limit 1");
				if(this->fetchRow()) {
					opt_cdr_partition_oldver = true;
					syslog(LOG_NOTICE, "database contain old mode partitions");
				}
			}
		}
	}
	sql_disable_next_attempt_if_error = 0;
}

void SqlDb_mysql::createTable(const char *tableName) {
	if(!strcmp(tableName, "fraud_alert_info")) {
		this->query("show tables like 'alerts'");
		if(this->fetchRow()) {
			this->query(
			"CREATE TABLE IF NOT EXISTS `fraud_alert_info` (\
					`id` INT UNSIGNED NOT NULL AUTO_INCREMENT,\
					`alert_id` INT NOT NULL,\
					`at` DATETIME NOT NULL,\
					`alert_info` TEXT NOT NULL,\
					PRIMARY KEY (`ID`),\
					CONSTRAINT `fraud_alert_info_ibfk_1` FOREIGN KEY (`alert_id`) REFERENCES `alerts` (`id`) ON UPDATE CASCADE ON DELETE CASCADE\
			) ENGINE=InnoDB DEFAULT CHARSET=latin1;");
		}		
	}
}

void SqlDb_mysql::checkSchema() {
	extern bool existsColumnCalldateInCdrNext;
	extern bool existsColumnCalldateInCdrRtp;
	extern bool existsColumnCalldateInCdrDtmf;
	sql_disable_next_attempt_if_error = 1;
	this->query("show columns from cdr_next where Field='calldate'");
	existsColumnCalldateInCdrNext = this->fetchRow();
	this->query("show columns from cdr_rtp where Field='calldate'");
	existsColumnCalldateInCdrRtp = this->fetchRow();
	this->query("show columns from cdr_dtmf where Field='calldate'");
	existsColumnCalldateInCdrDtmf = this->fetchRow();
	if(!opt_cdr_partition &&
	   (cloud_host[0] ||
	    this->getDbMajorVersion() * 100 + this->getDbMinorVersion() > 500)) {
		this->query("EXPLAIN PARTITIONS SELECT * from cdr limit 1");
		SqlDb_row row;
		if((row = this->fetchRow())) {
			if(row["partitions"] != "") {
				syslog(LOG_INFO, "enable opt_cdr_partition (table cdr has partitions)");
				opt_cdr_partition = true;
			}
		}
	}
	sql_disable_next_attempt_if_error = 0;
}

bool SqlDb_mysql::checkSourceTables() {
	bool ok = true;
	vector<string> sourceTables = this->getSourceTables();
	sql_disable_next_attempt_if_error = 1;
	for(size_t i = 0; i < sourceTables.size(); i++) {
		if(!this->query("select * from " + sourceTables[i] + " limit 1")) {
			ok = false;
		} else {
			while(this->fetchRow());
		}
	}
	sql_disable_next_attempt_if_error = 0;
	return(ok);
}

void SqlDb_mysql::copyFromSourceTables(SqlDb_mysql *sqlDbSrc) {
	unsigned long maxDiffId = 100000;
	this->copyFromSourceTable(sqlDbSrc, "cdr_sip_response");
	if(terminating) return;
	this->copyFromSourceTable(sqlDbSrc, "cdr_ua");
	if(terminating) return;
	this->copyFromSourceTable(sqlDbSrc, "contenttype");
	if(terminating) return;
	this->copyFromSourceTable(sqlDbSrc, "cdr", NULL, maxDiffId);
	if(terminating) return;
	if(opt_enable_http_enum_tables) {
		this->copyFromSourceTable(sqlDbSrc, "http_jj", NULL, maxDiffId);
		if(terminating) return;
		this->copyFromSourceTable(sqlDbSrc, "enum_jj", NULL, maxDiffId);
		if(terminating) return;
	}
	if(opt_enable_webrtc_table) {
		this->copyFromSourceTable(sqlDbSrc, "webrtc", NULL, maxDiffId);
		if(terminating) return;
	}
	this->copyFromSourceTable(sqlDbSrc, "message", NULL, maxDiffId);
	if(terminating) return;
	this->copyFromSourceTable(sqlDbSrc, "register_state", NULL, maxDiffId);
	if(terminating) return;
	this->copyFromSourceTable(sqlDbSrc, "register_failed", NULL, maxDiffId);
}

void SqlDb_mysql::copyFromSourceTable(SqlDb_mysql *sqlDbSrc, const char *tableName, const char *id, unsigned long maxDiffId, 
				      unsigned long minIdInSrc, unsigned long useMaxIdInSrc) {
	if(!id) {
		id = "id";
	}
	bool joinCdrCalldate = string(tableName) == "cdr_next" ||
			       string(tableName) == "cdr_rtp" ||
			       string(tableName) == "cdr_dtmf" ||
			       string(tableName) == "cdr_proxy";
	if(joinCdrCalldate) {
		sqlDbSrc->query(string("show columns from ") + tableName + " where Field='calldate'");
		if(sqlDbSrc->fetchRow()) {
			joinCdrCalldate = false;
		}
	}
	sqlDbSrc->query(string("select max(") + id + ") as max_id from " + tableName);
	unsigned long maxIdInSrc = atoll(sqlDbSrc->fetchRow()["max_id"].c_str());
	this->query(string("select max(") + id + ") as max_id from " + string(tableName));
	unsigned long maxIdInDst = atoll(this->fetchRow()["max_id"].c_str());
	if(!maxIdInDst) {
		extern char opt_database_backup_from_date[20];
		if(opt_database_backup_from_date[0] &&
		   (string(tableName) == "cdr" ||
		    string(tableName) == "http_jj" ||
		    string(tableName) == "enum_jj" ||
		    string(tableName) == "message" ||
		    string(tableName) == "register_state" ||
		    string(tableName) == "register_failed")) {
			string timeColumn = (string(tableName) == "cdr" || string(tableName) == "message") ? "calldate" : 
					    (string(tableName) == "http_jj" || string(tableName) == "enum_jj") ? "timestamp" : "created_at";
			sqlDbSrc->query(string("select min(") + id + ") as min_id from " + tableName +
					" where " + timeColumn + " = " + 
					"(select min(" + timeColumn + ") from " + tableName + " where " + timeColumn + " > '" + opt_database_backup_from_date + "')");
			
			minIdInSrc = atoll(sqlDbSrc->fetchRow()["min_id"].c_str());
		}
	}
	if(maxIdInSrc > maxIdInDst) {
		stringstream queryStr;
		queryStr << "select " << tableName << ".*";
		if(joinCdrCalldate) {
			queryStr << ",cdr.calldate";
		}
		queryStr << " from " << tableName;
		if(joinCdrCalldate) {
			queryStr << " join cdr on (cdr.id = " << tableName << ".cdr_id)";
		}
		queryStr << " where "
			 << id << " >= " << max(minIdInSrc, maxIdInDst + 1);
		if(useMaxIdInSrc) {
			queryStr << " and " << id << " <= " << useMaxIdInSrc;
		}
		if(string(tableName) == "register_failed") {
			queryStr << " and created_at < '" << sqlDateTimeString(time(NULL) - 3600) << "'";
		}
		queryStr << " order by " << id;
		if(maxDiffId) {
			queryStr << " limit " << maxDiffId;
		}
		syslog(LOG_NOTICE, ("select query: " + queryStr.str()).c_str());
		if(sqlDbSrc->query(queryStr.str())) {
			extern MySqlStore *sqlStore;
			SqlDb_row row;
			vector<SqlDb_row> rows;
			unsigned int counterInsert = 0;
			extern int opt_database_backup_insert_threads;
			unsigned int insertThreads = opt_database_backup_insert_threads > 1 ? opt_database_backup_insert_threads : 1;
			while(!terminating && (row = sqlDbSrc->fetchRow(true))) {
				if(maxDiffId) {
					useMaxIdInSrc = atoll(row[id].c_str());
				}
				rows.push_back(row);
				if(rows.size() >= 100) {
					string insertQuery = this->insertQuery(tableName, &rows, false, true, true);
					sqlStore->query(insertQuery.c_str(), 
							insertThreads > 1 ?
								((counterInsert++ % insertThreads) + 1) :
								1);
					rows.clear();
				}
				while(!terminating && sqlStore->getAllSize() > 1000) {
					usleep(100000);
				}
			}
			if(!terminating && rows.size()) {
				string insertQuery = this->insertQuery(tableName, &rows, false, true, true);
				sqlStore->query(insertQuery.c_str(), 
						insertThreads > 1 ?
							((counterInsert++ % insertThreads) + 1) :
							1);
				rows.clear();
			}
		}
		if(string(tableName) == "cdr") {
			if(terminating) return;
			this->copyFromSourceTable(sqlDbSrc, "cdr_next", "cdr_id", 0, minIdInSrc, useMaxIdInSrc);
			if(terminating) return;
			this->copyFromSourceTable(sqlDbSrc, "cdr_rtp", "cdr_id", 0, minIdInSrc, useMaxIdInSrc);
			if(terminating) return;
			this->copyFromSourceTable(sqlDbSrc, "cdr_dtmf", "cdr_id", 0, minIdInSrc, useMaxIdInSrc);
			if(terminating) return;
			this->copyFromSourceTable(sqlDbSrc, "cdr_proxy", "cdr_id", 0, minIdInSrc, useMaxIdInSrc);
		}
	}
}

void SqlDb_mysql::copyFromSourceGuiTables(SqlDb_mysql *sqlDbSrc) {
	vector<string> mainSourceTables = getSourceTables();
	sqlDbSrc->query("show tables");
	SqlDb_row row;
	while(row = sqlDbSrc->fetchRow()) {
		string tableName = row[0];
		if((tableName == "http_jj" || tableName == "enum_jj") && !opt_enable_http_enum_tables) {
			continue;
		}
		if(tableName == "webrtc" && !opt_enable_webrtc_table) {
			continue;
		}
		bool isMainSourceTable = false;
		for(size_t i = 0; i < mainSourceTables.size(); i++) {
			if(tableName == mainSourceTables[i]) {
				isMainSourceTable = true;
				break;
			}
		}
		if(isMainSourceTable ||
		   tableName == "register" ||
		   tableName.find("livepacket") != string::npos) {
			continue;
		}
		cout << tableName << endl;
		this->copyFromSourceGuiTable(sqlDbSrc, tableName.c_str());
	}
}

void SqlDb_mysql::copyFromSourceGuiTable(SqlDb_mysql *sqlDbSrc, const char *tableName) {
	this->query("set FOREIGN_KEY_CHECKS=0");
	this->query(string("drop table if exists ") + tableName);
	this->query("set FOREIGN_KEY_CHECKS=1");
	string cmdCopyTable = 
		string("mysqldump --opt") +
		" -h" + sqlDbSrc->conn_server +
		" -u" + sqlDbSrc->conn_user +
		(sqlDbSrc->conn_password.length() ? " -p" + sqlDbSrc->conn_password : "") +
		" " + sqlDbSrc->conn_database + 
		" " + tableName +
		" | mysql" +
		
		" -h" + this->conn_server +
		" -u" + this->conn_user +
		(this->conn_password.length() ? " -p" + this->conn_password : "") +
		" -D" + sqlDbSrc->conn_database;
	cout << cmdCopyTable << endl;
	system(cmdCopyTable.c_str());
}

vector<string> SqlDb_mysql::getSourceTables() {
	const char *sourceTables[] = {
		"cdr_sip_response",
		"cdr_ua",
		"contenttype",
		"cdr",
		"cdr_next",
		"cdr_rtp",
		"cdr_dtmf",
		"cdr_proxy",
		opt_enable_http_enum_tables ? "http_jj" : NULL,
		opt_enable_http_enum_tables ? "enum_jj" : NULL,
		opt_enable_webrtc_table ? "webrtc" : NULL,
		"message",
		"register_failed",
		"register_state"
	};
	vector<string> rsltTables;
	for(size_t i = 0; i < sizeof(sourceTables) / sizeof(sourceTables[0]); i++) {
		if(sourceTables[i]) {
			rsltTables.push_back(sourceTables[i]);
		}
	}
	return(rsltTables);
}

bool SqlDb_mysql::checkFederatedTables() {
	bool ok = true;
	vector<string> federatedTables = this->getFederatedTables();
	sql_disable_next_attempt_if_error = 1;
	for(size_t i = 0; i < federatedTables.size(); i++) {
		if(!this->query("select * from " + federatedTables[i] + " limit 1")) {
			ok = false;
		} else {
			while(this->fetchRow());
		}
	}
	sql_disable_next_attempt_if_error = 0;
	return(ok);
}

void SqlDb_mysql::copyFromFederatedTables() {
	unsigned long maxDiffId = 10000;
	this->copyFromFederatedTable("cdr_sip_response");
	if(terminating) return;
	this->copyFromFederatedTable("cdr_ua");
	if(terminating) return;
	this->copyFromFederatedTable("contenttype");
	if(terminating) return;
	this->copyFromFederatedTable("cdr", NULL, maxDiffId);
	if(terminating) return;
	if(opt_enable_http_enum_tables) {
		this->copyFromFederatedTable("http_jj", NULL, maxDiffId);
		if(terminating) return;
		this->copyFromFederatedTable("enum_jj", NULL, maxDiffId);
		if(terminating) return;
	}
	if(opt_enable_webrtc_table) {
		this->copyFromFederatedTable("webrtc", NULL, maxDiffId);
		if(terminating) return;
	}
	this->copyFromFederatedTable("message", NULL, maxDiffId);
	if(terminating) return;
	this->copyFromFederatedTable("register_state", NULL, maxDiffId);
	if(terminating) return;
	this->copyFromFederatedTable("register_failed", NULL, maxDiffId);
}

void SqlDb_mysql::copyFromFederatedTable(const char *tableName, const char *id, unsigned long maxDiffId, 
					 unsigned long minIdInFederated, unsigned long useMaxIdInFederated) {
	if(!id) {
		id = "id";
	}
	string tableNameFederated = string(tableName) + "_fed";
	this->query(string("select max(") + id + ") as max_id from " + tableNameFederated);
	unsigned long maxIdInFederated = atoll(this->fetchRow()["max_id"].c_str());
	extern char opt_database_backup_from_date[20];
	if(opt_database_backup_from_date[0] &&
	   (string(tableName) == "cdr" ||
	    string(tableName) == "http_jj" ||
	    string(tableName) == "enum_jj" ||
	    string(tableName) == "message" ||
	    string(tableName) == "register_state" ||
	    string(tableName) == "register_failed")) {
		string timeColumn = (string(tableName) == "cdr" || string(tableName) == "message") ? "calldate" : 
				    (string(tableName) == "http_jj" || string(tableName) == "enum_jj") ? "timestamp" : "created_at";
		this->query(string("select min(") + id + ") as min_id from " + tableNameFederated + 
			    " where " + timeColumn + " > '" + opt_database_backup_from_date + "'");
		minIdInFederated = atoll(this->fetchRow()["min_id"].c_str());
	}
	this->query(string("select max(") + id + ") as max_id from " + string(tableName));
	unsigned long maxIdInDst = atoll(this->fetchRow()["max_id"].c_str());
	if(maxIdInFederated > maxIdInDst) {
		if(!useMaxIdInFederated && maxDiffId) {
			useMaxIdInFederated = min(max(minIdInFederated - 1, maxIdInDst) + maxDiffId, maxIdInFederated);
		}
		stringstream queryStr;
		queryStr << "insert into " << tableName
			 << " select * from " << tableNameFederated
			 << " where "
			 << id << " >= " << max(minIdInFederated, maxIdInDst + 1);
		if(useMaxIdInFederated) {
			queryStr << " and " << id << " <= " << useMaxIdInFederated;
		}
		queryStr << " order by " << id;
		syslog(LOG_NOTICE, ("copy query: " + queryStr.str()).c_str());
		this->query(queryStr.str());
		if(string(tableName) == "cdr") {
			if(terminating) return;
			this->copyFromFederatedTable("cdr_next", "cdr_id", 0, minIdInFederated, useMaxIdInFederated);
			if(terminating) return;
			this->copyFromFederatedTable("cdr_rtp", "cdr_id", 0, minIdInFederated, useMaxIdInFederated);
			if(terminating) return;
			this->copyFromFederatedTable("cdr_dtmf", "cdr_id", 0, minIdInFederated, useMaxIdInFederated);
			if(terminating) return;
			this->copyFromFederatedTable("cdr_proxy", "cdr_id", 0, minIdInFederated, useMaxIdInFederated);
		}
	}
}

void SqlDb_mysql::dropFederatedTables() {
	vector<string> federatedTables = this->getFederatedTables();
	sql_disable_next_attempt_if_error = 1;
	for(size_t i = 0; i < federatedTables.size(); i++) {
		this->query("drop table if exists " + federatedTables[i]);
	}
	sql_disable_next_attempt_if_error = 0;
}

vector<string> SqlDb_mysql::getFederatedTables() {
	const char *federatedTables[] = {
		"cdr_sip_response_fed",
		"cdr_ua_fed",
		"contenttype_fed",
		"cdr_fed",
		"cdr_next_fed",
		"cdr_rtp_fed",
		"cdr_dtmf_fed",
		"cdr_proxy_fed",
		opt_enable_http_enum_tables ? "http_jj_fed" : NULL,
		opt_enable_http_enum_tables ? "enum_jj_fed" : NULL,
		opt_enable_webrtc_table ? "webrtc" : NULL,
		"message_fed",
		"register_failed_fed",
		"register_state_fed"
	};
	vector<string> rsltTables;
	for(size_t i = 0; i < sizeof(federatedTables) / sizeof(federatedTables[0]); i++) {
		rsltTables.push_back(federatedTables[i]);
	}
	return(rsltTables);
}


void SqlDb_odbc::createSchema(const char *host, const char *database, const char *user, const char *password) {
  cout << "SUBTYPE: " << this->subtypeDb << endl;
	if (this->subtypeDb=="pgsql") {
		this->query("CREATE TABLE IF NOT EXISTS sensor_conf (\
		  id serial,\
		  id_sensor bigint DEFAULT NULL,\
		  interface character varying (255) DEFAULT NULL,\
		  threading_mod smallint DEFAULT '1',\
		  mirror_destination_ip bigint DEFAULT NULL,\
		  mirror_destination_port smallint DEFAULT NULL,\
		  mirror_bind_ip bigint DEFAULT NULL,\
		  mirror_bind_port smallint DEFAULT '5030',\
		  mirror_bind_dlt bigint  DEFAULT '1',\
		  scanpcapdir character varying (255) DEFAULT NULL,\
		  scanpcapmethod character varying (255) DEFAULT 'newfile',\
		  natalias text,\
		  sdp_reverse_ipport smallint DEFAULT '0',\
		  managerip character varying (255) DEFAULT '127.0.0.1',\
		  httpport character varying (255) DEFAULT NULL,\
		  sipport character varying (255) DEFAULT '5060',\
		  cdr_sipport smallint DEFAULT '1',\
		  destination_number_mode smallint DEFAULT '1',\
		  onowaytimeout bigint DEFAULT '15',\
		  rtptimeout bigint DEFAULT '30',\
		  ringbuffer bigint DEFAULT '50',\
		  packetbuffer_enable smallint DEFAULT '1',\
		  packetbuffer_total_maxheap bigint DEFAULT '2000',\
		  packetbuffer_compress smallint DEFAULT '1',\
		  packetbuffer_file_totalmaxsize smallint DEFAULT '0',\
		  packetbuffer_file_path character varying (255) DEFAULT '/var/spool/voipmonitor/packetbuffer',\
		  rtpthreads bigint DEFAULT NULL,\
		  jitterbuffer_f1 smallint DEFAULT '1',\
		  jitterbuffer_f2 smallint DEFAULT '1',\
		  jitterbuffer_adapt smallint DEFAULT '1',\
		  callslimit bigint DEFAULT '0',\
		  cdrproxy smallint DEFAULT '1',\
		  cdr_ua_enable smallint DEFAULT '1',\
		  \"rtp-firstleg\" smallint DEFAULT '0',\
		  \"allow-zerossrc\" smallint DEFAULT '0',\
		  deduplicate smallint DEFAULT '0',\
		  deduplicate_ipheader smallint DEFAULT '1',\
		  sipoverlap smallint DEFAULT '1',\
		  \"sip-register\" smallint DEFAULT '0',\
		  \"sip-register-active-nologbin\" smallint DEFAULT '1',\
		  nocdr smallint DEFAULT '0',\
		  skipdefault smallint DEFAULT '0',\
		  cdronlyanswered smallint DEFAULT '0',\
		  cdronlyrtp smallint DEFAULT '0',\
		  maxpcapsize bigint DEFAULT NULL,\
		  savesip smallint DEFAULT '1',\
		  savertp smallint DEFAULT '1',\
		  pcapsplit smallint DEFAULT '1',\
		  savertcp smallint DEFAULT '1',\
		  saveaudio character varying (255) DEFAULT NULL,\
		  saveaudio_reversestereo smallint DEFAULT '0',\
		  keycheck text,\
		  saverfc2833 smallint DEFAULT '0',\
		  dtmf2db smallint DEFAULT '0',\
		  savegraph character varying (255) DEFAULT 'plain',\
		  \"norecord-header\" smallint DEFAULT '0',\
		  \"norecord-dtmf\" smallint DEFAULT '0',\
		  pauserecordingdtmf character varying (255) DEFAULT NULL,\
		  convert_dlt_sll2en10 smallint DEFAULT '0',\
		  mos_g729 smallint DEFAULT '0',\
		  mos_lqo smallint DEFAULT '0',\
		  mos_lqo_bin character varying (255) DEFAULT 'pesq',\
		  mos_lqo_ref character varying (255) DEFAULT '/usr/local/share/voipmonitor/audio/mos_lqe_original.wav',\
		  mos_lqo_ref16 character varying (255) DEFAULT '/usr/local/share/voipmonitor/audio/mos_lqe_original_16khz.wav',\
		  dscp smallint DEFAULT '1',\
		  custom_headers text,\
		  custom_headers_message text,\
		  matchheader text,\
		  domainport smallint DEFAULT '0',\
		  pcapcommand text,\
		  filtercommand text,\
		  filter text,\
		  openfile_max bigint DEFAULT NULL,\
		  convertchar text,\
		  spooldir character varying (255) DEFAULT '/var/spool/voipmonitor',\
		  spooldiroldschema smallint DEFAULT '0',\
		  cleandatabase_cdr bigint DEFAULT '0',\
		  cleandatabase_register_failed bigint DEFAULT '0',\
		  cleandatabase bigint DEFAULT '0',\
		  maxpoolsize bigint DEFAULT '102400',\
		  maxpooldays bigint DEFAULT '0',\
		  maxpoolsipsize bigint DEFAULT '0',\
		  maxpoolsipdays bigint DEFAULT '0',\
		  maxpoolrtpsize bigint DEFAULT '0',\
		  maxpoolrtpdays bigint DEFAULT '0',\
		  maxpoolgraphsize bigint DEFAULT '0',\
		  maxpoolgraphdays bigint DEFAULT '0',\
		  cachedir text,\
		  promisc smallint DEFAULT '1',\
		  sqlcallend smallint DEFAULT '1',\
		  cdr_partition smallint DEFAULT '1',\
		  disable_partition_operations smallint DEFAULT '0',\
		  upgrade_try_http_if_https_fail smallint DEFAULT '1',\
		  opt_saveaudio_reversestereo smallint DEFAULT '0',\
		  onewaytimeout smallint DEFAULT '15',\
		  \"sip-register-timeout\" smallint DEFAULT '5',\
		  CONSTRAINT filter_sensor_conf_pkey PRIMARY KEY (id)\
		);");
	
		this->query("CREATE TABLE IF NOT EXISTS filter_ip (\
			id serial,\
			ip BIGINT NULL,\
			mask BIGINT NULL,\
			direction SMALLINT DEFAULT '0',\
			rtp SMALLINT DEFAULT '0',\
			sip SMALLINT DEFAULT '0',\
			register SMALLINT DEFAULT '0',\
			graph SMALLINT DEFAULT '0',\
			wav SMALLINT DEFAULT '0',\
			skip SMALLINT DEFAULT '0',\
			script SMALLINT DEFAULT '0',\
			mos_lqo SMALLINT DEFAULT '0',\
			hide_message SMALLINT DEFAULT NULL,\
			note text, \
			remove_at timestamp default NULL,\
			CONSTRAINT filter_ip_pkey PRIMARY KEY (id));");
		
		this->query(
		"CREATE TABLE IF NOT EXISTS filter_telnum (\
			id serial,\
			prefix bigint NULL,\
			fixed_len bigint DEFAULT '0',\
			direction smallint DEFAULT '0',\
			rtp smallint DEFAULT '0',\
			sip smallint DEFAULT '0',\
			register smallint DEFAULT '0',\
			graph smallint DEFAULT '0',\
			wav smallint DEFAULT '0',\
			skip smallint DEFAULT '0',\
			script smallint DEFAULT '0',\
			mos_lqo smallint DEFAULT '0',\
			note text, \
			remove_at timestamp default NULL,\
			CONSTRAINT filter_telnum_pkey PRIMARY KEY (id)\
			);\
		");
		
		this->query("CREATE TABLE IF NOT EXISTS filter_domain (\
			id serial,\
			domain character varying(128) DEFAULT NULL,\
			direction smallint DEFAULT NULL,\
			rtp smallint DEFAULT NULL,\
			sip smallint DEFAULT NULL,\
			register smallint DEFAULT NULL,\
			graph smallint DEFAULT NULL,\
			wav smallint DEFAULT NULL,\
			skip smallint DEFAULT NULL,\
			script smallint DEFAULT NULL,\
			mos_lqo smallint DEFAULT NULL,\
			hide_message smallint DEFAULT NULL,\
			note text,\
			remove_at timestamp default NULL,\
		CONSTRAINT filter_domain_pkey PRIMARY KEY (id)\
		);");
		
		this->query(
		"CREATE TABLE IF NOT EXISTS cdr_sip_response (\
			id serial,\
			lastSIPresponse character varying(255) NULL,\
			 CONSTRAINT cdr_sip_response_pkey PRIMARY KEY (id));\
			CREATE UNIQUE INDEX ON cdr_sip_response (lastSIPresponse);\
		");
		
		this->query(
		"CREATE TABLE IF NOT EXISTS cdr_ua (\
				id serial,\
				ua character varying(512) NULL, \
				CONSTRAINT cdr_ua_pkey PRIMARY KEY (id) \
			);\
			CREATE UNIQUE INDEX ON cdr_ua (ua);\
		");
		
		this->query(
		"CREATE TABLE IF NOT EXISTS cdr (\
			  id serial,\
			  calldate timestamp NOT NULL,\
			  callend timestamp NOT NULL,\
			  duration integer DEFAULT NULL,\
			  connect_duration integer DEFAULT NULL,\
			  progress_time integer DEFAULT NULL,\
			  first_rtp_time integer DEFAULT NULL,\
			  caller character varying(255) DEFAULT NULL,\
			  caller_domain character varying(255) DEFAULT NULL,\
			  caller_reverse character varying(255) DEFAULT NULL,\
			  callername character varying(255) DEFAULT NULL,\
			  callername_reverse character varying(255) DEFAULT NULL,\
			  called character varying(255) DEFAULT NULL,\
			  called_domain character varying(255) DEFAULT NULL,\
			  called_reverse character varying(255) DEFAULT NULL,\
			  sipcallerip bigint DEFAULT NULL,\
			  sipcallerport integer DEFAULT NULL,\
			  sipcalledip bigint DEFAULT NULL,\
			  sipcalledport integer DEFAULT NULL,\
			  whohanged character varying(30) DEFAULT NULL,\
			  bye smallint DEFAULT NULL,\
			  lastSIPresponse_id integer DEFAULT NULL,\
			  lastSIPresponseNum integer DEFAULT NULL,\
			  sighup smallint DEFAULT NULL,\
			  dscp bigint DEFAULT NULL,\
			  a_index smallint DEFAULT NULL,\
			  b_index smallint DEFAULT NULL,\
			  a_payload bigint DEFAULT NULL,\
			  b_payload bigint DEFAULT NULL,\
			  a_saddr bigint DEFAULT NULL,\
			  b_saddr bigint DEFAULT NULL,\
			  a_received integer DEFAULT NULL,\
			  b_received integer DEFAULT NULL,\
			  a_lost integer DEFAULT NULL,\
			  b_lost integer DEFAULT NULL,\
			  a_ua_id bigint DEFAULT NULL,\
			  b_ua_id bigint DEFAULT NULL,\
			  a_avgjitter_mult10 integer DEFAULT NULL,\
			  b_avgjitter_mult10 integer DEFAULT NULL,\
			  a_maxjitter integer DEFAULT NULL,\
			  b_maxjitter integer DEFAULT NULL,\
			  a_sl1 integer DEFAULT NULL,\
			  a_sl2 integer DEFAULT NULL,\
			  a_sl3 integer DEFAULT NULL,\
			  a_sl4 integer DEFAULT NULL,\
			  a_sl5 integer DEFAULT NULL,\
			  a_sl6 integer DEFAULT NULL,\
			  a_sl7 integer DEFAULT NULL,\
			  a_sl8 integer DEFAULT NULL,\
			  a_sl9 integer DEFAULT NULL,\
			  a_sl10 integer DEFAULT NULL,\
			  a_d50 integer DEFAULT NULL,\
			  a_d70 integer DEFAULT NULL,\
			  a_d90 integer DEFAULT NULL,\
			  a_d120 integer DEFAULT NULL,\
			  a_d150 integer DEFAULT NULL,\
			  a_d200 integer DEFAULT NULL,\
			  a_d300 integer DEFAULT NULL,\
			  b_sl1 integer DEFAULT NULL,\
			  b_sl2 integer DEFAULT NULL,\
			  b_sl3 integer DEFAULT NULL,\
			  b_sl4 integer DEFAULT NULL,\
			  b_sl5 integer DEFAULT NULL,\
			  b_sl6 integer DEFAULT NULL,\
			  b_sl7 integer DEFAULT NULL,\
			  b_sl8 integer DEFAULT NULL,\
			  b_sl9 integer DEFAULT NULL,\
			  b_sl10 integer DEFAULT NULL,\
			  b_d50 integer DEFAULT NULL,\
			  b_d70 integer DEFAULT NULL,\
			  b_d90 integer DEFAULT NULL,\
			  b_d120 integer DEFAULT NULL,\
			  b_d150 integer DEFAULT NULL,\
			  b_d200 integer DEFAULT NULL,\
			  b_d300 integer DEFAULT NULL,\
			  a_mos_lqo_mult10 smallint DEFAULT NULL,\
			  b_mos_lqo_mult10 smallint DEFAULT NULL,\
			  a_mos_f1_mult10 smallint DEFAULT NULL,\
			  a_mos_f2_mult10 smallint DEFAULT NULL,\
			  a_mos_adapt_mult10 smallint DEFAULT NULL,\
			  b_mos_f1_mult10 smallint DEFAULT NULL,\
			  b_mos_f2_mult10 smallint DEFAULT NULL,\
			  b_mos_adapt_mult10 smallint DEFAULT NULL,\
			  a_rtcp_loss integer DEFAULT NULL,\
			  a_rtcp_maxfr integer DEFAULT NULL,\
			  a_rtcp_avgfr_mult10 integer DEFAULT NULL,\
			  a_rtcp_maxjitter integer DEFAULT NULL,\
			  a_rtcp_avgjitter_mult10 integer DEFAULT NULL,\
			  b_rtcp_loss integer DEFAULT NULL,\
			  b_rtcp_maxfr integer DEFAULT NULL,\
			  b_rtcp_avgfr_mult10 integer DEFAULT NULL,\
			  b_rtcp_maxjitter integer DEFAULT NULL,\
			  b_rtcp_avgjitter_mult10 integer DEFAULT NULL,\
			  a_last_rtp_from_end integer DEFAULT NULL,\
			  b_last_rtp_from_end integer DEFAULT NULL,\
			  payload bigint DEFAULT NULL,\
			  jitter_mult10 integer DEFAULT NULL,\
			  mos_min_mult10 smallint DEFAULT NULL,\
			  a_mos_min_mult10 smallint DEFAULT NULL,\
			  b_mos_min_mult10 smallint DEFAULT NULL,\
			  packet_loss_perc_mult1000 integer DEFAULT NULL,\
			  a_packet_loss_perc_mult1000 integer DEFAULT NULL,\
			  b_packet_loss_perc_mult1000 integer DEFAULT NULL,\
			  delay_sum integer DEFAULT NULL,\
			  a_delay_sum integer DEFAULT NULL,\
			  b_delay_sum integer DEFAULT NULL,\
			  delay_avg_mult100 integer DEFAULT NULL,\
			  a_delay_avg_mult100 integer DEFAULT NULL,\
			  b_delay_avg_mult100 integer DEFAULT NULL,\
			  delay_cnt integer DEFAULT NULL,\
			  a_delay_cnt integer DEFAULT NULL,\
			  b_delay_cnt integer DEFAULT NULL,\
			  rtcp_avgfr_mult10 integer DEFAULT NULL,\
			  rtcp_avgjitter_mult10 integer DEFAULT NULL,\
			  lost integer DEFAULT NULL,\
			  id_sensor integer DEFAULT NULL,\
			  price_operator_mult100 bigint DEFAULT NULL,\
			  price_operator_currency_id smallint DEFAULT NULL,\
			  price_customer_mult100 bigint DEFAULT NULL,\
			  price_customer_currency_id smallint DEFAULT NULL,\
			CONSTRAINT cdr_pkey PRIMARY KEY (id),\
			CONSTRAINT cdr_id_calldate_key UNIQUE (id,calldate),\
			  CONSTRAINT \"fkCdrUaA\" FOREIGN KEY (a_ua_id)\
				  REFERENCES cdr_ua (id) MATCH SIMPLE\
				  ON UPDATE NO ACTION ON DELETE NO ACTION,\
			  CONSTRAINT \"fkCdrUaB\" FOREIGN KEY (b_ua_id)\
				  REFERENCES cdr_ua (id) MATCH SIMPLE\
				  ON UPDATE NO ACTION ON DELETE NO ACTION,\
			  CONSTRAINT \"fkLastSIPresponse\" FOREIGN KEY (lastsipresponse_id)\
				  REFERENCES cdr_sip_response (id) MATCH SIMPLE\
				  ON UPDATE NO ACTION ON DELETE NO ACTION\
			);\
			CREATE INDEX ON cdr (calldate);\
			CREATE INDEX ON cdr (callend);\
			CREATE INDEX ON cdr (duration);\
			CREATE INDEX ON cdr (caller);\
			CREATE INDEX ON cdr (caller_reverse);\
			CREATE INDEX ON cdr (called);\
			CREATE INDEX ON cdr (called_reverse);\
			CREATE INDEX ON cdr (callername);\
			CREATE INDEX ON cdr (callername_reverse);\
			CREATE INDEX ON cdr (sipcallerip);\
			CREATE INDEX ON cdr (sipcalledip);\
			CREATE INDEX ON cdr (lastSIPresponseNum);\
			CREATE INDEX ON cdr (bye);\
			CREATE INDEX ON cdr (a_saddr);\
			CREATE INDEX ON cdr (b_saddr);\
			CREATE INDEX ON cdr (a_lost);\
			CREATE INDEX ON cdr (b_lost);\
			CREATE INDEX ON cdr (a_maxjitter);\
			CREATE INDEX ON cdr (b_maxjitter);\
			CREATE INDEX ON cdr (a_rtcp_loss);\
			CREATE INDEX ON cdr (a_rtcp_maxfr);\
			CREATE INDEX ON cdr (a_rtcp_maxjitter);\
			CREATE INDEX ON cdr (b_rtcp_loss);\
			CREATE INDEX ON cdr (b_rtcp_maxfr);\
			CREATE INDEX ON cdr (b_rtcp_maxjitter);\
			CREATE INDEX ON cdr (a_ua_id);\
			CREATE INDEX ON cdr (b_ua_id);\
			CREATE INDEX ON cdr (a_avgjitter_mult10);\
			CREATE INDEX ON cdr (b_avgjitter_mult10);\
			CREATE INDEX ON cdr (a_rtcp_avgjitter_mult10);\
			CREATE INDEX ON cdr (b_rtcp_avgjitter_mult10);\
			CREATE INDEX ON cdr (lastSIPresponse_id);\
			CREATE INDEX ON cdr (payload);\
			CREATE INDEX ON cdr (id_sensor);\
		");

		this->query("CREATE TABLE IF NOT EXISTS files (\
			datehour serial,\
			id_sensor bigint NULL,\
			sipsize bigint DEFAULT '0',\
			rtpsize bigint DEFAULT '0',\
			graphsize bigint DEFAULT '0',\
			regsize bigint DEFAULT '0',\
			audiosize bigint DEFAULT '0',\
			CONSTRAINT files_pkey PRIMARY KEY (datehour, id_sensor)\
		);");

		this->query("CREATE TABLE IF NOT EXISTS cdr_next (\
			cdr_id integer NOT NULL,\
			calldate timestamp NOT NULL,\
			custom_header1 character varying(255),\
			fbasename character varying(255),\
			match_header character varying(128) DEFAULT NULL,\
			GeoPosition character varying(255) DEFAULT NULL,\
			CONSTRAINT cdr_next_pkey PRIMARY KEY (cdr_id,calldate),\
			CONSTRAINT cdr_next_cdr_id_fkey FOREIGN KEY (cdr_id)\
					REFERENCES cdr (id) MATCH SIMPLE\
					ON UPDATE CASCADE ON DELETE CASCADE);\
		");
		
		this->query("CREATE TABLE IF NOT EXISTS cdr_proxy (\
			id serial NOT NULL,\
			cdr_id bigint NOT NULL,\
			calldate timestamp NOT NULL,\
			src bigint,\
			dst bigint,\
			CONSTRAINT cdr_proxy_pkey PRIMARY KEY (id,calldate),\
			CONSTRAINT cdr_proxy_cdr_id_fkey FOREIGN KEY (cdr_id)\
				  REFERENCES cdr (id) MATCH SIMPLE\
				  ON UPDATE CASCADE ON DELETE CASCADE);\
		");
		
		this->query("CREATE TABLE IF NOT EXISTS cdr_rtp (\
				id serial,\
				cdr_id bigint,\
				calldate timestamp NOT NULL,\
				saddr bigint NULL,\
				daddr bigint NULL,\
				dport integer DEFAULT NULL,\
				ssrc bigint NULL,\
				received bigint NULL,\
				loss bigint NULL,\
				firsttime real NULL,\
				payload smallint NULL,\
				maxjitter_mult10 smallint DEFAULT NULL,\
				CONSTRAINT cdr_rtp_pkey PRIMARY KEY (id,calldate),\
				CONSTRAINT cdr_rtp_cdr_id_fkey FOREIGN KEY (cdr_id)\
					REFERENCES cdr (id) MATCH SIMPLE\
					ON UPDATE CASCADE ON DELETE CASCADE);\
		");
		
		this->query("CREATE TABLE IF NOT EXISTS cdr_dtmf (\
			id serial,\
			cdr_id bigint,\
			calldate timestamp NOT NULL,\
			daddr bigint DEFAULT NULL,\
			saddr bigint DEFAULT NULL,\
			firsttime real NULL,\
			dtmf char NULL,\
			CONSTRAINT cdr_dtmf_pkey PRIMARY KEY (id,calldate),\
			CONSTRAINT cdr_dtmf_cdr_id_fkey FOREIGN KEY (cdr_id)\
				REFERENCES cdr (id) MATCH SIMPLE\
				ON UPDATE CASCADE ON DELETE CASCADE);\
		");
	  
	  	this->query("CREATE TABLE IF NOT EXISTS contenttype (\
				id serial,\
				contenttype character varying(255) NULL,\
				CONSTRAINT contenttype_pkey PRIMARY KEY (id)\
			);\
			CREATE INDEX ON contenttype (contenttype);\
		");
		
		this->query("CREATE TABLE IF NOT EXISTS register (\
			id serial,\
			id_sensor bigint NOT NULL,\
			fname bigint default NULL,\
			calldate timestamp NOT NULL,\
			sipcallerip bigint NOT NULL,\
			sipcalledip bigint NOT NULL,\
			from_num character varying(255) DEFAULT NULL,\
			from_name character varying(255) DEFAULT NULL,\
			from_domain character varying(255) DEFAULT NULL,\
			to_num character varying(255) DEFAULT NULL,\
			to_domain character varying(255) DEFAULT NULL,\
			contact_num character varying(255) DEFAULT NULL,\
			contact_domain character varying(255) DEFAULT NULL,\
			digestusername character varying(255) DEFAULT NULL,\
			digestrealm character varying(255) DEFAULT NULL,\
			expires integer DEFAULT NULL,\
			expires_at  timestamp DEFAULT NULL,\
			state  smallint DEFAULT NULL,\
			ua_id bigint DEFAULT NULL,\
		CONSTRAINT register_pkey PRIMARY KEY (id));\
		CREATE INDEX ON register (calldate);\
		CREATE INDEX ON register (sipcallerip);\
		CREATE INDEX ON register (sipcalledip);\
		CREATE INDEX ON register (from_num);\
		CREATE INDEX ON register (digestusername);\
		");
		
		this->query("CREATE TABLE IF NOT EXISTS register_failed (\
			id serial,\
			id_sensor bigint NOT NULL,\
			fname bigint default NULL,\
			counter bigint default '0',\
			created_at timestamp NOT NULL,\
			sipcallerip bigint NOT NULL,\
			sipcalledip bigint NOT NULL,\
			from_num character varying(255) DEFAULT NULL,\
			to_num character varying(255) DEFAULT NULL,\
			contact_num character varying(255) DEFAULT NULL,\
			contact_domain character varying(255) DEFAULT NULL,\
			digestusername character varying(255) DEFAULT NULL,\
			ua_id bigint DEFAULT NULL,\
			to_domain character varying(255) DEFAULT NULL,\
		CONSTRAINT register_failed_pkey PRIMARY KEY (id));\
		CREATE INDEX ON register_failed (created_at);\
		CREATE INDEX ON register_failed (sipcallerip);\
		CREATE INDEX ON register_failed (sipcalledip);\
		");
		
		
		this->query("CREATE TABLE IF NOT EXISTS sensor_conf (\
		  id serial,\
		  id_sensor bigint DEFAULT NULL,\
		  interface character varying (255) DEFAULT NULL,\
		  threading_mod smallint DEFAULT '1',\
		  mirror_destination_ip bigint DEFAULT NULL,\
		  mirror_destination_port smallint DEFAULT NULL,\
		  mirror_bind_ip bigint DEFAULT NULL,\
		  mirror_bind_port smallint DEFAULT '5030',\
		  mirror_bind_dlt bigint  DEFAULT '1',\
		  scanpcapdir character varying (255) DEFAULT NULL,\
		  scanpcapmethod character varying (255) DEFAULT 'newfile',\
		  natalias text,\
		  sdp_reverse_ipport smallint DEFAULT '0',\
		  managerip character varying (255) DEFAULT '127.0.0.1',\
		  httpport character varying (255) DEFAULT NULL,\
		  sipport character varying (255) DEFAULT '5060',\
		  cdr_sipport smallint DEFAULT '1',\
		  destination_number_mode smallint DEFAULT '1',\
		  onowaytimeout bigint DEFAULT '15',\
		  rtptimeout bigint DEFAULT '30',\
		  ringbuffer bigint DEFAULT '50',\
		  packetbuffer_enable smallint DEFAULT '1',\
		  packetbuffer_total_maxheap bigint DEFAULT '2000',\
		  packetbuffer_compress smallint DEFAULT '1',\
		  packetbuffer_file_totalmaxsize smallint DEFAULT '0',\
		  packetbuffer_file_path character varying (255) DEFAULT '/var/spool/voipmonitor/packetbuffer',\
		  rtpthreads bigint DEFAULT NULL,\
		  jitterbuffer_f1 smallint DEFAULT '1',\
		  jitterbuffer_f2 smallint DEFAULT '1',\
		  jitterbuffer_adapt smallint DEFAULT '1',\
		  callslimit bigint DEFAULT '0',\
		  cdrproxy smallint DEFAULT '1',\
		  cdr_ua_enable smallint DEFAULT '1',\
		  \"rtp-firstleg\" smallint DEFAULT '0',\
		  \"allow-zerossrc\" smallint DEFAULT '0',\
		  deduplicate smallint DEFAULT '0',\
		  deduplicate_ipheader smallint DEFAULT '1',\
		  sipoverlap smallint DEFAULT '1',\
		  \"sip-register\" smallint DEFAULT '0',\
		  \"sip-register-active-nologbin\" smallint DEFAULT '1',\
		  nocdr smallint DEFAULT '0',\
		  skipdefault smallint DEFAULT '0',\
		  cdronlyanswered smallint DEFAULT '0',\
		  cdronlyrtp smallint DEFAULT '0',\
		  maxpcapsize bigint DEFAULT NULL,\
		  savesip smallint DEFAULT '1',\
		  savertp smallint DEFAULT '1',\
		  pcapsplit smallint DEFAULT '1',\
		  savertcp smallint DEFAULT '1',\
		  saveaudio character varying (255) DEFAULT NULL,\
		  saveaudio_reversestereo smallint DEFAULT '0',\
		  keycheck text,\
		  saverfc2833 smallint DEFAULT '0',\
		  dtmf2db smallint DEFAULT '0',\
		  savegraph character varying (255) DEFAULT 'plain',\
		  \"norecord-header\" smallint DEFAULT '0',\
		  \"norecord-dtmf\" smallint DEFAULT '0',\
		  pauserecordingdtmf character varying (255) DEFAULT NULL,\
		  convert_dlt_sll2en10 smallint DEFAULT '0',\
		  mos_g729 smallint DEFAULT '0',\
		  mos_lqo smallint DEFAULT '0',\
		  mos_lqo_bin character varying (255) DEFAULT 'pesq',\
		  mos_lqo_ref character varying (255) DEFAULT '/usr/local/share/voipmonitor/audio/mos_lqe_original.wav',\
		  mos_lqo_ref16 character varying (255) DEFAULT '/usr/local/share/voipmonitor/audio/mos_lqe_original_16khz.wav',\
		  dscp smallint DEFAULT '1',\
		  custom_headers text,\
		  custom_headers_message text,\
		  matchheader text,\
		  domainport smallint DEFAULT '0',\
		  pcapcommand text,\
		  filtercommand text,\
		  filter text,\
		  openfile_max bigint DEFAULT NULL,\
		  convertchar text,\
		  spooldir character varying (255) DEFAULT '/var/spool/voipmonitor',\
		  spooldiroldschema smallint DEFAULT '0',\
		  cleandatabase_cdr bigint DEFAULT '0',\
		  cleandatabase_register_failed bigint DEFAULT '0',\
		  cleandatabase bigint DEFAULT '0',\
		  maxpoolsize bigint DEFAULT '102400',\
		  maxpooldays bigint DEFAULT '0',\
		  maxpoolsipsize bigint DEFAULT '0',\
		  maxpoolsipdays bigint DEFAULT '0',\
		  maxpoolrtpsize bigint DEFAULT '0',\
		  maxpoolrtpdays bigint DEFAULT '0',\
		  maxpoolgraphsize bigint DEFAULT '0',\
		  maxpoolgraphdays bigint DEFAULT '0',\
		  cachedir text,\
		  promisc smallint DEFAULT '1',\
		  sqlcallend smallint DEFAULT '1',\
		  cdr_partition smallint DEFAULT '1',\
		  disable_partition_operations smallint DEFAULT '0',\
		  upgrade_try_http_if_https_fail smallint DEFAULT '1',\
		  opt_saveaudio_reversestereo smallint DEFAULT '0',\
		  onewaytimeout smallint DEFAULT '15',\
		  \"sip-register-timeout\" smallint DEFAULT '5',\
		  CONSTRAINT filter_sensor_conf_pkey PRIMARY KEY (id)\
		);");
		
		this->query("CREATE TABLE IF NOT EXISTS sensors (\
		  id_sensor bigint NOT NULL,\
		  host character varying(255) DEFAULT NULL,\
		  port bigint DEFAULT NULL,\
		  CONSTRAINT sensors_pkey PRIMARY KEY (id_sensor)\
		);");
		
		this->query("CREATE OR REPLACE FUNCTION iif(boolean, double precision, double precision)\
					  RETURNS double precision AS\
					$BODY$\
					DECLARE\
						rtVal double precision;\
					BEGIN\
						rtVal := (SELECT case $1 when true then $2 else $3 end);\
						RETURN rtVal;\
					END;\
					$BODY$\
					  LANGUAGE plpgsql IMMUTABLE\
					  COST 100;");
		
		this->query("CREATE OR REPLACE FUNCTION subtime(time1 timestamp without time zone, time2 interval)\
					  RETURNS timestamp without time zone AS\
					$BODY$BEGIN\
						RETURN  time1 - time2;\
					END;$BODY$\
					  LANGUAGE plpgsql VOLATILE\
					  COST 100;");
		
		this->query("CREATE OR REPLACE FUNCTION \"trim\"(strval character varying)\
					  RETURNS character varying AS\
					$BODY$BEGIN\
						RETURN rtrim(ltrim(strval));\
					END;$BODY$\
					  LANGUAGE plpgsql VOLATILE\
					  COST 100;");
		
		this->query("CREATE OR REPLACE FUNCTION unix_timestamp(ts timestamp without time zone)\
					  RETURNS bigint AS\
					$BODY$BEGIN\
						RETURN  EXTRACT(EPOCH FROM ts);\
					END;$BODY$\
					  LANGUAGE plpgsql VOLATILE\
					  COST 100;");
		// END POSTGRESQL
	} else {
		// SQL SERVER
		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'filter_ip') BEGIN\
			CREATE TABLE filter_ip (\
				id int PRIMARY KEY IDENTITY,\
				ip bigint NULL,\
				mask int NULL,\
				direction tinyint DEFAULT '0',\
				rtp tinyint DEFAULT '0',\
				sip tinyint DEFAULT '0',\
				register tinyint DEFAULT '0',\
				graph tinyint DEFAULT '0',\
				wav tinyint DEFAULT '0',\
				skip tinyint DEFAULT '0',\
				script tinyint DEFAULT '0',\
				mos_lqo tinyint DEFAULT '0',\
				note text);\
		END");

		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'filter_telnum') BEGIN\
			CREATE TABLE filter_telnum (\
				id int PRIMARY KEY IDENTITY,\
				prefix bigint NULL,\
				fixed_len int DEFAULT '0',\
				direction tinyint DEFAULT '0',\
				rtp tinyint DEFAULT '0',\
				sip tinyint DEFAULT '0',\
				register tinyint DEFAULT '0',\
				graph tinyint DEFAULT '0',\
				wav tinyint DEFAULT '0',\
				skip tinyint DEFAULT '0',\
				script tinyint DEFAULT '0',\
				mos_lqo tinyint DEFAULT '0',\
				note text);\
		END");

		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'cdr_sip_response') BEGIN\
			CREATE TABLE cdr_sip_response (\
				id mediumint PRIMARY KEY IDENTITY,\
				lastSIPresponse varchar(255) NULL);\
			CREATE UNIQUE INDEX lastSIPresponse ON cdr_sip_response (lastSIPresponse);\
		END");

		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'cdr_ua') BEGIN\
			CREATE TABLE cdr_ua (\
				id int PRIMARY KEY IDENTITY,\
				ua varchar(512) NULL);\
			CREATE UNIQUE INDEX ua ON cdr_ua (ua);\
		END");

		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'cdr') BEGIN\
			CREATE TABLE cdr (\
				ID int PRIMARY KEY IDENTITY,\
				calldate datetime NOT NULL,\
				callend datetime NOT NULL,\
				duration int NULL,\
				connect_duration int NULL,\
				progress_time int NULL,\
				first_rtp_time int NULL,\
				caller varchar(255) NULL,\
				caller_domain varchar(255) NULL,\
				caller_reverse varchar(255) NULL,\
				callername varchar(255) NULL,\
				callername_reverse varchar(255) NULL,\
				called varchar(255) NULL,\
				called_domain varchar(255) NULL,\
				called_reverse varchar(255) NULL,\
				sipcallerip bigint NULL,\
				sipcalledip bigint NULL,\
				whohanged char(10) NULL,\
				bye tinyint NULL,\
				lastSIPresponse_id mediumint NULL\
					FOREIGN KEY REFERENCES cdr_sip_response (id),\
				lastSIPresponseNum smallint NULL,\
				dscp bigint NULL,\
				sighup tinyint NULL,\
				a_index tinyint NULL,\
				b_index tinyint NULL,\
				a_payload int NULL,\
				b_payload int NULL,\
				a_saddr bigint NULL,\
				b_saddr bigint NULL,\
				a_received int NULL,\
				b_received int NULL,\
				a_lost int NULL,\
				b_lost int NULL,\
				a_ua_id int NULL\
					FOREIGN KEY REFERENCES cdr_ua (id),\
				b_ua_id int NULL\
					FOREIGN KEY REFERENCES cdr_ua (id),\
				a_avgjitter_mult10 int NULL,\
				b_avgjitter_mult10 int NULL,\
				a_maxjitter smallint NULL,\
				b_maxjitter smallint NULL,\
				a_sl1 int NULL,\
				a_sl2 int NULL,\
				a_sl3 int NULL,\
				a_sl4 int NULL,\
				a_sl5 int NULL,\
				a_sl6 int NULL,\
				a_sl7 int NULL,\
				a_sl8 int NULL,\
				a_sl9 int NULL,\
				a_sl10 int NULL,\
				a_d50 int NULL,\
				a_d70 int NULL,\
				a_d90 int NULL,\
				a_d120 int NULL,\
				a_d150 int NULL,\
				a_d200 int NULL,\
				a_d300 int NULL,\
				b_sl1 int NULL,\
				b_sl2 int NULL,\
				b_sl3 int NULL,\
				b_sl4 int NULL,\
				b_sl5 int NULL,\
				b_sl6 int NULL,\
				b_sl7 int NULL,\
				b_sl8 int NULL,\
				b_sl9 int NULL,\
				b_sl10 int NULL,\
				b_d50 int NULL,\
				b_d70 int NULL,\
				b_d90 int NULL,\
				b_d120 int NULL,\
				b_d150 int NULL,\
				b_d200 int NULL,\
				b_d300 int NULL,\
				a_mos_lqo_mult10 tinyint NULL,\
				b_mos_lqo_mult10 tinyint NULL,\
				a_mos_f1_mult10 tinyint NULL,\
				a_mos_f2_mult10 tinyint NULL,\
				a_mos_adapt_mult10 tinyint NULL,\
				b_mos_f1_mult10 tinyint NULL,\
				b_mos_f2_mult10 tinyint NULL,\
				b_mos_adapt_mult10 tinyint NULL,\
				a_rtcp_loss smallint NULL,\
				a_rtcp_maxfr smallint NULL,\
				a_rtcp_avgfr_mult10 smallint NULL,\
				a_rtcp_maxjitter smallint NULL,\
				a_rtcp_avgjitter_mult10 smallint NULL,\
				b_rtcp_loss smallint NULL,\
				b_rtcp_maxfr smallint NULL,\
				b_rtcp_avgfr_mult10 smallint NULL,\
				b_rtcp_maxjitter smallint NULL,\
				b_rtcp_avgjitter_mult10 smallint NULL,\
				a_last_rtp_from_end smallint NULL,\
				b_last_rtp_from_end smallint NULL,\
				payload int NULL,\
				jitter_mult10 int NULL,\
				mos_min_mult10 tinyint NULL,\
				a_mos_min_mult10 tinyint NULL,\
				b_mos_min_mult10 tinyint NULL,\
				packet_loss_perc_mult1000 int NULL,\
				a_packet_loss_perc_mult1000 int NULL,\
				b_packet_loss_perc_mult1000 int NULL,\
				delay_sum int NULL,\
				a_delay_sum int NULL,\
				b_delay_sum int NULL,\
				delay_avg_mult100 int NULL,\
				a_delay_avg_mult100 int NULL,\
				b_delay_avg_mult100 int NULL,\
				delay_cnt int NULL,\
				a_delay_cnt int NULL,\
				b_delay_cnt int NULL,\
				rtcp_avgfr_mult10 smallint NULL,\
				rtcp_avgjitter_mult10 smallint NULL,\
				lost int NULL,\
				id_sensor smallint NULL,);\
			CREATE INDEX calldate ON cdr (calldate);\
			CREATE INDEX callend ON cdr (callend);\
			CREATE INDEX duration ON cdr (duration);\
			CREATE INDEX source ON cdr (caller);\
			CREATE INDEX source_reverse ON cdr (caller_reverse);\
			CREATE INDEX destination ON cdr (called);\
			CREATE INDEX destination_reverse ON cdr (called_reverse);\
			CREATE INDEX callername ON cdr (callername);\
			CREATE INDEX callername_reverse ON cdr (callername_reverse);\
			CREATE INDEX sipcallerip ON cdr (sipcallerip);\
			CREATE INDEX sipcalledip ON cdr (sipcalledip);\
			CREATE INDEX lastSIPresponseNum ON cdr (lastSIPresponseNum);\
			CREATE INDEX bye ON cdr (bye);\
			CREATE INDEX a_saddr ON cdr (a_saddr);\
			CREATE INDEX b_saddr ON cdr (b_saddr);\
			CREATE INDEX a_lost ON cdr (a_lost);\
			CREATE INDEX b_lost ON cdr (b_lost);\
			CREATE INDEX a_maxjitter ON cdr (a_maxjitter);\
			CREATE INDEX b_maxjitter ON cdr (b_maxjitter);\
			CREATE INDEX a_rtcp_loss ON cdr (a_rtcp_loss);\
			CREATE INDEX a_rtcp_maxfr ON cdr (a_rtcp_maxfr);\
			CREATE INDEX a_rtcp_maxjitter ON cdr (a_rtcp_maxjitter);\
			CREATE INDEX b_rtcp_loss ON cdr (b_rtcp_loss);\
			CREATE INDEX b_rtcp_maxfr ON cdr (b_rtcp_maxfr);\
			CREATE INDEX b_rtcp_maxjitter ON cdr (b_rtcp_maxjitter);\
			CREATE INDEX a_ua_id ON cdr (a_ua_id);\
			CREATE INDEX b_ua_id ON cdr (b_ua_id);\
			CREATE INDEX a_avgjitter_mult10 ON cdr (a_avgjitter_mult10);\
			CREATE INDEX b_avgjitter_mult10 ON cdr (b_avgjitter_mult10);\
			CREATE INDEX a_rtcp_avgjitter_mult10 ON cdr (a_rtcp_avgjitter_mult10);\
			CREATE INDEX b_rtcp_avgjitter_mult10 ON cdr (b_rtcp_avgjitter_mult10);\
			CREATE INDEX lastSIPresponse_id ON cdr (lastSIPresponse_id);\
			CREATE INDEX payload ON cdr (payload);\
			CREATE INDEX id_sensor ON cdr (id_sensor);\
		END");

		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'files') BEGIN\
			CREATE TABLE files (\
				datehour int PRIMARY KEY IDENTITY,\
				id_sensor int NULL,\
				sipsize bigint DEFAULT '0',\
				rtpsize bigint DEFAULT '0',\
				graphsize bigint DEFAULT '0',\
				regsize bigint DEFAULT '0',\
				audiosize bigint DEFAULT '0');\
		END");
	
		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'cdr_next') BEGIN\
			CREATE TABLE cdr_next (\
				cdr_ID int PRIMARY KEY NOT NULL\
					FOREIGN KEY REFERENCES cdr (ID),\
				custom_header1 varchar(255) NULL,\
				fbasename varchar(255) NULL);\
			CREATE INDEX fbasename ON cdr_next (fbasename);\
		END");

		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'cdr_proxy') BEGIN\
			CREATE TABLE cdr_proxy (\
				ID int PRIMARY KEY IDENTITY,\
				cdr_ID int NOT NULL\
					FOREIGN KEY REFERENCES cdr (ID),\
				src bigint NULL,\
				dst bigint NULL);\
			CREATE INDEX src ON cdr_proxy (src);\
			CREATE INDEX dst ON cdr_proxy (dst);\
		END");


		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'cdr_rtp') BEGIN\
			CREATE TABLE cdr_rtp (\
				ID int PRIMARY KEY IDENTITY,\
				cdr_ID int \
					FOREIGN KEY REFERENCES cdr (ID),\
				saddr bigint NULL,\
				daddr bigint NULL,\
				ssrc bigint NULL,\
				received int NULL,\
				loss int NULL,\
				firsttime float NULL,\
				payload smallint NULL,\
				maxjitter_mult10 smallint DEFAULT NULL);\
		END");

		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'cdr_dtmf') BEGIN\
			CREATE TABLE cdr_dtmf (\
				ID int PRIMARY KEY IDENTITY,\
				cdr_ID int \
					FOREIGN KEY REFERENCES cdr (ID),\
				firsttime float NULL,\
				dtmf char NULL,\
				daddr bigint DEFAULT NULL,\
				saddr bigint DEFAULT NULL);\
		END");

		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'contenttype') BEGIN\
			CREATE TABLE contenttype (\
				id int PRIMARY KEY IDENTITY,\
				contenttype varchar(255) NULL);\
			CREATE INDEX contenttype ON contenttype (contenttype);\
		END");

		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'message') BEGIN\
			CREATE TABLE message (\
				ID int PRIMARY KEY IDENTITY,\
				id_contenttype int NOT NULL\
					FOREIGN KEY REFERENCES contenttype (id),\
				calldate datetime NOT NULL,\
				caller varchar(255) NULL,\
				caller_domain varchar(255) NULL,\
				caller_reverse varchar(255) NULL,\
				callername varchar(255) NULL,\
				callername_reverse varchar(255) NULL,\
				called varchar(255) NULL,\
				called_domain varchar(255) NULL,\
				called_reverse varchar(255) NULL,\
				sipcallerip bigint NULL,\
				sipcalledip bigint NULL,\
				bye tinyint NULL,\
				lastSIPresponse_id mediumint NULL\
					FOREIGN KEY REFERENCES cdr_sip_response (id),\
				lastSIPresponseNum smallint NULL,\
				id_sensor smallint NULL,\
				a_ua_id int NULL\
					FOREIGN KEY REFERENCES cdr_ua (id),\
				b_ua_id int NULL\
					FOREIGN KEY REFERENCES cdr_ua (id),\
				fbasename varchar(255) NULL,\
				message MEDIUMTEXT);\
			CREATE INDEX calldate ON message (calldate);\
			CREATE INDEX caller ON message (caller);\
			CREATE INDEX caller_domain ON message (caller_domain);\
			CREATE INDEX caller_reverse ON message (caller_reverse);\
			CREATE INDEX callername ON message (callername);\
			CREATE INDEX callername_reverse ON message (callername_reverse);\
			CREATE INDEX called ON message (called);\
			CREATE INDEX called_reverse ON message (called_reverse);\
			CREATE INDEX sipcallerip ON message (sipcallerip);\
			CREATE INDEX sipcalledip ON message (sipcalledip);\
			CREATE INDEX lastSIPresponseNum ON message (lastSIPresponseNum);\
			CREATE INDEX bye ON message (bye);\
			CREATE INDEX lastSIPresponse_id ON message (lastSIPresponse_id);\
			CREATE INDEX id_sensor ON message (id_sensor);\
			CREATE INDEX a_ua_id ON message (a_ua_id);\
			CREATE INDEX b_ua_id ON message (b_ua_id);\
			CREATE INDEX fbasename ON message (fbasename);\
		END");
	
		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'register') BEGIN\
			CREATE TABLE register (\
				ID int PRIMARY KEY IDENTITY,\
				id_sensor int NULL,\
				fname bigint NULL,\
				calldate datetime NOT NULL,\
				sipcallerip bigint NOT NULL,\
				sipcalledip bigint NOT NULL,\
				from_num varchar(255) NULL,\
				from_name varchar(255) NULL,\
				from_domain varchar(255) NULL,\
				to_num varchar(255) NULL,\
				to_domain varchar(255) NULL,\
				contact_num varchar(255) NULL,\
				contact_domain varchar(255) NULL,\
				digestusername varchar(255) NULL,\
				digestrealm varchar(255) NULL,\
				expires int NULL,\
				expires_at datetime NULL,\
				state tinyint NULL,\
				ua_id int NULL);\
			CREATE INDEX calldate ON register (calldate);\
			CREATE INDEX sipcallerip ON register (sipcallerip);\
			CREATE INDEX sipcalledip ON register (sipcalledip);\
			CREATE INDEX from_num ON register (from_num);\
			CREATE INDEX digestusername ON register (digestusername)\
		END");

		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'register_state') BEGIN\
			CREATE TABLE register_state (\
				ID int PRIMARY KEY IDENTITY,\
				id_sensor int NULL,\
				fname bigint NULL,\
				created_at datetime NOT NULL,\
				sipcallerip bigint NOT NULL,\
				sipcalledip bigint NOT NULL,\
				from_num varchar(255) NULL,\
				to_num varchar(255) NULL,\
				contact_num varchar(255) NULL,\
				contact_domain varchar(255) NULL,\
				digestusername varchar(255) NULL,\
				expires int NULL,\
				state tinyint NULL,\
				ua_id int NULL);\
			CREATE INDEX created_at ON register_state (created_at);\
			CREATE INDEX sipcallerip ON register_state (sipcallerip);\
			CREATE INDEX sipcalledip ON register_state (sipcalledip);\
		END");
	
		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'register_failed') BEGIN\
			CREATE TABLE register_failed (\
				ID int PRIMARY KEY IDENTITY,\
				id_sensor int NULL,\
				fname bigint NULL,\
				counter int DEFAULT 0,\
				created_at datetime NOT NULL,\
				sipcallerip bigint NOT NULL,\
				sipcalledip bigint NOT NULL,\
				from_num varchar(255) NULL,\
				to_num varchar(255) NULL,\
				contact_num varchar(255) NULL,\
				contact_domain varchar(255) NULL,\
				digestusername varchar(255) NULL,\
				ua_id int NULL);\
			CREATE INDEX created_at ON register_failed (created_at);\
			CREATE INDEX sipcallerip ON register_failed (sipcallerip);\
			CREATE INDEX sipcalledip ON register_failed (sipcalledip);\
		END");
	
		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'sensors') BEGIN\
			CREATE TABLE sensors (\
			id_sensor int PRIMARY KEY IDENTITY,\
			host varchar(255) NULL,\
			port int NULL,);\
		END");

		if(opt_ipaccount) {
		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'ipacc') BEGIN\
			CREATE TABLE ipacc (\
				saddr bigint NOT NULL,\
				src_id_customer int NOT NULL,\
				daddr bigint NOT NULL,\
				dst_id_customer int NOT NULL,\
				port smallint NOT NULL,\
				proto smallint NOT NULL,\
				octects int NOT NULL,\
				numpackets int NOT NULL,\
				interval_time datetime NOT NULL,\
				voip int NOT NULL\
				do_agr_trigger tinyint NOT NULL);\
			CREATE INDEX saddr ON ipacc (saddr);\
			CREATE INDEX src_id_customer ON ipacc (src_id_customer);\
			CREATE INDEX daddr ON ipacc (daddr);\
			CREATE INDEX dst_id_customer ON ipacc (dst_id_customer);\
			CREATE INDEX port ON ipacc (port);\
			CREATE INDEX proto ON ipacc (proto);\
			CREATE INDEX interval_time ON ipacc (interval_time)\
		END");
		}
	
		this->query(
		"IF NOT EXISTS (SELECT * FROM sys.objects WHERE name = 'livepacket') BEGIN\
			CREATE TABLE livepacket (\
				id int PRIMARY KEY IDENTITY,\
				id_sensor INT NULL,\
				sipcallerip BIGINT NOT NULL ,\
				sipcalledip BIGINT NOT NULL ,\
				sport SMALLINT NOT NULL ,\
				dport SMALLINT NOT NULL ,\
				istcp TINYINT NOT NULL ,\
				created_at TIMESTAMP NOT NULL ,\
				microseconds INT NOT NULL ,\
				callid VARCHAR(255) NOT NULL ,\
				description VARCHAR(1024),\
				data VARBINARY(8000) NOT NULL);\
			CREATE INDEX created_at__microseconds ON livepacket (created_at, microseconds)\
		END");
	
		sql_noerror = 1;

		//5.2 -> 5.3
		if(opt_match_header[0] != '\0') {
			this->query("ALTER TABLE cdr_next\
					ADD match_header VARCHAR(128)");
			this->query("CREATE INDEX match_header ON cdr_next (match_header)");
		}
		//5.3 -> 5.4
		this->query("CREATE INDEX to_domain ON register (to_domain)");
		this->query("CREATE INDEX to_num ON register (to_num)");
		this->query("ALTER TABLE register_state\
				ADD to_domain varchar(255) NULL;");
		this->query("ALTER TABLE register_failed\
				ADD to_domain varchar(255) NULL;");
		//5.4 -> 5.5
		this->query("ALTER TABLE register_state\
				ADD sipcalledip bigint");
		this->query("CREATE INDEX sipcalledip ON register_state (sipcalledip)");
		this->query("ALTER TABLE register_failed\
				ADD sipcalledip bigint");
		this->query("CREATE INDEX sipcalledip ON register_failed (sipcalledip)");
		//6.0 -> 6.1
		this->query("ALTER TABLE message\
				ADD id_contenttype INT");
		this->query("CREATE INDEX id_contenttype ON message (id_contenttype)");

		//6.5RC2 ->
		this->query("ALTER TABLE message\
				ADD GeoPosition varchar(255) NULL;");
		this->query("ALTER TABLE cdr\
				ADD GeoPosition varchar(255) NULL;");
		this->query("ALTER TABLE register\
				ADD fname BIGINT NULL;");
		this->query("ALTER TABLE register_failed\
				ADD fname BIGINT NULL;");
		this->query("ALTER TABLE register_state\
				ADD fname BIGINT NULL;");
		this->query("ALTER TABLE register\
				ADD id_sensor INT NULL;");
		this->query("ALTER TABLE register_failed\
				ADD id_sensor INT NULL;");
		this->query("ALTER TABLE register_state\
				ADD id_sensor INT NULL;");

		this->query("ALTER TABLE filter_ip\
				ADD skip tinyint NULL;");
		this->query("ALTER TABLE filter_telnum\
				ADD skip tinyint NULL;");
	
		//8.0
		if(opt_dscp) {
			this->query("ALTER TABLE cdr ADD dscp int NULL");
		}
	
		//8.2
		this->query("ALTER TABLE filter_ip\
				ADD script tinyint NULL;");
		this->query("ALTER TABLE filter_telnum\
				ADD script tinyint NULL;");

		this->query("ALTER TABLE filter_ip\
				ADD mos_lqo tinyint NULL;");
		this->query("ALTER TABLE filter_telnum\
				ADD mos_lqo tinyint NULL;");

		if(opt_dscp) {
			this->query("ALTER TABLE filter_telnum ADD dscp bigint NULL;");
		}
	
		sql_noerror = 0;
	
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'concat' AND type = 'FN') DROP FUNCTION dbo.concat");
		this->query("CREATE FUNCTION dbo.concat(@str1 VARCHAR(MAX),@str2 VARCHAR(MAX))\
				RETURNS VARCHAR(MAX) AS\
				BEGIN\
					RETURN @str1 + @str2\
				END");
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'trim' AND type = 'FN') DROP FUNCTION dbo.trim");
		this->query("CREATE FUNCTION dbo.trim(@str VARCHAR(MAX))\
				RETURNS VARCHAR(MAX) AS\
				BEGIN\
					RETURN LTRIM(RTRIM(@str))\
				END");
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'div' AND type = 'FN') DROP FUNCTION dbo.div");
		this->query("CREATE FUNCTION dbo.div(@oper1 FLOAT,@oper2 FLOAT)\
				RETURNS FLOAT AS\
				BEGIN\
					RETURN CASE WHEN (@oper2 is NULL or @oper2=0) THEN NULL ELSE @oper1/@oper2 END;\
				END");
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'iif' AND type = 'FN') DROP FUNCTION dbo.iif");
		this->query("CREATE FUNCTION dbo.iif(@rsltCond VARCHAR(MAX),@rslt1 VARCHAR(MAX),@rslt2 VARCHAR(MAX))\
				RETURNS FLOAT AS\
				BEGIN\
					RETURN CAST((CASE WHEN (@rsltCond is not NULL and @rsltCond<>0) THEN @rslt1 ELSE @rslt2 END) as FLOAT);\
				END");
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'greatest' AND type = 'FN') DROP FUNCTION dbo.greatest");
		this->query("CREATE FUNCTION dbo.greatest(@par1 FLOAT,@par2 FLOAT)\
				RETURNS FLOAT AS\
				BEGIN\
					RETURN CASE WHEN @par1>@par2 THEN @par1 ELSE coalesce(@par2, @par1) END;\
				END");
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'least' AND type = 'FN') DROP FUNCTION dbo.least");
		this->query("CREATE FUNCTION dbo.least(@par1 FLOAT,@par2 FLOAT)\
				RETURNS FLOAT AS\
				BEGIN\
					RETURN CASE WHEN @par1<@par2 THEN @par1 ELSE coalesce(@par2, @par1) END;\
				END");
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'inet_aton' AND type = 'FN') DROP FUNCTION dbo.inet_aton;");
		this->query("CREATE FUNCTION dbo.inet_aton (@ipstr VARCHAR(15))\
				RETURNS BIGINT AS\
				BEGIN\
					RETURN CAST(\
						CAST((256*256*256) as BIGINT) * PARSENAME(@ipstr, 4) + \
						256*256 * PARSENAME(@ipstr, 3) +\
						256 * PARSENAME(@ipstr, 2) +\
						1 * PARSENAME(@ipstr, 1) AS BIGINT);\
				END");
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'inet_ntoa' AND type = 'FN') DROP FUNCTION dbo.inet_ntoa");
		this->query("CREATE FUNCTION dbo.inet_ntoa(@ipnumber BIGINT)\
				RETURNS VARCHAR(15) AS\
				BEGIN\
					RETURN CAST(\
						CAST(@ipnumber/(256*256*256) as VARCHAR(3)) + '.' +\
						CAST(@ipnumber%(256*256*256)/(256*256) as VARCHAR(3)) + '.' +\
						CAST(@ipnumber%(256*256)/(256) as VARCHAR(3)) + '.' +\
						CAST(@ipnumber%256 as VARCHAR(3)) as VARCHAR(15));\
				END");
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'unix_timestamp' AND type = 'FN') DROP FUNCTION dbo.unix_timestamp");
		this->query("CREATE FUNCTION dbo.unix_timestamp(@ctimestamp DATETIME)\
				RETURNS INTEGER AS\
				BEGIN\
					RETURN DATEDIFF(SECOND, '1970-01-01', @ctimestamp)\
				END");
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'now' AND type = 'FN') DROP FUNCTION dbo.now");
		this->query("CREATE FUNCTION now()\
				RETURNS DATETIME AS\
				BEGIN\
					RETURN GETDATE()\
				END");
		this->query("IF EXISTS (SELECT name FROM sys.objects WHERE name = 'subtime' AND type = 'FN') DROP FUNCTION dbo.subtime");
		this->query("CREATE FUNCTION dbo.subtime(@time1 DATETIME, @time2 DATETIME)\
				RETURNS DATETIME AS\
				BEGIN\
					RETURN DATEADD(SECOND, -DATEDIFF(second, 0, @time2), @time1)\
				END");
			}
}

void SqlDb_odbc::createTable(const char *tableName) {
	if(!strcmp(tableName, "fraud_alert_info")) {
		if (this->getSubtypeDb()=="pgsql") {
			this->query("SELECT * FROM pg_catalog.pg_tables WHERE tablename LIKE '%alerts%'");
			if(this->fetchRow()) {
				this->query(
				"CREATE TABLE IF NOT EXISTS fraud_alert_info (\
						`id` serial,\
						`alert_id` bigint NOT NULL,\
						`at` timestamp NOT NULL,\
						`alert_info` TEXT NOT NULL,\
						CONSTRAINT fraud_alert_info_pkey PRIMARY KEY (id),\
						CONSTRAINT \"fraud_alert_info_fk1\" FOREIGN KEY (alert_id)\
				  REFERENCES alerts (id) MATCH SIMPLE\
				  ON UPDATE NO ACTION ON DELETE CASCADE);");
			}
		}
	}
}

void SqlDb_odbc::checkDbMode() {
}

void SqlDb_odbc::checkSchema() {
}

void createMysqlPartitionsCdr() {
	syslog(LOG_NOTICE, "create cdr partitions - begin");
	SqlDb *sqlDb = createSqlObject();
	if(cloud_host[0]) {
		sqlDb->setMaxQueryPass(1);
		sqlDb->query(
			"call create_partitions_cdr(0);");
		sqlDb->query(
			"call create_partitions_cdr(1);");
	} else {
		sqlDb->query(
			string("call `") + mysql_database + "`.create_partitions_cdr('" + mysql_database + "', 0);");
		sqlDb->query(
			string("call `") + mysql_database + "`.create_partitions_cdr('" + mysql_database + "', 1);");
	}
	delete sqlDb;
	syslog(LOG_NOTICE, "create cdr partitions - end");
}

void createMysqlPartitionsIpacc() {
	SqlDb *sqlDb = createSqlObject();
	syslog(LOG_NOTICE, "create ipacc partitions - begin");
	if(cloud_host[0]) {
		sqlDb->setMaxQueryPass(1);
		sqlDb->query(
			"call create_partitions_ipacc(0);");
		sqlDb->query(
			"call create_partitions_ipacc(1);");
	} else {
		sqlDb->query(
			string("call `") + mysql_database + "`.create_partitions_ipacc('" + mysql_database + "', 0);");
		sqlDb->query(
			string("call `") + mysql_database + "`.create_partitions_ipacc('" + mysql_database + "', 1);");
	}
	delete sqlDb;
	syslog(LOG_NOTICE, "create ipacc partitions - end");
}

void createMysqlPartitionsBillingAgregation() {
	SqlDb *sqlDb = createSqlObject();
	sqlDb->query("show tables like 'billing'");
	if(!sqlDb->fetchRow()) {
		delete sqlDb;
		return;
	}
	sqlDb->query("show tables like 'billing_agregation_day_addresses'");
	if(!sqlDb->fetchRow()) {
		delete sqlDb;
		return;
	}
	sqlDb->query("select * from billing");
	if(!sqlDb->fetchRow()) {
		delete sqlDb;
		return;
	}
	syslog(LOG_NOTICE, "create billing agregation partitions - begin");
	
	if(cloud_host[0]) {
		sqlDb->setMaxQueryPass(1);
	}
	sqlDb->query(
		"call billing_agregation_create_parts();");
	delete sqlDb;
	syslog(LOG_NOTICE, "create billing agregation partitions - end");
}

void dropMysqlPartitionsCdr() {
	extern int opt_cleandatabase_cdr;
	extern int opt_cleandatabase_http_enum;
	extern int opt_cleandatabase_webrtc;
	extern int opt_cleandatabase_register_state;
	extern int opt_cleandatabase_register_failed;
	static unsigned long counterDropPartitions = 0;
	if(opt_cleandatabase_cdr > 0 ||
	   (opt_enable_http_enum_tables && opt_cleandatabase_http_enum > 0) ||
	   (opt_enable_webrtc_table && opt_cleandatabase_webrtc > 0) ||
	   opt_cleandatabase_register_state > 0 ||
	   opt_cleandatabase_register_failed > 0) {
		syslog(LOG_NOTICE, "drop old partitions - begin");
		SqlDb *sqlDb = createSqlObject();
		sqlDb->setDisableNextAttemptIfError();
		if(opt_cleandatabase_cdr > 0) {
			time_t act_time = time(NULL);
			time_t prev_day_time = act_time - opt_cleandatabase_cdr * 24 * 60 * 60;
			struct tm *prevDayTime = localtime(&prev_day_time);
			char limitPartName[20] = "";
			strftime(limitPartName, sizeof(limitPartName), "p%y%m%d", prevDayTime);
			vector<string> partitions;
			if(counterDropPartitions == 0) {
				if(cloud_host[0]) {
					sqlDb->query("explain partitions select * from cdr");
					SqlDb_row row = sqlDb->fetchRow();
					if(row) {
						vector<string> exists_partitions = split(row["partitions"], ',');
						for(size_t i = 0; i < exists_partitions.size(); i++) {
							if(exists_partitions[i] <= limitPartName) {
								partitions.push_back(exists_partitions[i]);
							}
						}
					}
				} else {
					sqlDb->query(string("select partition_name from information_schema.partitions where table_schema='") + 
						     mysql_database+ "' and table_name='cdr' and partition_name<='" + limitPartName+ "' order by partition_name");
					SqlDb_row row;
					while((row = sqlDb->fetchRow())) {
						partitions.push_back(row["partition_name"]);
					}
				}
			} else {
				partitions.push_back(limitPartName);
			}
			for(size_t i = 0; i < partitions.size(); i++) {
				syslog(LOG_NOTICE, "DROP CDR PARTITION %s", partitions[i].c_str());
				sqlDb->query("ALTER TABLE cdr DROP PARTITION " + partitions[i]);
				sqlDb->query("ALTER TABLE cdr_next DROP PARTITION " + partitions[i]);
				sqlDb->query("ALTER TABLE cdr_rtp DROP PARTITION " + partitions[i]);
				sqlDb->query("ALTER TABLE cdr_dtmf DROP PARTITION " + partitions[i]);
				sqlDb->query("ALTER TABLE cdr_proxy DROP PARTITION " + partitions[i]);
				sqlDb->query("ALTER TABLE message DROP PARTITION " + partitions[i]);
			}
		}
		if(opt_enable_http_enum_tables && opt_cleandatabase_http_enum > 0) {
			time_t act_time = time(NULL);
			time_t prev_day_time = act_time - opt_cleandatabase_http_enum * 24 * 60 * 60;
			struct tm *prevDayTime = localtime(&prev_day_time);
			char limitPartName[20] = "";
			strftime(limitPartName, sizeof(limitPartName), "p%y%m%d", prevDayTime);
			vector<string> partitions_http;
			vector<string> partitions_enum;
			if(counterDropPartitions == 0) {
				sqlDb->query(string("select partition_name from information_schema.partitions where table_schema='") + 
					     mysql_database+ "' and table_name='http_jj' and partition_name<='" + limitPartName+ "' order by partition_name");
				SqlDb_row row;
				while((row = sqlDb->fetchRow())) {
					partitions_http.push_back(row["partition_name"]);
				}
				sqlDb->query(string("select partition_name from information_schema.partitions where table_schema='") + 
					     mysql_database+ "' and table_name='enum_jj' and partition_name<='" + limitPartName+ "' order by partition_name");
				while((row = sqlDb->fetchRow())) {
					partitions_enum.push_back(row["partition_name"]);
				}
			} else {
				partitions_http.push_back(limitPartName);
				partitions_enum.push_back(limitPartName);
			}
			for(size_t i = 0; i < partitions_http.size(); i++) {
				syslog(LOG_NOTICE, "DROP HTTP_JJ PARTITION %s", partitions_http[i].c_str());
				sqlDb->query("ALTER TABLE http_jj DROP PARTITION " + partitions_http[i]);
			}
			for(size_t i = 0; i < partitions_enum.size(); i++) {
				syslog(LOG_NOTICE, "DROP ENUM_JJ PARTITION %s", partitions_enum[i].c_str());
				sqlDb->query("ALTER TABLE enum_jj DROP PARTITION " + partitions_enum[i]);
			}
		}
		if(opt_enable_webrtc_table && opt_cleandatabase_webrtc > 0) {
			time_t act_time = time(NULL);
			time_t prev_day_time = act_time - opt_cleandatabase_webrtc * 24 * 60 * 60;
			struct tm *prevDayTime = localtime(&prev_day_time);
			char limitPartName[20] = "";
			strftime(limitPartName, sizeof(limitPartName), "p%y%m%d", prevDayTime);
			vector<string> partitions;
			if(counterDropPartitions == 0) {
				sqlDb->query(string("select partition_name from information_schema.partitions where table_schema='") + 
					     mysql_database+ "' and table_name='webrtc' and partition_name<='" + limitPartName+ "' order by partition_name");
				SqlDb_row row;
				while((row = sqlDb->fetchRow())) {
					partitions.push_back(row["partition_name"]);
				}
			} else {
				partitions.push_back(limitPartName);
			}
			for(size_t i = 0; i < partitions.size(); i++) {
				syslog(LOG_NOTICE, "DROP WEBRTC PARTITION %s", partitions[i].c_str());
				sqlDb->query("ALTER TABLE webrtc DROP PARTITION " + partitions[i]);
			}
		}
		if(opt_cleandatabase_register_state > 0) {
			time_t act_time = time(NULL);
			time_t prev_day_time = act_time - opt_cleandatabase_register_state * 24 * 60 * 60;
			struct tm *prevDayTime = localtime(&prev_day_time);
			char limitPartName[20] = "";
			strftime(limitPartName, sizeof(limitPartName), "p%y%m%d", prevDayTime);
			vector<string> partitions;
			if(counterDropPartitions == 0) {
				if(cloud_host[0]) {
					sqlDb->query("explain partitions select * from register_state");
					SqlDb_row row = sqlDb->fetchRow();
					if(row) {
						vector<string> exists_partitions = split(row["partitions"], ',');
						for(size_t i = 0; i < exists_partitions.size(); i++) {
							if(exists_partitions[i] <= limitPartName) {
								partitions.push_back(exists_partitions[i]);
							}
						}
					}
				} else {
					sqlDb->query(string("select partition_name from information_schema.partitions where table_schema='") + 
						     mysql_database+ "' and table_name='register_state' and partition_name<='" + limitPartName+ "' order by partition_name");
					SqlDb_row row;
					while((row = sqlDb->fetchRow())) {
						partitions.push_back(row["partition_name"]);
					}
				}
			} else {
				partitions.push_back(limitPartName);
			}
			for(size_t i = 0; i < partitions.size(); i++) {
				syslog(LOG_NOTICE, "DROP REGISTER_STATE PARTITION %s", partitions[i].c_str());
				sqlDb->query("ALTER TABLE register_state DROP PARTITION " + partitions[i]);
			}
		}
		if(opt_cleandatabase_register_failed > 0) {
			time_t act_time = time(NULL);
			time_t prev_day_time = act_time - opt_cleandatabase_register_failed * 24 * 60 * 60;
			struct tm *prevDayTime = localtime(&prev_day_time);
			char limitPartName[20] = "";
			strftime(limitPartName, sizeof(limitPartName), "p%y%m%d", prevDayTime);
			vector<string> partitions;
			if(counterDropPartitions == 0) {
				if(cloud_host[0]) {
					sqlDb->query("explain partitions select * from register_failed");
					SqlDb_row row = sqlDb->fetchRow();
					if(row) {
						vector<string> exists_partitions = split(row["partitions"], ',');
						for(size_t i = 0; i < exists_partitions.size(); i++) {
							if(exists_partitions[i] <= limitPartName) {
								partitions.push_back(exists_partitions[i]);
							}
						}
					}
				} else {
					sqlDb->query(string("select partition_name from information_schema.partitions where table_schema='") + 
						     mysql_database+ "' and table_name='register_failed' and partition_name<='" + limitPartName+ "' order by partition_name");
					SqlDb_row row;
					while((row = sqlDb->fetchRow())) {
						partitions.push_back(row["partition_name"]);
					}
				}
			} else {
				partitions.push_back(limitPartName);
			}
			for(size_t i = 0; i < partitions.size(); i++) {
				syslog(LOG_NOTICE, "DROP REGISTER_FAILED PARTITION %s", partitions[i].c_str());
				sqlDb->query("ALTER TABLE register_failed DROP PARTITION " + partitions[i]);
			}
		}
		++counterDropPartitions;
		delete sqlDb;
		syslog(LOG_NOTICE, "drop old partitions - end");
	}
}
