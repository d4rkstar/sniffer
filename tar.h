#ifndef TAR_H
#define TAR_H 

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include "config.h"
#ifdef HAVE_LIBLZMA
#include <lzma.h>
#endif


#define T_BLOCKSIZE		512
#define T_NAMELEN	       100
#define T_PREFIXLEN	     155
#define T_MAXPATHLEN	    (T_NAMELEN + T_PREFIXLEN)

/* GNU extensions for typeflag */
#define GNU_LONGNAME_TYPE       'L'
#define GNU_LONGLINK_TYPE       'K'

/* constant values for the TAR options field */
#define TAR_GNU		  1      /* use GNU extensions */
#define TAR_VERBOSE	      2      /* output file info to stdout */
#define TAR_NOOVERWRITE	  4      /* don't overwrite existing files */
#define TAR_IGNORE_EOT	   8      /* ignore double zero blocks as EOF */
#define TAR_CHECK_MAGIC	 16      /* check magic in file header */
#define TAR_CHECK_VERSION       32      /* check version in file header */
#define TAR_IGNORE_CRC	  64      /* ignore CRC in file header */

/* this is obsolete - it's here for backwards-compatibility only */
#define TAR_IGNORE_MAGIC	0

using namespace std;

/* integer to NULL-terminated string-octal conversion */
#define int_to_oct(num, oct, octlen) \
	snprintf((oct), (octlen), "%*lo ", (octlen) - 2, (unsigned long)(num))

class Tar {
public:
	/* our version of the tar header structure */
	struct tar_header
	{       
		char name[100];
		char mode[8];
		char uid[8];
		char gid[8];    
		char size[12];  
		char mtime[12]; 
		char chksum[8];
		char typeflag;
		char linkname[100];
		char magic[6];
		char version[2];
		char uname[32];
		char gname[32];
		char devmajor[8];
		char devminor[8];
		char prefix[155];
		char padding[12];
		char *gnu_longname;
		char *gnu_longlink;
	};
	typedef int (*openfunc_t)(const char *, int, ...);
	typedef int (*closefunc_t)(int);
	typedef ssize_t (*readfunc_t)(int, void *, size_t);
	typedef ssize_t (*writefunc_t)(int, const void *, size_t);

	string pathname;

	typedef struct
	{
		long fd;
		int oflags;
		int options;
		struct tar_header th_buf;
		int qtype;
		//libtar_hash_t *h;
	}
	TAR;
	TAR tar;

	struct data_t {
		char *buffer;
		size_t len;
		string filename;
		int year, mon, day;
	};

	unsigned int created_at;
	

	Tar() {    
		this->zipStream = NULL;
		this->zipBufferLength = 4*8192;
		this->zipBuffer = new char[this->zipBufferLength];
#ifdef HAVE_LIBLZMA
		this->lzmaStream = NULL;
#endif
	};
	~Tar();

	//tar functions 
	int tar_init(int oflags, int mode, int options);
	int tar_open(string, int, int, int);
	void th_finish();
	int th_write();
	int tar_append_buffer(char *buffer, size_t size);
	int gziplevel;
	int lzmalevel;

	void th_set_type(mode_t mode);
	void th_set_path(char *pathname);
	void th_set_link(char *linkname);
	void th_set_device(dev_t device);
	void th_set_user(uid_t uid);
	void th_set_group(gid_t gid);
	void th_set_mode(mode_t fmode);

	void th_set_mtime(int fmtime){
		int_to_oct_nonull(fmtime, tar.th_buf.mtime, 12);
	};
	void th_set_size(int fsize){
		int_to_oct_nonull(fsize, tar.th_buf.size, 12);
	};
	int tar_block_write(const char *buf);

	void int_to_oct_nonull(int num, char *oct, size_t octlen);
	int th_crc_calc();
	int oct_to_int(char*);
	int initZip();
	ssize_t writeZip(const void *buf, size_t len);
	void flushZip();
#ifdef HAVE_LIBLZMA
	int initLzma();
	void flushLzma();
	ssize_t writeLzma(const void *buf, size_t len);
#endif


private:
	z_stream *zipStream;
	int zipBufferLength;
	char *zipBuffer;

#ifdef HAVE_LIBLZMA
	lzma_stream *lzmaStream;// = LZMA_STREAM_INIT; /* alloc and init lzma_stream struct */
	/* analogous to xz CLI options: -0 to -9 */
	#define LZMA_COMPRESSION_LEVEL 6
				
	/* boolean setting, analogous to xz CLI option: -e */
	#define LZMA_COMPRESSION_EXTREME true

	/* error codes */       
	#define LZMA_RET_OK		  0
	#define LZMA_RET_ERROR_INIT	  1
	#define LZMA_RET_ERROR_INPUT	 2
	#define LZMA_RET_ERROR_OUTPUT	3
	#define LZMA_RET_ERROR_COMPRESSION   4

#endif

};

class TarQueue {
public:		 
	TarQueue() {    
		pthread_mutex_init(&mutexlock, NULL);
		pthread_mutex_init(&flushlock, NULL);
	};
	~TarQueue();
	void lock() {pthread_mutex_lock(&mutexlock);};
	void unlock() {pthread_mutex_unlock(&mutexlock);};
	       
	struct data_t {
		char *buffer;
		size_t len;
		string filename;
		int year, mon, day;
	};
	
	
	void add(string filename, unsigned int time, char *buffer, size_t len);
	void flushQueue();
	int write(int, unsigned int, data_t);
	int queuelen();



private:
	map<unsigned int, vector<data_t> > queue[4]; //queue for all, sip, rtp, graph
	pthread_mutex_t mutexlock;
	pthread_mutex_t flushlock;
	map<string, Tar*> tars; //queue for all, sip, rtp, graph
	map<string, Tar*>::iterator tars_it; //queue for all, sip, rtp, graph
};

void *TarQueueThread(void *dummy);


#endif
