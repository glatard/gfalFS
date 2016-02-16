// Copyright @ Members of the EMI Collaboration, 2010.
// See www.eu-emi.eu for details on the copyright holders.
// 
// Licensed under the Apache License, Version 2.0 (the "License"); 
// you may not use this file except in compliance with the License. 
// You may obtain a copy of the License at 
// 
//     http://www.apache.org/licenses/LICENSE-2.0 
// 
// Unless required by applicable law or agreed to in writing, software 
// distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and 
// limitations under the License.

/*
 * gfal_opers.c
 * map of the gfal operators
 * author Devresse Adrien
 * */

#define _GNU_SOURCE

#include <string.h>
#include <errno.h>

#include <gfal_api.h>


#include "gfal_opers.h"
#include "gfal_ext.h"

char mount_point[2048];
size_t s_mount_point=0;

char local_mount_point[2048];
size_t s_local_mount_point=0;

gboolean guid_mode=FALSE;

void gfalfs_set_local_mount_point(const char* local_mp){
    g_strlcpy(local_mount_point, local_mp, 2048);
	s_local_mount_point= strlen(local_mount_point);
}

void gfalfs_set_remote_mount_point(const char* remote_mp){
    g_strlcpy(mount_point, remote_mp, 2048);
	s_mount_point= strlen(remote_mp);
}

void gfalfs_construct_path(const char* path, char* buff, size_t s_buff){
	if(guid_mode){
		g_strlcpy(buff, path+1, s_buff);
	}else{
		 char* p = (char*) mempcpy(buff, mount_point, MIN(s_buff-1, s_mount_point));
		 p = mempcpy(p, path, MIN(s_buff-1-(p-buff), strlen(path)));
		 *p ='\0' ;	
    }
}

void gfalfs_construct_path_from_abs_local(const char* path, char* buff, size_t s_buff){
    char tmp_buff[2048];
	if(strstr(path, local_mount_point)== (char*)path){
        g_strlcpy(tmp_buff, path + s_local_mount_point, 2048);
    }
    gfalfs_construct_path(tmp_buff, buff, s_buff);
}

// convert a remote path of result in a local path
static void convert_external_readlink_to_local_readlink(char* external_buff, size_t s_ext, char* local_buff, ssize_t s_local){ 
	if( s_local > 0){	
        char internal_buff[2048];
			if(s_ext > s_mount_point){
				size_t s_input = MIN(s_ext, 2048-1);
				*((char*)mempcpy(internal_buff, external_buff+s_mount_point , s_input-s_mount_point )) = '\0';

				g_strlcpy(local_buff,local_mount_point, s_local );
				if(s_local > s_local_mount_point)
					g_strlcat(local_buff, internal_buff , s_local);
			}else{
				*((char*)mempcpy(local_buff, external_buff, MIN(s_ext, s_local-1) )) = '\0';
        }

    }
}


static int gfalfs_getattr(const char *path, struct stat *stbuf)
{
    gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE, "gfalfs_getattr path %s ", (char*) path);
    char buff[2048];
    char err_buff[1024];
	int ret=-1;
    gfalfs_construct_path(path, buff, 2048);
	if(fuse_interrupted())
        return -(ECANCELED);
    int a= gfal_lstat(buff, stbuf);
    if( (ret = -(gfal_posix_code_error()))){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_getattr error %d for path %s: %s ", (int) gfal_posix_code_error(), (char*)buff, (char*)gfal_posix_strerror_r(err_buff, 1024));
        gfal_posix_clear_error();
        return ret;
    }else{
        gfalfs_tune_stat(stbuf);
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return a;
}

