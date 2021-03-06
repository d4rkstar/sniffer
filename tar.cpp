#include <regex.h>
#include <sys/time.h>
#include <string.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/statvfs.h>
#include <curl/curl.h>
#include <cerrno>
#include <json.h>
#include <iomanip>
#include <openssl/sha.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

#include "voipmonitor.h"

#ifdef FREEBSD
#include <sys/uio.h>
#include <sys/thr.h>
#else
#include <sys/sendfile.h>
#endif

#include <algorithm> // for std::min
#include <iostream>

#include "voipmonitor.h"
#include "calltable.h"
#include "rtp.h"
#include "tools.h"
#include "md5.h"
#include "pcap_queue.h"
#include "sql_db.h"
#include "tar.h"
#include "tools.h"
#include "config.h"


using namespace std;

map<unsigned int, int> tartimemap;
pthread_mutex_t tartimemaplock;

extern char opt_chdir[1024];     
volatile unsigned int glob_tar_queued_files;

extern int opt_pcap_dump_tar_compress_sip; //0 off, 1 gzip, 2 lzma
extern int opt_pcap_dump_tar_gzip_sip_level;
extern int opt_pcap_dump_tar_lzma_sip_level;
extern int opt_pcap_dump_tar_compress_rtp;
extern int opt_pcap_dump_tar_gzip_rtp_level;
extern int opt_pcap_dump_tar_lzma_rtp_level;
extern int opt_pcap_dump_tar_compress_graph;
extern int opt_pcap_dump_tar_gzip_graph_level;
extern int opt_pcap_dump_tar_lzma_graph_level;


extern int terminating; 
extern TarQueue tarQueue;
extern volatile unsigned int glob_last_packet_time;

/* magic, version, and checksum */
void
Tar::th_finish()
{
	int i, sum = 0;

	strncpy(tar.th_buf.magic, "ustar  ", 8);

	for (i = 0; i < T_BLOCKSIZE; i++)
		sum += ((char *)(&(tar.th_buf)))[i];
	for (i = 0; i < 8; i++)
		sum += (' ' - tar.th_buf.chksum[i]);
	int_to_oct(sum, tar.th_buf.chksum, 8);
}

/* encode file path */
void
Tar::th_set_path(char *pathname)
{
	char suffix[2] = "";
	
#ifdef DEBUG
	printf("in th_set_path(th, pathname=\"%s\")\n", pathname);
#endif  

	if (tar.th_buf.gnu_longname != NULL)
		free(tar.th_buf.gnu_longname);
	tar.th_buf.gnu_longname = NULL;
	
	/* classic tar format */
	snprintf(tar.th_buf.name, 100, "%s%s", pathname, suffix);
	       
#ifdef DEBUG   
	puts("returning from th_set_path()...");
#endif 
}

/* map a file mode to a typeflag */
void
Tar::th_set_type(mode_t mode)
{       
	tar.th_buf.typeflag = 0; // regular file
}


/* encode device info */
void
Tar::th_set_device(dev_t device)
{
#ifdef DEBUG
	printf("th_set_device(): major = %d, minor = %d\n",
	       major(device), minor(device));
#endif 
	int_to_oct(major(device), tar.th_buf.devmajor, 8);
	int_to_oct(minor(device), tar.th_buf.devminor, 8);
}


/* encode user info */
void
Tar::th_set_user(uid_t uid)
{
	struct passwd *pw;

	pw = getpwuid(uid);
	if (pw != NULL)
		*((char *)mempcpy(tar.th_buf.uname, pw->pw_name, sizeof(tar.th_buf.uname))) = '\0';

	int_to_oct(uid, tar.th_buf.uid, 8);
}


/* encode group info */
void
Tar::th_set_group(gid_t gid)
{
	struct group *gr;

	gr = getgrgid(gid);
	if (gr != NULL)
		*((char *)mempcpy(tar.th_buf.gname, gr->gr_name, sizeof(tar.th_buf.gname))) = '\0';

	int_to_oct(gid, tar.th_buf.gid, 8);
}


/* encode file mode */
void
Tar::th_set_mode( mode_t fmode)
{      
	int_to_oct(fmode, tar.th_buf.mode, 8);
}


