

/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.

  gcc -Wall hello.c `pkg-config fuse --cflags --libs` -o hello
*/

#define FUSE_USE_VERSION 26

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <sys/stat.h>
#include <pthread.h>

#include <curl/curl.h>

static sqlite3 *db;

//SQL - Get the root node id
static const char * rootSQL = "select ID from nodes where name is NULL";

//SQL - gets the id of a subdir or file in a perent folder given by id - for path -> ID
//sprintf params - subdir/file Name, Parents ID 
static const char * NextFolderSQL = "select id from nodes where name=\"%s\" and id in  (select child from  parentage where parent is \"%s\")";

//SQL - gets the name and type of all sub files/folders of given parent ID - for readdir
//sprintf params - Parents ID 
static const char * ListFolderSQL = "select name,type from nodes where status='AVAILABLE' and id in (select child from  parentage where parent is \"%s\")";

//SQL returns file info given by ID - for getattr
//sprintf params - Node ID
static const char * FileInfoSQL = "SELECT nodes.type, strftime(\"%%s\",nodes.created), strftime(\"%%s\",nodes.modified), strftime(\"%%s\",nodes.updated), files.size, files.md5 FROM nodes LEFT JOIN files ON nodes.id=files.id where nodes.id=\"%s\"";


static int SQLcallback_readdir_id(void *ctx, int argc, char **argv, char **colnamev){
        char * idptr = ctx;
        fprintf(stderr,"ID: %s\n", argv[0]);
        strncpy(idptr, argv[0], 50);
        
        return 0;
}

static int SQLcallback_getattr(void *ctx, int argc, char **argv, char **colnamev){
        struct stat *stbuf = ctx;
        long long unsigned int tmp;
        
        fprintf(stderr,"%s,%s\n", argv[0], argv[1]);
        if(strcmp(argv[0], "folder")==0){
                stbuf->st_mode = S_IFDIR | 0777;
                stbuf->st_nlink = 1;
        }
        if(strcmp(argv[0], "file")==0){
                stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;

                //off_t st_size - For regular files, the file size in bytes
                sscanf(argv[4], "%llu",&tmp);
		stbuf->st_size = tmp;

                //argv[5] is md5 - for xattr
        }

        //time_t st_ctime - Time of last status change
        sscanf(argv[1], "%llu",&tmp);
        stbuf->st_ctime = tmp;

        //time_t st_mtime - Time of last data modification
        sscanf(argv[2], "%llu",&tmp);
        stbuf->st_mtime = tmp;
        
        //time_t st_atime - Time of last access
        sscanf(argv[3], "%llu",&tmp);
        stbuf->st_atime = tmp;
                
        return 0;
}


int acd_path_to_id(const char *path, char * ID){
        char *zErrMsg = 0;
        int rc;
        char SQLstr[1024];
        char TmpID[50];
        
        /*Get Root ID*/
        rc = sqlite3_exec(db, rootSQL, SQLcallback_readdir_id, ID, &zErrMsg);
        if( rc!=SQLITE_OK ){
                fprintf(stderr, "SQL error: %s\n", zErrMsg);
                sqlite3_free(zErrMsg);
        }
        
        /* Get Next folder ids */
        char * tok;
        char * tok_ctx;
        tok = strtok_r(path,"/", &tok_ctx);
        while (tok != NULL){
                //Tok the name of next folder
                TmpID[0]=0;
                sprintf(SQLstr, NextFolderSQL, tok,ID);
                rc = sqlite3_exec(db, SQLstr, SQLcallback_readdir_id, TmpID, &zErrMsg);
                if( rc!=SQLITE_OK ){
                        fprintf(stderr, "SQL error: %s\n", zErrMsg);
                        sqlite3_free(zErrMsg);
                }       
                if(strlen(TmpID)==0) return -1; //Next node doesnt exsist
                strcpy(ID,TmpID);
                tok = strtok_r(NULL, "/", &tok_ctx);
        }

        return 0;
}

static int acd_getattr(const char *path, struct stat *stbuf)
{
        char *zErrMsg = 0;
        int rc;
        char SQLstr[1024];
        char ID[50];

        if(acd_path_to_id(path, ID)){
                fprintf(stderr,"No Such Path: %s",path);
                return -ENOENT;
        }
        
        sprintf(SQLstr, FileInfoSQL, ID);
        rc = sqlite3_exec(db, SQLstr, SQLcallback_getattr, (void*)stbuf, &zErrMsg);
        if( rc!=SQLITE_OK ){
                fprintf(stderr, "SQL error: %s\n", zErrMsg);
                sqlite3_free(zErrMsg);
        }
        

	return 0;
}

typedef struct{
        fuse_fill_dir_t filler;
        void *buf;
}fill_ctx;

static int SQLcallback_readdir_fill(void *ctx, int argc, char **argv, char **colnamev){
        fill_ctx * fctx = ctx;
        fuse_fill_dir_t filler = fctx->filler;
        void* buf = fctx->buf;

        struct stat st;
        if(strcmp(argv[1],"folder")==0){        
                st.st_mode=S_IFDIR|0755;
                st.st_nlink = 2;
        }
        if(strcmp(argv[1],"file")==0){
                st.st_mode=S_IFREG|0444;
                st.st_nlink = 1;
        }
        
        filler(buf, argv[0], &st, 0);

        return 0;
}