static int gfalfs_readlink(const char *path, char* link_buff, size_t buffsiz)
{
    gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE, "gfalfs_readlink path %s ", (char*) path);
    char buff[2048];
    char err_buff[1024];
    char tmp_link_buff[2048];
	int ret=-1;
    gfalfs_construct_path(path, buff, 2048);
    ssize_t a= gfal_readlink(buff, tmp_link_buff, 2048-1);
	convert_external_readlink_to_local_readlink(tmp_link_buff, a, link_buff, buffsiz );
    if( (ret = -(gfal_posix_code_error()))){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_readlink error %d for path %s: %s ", (int) gfal_posix_code_error(), (char*)buff, (char*)gfal_posix_strerror_r(err_buff, 1024));
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return 0;
}

static int gfalfs_opendir(const char * path, struct fuse_file_info * f){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_opendir path %s ", (char*) path);
    char buff[2048];
    char err_buff[1024];
    int ret;
    gfalfs_construct_path(path, buff, 2048);
    DIR* i = gfal_opendir(buff);
    if( (ret= -(gfal_posix_code_error()))){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_opendir err %d for path %s: %s", (int)gfal_posix_code_error(), (char*) buff, (char*) gfal_posix_strerror_r(err_buff, 1024));
        gfal_posix_clear_error();
        return ret;
    }
	f->fh= (uint64_t) gfalFS_dir_handle_new((void*)i, buff);
	if(fuse_interrupted())
        return -(ECANCELED);
	return (i!= NULL)?0:-(gfal_posix_code_error());
}

static int gfalfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                         off_t offset, struct fuse_file_info *fi)
{
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_readdir path %s ",(char*) path);

	return gfalFS_dir_handle_readdir((gfalFS_dir_handle)fi->fh, offset, buf, filler);
}

static int gfalfs_open(const char *path, struct fuse_file_info *fi)
{
    char buff[2048];
    char err_buff[1024];
	int ret =-1;
    gfalfs_construct_path(path, buff, 2048);
	int i = gfal_open(buff,fi->flags,755);
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_open path %s %d", (char*) path, (int) i);
    if( (ret = -(gfal_posix_code_error())) || i==0){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_open err %d for path %s: %s ", (int) gfal_posix_code_error(), (char*)buff, (char*)gfal_posix_strerror_r(err_buff, 1024));
        gfal_posix_clear_error();
        return ret;
    }

	fi->fh= i;
	if(fuse_interrupted())
        return -(ECANCELED);
    return 0;
}




static int gfalfs_create(const char * path, mode_t mode, struct fuse_file_info * fi) {
    gfal_posix_clear_error();
    char buff[2048];
    char surl[1024];
    char lfn[1024];
    GError* error = NULL;
    char date[100];
   
    gfalfs_construct_path(path, buff, 2048);
  
    //create a context to read from configure file in /etc/gfal2.d/lfc_plugin.conf
    gfal2_context_t context_config;
    if ((context_config = gfal2_context_new(&error)) == NULL) {
        gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE, "Could not create the gfal2 context %d: %s", error->code, error->message);
        return -1;
    }
    
    //read params from config file
    gchar* vo_from_config = gfal2_get_opt_string(context_config, "LFC PLUGIN", "VO", &error);
    gchar* se_from_config = gfal2_get_opt_string(context_config, "LFC PLUGIN", "SE", &error);
    gchar* lfc_from_config = gfal2_get_opt_string(context_config, "LFC PLUGIN", "LFC_HOST", &error);
    gchar* empty_file__from_config = gfal2_get_opt_string(context_config, "LFC PLUGIN", "EMPTY_FILE", &error);
    //free the context
    gfal2_context_free(context_config);
    //create the surl
    //generate date to have a unique identifier of the file in the surl
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(date, sizeof (date) - 1, "%d%m%Y%H%M%S", t);
    strcpy(surl, "srm://");
    strcat(surl, se_from_config);
    strcat(surl, "/");
    strcat(surl, vo_from_config);
    strcat(surl, "/");
    strcat(surl, date);
    // Create a gfal2 context to copy files
    gfal2_context_t context;
    if ((context = gfal2_context_new(&error)) == NULL) {
        gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE, "Could not create the gfal2 context %d: %s", error->code, error->message);
        return -1;
    }
    // Set some parameters for the copy
    gfalt_params_t params = gfalt_params_handle_new(&error);

    gfalt_set_replace_existing_file(params, TRUE, &error); // Just in case, do not overwrite

    gfalt_set_create_parent_dir(params, TRUE, &error); // Create the parent directory if needed
    //create the lfn
    memmove(buff, buff + 6, strlen(buff));
    int size;
    size = strlen(lfc_from_config);
    memmove(buff, buff + size, strlen(buff));
    strcpy(lfn, "lfn:");
    strcat(lfn, buff);
    gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE, "affiche params %s %s", (char*) buff
            , (char*) path);
    //copy the empty file in the created surl
    if (gfalt_copy_file(context, params, empty_file__from_config, surl, &error) != 0) {
        gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE, "error while copy the file%d %s", error->code, error->message);
        gfal2_context_free(context);
        return -1;
    }
    //register the source in the LFC
   if (gfalt_copy_file(context, params, surl, lfn, &error) != 0) {
        gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE, "error while copy the file%d %s", error->code, error->message);
        gfal2_context_free(context);
        return -1;
    }

    gfal2_context_free(context);
    return 0;

}