/* calculate header checksum */
int    
Tar::th_crc_calc()
{
	int i, sum = 0;

	for (i = 0; i < T_BLOCKSIZE; i++)
		sum += ((unsigned char *)(&(tar.th_buf)))[i];
	for (i = 0; i < 8; i++)
		sum += (' ' - (unsigned char)tar.th_buf.chksum[i]);
       
	return sum;
}      

	       
/* string-octal to integer conversion */
int
Tar::oct_to_int(char *oct)
{
	int i;
       
	sscanf(oct, "%o", &i);

	return i;
}


/* integer to string-octal conversion, no NULL */
void   
Tar::int_to_oct_nonull(int num, char *oct, size_t octlen)
{      
	snprintf(oct, (unsigned long)octlen, "%*lo", (int)octlen - 1, (unsigned long)num);
	oct[octlen - 1] = ' ';
}

int
Tar::tar_init(int oflags, int mode, int options)
{
	memset(&tar, 0, sizeof(TAR));
	
	tar.options = options;
//	tar.type = (type ? type : &default_type);
	tar.oflags = oflags;

/*
	if ((oflags & O_ACCMODE) == O_RDONLY)
		tar.h = libtar_hash_new(256, (libtar_hashfunc_t)path_hashfunc);
	else
		tar.h = libtar_hash_new(16, (libtar_hashfunc_t)dev_hash);
*/
	return 0;
}

/* open a new tarfile handle */
int     
Tar::tar_open(string pathname, int oflags, int mode, int options)
{       
	this->pathname = pathname;
	if (tar_init(oflags, mode, options) == -1)
		return -1;

	if ((options & TAR_NOOVERWRITE) && (oflags & O_CREAT))
		oflags |= O_EXCL;

	if(file_exists(pathname)) {
		int i = 1;
		while(i < 100) {
			stringstream newpathname;
			newpathname << this->pathname;
			newpathname << "." << i;
			if(file_exists(newpathname.str())) {
				continue;
			} else {
				rename(pathname.c_str(), newpathname.str().c_str());
				if(sverb.tar) syslog(LOG_NOTICE, "tar: renaming %s -> %s", pathname.c_str(), newpathname.str().c_str());
				break;
			}
			i++;
		}
	}
	tar.fd = open((char*)this->pathname.c_str(), oflags, mode);
	if (tar.fd == -1)
	{
		return -1;
	}
	return 0;
}

/* write a header block */
int
Tar::th_write()
{      
	int i;
	th_finish();
	i = tar_block_write((const char*)&(tar.th_buf));
	if (i != T_BLOCKSIZE)
	{	       
//		if (i != -1)    
//			errno = EINVAL;
		return -1;
	}
		
#ifdef DEBUG    
	puts("th_write(): returning 0");
#endif	       
	return 0;      
}		       

/* add file contents to a tarchive */
int
Tar::tar_append_buffer(char *buffer, size_t size)
{
	char block[T_BLOCKSIZE];
	int i;
	char *tmp = buffer;
       
	for (i = size; i > T_BLOCKSIZE; i -= T_BLOCKSIZE)
	{
		if (tar_block_write(tmp) == -1)
			return -1;
		tmp += T_BLOCKSIZE;
	}	      
       
	if (i > 0)
	{
		memcpy(block, tmp, i);
		memset(&(block[i]), 0, T_BLOCKSIZE - i);
		if (tar_block_write(block) == -1)
			return -1;
	}	      
       
	return 0;
}