static int acd_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;
        char *zErrMsg = 0;
        int rc;
        char SQLstr[1024];
        char ID[50];
        
        if(acd_path_to_id(path, ID)){
                fprintf(stderr,"No Such Path: %s",path);
                return -ENOENT;
        }
        
        fprintf(stderr,"DIR: %s,%s\n",path,ID);
        /* List Folder contents */
        
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);
        
        //Put Folder ID in sql quary
        sprintf(SQLstr, ListFolderSQL, ID);

        fill_ctx ctx;
        ctx.filler=filler;
        ctx.buf = buf;

        rc = sqlite3_exec(db, SQLstr, SQLcallback_readdir_fill, (void*)&ctx, &zErrMsg);
        if( rc!=SQLITE_OK ){
                fprintf(stderr, "SQL error: %s\n", zErrMsg);
                sqlite3_free(zErrMsg);
        }
        
	return 0;
}

typedef struct{
        size_t size;//downloaded amount
        size_t total;//total to be downloaded
        void * buf;
        uint8_t first;
        pthread_mutex_t mutex;
        CURL *curl;
        char * url;
}file_ctx;

static int acd_open(const char *path, struct fuse_file_info *fi)
{
        char ID[50];
        if(acd_path_to_id(path, ID)){
                fprintf(stderr,"No Such Path: %s",path);
                return -ENOENT;
        }

        /* Get url to file */
        FILE *fp;
        char cmd[1024];
        sprintf(cmd, "acd_cli m %s", ID);
        fp = popen(cmd, "r");
        if (fp == NULL) {
                fprintf(stderr,"Failed to run command\n" );
        }

        while (fgets(cmd, sizeof(cmd)-1, fp) != NULL) {
                if(strstr(cmd,"tempLink")){
                        fprintf(stderr,"%s\n", cmd);
                        break;
                }
                
        }
        pclose(fp);
        
        char * url = cmd;
        char * p;
        while( *url != ':') url++;
        url+=3;
        p=url;
        while( *p != '"') p++;
        *p=0;
        
        fprintf(stderr,"URL: %s\n", url);

        file_ctx * ctx = malloc(sizeof(file_ctx));
        ctx->url = malloc(strlen(url)+1);
        strcpy(ctx->url, url);

        pthread_mutex_init(&ctx->mutex, NULL);
        ctx->first=1;
        fi->fh = (void*)ctx;
        return 0;
}


size_t curl_callback(char *ptr, size_t size, size_t nmemb, void *userdata){
        file_ctx * ctx = (file_ctx*)userdata;

        fprintf(stderr,"CURL: %u, %u of %u \n", size*nmemb, ctx->size, ctx->total );
        memcpy(ctx->buf+ctx->size, ptr, (size*nmemb));
        ctx->size+=(size*nmemb);

        return (size*nmemb);
}

static int acd_read(const char *path, char *buf, size_t size, off_t offset,
		      struct fuse_file_info *fi)
{
	size_t len;
	(void) fi;
        char range[512];

        sprintf(range,"%u-%u", offset, offset+size-1); 
        fprintf(stderr, "READ: %s, size:%u, off:%u, range:%s\n", path,size,offset, range);

        file_ctx * ctx = fi->fh;
        pthread_mutex_lock(&ctx->mutex);
        
        ctx->size=0;
        ctx->buf = buf;
        ctx->total=size;
        memset(buf, 0, size);
        
        if(ctx->first){
                ctx->curl = curl_easy_init();   
                if(!ctx->curl){
                        fprintf(stderr,"Curl init failed",path);
                        return -ENOENT;
                }
                //curl_easy_setopt(ctx->curl, CURLOPT_VERBOSE, 1L);
                curl_easy_setopt(ctx->curl, CURLOPT_URL, ctx->url);
                curl_easy_setopt(ctx->curl, CURLOPT_WRITEFUNCTION, curl_callback);
                curl_easy_setopt(ctx->curl, CURLOPT_WRITEDATA, ctx);
                ctx->first=0;
        }

        if(ctx->curl){
                curl_easy_setopt(ctx->curl, CURLOPT_RANGE, range);
                curl_easy_perform(ctx->curl);
        }
        
        
        size=ctx->size;
        pthread_mutex_unlock(&ctx->mutex);        
	return size;
}

static struct fuse_operations acd_oper = {
	.getattr	= acd_getattr,
	.readdir	= acd_readdir,
	.open		= acd_open,
	.read		= acd_read,
};


int main(int argc, char *argv[])
{
        const char * dbs = "/.cache/acd_cli/nodes.db";
        char * home=getenv("HOME");
        char * dbString = malloc(strlen(home)+strlen(dbs));
        strcpy(dbString,home);
        strcat(dbString,dbs);

        int rc;

        printf("using sqlite db: %s\n", dbString);

        rc = sqlite3_open(dbString, &db);
        if( rc ){
                fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
                sqlite3_close(db);
                return(1);
        }

        curl_global_init(CURL_GLOBAL_DEFAULT);
        
	rc = fuse_main(argc, argv, &acd_oper, NULL);
        sqlite3_close(db);
        curl_global_cleanup();
         
        return rc;
}