int gfalfs_chown (const char * path, uid_t uid, gid_t guid){
    // do nothing, change prop not authorized
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_chown path : %s ", (char*) path);
    gfal_posix_clear_error();
    return 0;
}

int gfalfs_utimens (const char * path, const struct timespec tv[2]){
    // do nothing, not implemented yet
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_utimens path : %s ", (char*) path);
    gfal_posix_clear_error();
    return 0;
}

int gfalfs_truncate (const char * path, off_t size){
    // do nothing, not implemented yet
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_truncate path : %s ", (char*) path);
    gfal_posix_clear_error();
    return 0;
}


static int gfalfs_read(const char *path, char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
    char err_buff[1024];
    int ret = 0;
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_read path : %s fd : %d", (char*) path, (int) fi->fh);

	ret = gfal_pread(GPOINTER_TO_INT(fi->fh),(void*)buf, size, offset);
	if(ret <0 ){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_pread err %d for path %s: %s ", (int) gfal_posix_code_error(), (char*) path, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(gfal_posix_code_error());
        gfal_posix_clear_error();
    }

	if(fuse_interrupted())
        return -(ECANCELED);
    return ret;
}


static int gfalfs_write(const char *path, const char *buf, size_t size, off_t offset,
                      struct fuse_file_info *fi)
{
	char err_buff[1024];
	int ret = 0;
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_write path : %s fd : %d", (char*) path, (int) fi->fh);
	
	ret = gfal_pwrite(GPOINTER_TO_INT(fi->fh),(void*)buf, size, offset);
	if(ret <0 ){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_pwrite err %d for path %s: %s ", (int) gfal_posix_code_error(), (char*) path, (char*) gfal_posix_strerror_r(err_buff, 1024));
		ret = -(gfal_posix_code_error());
		gfal_posix_clear_error();
	}
	
	if(fuse_interrupted())
		return -(ECANCELED);
    return ret;
}