int    
Tar::initZip() {
	if(!this->zipStream) {
		this->zipStream =  new z_stream;
		this->zipStream->zalloc = Z_NULL;
		this->zipStream->zfree = Z_NULL;
		this->zipStream->opaque = Z_NULL;
		if(deflateInit2(this->zipStream, gziplevel, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
			deflateEnd(this->zipStream);
			//this->setError("zip initialize failed");
			return(false);
		} else {
			this->zipBufferLength = 8192*4;
			this->zipBuffer = new char[this->zipBufferLength];
		}
	}
	return(true);
}
       
void 
Tar::flushZip() {
	do {
		this->zipStream->avail_out = this->zipBufferLength;
		this->zipStream->next_out = (unsigned char*)this->zipBuffer;
		if(deflate(this->zipStream, Z_FINISH)) {
			int have = this->zipBufferLength - this->zipStream->avail_out;
			if(::write(tar.fd, (const char*)this->zipBuffer, have) <= 0) {
				//this->setError();
				break;
			};
		}
	} while(this->zipStream->avail_out == 0);
}	

ssize_t
Tar::writeZip(const void *buf, size_t len) {
	int flush = 0;
	if(!this->initZip()) {
		return(false);
	}      
	this->zipStream->avail_in = len;
	this->zipStream->next_in = (unsigned char*)buf;
	do {
		this->zipStream->avail_out = this->zipBufferLength;
		this->zipStream->next_out = (unsigned char*)this->zipBuffer;

		if(deflate(this->zipStream, flush ? Z_FINISH : Z_NO_FLUSH) != Z_STREAM_ERROR) {
			int have = this->zipBufferLength - this->zipStream->avail_out;
			if(::write(tar.fd, (const char*)this->zipBuffer, have) <= 0) {
				//this->setError();
				return(false);
			};     
		} else {
			//this->setError("zip deflate failed");
			return(false);
		}      
	} while(this->zipStream->avail_out == 0);
	return(true);
}      

#ifdef HAVE_LIBLZMA
int
Tar::initLzma() {
	if(!this->lzmaStream) {
		/* initialize xz encoder */
		//uint32_t preset = LZMA_COMPRESSION_LEVEL | (LZMA_COMPRESSION_EXTREME ? LZMA_PRESET_EXTREME : 0);
		lzmaStream = new lzma_stream;
		*lzmaStream = LZMA_STREAM_INIT; 

		int ret_xz = lzma_easy_encoder (this->lzmaStream, lzmalevel, LZMA_CHECK_CRC64);
		if (ret_xz != LZMA_OK) {
			fprintf (stderr, "lzma_easy_encoder error: %d\n", (int) ret_xz);
			return(false);
		}
		if(!zipBuffer) {
			this->zipBufferLength = 8192*4;
			this->zipBuffer = new char[this->zipBufferLength];
		}
	}
	return(true);
}

void 
Tar::flushLzma() {
	int ret_xz;
//	this->lzmaStream->next_in = NULL;
//	this->lzmaStream->avail_in = 0;
	do {
		this->lzmaStream->avail_out = this->zipBufferLength;
		this->lzmaStream->next_out = (unsigned char*)this->zipBuffer;
		ret_xz = lzma_code(this->lzmaStream, LZMA_FINISH);
		if(ret_xz == LZMA_STREAM_END) {
			int have = this->zipBufferLength - this->lzmaStream->avail_out;
			if(::write(tar.fd, (const char*)this->zipBuffer, have) <= 0) {
				//this->setError();
				break;
			};
			break;
		}
		int have = this->zipBufferLength - this->lzmaStream->avail_out;
		if(::write(tar.fd, (const char*)this->zipBuffer, have) <= 0) {
			//this->setError();
			break;
		};
	} while(1);
}	

ssize_t
Tar::writeLzma(const void *buf, size_t len) {
	int ret_xz;
	if(!this->initLzma()) {
		return(false);
	}
	this->lzmaStream->next_in = (const uint8_t*)buf;
	this->lzmaStream->avail_in = len;
	do {
		this->lzmaStream->next_out = (unsigned char*)this->zipBuffer;
		this->lzmaStream->avail_out = this->zipBufferLength;

		/* compress data */
		ret_xz = lzma_code(lzmaStream, LZMA_RUN);

		if ((ret_xz != LZMA_OK) && (ret_xz != LZMA_STREAM_END)) {
			fprintf (stderr, "lzma_code error: %d\n", (int) ret_xz);
			return LZMA_RET_ERROR_COMPRESSION;
		} else {
			int have = this->zipBufferLength - this->lzmaStream->avail_out;
			if(::write(tar.fd, (const char*)this->zipBuffer, have) <= 0) {
				//this->setError();
				return(false);
			}
		}
	} while(this->lzmaStream->avail_out == 0);
	return(true);
}      
#endif

int
Tar::tar_block_write(const char *buf){
	int zip = false;
	int lzma = false;
	switch(tar.qtype) {
	case 1:
		if(opt_pcap_dump_tar_compress_sip == 1) {
			gziplevel = opt_pcap_dump_tar_gzip_sip_level;
			zip = true;
		} else if(opt_pcap_dump_tar_compress_sip == 2) {
			lzmalevel = opt_pcap_dump_tar_lzma_sip_level;
			lzma = true;
		}
		break;
	case 2:
		if(opt_pcap_dump_tar_compress_rtp == 1) {
			gziplevel = opt_pcap_dump_tar_gzip_rtp_level;
			zip = true;
		} else if(opt_pcap_dump_tar_compress_rtp == 2) {
			lzmalevel = opt_pcap_dump_tar_lzma_rtp_level;
			lzma = true;
		}
		break;
	case 3:
		if(opt_pcap_dump_tar_compress_graph == 1) {
			gziplevel = opt_pcap_dump_tar_gzip_graph_level;
			zip = true;
		} else if(opt_pcap_dump_tar_compress_graph == 2) {
			lzmalevel = opt_pcap_dump_tar_lzma_graph_level;
			lzma = true;
		}
		break;
	}
	
	if(zip) {
		writeZip((char *)(buf), T_BLOCKSIZE);
	} else if(lzma){
		writeLzma((char *)(buf), T_BLOCKSIZE);
	} else {
		::write(tar.fd, (char *)(buf), T_BLOCKSIZE);
	}
	
	
	return(T_BLOCKSIZE);
};

Tar::~Tar() {

	if(this->zipStream) {
		flushZip();
		deflateEnd(this->zipStream);
		delete this->zipStream;
	}
#if HAVE_LIBLZMA
	if(this->lzmaStream) {
		flushLzma();
		lzma_end(this->lzmaStream);
		delete this->lzmaStream;
		this->lzmaStream = NULL;
	}
#endif
	if(this->zipBuffer) {
		delete [] this->zipBuffer;
	}
	if(sverb.tar) syslog(LOG_NOTICE, "tar %s deatroyd (destructor)\n", pathname.c_str());
}

void			   
TarQueue::add(string filename, unsigned int time, char *buffer, size_t len){
	glob_tar_queued_files++;
	data_t data;
	data.buffer = buffer;
	data.len = len;
	lock();
	unsigned int year, mon, day, hour, minute;
	char type[12];
	char fbasename[2*1024];
	sscanf(filename.c_str(), "%u-%u-%u/%u/%u/%[^/]/%s", &year, &mon, &day, &hour, &minute, type, fbasename);
//      printf("%s: %u-%u-%u/%u/%u/%s/%s\n", filename.c_str(), year, mon, day, hour, minute, type, fbasename);
	data.filename = fbasename;
	data.year = year;
	data.mon = mon;
	data.day = day;
	if(type[0] == 'S') {
		queue[1][time - time % TAR_MODULO_SECONDS].push_back(data);
	} else if(type[0] == 'R') {
		queue[2][time - time % TAR_MODULO_SECONDS].push_back(data);
	} else if(type[0] == 'G') {
		queue[3][time - time % TAR_MODULO_SECONDS].push_back(data);
	}      
	unlock();
}      

string
qtype2str(int qtype) {
	if(qtype == 1) return "sip";
	else if(qtype == 2) return "rtp";
	else if(qtype == 3) return "graph";
	else return "all";
}

int			    
TarQueue::write(int qtype, unsigned int time, data_t data) {
	glob_tar_queued_files--;
	stringstream tar_dir, tar_name;
	tar_dir << opt_chdir << "/" << data.year << "-" << data.mon << "-" << data.day;
	tar_name << tar_dir.str() << "/" << qtype2str(qtype) << "_" << time << ".tar";
	switch(qtype) {
	case 1:
		switch(opt_pcap_dump_tar_compress_sip) {
		case 1:
			tar_name << ".gz";
			break;
		case 2:
			tar_name << ".xz";
			break;
		}
		break;
	case 2:
		switch(opt_pcap_dump_tar_compress_rtp) {
		case 1:
			tar_name << ".gz";
			break;
		case 2:
			tar_name << ".xz";
			break;
		}
		break;
	case 3:
		switch(opt_pcap_dump_tar_compress_graph) {
		case 1:
			tar_name << ".gz";
			break;
		case 2:
			tar_name << ".xz";
			break;
		}
		break;
	}
	mkdir_r(tar_dir.str(), 0777);
	//printf("tar_name %s\n", tar_name.str().c_str());
       
	Tar *tar = tars[tar_name.str()];
	if(!tar) {
		tar = new Tar;
		if(sverb.tar) syslog(LOG_NOTICE, "new tar %s\n", tar_name.str().c_str());
		tars[tar_name.str()] = tar;
		tar->tar_open(tar_name.str(), O_WRONLY | O_CREAT | O_APPEND, 0777, TAR_GNU);
		tar->tar.qtype = qtype;
		tar->created_at = time;
	}      
       
	//reset and set header
	memset(&(tar->tar.th_buf), 0, sizeof(struct Tar::tar_header));
	tar->th_set_type(0); //s->st_mode, 0 is regular file
	tar->th_set_user(0); //st_uid
	tar->th_set_group(0); //st_gid
	tar->th_set_mode(0); //s->st_mode
	tar->th_set_mtime(time);
	tar->th_set_size(data.len);
	tar->th_set_path((char*)data.filename.c_str());
       
	/* write header */
	if (tar->th_write() != 0) {
		return -1;
	}

	/* if it's a regular file, write the contents as well */
	if(tar->tar_append_buffer(data.buffer, data.len) != 0) {
		delete [] data.buffer;
		return -1;
	}
	delete [] data.buffer;

	return 0;
}

void   
TarQueue::flushQueue() {
	pthread_mutex_lock(&flushlock);
	// get candidate vector which has the biggest datalen in all files 
	int winner_qtype = 0;
	
	vector<data_t> winner;
	unsigned int winnertime = 0;
	size_t maxlen = 0;
	map<unsigned int, vector<data_t> >::iterator it;
	// walk all maps

	while(1) {
		lock();
		maxlen = 0;
		winnertime = 0;
		for(int i = 0; i < 4; i++) {
			//walk map
			for(it = queue[i].begin(); it != queue[i].end(); it++) {
				vector<data_t>::iterator itv;
				size_t sum = 0;
				for(itv = it->second.begin(); itv != it->second.end(); itv++) {
					sum += itv->len;
				}       
				if(sum > maxlen) {
					maxlen = sum;
					winnertime = it->first;
					winner = it->second;
					winner_qtype = i;
				}       
			}       
		}       
		
		if(maxlen > 0) {
			queue[winner_qtype][winnertime].clear();
			queue[winner_qtype].erase(winnertime);
			unlock();
			
			vector<data_t>::iterator itv;
			for(itv = winner.begin(); itv != winner.end(); itv++) {
				this->write(winner_qtype, winnertime, *itv);
			}
			continue;
		} else {
			unlock();
			break;
		}
	}
	// check if tar can be removed from map (check if there are still calls in memory) 
	for(tars_it = tars.begin(); tars_it != tars.end();) {
		Tar *tar = tars_it->second;
		pthread_mutex_lock(&tartimemaplock);
		unsigned int lpt = glob_last_packet_time;
		if(!tartimemap[tar->created_at] and (tar->created_at != (lpt - lpt % TAR_MODULO_SECONDS))) {
			// there are no calls in this time and possible new calls goes to new tar 
			if(sverb.tar) syslog(LOG_NOTICE, "destroying tar %s - (no calls in mem)\n", tars_it->second->pathname.c_str());
			pthread_mutex_unlock(&tartimemaplock);
			delete tars_it->second;
			tars.erase(tars_it++);
		} else {
			pthread_mutex_unlock(&tartimemaplock);
			tars_it++;
		}
	}
	pthread_mutex_unlock(&flushlock);
}

int
TarQueue::queuelen() {
	int len = 0;
	for(int i = 0; i < 4; i++) {
		len += queue[i].size();
	}
	return len;
}

TarQueue::~TarQueue() {

	pthread_mutex_destroy(&mutexlock);
	pthread_mutex_destroy(&flushlock);

	// destroy all tars
	for(map<string, Tar*>::iterator it = tars.begin(); it != tars.end(); it++) {
		delete(it->second);
	}
}      



void *TarQueueThread(void *dummy) {
	// run each second flushQueue
	while(!terminating) {
		tarQueue.flushQueue();
		sleep(1);
	}      
	return NULL;
}      