static int gfalfs_access(const char * path, int flag){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_access path : %s ", (char*) path);
    char buff[2048];
    char err_buff[1024];
    int ret;

    gfalfs_construct_path(path, buff, 2048);
    int i = gfal_access(buff, flag);
	if( i < 0){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_access err %d for path %s: %s ", (int)gfal_posix_code_error(), (char*) buff, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(gfal_posix_code_error());
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return i;
}


static int gfalfs_unlink(const char * path){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_access path : %s ", (char*) path);
    char buff[2048];
    char err_buff[1024];
    int ret;

    gfalfs_construct_path(path, buff, 2048);
    int i = gfal_unlink(buff);
	if( i < 0){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_access err %d for path %s: %s ", (int)gfal_posix_code_error(), (char*) buff, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(gfal_posix_code_error());
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return i;
}


static int gfalfs_mkdir(const char * path, mode_t mode){
    gfal_posix_clear_error();
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_mkdir path : %s ", (char*) path);	
    char buff_path[2048];
    char err_buff[1024];

    gfalfs_construct_path(path, buff_path, 2048);
    int ret;
    int i = gfal_mkdir(buff_path, mode);
	if( i < 0){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_mkdir err %d for path %s: %s ", (int) gfal_posix_code_error(), (char*) buff_path, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(gfal_posix_code_error());
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return i;
}

static int gfalfs_getxattr (const char * path, const char *name , char *buff, size_t s_buff){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_getxattr path : %s, name : %s, size %d", (char*) path, (char*) name, (int) s_buff);	
    char buff_path[2048];
    char err_buff[1024];

    gfalfs_construct_path(path, buff_path, 2048);
    int ret;
    int i = gfal_getxattr(buff_path, name, buff, s_buff);
	if( i < 0 ){
        int errcode = gfal_posix_code_error();
        if(errcode == EPROTONOSUPPORT) // silent the non supported errors
            errcode = ENOATTR;

		if(errcode != ENOATTR) // suppress verbose error for ENOATTR for perfs reasons
			gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_getxattr err %d for path %s: %s ", (int) errcode, (char*) buff_path, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(errcode);
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return i;
}


static int gfalfs_setxattr (const char * path, const char *name , const char *buff, size_t s_buff, int flag){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_setxattr path : %s, name : %s", (char*) path, (char*) name);	
    char buff_path[2048];
    char err_buff[1024];
    gfalfs_construct_path(path, buff_path, 2048);


    int ret;
    int i = gfal_setxattr(buff_path, name, buff, s_buff, flag);
	if( i < 0 ){
        const int errcode = gfal_posix_code_error();
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_setxattr err %d for path %s: %s ", (int) errcode, (char*) buff_path, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(errcode);
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return i;
}


static int gfalfs_listxattr (const char * path, char *list, size_t s_list){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_listxattr path : %s, size %d", (char*) path, (int) s_list);	
    char buff_path[2048];
    char err_buff[1024];
    gfalfs_construct_path(path, buff_path, 2048);


    int ret;
    int i = gfal_listxattr(buff_path, list, s_list);
	if( i < 0){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_listxattr err %d for path %s: %s ", (int) gfal_posix_code_error(), (char*) buff_path, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(gfal_posix_code_error());
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return i;
}

static int gfalfs_rename(const char*oldpath, const char* newpath){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_rename oldpath : %s, newpath : %s ", (char*) oldpath, (char*) newpath);	
    char buff_oldpath[2048];
    char buff_newpath[2048];
    char err_buff[1024];

    int ret;
    gfalfs_construct_path(oldpath, buff_oldpath, 2048);
    gfalfs_construct_path(newpath, buff_newpath, 2048);
    int i = gfal_rename(buff_oldpath, buff_newpath);
	if( i < 0){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_rename err %d for oldpath %s: %s ", (int) gfal_posix_code_error(), (char*) buff_oldpath, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(gfal_posix_code_error());
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return i;

}

static int gfalfs_symlink(const char*oldpath, const char* newpath){
    char buff_oldpath[2048];
    char buff_newpath[2048];
    char err_buff[1024];
    int ret;

    gfalfs_construct_path_from_abs_local(oldpath, buff_oldpath, 2048);
    gfalfs_construct_path(newpath, buff_newpath, 2048);
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_symlink oldpath : %s, newpath : %s ", (char*) buff_oldpath, (char*) buff_newpath);	
    int i = gfal_symlink(buff_oldpath, buff_newpath);
	if( i < 0){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_symlink err %d for oldpath %s: %s ", (int) gfal_posix_code_error(), (char*) buff_oldpath, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(gfal_posix_code_error());
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return i;
}

static int gfalfs_release(const char* path, struct fuse_file_info *fi){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_close fd : %d", (int) fi->fh);
    char err_buff[1024];
	const int fd = GPOINTER_TO_INT(fi->fh);

    int i = gfal_close(fd);
	if(i <0 ){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_close err %d for fd %d: %s ", (int) gfal_posix_code_error(), (int) fi->fh, (char*) gfal_posix_strerror_r(err_buff, 1024));
		i = -(gfal_posix_code_error());
		gfal_posix_clear_error();
	}
    return i;	
}

static int gfalfs_releasedir(const char* path, struct fuse_file_info *fi){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_closedir fd : %d", (int) fi->fh);
	char err_buff[1024];
	
	DIR* d = gfalFS_dir_handle_get_fd((void*)fi->fh);
    int i = gfal_closedir(d);
    int ret;
	if(i <0 ){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_closedir err %d for fd %d: %s ", (int) gfal_posix_code_error(), (int) fi->fh, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(gfal_posix_code_error());
        gfal_posix_clear_error();
        return ret;
    }
    return i;
}

static int gfalfs_chmod(const char* path, mode_t mode){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_chmod path : %s ", (char*) path);	
    char buff_path[2048];
    char err_buff[1024];
    int ret;

    gfalfs_construct_path(path, buff_path, 2048);
    int i = gfal_chmod(buff_path, mode);
	if( i < 0){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_chmod err %d for path %s: %s ", (int) gfal_posix_code_error(), (char*) buff_path, (char*) gfal_posix_strerror_r(err_buff, 1024));
        ret = -(gfal_posix_code_error());
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return i;

}

static int gfalfs_rmdir(const char* path){
	gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE,"gfalfs_rmdir path : %s ", (char*) path);	
    char buff_path[2048];
    char err_buff[1024];
    int ret;

    gfalfs_construct_path(path, buff_path, 2048);
    int i = gfal_rmdir(buff_path);
	if( i < 0){
		gfalfs_log(NULL, G_LOG_LEVEL_WARNING , "gfalfs_rmdir err %d for path %s: %s ", (int) gfal_posix_code_error(),(char*) buff_path, (char*)gfal_posix_strerror_r(err_buff, 1024));
        ret = -(gfal_posix_code_error());
        gfal_posix_clear_error();
        return ret;
    }
	if(fuse_interrupted())
        return -(ECANCELED);
    return i;
}


int gfalfs_fake_fgetattr (const char * url, struct stat * st, struct fuse_file_info * f){
    if(f->flags & O_CREAT && (strncmp(mount_point, "srm",3) ==0 ||strncmp(mount_point, "gsiftp",5) ==0 )){ // tmp hack for srm & gsiftp consistency
		gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE ," fgetattr create mode, bypass and set to default, speed hack");
		memset(st,0,sizeof(struct stat));
        st->st_mode = S_IFREG | 0666;
        return 0;
	}else{
		gfalfs_log(NULL, G_LOG_LEVEL_MESSAGE ," fgetattr other mode");
        return gfalfs_getattr(url, st);
    }
}

struct fuse_operations gfal_oper = {
    .getattr	= gfalfs_getattr,
    .readdir	= gfalfs_readdir,
    .opendir	= gfalfs_opendir,
    .open	= gfalfs_open,
    .read	= gfalfs_read,
    .release = gfalfs_release,
    .releasedir = gfalfs_releasedir,
    .access = gfalfs_access,
    .create = gfalfs_create,
    .mkdir = gfalfs_mkdir,
    .rmdir = gfalfs_rmdir,
    .chmod = gfalfs_chmod,
    .rename = gfalfs_rename,
    .write = gfalfs_write,
    .chown = gfalfs_chown,
    .utimens = gfalfs_utimens,
    .truncate = gfalfs_truncate,
    .symlink= gfalfs_symlink,
    .setxattr = gfalfs_setxattr,
    .getxattr= gfalfs_getxattr,
    .fgetattr = gfalfs_fake_fgetattr,
    .listxattr= gfalfs_listxattr,
    .readlink = gfalfs_readlink,
    .unlink = gfalfs_unlink
};



