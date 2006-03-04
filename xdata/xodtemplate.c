 /*****************************************************************************
 *
 * XODTEMPLATE.C - Template-based object configuration data input routines
 *
 * Copyright (c) 2001-2006 Ethan Galstad (nagios@nagios.org)
 * Last Modified: 03-03-2006
 *
 * Description:
 *
 * Routines for parsing and resolving template-based object definitions.
 * Basic steps involved in this in the daemon are as follows:
 *
 *    1) Read
 *    2) Resolve
 *    3) Duplicate
 *    4) Recombobulate
 *    5) Cache
 *    7) Register
 *    8) Cleanup
 *
 * The steps involved for the CGIs differ a bit, since they read the cached
 * definitions which are already resolved, recombobulated and duplicated.  In
 * otherwords, they've already been "flattened"...
 *
 *    1) Read
 *    2) Register
 *    3) Cleanup
 *
 *
 * License:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *****************************************************************************/


/*********** COMMON HEADER FILES ***********/

#include "../include/config.h"
#include "../include/common.h"
#include "../include/objects.h"
#include "../include/locations.h"

/**** CORE OR CGI SPECIFIC HEADER FILES ****/

#ifdef NSCORE
#include "../include/nagios.h"
#endif

#ifdef NSCGI
#include "../include/cgiutils.h"
#endif

/**** DATA INPUT-SPECIFIC HEADER FILES ****/

#include "xodtemplate.h"


#ifdef NSCORE
extern int use_regexp_matches;
extern int use_true_regexp_matching;
extern char *macro_x[MACRO_X_COUNT];
extern int verify_config;
extern int test_scheduling;
extern int use_precached_objects;
#endif

xodtemplate_timeperiod *xodtemplate_timeperiod_list=NULL;
xodtemplate_command *xodtemplate_command_list=NULL;
xodtemplate_contactgroup *xodtemplate_contactgroup_list=NULL;
xodtemplate_hostgroup *xodtemplate_hostgroup_list=NULL;
xodtemplate_servicegroup *xodtemplate_servicegroup_list=NULL;
xodtemplate_servicedependency *xodtemplate_servicedependency_list=NULL;
xodtemplate_serviceescalation *xodtemplate_serviceescalation_list=NULL;
xodtemplate_contact *xodtemplate_contact_list=NULL;
xodtemplate_host *xodtemplate_host_list=NULL;
xodtemplate_service *xodtemplate_service_list=NULL;
xodtemplate_hostdependency *xodtemplate_hostdependency_list=NULL;
xodtemplate_hostescalation *xodtemplate_hostescalation_list=NULL;
xodtemplate_hostextinfo *xodtemplate_hostextinfo_list=NULL;
xodtemplate_serviceextinfo *xodtemplate_serviceextinfo_list=NULL;

void *xodtemplate_current_object=NULL;
int xodtemplate_current_object_type=XODTEMPLATE_NONE;

int xodtemplate_current_config_file=0;
char **xodtemplate_config_files=NULL;

char *xodtemplate_cache_file=NULL;
char *xodtemplate_precache_file=NULL;


/******************************************************************/
/************* TOP-LEVEL CONFIG DATA INPUT FUNCTION ***************/
/******************************************************************/

/* process all config files - both core and CGIs pass in name of main config file */
int xodtemplate_read_config_data(char *main_config_file, int options, int cache, int precache){
#ifdef NSCORE
	char *config_file=NULL;;
	char *input=NULL;
	char *var=NULL;
	char *val=NULL;
	struct timeval tv[12];
	double runtime[12];
#endif
	mmapfile *thefile=NULL;
	int result=OK;

#ifdef DEBUG0
	printf("xodtemplate_read_config_data() start\n");
#endif

	/* get variables from main config file */
	xodtemplate_grab_config_info(main_config_file);

	/* initialize variables */
	xodtemplate_timeperiod_list=NULL;
	xodtemplate_command_list=NULL;
	xodtemplate_contactgroup_list=NULL;
	xodtemplate_hostgroup_list=NULL;
	xodtemplate_servicegroup_list=NULL;
	xodtemplate_servicedependency_list=NULL;
	xodtemplate_serviceescalation_list=NULL;
	xodtemplate_contact_list=NULL;
	xodtemplate_host_list=NULL;
	xodtemplate_service_list=NULL;
	xodtemplate_hostdependency_list=NULL;
	xodtemplate_hostescalation_list=NULL;
	xodtemplate_hostextinfo_list=NULL;
	xodtemplate_serviceextinfo_list=NULL;

	xodtemplate_current_object=NULL;
	xodtemplate_current_object_type=XODTEMPLATE_NONE;

	/* allocate memory for 256 config files (increased dynamically) */
	xodtemplate_current_config_file=0;
	xodtemplate_config_files=(char **)malloc(256*sizeof(char **));
	if(xodtemplate_config_files==NULL){
#ifdef DEBUG1
		printf("Error: Cannot allocate memory for config file names!\n");
#endif
		return ERROR;
	        }

#ifdef NSCORE
	if(test_scheduling==TRUE)
		gettimeofday(&tv[0],NULL);

	/* only process the precached object file as long as we're not regenerating it and we're not verifying the config */
	if(use_precached_objects==TRUE)
		result=xodtemplate_process_config_file(xodtemplate_precache_file,options);

	/* process object config files normally... */
	else{

		/* open the main config file for reading (we need to find all the config files to read) */
		if((thefile=mmap_fopen(main_config_file))==NULL){
#ifdef DEBUG1
			printf("Error: Cannot open main configuration file '%s' for reading!\n",main_config_file);
#endif
			my_free((void **)&xodtemplate_config_files);
			return ERROR;
	                }

		/* daemon reads all config files/dirs specified in the main config file */
		/* read in all lines from the main config file */
		while(1){

			/* free memory */
			my_free((void **)&input);

			/* get the next line */
			if((input=mmap_fgets(thefile))==NULL)
				break;

			/* strip input */
			strip(input);

			/* skip blank lines and comments */
			if(input[0]=='#' || input[0]==';' || input[0]=='\x0')
				continue;
			
			if((var=strtok(input,"="))==NULL)
				continue;
			
			if((val=strtok(NULL,"\n"))==NULL)
				continue;
			
			/* process a single config file */
			if(!strcmp(var,"xodtemplate_config_file") || !strcmp(var,"cfg_file")){

				config_file=(char *)strdup(val);

				/* process the config file... */
				result=xodtemplate_process_config_file(config_file,options);

				my_free((void **)&config_file);
				
				/* if there was an error processing the config file, break out of loop */
				if(result==ERROR)
					break;
		                }

			/* process all files in a config directory */
			else if(!strcmp(var,"xodtemplate_config_dir") || !strcmp(var,"cfg_dir")){
				
				config_file=(char *)strdup(val);

				/* strip trailing / if necessary */
				if(config_file!=NULL && config_file[strlen(config_file)-1]=='/')
					config_file[strlen(config_file)-1]='\x0';

				/* process the config directory... */
				result=xodtemplate_process_config_dir(config_file,options);

				my_free((void **)&config_file);

				/* if there was an error processing the config file, break out of loop */
				if(result==ERROR)
					break;
		                }
	                }

		/* free memory and close the file */
		my_free((void **)&input);
		mmap_fclose(thefile);
	        }

	if(test_scheduling==TRUE)
		gettimeofday(&tv[1],NULL);
#endif

#ifdef NSCGI
	/* CGIs process only one file - the cached objects file */
	result=xodtemplate_process_config_file(xodtemplate_cache_file,options);
#endif

#ifdef NSCORE

	/* only perform intensive operations if we're not using the precached object file */
	if(use_precached_objects==FALSE){

		/* resolve objects definitions */
		if(result==OK)
			result=xodtemplate_resolve_objects();
		if(test_scheduling==TRUE)
			gettimeofday(&tv[2],NULL);

		/* do the meat and potatoes stuff... */
		if(result==OK)
			result=xodtemplate_recombobulate_contactgroups();
		if(test_scheduling==TRUE)
			gettimeofday(&tv[3],NULL);
		
		if(result==OK)
			result=xodtemplate_recombobulate_hostgroups();
		if(test_scheduling==TRUE)
			gettimeofday(&tv[4],NULL);
		
		if(result==OK)
			result=xodtemplate_duplicate_services();
		if(test_scheduling==TRUE)
			gettimeofday(&tv[5],NULL);
		
		if(result==OK)
			result=xodtemplate_recombobulate_servicegroups();
		if(test_scheduling==TRUE)
			gettimeofday(&tv[6],NULL);

		if(result==OK)
			result=xodtemplate_duplicate_objects();
		if(test_scheduling==TRUE)
			gettimeofday(&tv[7],NULL);


		/* sort objects */
		if(result==OK)
			result=xodtemplate_sort_objects();
		if(test_scheduling==TRUE)
			gettimeofday(&tv[8],NULL);
	        }

	if(result==OK){

		/* merge host/service extinfo definitions with host/service definitions */
		/* this will be removed in Nagios 4.x */
		xodtemplate_merge_extinfo_ojects();

		/* cache object definitions for the CGIs and external apps */
		if(cache==TRUE)
			xodtemplate_cache_objects(xodtemplate_cache_file);

		/* precache object definitions for future runs */
		if(precache==TRUE)
			xodtemplate_cache_objects(xodtemplate_precache_file);
	        }

	if(test_scheduling==TRUE)
		gettimeofday(&tv[9],NULL);

#endif

	/* register objects */
	if(result==OK)
		result=xodtemplate_register_objects();
#ifdef NSCORE
	if(test_scheduling==TRUE)
		gettimeofday(&tv[10],NULL);
#endif

	/* cleanup */
	xodtemplate_free_memory();
#ifdef NSCORE
	if(test_scheduling==TRUE)
		gettimeofday(&tv[11],NULL);
#endif

	/* free memory */
	my_free((void **)&xodtemplate_cache_file);
	my_free((void **)&xodtemplate_precache_file);

#ifdef NSCORE
	if(test_scheduling==TRUE){

		runtime[0]=(double)((double)(tv[1].tv_sec-tv[0].tv_sec)+(double)((tv[1].tv_usec-tv[0].tv_usec)/1000.0)/1000.0);
		if(use_precached_objects==FALSE){
			runtime[1]=(double)((double)(tv[2].tv_sec-tv[1].tv_sec)+(double)((tv[2].tv_usec-tv[1].tv_usec)/1000.0)/1000.0);
			runtime[2]=(double)((double)(tv[3].tv_sec-tv[2].tv_sec)+(double)((tv[3].tv_usec-tv[2].tv_usec)/1000.0)/1000.0);
			runtime[3]=(double)((double)(tv[4].tv_sec-tv[3].tv_sec)+(double)((tv[4].tv_usec-tv[3].tv_usec)/1000.0)/1000.0);
			runtime[4]=(double)((double)(tv[5].tv_sec-tv[4].tv_sec)+(double)((tv[5].tv_usec-tv[4].tv_usec)/1000.0)/1000.0);
			runtime[5]=(double)((double)(tv[6].tv_sec-tv[5].tv_sec)+(double)((tv[6].tv_usec-tv[5].tv_usec)/1000.0)/1000.0);
			runtime[6]=(double)((double)(tv[7].tv_sec-tv[6].tv_sec)+(double)((tv[7].tv_usec-tv[6].tv_usec)/1000.0)/1000.0);
			runtime[7]=(double)((double)(tv[8].tv_sec-tv[7].tv_sec)+(double)((tv[8].tv_usec-tv[7].tv_usec)/1000.0)/1000.0);
			runtime[8]=(double)((double)(tv[9].tv_sec-tv[8].tv_sec)+(double)((tv[9].tv_usec-tv[8].tv_usec)/1000.0)/1000.0);
			runtime[9]=(double)((double)(tv[10].tv_sec-tv[9].tv_sec)+(double)((tv[10].tv_usec-tv[9].tv_usec)/1000.0)/1000.0);
		        }
		else{
			runtime[1]=0.0;
			runtime[2]=0.0;
			runtime[3]=0.0;
			runtime[4]=0.0;
			runtime[5]=0.0;
			runtime[6]=0.0;
			runtime[7]=0.0;
			runtime[8]=0.0;
			runtime[9]=(double)((double)(tv[10].tv_sec-tv[1].tv_sec)+(double)((tv[10].tv_usec-tv[1].tv_usec)/1000.0)/1000.0);
		        }
		runtime[10]=(double)((double)(tv[11].tv_sec-tv[10].tv_sec)+(double)((tv[11].tv_usec-tv[10].tv_usec)/1000.0)/1000.0);
		runtime[11]=(double)((double)(tv[11].tv_sec-tv[0].tv_sec)+(double)((tv[11].tv_usec-tv[0].tv_usec)/1000.0)/1000.0);

		printf("Timing information on object configuration processing is listed\n");
		printf("below.  You can use this information to see if precaching your\n");
		printf("object configuration would be useful.\n\n");

		printf("Object Config Source: %s\n\n",(use_precached_objects==TRUE)?"Pre-cached file":"Config files (not pre-cached)");

		printf("OBJECT CONFIG PROCESSING TIMES\n");
		printf("----------------------------------\n");
		printf("Read:                 %.6lf sec\n",runtime[0]);
		printf("Resolve:              %.6lf sec *\n",runtime[1]);
		printf("Recomb Contactgroups: %.6lf sec *\n",runtime[2]);
		printf("Recomb Hostgroups:    %.6lf sec *\n",runtime[3]);
		printf("Dup Services:         %.6lf sec *\n",runtime[4]);
		printf("Recomb Servicegroups: %.6lf sec *\n",runtime[5]);
		printf("Duplicate:            %.6lf sec *\n",runtime[6]);
		printf("Sort:                 %.6lf sec *\n",runtime[7]);
/*		printf("Cache:                %.6lf sec\n",runtime[8]);*/
		printf("Register:             %.6lf sec\n",runtime[9]);
		printf("Free:                 %.6lf sec\n",runtime[10]);
		printf("                      ============\n");
		printf("TOTAL:                %.6lf sec\n",runtime[11]);
		printf("Est Precache Savings: %.6lf sec *\n",runtime[11]-runtime[0]-runtime[9]-runtime[10]);
		printf("\n\n");
	        }
#endif

#ifdef DEBUG0
	printf("xodtemplate_read_config_data() end\n");
#endif

	return result;
	}



/* grab config variable from main config file */
int xodtemplate_grab_config_info(char *main_config_file){
	char *input=NULL;
	char *var=NULL;
	char *val=NULL;
	mmapfile *thefile=NULL;
	
#ifdef DEBUG0
	printf("xodtemplate_grab_config_info() start\n");
#endif

	/* open the main config file for reading */
	if((thefile=mmap_fopen(main_config_file))==NULL)
		return ERROR;

	/* read in all lines from the main config file */
	while(1){

		/* free memory */
		my_free((void **)&input);

		/* read the next line */
		if((input=mmap_fgets(thefile))==NULL)
			break;

		/* strip input */
		strip(input);

		/* skip blank lines and comments */
		if(input[0]=='#' || input[0]==';' || input[0]=='\x0')
			continue;

		if((var=strtok(input,"="))==NULL)
			continue;

		if((val=strtok(NULL,"\n"))==NULL)
			continue;

		/* cached object file definition (overrides default location) */
		if(!strcmp(var,"object_cache_file"))
			xodtemplate_cache_file=(char *)strdup(val);

		/* pre-cached object file definition */
		if(!strcmp(var,"precached_object_file"))
			xodtemplate_precache_file=(char *)strdup(val);
	        }

	/* close the file */
	mmap_fclose(thefile);

	/* default locations */
	if(xodtemplate_cache_file==NULL)
		xodtemplate_cache_file=(char *)strdup(DEFAULT_OBJECT_CACHE_FILE);
	if(xodtemplate_precache_file==NULL)
		xodtemplate_precache_file=(char *)strdup(DEFAULT_PRECACHED_OBJECT_FILE);

	/* make sure we have what we need */
	if(xodtemplate_cache_file==NULL || xodtemplate_precache_file==NULL)
		return ERROR;

#ifdef NSCORE
	/* save the object cache file macro */
	my_free((void **)&macro_x[MACRO_OBJECTCACHEFILE]);
	if((macro_x[MACRO_OBJECTCACHEFILE]=(char *)strdup(xodtemplate_cache_file)))
		strip(macro_x[MACRO_OBJECTCACHEFILE]);
#endif

#ifdef DEBUG0
	printf("xodtemplate_grab_config_info() end\n");
#endif
	return OK;
        }



/* process all files in a specific config directory */
int xodtemplate_process_config_dir(char *dirname, int options){
	char file[MAX_FILENAME_LENGTH];
	DIR *dirp=NULL;
	struct dirent *dirfile=NULL;
	int result=OK;
	register int x=0;
	char link_buffer[MAX_FILENAME_LENGTH];
	char linked_to_buffer[MAX_FILENAME_LENGTH];
	struct stat stat_buf;
	ssize_t readlink_count;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_process_config_dir() start\n");
#endif

	/* open the directory for reading */
	dirp=opendir(dirname);
        if(dirp==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not open config directory '%s' for reading.\n",dirname);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* process all files in the directory... */
	while((dirfile=readdir(dirp))!=NULL){

		/* create /path/to/file */
		snprintf(file,sizeof(file),"%s/%s",dirname,dirfile->d_name);
		file[sizeof(file)-1]='\x0';

		/* process this if it's a config file... */
		x=strlen(dirfile->d_name);
		if(x>4 && !strcmp(dirfile->d_name+(x-4),".cfg")){

#ifdef _DIRENT_HAVE_D_TYPE
			/* only process normal files and symlinks */
			if(dirfile->d_type==DT_UNKNOWN){
				x=stat(file,&stat_buf);
				if(x==0){
					if(!S_ISREG(stat_buf.st_mode) && !S_ISLNK(stat_buf.st_mode))
						continue;
				        }
			        }
			else{
				if(dirfile->d_type!=DT_REG && dirfile->d_type!=DT_LNK)
					continue;
			        }
#endif

			/* process the config file */
			result=xodtemplate_process_config_file(file,options);

			/* break out if we encountered an error */
			if(result==ERROR)
				break;
		        }

#ifdef _DIRENT_HAVE_D_TYPE
		/* recurse into subdirectories... */
		if(dirfile->d_type==DT_UNKNOWN || dirfile->d_type==DT_DIR || dirfile->d_type==DT_LNK){

			if(dirfile->d_type==DT_UNKNOWN){
				x=stat(file,&stat_buf);
				if(x==0){
					if(!S_ISDIR(stat_buf.st_mode) && !S_ISLNK(stat_buf.st_mode))
						continue;
				        }
				else
					continue;
			        }

			/* ignore current, parent and hidden directory entries */
			if(dirfile->d_name[0]=='.')
				continue;

			/* check that a symlink points to a dir */

			if(dirfile->d_type==DT_LNK || (dirfile->d_type==DT_UNKNOWN && S_ISLNK(stat_buf.st_mode))){

				readlink_count=readlink(file,link_buffer,MAX_FILENAME_LENGTH);

				/* Handle special case with maxxed out buffer */
				if(readlink_count==MAX_FILENAME_LENGTH){
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Cannot follow symlink '%s' - Too big!\n",file);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }

				/* Check if reading symlink failed */
				if(readlink_count==-1){
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Cannot read symlink '%s': %s!\n",file, strerror(errno));
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }

				/* terminate string */
				link_buffer[readlink_count]='\x0';
			
				/* create new symlink buffer name */
				if(link_buffer[0]=='/'){
					/* full path */
					snprintf(linked_to_buffer,sizeof(linked_to_buffer)-1,"%s",link_buffer);
					linked_to_buffer[sizeof(linked_to_buffer)-1]='\x0';
				        }
				else{
					/* relative path */
					snprintf(linked_to_buffer,sizeof(linked_to_buffer)-1,"%s/%s",dirname,link_buffer);
					linked_to_buffer[sizeof(linked_to_buffer)-1]='\x0';
				        }

				/* 
				 * At this point, we know it's a symlink -
				 * now check for whether it points to a 
				 * directory or not
				 */

				x=stat(linked_to_buffer,&stat_buf);
				if(x!=0){

					/* non-existent symlink - bomb out */
					if(errno==ENOENT){
#ifdef NSCORE
						asprintf(&temp_buffer,"Error: symlink '%s' points to non-existent '%s'!\n",file,link_buffer);
						write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
						my_free((void **)&temp_buffer);
#endif
						return ERROR;
					        }

#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Cannot stat symlinked from '%s' to %s!\n",file,link_buffer);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }

				if(!S_ISDIR(stat_buf.st_mode)){
					/* Not a symlink to a dir - skip */
					continue;
				        }

				/* Otherwise, we may proceed! */
			        }

			/* process the config directory */
			result=xodtemplate_process_config_dir(file,options);

			/* break out if we encountered an error */
			if(result==ERROR)
				break;
		        }
#endif
		}

	closedir(dirp);

#ifdef DEBUG0
	printf("xodtemplate_process_config_dir() end\n");
#endif

	return result;
        }


/* process data in a specific config file */
int xodtemplate_process_config_file(char *filename, int options){
	mmapfile *thefile=NULL;
	char *input=NULL;
	register int in_definition=FALSE;
	register int current_line=0;
	int result=OK;
	register int x=0;
	register int y=0;
	char *ptr=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_process_config_file() start\n");
#endif


	/* save config file name */
	xodtemplate_config_files[xodtemplate_current_config_file++]=(char *)strdup(filename);

	/* reallocate memory for config files */
	if(!(xodtemplate_current_config_file%256)){
		xodtemplate_config_files=(char **)realloc(xodtemplate_config_files,(xodtemplate_current_config_file+256)*sizeof(char **));
		if(xodtemplate_config_files==NULL){
#ifdef DEBUG1
			printf("Error: Cannot re-allocate memory for config file names!\n");
#endif
			return ERROR;
		        }
	        }

	/* open the config file for reading */
	if((thefile=mmap_fopen(filename))==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Cannot open config file '%s' for reading: %s\n",filename,strerror(errno));
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* read in all lines from the config file */
	while(1){

		/* free memory */
		my_free((void **)&input);

		/* read the next line */
		if((input=mmap_fgets(thefile))==NULL)
			break;

		current_line++;

		/* skip empty lines */
		if(input[0]=='#' || input[0]==';' || input[0]=='\r' || input[0]=='\n')
			continue;

		/* grab data before comment delimiter - faster than a strtok() and strncpy()... */
		for(x=0;input[x]!='\x0';x++)
			if(input[x]==';')
				break;
		input[x]='\x0';

		/* strip input */
		strip(input);

		/* skip blank lines */
		if(input[0]=='\x0')
			continue;

		/* this is the start of an object definition */
		if(strstr(input,"define")==input){

			/* get the type of object we're defining... */
			for(x=6;input[x]!='\x0';x++)
				if(input[x]!=' ' && input[x]!='\t')
					break;
			for(y=0;input[x]!='\x0';x++){
				if(input[x]==' ' || input[x]=='\t' ||  input[x]=='{')
					break;
				else
					input[y++]=input[x];
			        }
			input[y]='\x0';

			/* make sure an object type is specified... */
			if(input[0]=='\x0'){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: No object type specified in file '%s' on line %d.\n",filename,current_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				result=ERROR;
				break;
			        }

			/* check validity of object type */
			if(strcmp(input,"timeperiod") && strcmp(input,"command") && strcmp(input,"contact") && strcmp(input,"contactgroup") && strcmp(input,"host") && strcmp(input,"hostgroup") && strcmp(input,"servicegroup") && strcmp(input,"service") && strcmp(input,"servicedependency") && strcmp(input,"serviceescalation") && strcmp(input,"hostgroupescalation") && strcmp(input,"hostdependency") && strcmp(input,"hostescalation") && strcmp(input,"hostextinfo") && strcmp(input,"serviceextinfo")){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Invalid object definition type '%s' in file '%s' on line %d.\n",input,filename,current_line);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				result=ERROR;
				break;
			        }

			/* we're already in an object definition... */
			if(in_definition==TRUE){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Unexpected start of object definition in file '%s' on line %d.  Make sure you close preceding objects before starting a new one.\n",filename,current_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif				
				result=ERROR;
				break;
			        }

			/* start a new definition */
			if(xodtemplate_begin_object_definition(input,options,xodtemplate_current_config_file,current_line)==ERROR){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add object definition in file '%s' on line %d.\n",filename,current_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				result=ERROR;
				break;
			        }

			in_definition=TRUE;
		        }

		/* this is the close of an object definition */
		else if(!strcmp(input,"}") && in_definition==TRUE){

			in_definition=FALSE;

			/* close out current definition */
			if(xodtemplate_end_object_definition(options)==ERROR){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not complete object definition in file '%s' on line %d.\n",filename,current_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				result=ERROR;
				break;
			        }
		        }

		/* we're currently inside an object definition */
		else if(in_definition==TRUE){

			/* this is the close of an object definition */
			if(!strcmp(input,"}")){

				in_definition=FALSE;

				/* close out current definition */
				if(xodtemplate_end_object_definition(options)==ERROR){
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Could not complete object definition in file '%s' on line %d.\n",filename,current_line);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					result=ERROR;
					break;
			                }
		                }

			/* this is a directive inside an object definition */
			else{

				/* add directive to object definition */
				if(xodtemplate_add_object_property(input,options)==ERROR){
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Could not add object property in file '%s' on line %d.\n",filename,current_line);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					result=ERROR;
					break;
				        }
			        }
		        }

		/* include another file */
		else if(strstr(input,"include_file=")==input){

			ptr=strtok(input,"=");
			ptr=strtok(NULL,"\n");

			if(ptr!=NULL){
				result=xodtemplate_process_config_file(ptr,options);
				if(result==ERROR)
					break;
			        }
		        }

		/* include a directory */
		else if(strstr(input,"include_dir")==input){

			ptr=strtok(input,"=");
			ptr=strtok(NULL,"\n");

			if(ptr!=NULL){
				result=xodtemplate_process_config_dir(ptr,options);
				if(result==ERROR)
					break;
			        }
		        }

		/* unexpected token or statement */
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Unexpected token or statement in file '%s' on line %d.\n",filename,current_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			result=ERROR;
			break;
		        }
	        }

	/* free memory and close file */
	my_free((void **)&input);
	mmap_fclose(thefile);

	/* whoops - EOF while we were in the middle of an object definition... */
	if(in_definition==TRUE && result==OK){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Unexpected EOF in file '%s' on line %d - check for a missing closing bracket.\n",filename,current_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		result=ERROR;
	        }

#ifdef DEBUG0
	printf("xodtemplate_process_config_file() end\n");
#endif

	return result;
        }





/******************************************************************/
/***************** OBJECT DEFINITION FUNCTIONS ********************/
/******************************************************************/

/* starts a new object definition */
int xodtemplate_begin_object_definition(char *input, int options, int config_file, int start_line){
	int result=OK;
	xodtemplate_timeperiod *new_timeperiod=NULL;
	xodtemplate_command *new_command=NULL;
	xodtemplate_contactgroup *new_contactgroup=NULL;
	xodtemplate_hostgroup *new_hostgroup=NULL;
	xodtemplate_servicegroup *new_servicegroup=NULL;
	xodtemplate_servicedependency *new_servicedependency=NULL;
	xodtemplate_serviceescalation *new_serviceescalation=NULL;
	xodtemplate_contact *new_contact=NULL;
	xodtemplate_host *new_host=NULL;
	xodtemplate_service *new_service=NULL;
	xodtemplate_hostdependency *new_hostdependency=NULL;
	xodtemplate_hostescalation *new_hostescalation=NULL;
	xodtemplate_hostextinfo *new_hostextinfo=NULL;
	xodtemplate_serviceextinfo *new_serviceextinfo=NULL;
	register int x=0;

#ifdef DEBUG0
	printf("xodtemplate_begin_object_definition() start\n");
#endif

	if(!strcmp(input,"service"))
		xodtemplate_current_object_type=XODTEMPLATE_SERVICE;
	else if(!strcmp(input,"host"))
		xodtemplate_current_object_type=XODTEMPLATE_HOST;
	else if(!strcmp(input,"command"))
		xodtemplate_current_object_type=XODTEMPLATE_COMMAND;
	else if(!strcmp(input,"contact"))
		xodtemplate_current_object_type=XODTEMPLATE_CONTACT;
	else if(!strcmp(input,"contactgroup"))
		xodtemplate_current_object_type=XODTEMPLATE_CONTACTGROUP;
	else if(!strcmp(input,"hostgroup"))
		xodtemplate_current_object_type=XODTEMPLATE_HOSTGROUP;
	else if(!strcmp(input,"servicegroup"))
		xodtemplate_current_object_type=XODTEMPLATE_SERVICEGROUP;
	else if(!strcmp(input,"timeperiod"))
		xodtemplate_current_object_type=XODTEMPLATE_TIMEPERIOD;
	else if(!strcmp(input,"servicedependency"))
		xodtemplate_current_object_type=XODTEMPLATE_SERVICEDEPENDENCY;
	else if(!strcmp(input,"serviceescalation"))
		xodtemplate_current_object_type=XODTEMPLATE_SERVICEESCALATION;
	else if(!strcmp(input,"hostdependency"))
		xodtemplate_current_object_type=XODTEMPLATE_HOSTDEPENDENCY;
	else if(!strcmp(input,"hostescalation"))
		xodtemplate_current_object_type=XODTEMPLATE_HOSTESCALATION;
	else if(!strcmp(input,"hostextinfo"))
		xodtemplate_current_object_type=XODTEMPLATE_HOSTEXTINFO;
	else if(!strcmp(input,"serviceextinfo"))
		xodtemplate_current_object_type=XODTEMPLATE_SERVICEEXTINFO;
	else
		return ERROR;


	/* check to see if we should process this type of object */
	switch(xodtemplate_current_object_type){
	case XODTEMPLATE_TIMEPERIOD:
		if(!(options & READ_TIMEPERIODS))
			return OK;
		break;
	case XODTEMPLATE_COMMAND:
		if(!(options & READ_COMMANDS))
			return OK;
		break;
	case XODTEMPLATE_CONTACT:
		if(!(options & READ_CONTACTS))
			return OK;
		break;
	case XODTEMPLATE_CONTACTGROUP:
		if(!(options & READ_CONTACTGROUPS))
			return OK;
		break;
	case XODTEMPLATE_HOST:
		if(!(options & READ_HOSTS))
			return OK;
		break;
	case XODTEMPLATE_HOSTGROUP:
		if(!(options & READ_HOSTGROUPS))
			return OK;
		break;
	case XODTEMPLATE_SERVICEGROUP:
		if(!(options & READ_SERVICEGROUPS))
			return OK;
		break;
	case XODTEMPLATE_SERVICE:
		if(!(options & READ_SERVICES))
			return OK;
		break;
	case XODTEMPLATE_SERVICEDEPENDENCY:
		if(!(options & READ_SERVICEDEPENDENCIES))
			return OK;
		break;
	case XODTEMPLATE_SERVICEESCALATION:
		if(!(options & READ_SERVICEESCALATIONS))
			return OK;
		break;
	case XODTEMPLATE_HOSTDEPENDENCY:
		if(!(options & READ_HOSTDEPENDENCIES))
			return OK;
		break;
	case XODTEMPLATE_HOSTESCALATION:
		if(!(options & READ_HOSTESCALATIONS))
			return OK;
		break;
	case XODTEMPLATE_HOSTEXTINFO:
		if(!(options & READ_HOSTEXTINFO))
			return OK;
		break;
	case XODTEMPLATE_SERVICEEXTINFO:
		if(!(options & READ_SERVICEEXTINFO))
			return OK;
		break;
	default:
		return ERROR;
		break;
	        }



	/* add a new (blank) object */
	switch(xodtemplate_current_object_type){

	case XODTEMPLATE_TIMEPERIOD:

		/* allocate memory */
		new_timeperiod=(xodtemplate_timeperiod *)malloc(sizeof(xodtemplate_timeperiod));
		if(new_timeperiod==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for timeperiod definition\n");
#endif
			return ERROR;
		        }

		new_timeperiod->template=NULL;
		new_timeperiod->name=NULL;

		new_timeperiod->timeperiod_name=NULL;
		new_timeperiod->alias=NULL;
		for(x=0;x<7;x++)
			new_timeperiod->timeranges[x]=NULL;
		new_timeperiod->has_been_resolved=FALSE;
		new_timeperiod->register_object=TRUE;

		new_timeperiod->_config_file=config_file;
		new_timeperiod->_start_line=start_line;

		/* add new timeperiod to head of list in memory */
		new_timeperiod->next=xodtemplate_timeperiod_list;
		xodtemplate_timeperiod_list=new_timeperiod;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_timeperiod_list;

		break;

	case XODTEMPLATE_COMMAND:

		/* allocate memory */
		new_command=(xodtemplate_command *)malloc(sizeof(xodtemplate_command));
		if(new_command==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for command definition\n");
#endif
			return ERROR;
		        }
		
		new_command->template=NULL;
		new_command->name=NULL;

		new_command->command_name=NULL;
		new_command->command_line=NULL;
		new_command->has_been_resolved=FALSE;
		new_command->register_object=TRUE;

		new_command->_config_file=config_file;
		new_command->_start_line=start_line;

		/* add new command to head of list in memory */
		new_command->next=xodtemplate_command_list;
		xodtemplate_command_list=new_command;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_command_list;

		break;

	case XODTEMPLATE_CONTACTGROUP:

		/* allocate memory */
		new_contactgroup=(xodtemplate_contactgroup *)malloc(sizeof(xodtemplate_contactgroup));
		if(new_contactgroup==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for contactgroup definition\n");
#endif
			return ERROR;
		        }
		
		new_contactgroup->template=NULL;
		new_contactgroup->name=NULL;

		new_contactgroup->contactgroup_name=NULL;
		new_contactgroup->alias=NULL;
		new_contactgroup->members=NULL;
		new_contactgroup->has_been_resolved=FALSE;
		new_contactgroup->register_object=TRUE;

		new_contactgroup->_config_file=config_file;
		new_contactgroup->_start_line=start_line;

		/* add new contactgroup to head of list in memory */
		new_contactgroup->next=xodtemplate_contactgroup_list;
		xodtemplate_contactgroup_list=new_contactgroup;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_contactgroup_list;
		break;


	case XODTEMPLATE_HOSTGROUP:

		/* allocate memory */
		new_hostgroup=(xodtemplate_hostgroup *)malloc(sizeof(xodtemplate_hostgroup));
		if(new_hostgroup==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for hostgroup definition\n");
#endif
			return ERROR;
		        }
		
		new_hostgroup->template=NULL;
		new_hostgroup->name=NULL;

		new_hostgroup->hostgroup_name=NULL;
		new_hostgroup->alias=NULL;
		new_hostgroup->members=NULL;
		new_hostgroup->have_members=FALSE;
		new_hostgroup->hostgroup_members=NULL;
		new_hostgroup->have_hostgroup_members=FALSE;
		new_hostgroup->has_been_resolved=FALSE;
		new_hostgroup->register_object=TRUE;

		new_hostgroup->_config_file=config_file;
		new_hostgroup->_start_line=start_line;

		/* add new hostgroup to head of list in memory */
		new_hostgroup->next=xodtemplate_hostgroup_list;
		xodtemplate_hostgroup_list=new_hostgroup;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_hostgroup_list;
		break;

	case XODTEMPLATE_SERVICEGROUP:

		/* allocate memory */
		new_servicegroup=(xodtemplate_servicegroup *)malloc(sizeof(xodtemplate_servicegroup));
		if(new_servicegroup==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for servicegroup definition\n");
#endif
			return ERROR;
		        }
		
		new_servicegroup->template=NULL;
		new_servicegroup->name=NULL;

		new_servicegroup->servicegroup_name=NULL;
		new_servicegroup->alias=NULL;
		new_servicegroup->members=NULL;
		new_servicegroup->have_members=FALSE;
		new_servicegroup->servicegroup_members=NULL;
		new_servicegroup->have_servicegroup_members=FALSE;
		new_servicegroup->has_been_resolved=FALSE;
		new_servicegroup->register_object=TRUE;

		new_servicegroup->_config_file=config_file;
		new_servicegroup->_start_line=start_line;

		/* add new servicegroup to head of list in memory */
		new_servicegroup->next=xodtemplate_servicegroup_list;
		xodtemplate_servicegroup_list=new_servicegroup;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_servicegroup_list;

		break;

	case XODTEMPLATE_SERVICEDEPENDENCY:

		/* allocate memory */
		new_servicedependency=(xodtemplate_servicedependency *)malloc(sizeof(xodtemplate_servicedependency));
		if(new_servicedependency==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for servicedependency definition\n");
#endif
			return ERROR;
		        }
		
		new_servicedependency->template=NULL;
		new_servicedependency->name=NULL;

		new_servicedependency->host_name=NULL;
		new_servicedependency->have_host_name=FALSE;
		new_servicedependency->service_description=NULL;
		new_servicedependency->have_service_description=FALSE;
		new_servicedependency->dependent_host_name=NULL;
		new_servicedependency->have_dependent_host_name=FALSE;
		new_servicedependency->dependent_service_description=NULL;
		new_servicedependency->have_dependent_service_description=FALSE;
		new_servicedependency->servicegroup_name=NULL;
		new_servicedependency->have_servicegroup_name=FALSE;
		new_servicedependency->hostgroup_name=NULL;
		new_servicedependency->have_hostgroup_name=FALSE;
		new_servicedependency->dependent_servicegroup_name=NULL;
		new_servicedependency->have_dependent_servicegroup_name=FALSE;
		new_servicedependency->dependent_hostgroup_name=NULL;
		new_servicedependency->have_dependent_hostgroup_name=FALSE;
		new_servicedependency->inherits_parent=FALSE;
		new_servicedependency->fail_execute_on_ok=FALSE;
		new_servicedependency->fail_execute_on_unknown=FALSE;
		new_servicedependency->fail_execute_on_warning=FALSE;
		new_servicedependency->fail_execute_on_critical=FALSE;
		new_servicedependency->fail_execute_on_pending=FALSE;
		new_servicedependency->fail_notify_on_ok=FALSE;
		new_servicedependency->fail_notify_on_unknown=FALSE;
		new_servicedependency->fail_notify_on_warning=FALSE;
		new_servicedependency->fail_notify_on_critical=FALSE;
		new_servicedependency->fail_notify_on_pending=FALSE;
		new_servicedependency->have_inherits_parent=FALSE;
		new_servicedependency->have_execution_dependency_options=FALSE;
		new_servicedependency->have_notification_dependency_options=FALSE;
		new_servicedependency->has_been_resolved=FALSE;
		new_servicedependency->register_object=TRUE;

		new_servicedependency->_config_file=config_file;
		new_servicedependency->_start_line=start_line;

		/* add new servicedependency to head of list in memory */
		new_servicedependency->next=xodtemplate_servicedependency_list;
		xodtemplate_servicedependency_list=new_servicedependency;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_servicedependency_list;
		break;

	case XODTEMPLATE_SERVICEESCALATION:

		/* allocate memory */
		new_serviceescalation=(xodtemplate_serviceescalation *)malloc(sizeof(xodtemplate_serviceescalation));
		if(new_serviceescalation==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for serviceescalation definition\n");
#endif
			return ERROR;
		        }
		
		new_serviceescalation->template=NULL;
		new_serviceescalation->name=NULL;

		new_serviceescalation->host_name=NULL;
		new_serviceescalation->have_host_name=FALSE;
		new_serviceescalation->service_description=NULL;
		new_serviceescalation->have_service_description=FALSE;
		new_serviceescalation->servicegroup_name=NULL;
		new_serviceescalation->have_servicegroup_name=FALSE;
		new_serviceescalation->hostgroup_name=NULL;
		new_serviceescalation->have_hostgroup_name=FALSE;
		new_serviceescalation->escalation_period=NULL;
		new_serviceescalation->have_escalation_period=FALSE;
		new_serviceescalation->contact_groups=NULL;
		new_serviceescalation->have_contact_groups=FALSE;
		new_serviceescalation->first_notification=-2;
		new_serviceescalation->last_notification=-2;
		new_serviceescalation->notification_interval=-2;
		new_serviceescalation->escalate_on_warning=FALSE;
		new_serviceescalation->escalate_on_unknown=FALSE;
		new_serviceescalation->escalate_on_critical=FALSE;
		new_serviceescalation->escalate_on_recovery=FALSE;
		new_serviceescalation->have_first_notification=FALSE;
		new_serviceescalation->have_last_notification=FALSE;
		new_serviceescalation->have_notification_interval=FALSE;
		new_serviceescalation->have_escalation_options=FALSE;
		new_serviceescalation->has_been_resolved=FALSE;
		new_serviceescalation->register_object=TRUE;

		new_serviceescalation->_config_file=config_file;
		new_serviceescalation->_start_line=start_line;

		/* add new serviceescalation to head of list in memory */
		new_serviceescalation->next=xodtemplate_serviceescalation_list;
		xodtemplate_serviceescalation_list=new_serviceescalation;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_serviceescalation_list;
		break;

	case XODTEMPLATE_CONTACT:

		/* allocate memory */
		new_contact=(xodtemplate_contact *)malloc(sizeof(xodtemplate_contact));
		if(new_contact==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for contact definition\n");
#endif
			return ERROR;
		        }
		
		new_contact->template=NULL;
		new_contact->name=NULL;

		new_contact->contact_name=NULL;
		new_contact->alias=NULL;
		new_contact->contact_groups=NULL;
		new_contact->have_contact_groups=FALSE;
		new_contact->email=NULL;
		new_contact->have_email=FALSE;
		new_contact->pager=NULL;
		new_contact->have_pager=FALSE;
		for(x=0;x<MAX_XODTEMPLATE_CONTACT_ADDRESSES;x++){
			new_contact->address[x]=NULL;
			new_contact->have_address[x]=FALSE;
		        }
		new_contact->host_notification_period=NULL;
		new_contact->have_host_notification_period=FALSE;
		new_contact->host_notification_commands=NULL;
		new_contact->have_host_notification_commands=FALSE;
		new_contact->service_notification_period=NULL;
		new_contact->have_service_notification_period=FALSE;
		new_contact->service_notification_commands=NULL;
		new_contact->have_service_notification_commands=FALSE;
		new_contact->notify_on_host_down=FALSE;
		new_contact->notify_on_host_unreachable=FALSE;
		new_contact->notify_on_host_recovery=FALSE;
		new_contact->notify_on_host_flapping=FALSE;
		new_contact->notify_on_service_unknown=FALSE;
		new_contact->notify_on_service_warning=FALSE;
		new_contact->notify_on_service_critical=FALSE;
		new_contact->notify_on_service_recovery=FALSE;
		new_contact->notify_on_service_flapping=FALSE;
		new_contact->have_host_notification_options=FALSE;
		new_contact->have_service_notification_options=FALSE;
		new_contact->host_notifications_enabled=TRUE;
		new_contact->have_host_notifications_enabled=FALSE;
		new_contact->service_notifications_enabled=TRUE;
		new_contact->have_service_notifications_enabled=FALSE;
		new_contact->can_submit_commands=TRUE;
		new_contact->have_can_submit_commands=FALSE;
		new_contact->retain_status_information=TRUE;
		new_contact->have_retain_status_information=FALSE;
		new_contact->retain_nonstatus_information=TRUE;
		new_contact->have_retain_nonstatus_information=FALSE;
		new_contact->custom_variables=NULL;
		new_contact->has_been_resolved=FALSE;
		new_contact->register_object=TRUE;

		new_contact->_config_file=config_file;
		new_contact->_start_line=start_line;

		/* add new contact to head of list in memory */
		new_contact->next=xodtemplate_contact_list;
		xodtemplate_contact_list=new_contact;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_contact_list;
		break;

	case XODTEMPLATE_HOST:

		/* allocate memory */
		new_host=(xodtemplate_host *)malloc(sizeof(xodtemplate_host));
		if(new_host==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for host definition\n");
#endif
			return ERROR;
		        }
		
		new_host->template=NULL;
		new_host->name=NULL;

		new_host->host_name=NULL;
		new_host->display_name=NULL;
		new_host->have_display_name=FALSE;
		new_host->alias=NULL;
		new_host->address=NULL;
		new_host->parents=NULL;
		new_host->have_parents=FALSE;
		new_host->host_groups=NULL;
		new_host->have_host_groups=FALSE;
		new_host->check_command=NULL;
		new_host->have_check_command=FALSE;
		new_host->check_period=NULL;
		new_host->have_check_period=FALSE;
		new_host->event_handler=NULL;
		new_host->have_event_handler=FALSE;
		new_host->contact_groups=NULL;
		new_host->have_contact_groups=FALSE;
		new_host->notification_period=NULL;
		new_host->have_notification_period=FALSE;
		new_host->failure_prediction_options=NULL;
		new_host->have_failure_prediction_options=FALSE;
		new_host->check_interval=0;
		new_host->have_check_interval=FALSE;
		new_host->active_checks_enabled=TRUE;
		new_host->have_active_checks_enabled=FALSE;
		new_host->passive_checks_enabled=TRUE;
		new_host->have_passive_checks_enabled=FALSE;
		new_host->obsess_over_host=TRUE;
		new_host->have_obsess_over_host=FALSE;
		new_host->max_check_attempts=-2;
		new_host->have_max_check_attempts=FALSE;
		new_host->event_handler_enabled=TRUE;
		new_host->have_event_handler_enabled=FALSE;
		new_host->check_freshness=FALSE;
		new_host->have_check_freshness=FALSE;
		new_host->freshness_threshold=0;
		new_host->have_freshness_threshold=0;
		new_host->flap_detection_enabled=TRUE;
		new_host->have_flap_detection_enabled=FALSE;
		new_host->low_flap_threshold=0.0;
		new_host->have_low_flap_threshold=FALSE;
		new_host->high_flap_threshold=0.0;
		new_host->have_high_flap_threshold=FALSE;
		new_host->flap_detection_on_up=TRUE;
		new_host->flap_detection_on_down=TRUE;
		new_host->flap_detection_on_unreachable=TRUE;
		new_host->have_flap_detection_options=FALSE;
		new_host->notify_on_down=FALSE;
		new_host->notify_on_unreachable=FALSE;
		new_host->notify_on_recovery=FALSE;
		new_host->notify_on_flapping=FALSE;
		new_host->have_notification_options=FALSE;
		new_host->notifications_enabled=TRUE;
		new_host->have_notifications_enabled=FALSE;
		new_host->notification_interval=-2;
		new_host->have_notification_interval=FALSE;
		new_host->first_notification_delay=0;
		new_host->have_first_notification_delay=FALSE;
		new_host->stalk_on_up=FALSE;
		new_host->stalk_on_down=FALSE;
		new_host->stalk_on_unreachable=FALSE;
		new_host->have_stalking_options=FALSE;
		new_host->process_perf_data=TRUE;
		new_host->have_process_perf_data=FALSE;
		new_host->failure_prediction_enabled=TRUE;
		new_host->have_failure_prediction_enabled=FALSE;
		new_host->retain_status_information=TRUE;
		new_host->have_retain_status_information=FALSE;
		new_host->retain_nonstatus_information=TRUE;
		new_host->have_retain_nonstatus_information=FALSE;
		new_host->custom_variables=NULL;
		new_host->has_been_resolved=FALSE;
		new_host->register_object=TRUE;

		new_host->_config_file=config_file;
		new_host->_start_line=start_line;

		/* add new host to head of list in memory */
		new_host->next=xodtemplate_host_list;
		xodtemplate_host_list=new_host;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_host_list;
		break;

	case XODTEMPLATE_SERVICE:

		/* allocate memory */
		new_service=(xodtemplate_service *)malloc(sizeof(xodtemplate_service));
		if(new_service==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for service definition\n");
#endif
			return ERROR;
		        }
		
		new_service->template=NULL;
		new_service->name=NULL;

		new_service->host_name=NULL;
		new_service->have_host_name=FALSE;
		new_service->service_description=NULL;
		new_service->have_service_description=FALSE;
		new_service->display_name=NULL;
		new_service->have_display_name=FALSE;
		new_service->hostgroup_name=NULL;
		new_service->have_hostgroup_name=FALSE;
		new_service->service_groups=NULL;
		new_service->have_service_groups=FALSE;
		new_service->check_command=NULL;
		new_service->have_check_command=FALSE;
		new_service->check_period=NULL;
		new_service->have_check_period=FALSE;
		new_service->event_handler=NULL;
		new_service->have_event_handler=FALSE;
		new_service->notification_period=NULL;
		new_service->have_notification_period=FALSE;
		new_service->contact_groups=NULL;
		new_service->have_contact_groups=FALSE;
		new_service->failure_prediction_options=NULL;
		new_service->have_failure_prediction_options=FALSE;
		new_service->max_check_attempts=-2;
		new_service->have_max_check_attempts=FALSE;
		new_service->normal_check_interval=-2;
		new_service->have_normal_check_interval=FALSE;
		new_service->retry_check_interval=-2;
		new_service->have_retry_check_interval=FALSE;
		new_service->active_checks_enabled=TRUE;
		new_service->have_active_checks_enabled=FALSE;
		new_service->passive_checks_enabled=TRUE;
		new_service->have_passive_checks_enabled=FALSE;
		new_service->parallelize_check=TRUE;
		new_service->have_parallelize_check=FALSE;
		new_service->is_volatile=FALSE;
		new_service->have_is_volatile=FALSE;
		new_service->obsess_over_service=TRUE;
		new_service->have_obsess_over_service=FALSE;
		new_service->event_handler_enabled=TRUE;
		new_service->have_event_handler_enabled=FALSE;
		new_service->check_freshness=FALSE;
		new_service->have_check_freshness=FALSE;
		new_service->freshness_threshold=0;
		new_service->have_freshness_threshold=FALSE;
		new_service->flap_detection_enabled=TRUE;
		new_service->have_flap_detection_enabled=FALSE;
		new_service->low_flap_threshold=0.0;
		new_service->have_low_flap_threshold=FALSE;
		new_service->high_flap_threshold=0.0;
		new_service->have_high_flap_threshold=FALSE;
		new_service->flap_detection_on_ok=TRUE;
		new_service->flap_detection_on_warning=TRUE;
		new_service->flap_detection_on_unknown=TRUE;
		new_service->flap_detection_on_critical=TRUE;
		new_service->have_flap_detection_options=FALSE;
		new_service->notify_on_unknown=FALSE;
		new_service->notify_on_warning=FALSE;
		new_service->notify_on_critical=FALSE;
		new_service->notify_on_recovery=FALSE;
		new_service->notify_on_flapping=FALSE;
		new_service->have_notification_options=FALSE;
		new_service->notifications_enabled=TRUE;
		new_service->have_notifications_enabled=FALSE;
		new_service->notification_interval=-2;
		new_service->have_notification_interval=FALSE;
		new_service->first_notification_delay=0;
		new_service->have_first_notification_delay=FALSE;
		new_service->stalk_on_ok=FALSE;
		new_service->stalk_on_unknown=FALSE;
		new_service->stalk_on_warning=FALSE;
		new_service->stalk_on_critical=FALSE;
		new_service->have_stalking_options=FALSE;
		new_service->process_perf_data=TRUE;
		new_service->have_process_perf_data=FALSE;
		new_service->failure_prediction_enabled=TRUE;
		new_service->have_failure_prediction_enabled=FALSE;
		new_service->notes=NULL;
		new_service->have_notes=FALSE;
		new_service->notes_url=NULL;
		new_service->have_notes_url=FALSE;
		new_service->action_url=NULL;
		new_service->have_action_url=FALSE;
		new_service->icon_image=NULL;
		new_service->have_icon_image=FALSE;
		new_service->icon_image_alt=NULL;
		new_service->have_icon_image_alt=FALSE;
		new_service->retain_status_information=TRUE;
		new_service->have_retain_status_information=FALSE;
		new_service->retain_nonstatus_information=TRUE;
		new_service->have_retain_nonstatus_information=FALSE;
		new_service->custom_variables=NULL;
		new_service->has_been_resolved=FALSE;
		new_service->register_object=TRUE;

		new_service->_config_file=config_file;
		new_service->_start_line=start_line;

		/* add new service to head of list in memory */
		new_service->next=xodtemplate_service_list;
		xodtemplate_service_list=new_service;

		/* update current object pointer */
		xodtemplate_current_object=new_service;
		break;

	case XODTEMPLATE_HOSTDEPENDENCY:

		/* allocate memory */
		new_hostdependency=(xodtemplate_hostdependency *)malloc(sizeof(xodtemplate_hostdependency));
		if(new_hostdependency==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for hostdependency definition\n");
#endif
			return ERROR;
		        }
		
		new_hostdependency->template=NULL;
		new_hostdependency->name=NULL;

		new_hostdependency->host_name=NULL;
		new_hostdependency->have_host_name=FALSE;
		new_hostdependency->dependent_host_name=NULL;
		new_hostdependency->have_dependent_host_name=FALSE;
		new_hostdependency->hostgroup_name=NULL;
		new_hostdependency->have_hostgroup_name=FALSE;
		new_hostdependency->dependent_hostgroup_name=NULL;
		new_hostdependency->have_dependent_hostgroup_name=FALSE;
		new_hostdependency->inherits_parent=FALSE;
		new_hostdependency->fail_notify_on_up=FALSE;
		new_hostdependency->fail_notify_on_down=FALSE;
		new_hostdependency->fail_notify_on_unreachable=FALSE;
		new_hostdependency->fail_notify_on_pending=FALSE;
		new_hostdependency->fail_execute_on_up=FALSE;
		new_hostdependency->fail_execute_on_down=FALSE;
		new_hostdependency->fail_execute_on_unreachable=FALSE;
		new_hostdependency->fail_execute_on_pending=FALSE;
		new_hostdependency->have_inherits_parent=FALSE;
		new_hostdependency->have_notification_dependency_options=FALSE;
		new_hostdependency->have_execution_dependency_options=FALSE;
		new_hostdependency->has_been_resolved=FALSE;
		new_hostdependency->register_object=TRUE;

		new_hostdependency->_config_file=config_file;
		new_hostdependency->_start_line=start_line;

		/* add new hostdependency to head of list in memory */
		new_hostdependency->next=xodtemplate_hostdependency_list;
		xodtemplate_hostdependency_list=new_hostdependency;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_hostdependency_list;
		break;

	case XODTEMPLATE_HOSTESCALATION:

		/* allocate memory */
		new_hostescalation=(xodtemplate_hostescalation *)malloc(sizeof(xodtemplate_hostescalation));
		if(new_hostescalation==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for hostescalation definition\n");
#endif
			return ERROR;
		        }
		
		new_hostescalation->template=NULL;
		new_hostescalation->name=NULL;

		new_hostescalation->hostgroup_name=NULL;
		new_hostescalation->have_hostgroup_name=FALSE;
		new_hostescalation->host_name=NULL;
		new_hostescalation->have_host_name=FALSE;
		new_hostescalation->escalation_period=NULL;
		new_hostescalation->have_escalation_period=FALSE;
		new_hostescalation->contact_groups=NULL;
		new_hostescalation->have_contact_groups=FALSE;
		new_hostescalation->first_notification=-2;
		new_hostescalation->last_notification=-2;
		new_hostescalation->notification_interval=-2;
		new_hostescalation->escalate_on_down=FALSE;
		new_hostescalation->escalate_on_unreachable=FALSE;
		new_hostescalation->escalate_on_recovery=FALSE;
		new_hostescalation->have_first_notification=FALSE;
		new_hostescalation->have_last_notification=FALSE;
		new_hostescalation->have_notification_interval=FALSE;
		new_hostescalation->have_escalation_options=FALSE;
		new_hostescalation->has_been_resolved=FALSE;
		new_hostescalation->register_object=TRUE;

		new_hostescalation->_config_file=config_file;
		new_hostescalation->_start_line=start_line;

		/* add new hostescalation to head of list in memory */
		new_hostescalation->next=xodtemplate_hostescalation_list;
		xodtemplate_hostescalation_list=new_hostescalation;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_hostescalation_list;
		break;

	case XODTEMPLATE_HOSTEXTINFO:

		/* allocate memory */
		new_hostextinfo=(xodtemplate_hostextinfo *)malloc(sizeof(xodtemplate_hostextinfo));
		if(new_hostextinfo==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for extended host info\n");
#endif
			return ERROR;
		        }

		new_hostextinfo->template=NULL;
		new_hostextinfo->name=NULL;

		new_hostextinfo->host_name=NULL;
		new_hostextinfo->have_host_name=FALSE;
		new_hostextinfo->hostgroup_name=NULL;
		new_hostextinfo->have_hostgroup_name=FALSE;
		new_hostextinfo->notes=NULL;
		new_hostextinfo->have_notes=FALSE;
		new_hostextinfo->notes_url=NULL;
		new_hostextinfo->have_notes_url=FALSE;
		new_hostextinfo->action_url=NULL;
		new_hostextinfo->have_action_url=FALSE;
		new_hostextinfo->icon_image=NULL;
		new_hostextinfo->have_icon_image=FALSE;
		new_hostextinfo->icon_image_alt=NULL;
		new_hostextinfo->have_icon_image_alt=FALSE;
		new_hostextinfo->vrml_image=NULL;
		new_hostextinfo->have_vrml_image=FALSE;
		new_hostextinfo->statusmap_image=NULL;
		new_hostextinfo->have_statusmap_image=FALSE;
		new_hostextinfo->x_2d=-1;
		new_hostextinfo->y_2d=-1;
		new_hostextinfo->x_3d=0.0;
		new_hostextinfo->y_3d=0.0;
		new_hostextinfo->z_3d=0.0;
		new_hostextinfo->have_2d_coords=FALSE;
		new_hostextinfo->have_3d_coords=FALSE;
		new_hostextinfo->has_been_resolved=FALSE;
		new_hostextinfo->register_object=TRUE;

		new_hostextinfo->_config_file=config_file;
		new_hostextinfo->_start_line=start_line;

		/* add new extended host info to head of list in memory */
		new_hostextinfo->next=xodtemplate_hostextinfo_list;
		xodtemplate_hostextinfo_list=new_hostextinfo;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_hostextinfo_list;
		break;

	case XODTEMPLATE_SERVICEEXTINFO:

		/* allocate memory */
		new_serviceextinfo=(xodtemplate_serviceextinfo *)malloc(sizeof(xodtemplate_serviceextinfo));
		if(new_serviceextinfo==NULL){
#ifdef DEBUG1
			printf("Error: Could not allocate memory for extended service info\n");
#endif
			return ERROR;
		        }

		new_serviceextinfo->template=NULL;
		new_serviceextinfo->name=NULL;

		new_serviceextinfo->host_name=NULL;
		new_serviceextinfo->have_host_name=FALSE;
		new_serviceextinfo->hostgroup_name=NULL;
		new_serviceextinfo->have_hostgroup_name=FALSE;
		new_serviceextinfo->service_description=NULL;
		new_serviceextinfo->have_service_description=FALSE;
		new_serviceextinfo->notes=NULL;
		new_serviceextinfo->have_notes=FALSE;
		new_serviceextinfo->notes_url=NULL;
		new_serviceextinfo->have_notes_url=FALSE;
		new_serviceextinfo->action_url=NULL;
		new_serviceextinfo->have_action_url=FALSE;
		new_serviceextinfo->icon_image=NULL;
		new_serviceextinfo->have_icon_image=FALSE;
		new_serviceextinfo->icon_image_alt=NULL;
		new_serviceextinfo->have_icon_image_alt=FALSE;
		new_serviceextinfo->has_been_resolved=FALSE;
		new_serviceextinfo->register_object=TRUE;

		new_serviceextinfo->_config_file=config_file;
		new_serviceextinfo->_start_line=start_line;

		/* add new extended service info to head of list in memory */
		new_serviceextinfo->next=xodtemplate_serviceextinfo_list;
		xodtemplate_serviceextinfo_list=new_serviceextinfo;

		/* update current object pointer */
		xodtemplate_current_object=xodtemplate_serviceextinfo_list;
		break;

	default:
		return ERROR;
		break;
	        }

#ifdef DEBUG0
	printf("xodtemplate_begin_object_definition() end\n");
#endif

	return result;
        }



/* adds a property to an object definition */
int xodtemplate_add_object_property(char *input, int options){
	int result=OK;
	char *variable=NULL;
	char *value=NULL;
	char *temp_ptr=NULL;
	char *customvarname=NULL;
	char *customvarvalue=NULL;
	xodtemplate_timeperiod *temp_timeperiod=NULL;
	xodtemplate_command *temp_command=NULL;
	xodtemplate_contactgroup *temp_contactgroup=NULL;
	xodtemplate_hostgroup *temp_hostgroup=NULL;
	xodtemplate_servicegroup *temp_servicegroup=NULL;
	xodtemplate_servicedependency *temp_servicedependency=NULL;
	xodtemplate_serviceescalation *temp_serviceescalation=NULL;
	xodtemplate_contact *temp_contact=NULL;
	xodtemplate_host *temp_host=NULL;
	xodtemplate_service *temp_service=NULL;
	xodtemplate_hostdependency *temp_hostdependency=NULL;
	xodtemplate_hostescalation *temp_hostescalation=NULL;
	xodtemplate_hostextinfo *temp_hostextinfo=NULL;
	xodtemplate_serviceextinfo *temp_serviceextinfo=NULL;
	register int x=0;
	register int y=0;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_add_object_property() start\n");
#endif


	/* check to see if we should process this type of object */
	switch(xodtemplate_current_object_type){
	case XODTEMPLATE_TIMEPERIOD:
		if(!(options & READ_TIMEPERIODS))
			return OK;
		break;
	case XODTEMPLATE_COMMAND:
		if(!(options & READ_COMMANDS))
			return OK;
		break;
	case XODTEMPLATE_CONTACT:
		if(!(options & READ_CONTACTS))
			return OK;
		break;
	case XODTEMPLATE_CONTACTGROUP:
		if(!(options & READ_CONTACTGROUPS))
			return OK;
		break;
	case XODTEMPLATE_HOST:
		if(!(options & READ_HOSTS))
			return OK;
		break;
	case XODTEMPLATE_HOSTGROUP:
		if(!(options & READ_HOSTGROUPS))
			return OK;
		break;
	case XODTEMPLATE_SERVICEGROUP:
		if(!(options & READ_SERVICEGROUPS))
			return OK;
		break;
	case XODTEMPLATE_SERVICE:
		if(!(options & READ_SERVICES))
			return OK;
		break;
	case XODTEMPLATE_SERVICEDEPENDENCY:
		if(!(options & READ_SERVICEDEPENDENCIES))
			return OK;
		break;
	case XODTEMPLATE_SERVICEESCALATION:
		if(!(options & READ_SERVICEESCALATIONS))
			return OK;
		break;
	case XODTEMPLATE_HOSTDEPENDENCY:
		if(!(options & READ_HOSTDEPENDENCIES))
			return OK;
		break;
	case XODTEMPLATE_HOSTESCALATION:
		if(!(options & READ_HOSTESCALATIONS))
			return OK;
		break;
	case XODTEMPLATE_HOSTEXTINFO:
		if(!(options & READ_HOSTEXTINFO))
			return OK;
		break;
	case XODTEMPLATE_SERVICEEXTINFO:
		if(!(options & READ_SERVICEEXTINFO))
			return OK;
		break;
	default:
		return ERROR;
		break;
	        }

	/* get variable name */
	if((variable=(char *)strdup(input))==NULL)
		return ERROR;
	/* trim at first whitespace occurance */
	for(x=0,y=0;variable[x]!='\x0';x++){
		if(variable[x]==' ' || variable[x]=='\t')
			break;
		y++;
	        }
	variable[y]='\x0';
			
	/* get variable value */
	if((value=(char *)strdup(input+x))==NULL){
		my_free((void **)&variable);
		return ERROR;
	        }
	strip(value);


	switch(xodtemplate_current_object_type){

	case XODTEMPLATE_TIMEPERIOD:
		
		temp_timeperiod=(xodtemplate_timeperiod *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_timeperiod->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_timeperiod(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for timeperiod '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_timeperiod->_config_file),temp_timeperiod->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_timeperiod->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"timeperiod_name")){
			if((temp_timeperiod->timeperiod_name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"alias")){
			if((temp_timeperiod->alias=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"monday") || !strcmp(variable,"tuesday") || !strcmp(variable,"wednesday") || !strcmp(variable,"thursday") || !strcmp(variable,"friday") || !strcmp(variable,"saturday") || !strcmp(variable,"sunday")){
			if(!strcmp(variable,"monday"))
				x=1;
			else if(!strcmp(variable,"tuesday"))
				x=2;
			else if(!strcmp(variable,"wednesday"))
				x=3;
			else if(!strcmp(variable,"thursday"))
				x=4;
			else if(!strcmp(variable,"friday"))
				x=5;
			else if(!strcmp(variable,"saturday"))
				x=6;
			else
				x=0;
			if((temp_timeperiod->timeranges[x]=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"register"))
			temp_timeperiod->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid timeperiod object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
		break;



	case XODTEMPLATE_COMMAND:

		temp_command=(xodtemplate_command *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_command->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_command(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for command '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_command->_config_file),temp_command->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_command->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"command_name")){
			if((temp_command->command_name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"command_line")){
			if((temp_command->command_line=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"register"))
			temp_command->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid command object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		break;

	case XODTEMPLATE_CONTACTGROUP:

		temp_contactgroup=(xodtemplate_contactgroup *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_contactgroup->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_contactgroup(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for contactgroup '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_contactgroup->_config_file),temp_contactgroup->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_contactgroup->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"contactgroup_name")){
			if((temp_contactgroup->contactgroup_name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"alias")){
			if((temp_contactgroup->alias=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"members")){
			if((temp_contactgroup->members=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"register"))
			temp_contactgroup->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid contactgroup object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		break;

	case XODTEMPLATE_HOSTGROUP:

		temp_hostgroup=(xodtemplate_hostgroup *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_hostgroup->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_hostgroup(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for hostgroup '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_hostgroup->_config_file),temp_hostgroup->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_hostgroup->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"hostgroup_name")){
			if((temp_hostgroup->hostgroup_name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"alias")){
			if((temp_hostgroup->alias=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"members")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if(temp_hostgroup->members==NULL)
					temp_hostgroup->members=(char *)strdup(value);
				else{
					temp_hostgroup->members=(char *)realloc(temp_hostgroup->members,strlen(temp_hostgroup->members)+strlen(value)+2);
					if(temp_hostgroup->members!=NULL){
						strcat(temp_hostgroup->members,",");
						strcat(temp_hostgroup->members,value);
				                }
			                } 
				if(temp_hostgroup->members==NULL)
					result=ERROR;
			        }
			temp_hostgroup->have_members=TRUE;
		        }
		else if(!strcmp(variable,"hostgroup_members")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if(temp_hostgroup->hostgroup_members==NULL)
					temp_hostgroup->hostgroup_members=(char *)strdup(value);
				else{
					temp_hostgroup->hostgroup_members=(char *)realloc(temp_hostgroup->hostgroup_members,strlen(temp_hostgroup->hostgroup_members)+strlen(value)+2);
					if(temp_hostgroup->hostgroup_members!=NULL){
						strcat(temp_hostgroup->hostgroup_members,",");
						strcat(temp_hostgroup->hostgroup_members,value);
				                }
			                }
				if(temp_hostgroup->hostgroup_members==NULL)
					result=ERROR;
			        }
			temp_hostgroup->have_hostgroup_members=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_hostgroup->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid hostgroup object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		break;

	
	case XODTEMPLATE_SERVICEGROUP:

		temp_servicegroup=(xodtemplate_servicegroup *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_servicegroup->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_servicegroup(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for servicegroup '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_servicegroup->_config_file),temp_servicegroup->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_servicegroup->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"servicegroup_name")){
			if((temp_servicegroup->servicegroup_name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"alias")){
			if((temp_servicegroup->alias=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"members")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if(temp_servicegroup->members==NULL)
					temp_servicegroup->members=(char *)strdup(value);
				else{
					temp_servicegroup->members=(char *)realloc(temp_servicegroup->members,strlen(temp_servicegroup->members)+strlen(value)+2);
					if(temp_servicegroup->members!=NULL){
						strcat(temp_servicegroup->members,",");
						strcat(temp_servicegroup->members,value);
				                }
			                }
				if(temp_servicegroup->members==NULL)
					result=ERROR;
			        }
			temp_servicegroup->have_members=TRUE;
		        }
		else if(!strcmp(variable,"servicegroup_members")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if(temp_servicegroup->servicegroup_members==NULL)
					temp_servicegroup->servicegroup_members=(char *)strdup(value);
				else{
					temp_servicegroup->servicegroup_members=(char *)realloc(temp_servicegroup->servicegroup_members,strlen(temp_servicegroup->servicegroup_members)+strlen(value)+2);
					if(temp_servicegroup->servicegroup_members!=NULL){
						strcat(temp_servicegroup->servicegroup_members,",");
						strcat(temp_servicegroup->servicegroup_members,value);
				                }
			                }
				if(temp_servicegroup->servicegroup_members==NULL)
					result=ERROR;
			        }
			temp_servicegroup->have_servicegroup_members=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_servicegroup->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid servicegroup object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		break;

	
	case XODTEMPLATE_SERVICEDEPENDENCY:

		temp_servicedependency=(xodtemplate_servicedependency *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_servicedependency->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_servicedependency(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for service dependency '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_servicedependency->_config_file),temp_servicedependency->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_servicedependency->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"servicegroup") || !strcmp(variable,"servicegroups") || !strcmp(variable,"servicegroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_servicedependency->servicegroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_servicedependency->have_servicegroup_name=TRUE;
		        }
		else if(!strcmp(variable,"hostgroup") || !strcmp(variable,"hostgroups") || !strcmp(variable,"hostgroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_servicedependency->hostgroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_servicedependency->have_hostgroup_name=TRUE;
		        }
		else if(!strcmp(variable,"host") || !strcmp(variable,"host_name") || !strcmp(variable,"master_host") || !strcmp(variable,"master_host_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_servicedependency->host_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_servicedependency->have_host_name=TRUE;
		        }
		else if(!strcmp(variable,"description") || !strcmp(variable,"service_description") || !strcmp(variable,"master_description") || !strcmp(variable,"master_service_description")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_servicedependency->service_description=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_servicedependency->have_service_description=TRUE;
		        }
		else if(!strcmp(variable,"dependent_servicegroup") || !strcmp(variable,"dependent_servicegroups") || !strcmp(variable,"dependent_servicegroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_servicedependency->dependent_servicegroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_servicedependency->have_dependent_servicegroup_name=TRUE;
		        }
		else if(!strcmp(variable,"dependent_hostgroup") || !strcmp(variable,"dependent_hostgroups") || !strcmp(variable,"dependent_hostgroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_servicedependency->dependent_hostgroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_servicedependency->have_dependent_hostgroup_name=TRUE;
		        }
		else if(!strcmp(variable,"dependent_host") || !strcmp(variable,"dependent_host_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_servicedependency->dependent_host_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_servicedependency->have_dependent_host_name=TRUE;
		        }
		else if(!strcmp(variable,"dependent_description") || !strcmp(variable,"dependent_service_description")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_servicedependency->dependent_service_description=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_servicedependency->have_dependent_service_description=TRUE;
		        }
		else if(!strcmp(variable,"inherits_parent")){
			temp_servicedependency->inherits_parent=(atoi(value)>0)?TRUE:FALSE;
			temp_servicedependency->have_inherits_parent=TRUE;
		        }
		else if(!strcmp(variable,"execution_failure_options") || !strcmp(variable,"execution_failure_criteria")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"o") || !strcmp(temp_ptr,"ok"))
					temp_servicedependency->fail_execute_on_ok=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unknown"))
					temp_servicedependency->fail_execute_on_unknown=TRUE;
				else if(!strcmp(temp_ptr,"w") || !strcmp(temp_ptr,"warning"))
					temp_servicedependency->fail_execute_on_warning=TRUE;
				else if(!strcmp(temp_ptr,"c") || !strcmp(temp_ptr,"critical"))
					temp_servicedependency->fail_execute_on_critical=TRUE;
				else if(!strcmp(temp_ptr,"p") || !strcmp(temp_ptr,"pending"))
					temp_servicedependency->fail_execute_on_pending=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_servicedependency->fail_execute_on_ok=FALSE;
					temp_servicedependency->fail_execute_on_unknown=FALSE;
					temp_servicedependency->fail_execute_on_warning=FALSE;
					temp_servicedependency->fail_execute_on_critical=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid execution dependency option '%s' in servicedependency definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_servicedependency->have_execution_dependency_options=TRUE;
		        }
		else if(!strcmp(variable,"notification_failure_options") || !strcmp(variable,"notification_failure_criteria")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"o") || !strcmp(temp_ptr,"ok"))
					temp_servicedependency->fail_notify_on_ok=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unknown"))
					temp_servicedependency->fail_notify_on_unknown=TRUE;
				else if(!strcmp(temp_ptr,"w") || !strcmp(temp_ptr,"warning"))
					temp_servicedependency->fail_notify_on_warning=TRUE;
				else if(!strcmp(temp_ptr,"c") || !strcmp(temp_ptr,"critical"))
					temp_servicedependency->fail_notify_on_critical=TRUE;
				else if(!strcmp(temp_ptr,"p") || !strcmp(temp_ptr,"pending"))
					temp_servicedependency->fail_notify_on_pending=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_servicedependency->fail_notify_on_ok=FALSE;
					temp_servicedependency->fail_notify_on_unknown=FALSE;
					temp_servicedependency->fail_notify_on_warning=FALSE;
					temp_servicedependency->fail_notify_on_critical=FALSE;
					temp_servicedependency->fail_notify_on_pending=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid notification dependency option '%s' in servicedependency definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_servicedependency->have_notification_dependency_options=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_servicedependency->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid servicedependency object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
#endif
			return ERROR;
		        }

		break;

	
	case XODTEMPLATE_SERVICEESCALATION:

		temp_serviceescalation=(xodtemplate_serviceescalation *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_serviceescalation->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_serviceescalation(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for service escalation '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_serviceescalation->_config_file),temp_serviceescalation->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_serviceescalation->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"host") || !strcmp(variable,"host_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceescalation->host_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceescalation->have_host_name=TRUE;
		        }
		else if(!strcmp(variable,"description") || !strcmp(variable,"service_description")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceescalation->service_description=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceescalation->have_service_description=TRUE;
		        }
		else if(!strcmp(variable,"servicegroup") || !strcmp(variable,"servicegroups") || !strcmp(variable,"servicegroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceescalation->servicegroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceescalation->have_servicegroup_name=TRUE;
		        }
		else if(!strcmp(variable,"hostgroup") || !strcmp(variable,"hostgroups") || !strcmp(variable,"hostgroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceescalation->hostgroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceescalation->have_hostgroup_name=TRUE;
		        }
		else if(!strcmp(variable,"contact_groups")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceescalation->contact_groups=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceescalation->have_contact_groups=TRUE;
		        }
		else if(!strcmp(variable,"escalation_period")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceescalation->escalation_period=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceescalation->have_escalation_period=TRUE;
		        }
		else if(!strcmp(variable,"first_notification")){
			temp_serviceescalation->first_notification=atoi(value);
			temp_serviceescalation->have_first_notification=TRUE;
		        }
		else if(!strcmp(variable,"last_notification")){
			temp_serviceescalation->last_notification=atoi(value);
			temp_serviceescalation->have_last_notification=TRUE;
		        }
		else if(!strcmp(variable,"notification_interval")){
			temp_serviceescalation->notification_interval=atoi(value);
			temp_serviceescalation->have_notification_interval=TRUE;
		        }
		else if(!strcmp(variable,"escalation_options")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"w") || !strcmp(temp_ptr,"warning"))
					temp_serviceescalation->escalate_on_warning=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unknown"))
					temp_serviceescalation->escalate_on_unknown=TRUE;
				else if(!strcmp(temp_ptr,"c") || !strcmp(temp_ptr,"critical"))
					temp_serviceescalation->escalate_on_critical=TRUE;
				else if(!strcmp(temp_ptr,"r") || !strcmp(temp_ptr,"recovery"))
					temp_serviceescalation->escalate_on_recovery=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_serviceescalation->escalate_on_warning=FALSE;
					temp_serviceescalation->escalate_on_unknown=FALSE;
					temp_serviceescalation->escalate_on_critical=FALSE;
					temp_serviceescalation->escalate_on_recovery=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid escalation option '%s' in serviceescalation definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_serviceescalation->have_escalation_options=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_serviceescalation->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid serviceescalation object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		break;
	

	case XODTEMPLATE_CONTACT:

		temp_contact=(xodtemplate_contact *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_contact->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_contact(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for contact '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_contact->_config_file),temp_contact->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_contact->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"contact_name")){
			if((temp_contact->contact_name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"alias")){
			if((temp_contact->alias=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"contact_groups") || !strcmp(variable,"contactgroups")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_contact->contact_groups=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_contact->have_contact_groups=TRUE;
		        }
		else if(!strcmp(variable,"email")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_contact->email=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_contact->have_email=TRUE;
		        }
		else if(!strcmp(variable,"pager")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_contact->pager=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_contact->have_pager=TRUE;
		        }
		else if(strstr(variable,"address")==variable){
			x=atoi(variable+7);
			if(x<1 || x>MAX_XODTEMPLATE_CONTACT_ADDRESSES)
				result=ERROR;
			else if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_contact->address[x-1]=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			if(result==OK)
				temp_contact->have_address[x-1]=TRUE;
		        }
		else if(!strcmp(variable,"host_notification_period")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_contact->host_notification_period=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_contact->have_host_notification_period=TRUE;
		        }
		else if(!strcmp(variable,"host_notification_commands")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_contact->host_notification_commands=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_contact->have_host_notification_commands=TRUE;
		        }
		else if(!strcmp(variable,"service_notification_period")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_contact->service_notification_period=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_contact->have_service_notification_period=TRUE;
		        }
		else if(!strcmp(variable,"service_notification_commands")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_contact->service_notification_commands=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_contact->have_service_notification_commands=TRUE;
		        }
		else if(!strcmp(variable,"host_notification_options")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"d") || !strcmp(temp_ptr,"down"))
					temp_contact->notify_on_host_down=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unreachable"))
					temp_contact->notify_on_host_unreachable=TRUE;
				else if(!strcmp(temp_ptr,"r") || !strcmp(temp_ptr,"recovery"))
					temp_contact->notify_on_host_recovery=TRUE;
				else if(!strcmp(temp_ptr,"f") || !strcmp(temp_ptr,"flapping"))
					temp_contact->notify_on_host_flapping=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_contact->notify_on_host_down=FALSE;
					temp_contact->notify_on_host_unreachable=FALSE;
					temp_contact->notify_on_host_recovery=FALSE;
					temp_contact->notify_on_host_flapping=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid host notification option '%s' in contact definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_contact->have_host_notification_options=TRUE;
		        }
		else if(!strcmp(variable,"service_notification_options")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unknown"))
					temp_contact->notify_on_service_unknown=TRUE;
				else if(!strcmp(temp_ptr,"w") || !strcmp(temp_ptr,"warning"))
					temp_contact->notify_on_service_warning=TRUE;
				else if(!strcmp(temp_ptr,"c") || !strcmp(temp_ptr,"critical"))
					temp_contact->notify_on_service_critical=TRUE;
				else if(!strcmp(temp_ptr,"r") || !strcmp(temp_ptr,"recovery"))
					temp_contact->notify_on_service_recovery=TRUE;
				else if(!strcmp(temp_ptr,"f") || !strcmp(temp_ptr,"flapping"))
					temp_contact->notify_on_service_flapping=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_contact->notify_on_service_unknown=FALSE;
					temp_contact->notify_on_service_warning=FALSE;
					temp_contact->notify_on_service_critical=FALSE;
					temp_contact->notify_on_service_recovery=FALSE;
					temp_contact->notify_on_service_flapping=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid service notification option '%s' in contact definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_contact->have_service_notification_options=TRUE;
		        }
		else if(!strcmp(variable,"host_notifications_enabled")){
			temp_contact->host_notifications_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_contact->have_host_notifications_enabled=TRUE;
		        }
		else if(!strcmp(variable,"service_notifications_enabled")){
			temp_contact->service_notifications_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_contact->have_service_notifications_enabled=TRUE;
		        }
		else if(!strcmp(variable,"can_submit_commands")){
			temp_contact->can_submit_commands=(atoi(value)>0)?TRUE:FALSE;
			temp_contact->have_can_submit_commands=TRUE;
		        }
		else if(!strcmp(variable,"retain_status_information")){
			temp_contact->retain_status_information=(atoi(value)>0)?TRUE:FALSE;
			temp_contact->have_retain_status_information=TRUE;
		        }
		else if(!strcmp(variable,"retain_nonstatus_information")){
			temp_contact->retain_nonstatus_information=(atoi(value)>0)?TRUE:FALSE;
			temp_contact->have_retain_nonstatus_information=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_contact->register_object=(atoi(value)>0)?TRUE:FALSE;
		else if(variable[0]=='_'){

			/* get the variable name */
			customvarname=(char *)strdup(variable+1);

			/* make sure we have a variable name */
			if(customvarname==NULL || !strcmp(customvarname,"")){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Null custom variable name.\n");
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				my_free((void **)&customvarname);
				return ERROR;
			        }

			/* get the variable value */
			if(strcmp(value,XODTEMPLATE_NULL))
				customvarvalue=(char *)strdup(value);
			else
				customvarvalue=NULL;

			/* add the custom variable */
			if(xodtemplate_add_custom_variable_to_contact(temp_contact,customvarname,customvarvalue)==NULL){
#ifdef DEBUG1
				printf("Error: Could not add custom variable '%s' for contact.\n",varname);
#endif
				my_free((void **)&customvarname);
				my_free((void **)&customvarvalue);
				return ERROR;
			        }

			/* free memory */
			my_free((void **)&customvarname);
			my_free((void **)&customvarvalue);
		        }
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid contact object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		break;


	case XODTEMPLATE_HOST:

		temp_host=(xodtemplate_host *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_host->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_host(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for host '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_host->_config_file),temp_host->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_host->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"host_name")){
			if((temp_host->host_name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"display_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_host->display_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_host->have_display_name=TRUE;
		        }
		else if(!strcmp(variable,"alias")){
			if((temp_host->alias=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"address")){
			if((temp_host->address=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"parents")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_host->parents=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_host->have_parents=TRUE;
		        }
		else if(!strcmp(variable,"host_groups") || !strcmp(variable,"hostgroups")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_host->host_groups=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_host->have_host_groups=TRUE;
		        }
		else if(!strcmp(variable,"contact_groups")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_host->contact_groups=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_host->have_contact_groups=TRUE;
		        }
		else if(!strcmp(variable,"notification_period")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_host->notification_period=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_host->have_notification_period=TRUE;
		        }
		else if(!strcmp(variable,"check_command")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_host->check_command=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_host->have_check_command=TRUE;
		        }
		else if(!strcmp(variable,"check_period")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_host->check_period=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_host->have_check_period=TRUE;
		        }
		else if(!strcmp(variable,"event_handler")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_host->event_handler=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_host->have_event_handler=TRUE;
		        }
		else if(!strcmp(variable,"failure_prediction_options")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_host->failure_prediction_options=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_host->have_failure_prediction_options=TRUE;
		        }
		else if(!strcmp(variable,"check_interval") || !strcmp(variable,"normal_check_interval")){
			temp_host->check_interval=atoi(value);
			temp_host->have_check_interval=TRUE;
		        }
		else if(!strcmp(variable,"max_check_attempts")){
			temp_host->max_check_attempts=atoi(value);
			temp_host->have_max_check_attempts=TRUE;
		        }
		else if(!strcmp(variable,"checks_enabled") || !strcmp(variable,"active_checks_enabled")){
			temp_host->active_checks_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_active_checks_enabled=TRUE;
		        }
		else if(!strcmp(variable,"passive_checks_enabled")){
			temp_host->passive_checks_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_passive_checks_enabled=TRUE;
		        }
		else if(!strcmp(variable,"event_handler_enabled")){
			temp_host->event_handler_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_event_handler_enabled=TRUE;
		        }
		else if(!strcmp(variable,"check_freshness")){
			temp_host->check_freshness=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_check_freshness=TRUE;
		        }
		else if(!strcmp(variable,"freshness_threshold")){
			temp_host->freshness_threshold=atoi(value);
			temp_host->have_freshness_threshold=TRUE;
		        }
		else if(!strcmp(variable,"low_flap_threshold")){
			temp_host->low_flap_threshold=strtod(value,NULL);
			temp_host->have_low_flap_threshold=TRUE;
		        }
		else if(!strcmp(variable,"high_flap_threshold")){
			temp_host->high_flap_threshold=strtod(value,NULL);
			temp_host->have_high_flap_threshold=TRUE;
		        }
		else if(!strcmp(variable,"flap_detection_enabled")){
			temp_host->flap_detection_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_flap_detection_enabled=TRUE;
		        }
		else if(!strcmp(variable,"flap_detection_options")){

			/* user is specifying something, so discard defaults... */
			temp_host->flap_detection_on_up=FALSE;
			temp_host->flap_detection_on_down=FALSE;
			temp_host->flap_detection_on_unreachable=FALSE;

			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"o") || !strcmp(temp_ptr,"up"))
					temp_host->flap_detection_on_up=TRUE;
				else if(!strcmp(temp_ptr,"d") || !strcmp(temp_ptr,"down"))
					temp_host->flap_detection_on_down=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unreachable"))
					temp_host->flap_detection_on_unreachable=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_host->flap_detection_on_up=FALSE;
					temp_host->flap_detection_on_down=FALSE;
					temp_host->flap_detection_on_unreachable=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid flap detection option '%s' in host definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					result=ERROR;
				        }
			        }
			temp_host->have_flap_detection_options=TRUE;
		        }
		else if(!strcmp(variable,"notification_options")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"d") || !strcmp(temp_ptr,"down"))
					temp_host->notify_on_down=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unreachable"))
					temp_host->notify_on_unreachable=TRUE;
				else if(!strcmp(temp_ptr,"r") || !strcmp(temp_ptr,"recovery"))
					temp_host->notify_on_recovery=TRUE;
				else if(!strcmp(temp_ptr,"f") || !strcmp(temp_ptr,"flapping"))
					temp_host->notify_on_flapping=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_host->notify_on_down=FALSE;
					temp_host->notify_on_unreachable=FALSE;
					temp_host->notify_on_recovery=FALSE;
					temp_host->notify_on_flapping=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid notification option '%s' in host definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					result=ERROR;
				        }
			        }
			temp_host->have_notification_options=TRUE;
		        }
		else if(!strcmp(variable,"notifications_enabled")){
			temp_host->notifications_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_notifications_enabled=TRUE;
		        }
		else if(!strcmp(variable,"notification_interval")){
			temp_host->notification_interval=atoi(value);
			temp_host->have_notification_interval=TRUE;
		        }
		else if(!strcmp(variable,"first_notification_delay")){
			temp_host->first_notification_delay=atoi(value);
			temp_host->have_first_notification_delay=TRUE;
		        }
		else if(!strcmp(variable,"stalking_options")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"o") || !strcmp(temp_ptr,"up"))
					temp_host->stalk_on_up=TRUE;
				else if(!strcmp(temp_ptr,"d") || !strcmp(temp_ptr,"down"))
					temp_host->stalk_on_down=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unreachable"))
					temp_host->stalk_on_unreachable=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_host->stalk_on_up=FALSE;
					temp_host->stalk_on_down=FALSE;
					temp_host->stalk_on_unreachable=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid stalking option '%s' in host definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					result=ERROR;
				        }
			        }
			temp_host->have_stalking_options=TRUE;
		        }
		else if(!strcmp(variable,"process_perf_data")){
			temp_host->process_perf_data=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_process_perf_data=TRUE;
		        }
		else if(!strcmp(variable,"failure_prediction_enabled")){
			temp_host->failure_prediction_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_failure_prediction_enabled=TRUE;
		        }
		else if(!strcmp(variable,"obsess_over_host")){
			temp_host->obsess_over_host=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_obsess_over_host=TRUE;
		        }
		else if(!strcmp(variable,"retain_status_information")){
			temp_host->retain_status_information=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_retain_status_information=TRUE;
		        }
		else if(!strcmp(variable,"retain_nonstatus_information")){
			temp_host->retain_nonstatus_information=(atoi(value)>0)?TRUE:FALSE;
			temp_host->have_retain_nonstatus_information=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_host->register_object=(atoi(value)>0)?TRUE:FALSE;
		else if(variable[0]=='_'){

			/* get the variable name */
			customvarname=(char *)strdup(variable+1);

			/* make sure we have a variable name */
			if(customvarname==NULL || !strcmp(customvarname,"")){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Null custom variable name.\n");
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				my_free((void **)&customvarname);
				return ERROR;
			        }

			/* get the variable value */
			customvarvalue=NULL;
			if(strcmp(value,XODTEMPLATE_NULL))
				customvarvalue=(char *)strdup(value);

			/* add the custom variable */
			if(xodtemplate_add_custom_variable_to_host(temp_host,customvarname,customvarvalue)==NULL){
#ifdef DEBUG1
				printf("Error: Could not add custom variable '%s' for host.\n",varname);
#endif
				my_free((void **)&customvarname);
				my_free((void **)&customvarvalue);
				return ERROR;
			        }

			/* free memory */
			my_free((void **)&customvarname);
			my_free((void **)&customvarvalue);
		        }
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid host object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		break;

	case XODTEMPLATE_SERVICE:

		temp_service=(xodtemplate_service *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_service->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_service(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for service '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_service->_config_file),temp_service->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_service->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"host") || !strcmp(variable,"hosts") || !strcmp(variable,"host_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->host_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_host_name=TRUE;
		        }
		else if(!strcmp(variable,"service_description") || !strcmp(variable,"description")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->service_description=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_service_description=TRUE;
		        }
		else if(!strcmp(variable,"display_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->display_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_display_name=TRUE;
		        }
		else if(!strcmp(variable,"hostgroup") || !strcmp(variable,"hostgroups") || !strcmp(variable,"hostgroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->hostgroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_hostgroup_name=TRUE;
		        }
		else if(!strcmp(variable,"service_groups") || !strcmp(variable,"servicegroups")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->service_groups=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_service_groups=TRUE;
		        }
		else if(!strcmp(variable,"check_command")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->check_command=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_check_command=TRUE;
		        }
		else if(!strcmp(variable,"check_period")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->check_period=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_check_period=TRUE;
		        }
		else if(!strcmp(variable,"event_handler")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->event_handler=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_event_handler=TRUE;
		        }
		else if(!strcmp(variable,"notification_period")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->notification_period=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_notification_period=TRUE;
		        }
		else if(!strcmp(variable,"contact_groups")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->contact_groups=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_contact_groups=TRUE;
		        }
		else if(!strcmp(variable,"failure_prediction_options")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->failure_prediction_options=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_failure_prediction_options=TRUE;
		        }
		else if(!strcmp(variable,"notes")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->notes=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_notes=TRUE;
		        }
		else if(!strcmp(variable,"notes_url")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->notes_url=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_notes_url=TRUE;
		        }
		else if(!strcmp(variable,"action_url")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->action_url=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_action_url=TRUE;
		        }
		else if(!strcmp(variable,"icon_image")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->icon_image=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_icon_image=TRUE;
		        }
		else if(!strcmp(variable,"icon_image_alt")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_service->icon_image_alt=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_service->have_icon_image_alt=TRUE;
		        }
		else if(!strcmp(variable,"max_check_attempts")){
			temp_service->max_check_attempts=atoi(value);
			temp_service->have_max_check_attempts=TRUE;
		        }
		else if(!strcmp(variable,"normal_check_interval")){
			temp_service->normal_check_interval=atoi(value);
			temp_service->have_normal_check_interval=TRUE;
		        }
		else if(!strcmp(variable,"retry_check_interval")){
			temp_service->retry_check_interval=atoi(value);
			temp_service->have_retry_check_interval=TRUE;
		        }
		else if(!strcmp(variable,"active_checks_enabled")){
			temp_service->active_checks_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_active_checks_enabled=TRUE;
		        }
		else if(!strcmp(variable,"passive_checks_enabled")){
			temp_service->passive_checks_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_passive_checks_enabled=TRUE;
		        }
		else if(!strcmp(variable,"parallelize_check")){
			temp_service->parallelize_check=atoi(value);
			temp_service->have_parallelize_check=TRUE;
		        }
		else if(!strcmp(variable,"is_volatile")){
			temp_service->is_volatile=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_is_volatile=TRUE;
		        }
		else if(!strcmp(variable,"obsess_over_service")){
			temp_service->obsess_over_service=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_obsess_over_service=TRUE;
		        }
		else if(!strcmp(variable,"event_handler_enabled")){
			temp_service->event_handler_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_event_handler_enabled=TRUE;
		        }
		else if(!strcmp(variable,"check_freshness")){
			temp_service->check_freshness=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_check_freshness=TRUE;
		        }
		else if(!strcmp(variable,"freshness_threshold")){
			temp_service->freshness_threshold=atoi(value);
			temp_service->have_freshness_threshold=TRUE;
		        }
		else if(!strcmp(variable,"low_flap_threshold")){
			temp_service->low_flap_threshold=strtod(value,NULL);
			temp_service->have_low_flap_threshold=TRUE;
		        }
		else if(!strcmp(variable,"high_flap_threshold")){
			temp_service->high_flap_threshold=strtod(value,NULL);
			temp_service->have_high_flap_threshold=TRUE;
		        }
		else if(!strcmp(variable,"flap_detection_enabled")){
			temp_service->flap_detection_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_flap_detection_enabled=TRUE;
		        }	
		else if(!strcmp(variable,"flap_detection_options")){

			/* user is specifying something, so discard defaults... */
			temp_service->flap_detection_on_ok=FALSE;
			temp_service->flap_detection_on_warning=FALSE;
			temp_service->flap_detection_on_unknown=FALSE;
			temp_service->flap_detection_on_critical=FALSE;

			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"o") || !strcmp(temp_ptr,"ok"))
					temp_service->flap_detection_on_ok=TRUE;
				else if(!strcmp(temp_ptr,"w") || !strcmp(temp_ptr,"warning"))
					temp_service->flap_detection_on_warning=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unknown"))
					temp_service->flap_detection_on_unknown=TRUE;
				else if(!strcmp(temp_ptr,"c") || !strcmp(temp_ptr,"critical"))
					temp_service->flap_detection_on_critical=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_service->flap_detection_on_ok=FALSE;
					temp_service->flap_detection_on_warning=FALSE;
					temp_service->flap_detection_on_unknown=FALSE;
					temp_service->flap_detection_on_critical=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid flap detection option '%s' in service definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_service->have_flap_detection_options=TRUE;
		        }
		else if(!strcmp(variable,"notification_options")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unknown"))
					temp_service->notify_on_unknown=TRUE;
				else if(!strcmp(temp_ptr,"w") || !strcmp(temp_ptr,"warning"))
					temp_service->notify_on_warning=TRUE;
				else if(!strcmp(temp_ptr,"c") || !strcmp(temp_ptr,"critical"))
					temp_service->notify_on_critical=TRUE;
				else if(!strcmp(temp_ptr,"r") || !strcmp(temp_ptr,"recovery"))
					temp_service->notify_on_recovery=TRUE;
				else if(!strcmp(temp_ptr,"f") || !strcmp(temp_ptr,"flapping"))
					temp_service->notify_on_flapping=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_service->notify_on_unknown=FALSE;
					temp_service->notify_on_warning=FALSE;
					temp_service->notify_on_critical=FALSE;
					temp_service->notify_on_recovery=FALSE;
					temp_service->notify_on_flapping=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid notification option '%s' in service definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_service->have_notification_options=TRUE;
		        }
		else if(!strcmp(variable,"notifications_enabled")){
			temp_service->notifications_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_notifications_enabled=TRUE;
		        }
		else if(!strcmp(variable,"notification_interval")){
			temp_service->notification_interval=atoi(value);
			temp_service->have_notification_interval=TRUE;
		        }
		else if(!strcmp(variable,"first_notification_delay")){
			temp_service->first_notification_delay=atoi(value);
			temp_service->have_first_notification_delay=TRUE;
		        }
		else if(!strcmp(variable,"stalking_options")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"o") || !strcmp(temp_ptr,"ok"))
					temp_service->stalk_on_ok=TRUE;
				else if(!strcmp(temp_ptr,"w") || !strcmp(temp_ptr,"warning"))
					temp_service->stalk_on_warning=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unknown"))
					temp_service->stalk_on_unknown=TRUE;
				else if(!strcmp(temp_ptr,"c") || !strcmp(temp_ptr,"critical"))
					temp_service->stalk_on_critical=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_service->stalk_on_ok=FALSE;
					temp_service->stalk_on_warning=FALSE;
					temp_service->stalk_on_unknown=FALSE;
					temp_service->stalk_on_critical=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid stalking option '%s' in service definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_service->have_stalking_options=TRUE;
		        }
		else if(!strcmp(variable,"process_perf_data")){
			temp_service->process_perf_data=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_process_perf_data=TRUE;
		        }
		else if(!strcmp(variable,"failure_prediction_enabled")){
			temp_service->failure_prediction_enabled=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_failure_prediction_enabled=TRUE;
		        }
		else if(!strcmp(variable,"retain_status_information")){
			temp_service->retain_status_information=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_retain_status_information=TRUE;
		        }
		else if(!strcmp(variable,"retain_nonstatus_information")){
			temp_service->retain_nonstatus_information=(atoi(value)>0)?TRUE:FALSE;
			temp_service->have_retain_nonstatus_information=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_service->register_object=(atoi(value)>0)?TRUE:FALSE;
		else if(variable[0]=='_'){

			/* get the variable name */
			customvarname=(char *)strdup(variable+1);

			/* make sure we have a variable name */
			if(customvarname==NULL || !strcmp(customvarname,"")){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Null custom variable name.\n");
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				my_free((void **)&customvarname);
				return ERROR;
			        }

			/* get the variable value */
			if(strcmp(value,XODTEMPLATE_NULL))
				customvarvalue=(char *)strdup(value);
			else
				customvarvalue=NULL;

			/* add the custom variable */
			if(xodtemplate_add_custom_variable_to_service(temp_service,customvarname,customvarvalue)==NULL){
#ifdef DEBUG1
				printf("Error: Could not add custom variable '%s' for service.\n",varname);
#endif
				my_free((void **)&customvarname);
				my_free((void **)&customvarvalue);
				return ERROR;
			        }

			/* free memory */
			my_free((void **)&customvarname);
			my_free((void **)&customvarvalue);
		        }
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid service object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		break;

	case XODTEMPLATE_HOSTDEPENDENCY:

		temp_hostdependency=(xodtemplate_hostdependency *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_hostdependency->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_hostdependency(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for host dependency '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_hostdependency->_config_file),temp_hostdependency->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_hostdependency->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"hostgroup") || !strcmp(variable,"hostgroups") || !strcmp(variable,"hostgroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostdependency->hostgroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostdependency->have_hostgroup_name=TRUE;
		        }
		else if(!strcmp(variable,"host") || !strcmp(variable,"host_name") || !strcmp(variable,"master_host") || !strcmp(variable,"master_host_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostdependency->host_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostdependency->have_host_name=TRUE;
		        }
		else if(!strcmp(variable,"dependent_hostgroup") || !strcmp(variable,"dependent_hostgroups") || !strcmp(variable,"dependent_hostgroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostdependency->dependent_hostgroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostdependency->have_dependent_hostgroup_name=TRUE;
		        }
		else if(!strcmp(variable,"dependent_host") || !strcmp(variable,"dependent_host_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostdependency->dependent_host_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostdependency->have_dependent_host_name=TRUE;
		        }
		else if(!strcmp(variable,"inherits_parent")){
			temp_hostdependency->inherits_parent=(atoi(value)>0)?TRUE:FALSE;
			temp_hostdependency->have_inherits_parent=TRUE;
		        }
		else if(!strcmp(variable,"notification_failure_options") || !strcmp(variable,"notification_failure_criteria")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"o") || !strcmp(temp_ptr,"up"))
					temp_hostdependency->fail_notify_on_up=TRUE;
				else if(!strcmp(temp_ptr,"d") || !strcmp(temp_ptr,"down"))
					temp_hostdependency->fail_notify_on_down=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unreachable"))
					temp_hostdependency->fail_notify_on_unreachable=TRUE;
				else if(!strcmp(temp_ptr,"p") || !strcmp(temp_ptr,"pending"))
					temp_hostdependency->fail_notify_on_pending=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_hostdependency->fail_notify_on_up=FALSE;
					temp_hostdependency->fail_notify_on_down=FALSE;
					temp_hostdependency->fail_notify_on_unreachable=FALSE;
					temp_hostdependency->fail_notify_on_pending=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid notification dependency option '%s' in hostdependency definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_hostdependency->have_notification_dependency_options=TRUE;
		        }
		else if(!strcmp(variable,"execution_failure_options") || !strcmp(variable,"execution_failure_criteria")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"o") || !strcmp(temp_ptr,"up"))
					temp_hostdependency->fail_execute_on_up=TRUE;
				else if(!strcmp(temp_ptr,"d") || !strcmp(temp_ptr,"down"))
					temp_hostdependency->fail_execute_on_down=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unreachable"))
					temp_hostdependency->fail_execute_on_unreachable=TRUE;
				else if(!strcmp(temp_ptr,"p") || !strcmp(temp_ptr,"pending"))
					temp_hostdependency->fail_execute_on_pending=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_hostdependency->fail_execute_on_up=FALSE;
					temp_hostdependency->fail_execute_on_down=FALSE;
					temp_hostdependency->fail_execute_on_unreachable=FALSE;
					temp_hostdependency->fail_execute_on_pending=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid execution dependency option '%s' in hostdependency definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_hostdependency->have_execution_dependency_options=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_hostdependency->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid hostdependency object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
#endif
			return ERROR;
		        }

		break;

	
	case XODTEMPLATE_HOSTESCALATION:

		temp_hostescalation=(xodtemplate_hostescalation *)xodtemplate_current_object;

		if(!strcmp(variable,"use")){
			if((temp_hostescalation->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_hostescalation(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for host escalation '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_hostescalation->_config_file),temp_hostescalation->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_hostescalation->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"hostgroup") || !strcmp(variable,"hostgroups") || !strcmp(variable,"hostgroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostescalation->hostgroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostescalation->have_hostgroup_name=TRUE;
		        }
		else if(!strcmp(variable,"host") || !strcmp(variable,"host_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostescalation->host_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostescalation->have_host_name=TRUE;
		        }
		else if(!strcmp(variable,"contact_groups")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostescalation->contact_groups=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostescalation->have_contact_groups=TRUE;
		        }
		else if(!strcmp(variable,"escalation_period")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostescalation->escalation_period=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostescalation->have_escalation_period=TRUE;
		        }
		else if(!strcmp(variable,"first_notification")){
			temp_hostescalation->first_notification=atoi(value);
			temp_hostescalation->have_first_notification=TRUE;
		        }
		else if(!strcmp(variable,"last_notification")){
			temp_hostescalation->last_notification=atoi(value);
			temp_hostescalation->have_last_notification=TRUE;
		        }
		else if(!strcmp(variable,"notification_interval")){
			temp_hostescalation->notification_interval=atoi(value);
			temp_hostescalation->have_notification_interval=TRUE;
		        }
		else if(!strcmp(variable,"escalation_options")){
			for(temp_ptr=strtok(value,", ");temp_ptr;temp_ptr=strtok(NULL,", ")){
				if(!strcmp(temp_ptr,"d") || !strcmp(temp_ptr,"down"))
					temp_hostescalation->escalate_on_down=TRUE;
				else if(!strcmp(temp_ptr,"u") || !strcmp(temp_ptr,"unreachable"))
					temp_hostescalation->escalate_on_unreachable=TRUE;
				else if(!strcmp(temp_ptr,"r") || !strcmp(temp_ptr,"recovery"))
					temp_hostescalation->escalate_on_recovery=TRUE;
				else if(!strcmp(temp_ptr,"n") || !strcmp(temp_ptr,"none")){
					temp_hostescalation->escalate_on_down=FALSE;
					temp_hostescalation->escalate_on_unreachable=FALSE;
					temp_hostescalation->escalate_on_recovery=FALSE;
				        }
				else{
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Invalid escalation option '%s' in hostescalation definition.\n",temp_ptr);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					return ERROR;
				        }
			        }
			temp_hostescalation->have_escalation_options=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_hostescalation->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid hostescalation object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
#endif
			return ERROR;
		        }

		break;
	
	case XODTEMPLATE_HOSTEXTINFO:
		
		temp_hostextinfo=xodtemplate_hostextinfo_list;

		if(!strcmp(variable,"use")){
			if((temp_hostextinfo->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_hostextinfo(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for extended host info '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_hostextinfo->_config_file),temp_hostextinfo->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_hostextinfo->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"host_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostextinfo->host_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostextinfo->have_host_name=TRUE;
		        }
		else if(!strcmp(variable,"hostgroup") || !strcmp(variable,"hostgroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostextinfo->hostgroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostextinfo->have_hostgroup_name=TRUE;
		        }
		else if(!strcmp(variable,"notes")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostextinfo->notes=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostextinfo->have_notes=TRUE;
		        }
		else if(!strcmp(variable,"notes_url")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostextinfo->notes_url=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostextinfo->have_notes_url=TRUE;
		        }
		else if(!strcmp(variable,"action_url")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostextinfo->action_url=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostextinfo->have_action_url=TRUE;
		        }
		else if(!strcmp(variable,"icon_image")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostextinfo->icon_image=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostextinfo->have_icon_image=TRUE;
		        }
		else if(!strcmp(variable,"icon_image_alt")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostextinfo->icon_image_alt=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostextinfo->have_icon_image_alt=TRUE;
		        }
		else if(!strcmp(variable,"vrml_image")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostextinfo->vrml_image=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostextinfo->have_vrml_image=TRUE;
		        }
		else if(!strcmp(variable,"gd2_image")|| !strcmp(variable,"statusmap_image")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_hostextinfo->statusmap_image=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_hostextinfo->have_statusmap_image=TRUE;
		        }
		else if(!strcmp(variable,"2d_coords")){
			temp_ptr=strtok(value,", ");
			if(temp_ptr==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Invalid 2d_coords value '%s' in extended host info definition.\n",temp_ptr);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
			temp_hostextinfo->x_2d=atoi(temp_ptr);
			temp_ptr=strtok(NULL,", ");
			if(temp_ptr==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Invalid 2d_coords value '%s' in extended host info definition.\n",temp_ptr);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
			temp_hostextinfo->y_2d=atoi(temp_ptr);
			temp_hostextinfo->have_2d_coords=TRUE;
		        }
		else if(!strcmp(variable,"3d_coords")){
			temp_ptr=strtok(value,", ");
			if(temp_ptr==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Invalid 3d_coords value '%s' in extended host info definition.\n",temp_ptr);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
			temp_hostextinfo->x_3d=strtod(temp_ptr,NULL);
			temp_ptr=strtok(NULL,", ");
			if(temp_ptr==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Invalid 3d_coords value '%s' in extended host info definition.\n",temp_ptr);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
			temp_hostextinfo->y_3d=strtod(temp_ptr,NULL);
			temp_ptr=strtok(NULL,", ");
			if(temp_ptr==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Invalid 3d_coords value '%s' in extended host info definition.\n",temp_ptr);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
			temp_hostextinfo->z_3d=strtod(temp_ptr,NULL);
			temp_hostextinfo->have_3d_coords=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_hostextinfo->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid hostextinfo object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		break;
	
	case XODTEMPLATE_SERVICEEXTINFO:
		
		temp_serviceextinfo=xodtemplate_serviceextinfo_list;

		if(!strcmp(variable,"use")){
			if((temp_serviceextinfo->template=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"name")){

#ifdef NSCORE
			/* check for duplicates */
			if(xodtemplate_find_serviceextinfo(value)!=NULL){
				asprintf(&temp_buffer,"Warning: Duplicate definition found for extended service info '%s' (config file '%s', starting on line %d)\n",value,xodtemplate_config_file_name(temp_serviceextinfo->_config_file),temp_serviceextinfo->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
				my_free((void **)&temp_buffer);
			        }
#endif

			if((temp_serviceextinfo->name=(char *)strdup(value))==NULL)
				result=ERROR;
		        }
		else if(!strcmp(variable,"host_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceextinfo->host_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceextinfo->have_host_name=TRUE;
		        }
		else if(!strcmp(variable,"hostgroup") || !strcmp(variable,"hostgroup_name")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceextinfo->hostgroup_name=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceextinfo->have_hostgroup_name=TRUE;
		        }
		else if(!strcmp(variable,"service_description")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceextinfo->service_description=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceextinfo->have_service_description=TRUE;
		        }
		else if(!strcmp(variable,"notes")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceextinfo->notes=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceextinfo->have_notes=TRUE;
		        }
		else if(!strcmp(variable,"notes_url")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceextinfo->notes_url=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceextinfo->have_notes_url=TRUE;
		        }
		else if(!strcmp(variable,"action_url")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceextinfo->action_url=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceextinfo->have_action_url=TRUE;
		        }
		else if(!strcmp(variable,"icon_image")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceextinfo->icon_image=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceextinfo->have_icon_image=TRUE;
		        }
		else if(!strcmp(variable,"icon_image_alt")){
			if(strcmp(value,XODTEMPLATE_NULL)){
				if((temp_serviceextinfo->icon_image_alt=(char *)strdup(value))==NULL)
					result=ERROR;
			        }
			temp_serviceextinfo->have_icon_image_alt=TRUE;
		        }
		else if(!strcmp(variable,"register"))
			temp_serviceextinfo->register_object=(atoi(value)>0)?TRUE:FALSE;
		else{
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Invalid serviceextinfo object directive '%s'.\n",variable);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		break;

	default:
		return ERROR;
		break;
	        }

	/* free memory */
	my_free((void **)&variable);
	my_free((void **)&value);

#ifdef DEBUG0
	printf("xodtemplate_add_object_property() end\n");
#endif

	return result;
        }



/* completes an object definition */
int xodtemplate_end_object_definition(int options){
	int result=OK;

#ifdef DEBUG0
	printf("xodtemplate_end_object_definition() start\n");
#endif

	xodtemplate_current_object=NULL;
	xodtemplate_current_object_type=XODTEMPLATE_NONE;

#ifdef DEBUG0
	printf("xodtemplate_end_object_definition() end\n");
#endif

	return result;
        }



/* adds a custom variable to a host */
xodtemplate_customvariablesmember *xodtemplate_add_custom_variable_to_host(xodtemplate_host *hst, char *varname, char *varvalue){

	return xodtemplate_add_custom_variable_to_object(&hst->custom_variables,varname,varvalue);
        }



/* adds a custom variable to a service */
xodtemplate_customvariablesmember *xodtemplate_add_custom_variable_to_service(xodtemplate_service *svc, char *varname, char *varvalue){

	return xodtemplate_add_custom_variable_to_object(&svc->custom_variables,varname,varvalue);
        }



/* adds a custom variable to a contact */
xodtemplate_customvariablesmember *xodtemplate_add_custom_variable_to_contact(xodtemplate_contact *cntct, char *varname, char *varvalue){

	return xodtemplate_add_custom_variable_to_object(&cntct->custom_variables,varname,varvalue);
        }



/* adds a custom variable to an object */
xodtemplate_customvariablesmember *xodtemplate_add_custom_variable_to_object(xodtemplate_customvariablesmember **object_ptr, char *varname, char *varvalue){
	xodtemplate_customvariablesmember *new_customvariablesmember=NULL;
	register int x=0;

	/* make sure we have the data we need */
	if(object_ptr==NULL)
		return NULL;

	if(varname==NULL || !strcmp(varname,""))
		return NULL;

	/* allocate memory for a new member */
	if((new_customvariablesmember=malloc(sizeof(xodtemplate_customvariablesmember)))==NULL)
		return NULL;
	if((new_customvariablesmember->variable_name=(char *)strdup(varname))==NULL){
		my_free((void **)&new_customvariablesmember);
		return NULL;
	        }
	if(varvalue){
		if((new_customvariablesmember->variable_value=(char *)strdup(varvalue))==NULL){
			my_free((void **)&new_customvariablesmember->variable_name);
			my_free((void **)&new_customvariablesmember);
			return NULL;
	                }
	        }
	else
		new_customvariablesmember->variable_value=NULL;

	/* convert varname to all uppercase (saves CPU time during macro functions) */
	for(x=0;new_customvariablesmember->variable_name[x]!='\x0';x++)
		new_customvariablesmember->variable_name[x]=toupper(new_customvariablesmember->variable_name[x]);
	
	/* add the new member to the head of the member list */
	new_customvariablesmember->next=*object_ptr;
	*object_ptr=new_customvariablesmember;

	return new_customvariablesmember;
        }




/******************************************************************/
/***************** OBJECT DUPLICATION FUNCTIONS *******************/
/******************************************************************/

#ifdef NSCORE


/* duplicates service definitions */
int xodtemplate_duplicate_services(void){
	int result=OK;
	xodtemplate_service *temp_service=NULL;
	xodtemplate_hostlist *temp_hostlist=NULL;
	xodtemplate_hostlist *this_hostlist=NULL;
	char *host_name=NULL;
	int first_item=FALSE;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_duplicate_services() start\n");
#endif


	/****** DUPLICATE SERVICE DEFINITIONS WITH ONE OR MORE HOSTGROUP AND/OR HOST NAMES ******/
	for(temp_service=xodtemplate_service_list;temp_service!=NULL;temp_service=temp_service->next){

		/* skip service definitions without enough data */
		if(temp_service->hostgroup_name==NULL && temp_service->host_name==NULL)
			continue;

		/* skip services that shouldn't be registered */
		if(temp_service->register_object==FALSE)
			continue;

		/* get list of hosts */
		temp_hostlist=xodtemplate_expand_hostgroups_and_hosts(temp_service->hostgroup_name,temp_service->host_name);
		if(temp_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand hostgroups and/or hosts specified in service (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_service->_config_file),temp_service->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* add a copy of the service for every host in the hostgroup/host name list */
		first_item=TRUE;
		for(this_hostlist=temp_hostlist;this_hostlist!=NULL;this_hostlist=this_hostlist->next){

			/* if this is the first duplication, use the existing entry */
			if(first_item==TRUE){

				my_free((void **)&temp_service->host_name);
				temp_service->host_name=(char *)strdup(this_hostlist->host_name);
				if(temp_service->host_name==NULL){
					xodtemplate_free_hostlist(temp_hostlist);
					return ERROR;
				        }

				first_item=FALSE;
				continue;
			        }

			/* duplicate service definition */
			result=xodtemplate_duplicate_service(temp_service,this_hostlist->host_name);

			/* exit on error */
			if(result==ERROR){
				my_free((void **)&host_name);
				return ERROR;
		                }
		        }

		/* free memory we used for host list */
		xodtemplate_free_hostlist(temp_hostlist);
	        }

#ifdef DEBUG0
	printf("xodtemplate_duplicate_services() end\n");
#endif

	return OK;
        }



/* duplicates object definitions */
int xodtemplate_duplicate_objects(void){
	int result=OK;
	xodtemplate_hostescalation *temp_hostescalation=NULL;
	xodtemplate_serviceescalation *temp_serviceescalation=NULL;
	xodtemplate_hostdependency *temp_hostdependency=NULL;
	xodtemplate_servicedependency *temp_servicedependency=NULL;
	xodtemplate_hostlist *temp_hostlist=NULL;
	xodtemplate_hostlist *this_hostlist=NULL;
	xodtemplate_servicelist *temp_servicelist=NULL;
	xodtemplate_servicelist *this_servicelist=NULL;
	xodtemplate_hostlist *master_hostlist=NULL;
	xodtemplate_hostlist *dependent_hostlist=NULL;
	xodtemplate_hostextinfo *temp_hostextinfo=NULL;
	xodtemplate_serviceextinfo *temp_serviceextinfo=NULL;
	char *host_name=NULL;
	int first_item=FALSE;
#ifdef NSCORE
	char temp_buffer[MAX_XODTEMPLATE_INPUT_BUFFER];
#endif

#ifdef DEBUG0
	printf("xodtemplate_duplicate_objects() start\n");
#endif


	/*************************************/
	/* SERVICES ARE DUPLICATED ELSEWHERE */
	/*************************************/


	/****** DUPLICATE HOST ESCALATION DEFINITIONS WITH ONE OR MORE HOSTGROUP AND/OR HOST NAMES ******/
	for(temp_hostescalation=xodtemplate_hostescalation_list;temp_hostescalation!=NULL;temp_hostescalation=temp_hostescalation->next){

		/* skip host escalation definitions without enough data */
		if(temp_hostescalation->hostgroup_name==NULL && temp_hostescalation->host_name==NULL)
			continue;

		/* get list of hosts */
		temp_hostlist=xodtemplate_expand_hostgroups_and_hosts(temp_hostescalation->hostgroup_name,temp_hostescalation->host_name);
		if(temp_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand hostgroups and/or hosts specified in host escalation (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_hostescalation->_config_file),temp_hostescalation->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* add a copy of the hostescalation for every host in the hostgroup/host name list */
		first_item=TRUE;
		for(this_hostlist=temp_hostlist;this_hostlist!=NULL;this_hostlist=this_hostlist->next){

			/* if this is the first duplication, use the existing entry */
			if(first_item==TRUE){

				my_free((void **)&temp_hostescalation->host_name);
				temp_hostescalation->host_name=(char *)strdup(this_hostlist->host_name);
				if(temp_hostescalation->host_name==NULL){
					xodtemplate_free_hostlist(temp_hostlist);
					return ERROR;
				        }

				first_item=FALSE;
				continue;
			        }

			/* duplicate hostescalation definition */
			result=xodtemplate_duplicate_hostescalation(temp_hostescalation,this_hostlist->host_name);

			/* exit on error */
			if(result==ERROR){
				my_free((void **)&host_name);
				return ERROR;
		                }
		        }

		/* free memory we used for host list */
		xodtemplate_free_hostlist(temp_hostlist);
	        }

	
	/****** DUPLICATE SERVICE ESCALATION DEFINITIONS WITH ONE OR MORE HOSTGROUP AND/OR HOST NAMES ******/
	for(temp_serviceescalation=xodtemplate_serviceescalation_list;temp_serviceescalation!=NULL;temp_serviceescalation=temp_serviceescalation->next){

		/* skip service escalation definitions without enough data */
		if(temp_serviceescalation->hostgroup_name==NULL && temp_serviceescalation->host_name==NULL)
			continue;

		/* get list of hosts */
		temp_hostlist=xodtemplate_expand_hostgroups_and_hosts(temp_serviceescalation->hostgroup_name,temp_serviceescalation->host_name);
		if(temp_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand hostgroups and/or hosts specified in service escalation (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_serviceescalation->_config_file),temp_serviceescalation->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* duplicate service escalation entries */
		first_item=TRUE;
		for(this_hostlist=temp_hostlist;this_hostlist!=NULL;this_hostlist=this_hostlist->next){

			/* if this is the first duplication,use the existing entry */
			if(first_item==TRUE){

				my_free((void **)&temp_serviceescalation->host_name);
				temp_serviceescalation->host_name=(char *)strdup(this_hostlist->host_name);
				if(temp_serviceescalation->host_name==NULL){
					xodtemplate_free_hostlist(temp_hostlist);
					return ERROR;
				        }

				first_item=FALSE;
				continue;
			        }

			/* duplicate service escalation definition */
			result=xodtemplate_duplicate_serviceescalation(temp_serviceescalation,this_hostlist->host_name,NULL);

			/* exit on error */
			if(result==ERROR){
				xodtemplate_free_hostlist(temp_hostlist);
				return ERROR;
		                }
		        }

		/* free memory we used for host list */
		xodtemplate_free_hostlist(temp_hostlist);
	        }


	/****** DUPLICATE SERVICE ESCALATION DEFINITIONS WITH MULTIPLE DESCRIPTIONS ******/
	/* THIS MUST BE DONE AFTER DUPLICATING FOR MULTIPLE HOST NAMES (SEE ABOVE) */
	for(temp_serviceescalation=xodtemplate_serviceescalation_list;temp_serviceescalation!=NULL;temp_serviceescalation=temp_serviceescalation->next){

		/* skip serviceescalations without enough data */
		if(temp_serviceescalation->service_description==NULL || temp_serviceescalation->host_name==NULL)
			continue;

		/* get list of services */
		temp_servicelist=xodtemplate_expand_servicegroups_and_services(NULL,temp_serviceescalation->host_name,temp_serviceescalation->service_description);
		if(temp_servicelist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand services specified in service escalation (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_serviceescalation->_config_file),temp_serviceescalation->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* duplicate service escalation entries */
		first_item=TRUE;
		for(this_servicelist=temp_servicelist;this_servicelist!=NULL;this_servicelist=this_servicelist->next){

			/* if this is the first duplication, use the existing entry */
			if(first_item==TRUE){

				my_free((void **)&temp_serviceescalation->service_description);
				temp_serviceescalation->service_description=(char *)strdup(this_servicelist->service_description);
				if(temp_serviceescalation->service_description==NULL){
					xodtemplate_free_servicelist(temp_servicelist);
					return ERROR;
				        }

				first_item=FALSE;
				continue;
			        }

			/* duplicate service escalation definition */
			result=xodtemplate_duplicate_serviceescalation(temp_serviceescalation,temp_serviceescalation->host_name,this_servicelist->service_description);

			/* exit on error */
			if(result==ERROR){
				xodtemplate_free_servicelist(temp_servicelist);
				return ERROR;
		                }
		        }

		/* free memory we used for service list */
		xodtemplate_free_servicelist(temp_servicelist);
	        }



	/****** DUPLICATE SERVICE ESCALATION DEFINITIONS WITH SERVICEGROUPS ******/
	/* THIS MUST BE DONE AFTER DUPLICATING FOR MULTIPLE HOST NAMES (SEE ABOVE) */
	for(temp_serviceescalation=xodtemplate_serviceescalation_list;temp_serviceescalation!=NULL;temp_serviceescalation=temp_serviceescalation->next){

		/* skip serviceescalations without enough data */
		if(temp_serviceescalation->servicegroup_name==NULL)
			continue;

		/* get list of services */
		temp_servicelist=xodtemplate_expand_servicegroups_and_services(temp_serviceescalation->servicegroup_name,NULL,NULL);
		if(temp_servicelist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand servicegroups specified in service escalation (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_serviceescalation->_config_file),temp_serviceescalation->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* duplicate service escalation entries */
		first_item=TRUE;
		for(this_servicelist=temp_servicelist;this_servicelist!=NULL;this_servicelist=this_servicelist->next){

			/* if this is the first duplication, use the existing entry if possible */
			if(first_item==TRUE && temp_serviceescalation->host_name==NULL && temp_serviceescalation->service_description==NULL){

				my_free((void **)&temp_serviceescalation->host_name);
				temp_serviceescalation->host_name=(char *)strdup(this_servicelist->host_name);
				my_free((void **)&temp_serviceescalation->service_description);
				temp_serviceescalation->service_description=(char *)strdup(this_servicelist->service_description);
				if(temp_serviceescalation->service_description==NULL){
					xodtemplate_free_servicelist(temp_servicelist);
					return ERROR;
				        }

				first_item=FALSE;
				continue;
			        }

			/* duplicate service escalation definition */
			result=xodtemplate_duplicate_serviceescalation(temp_serviceescalation,this_servicelist->host_name,this_servicelist->service_description);

			/* exit on error */
			if(result==ERROR){
				xodtemplate_free_servicelist(temp_servicelist);
				return ERROR;
		                }
		        }

		/* free memory we used for service list */
		xodtemplate_free_servicelist(temp_servicelist);
	        }


	/****** DUPLICATE HOST DEPENDENCY DEFINITIONS WITH MULTIPLE HOSTGROUP AND/OR HOST NAMES (MASTER AND DEPENDENT) ******/
	for(temp_hostdependency=xodtemplate_hostdependency_list;temp_hostdependency!=NULL;temp_hostdependency=temp_hostdependency->next){
		
		/* skip host dependencies without enough data */
		if(temp_hostdependency->hostgroup_name==NULL && temp_hostdependency->dependent_hostgroup_name==NULL && temp_hostdependency->host_name==NULL && temp_hostdependency->dependent_host_name==NULL)
			continue;

		/* get list of master host names */
		master_hostlist=xodtemplate_expand_hostgroups_and_hosts(temp_hostdependency->hostgroup_name,temp_hostdependency->host_name);
		if(master_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand master hostgroups and/or hosts specified in host dependency (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_hostdependency->_config_file),temp_hostdependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* get list of dependent host names */
		dependent_hostlist=xodtemplate_expand_hostgroups_and_hosts(temp_hostdependency->dependent_hostgroup_name,temp_hostdependency->dependent_host_name);
		if(dependent_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand dependent hostgroups and/or hosts specified in host dependency (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_hostdependency->_config_file),temp_hostdependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* duplicate the dependency definitions */
		first_item=TRUE;
		for(temp_hostlist=master_hostlist;temp_hostlist!=NULL;temp_hostlist=temp_hostlist->next){

			for(this_hostlist=dependent_hostlist;this_hostlist!=NULL;this_hostlist=this_hostlist->next){

				/* existing definition gets first names */
				if(first_item==TRUE){
					my_free((void **)&temp_hostdependency->host_name);
					my_free((void **)&temp_hostdependency->dependent_host_name);
					temp_hostdependency->host_name=(char *)strdup(temp_hostlist->host_name);
					temp_hostdependency->dependent_host_name=(char *)strdup(this_hostlist->host_name);
					first_item=FALSE;
					continue;
				        }
				else
					result=xodtemplate_duplicate_hostdependency(temp_hostdependency,temp_hostlist->host_name,this_hostlist->host_name);

				/* exit on error */
				if(result==ERROR)
					return ERROR;
			        }
		        }

		/* free memory used to store host lists */
		xodtemplate_free_hostlist(master_hostlist);
		xodtemplate_free_hostlist(dependent_hostlist);
	        }


	/****** DUPLICATE SERVICE DEPENDENCY DEFINITIONS WITH MULTIPLE HOSTGROUP AND/OR HOST NAMES (MASTER AND DEPENDENT) ******/
	for(temp_servicedependency=xodtemplate_servicedependency_list;temp_servicedependency!=NULL;temp_servicedependency=temp_servicedependency->next){

		/* skip service dependencies without enough data */
		if(temp_servicedependency->hostgroup_name==NULL && temp_servicedependency->dependent_hostgroup_name==NULL && temp_servicedependency->host_name==NULL && temp_servicedependency->dependent_host_name==NULL)
			continue;

		/* get list of master host names */
		master_hostlist=xodtemplate_expand_hostgroups_and_hosts(temp_servicedependency->hostgroup_name,temp_servicedependency->host_name);
		if(master_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand master hostgroups and/or hosts specified in service dependency (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_servicedependency->_config_file),temp_servicedependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* get list of dependent host names */
		dependent_hostlist=xodtemplate_expand_hostgroups_and_hosts(temp_servicedependency->dependent_hostgroup_name,temp_servicedependency->dependent_host_name);
		if(dependent_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand dependent hostgroups and/or hosts specified in service dependency (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_servicedependency->_config_file),temp_servicedependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* duplicate the dependency definitions */
		first_item=TRUE;
		for(temp_hostlist=master_hostlist;temp_hostlist!=NULL;temp_hostlist=temp_hostlist->next){

			for(this_hostlist=dependent_hostlist;this_hostlist!=NULL;this_hostlist=this_hostlist->next){

				/* existing definition gets first names */
				if(first_item==TRUE){
					my_free((void **)&temp_servicedependency->host_name);
					my_free((void **)&temp_servicedependency->dependent_host_name);
					temp_servicedependency->host_name=(char *)strdup(temp_hostlist->host_name);
					temp_servicedependency->dependent_host_name=(char *)strdup(this_hostlist->host_name);
					first_item=FALSE;
					continue;
				        }
				else
					result=xodtemplate_duplicate_servicedependency(temp_servicedependency,temp_hostlist->host_name,temp_servicedependency->service_description,this_hostlist->host_name,temp_servicedependency->dependent_service_description);

				/* exit on error */
				if(result==ERROR)
					return ERROR;
			        }
		        }

		/* free memory used to store host lists */
		xodtemplate_free_hostlist(master_hostlist);
		xodtemplate_free_hostlist(dependent_hostlist);
	        }


	/****** DUPLICATE SERVICE DEPENDENCY DEFINITIONS WITH MULTIPLE MASTER DESCRIPTIONS ******/
	/* THIS MUST BE DONE AFTER DUPLICATING FOR MULTIPLE HOST NAMES (SEE ABOVE) */
	for(temp_servicedependency=xodtemplate_servicedependency_list;temp_servicedependency!=NULL;temp_servicedependency=temp_servicedependency->next){

		/* skip servicedependencies without enough data */
		if(temp_servicedependency->service_description==NULL || temp_servicedependency->host_name==NULL)
			continue;

		/* get list of services */
		temp_servicelist=xodtemplate_expand_servicegroups_and_services(temp_servicedependency->servicegroup_name,temp_servicedependency->host_name,temp_servicedependency->service_description);
		if(temp_servicelist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand services specified in service dependency (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_servicedependency->_config_file),temp_servicedependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* duplicate service escalation entries */
		first_item=TRUE;
		for(this_servicelist=temp_servicelist;this_servicelist!=NULL;this_servicelist=this_servicelist->next){

			/* if this is the first duplication, use the existing entry */
			if(first_item==TRUE){

				my_free((void **)&temp_servicedependency->service_description);
				temp_servicedependency->service_description=(char *)strdup(this_servicelist->service_description);
				if(temp_servicedependency->service_description==NULL){
					xodtemplate_free_servicelist(temp_servicelist);
					return ERROR;
				        }

				first_item=FALSE;
				continue;
			        }

			/* duplicate service escalation definition */
			result=xodtemplate_duplicate_servicedependency(temp_servicedependency,temp_servicedependency->host_name,this_servicelist->service_description,temp_servicedependency->dependent_host_name,temp_servicedependency->dependent_service_description);

			/* exit on error */
			if(result==ERROR){
				xodtemplate_free_servicelist(temp_servicelist);
				return ERROR;
		                }
		        }

		/* free memory we used for service list */
		xodtemplate_free_servicelist(temp_servicelist);
	        }


	/****** DUPLICATE SERVICE DEPENDENCY DEFINITIONS WITH MULTIPLE DEPENDENCY DESCRIPTIONS ******/
	/* THIS MUST BE DONE AFTER DUPLICATING FOR MULTIPLE HOST NAMES (SEE ABOVE) */
	for(temp_servicedependency=xodtemplate_servicedependency_list;temp_servicedependency!=NULL;temp_servicedependency=temp_servicedependency->next){

		/* skip servicedependencies without enough data */
		if(temp_servicedependency->dependent_service_description==NULL || temp_servicedependency->dependent_host_name==NULL)
			continue;

		/* get list of services */
		temp_servicelist=xodtemplate_expand_servicegroups_and_services(temp_servicedependency->dependent_servicegroup_name,temp_servicedependency->dependent_host_name,temp_servicedependency->dependent_service_description);
		if(temp_servicelist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand services specified in service dependency (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_servicedependency->_config_file),temp_servicedependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* duplicate service escalation entries */
		first_item=TRUE;
		for(this_servicelist=temp_servicelist;this_servicelist!=NULL;this_servicelist=this_servicelist->next){

			/* if this is the first duplication, use the existing entry */
			if(first_item==TRUE){

				my_free((void **)&temp_servicedependency->dependent_service_description);
				temp_servicedependency->dependent_service_description=(char *)strdup(this_servicelist->service_description);
				if(temp_servicedependency->dependent_service_description==NULL){
					xodtemplate_free_servicelist(temp_servicelist);
					return ERROR;
				        }

				first_item=FALSE;
				continue;
			        }

			/* duplicate service escalation definition */
			result=xodtemplate_duplicate_servicedependency(temp_servicedependency,temp_servicedependency->host_name,temp_servicedependency->service_description,temp_servicedependency->dependent_host_name,this_servicelist->service_description);

			/* exit on error */
			if(result==ERROR){
				xodtemplate_free_servicelist(temp_servicelist);
				return ERROR;
		                }
		        }

		/* free memory we used for service list */
		xodtemplate_free_servicelist(temp_servicelist);
	        }


	/****** DUPLICATE HOSTEXTINFO DEFINITIONS WITH ONE OR MORE HOSTGROUP AND/OR HOST NAMES ******/
	for(temp_hostextinfo=xodtemplate_hostextinfo_list;temp_hostextinfo!=NULL;temp_hostextinfo=temp_hostextinfo->next){

		/* skip definitions without enough data */
		if(temp_hostextinfo->hostgroup_name==NULL && temp_hostextinfo->host_name==NULL)
			continue;

		/* get list of hosts */
		temp_hostlist=xodtemplate_expand_hostgroups_and_hosts(temp_hostextinfo->hostgroup_name,temp_hostextinfo->host_name);
		if(temp_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand hostgroups and/or hosts specified in extended host info (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_hostextinfo->_config_file),temp_hostextinfo->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* add a copy of the definition for every host in the hostgroup/host name list */
		first_item=TRUE;
		for(this_hostlist=temp_hostlist;this_hostlist!=NULL;this_hostlist=this_hostlist->next){

			/* if this is the first duplication, use the existing entry */
			if(first_item==TRUE){

				my_free((void **)&temp_hostextinfo->host_name);
				temp_hostextinfo->host_name=(char *)strdup(this_hostlist->host_name);
				if(temp_hostextinfo->host_name==NULL){
					xodtemplate_free_hostlist(temp_hostlist);
					return ERROR;
				        }
				first_item=FALSE;
				continue;
			        }

			/* duplicate hostextinfo definition */
			result=xodtemplate_duplicate_hostextinfo(temp_hostextinfo,this_hostlist->host_name);

			/* exit on error */
			if(result==ERROR){
				xodtemplate_free_hostlist(temp_hostlist);
				return ERROR;
			        }
		        }

		/* free memory we used for host list */
		xodtemplate_free_hostlist(temp_hostlist);
	        }


	/****** DUPLICATE SERVICEEXTINFO DEFINITIONS WITH ONE OR MORE HOSTGROUP AND/OR HOST NAMES ******/
	for(temp_serviceextinfo=xodtemplate_serviceextinfo_list;temp_serviceextinfo!=NULL;temp_serviceextinfo=temp_serviceextinfo->next){

		/* skip definitions without enough data */
		if(temp_serviceextinfo->hostgroup_name==NULL && temp_serviceextinfo->host_name==NULL)
			continue;

		/* get list of hosts */
		temp_hostlist=xodtemplate_expand_hostgroups_and_hosts(temp_serviceextinfo->hostgroup_name,temp_serviceextinfo->host_name);
		if(temp_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand hostgroups and/or hosts specified in extended service info (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_serviceextinfo->_config_file),temp_serviceextinfo->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		/* add a copy of the definition for every host in the hostgroup/host name list */
		first_item=TRUE;
		for(this_hostlist=temp_hostlist;this_hostlist!=NULL;this_hostlist=this_hostlist->next){

			/* existing definition gets first host name */
			if(first_item==TRUE){
				my_free((void **)&temp_serviceextinfo->host_name);
				temp_serviceextinfo->host_name=(char *)strdup(this_hostlist->host_name);
				if(temp_serviceextinfo->host_name==NULL){
					xodtemplate_free_hostlist(temp_hostlist);
					return ERROR;
				        }
				first_item=FALSE;
				continue;
			        }

			/* duplicate serviceextinfo definition */
			result=xodtemplate_duplicate_serviceextinfo(temp_serviceextinfo,this_hostlist->host_name);

			/* exit on error */
			if(result==ERROR){
				xodtemplate_free_hostlist(temp_hostlist);
				return ERROR;
			        }
		        }

		/* free memory we used for host list */
		xodtemplate_free_hostlist(temp_hostlist);
	        }

#ifdef DEBUG0
	printf("xodtemplate_duplicate_objects() end\n");
#endif

	return OK;
        }



/* duplicates a service definition (with a new host name) */
int xodtemplate_duplicate_service(xodtemplate_service *temp_service, char *host_name){
	xodtemplate_service *new_service=NULL;
	xodtemplate_customvariablesmember *temp_customvariablesmember=NULL;
	int error=FALSE;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_service() start\n");
#endif

	/* allocate memory for a new service definition */
	new_service=(xodtemplate_service *)malloc(sizeof(xodtemplate_service));
	if(new_service==NULL){
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of service.\n");
#endif
		return ERROR;
	        }

	/* standard items */
	new_service->template=NULL;
	new_service->name=NULL;
	new_service->has_been_resolved=temp_service->has_been_resolved;
	new_service->register_object=temp_service->register_object;
	new_service->_config_file=temp_service->_config_file;
	new_service->_start_line=temp_service->_start_line;

	/* string defaults */
	new_service->hostgroup_name=NULL;
	new_service->have_hostgroup_name=temp_service->have_hostgroup_name;
	new_service->host_name=NULL;
	new_service->have_host_name=temp_service->have_host_name;
	new_service->service_description=NULL;
	new_service->have_service_description=temp_service->have_service_description;
	new_service->display_name=NULL;
	new_service->have_display_name=temp_service->have_display_name;
	new_service->service_groups=NULL;
	new_service->have_service_groups=temp_service->have_service_groups;
	new_service->check_command=NULL;
	new_service->have_check_command=temp_service->have_check_command;
	new_service->check_period=NULL;
	new_service->have_check_period=temp_service->have_check_period;
	new_service->event_handler=NULL;
	new_service->have_event_handler=temp_service->have_event_handler;
	new_service->notification_period=NULL;
	new_service->have_notification_period=temp_service->have_notification_period;
	new_service->contact_groups=NULL;
	new_service->have_contact_groups=temp_service->have_contact_groups;
	new_service->failure_prediction_options=NULL;
	new_service->have_failure_prediction_options=temp_service->have_failure_prediction_options;
	new_service->notes=NULL;
	new_service->have_notes=temp_service->have_notes;
	new_service->notes_url=NULL;
	new_service->have_notes_url=temp_service->have_notes_url;
	new_service->action_url=NULL;
	new_service->have_action_url=temp_service->have_action_url;
	new_service->icon_image=NULL;
	new_service->have_icon_image=temp_service->have_icon_image;
	new_service->icon_image_alt=NULL;
	new_service->have_icon_image_alt=temp_service->have_icon_image_alt;
	new_service->custom_variables=NULL;

	/* make sure hostgroup member in new service definition is NULL */
	new_service->hostgroup_name=NULL;

	/* allocate memory for and copy string members of service definition (host name provided, DO NOT duplicate hostgroup member!)*/
	if(temp_service->host_name!=NULL && (new_service->host_name=(char *)strdup(host_name))==NULL)
		error=TRUE;
	if(temp_service->template!=NULL && (new_service->template=(char *)strdup(temp_service->template))==NULL)
		error=TRUE;
	if(temp_service->name!=NULL && (new_service->name=(char *)strdup(temp_service->name))==NULL)
		error=TRUE;
	if(temp_service->service_description!=NULL && (new_service->service_description=(char *)strdup(temp_service->service_description))==NULL)
		error=TRUE;
	if(temp_service->display_name!=NULL && (new_service->display_name=(char *)strdup(temp_service->display_name))==NULL)
		error=TRUE;
	if(temp_service->service_groups!=NULL && (new_service->service_groups=(char *)strdup(temp_service->service_groups))==NULL)
		error=TRUE;
	if(temp_service->check_command!=NULL && (new_service->check_command=(char *)strdup(temp_service->check_command))==NULL)
		error=TRUE;
	if(temp_service->check_period!=NULL && (new_service->check_period=(char *)strdup(temp_service->check_period))==NULL)
		error=TRUE;
	if(temp_service->event_handler!=NULL && (new_service->event_handler=(char *)strdup(temp_service->event_handler))==NULL)
		error=TRUE;
	if(temp_service->notification_period!=NULL && (new_service->notification_period=(char *)strdup(temp_service->notification_period))==NULL)
		error=TRUE;
	if(temp_service->contact_groups!=NULL && (new_service->contact_groups=(char *)strdup(temp_service->contact_groups))==NULL)
		error=TRUE;
	if(temp_service->failure_prediction_options!=NULL && (new_service->failure_prediction_options=(char *)strdup(temp_service->failure_prediction_options))==NULL)
		error=TRUE;
	if(temp_service->notes!=NULL && (new_service->notes=(char *)strdup(temp_service->notes))==NULL)
		error=TRUE;
	if(temp_service->notes_url!=NULL && (new_service->notes_url=(char *)strdup(temp_service->notes_url))==NULL)
		error=TRUE;
	if(temp_service->action_url!=NULL && (new_service->action_url=(char *)strdup(temp_service->action_url))==NULL)
		error=TRUE;
	if(temp_service->icon_image!=NULL && (new_service->icon_image=(char *)strdup(temp_service->icon_image))==NULL)
		error=TRUE;
	if(temp_service->icon_image_alt!=NULL && (new_service->icon_image_alt=(char *)strdup(temp_service->icon_image_alt))==NULL)
		error=TRUE;

	if(error==TRUE){
		my_free((void **)&new_service->host_name);
		my_free((void **)&new_service->template);
		my_free((void **)&new_service->name);
		my_free((void **)&new_service->service_description);
		my_free((void **)&new_service->display_name);
		my_free((void **)&new_service->service_groups);
		my_free((void **)&new_service->check_command);
		my_free((void **)&new_service->check_period);
		my_free((void **)&new_service->event_handler);
		my_free((void **)&new_service->notification_period);
		my_free((void **)&new_service->contact_groups);
		my_free((void **)&new_service->failure_prediction_options);
		my_free((void **)&new_service->notes);
		my_free((void **)&new_service->notes_url);
		my_free((void **)&new_service->action_url);
		my_free((void **)&new_service->icon_image);
		my_free((void **)&new_service->icon_image_alt);
		my_free((void **)&new_service);
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of host escalation.\n");
#endif
		return ERROR;
	        }

	/* duplicate custom variables */
	for(temp_customvariablesmember=temp_service->custom_variables;temp_customvariablesmember!=NULL;temp_customvariablesmember=temp_customvariablesmember->next)
		xodtemplate_add_custom_variable_to_service(new_service,temp_customvariablesmember->variable_name,temp_customvariablesmember->variable_value);

	/* duplicate non-string members */
	new_service->max_check_attempts=temp_service->max_check_attempts;
	new_service->have_max_check_attempts=temp_service->have_max_check_attempts;
	new_service->normal_check_interval=temp_service->normal_check_interval;
	new_service->have_normal_check_interval=temp_service->have_normal_check_interval;
	new_service->retry_check_interval=temp_service->retry_check_interval;
	new_service->have_retry_check_interval=temp_service->have_retry_check_interval;
	new_service->active_checks_enabled=temp_service->active_checks_enabled;
	new_service->have_active_checks_enabled=temp_service->have_active_checks_enabled;
	new_service->passive_checks_enabled=temp_service->passive_checks_enabled;
	new_service->have_passive_checks_enabled=temp_service->have_passive_checks_enabled;
	new_service->parallelize_check=temp_service->parallelize_check;
	new_service->have_parallelize_check=temp_service->have_parallelize_check;
	new_service->is_volatile=temp_service->is_volatile;
	new_service->have_is_volatile=temp_service->have_is_volatile;
	new_service->obsess_over_service=temp_service->obsess_over_service;
	new_service->have_obsess_over_service=temp_service->have_obsess_over_service;
	new_service->event_handler_enabled=temp_service->event_handler_enabled;
	new_service->have_event_handler_enabled=temp_service->have_event_handler_enabled;
	new_service->check_freshness=temp_service->check_freshness;
	new_service->have_check_freshness=temp_service->have_check_freshness;
	new_service->freshness_threshold=temp_service->freshness_threshold;
	new_service->have_freshness_threshold=temp_service->have_freshness_threshold;
	new_service->flap_detection_enabled=temp_service->flap_detection_enabled;
	new_service->have_flap_detection_enabled=temp_service->have_flap_detection_enabled;
	new_service->low_flap_threshold=temp_service->low_flap_threshold;
	new_service->have_low_flap_threshold=temp_service->have_low_flap_threshold;
	new_service->high_flap_threshold=temp_service->high_flap_threshold;
	new_service->have_high_flap_threshold=temp_service->have_high_flap_threshold;
	new_service->flap_detection_on_ok=temp_service->flap_detection_on_ok;
	new_service->flap_detection_on_warning=temp_service->flap_detection_on_warning;
	new_service->flap_detection_on_unknown=temp_service->flap_detection_on_unknown;
	new_service->flap_detection_on_critical=temp_service->flap_detection_on_critical;
	new_service->have_flap_detection_options=temp_service->have_flap_detection_options;
	new_service->notify_on_unknown=temp_service->notify_on_unknown;
	new_service->notify_on_warning=temp_service->notify_on_warning;
	new_service->notify_on_critical=temp_service->notify_on_critical;
	new_service->notify_on_recovery=temp_service->notify_on_recovery;
	new_service->notify_on_flapping=temp_service->notify_on_flapping;
	new_service->have_notification_options=temp_service->have_notification_options;
	new_service->notifications_enabled=temp_service->notifications_enabled;
	new_service->have_notifications_enabled=temp_service->have_notifications_enabled;
	new_service->notification_interval=temp_service->notification_interval;
	new_service->have_notification_interval=temp_service->have_notification_interval;
	new_service->first_notification_delay=temp_service->first_notification_delay;
	new_service->have_first_notification_delay=temp_service->have_first_notification_delay;
	new_service->stalk_on_ok=temp_service->stalk_on_ok;
	new_service->stalk_on_unknown=temp_service->stalk_on_unknown;
	new_service->stalk_on_warning=temp_service->stalk_on_warning;
	new_service->stalk_on_critical=temp_service->stalk_on_critical;
	new_service->have_stalking_options=temp_service->have_stalking_options;
	new_service->process_perf_data=temp_service->process_perf_data;
	new_service->have_process_perf_data=temp_service->have_process_perf_data;
	new_service->failure_prediction_enabled=temp_service->failure_prediction_enabled;
	new_service->have_failure_prediction_enabled=temp_service->have_failure_prediction_enabled;
	new_service->retain_status_information=temp_service->retain_status_information;
	new_service->have_retain_status_information=temp_service->have_retain_status_information;
	new_service->retain_nonstatus_information=temp_service->retain_nonstatus_information;
	new_service->have_retain_nonstatus_information=temp_service->have_retain_nonstatus_information;

	/* add new service to head of list in memory */
	new_service->next=xodtemplate_service_list;
	xodtemplate_service_list=new_service;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_service() end\n");
#endif

	return OK;
        }




/* duplicates a host escalation definition (with a new host name) */
int xodtemplate_duplicate_hostescalation(xodtemplate_hostescalation *temp_hostescalation, char *host_name){
	xodtemplate_hostescalation *new_hostescalation=NULL;
	int error=FALSE;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_hostescalation() start\n");
#endif

	/* allocate memory for a new host escalation definition */
	new_hostescalation=(xodtemplate_hostescalation *)malloc(sizeof(xodtemplate_hostescalation));
	if(new_hostescalation==NULL){
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of host escalation.\n");
#endif
		return ERROR;
	        }

	/* standard items */
	new_hostescalation->template=NULL;
	new_hostescalation->name=NULL;
	new_hostescalation->has_been_resolved=temp_hostescalation->has_been_resolved;
	new_hostescalation->register_object=temp_hostescalation->register_object;
	new_hostescalation->_config_file=temp_hostescalation->_config_file;
	new_hostescalation->_start_line=temp_hostescalation->_start_line;

	/* string defaults */
	new_hostescalation->hostgroup_name=NULL;
	new_hostescalation->have_hostgroup_name=temp_hostescalation->have_hostgroup_name;
	new_hostescalation->host_name=NULL;
	new_hostescalation->have_host_name=temp_hostescalation->have_host_name;
	new_hostescalation->contact_groups=NULL;
	new_hostescalation->have_contact_groups=temp_hostescalation->have_contact_groups;
	new_hostescalation->escalation_period=NULL;
	new_hostescalation->have_escalation_period=temp_hostescalation->have_escalation_period;

	/* allocate memory for and copy string members of hostescalation definition */
	if(temp_hostescalation->host_name!=NULL && (new_hostescalation->host_name=(char *)strdup(host_name))==NULL)
		error=TRUE;
	if(temp_hostescalation->template!=NULL && (new_hostescalation->template=(char *)strdup(temp_hostescalation->template))==NULL)
		error=TRUE;
	if(temp_hostescalation->name!=NULL && (new_hostescalation->name=(char *)strdup(temp_hostescalation->name))==NULL)
		error=TRUE;
	if(temp_hostescalation->contact_groups!=NULL && (new_hostescalation->contact_groups=(char *)strdup(temp_hostescalation->contact_groups))==NULL)
		error=TRUE;
	if(temp_hostescalation->escalation_period!=NULL && (new_hostescalation->escalation_period=(char *)strdup(temp_hostescalation->escalation_period))==NULL)
		error=TRUE;

	if(error==TRUE){
		my_free((void **)&new_hostescalation->escalation_period);
		my_free((void **)&new_hostescalation->contact_groups);
		my_free((void **)&new_hostescalation->host_name);
		my_free((void **)&new_hostescalation->template);
		my_free((void **)&new_hostescalation->name);
		my_free((void **)&new_hostescalation);
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of host escalation.\n");
#endif
		return ERROR;
	        }

	/* duplicate non-string members */
	new_hostescalation->first_notification=temp_hostescalation->first_notification;
	new_hostescalation->last_notification=temp_hostescalation->last_notification;
	new_hostescalation->have_first_notification=temp_hostescalation->have_first_notification;
	new_hostescalation->have_last_notification=temp_hostescalation->have_last_notification;
	new_hostescalation->notification_interval=temp_hostescalation->notification_interval;
	new_hostescalation->have_notification_interval=temp_hostescalation->have_notification_interval;
	new_hostescalation->escalate_on_down=temp_hostescalation->escalate_on_down;
	new_hostescalation->escalate_on_unreachable=temp_hostescalation->escalate_on_unreachable;
	new_hostescalation->escalate_on_recovery=temp_hostescalation->escalate_on_recovery;
	new_hostescalation->have_escalation_options=temp_hostescalation->have_escalation_options;

	/* add new hostescalation to head of list in memory */
	new_hostescalation->next=xodtemplate_hostescalation_list;
	xodtemplate_hostescalation_list=new_hostescalation;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_hostescalation() end\n");
#endif

	return OK;
        }



/* duplicates a service escalation definition (with a new host name and/or service description) */
int xodtemplate_duplicate_serviceescalation(xodtemplate_serviceescalation *temp_serviceescalation, char *host_name, char *svc_description){
	xodtemplate_serviceescalation *new_serviceescalation=NULL;
	int error=FALSE;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_serviceescalation() start\n");
#endif

	/* allocate memory for a new service escalation definition */
	new_serviceescalation=(xodtemplate_serviceescalation *)malloc(sizeof(xodtemplate_serviceescalation));
	if(new_serviceescalation==NULL){
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of service escalation.\n");
#endif
		return ERROR;
	        }

	/* standard items */
	new_serviceescalation->template=NULL;
	new_serviceescalation->name=NULL;
	new_serviceescalation->has_been_resolved=temp_serviceescalation->has_been_resolved;
	new_serviceescalation->register_object=temp_serviceescalation->register_object;
	new_serviceescalation->_config_file=temp_serviceescalation->_config_file;
	new_serviceescalation->_start_line=temp_serviceescalation->_start_line;

	/* string defaults */
	new_serviceescalation->servicegroup_name=NULL;
	new_serviceescalation->have_servicegroup_name=temp_serviceescalation->have_servicegroup_name;
	new_serviceescalation->hostgroup_name=NULL;
	new_serviceescalation->have_hostgroup_name=temp_serviceescalation->have_hostgroup_name;
	new_serviceescalation->host_name=NULL;
	new_serviceescalation->have_host_name=temp_serviceescalation->have_host_name;
	new_serviceescalation->service_description=NULL;
	new_serviceescalation->have_service_description=temp_serviceescalation->have_service_description;
	new_serviceescalation->contact_groups=NULL;
	new_serviceescalation->have_contact_groups=temp_serviceescalation->have_contact_groups;
	new_serviceescalation->escalation_period=NULL;
	new_serviceescalation->have_escalation_period=temp_serviceescalation->have_escalation_period;

	/* allocate memory for and copy string members of serviceescalation definition */
	if(host_name!=NULL && (new_serviceescalation->host_name=(char *)strdup(host_name))==NULL)
		error=TRUE;
	else if(temp_serviceescalation->host_name!=NULL && (new_serviceescalation->host_name=(char *)strdup(temp_serviceescalation->host_name))==NULL)
		error=TRUE;
	if(temp_serviceescalation->template!=NULL && (new_serviceescalation->template=(char *)strdup(temp_serviceescalation->template))==NULL)
		error=TRUE;
	if(temp_serviceescalation->name!=NULL && (new_serviceescalation->name=(char *)strdup(temp_serviceescalation->name))==NULL)
		error=TRUE;
	if(svc_description!=NULL && (new_serviceescalation->service_description=(char *)strdup(svc_description))==NULL)
		error=TRUE;
	else if(temp_serviceescalation->service_description!=NULL && (new_serviceescalation->service_description=(char *)strdup(temp_serviceescalation->service_description))==NULL)
		error=TRUE;
	if(temp_serviceescalation->contact_groups!=NULL && (new_serviceescalation->contact_groups=(char *)strdup(temp_serviceescalation->contact_groups))==NULL)
		error=TRUE;
	if(temp_serviceescalation->escalation_period!=NULL && (new_serviceescalation->escalation_period=(char *)strdup(temp_serviceescalation->escalation_period))==NULL)
		error=TRUE;

	if(error==TRUE){
		my_free((void **)&new_serviceescalation->host_name);
		my_free((void **)&new_serviceescalation->template);
		my_free((void **)&new_serviceescalation->name);
		my_free((void **)&new_serviceescalation->service_description);
		my_free((void **)&new_serviceescalation->contact_groups);
		my_free((void **)&new_serviceescalation->contact_groups);
		my_free((void **)&new_serviceescalation);
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of service escalation.\n");
#endif
		return ERROR;
	        }

	/* duplicate non-string members */
	new_serviceescalation->first_notification=temp_serviceescalation->first_notification;
	new_serviceescalation->last_notification=temp_serviceescalation->last_notification;
	new_serviceescalation->have_first_notification=temp_serviceescalation->have_first_notification;
	new_serviceescalation->have_last_notification=temp_serviceescalation->have_last_notification;
	new_serviceescalation->notification_interval=temp_serviceescalation->notification_interval;
	new_serviceescalation->have_notification_interval=temp_serviceescalation->have_notification_interval;
	new_serviceescalation->escalate_on_warning=temp_serviceescalation->escalate_on_warning;
	new_serviceescalation->escalate_on_unknown=temp_serviceescalation->escalate_on_unknown;
	new_serviceescalation->escalate_on_critical=temp_serviceescalation->escalate_on_critical;
	new_serviceescalation->escalate_on_recovery=temp_serviceescalation->escalate_on_recovery;
	new_serviceescalation->have_escalation_options=temp_serviceescalation->have_escalation_options;
	
	/* add new serviceescalation to head of list in memory */
	new_serviceescalation->next=xodtemplate_serviceescalation_list;
	xodtemplate_serviceescalation_list=new_serviceescalation;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_serviceescalation() end\n");
#endif

	return OK;
        }



/* duplicates a host dependency definition (with master and dependent host names) */
int xodtemplate_duplicate_hostdependency(xodtemplate_hostdependency *temp_hostdependency, char *master_host_name, char *dependent_host_name){
	xodtemplate_hostdependency *new_hostdependency=NULL;
	int error=FALSE;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_hostdependency() start\n");
#endif

	/* allocate memory for a new host dependency definition */
	new_hostdependency=(xodtemplate_hostdependency *)malloc(sizeof(xodtemplate_hostdependency));
	if(new_hostdependency==NULL){
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of host dependency.\n");
#endif
		return ERROR;
	        }

	/* standard items */
	new_hostdependency->template=NULL;
	new_hostdependency->name=NULL;
	new_hostdependency->has_been_resolved=temp_hostdependency->has_been_resolved;
	new_hostdependency->register_object=temp_hostdependency->register_object;
	new_hostdependency->_config_file=temp_hostdependency->_config_file;
	new_hostdependency->_start_line=temp_hostdependency->_start_line;

	/* string defaults */
	new_hostdependency->hostgroup_name=NULL;
	new_hostdependency->have_hostgroup_name=temp_hostdependency->have_hostgroup_name;
	new_hostdependency->dependent_hostgroup_name=NULL;
	new_hostdependency->have_dependent_hostgroup_name=temp_hostdependency->have_dependent_hostgroup_name;
	new_hostdependency->host_name=NULL;
	new_hostdependency->have_host_name=temp_hostdependency->have_host_name;
	new_hostdependency->dependent_host_name=NULL;
	new_hostdependency->have_dependent_host_name=temp_hostdependency->have_dependent_host_name;

	/* allocate memory for and copy string members of hostdependency definition */
	if(temp_hostdependency->host_name!=NULL && (new_hostdependency->host_name=(char *)strdup(master_host_name))==NULL)
		error=TRUE;
       	if(temp_hostdependency->dependent_host_name!=NULL && (new_hostdependency->dependent_host_name=(char *)strdup(dependent_host_name))==NULL)
		error=TRUE;
	if(temp_hostdependency->template!=NULL && (new_hostdependency->template=(char *)strdup(temp_hostdependency->template))==NULL)
		error=TRUE;
	if(temp_hostdependency->name!=NULL && (new_hostdependency->name=(char *)strdup(temp_hostdependency->name))==NULL)
		error=TRUE;

	if(error==TRUE){
		my_free((void **)&new_hostdependency->dependent_host_name);
		my_free((void **)&new_hostdependency->host_name);
		my_free((void **)&new_hostdependency->template);
		my_free((void **)&new_hostdependency->name);
		my_free((void **)&new_hostdependency);
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of host dependency.\n");
#endif
		return ERROR;
	        } 

	/* duplicate non-string members */
	new_hostdependency->fail_notify_on_up=temp_hostdependency->fail_notify_on_up;
	new_hostdependency->fail_notify_on_down=temp_hostdependency->fail_notify_on_down;
	new_hostdependency->fail_notify_on_unreachable=temp_hostdependency->fail_notify_on_unreachable;
	new_hostdependency->fail_notify_on_pending=temp_hostdependency->fail_notify_on_pending;
	new_hostdependency->have_notification_dependency_options=temp_hostdependency->have_notification_dependency_options;
	new_hostdependency->fail_execute_on_up=temp_hostdependency->fail_execute_on_up;
	new_hostdependency->fail_execute_on_down=temp_hostdependency->fail_execute_on_down;
	new_hostdependency->fail_execute_on_unreachable=temp_hostdependency->fail_execute_on_unreachable;
	new_hostdependency->fail_execute_on_pending=temp_hostdependency->fail_execute_on_pending;
	new_hostdependency->have_execution_dependency_options=temp_hostdependency->have_execution_dependency_options;
	new_hostdependency->inherits_parent=temp_hostdependency->inherits_parent;
	new_hostdependency->have_inherits_parent=temp_hostdependency->have_inherits_parent;

	/* add new hostdependency to head of list in memory */
	new_hostdependency->next=xodtemplate_hostdependency_list;
	xodtemplate_hostdependency_list=new_hostdependency;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_hostdependency() end\n");
#endif

	return OK;
        }



/* duplicates a service dependency definition */
int xodtemplate_duplicate_servicedependency(xodtemplate_servicedependency *temp_servicedependency, char *master_host_name, char *master_service_description, char *dependent_host_name, char *dependent_service_description){
	xodtemplate_servicedependency *new_servicedependency=NULL;
	int error=FALSE;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_servicedependency() start\n");
#endif

	/* allocate memory for a new service dependency definition */
	new_servicedependency=(xodtemplate_servicedependency *)malloc(sizeof(xodtemplate_servicedependency));
	if(new_servicedependency==NULL){
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of service dependency.\n");
#endif
		return ERROR;
	        }

	/* standard items */
	new_servicedependency->template=NULL;
	new_servicedependency->name=NULL;
	new_servicedependency->has_been_resolved=temp_servicedependency->has_been_resolved;
	new_servicedependency->register_object=temp_servicedependency->register_object;
	new_servicedependency->_config_file=temp_servicedependency->_config_file;
	new_servicedependency->_start_line=temp_servicedependency->_start_line;

	/* string defaults */
	new_servicedependency->servicegroup_name=NULL;
	new_servicedependency->have_servicegroup_name=temp_servicedependency->have_servicegroup_name;
	new_servicedependency->hostgroup_name=NULL;
	new_servicedependency->have_hostgroup_name=temp_servicedependency->have_hostgroup_name;
	new_servicedependency->dependent_servicegroup_name=NULL;
	new_servicedependency->have_dependent_servicegroup_name=temp_servicedependency->have_dependent_servicegroup_name;
	new_servicedependency->dependent_hostgroup_name=NULL;
	new_servicedependency->have_dependent_hostgroup_name=temp_servicedependency->have_dependent_hostgroup_name;
	new_servicedependency->host_name=NULL;
	new_servicedependency->have_host_name=temp_servicedependency->have_host_name;
	new_servicedependency->dependent_host_name=NULL;
	new_servicedependency->have_dependent_host_name=temp_servicedependency->have_dependent_host_name;

	/* duplicate strings */
	if(temp_servicedependency->host_name!=NULL && (new_servicedependency->host_name=(char *)strdup(master_host_name))==NULL)
		error=TRUE;
	if(temp_servicedependency->service_description!=NULL && (new_servicedependency->service_description=(char *)strdup(master_service_description))==NULL)
		error=TRUE;
       	if(temp_servicedependency->dependent_host_name!=NULL && (new_servicedependency->dependent_host_name=(char *)strdup(dependent_host_name))==NULL)
		error=TRUE;
	if(temp_servicedependency->dependent_service_description!=NULL && (new_servicedependency->dependent_service_description=(char *)strdup(dependent_service_description))==NULL)
		error=TRUE;
	if(temp_servicedependency->template!=NULL && (new_servicedependency->template=(char *)strdup(temp_servicedependency->template))==NULL)
		error=TRUE;
	if(temp_servicedependency->name!=NULL && (new_servicedependency->name=(char *)strdup(temp_servicedependency->name))==NULL)
		error=TRUE;

	if(error==TRUE){
		my_free((void **)&new_servicedependency->host_name);
		my_free((void **)&new_servicedependency->service_description);
		my_free((void **)&new_servicedependency->dependent_host_name);
		my_free((void **)&new_servicedependency->dependent_service_description);
		my_free((void **)&new_servicedependency->template);
		my_free((void **)&new_servicedependency->name);
		my_free((void **)&new_servicedependency);
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of service dependency.\n");
#endif
		return ERROR;
	        } 

	/* duplicate non-string members */
	new_servicedependency->fail_notify_on_ok=temp_servicedependency->fail_notify_on_ok;
	new_servicedependency->fail_notify_on_unknown=temp_servicedependency->fail_notify_on_unknown;
	new_servicedependency->fail_notify_on_warning=temp_servicedependency->fail_notify_on_warning;
	new_servicedependency->fail_notify_on_critical=temp_servicedependency->fail_notify_on_critical;
	new_servicedependency->fail_notify_on_pending=temp_servicedependency->fail_notify_on_pending;
	new_servicedependency->have_notification_dependency_options=temp_servicedependency->have_notification_dependency_options;
	new_servicedependency->fail_execute_on_ok=temp_servicedependency->fail_execute_on_ok;
	new_servicedependency->fail_execute_on_unknown=temp_servicedependency->fail_execute_on_unknown;
	new_servicedependency->fail_execute_on_warning=temp_servicedependency->fail_execute_on_warning;
	new_servicedependency->fail_execute_on_critical=temp_servicedependency->fail_execute_on_critical;
	new_servicedependency->fail_execute_on_pending=temp_servicedependency->fail_execute_on_pending;
	new_servicedependency->have_execution_dependency_options=temp_servicedependency->have_execution_dependency_options;
	new_servicedependency->inherits_parent=temp_servicedependency->inherits_parent;
	new_servicedependency->have_inherits_parent=temp_servicedependency->have_inherits_parent;
	
	/* add new servicedependency to head of list in memory */
	new_servicedependency->next=xodtemplate_servicedependency_list;
	xodtemplate_servicedependency_list=new_servicedependency;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_servicedependency() end\n");
#endif

	return OK;
        }



/* duplicates a hostextinfo object definition */
int xodtemplate_duplicate_hostextinfo(xodtemplate_hostextinfo *this_hostextinfo, char *host_name){
	xodtemplate_hostextinfo *new_hostextinfo=NULL;
	int error=FALSE;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_hostextinfo() start\n");
#endif

	new_hostextinfo=(xodtemplate_hostextinfo *)malloc(sizeof(xodtemplate_hostextinfo));
	if(new_hostextinfo==NULL){
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of extended host info.\n");
#endif
		return ERROR;
	        }

	/* standard items */
	new_hostextinfo->template=NULL;
	new_hostextinfo->name=NULL;
	new_hostextinfo->has_been_resolved=this_hostextinfo->has_been_resolved;
	new_hostextinfo->register_object=this_hostextinfo->register_object;
	new_hostextinfo->_config_file=this_hostextinfo->_config_file;
	new_hostextinfo->_start_line=this_hostextinfo->_start_line;

	/* string defaults */
	new_hostextinfo->host_name=NULL;
	new_hostextinfo->have_host_name=this_hostextinfo->have_host_name;
	new_hostextinfo->hostgroup_name=NULL;
	new_hostextinfo->have_hostgroup_name=this_hostextinfo->have_hostgroup_name;
	new_hostextinfo->notes=NULL;
	new_hostextinfo->have_notes=this_hostextinfo->have_notes;
	new_hostextinfo->notes_url=NULL;
	new_hostextinfo->have_notes_url=this_hostextinfo->have_notes_url;
	new_hostextinfo->action_url=NULL;
	new_hostextinfo->have_action_url=this_hostextinfo->have_action_url;
	new_hostextinfo->icon_image=NULL;
	new_hostextinfo->have_icon_image=this_hostextinfo->have_icon_image;
	new_hostextinfo->icon_image_alt=NULL;
	new_hostextinfo->have_icon_image_alt=this_hostextinfo->have_icon_image_alt;
	new_hostextinfo->vrml_image=NULL;
	new_hostextinfo->have_vrml_image=this_hostextinfo->have_vrml_image;
	new_hostextinfo->statusmap_image=NULL;
	new_hostextinfo->have_statusmap_image=this_hostextinfo->have_statusmap_image;

	/* duplicate strings (host_name member is passed in) */
	if(host_name!=NULL && (new_hostextinfo->host_name=(char *)strdup(host_name))==NULL)
		error=TRUE;
	if(this_hostextinfo->template!=NULL && (new_hostextinfo->template=(char *)strdup(this_hostextinfo->template))==NULL)
		error=TRUE;
	if(this_hostextinfo->name!=NULL && (new_hostextinfo->name=(char *)strdup(this_hostextinfo->name))==NULL)
		error=TRUE;
	if(this_hostextinfo->notes!=NULL && (new_hostextinfo->notes=(char *)strdup(this_hostextinfo->notes))==NULL)
		error=TRUE;
	if(this_hostextinfo->notes_url!=NULL && (new_hostextinfo->notes_url=(char *)strdup(this_hostextinfo->notes_url))==NULL)
		error=TRUE;
	if(this_hostextinfo->action_url!=NULL && (new_hostextinfo->action_url=(char *)strdup(this_hostextinfo->action_url))==NULL)
		error=TRUE;
	if(this_hostextinfo->icon_image!=NULL && (new_hostextinfo->icon_image=(char *)strdup(this_hostextinfo->icon_image))==NULL)
		error=TRUE;
	if(this_hostextinfo->icon_image_alt!=NULL && (new_hostextinfo->icon_image_alt=(char *)strdup(this_hostextinfo->icon_image_alt))==NULL)
		error=TRUE;
	if(this_hostextinfo->vrml_image!=NULL && (new_hostextinfo->vrml_image=(char *)strdup(this_hostextinfo->vrml_image))==NULL)
		error=TRUE;
	if(this_hostextinfo->statusmap_image!=NULL && (new_hostextinfo->statusmap_image=(char *)strdup(this_hostextinfo->statusmap_image))==NULL)
		error=TRUE;

	if(error==TRUE){
		my_free((void **)&new_hostextinfo->host_name);
		my_free((void **)&new_hostextinfo->template);
		my_free((void **)&new_hostextinfo->name);
		my_free((void **)&new_hostextinfo->notes);
		my_free((void **)&new_hostextinfo->notes_url);
		my_free((void **)&new_hostextinfo->action_url);
		my_free((void **)&new_hostextinfo->icon_image);
		my_free((void **)&new_hostextinfo->icon_image_alt);
		my_free((void **)&new_hostextinfo->vrml_image);
		my_free((void **)&new_hostextinfo->statusmap_image);
		my_free((void **)&new_hostextinfo);
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of extended host info.\n");
#endif
		return ERROR;
	        }

	/* duplicate non-string members */
	new_hostextinfo->x_2d=this_hostextinfo->x_2d;
	new_hostextinfo->y_2d=this_hostextinfo->y_2d;
	new_hostextinfo->have_2d_coords=this_hostextinfo->have_2d_coords;
	new_hostextinfo->x_3d=this_hostextinfo->x_3d;
	new_hostextinfo->y_3d=this_hostextinfo->y_3d;
	new_hostextinfo->z_3d=this_hostextinfo->z_3d;
	new_hostextinfo->have_3d_coords=this_hostextinfo->have_3d_coords;

	/* add new object to head of list */
	new_hostextinfo->next=xodtemplate_hostextinfo_list;
	xodtemplate_hostextinfo_list=new_hostextinfo;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_hostextinfo() end\n");
#endif

	return OK;
        }



/* duplicates a serviceextinfo object definition */
int xodtemplate_duplicate_serviceextinfo(xodtemplate_serviceextinfo *this_serviceextinfo, char *host_name){
	xodtemplate_serviceextinfo *new_serviceextinfo=NULL;
	int error=FALSE;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_serviceextinfo() start\n");
#endif

	new_serviceextinfo=(xodtemplate_serviceextinfo *)malloc(sizeof(xodtemplate_serviceextinfo));
	if(new_serviceextinfo==NULL){
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of extended service info.\n");
#endif
		return ERROR;
	        }

	/* standard items */
	new_serviceextinfo->template=NULL;
	new_serviceextinfo->name=NULL;
	new_serviceextinfo->has_been_resolved=this_serviceextinfo->has_been_resolved;
	new_serviceextinfo->register_object=this_serviceextinfo->register_object;
	new_serviceextinfo->_config_file=this_serviceextinfo->_config_file;
	new_serviceextinfo->_start_line=this_serviceextinfo->_start_line;

	/* string defaults */
	new_serviceextinfo->host_name=NULL;
	new_serviceextinfo->have_host_name=this_serviceextinfo->have_host_name;
	new_serviceextinfo->hostgroup_name=NULL;
	new_serviceextinfo->have_hostgroup_name=this_serviceextinfo->have_hostgroup_name;
	new_serviceextinfo->notes=NULL;
	new_serviceextinfo->have_notes=this_serviceextinfo->have_notes;
	new_serviceextinfo->notes_url=NULL;
	new_serviceextinfo->have_notes_url=this_serviceextinfo->have_notes_url;
	new_serviceextinfo->action_url=NULL;
	new_serviceextinfo->have_action_url=this_serviceextinfo->have_action_url;
	new_serviceextinfo->icon_image=NULL;
	new_serviceextinfo->have_icon_image=this_serviceextinfo->have_icon_image;
	new_serviceextinfo->icon_image_alt=NULL;
	new_serviceextinfo->have_icon_image_alt=this_serviceextinfo->have_icon_image_alt;

	/* duplicate strings (host_name member is passed in) */
	if(host_name!=NULL && (new_serviceextinfo->host_name=(char *)strdup(host_name))==NULL)
		error=TRUE;
	if(this_serviceextinfo->template!=NULL && (new_serviceextinfo->template=(char *)strdup(this_serviceextinfo->template))==NULL)
		error=TRUE;
	if(this_serviceextinfo->name!=NULL && (new_serviceextinfo->name=(char *)strdup(this_serviceextinfo->name))==NULL)
		error=TRUE;
	if(this_serviceextinfo->service_description!=NULL && (new_serviceextinfo->service_description=(char *)strdup(this_serviceextinfo->service_description))==NULL)
		error=TRUE;
	if(this_serviceextinfo->notes!=NULL && (new_serviceextinfo->notes=(char *)strdup(this_serviceextinfo->notes))==NULL)
		error=TRUE;
	if(this_serviceextinfo->notes_url!=NULL && (new_serviceextinfo->notes_url=(char *)strdup(this_serviceextinfo->notes_url))==NULL)
		error=TRUE;
	if(this_serviceextinfo->action_url!=NULL && (new_serviceextinfo->action_url=(char *)strdup(this_serviceextinfo->action_url))==NULL)
		error=TRUE;
	if(this_serviceextinfo->icon_image!=NULL && (new_serviceextinfo->icon_image=(char *)strdup(this_serviceextinfo->icon_image))==NULL)
		error=TRUE;
	if(this_serviceextinfo->icon_image_alt!=NULL && (new_serviceextinfo->icon_image_alt=(char *)strdup(this_serviceextinfo->icon_image_alt))==NULL)
		error=TRUE;

	if(error==TRUE){
		my_free((void **)&new_serviceextinfo->host_name);
		my_free((void **)&new_serviceextinfo->template);
		my_free((void **)&new_serviceextinfo->name);
		my_free((void **)&new_serviceextinfo->service_description);
		my_free((void **)&new_serviceextinfo->notes);
		my_free((void **)&new_serviceextinfo->notes_url);
		my_free((void **)&new_serviceextinfo->action_url);
		my_free((void **)&new_serviceextinfo->icon_image);
		my_free((void **)&new_serviceextinfo->icon_image_alt);
		my_free((void **)&new_serviceextinfo);
#ifdef DEBUG1
		printf("Error: Could not allocate memory for duplicate definition of extended service info.\n");
#endif
		return ERROR;
	        }

	/* add new object to head of list */
	new_serviceextinfo->next=xodtemplate_serviceextinfo_list;
	xodtemplate_serviceextinfo_list=new_serviceextinfo;

#ifdef DEBUG0
	printf("xodtemplate_duplicate_serviceextinfo() end\n");
#endif

	return OK;
        }

#endif


/******************************************************************/
/***************** OBJECT RESOLUTION FUNCTIONS ********************/
/******************************************************************/

#ifdef NSCORE

/* resolves object definitions */
int xodtemplate_resolve_objects(void){
	xodtemplate_timeperiod *temp_timeperiod=NULL;
	xodtemplate_command *temp_command=NULL;
	xodtemplate_contactgroup *temp_contactgroup=NULL;
	xodtemplate_hostgroup *temp_hostgroup=NULL;
	xodtemplate_servicegroup *temp_servicegroup=NULL;
	xodtemplate_servicedependency *temp_servicedependency=NULL;
	xodtemplate_serviceescalation *temp_serviceescalation=NULL;
	xodtemplate_contact *temp_contact=NULL;
	xodtemplate_host *temp_host=NULL;
	xodtemplate_service *temp_service=NULL;
	xodtemplate_hostdependency *temp_hostdependency=NULL;
	xodtemplate_hostescalation *temp_hostescalation=NULL;
	xodtemplate_hostextinfo *temp_hostextinfo=NULL;
	xodtemplate_serviceextinfo *temp_serviceextinfo=NULL;

#ifdef DEBUG0
	printf("xodtemplate_resolve_objects() start\n");
#endif

	/* resolve all timeperiod objects */
	for(temp_timeperiod=xodtemplate_timeperiod_list;temp_timeperiod!=NULL;temp_timeperiod=temp_timeperiod->next){
		if(xodtemplate_resolve_timeperiod(temp_timeperiod)==ERROR)
			return ERROR;
	        }

	/* resolve all command objects */
	for(temp_command=xodtemplate_command_list;temp_command!=NULL;temp_command=temp_command->next){
		if(xodtemplate_resolve_command(temp_command)==ERROR)
			return ERROR;
	        }

	/* resolve all contactgroup objects */
	for(temp_contactgroup=xodtemplate_contactgroup_list;temp_contactgroup!=NULL;temp_contactgroup=temp_contactgroup->next){
		if(xodtemplate_resolve_contactgroup(temp_contactgroup)==ERROR)
			return ERROR;
	        }

	/* resolve all hostgroup objects */
	for(temp_hostgroup=xodtemplate_hostgroup_list;temp_hostgroup!=NULL;temp_hostgroup=temp_hostgroup->next){
		if(xodtemplate_resolve_hostgroup(temp_hostgroup)==ERROR)
			return ERROR;
	        }

	/* resolve all servicegroup objects */
	for(temp_servicegroup=xodtemplate_servicegroup_list;temp_servicegroup!=NULL;temp_servicegroup=temp_servicegroup->next){
		if(xodtemplate_resolve_servicegroup(temp_servicegroup)==ERROR)
			return ERROR;
	        }

	/* resolve all servicedependency objects */
	for(temp_servicedependency=xodtemplate_servicedependency_list;temp_servicedependency!=NULL;temp_servicedependency=temp_servicedependency->next){
		if(xodtemplate_resolve_servicedependency(temp_servicedependency)==ERROR)
			return ERROR;
	        }

	/* resolve all serviceescalation objects */
	for(temp_serviceescalation=xodtemplate_serviceescalation_list;temp_serviceescalation!=NULL;temp_serviceescalation=temp_serviceescalation->next){
		if(xodtemplate_resolve_serviceescalation(temp_serviceescalation)==ERROR)
			return ERROR;
	        }

	/* resolve all contact objects */
	for(temp_contact=xodtemplate_contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){
		if(xodtemplate_resolve_contact(temp_contact)==ERROR)
			return ERROR;
	        }

	/* resolve all host objects */
	for(temp_host=xodtemplate_host_list;temp_host!=NULL;temp_host=temp_host->next){
		if(xodtemplate_resolve_host(temp_host)==ERROR)
			return ERROR;
	        }

	/* resolve all service objects */
	for(temp_service=xodtemplate_service_list;temp_service!=NULL;temp_service=temp_service->next){
		if(xodtemplate_resolve_service(temp_service)==ERROR)
			return ERROR;
	        }

	/* resolve all hostdependency objects */
	for(temp_hostdependency=xodtemplate_hostdependency_list;temp_hostdependency!=NULL;temp_hostdependency=temp_hostdependency->next){
		if(xodtemplate_resolve_hostdependency(temp_hostdependency)==ERROR)
			return ERROR;
	        }

	/* resolve all hostescalation objects */
	for(temp_hostescalation=xodtemplate_hostescalation_list;temp_hostescalation!=NULL;temp_hostescalation=temp_hostescalation->next){
		if(xodtemplate_resolve_hostescalation(temp_hostescalation)==ERROR)
			return ERROR;
	        }

	/* resolve all hostextinfo objects */
	for(temp_hostextinfo=xodtemplate_hostextinfo_list;temp_hostextinfo!=NULL;temp_hostextinfo=temp_hostextinfo->next){
		if(xodtemplate_resolve_hostextinfo(temp_hostextinfo)==ERROR)
			return ERROR;
	        }

	/* resolve all serviceextinfo objects */
	for(temp_serviceextinfo=xodtemplate_serviceextinfo_list;temp_serviceextinfo!=NULL;temp_serviceextinfo=temp_serviceextinfo->next){
		if(xodtemplate_resolve_serviceextinfo(temp_serviceextinfo)==ERROR)
			return ERROR;
	        }

#ifdef DEBUG0
	printf("xodtemplate_resolve_objects() end\n");
#endif

	return OK;
        }



/* resolves a timeperiod object */
int xodtemplate_resolve_timeperiod(xodtemplate_timeperiod *this_timeperiod){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_timeperiod *template_timeperiod=NULL;
	int x;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_timeperiod() start\n");
#endif

	/* return if this timeperiod has already been resolved */
	if(this_timeperiod->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_timeperiod->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_timeperiod->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_timeperiod->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_timeperiod=xodtemplate_find_timeperiod(temp_ptr);
		if(template_timeperiod==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in timeperiod definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_timeperiod->_config_file),this_timeperiod->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template timeperiod... */
		xodtemplate_resolve_timeperiod(template_timeperiod);

		/* apply missing properties from template timeperiod... */
		if(this_timeperiod->timeperiod_name==NULL && template_timeperiod->timeperiod_name!=NULL)
			this_timeperiod->timeperiod_name=(char *)strdup(template_timeperiod->timeperiod_name);
		if(this_timeperiod->alias==NULL && template_timeperiod->alias!=NULL)
			this_timeperiod->alias=(char *)strdup(template_timeperiod->alias);
		for(x=0;x<7;x++){
			if(this_timeperiod->timeranges[x]==NULL && template_timeperiod->timeranges[x]!=NULL){
				this_timeperiod->timeranges[x]=(char *)strdup(template_timeperiod->timeranges[x]);
		                }
	                }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_timeperiod() end\n");
#endif

	return OK;
        }




/* resolves a command object */
int xodtemplate_resolve_command(xodtemplate_command *this_command){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_command *template_command=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_command() start\n");
#endif

	/* return if this command has already been resolved */
	if(this_command->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_command->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_command->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_command->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_command=xodtemplate_find_command(temp_ptr);
		if(template_command==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in command definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_command->_config_file),this_command->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template command... */
		xodtemplate_resolve_command(template_command);

		/* apply missing properties from template command... */
		if(this_command->command_name==NULL && template_command->command_name!=NULL)
			this_command->command_name=(char *)strdup(template_command->command_name);
		if(this_command->command_line==NULL && template_command->command_line!=NULL)
			this_command->command_line=(char *)strdup(template_command->command_line);
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_command() end\n");
#endif

	return OK;
        }




/* resolves a contactgroup object */
int xodtemplate_resolve_contactgroup(xodtemplate_contactgroup *this_contactgroup){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_contactgroup *template_contactgroup=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_contactgroup() start\n");
#endif

	/* return if this contactgroup has already been resolved */
	if(this_contactgroup->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_contactgroup->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_contactgroup->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_contactgroup->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_contactgroup=xodtemplate_find_contactgroup(temp_ptr);
		if(template_contactgroup==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in contactgroup definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_contactgroup->_config_file),this_contactgroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template contactgroup... */
		xodtemplate_resolve_contactgroup(template_contactgroup);

		/* apply missing properties from template contactgroup... */
		if(this_contactgroup->contactgroup_name==NULL && template_contactgroup->contactgroup_name!=NULL)
			this_contactgroup->contactgroup_name=(char *)strdup(template_contactgroup->contactgroup_name);
		if(this_contactgroup->alias==NULL && template_contactgroup->alias!=NULL)
			this_contactgroup->alias=(char *)strdup(template_contactgroup->alias);
		if(this_contactgroup->members==NULL && template_contactgroup->members!=NULL)
			this_contactgroup->members=(char *)strdup(template_contactgroup->members);
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_contactgroup() end\n");
#endif

	return OK;
        }




/* resolves a hostgroup object */
int xodtemplate_resolve_hostgroup(xodtemplate_hostgroup *this_hostgroup){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_hostgroup *template_hostgroup=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_hostgroup() start\n");
#endif

	/* return if this hostgroup has already been resolved */
	if(this_hostgroup->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_hostgroup->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_hostgroup->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_hostgroup->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_hostgroup=xodtemplate_find_hostgroup(temp_ptr);
		if(template_hostgroup==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in hostgroup definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_hostgroup->_config_file),this_hostgroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template hostgroup... */
		xodtemplate_resolve_hostgroup(template_hostgroup);

		/* apply missing properties from template hostgroup... */
		if(this_hostgroup->hostgroup_name==NULL && template_hostgroup->hostgroup_name!=NULL)
			this_hostgroup->hostgroup_name=(char *)strdup(template_hostgroup->hostgroup_name);
		if(this_hostgroup->alias==NULL && template_hostgroup->alias!=NULL)
			this_hostgroup->alias=(char *)strdup(template_hostgroup->alias);
		if(this_hostgroup->have_members==FALSE && template_hostgroup->have_members==TRUE){
			if(this_hostgroup->members==NULL && template_hostgroup->members!=NULL)
				this_hostgroup->members=(char *)strdup(template_hostgroup->members);
			this_hostgroup->have_members=TRUE;
		        }
		if(this_hostgroup->have_hostgroup_members==FALSE && template_hostgroup->have_hostgroup_members==TRUE){
			if(this_hostgroup->hostgroup_members==NULL && template_hostgroup->hostgroup_members!=NULL)
				this_hostgroup->hostgroup_members=(char *)strdup(template_hostgroup->hostgroup_members);
			this_hostgroup->have_hostgroup_members=TRUE;
		        }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_hostgroup() end\n");
#endif

	return OK;
        }




/* resolves a servicegroup object */
int xodtemplate_resolve_servicegroup(xodtemplate_servicegroup *this_servicegroup){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_servicegroup *template_servicegroup=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_servicegroup() start\n");
#endif

	/* return if this servicegroup has already been resolved */
	if(this_servicegroup->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_servicegroup->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_servicegroup->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_servicegroup->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_servicegroup=xodtemplate_find_servicegroup(temp_ptr);
		if(template_servicegroup==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in servicegroup definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_servicegroup->_config_file),this_servicegroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template servicegroup... */
		xodtemplate_resolve_servicegroup(template_servicegroup);

		/* apply missing properties from template servicegroup... */
		if(this_servicegroup->servicegroup_name==NULL && template_servicegroup->servicegroup_name!=NULL)
			this_servicegroup->servicegroup_name=(char *)strdup(template_servicegroup->servicegroup_name);
		if(this_servicegroup->alias==NULL && template_servicegroup->alias!=NULL)
			this_servicegroup->alias=(char *)strdup(template_servicegroup->alias);
		if(this_servicegroup->have_members==FALSE && template_servicegroup->have_members==TRUE){
			if(this_servicegroup->members==NULL && template_servicegroup->members!=NULL)
				this_servicegroup->members=(char *)strdup(template_servicegroup->members);
			this_servicegroup->have_members=TRUE;
		        }
		if(this_servicegroup->have_servicegroup_members==FALSE && template_servicegroup->have_servicegroup_members==TRUE){
			if(this_servicegroup->servicegroup_members==NULL && template_servicegroup->servicegroup_members!=NULL)
				this_servicegroup->servicegroup_members=(char *)strdup(template_servicegroup->servicegroup_members);
			this_servicegroup->have_servicegroup_members=TRUE;
		        }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_servicegroup() end\n");
#endif

	return OK;
        }


/* resolves a servicedependency object */
int xodtemplate_resolve_servicedependency(xodtemplate_servicedependency *this_servicedependency){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_servicedependency *template_servicedependency=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_servicedependency() start\n");
#endif

	/* return if this servicedependency has already been resolved */
	if(this_servicedependency->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_servicedependency->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_servicedependency->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_servicedependency->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_servicedependency=xodtemplate_find_servicedependency(temp_ptr);
		if(template_servicedependency==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in service dependency definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_servicedependency->_config_file),this_servicedependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template servicedependency... */
		xodtemplate_resolve_servicedependency(template_servicedependency);

		/* apply missing properties from template servicedependency... */
		if(this_servicedependency->have_servicegroup_name==FALSE && template_servicedependency->have_servicegroup_name==TRUE){
			if(this_servicedependency->servicegroup_name==NULL && template_servicedependency->servicegroup_name!=NULL)
				this_servicedependency->servicegroup_name=(char *)strdup(template_servicedependency->servicegroup_name);
			this_servicedependency->have_servicegroup_name=TRUE;
		        }
		if(this_servicedependency->have_hostgroup_name==FALSE && template_servicedependency->have_hostgroup_name==TRUE){
			if(this_servicedependency->hostgroup_name==NULL && template_servicedependency->hostgroup_name!=NULL)
				this_servicedependency->hostgroup_name=(char *)strdup(template_servicedependency->hostgroup_name);
			this_servicedependency->have_hostgroup_name=TRUE;
		        }
		if(this_servicedependency->have_host_name==FALSE && template_servicedependency->have_host_name==TRUE){
			if(this_servicedependency->host_name==NULL && template_servicedependency->host_name!=NULL)
				this_servicedependency->host_name=(char *)strdup(template_servicedependency->host_name);
			this_servicedependency->have_host_name=TRUE;
		        }
		if(this_servicedependency->have_service_description==FALSE && template_servicedependency->have_service_description==TRUE){
			if(this_servicedependency->service_description==NULL && template_servicedependency->service_description!=NULL)
				this_servicedependency->service_description=(char *)strdup(template_servicedependency->service_description);
			this_servicedependency->have_service_description=TRUE;
		        }
		if(this_servicedependency->have_dependent_servicegroup_name==FALSE && template_servicedependency->have_dependent_servicegroup_name==TRUE){
			if(this_servicedependency->dependent_servicegroup_name==NULL && template_servicedependency->dependent_servicegroup_name!=NULL)
				this_servicedependency->dependent_servicegroup_name=(char *)strdup(template_servicedependency->dependent_servicegroup_name);
			this_servicedependency->have_dependent_servicegroup_name=TRUE;
		        }
		if(this_servicedependency->have_dependent_hostgroup_name==FALSE && template_servicedependency->have_dependent_hostgroup_name==TRUE){
			if(this_servicedependency->dependent_hostgroup_name==NULL && template_servicedependency->dependent_hostgroup_name!=NULL)
				this_servicedependency->dependent_hostgroup_name=(char *)strdup(template_servicedependency->dependent_hostgroup_name);
			this_servicedependency->have_dependent_hostgroup_name=TRUE;
		        }
		if(this_servicedependency->have_dependent_host_name==FALSE && template_servicedependency->have_dependent_host_name==TRUE){
			if(this_servicedependency->dependent_host_name==NULL && template_servicedependency->dependent_host_name!=NULL)
				this_servicedependency->dependent_host_name=(char *)strdup(template_servicedependency->dependent_host_name);
			this_servicedependency->have_dependent_host_name=TRUE;
		        }
		if(this_servicedependency->have_dependent_service_description==FALSE && template_servicedependency->have_dependent_service_description==TRUE){
			if(this_servicedependency->dependent_service_description==NULL && template_servicedependency->dependent_service_description!=NULL)
				this_servicedependency->dependent_service_description=(char *)strdup(template_servicedependency->dependent_service_description);
			this_servicedependency->have_dependent_service_description=TRUE;
		        }
		if(this_servicedependency->have_inherits_parent==FALSE && template_servicedependency->have_inherits_parent==TRUE){
			this_servicedependency->inherits_parent=template_servicedependency->inherits_parent;
			this_servicedependency->have_inherits_parent=TRUE;
	                }
		if(this_servicedependency->have_execution_dependency_options==FALSE && template_servicedependency->have_execution_dependency_options==TRUE){
			this_servicedependency->fail_execute_on_ok=template_servicedependency->fail_execute_on_ok;
			this_servicedependency->fail_execute_on_unknown=template_servicedependency->fail_execute_on_unknown;
			this_servicedependency->fail_execute_on_warning=template_servicedependency->fail_execute_on_warning;
			this_servicedependency->fail_execute_on_critical=template_servicedependency->fail_execute_on_critical;
			this_servicedependency->fail_execute_on_pending=template_servicedependency->fail_execute_on_pending;
			this_servicedependency->have_execution_dependency_options=TRUE;
	                }
		if(this_servicedependency->have_notification_dependency_options==FALSE && template_servicedependency->have_notification_dependency_options==TRUE){
			this_servicedependency->fail_notify_on_ok=template_servicedependency->fail_notify_on_ok;
			this_servicedependency->fail_notify_on_unknown=template_servicedependency->fail_notify_on_unknown;
			this_servicedependency->fail_notify_on_warning=template_servicedependency->fail_notify_on_warning;
			this_servicedependency->fail_notify_on_critical=template_servicedependency->fail_notify_on_critical;
			this_servicedependency->fail_notify_on_pending=template_servicedependency->fail_notify_on_pending;
			this_servicedependency->have_notification_dependency_options=TRUE;
	                }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_servicedependency() end\n");
#endif

	return OK;
        }


/* resolves a serviceescalation object */
int xodtemplate_resolve_serviceescalation(xodtemplate_serviceescalation *this_serviceescalation){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_serviceescalation *template_serviceescalation=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_serviceescalation() start\n");
#endif

	/* return if this serviceescalation has already been resolved */
	if(this_serviceescalation->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_serviceescalation->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_serviceescalation->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_serviceescalation->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_serviceescalation=xodtemplate_find_serviceescalation(temp_ptr);
		if(template_serviceescalation==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in service escalation definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_serviceescalation->_config_file),this_serviceescalation->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template serviceescalation... */
		xodtemplate_resolve_serviceescalation(template_serviceescalation);

		/* apply missing properties from template serviceescalation... */
		if(this_serviceescalation->have_servicegroup_name==FALSE && template_serviceescalation->have_servicegroup_name==TRUE){
			if(this_serviceescalation->servicegroup_name==NULL && template_serviceescalation->servicegroup_name!=NULL)
				this_serviceescalation->servicegroup_name=(char *)strdup(template_serviceescalation->servicegroup_name);
			this_serviceescalation->have_servicegroup_name=TRUE;
		        }
		if(this_serviceescalation->have_hostgroup_name==FALSE && template_serviceescalation->have_hostgroup_name==TRUE){
			if(this_serviceescalation->hostgroup_name==NULL && template_serviceescalation->hostgroup_name!=NULL)
				this_serviceescalation->hostgroup_name=(char *)strdup(template_serviceescalation->hostgroup_name);
			this_serviceescalation->have_hostgroup_name=TRUE;
		        }
		if(this_serviceescalation->have_host_name==FALSE && template_serviceescalation->have_host_name==TRUE){
			if(this_serviceescalation->host_name==NULL && template_serviceescalation->host_name!=NULL)
				this_serviceescalation->host_name=(char *)strdup(template_serviceescalation->host_name);
			this_serviceescalation->have_host_name=TRUE;
		        }
		if(this_serviceescalation->have_service_description==FALSE && template_serviceescalation->have_service_description==TRUE){
			if(this_serviceescalation->service_description==NULL && template_serviceescalation->service_description!=NULL)
				this_serviceescalation->service_description=(char *)strdup(template_serviceescalation->service_description);
			this_serviceescalation->have_service_description=TRUE;
		        }
		if(this_serviceescalation->have_escalation_period==FALSE && template_serviceescalation->have_escalation_period==TRUE){
			if(this_serviceescalation->escalation_period==NULL && template_serviceescalation->escalation_period!=NULL)
				this_serviceescalation->escalation_period=(char *)strdup(template_serviceescalation->escalation_period);
			this_serviceescalation->have_escalation_period=TRUE;
		        }
		if(this_serviceescalation->have_contact_groups==FALSE && template_serviceescalation->have_contact_groups==TRUE){
			if(this_serviceescalation->contact_groups==NULL && template_serviceescalation->contact_groups!=NULL)
				this_serviceescalation->contact_groups=(char *)strdup(template_serviceescalation->contact_groups);
			this_serviceescalation->have_contact_groups=TRUE;
		        }
		if(this_serviceescalation->have_first_notification==FALSE && template_serviceescalation->have_first_notification==TRUE){
			this_serviceescalation->first_notification=template_serviceescalation->first_notification;
			this_serviceescalation->have_first_notification=TRUE;
	                }
		if(this_serviceescalation->have_last_notification==FALSE && template_serviceescalation->have_last_notification==TRUE){
			this_serviceescalation->last_notification=template_serviceescalation->last_notification;
			this_serviceescalation->have_last_notification=TRUE;
	                }
		if(this_serviceescalation->have_notification_interval==FALSE && template_serviceescalation->have_notification_interval==TRUE){
			this_serviceescalation->notification_interval=template_serviceescalation->notification_interval;
			this_serviceescalation->have_notification_interval=TRUE;
	                }
		if(this_serviceescalation->have_escalation_options==FALSE && template_serviceescalation->have_escalation_options==TRUE){
			this_serviceescalation->escalate_on_warning=template_serviceescalation->escalate_on_warning;
			this_serviceescalation->escalate_on_unknown=template_serviceescalation->escalate_on_unknown;
			this_serviceescalation->escalate_on_critical=template_serviceescalation->escalate_on_critical;
			this_serviceescalation->escalate_on_recovery=template_serviceescalation->escalate_on_recovery;
			this_serviceescalation->have_escalation_options=TRUE;
	                }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_serviceescalation() end\n");
#endif

	return OK;
        }



/* resolves a contact object */
int xodtemplate_resolve_contact(xodtemplate_contact *this_contact){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_contact *template_contact=NULL;
	xodtemplate_customvariablesmember *this_customvariablesmember=NULL;
	xodtemplate_customvariablesmember *temp_customvariablesmember=NULL;
	int x;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_contact() start\n");
#endif

	/* return if this contact has already been resolved */
	if(this_contact->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_contact->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_contact->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_contact->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_contact=xodtemplate_find_contact(temp_ptr);
		if(template_contact==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in contact definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_contact->_config_file),this_contact->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template contact... */
		xodtemplate_resolve_contact(template_contact);

		/* apply missing properties from template contact... */
		if(this_contact->contact_name==NULL && template_contact->contact_name!=NULL)
			this_contact->contact_name=(char *)strdup(template_contact->contact_name);
		if(this_contact->alias==NULL && template_contact->alias!=NULL)
			this_contact->alias=(char *)strdup(template_contact->alias);

		if(this_contact->have_email==FALSE && template_contact->have_email==TRUE){
			if(this_contact->email==NULL && template_contact->email!=NULL)
				this_contact->email=(char *)strdup(template_contact->email);
			this_contact->have_email=TRUE;
		        }
		if(this_contact->have_pager==FALSE && template_contact->have_pager==TRUE){
			if(this_contact->pager==NULL && template_contact->pager!=NULL)
				this_contact->pager=(char *)strdup(template_contact->pager);
			this_contact->have_pager=TRUE;
		        }
		for(x=0;x<MAX_XODTEMPLATE_CONTACT_ADDRESSES;x++){
			if(this_contact->have_address[x]==FALSE && template_contact->have_address[x]==TRUE){
				if(this_contact->address[x]==NULL && template_contact->address[x]!=NULL)
					this_contact->address[x]=(char *)strdup(template_contact->address[x]);
				this_contact->have_address[x]=TRUE;
			        }
	                }
		if(this_contact->have_contact_groups==FALSE && template_contact->have_contact_groups==TRUE){
			if(this_contact->contact_groups==NULL && template_contact->contact_groups!=NULL)
				this_contact->contact_groups=(char *)strdup(template_contact->contact_groups);
			this_contact->have_contact_groups=TRUE;
		        }
		if(this_contact->have_host_notification_period==FALSE && template_contact->have_host_notification_period==TRUE){
			if(this_contact->host_notification_period==NULL && template_contact->host_notification_period!=NULL)
				this_contact->host_notification_period=(char *)strdup(template_contact->host_notification_period);
			this_contact->have_host_notification_period=TRUE;
		        }
		if(this_contact->have_service_notification_period==FALSE && template_contact->have_service_notification_period==TRUE){
			if(this_contact->service_notification_period==NULL && template_contact->service_notification_period!=NULL)
				this_contact->service_notification_period=(char *)strdup(template_contact->service_notification_period);
			this_contact->have_service_notification_period=TRUE;
		        }
		if(this_contact->have_host_notification_commands==FALSE && template_contact->have_host_notification_commands==TRUE){
			if(this_contact->host_notification_commands==NULL && template_contact->host_notification_commands!=NULL)
				this_contact->host_notification_commands=(char *)strdup(template_contact->host_notification_commands);
			this_contact->have_host_notification_commands=TRUE;
		        }
		if(this_contact->have_service_notification_commands==FALSE && template_contact->have_service_notification_commands==TRUE){
			if(this_contact->service_notification_commands==NULL && template_contact->service_notification_commands!=NULL)
				this_contact->service_notification_commands=(char *)strdup(template_contact->service_notification_commands);
			this_contact->have_service_notification_commands=TRUE;
		        }
		if(this_contact->have_host_notification_options==FALSE && template_contact->have_host_notification_options==TRUE){
			this_contact->notify_on_host_down=template_contact->notify_on_host_down;
			this_contact->notify_on_host_unreachable=template_contact->notify_on_host_unreachable;
			this_contact->notify_on_host_recovery=template_contact->notify_on_host_recovery;
			this_contact->notify_on_host_flapping=template_contact->notify_on_host_flapping;
			this_contact->have_host_notification_options=TRUE;
	                }
		if(this_contact->have_service_notification_options==FALSE && template_contact->have_service_notification_options==TRUE){
			this_contact->notify_on_service_unknown=template_contact->notify_on_service_unknown;
			this_contact->notify_on_service_warning=template_contact->notify_on_service_warning;
			this_contact->notify_on_service_critical=template_contact->notify_on_service_critical;
			this_contact->notify_on_service_recovery=template_contact->notify_on_service_recovery;
			this_contact->notify_on_service_flapping=template_contact->notify_on_service_flapping;
			this_contact->have_service_notification_options=TRUE;
	                }
		if(this_contact->have_host_notifications_enabled==FALSE && template_contact->have_host_notifications_enabled==TRUE){
			this_contact->host_notifications_enabled=template_contact->host_notifications_enabled;
			this_contact->have_host_notifications_enabled=TRUE;
	                }
		if(this_contact->have_service_notifications_enabled==FALSE && template_contact->have_service_notifications_enabled==TRUE){
			this_contact->service_notifications_enabled=template_contact->service_notifications_enabled;
			this_contact->have_service_notifications_enabled=TRUE;
	                }
		if(this_contact->have_can_submit_commands==FALSE && template_contact->have_can_submit_commands==TRUE){
			this_contact->can_submit_commands=template_contact->can_submit_commands;
			this_contact->have_can_submit_commands=TRUE;
	                }
		if(this_contact->have_retain_status_information==FALSE && template_contact->have_retain_status_information==TRUE){
			this_contact->retain_status_information=template_contact->retain_status_information;
			this_contact->have_retain_status_information=TRUE;
	                }
		if(this_contact->have_retain_nonstatus_information==FALSE && template_contact->have_retain_nonstatus_information==TRUE){
			this_contact->retain_nonstatus_information=template_contact->retain_nonstatus_information;
			this_contact->have_retain_nonstatus_information=TRUE;
	                }

		/* apply missing custom variables from template contact... */
		for(temp_customvariablesmember=template_contact->custom_variables;temp_customvariablesmember!=NULL;temp_customvariablesmember=temp_customvariablesmember->next){

			/* see if this host has a variable by the same name */
			for(this_customvariablesmember=this_contact->custom_variables;this_customvariablesmember!=NULL;this_customvariablesmember=this_customvariablesmember->next){
				if(!strcmp(temp_customvariablesmember->variable_name,this_customvariablesmember->variable_name))
					break;
			        }

			/* we didn't find the same variable name, so add a new custom variable */
			if(this_customvariablesmember==NULL)
				xodtemplate_add_custom_variable_to_contact(this_contact,temp_customvariablesmember->variable_name,temp_customvariablesmember->variable_value);
		        }
	        }
	
	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_contact() end\n");
#endif

	return OK;
        }



/* resolves a host object */
int xodtemplate_resolve_host(xodtemplate_host *this_host){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_host *template_host=NULL;
	xodtemplate_customvariablesmember *this_customvariablesmember=NULL;
	xodtemplate_customvariablesmember *temp_customvariablesmember=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_host() start\n");
#endif

	/* return if this host has already been resolved */
	if(this_host->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_host->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_host->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_host->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_host=xodtemplate_find_host(temp_ptr);
		if(template_host==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in host definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_host->_config_file),this_host->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template host... */
		xodtemplate_resolve_host(template_host);

		/* apply missing properties from template host... */
		if(this_host->host_name==NULL && template_host->host_name!=NULL)
			this_host->host_name=(char *)strdup(template_host->host_name);
		if(this_host->have_display_name==FALSE && template_host->have_display_name==TRUE){
			if(this_host->display_name==NULL && template_host->display_name!=NULL)
				this_host->display_name=(char *)strdup(template_host->display_name);
			this_host->have_display_name=TRUE;
		        }
		if(this_host->alias==NULL && template_host->alias!=NULL)
			this_host->alias=(char *)strdup(template_host->alias);
		if(this_host->address==NULL && template_host->address!=NULL)
			this_host->address=(char *)strdup(template_host->address);
		if(this_host->have_parents==FALSE && template_host->have_parents==TRUE){
			if(this_host->parents==NULL && template_host->parents!=NULL)
				this_host->parents=(char *)strdup(template_host->parents);
			this_host->have_parents=TRUE;
		        }
		if(this_host->have_host_groups==FALSE && template_host->have_host_groups==TRUE){
			if(this_host->host_groups==NULL && template_host->host_groups!=NULL)
				this_host->host_groups=(char *)strdup(template_host->host_groups);
			this_host->have_host_groups=TRUE;
		        }
		if(this_host->have_check_command==FALSE && template_host->have_check_command==TRUE){
			if(this_host->check_command==NULL && template_host->check_command!=NULL)
				this_host->check_command=(char *)strdup(template_host->check_command);
			this_host->have_check_command=TRUE;
		        }
		if(this_host->have_check_period==FALSE && template_host->have_check_period==TRUE){
			if(this_host->check_period==NULL && template_host->check_period!=NULL)
				this_host->check_period=(char *)strdup(template_host->check_period);
			this_host->have_check_period=TRUE;
		        }
		if(this_host->have_event_handler==FALSE && template_host->have_event_handler==TRUE){
			if(this_host->event_handler==NULL && template_host->event_handler!=NULL)
				this_host->event_handler=(char *)strdup(template_host->event_handler);
			this_host->have_event_handler=TRUE;
		        }
		if(this_host->have_contact_groups==FALSE && template_host->have_contact_groups==TRUE){
			if(this_host->contact_groups==NULL && template_host->contact_groups!=NULL)
				this_host->contact_groups=(char *)strdup(template_host->contact_groups);
			this_host->have_contact_groups=TRUE;
		        }
		if(this_host->have_notification_period==FALSE && template_host->have_notification_period==TRUE){
			if(this_host->notification_period==NULL && template_host->notification_period!=NULL)
				this_host->notification_period=(char *)strdup(template_host->notification_period);
			this_host->have_notification_period=TRUE;
		        }
		if(this_host->have_failure_prediction_options==FALSE && template_host->have_failure_prediction_options==TRUE){
			if(this_host->failure_prediction_options==NULL && template_host->failure_prediction_options!=NULL)
				this_host->failure_prediction_options=(char *)strdup(template_host->failure_prediction_options);
			this_host->have_failure_prediction_options=TRUE;
		        }
		if(this_host->have_check_interval==FALSE && template_host->have_check_interval==TRUE){
			this_host->check_interval=template_host->check_interval;
			this_host->have_check_interval=TRUE;
	                }
		if(this_host->have_max_check_attempts==FALSE && template_host->have_max_check_attempts==TRUE){
			this_host->max_check_attempts=template_host->max_check_attempts;
			this_host->have_max_check_attempts=TRUE;
	                }
		if(this_host->have_active_checks_enabled==FALSE && template_host->have_active_checks_enabled==TRUE){
			this_host->active_checks_enabled=template_host->active_checks_enabled;
			this_host->have_active_checks_enabled=TRUE;
	                }
		if(this_host->have_passive_checks_enabled==FALSE && template_host->have_passive_checks_enabled==TRUE){
			this_host->passive_checks_enabled=template_host->passive_checks_enabled;
			this_host->have_passive_checks_enabled=TRUE;
	                }
		if(this_host->have_obsess_over_host==FALSE && template_host->have_obsess_over_host==TRUE){
			this_host->obsess_over_host=template_host->obsess_over_host;
			this_host->have_obsess_over_host=TRUE;
	                }
		if(this_host->have_event_handler_enabled==FALSE && template_host->have_event_handler_enabled==TRUE){
			this_host->event_handler_enabled=template_host->event_handler_enabled;
			this_host->have_event_handler_enabled=TRUE;
	                }
		if(this_host->have_check_freshness==FALSE && template_host->have_check_freshness==TRUE){
			this_host->check_freshness=template_host->check_freshness;
			this_host->have_check_freshness=TRUE;
	                }
		if(this_host->have_freshness_threshold==FALSE && template_host->have_freshness_threshold==TRUE){
			this_host->freshness_threshold=template_host->freshness_threshold;
			this_host->have_freshness_threshold=TRUE;
	                }
		if(this_host->have_low_flap_threshold==FALSE && template_host->have_low_flap_threshold==TRUE){
			this_host->low_flap_threshold=template_host->low_flap_threshold;
			this_host->have_low_flap_threshold=TRUE;
	                }
		if(this_host->have_high_flap_threshold==FALSE && template_host->have_high_flap_threshold==TRUE){
			this_host->high_flap_threshold=template_host->high_flap_threshold;
			this_host->have_high_flap_threshold=TRUE;
	                }
		if(this_host->have_flap_detection_enabled==FALSE && template_host->have_flap_detection_enabled==TRUE){
			this_host->flap_detection_enabled=template_host->flap_detection_enabled;
			this_host->have_flap_detection_enabled=TRUE;
	                }
		if(this_host->have_flap_detection_options==FALSE && template_host->have_flap_detection_options==TRUE){
			this_host->flap_detection_on_up=template_host->flap_detection_on_up;
			this_host->flap_detection_on_down=template_host->flap_detection_on_down;
			this_host->flap_detection_on_unreachable=template_host->flap_detection_on_unreachable;
			this_host->have_flap_detection_options=TRUE;
	                }
		if(this_host->have_notification_options==FALSE && template_host->have_notification_options==TRUE){
			this_host->notify_on_down=template_host->notify_on_down;
			this_host->notify_on_unreachable=template_host->notify_on_unreachable;
			this_host->notify_on_recovery=template_host->notify_on_recovery;
			this_host->notify_on_flapping=template_host->notify_on_flapping;
			this_host->have_notification_options=TRUE;
	                }
		if(this_host->have_notifications_enabled==FALSE && template_host->have_notifications_enabled==TRUE){
			this_host->notifications_enabled=template_host->notifications_enabled;
			this_host->have_notifications_enabled=TRUE;
	                }
		if(this_host->have_notification_interval==FALSE && template_host->have_notification_interval==TRUE){
			this_host->notification_interval=template_host->notification_interval;
			this_host->have_notification_interval=TRUE;
	                }
		if(this_host->have_first_notification_delay==FALSE && template_host->have_first_notification_delay==TRUE){
			this_host->first_notification_delay=template_host->first_notification_delay;
			this_host->have_first_notification_delay=TRUE;
	                }
		if(this_host->have_stalking_options==FALSE && template_host->have_stalking_options==TRUE){
			this_host->stalk_on_up=template_host->stalk_on_up;
			this_host->stalk_on_down=template_host->stalk_on_down;
			this_host->stalk_on_unreachable=template_host->stalk_on_unreachable;
			this_host->have_stalking_options=TRUE;
	                }
		if(this_host->have_process_perf_data==FALSE && template_host->have_process_perf_data==TRUE){
			this_host->process_perf_data=template_host->process_perf_data;
			this_host->have_process_perf_data=TRUE;
	                }
		if(this_host->have_failure_prediction_enabled==FALSE && template_host->have_failure_prediction_enabled==TRUE){
			this_host->failure_prediction_enabled=template_host->failure_prediction_enabled;
			this_host->have_failure_prediction_enabled=TRUE;
	                }
		if(this_host->have_retain_status_information==FALSE && template_host->have_retain_status_information==TRUE){
			this_host->retain_status_information=template_host->retain_status_information;
			this_host->have_retain_status_information=TRUE;
	                }
		if(this_host->have_retain_nonstatus_information==FALSE && template_host->have_retain_nonstatus_information==TRUE){
			this_host->retain_nonstatus_information=template_host->retain_nonstatus_information;
			this_host->have_retain_nonstatus_information=TRUE;
	                }

		/* apply missing custom variables from template host... */
		for(temp_customvariablesmember=template_host->custom_variables;temp_customvariablesmember!=NULL;temp_customvariablesmember=temp_customvariablesmember->next){

			/* see if this host has a variable by the same name */
			for(this_customvariablesmember=this_host->custom_variables;this_customvariablesmember!=NULL;this_customvariablesmember=this_customvariablesmember->next){
				if(!strcmp(temp_customvariablesmember->variable_name,this_customvariablesmember->variable_name))
					break;
			        }

			/* we didn't find the same variable name, so add a new custom variable */
			if(this_customvariablesmember==NULL)
				xodtemplate_add_custom_variable_to_host(this_host,temp_customvariablesmember->variable_name,temp_customvariablesmember->variable_value);
		        }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_host() end\n");
#endif

	return OK;
        }



/* resolves a service object */
int xodtemplate_resolve_service(xodtemplate_service *this_service){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_service *template_service=NULL;
	xodtemplate_customvariablesmember *this_customvariablesmember=NULL;
	xodtemplate_customvariablesmember *temp_customvariablesmember=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_service() start\n");
#endif

	/* return if this service has already been resolved */
	if(this_service->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_service->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_service->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_service->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_service=xodtemplate_find_service(temp_ptr);
		if(template_service==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in service definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_service->_config_file),this_service->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template service... */
		xodtemplate_resolve_service(template_service);

		/* apply missing properties from template service... */
		if(this_service->have_host_name==FALSE && template_service->have_host_name==TRUE){
			if(this_service->host_name==NULL && template_service->host_name!=NULL)
				this_service->host_name=(char *)strdup(template_service->host_name);
			this_service->have_host_name=TRUE;
		        }
		if(this_service->have_service_description==FALSE && template_service->have_service_description==TRUE){
			if(this_service->service_description==NULL && template_service->service_description!=NULL)
				this_service->service_description=(char *)strdup(template_service->service_description);
			this_service->have_service_description=TRUE;
		        }
		if(this_service->have_display_name==FALSE && template_service->have_display_name==TRUE){
			if(this_service->display_name==NULL && template_service->display_name!=NULL)
				this_service->display_name=(char *)strdup(template_service->display_name);
			this_service->have_display_name=TRUE;
		        }
		if(this_service->have_hostgroup_name==FALSE && template_service->have_hostgroup_name==TRUE){
			if(this_service->hostgroup_name==NULL && template_service->hostgroup_name!=NULL)
				this_service->hostgroup_name=(char *)strdup(template_service->hostgroup_name);
			this_service->have_hostgroup_name=TRUE;
		        }
		if(this_service->have_service_groups==FALSE && template_service->have_service_groups==TRUE){
			if(this_service->service_groups==NULL && template_service->service_groups!=NULL)
				this_service->service_groups=(char *)strdup(template_service->service_groups);
			this_service->have_service_groups=TRUE;
		        }
		if(this_service->have_check_command==FALSE && template_service->have_check_command==TRUE){
			if(this_service->check_command==NULL && template_service->check_command!=NULL)
				this_service->check_command=(char *)strdup(template_service->check_command);
			this_service->have_check_command=TRUE;
		        }
		if(this_service->have_check_period==FALSE && template_service->have_check_period==TRUE){
			if(this_service->check_period==NULL && template_service->check_period!=NULL)
				this_service->check_period=(char *)strdup(template_service->check_period);
			this_service->have_check_period=TRUE;
		        }
		if(this_service->have_event_handler==FALSE && template_service->have_event_handler==TRUE){
			if(this_service->event_handler==NULL && template_service->event_handler!=NULL)
				this_service->event_handler=(char *)strdup(template_service->event_handler);
			this_service->have_event_handler=TRUE;
		        }
		if(this_service->have_notification_period==FALSE && template_service->have_notification_period==TRUE){
			if(this_service->notification_period==NULL && template_service->notification_period!=NULL)
				this_service->notification_period=(char *)strdup(template_service->notification_period);
			this_service->have_notification_period=TRUE;
		        }
		if(this_service->have_contact_groups==FALSE && template_service->have_contact_groups==TRUE){
			if(this_service->contact_groups==NULL && template_service->contact_groups!=NULL)
				this_service->contact_groups=(char *)strdup(template_service->contact_groups);
			this_service->have_contact_groups=TRUE;
		        }
		if(this_service->have_failure_prediction_options==FALSE && template_service->have_failure_prediction_options==TRUE){
			if(this_service->failure_prediction_options==NULL && template_service->failure_prediction_options!=NULL)
				this_service->failure_prediction_options=(char *)strdup(template_service->failure_prediction_options);
			this_service->have_failure_prediction_options=TRUE;
		        }
		if(this_service->have_notes==FALSE && template_service->have_notes==TRUE){
			if(this_service->notes==NULL && template_service->notes!=NULL)
				this_service->notes=(char *)strdup(template_service->notes);
			this_service->have_notes=TRUE;
		        }
		if(this_service->have_notes_url==FALSE && template_service->have_notes_url==TRUE){
			if(this_service->notes_url==NULL && template_service->notes_url!=NULL)
				this_service->notes_url=(char *)strdup(template_service->notes_url);
			this_service->have_notes_url=TRUE;
		        }
		if(this_service->have_action_url==FALSE && template_service->have_action_url==TRUE){
			if(this_service->action_url==NULL && template_service->action_url!=NULL)
				this_service->action_url=(char *)strdup(template_service->action_url);
			this_service->have_action_url=TRUE;
		        }
		if(this_service->have_icon_image==FALSE && template_service->have_icon_image==TRUE){
			if(this_service->icon_image==NULL && template_service->icon_image!=NULL)
				this_service->icon_image=(char *)strdup(template_service->icon_image);
			this_service->have_icon_image=TRUE;
		        }
		if(this_service->have_icon_image_alt==FALSE && template_service->have_icon_image_alt==TRUE){
			if(this_service->icon_image_alt==NULL && template_service->icon_image_alt!=NULL)
				this_service->icon_image_alt=(char *)strdup(template_service->icon_image_alt);
			this_service->have_icon_image_alt=TRUE;
		        }
		if(this_service->have_max_check_attempts==FALSE && template_service->have_max_check_attempts==TRUE){
			this_service->max_check_attempts=template_service->max_check_attempts;
			this_service->have_max_check_attempts=TRUE;
	                }
		if(this_service->have_normal_check_interval==FALSE && template_service->have_normal_check_interval==TRUE){
			this_service->normal_check_interval=template_service->normal_check_interval;
			this_service->have_normal_check_interval=TRUE;
	                }
		if(this_service->have_retry_check_interval==FALSE && template_service->have_retry_check_interval==TRUE){
			this_service->retry_check_interval=template_service->retry_check_interval;
			this_service->have_retry_check_interval=TRUE;
	                }
		if(this_service->have_active_checks_enabled==FALSE && template_service->have_active_checks_enabled==TRUE){
			this_service->active_checks_enabled=template_service->active_checks_enabled;
			this_service->have_active_checks_enabled=TRUE;
	                }
		if(this_service->have_passive_checks_enabled==FALSE && template_service->have_passive_checks_enabled==TRUE){
			this_service->passive_checks_enabled=template_service->passive_checks_enabled;
			this_service->have_passive_checks_enabled=TRUE;
	                }
		if(this_service->have_parallelize_check==FALSE && template_service->have_parallelize_check==TRUE){
			this_service->parallelize_check=template_service->parallelize_check;
			this_service->have_parallelize_check=TRUE;
	                }
		if(this_service->have_is_volatile==FALSE && template_service->have_is_volatile==TRUE){
			this_service->is_volatile=template_service->is_volatile;
			this_service->have_is_volatile=TRUE;
	                }
		if(this_service->have_obsess_over_service==FALSE && template_service->have_obsess_over_service==TRUE){
			this_service->obsess_over_service=template_service->obsess_over_service;
			this_service->have_obsess_over_service=TRUE;
	                }
		if(this_service->have_event_handler_enabled==FALSE && template_service->have_event_handler_enabled==TRUE){
			this_service->event_handler_enabled=template_service->event_handler_enabled;
			this_service->have_event_handler_enabled=TRUE;
	                }
		if(this_service->have_check_freshness==FALSE && template_service->have_check_freshness==TRUE){
			this_service->check_freshness=template_service->check_freshness;
			this_service->have_check_freshness=TRUE;
	                }
		if(this_service->have_freshness_threshold==FALSE && template_service->have_freshness_threshold==TRUE){
			this_service->freshness_threshold=template_service->freshness_threshold;
			this_service->have_freshness_threshold=TRUE;
	                }
		if(this_service->have_low_flap_threshold==FALSE && template_service->have_low_flap_threshold==TRUE){
			this_service->low_flap_threshold=template_service->low_flap_threshold;
			this_service->have_low_flap_threshold=TRUE;
	                }
		if(this_service->have_high_flap_threshold==FALSE && template_service->have_high_flap_threshold==TRUE){
			this_service->high_flap_threshold=template_service->high_flap_threshold;
			this_service->have_high_flap_threshold=TRUE;
	                }
		if(this_service->have_flap_detection_enabled==FALSE && template_service->have_flap_detection_enabled==TRUE){
			this_service->flap_detection_enabled=template_service->flap_detection_enabled;
			this_service->have_flap_detection_enabled=TRUE;
	                }
		if(this_service->have_flap_detection_options==FALSE && template_service->have_flap_detection_options==TRUE){
			this_service->flap_detection_on_ok=template_service->flap_detection_on_ok;
			this_service->flap_detection_on_unknown=template_service->flap_detection_on_unknown;
			this_service->flap_detection_on_warning=template_service->flap_detection_on_warning;
			this_service->flap_detection_on_critical=template_service->flap_detection_on_critical;
			this_service->have_flap_detection_options=TRUE;
	                }
		if(this_service->have_notification_options==FALSE && template_service->have_notification_options==TRUE){
			this_service->notify_on_unknown=template_service->notify_on_unknown;
			this_service->notify_on_warning=template_service->notify_on_warning;
			this_service->notify_on_critical=template_service->notify_on_critical;
			this_service->notify_on_recovery=template_service->notify_on_recovery;
			this_service->notify_on_flapping=template_service->notify_on_flapping;
			this_service->have_notification_options=TRUE;
	                }
		if(this_service->have_notifications_enabled==FALSE && template_service->have_notifications_enabled==TRUE){
			this_service->notifications_enabled=template_service->notifications_enabled;
			this_service->have_notifications_enabled=TRUE;
	                }
		if(this_service->have_notification_interval==FALSE && template_service->have_notification_interval==TRUE){
			this_service->notification_interval=template_service->notification_interval;
			this_service->have_notification_interval=TRUE;
	                }
		if(this_service->have_first_notification_delay==FALSE && template_service->have_first_notification_delay==TRUE){
			this_service->first_notification_delay=template_service->first_notification_delay;
			this_service->have_first_notification_delay=TRUE;
	                }
		if(this_service->have_stalking_options==FALSE && template_service->have_stalking_options==TRUE){
			this_service->stalk_on_ok=template_service->stalk_on_ok;
			this_service->stalk_on_unknown=template_service->stalk_on_unknown;
			this_service->stalk_on_warning=template_service->stalk_on_warning;
			this_service->stalk_on_critical=template_service->stalk_on_critical;
			this_service->have_stalking_options=TRUE;
	                }
		if(this_service->have_process_perf_data==FALSE && template_service->have_process_perf_data==TRUE){
			this_service->process_perf_data=template_service->process_perf_data;
			this_service->have_process_perf_data=TRUE;
	                }
		if(this_service->have_failure_prediction_enabled==FALSE && template_service->have_failure_prediction_enabled==TRUE){
			this_service->failure_prediction_enabled=template_service->failure_prediction_enabled;
			this_service->have_failure_prediction_enabled=TRUE;
	                }
		if(this_service->have_retain_status_information==FALSE && template_service->have_retain_status_information==TRUE){
			this_service->retain_status_information=template_service->retain_status_information;
			this_service->have_retain_status_information=TRUE;
	                }
		if(this_service->have_retain_nonstatus_information==FALSE && template_service->have_retain_nonstatus_information==TRUE){
			this_service->retain_nonstatus_information=template_service->retain_nonstatus_information;
			this_service->have_retain_nonstatus_information=TRUE;
	                }

		/* apply missing custom variables from template service... */
		for(temp_customvariablesmember=template_service->custom_variables;temp_customvariablesmember!=NULL;temp_customvariablesmember=temp_customvariablesmember->next){

			/* see if this host has a variable by the same name */
			for(this_customvariablesmember=this_service->custom_variables;this_customvariablesmember!=NULL;this_customvariablesmember=this_customvariablesmember->next){
				if(!strcmp(temp_customvariablesmember->variable_name,this_customvariablesmember->variable_name))
					break;
			        }

			/* we didn't find the same variable name, so add a new custom variable */
			if(this_customvariablesmember==NULL)
				xodtemplate_add_custom_variable_to_service(this_service,temp_customvariablesmember->variable_name,temp_customvariablesmember->variable_value);
		        }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_service() end\n");
#endif

	return OK;
        }


/* resolves a hostdependency object */
int xodtemplate_resolve_hostdependency(xodtemplate_hostdependency *this_hostdependency){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_hostdependency *template_hostdependency=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_hostdependency() start\n");
#endif

	/* return if this hostdependency has already been resolved */
	if(this_hostdependency->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_hostdependency->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_hostdependency->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_hostdependency->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_hostdependency=xodtemplate_find_hostdependency(temp_ptr);
		if(template_hostdependency==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in host dependency definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_hostdependency->_config_file),this_hostdependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template hostdependency... */
		xodtemplate_resolve_hostdependency(template_hostdependency);

		/* apply missing properties from template hostdependency... */
		if(this_hostdependency->have_host_name==FALSE && template_hostdependency->have_host_name==TRUE){
			if(this_hostdependency->host_name==NULL && template_hostdependency->host_name!=NULL)
				this_hostdependency->host_name=(char *)strdup(template_hostdependency->host_name);
			this_hostdependency->have_host_name=TRUE;
		        }
		if(this_hostdependency->have_dependent_host_name==FALSE && template_hostdependency->have_dependent_host_name==TRUE){
			if(this_hostdependency->dependent_host_name==NULL && template_hostdependency->dependent_host_name!=NULL)
				this_hostdependency->dependent_host_name=(char *)strdup(template_hostdependency->dependent_host_name);
			this_hostdependency->have_dependent_host_name=TRUE;
		        }
		if(this_hostdependency->have_hostgroup_name==FALSE && template_hostdependency->have_hostgroup_name==TRUE){
			if(this_hostdependency->hostgroup_name==NULL && template_hostdependency->hostgroup_name!=NULL)
				this_hostdependency->hostgroup_name=(char *)strdup(template_hostdependency->hostgroup_name);
			this_hostdependency->have_hostgroup_name=TRUE;
		        }
		if(this_hostdependency->have_dependent_hostgroup_name==FALSE && template_hostdependency->have_dependent_hostgroup_name==TRUE){
			if(this_hostdependency->dependent_hostgroup_name==NULL && template_hostdependency->dependent_hostgroup_name!=NULL)
				this_hostdependency->dependent_hostgroup_name=(char *)strdup(template_hostdependency->dependent_hostgroup_name);
			this_hostdependency->have_dependent_hostgroup_name=TRUE;
		        }
		if(this_hostdependency->have_inherits_parent==FALSE && template_hostdependency->have_inherits_parent==TRUE){
			this_hostdependency->inherits_parent=template_hostdependency->inherits_parent;
			this_hostdependency->have_inherits_parent=TRUE;
	                }
		if(this_hostdependency->have_execution_dependency_options==FALSE && template_hostdependency->have_execution_dependency_options==TRUE){
			this_hostdependency->fail_execute_on_up=template_hostdependency->fail_execute_on_up;
			this_hostdependency->fail_execute_on_down=template_hostdependency->fail_execute_on_down;
			this_hostdependency->fail_execute_on_unreachable=template_hostdependency->fail_execute_on_unreachable;
			this_hostdependency->fail_execute_on_pending=template_hostdependency->fail_execute_on_pending;
			this_hostdependency->have_execution_dependency_options=TRUE;
	                }
		if(this_hostdependency->have_notification_dependency_options==FALSE && template_hostdependency->have_notification_dependency_options==TRUE){
			this_hostdependency->fail_notify_on_up=template_hostdependency->fail_notify_on_up;
			this_hostdependency->fail_notify_on_down=template_hostdependency->fail_notify_on_down;
			this_hostdependency->fail_notify_on_unreachable=template_hostdependency->fail_notify_on_unreachable;
			this_hostdependency->fail_notify_on_pending=template_hostdependency->fail_notify_on_pending;
			this_hostdependency->have_notification_dependency_options=TRUE;
	                }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_hostdependency() end\n");
#endif

	return OK;
        }


/* resolves a hostescalation object */
int xodtemplate_resolve_hostescalation(xodtemplate_hostescalation *this_hostescalation){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_hostescalation *template_hostescalation=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_hostescalation() start\n");
#endif

	/* return if this hostescalation has already been resolved */
	if(this_hostescalation->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_hostescalation->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_hostescalation->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_hostescalation->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_hostescalation=xodtemplate_find_hostescalation(temp_ptr);
		if(template_hostescalation==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in host escalation definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_hostescalation->_config_file),this_hostescalation->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template hostescalation... */
		xodtemplate_resolve_hostescalation(template_hostescalation);

		/* apply missing properties from template hostescalation... */
		if(this_hostescalation->have_host_name==FALSE && template_hostescalation->have_host_name==TRUE){
			if(this_hostescalation->host_name==NULL && template_hostescalation->host_name!=NULL)
				this_hostescalation->host_name=(char *)strdup(template_hostescalation->host_name);
			this_hostescalation->have_host_name=TRUE;
		        }
		if(this_hostescalation->have_hostgroup_name==FALSE && template_hostescalation->have_hostgroup_name==TRUE){
			if(this_hostescalation->hostgroup_name==NULL && template_hostescalation->hostgroup_name!=NULL)
				this_hostescalation->hostgroup_name=(char *)strdup(template_hostescalation->hostgroup_name);
			this_hostescalation->have_hostgroup_name=TRUE;
		        }
		if(this_hostescalation->have_escalation_period==FALSE && template_hostescalation->have_escalation_period==TRUE){
			if(this_hostescalation->escalation_period==NULL && template_hostescalation->escalation_period!=NULL)
				this_hostescalation->escalation_period=(char *)strdup(template_hostescalation->escalation_period);
			this_hostescalation->have_escalation_period=TRUE;
		        }
		if(this_hostescalation->have_contact_groups==FALSE && template_hostescalation->have_contact_groups==TRUE){
			if(this_hostescalation->contact_groups==NULL && template_hostescalation->contact_groups!=NULL)
				this_hostescalation->contact_groups=(char *)strdup(template_hostescalation->contact_groups);
			this_hostescalation->have_contact_groups=TRUE;
		        }
		if(this_hostescalation->have_first_notification==FALSE && template_hostescalation->have_first_notification==TRUE){
			this_hostescalation->first_notification=template_hostescalation->first_notification;
			this_hostescalation->have_first_notification=TRUE;
	                }
		if(this_hostescalation->have_last_notification==FALSE && template_hostescalation->have_last_notification==TRUE){
			this_hostescalation->last_notification=template_hostescalation->last_notification;
			this_hostescalation->have_last_notification=TRUE;
	                }
		if(this_hostescalation->have_notification_interval==FALSE && template_hostescalation->have_notification_interval==TRUE){
			this_hostescalation->notification_interval=template_hostescalation->notification_interval;
			this_hostescalation->have_notification_interval=TRUE;
	                }
		if(this_hostescalation->have_escalation_options==FALSE && template_hostescalation->have_escalation_options==TRUE){
			this_hostescalation->escalate_on_down=template_hostescalation->escalate_on_down;
			this_hostescalation->escalate_on_unreachable=template_hostescalation->escalate_on_unreachable;
			this_hostescalation->escalate_on_recovery=template_hostescalation->escalate_on_recovery;
			this_hostescalation->have_escalation_options=TRUE;
	                }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_hostdependency() end\n");
#endif

	return OK;
        }



/* resolves a hostextinfo object */
int xodtemplate_resolve_hostextinfo(xodtemplate_hostextinfo *this_hostextinfo){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_hostextinfo *template_hostextinfo=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_hostextinfo() start\n");
#endif

	/* return if this object has already been resolved */
	if(this_hostextinfo->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_hostextinfo->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_hostextinfo->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_hostextinfo->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_hostextinfo=xodtemplate_find_hostextinfo(temp_ptr);
		if(template_hostextinfo==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in extended host info definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_hostextinfo->_config_file),this_hostextinfo->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template hostextinfo... */
		xodtemplate_resolve_hostextinfo(template_hostextinfo);

		/* apply missing properties from template hostextinfo... */
		if(this_hostextinfo->have_host_name==FALSE && template_hostextinfo->have_host_name==TRUE){
			if(this_hostextinfo->host_name==NULL && template_hostextinfo->host_name!=NULL)
				this_hostextinfo->host_name=(char *)strdup(template_hostextinfo->host_name);
			this_hostextinfo->have_host_name=TRUE;
		        }
		if(this_hostextinfo->have_hostgroup_name==FALSE && template_hostextinfo->have_hostgroup_name==TRUE){
			if(this_hostextinfo->hostgroup_name==NULL && template_hostextinfo->hostgroup_name!=NULL)
				this_hostextinfo->hostgroup_name=(char *)strdup(template_hostextinfo->hostgroup_name);
			this_hostextinfo->have_hostgroup_name=TRUE;
		        }
		if(this_hostextinfo->have_notes==FALSE && template_hostextinfo->have_notes==TRUE){
			if(this_hostextinfo->notes==NULL && template_hostextinfo->notes!=NULL)
				this_hostextinfo->notes=(char *)strdup(template_hostextinfo->notes);
			this_hostextinfo->have_notes=TRUE;
		        }
		if(this_hostextinfo->have_notes_url==FALSE && template_hostextinfo->have_notes_url==TRUE){
			if(this_hostextinfo->notes_url==NULL && template_hostextinfo->notes_url!=NULL)
				this_hostextinfo->notes_url=(char *)strdup(template_hostextinfo->notes_url);
			this_hostextinfo->have_notes_url=TRUE;
		        }
		if(this_hostextinfo->have_action_url==FALSE && template_hostextinfo->have_action_url==TRUE){
			if(this_hostextinfo->action_url==NULL && template_hostextinfo->action_url!=NULL)
				this_hostextinfo->action_url=(char *)strdup(template_hostextinfo->action_url);
			this_hostextinfo->have_action_url=TRUE;
		        }
		if(this_hostextinfo->have_icon_image==FALSE && template_hostextinfo->have_icon_image==TRUE){
			if(this_hostextinfo->icon_image==NULL && template_hostextinfo->icon_image!=NULL)
				this_hostextinfo->icon_image=(char *)strdup(template_hostextinfo->icon_image);
			this_hostextinfo->have_icon_image=TRUE;
		        }
		if(this_hostextinfo->have_icon_image_alt==FALSE && template_hostextinfo->have_icon_image_alt==TRUE){
			if(this_hostextinfo->icon_image_alt==NULL && template_hostextinfo->icon_image_alt!=NULL)
				this_hostextinfo->icon_image_alt=(char *)strdup(template_hostextinfo->icon_image_alt);
			this_hostextinfo->have_icon_image_alt=TRUE;
		        }
		if(this_hostextinfo->have_vrml_image==FALSE && template_hostextinfo->have_vrml_image==TRUE){
			if(this_hostextinfo->vrml_image==NULL && template_hostextinfo->vrml_image!=NULL)
				this_hostextinfo->vrml_image=(char *)strdup(template_hostextinfo->vrml_image);
			this_hostextinfo->have_vrml_image=TRUE;
		        }
		if(this_hostextinfo->have_statusmap_image==FALSE && template_hostextinfo->have_statusmap_image==TRUE){
			if(this_hostextinfo->statusmap_image==NULL && template_hostextinfo->statusmap_image!=NULL)
				this_hostextinfo->statusmap_image=(char *)strdup(template_hostextinfo->statusmap_image);
			this_hostextinfo->have_statusmap_image=TRUE;
		        }
		if(this_hostextinfo->have_2d_coords==FALSE && template_hostextinfo->have_2d_coords==TRUE){
			this_hostextinfo->x_2d=template_hostextinfo->x_2d;
			this_hostextinfo->y_2d=template_hostextinfo->y_2d;
			this_hostextinfo->have_2d_coords=TRUE;
	                }
		if(this_hostextinfo->have_3d_coords==FALSE && template_hostextinfo->have_3d_coords==TRUE){
			this_hostextinfo->x_3d=template_hostextinfo->x_3d;
			this_hostextinfo->y_3d=template_hostextinfo->y_3d;
			this_hostextinfo->z_3d=template_hostextinfo->z_3d;
			this_hostextinfo->have_3d_coords=TRUE;
	                }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_hostextinfo() end\n");
#endif

	return OK;
        }



/* resolves a serviceextinfo object */
int xodtemplate_resolve_serviceextinfo(xodtemplate_serviceextinfo *this_serviceextinfo){
	char *temp_ptr=NULL;
	char *template_names=NULL;
	xodtemplate_serviceextinfo *template_serviceextinfo=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_resolve_serviceextinfo() start\n");
#endif

	/* return if this object has already been resolved */
	if(this_serviceextinfo->has_been_resolved==TRUE)
		return OK;

	/* set the resolved flag */
	this_serviceextinfo->has_been_resolved=TRUE;

	/* return if we have no template */
	if(this_serviceextinfo->template==NULL)
		return OK;

	if((template_names=(char *)strdup(this_serviceextinfo->template))==NULL)
		return ERROR;

	/* apply all templates */
	for(temp_ptr=strtok(template_names,",");temp_ptr!=NULL;temp_ptr=strtok(NULL,",")){

		template_serviceextinfo=xodtemplate_find_serviceextinfo(temp_ptr);
		if(template_serviceextinfo==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Template '%s' specified in extended service info definition could not be not found (config file '%s', starting on line %d)\n",temp_ptr,xodtemplate_config_file_name(this_serviceextinfo->_config_file),this_serviceextinfo->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&template_names);
			return ERROR;
	                }

		/* resolve the template serviceextinfo... */
		xodtemplate_resolve_serviceextinfo(template_serviceextinfo);

		/* apply missing properties from template serviceextinfo... */
		if(this_serviceextinfo->have_host_name==FALSE && template_serviceextinfo->have_host_name==TRUE){
			if(this_serviceextinfo->host_name==NULL && template_serviceextinfo->host_name!=NULL)
				this_serviceextinfo->host_name=(char *)strdup(template_serviceextinfo->host_name);
			this_serviceextinfo->have_host_name=TRUE;
		        }
		if(this_serviceextinfo->have_hostgroup_name==FALSE && template_serviceextinfo->have_hostgroup_name==TRUE){
		if(this_serviceextinfo->hostgroup_name==NULL && template_serviceextinfo->hostgroup_name!=NULL)
			this_serviceextinfo->hostgroup_name=(char *)strdup(template_serviceextinfo->hostgroup_name);
			this_serviceextinfo->have_hostgroup_name=TRUE;
		        }
		if(this_serviceextinfo->have_service_description==FALSE && template_serviceextinfo->have_service_description==TRUE){
		if(this_serviceextinfo->service_description==NULL && template_serviceextinfo->service_description!=NULL)
			this_serviceextinfo->service_description=(char *)strdup(template_serviceextinfo->service_description);
			this_serviceextinfo->have_service_description=TRUE;
		        }
		if(this_serviceextinfo->have_notes==FALSE && template_serviceextinfo->have_notes==TRUE){
		if(this_serviceextinfo->notes==NULL && template_serviceextinfo->notes!=NULL)
			this_serviceextinfo->notes=(char *)strdup(template_serviceextinfo->notes);
			this_serviceextinfo->have_notes=TRUE;
		        }
		if(this_serviceextinfo->have_notes_url==FALSE && template_serviceextinfo->have_notes_url==TRUE){
		if(this_serviceextinfo->notes_url==NULL && template_serviceextinfo->notes_url!=NULL)
			this_serviceextinfo->notes_url=(char *)strdup(template_serviceextinfo->notes_url);
			this_serviceextinfo->have_notes_url=TRUE;
		        }
		if(this_serviceextinfo->have_action_url==FALSE && template_serviceextinfo->have_action_url==TRUE){
		if(this_serviceextinfo->action_url==NULL && template_serviceextinfo->action_url!=NULL)
			this_serviceextinfo->action_url=(char *)strdup(template_serviceextinfo->action_url);
			this_serviceextinfo->have_action_url=TRUE;
		        }
		if(this_serviceextinfo->have_icon_image==FALSE && template_serviceextinfo->have_icon_image==TRUE){
		if(this_serviceextinfo->icon_image==NULL && template_serviceextinfo->icon_image!=NULL)
			this_serviceextinfo->icon_image=(char *)strdup(template_serviceextinfo->icon_image);
			this_serviceextinfo->have_icon_image=TRUE;
		        }
		if(this_serviceextinfo->have_icon_image_alt==FALSE && template_serviceextinfo->have_icon_image_alt==TRUE){
		if(this_serviceextinfo->icon_image_alt==NULL && template_serviceextinfo->icon_image_alt!=NULL)
			this_serviceextinfo->icon_image_alt=(char *)strdup(template_serviceextinfo->icon_image_alt);
			this_serviceextinfo->have_icon_image_alt=TRUE;
		        }
	        }

	my_free((void **)&template_names);

#ifdef DEBUG0
	printf("xodtemplate_resolve_serviceextinfo() end\n");
#endif

	return OK;
        }

#endif



/******************************************************************/
/*************** OBJECT RECOMBOBULATION FUNCTIONS *****************/
/******************************************************************/

#ifdef NSCORE


/* recombobulates contactgroup definitions */
int xodtemplate_recombobulate_contactgroups(void){
	xodtemplate_contact *temp_contact=NULL;
	xodtemplate_contactgroup *temp_contactgroup=NULL;
	xodtemplate_contactlist *temp_contactlist=NULL;
	xodtemplate_contactlist *this_contactlist=NULL;
	char *contactgroup_names=NULL;
	char *temp_ptr=NULL;
	char *new_members=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_recombobulate_contactgroups() start\n");
#endif

	/* This should happen before we expand contactgroup members, to avoid duplicate contact memberships 01/07/2006 EG */
	/* process all contacts that have contactgroup directives */
	for(temp_contact=xodtemplate_contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){

		/* skip contacts without contactgroup directives or contact names */
		if(temp_contact->contact_groups==NULL || temp_contact->contact_name==NULL)
			continue;

		/* process the list of contactgroups */
		if((contactgroup_names=(char *)strdup(temp_contact->contact_groups))==NULL)
			continue;
		for(temp_ptr=strtok(contactgroup_names,",");temp_ptr;temp_ptr=strtok(NULL,",")){

			/* strip trailing spaces */
			strip(temp_ptr);
			
			/* find the contactgroup */
			temp_contactgroup=xodtemplate_find_real_contactgroup(temp_ptr);
			if(temp_contactgroup==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not find contactgroup '%s' specified in contact '%s' definition (config file '%s', starting on line %d)\n",temp_ptr,temp_contact->contact_name,xodtemplate_config_file_name(temp_contact->_config_file),temp_contact->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				my_free((void **)&contactgroup_names);
				return ERROR;
			        }

			/* add this contact to the contactgroup members directive */
			if(temp_contactgroup->members==NULL)
				temp_contactgroup->members=(char *)strdup(temp_contact->contact_name);
			else{
				new_members=(char *)realloc(temp_contactgroup->members,strlen(temp_contactgroup->members)+strlen(temp_contact->contact_name)+2);
				if(new_members!=NULL){
					temp_contactgroup->members=new_members;
					strcat(temp_contactgroup->members,",");
					strcat(temp_contactgroup->members,temp_contact->contact_name);
				        }
			        }
		        }

		/* free memory */
		my_free((void **)&contactgroup_names);
	        }


	/* expand members of all contactgroups - this could be done in xodtemplate_register_contactgroup(), but we can save the CGIs some work if we do it here */
	for(temp_contactgroup=xodtemplate_contactgroup_list;temp_contactgroup;temp_contactgroup=temp_contactgroup->next){

		if(temp_contactgroup->members==NULL)
			continue;

		/* get list of contacts in the contactgroup */
		temp_contactlist=xodtemplate_expand_contacts(temp_contactgroup->members);

		/* add all members to the contact group */
		if(temp_contactlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand member contacts specified in contactgroup (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_contactgroup->_config_file),temp_contactgroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
	                }
		my_free((void **)&temp_contactgroup->members);
		for(this_contactlist=temp_contactlist;this_contactlist;this_contactlist=this_contactlist->next){

			/* add this contact to the contactgroup members directive */
			if(temp_contactgroup->members==NULL)
				temp_contactgroup->members=(char *)strdup(this_contactlist->contact_name);
			else{
				new_members=(char *)realloc(temp_contactgroup->members,strlen(temp_contactgroup->members)+strlen(this_contactlist->contact_name)+2);
				if(new_members!=NULL){
					temp_contactgroup->members=new_members;
					strcat(temp_contactgroup->members,",");
					strcat(temp_contactgroup->members,this_contactlist->contact_name);
				        }
			        }
	                }
		xodtemplate_free_contactlist(temp_contactlist);
	        }

#ifdef DEBUG0
	printf("xodtemplate_recombobulate_contactgroups() end\n");
#endif

	return OK;
        }



/* recombobulates hostgroup definitions */
int xodtemplate_recombobulate_hostgroups(void){
	xodtemplate_host *temp_host=NULL;
	xodtemplate_hostgroup *temp_hostgroup=NULL;
	xodtemplate_hostlist *temp_hostlist=NULL;
	xodtemplate_hostlist *this_hostlist=NULL;
	char *hostgroup_names=NULL;
	char *temp_ptr=NULL;
	char *new_members=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_recombobulate_hostgroups() start\n");
#endif

	/* This should happen before we expand hostgroup members, to avoid duplicate host memberships 01/07/2006 EG */
	/* process all hosts that have hostgroup directives */
	for(temp_host=xodtemplate_host_list;temp_host!=NULL;temp_host=temp_host->next){

		/* skip hosts without hostgroup directives or host names */
		if(temp_host->host_groups==NULL || temp_host->host_name==NULL)
			continue;

		/* skip hosts that shouldn't be registered */
		if(temp_host->register_object==FALSE)
			continue;

		/* process the list of hostgroups */
		if((hostgroup_names=(char *)strdup(temp_host->host_groups))==NULL)
			continue;
		for(temp_ptr=strtok(hostgroup_names,",");temp_ptr;temp_ptr=strtok(NULL,",")){

			/* strip trailing spaces */
			strip(temp_ptr);
			
			/* find the hostgroup */
			temp_hostgroup=xodtemplate_find_real_hostgroup(temp_ptr);
			if(temp_hostgroup==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not find hostgroup '%s' specified in host '%s' definition (config file '%s', starting on line %d)\n",temp_ptr,temp_host->host_name,xodtemplate_config_file_name(temp_host->_config_file),temp_host->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				my_free((void **)&hostgroup_names);
				return ERROR;
			        }

			/* add this list to the hostgroup members directive */
			if(temp_hostgroup->members==NULL)
				temp_hostgroup->members=(char *)strdup(temp_host->host_name);
			else{
				new_members=(char *)realloc(temp_hostgroup->members,strlen(temp_hostgroup->members)+strlen(temp_host->host_name)+2);
				if(new_members!=NULL){
					temp_hostgroup->members=new_members;
					strcat(temp_hostgroup->members,",");
					strcat(temp_hostgroup->members,temp_host->host_name);
				        }
			        }
		        }

		/* free memory */
		my_free((void **)&hostgroup_names);
	        }


	/* expand members of all hostgroups - this could be done in xodtemplate_register_hostgroup(), but we can save the CGIs some work if we do it here */
	for(temp_hostgroup=xodtemplate_hostgroup_list;temp_hostgroup;temp_hostgroup=temp_hostgroup->next){

		if(temp_hostgroup->members==NULL)
			continue;

		/* skip hostgroups that shouldn't be registered */
		if(temp_hostgroup->register_object==FALSE)
			continue;

		/* get list of hosts in the hostgroup */
		temp_hostlist=xodtemplate_expand_hostgroups_and_hosts(NULL,temp_hostgroup->members);

		/* add all members to the host group */
		if(temp_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand member hosts specified in hostgroup (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_hostgroup->_config_file),temp_hostgroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
	                }
		my_free((void **)&temp_hostgroup->members);
		for(this_hostlist=temp_hostlist;this_hostlist;this_hostlist=this_hostlist->next){

			/* add this host to the hostgroup members directive */
			if(temp_hostgroup->members==NULL)
				temp_hostgroup->members=(char *)strdup(this_hostlist->host_name);
			else{
				new_members=(char *)realloc(temp_hostgroup->members,strlen(temp_hostgroup->members)+strlen(this_hostlist->host_name)+2);
				if(new_members!=NULL){
					temp_hostgroup->members=new_members;
					strcat(temp_hostgroup->members,",");
					strcat(temp_hostgroup->members,this_hostlist->host_name);
				        }
			        }
	                }
		xodtemplate_free_hostlist(temp_hostlist);
	        }

	/* include (sub)hostgroup members */
	for(temp_hostgroup=xodtemplate_hostgroup_list;temp_hostgroup;temp_hostgroup=temp_hostgroup->next){

		if(temp_hostgroup->hostgroup_members==NULL)
			continue;

		/* skip hostgroups that shouldn't be registered */
		if(temp_hostgroup->register_object==FALSE)
			continue;

		/* get list of hosts in the (sub)hostgroup */
		temp_hostlist=xodtemplate_expand_hostgroups_and_hosts(temp_hostgroup->hostgroup_members,NULL);

		/* add all members to the host group */
		if(temp_hostlist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand member hostgroups '%s' specified in hostgroup (config file '%s', starting on line %d)\n",temp_hostgroup->hostgroup_members,xodtemplate_config_file_name(temp_hostgroup->_config_file),temp_hostgroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
	                }
		for(this_hostlist=temp_hostlist;this_hostlist;this_hostlist=this_hostlist->next){

			/* add this host to the hostgroup members directive */
			if(temp_hostgroup->members==NULL)
				temp_hostgroup->members=(char *)strdup(this_hostlist->host_name);
			else{
				new_members=(char *)realloc(temp_hostgroup->members,strlen(temp_hostgroup->members)+strlen(this_hostlist->host_name)+2);
				if(new_members!=NULL){
					temp_hostgroup->members=new_members;
					strcat(temp_hostgroup->members,",");
					strcat(temp_hostgroup->members,this_hostlist->host_name);
				        }
			        }
	                }
		xodtemplate_free_hostlist(temp_hostlist);
	        }

#ifdef DEBUG0
	printf("xodtemplate_recombobulate_hostgroups() end\n");
#endif

	return OK;
        }


/* recombobulates servicegroup definitions */
/***** THIS NEEDS TO BE CALLED AFTER OBJECTS (SERVICES) ARE RESOLVED AND DUPLICATED *****/
int xodtemplate_recombobulate_servicegroups(void){
	xodtemplate_service *temp_service=NULL;
	xodtemplate_servicegroup *temp_servicegroup=NULL;
	xodtemplate_servicelist *temp_servicelist=NULL;
	xodtemplate_servicelist *this_servicelist=NULL;
	char *servicegroup_names=NULL;
	char *member_names=NULL;
	char *host_name=NULL;
	char *service_description=NULL;
	char *temp_ptr=NULL;
	char *temp_ptr2=NULL;
	char *new_members=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_recombobulate_servicegroups() start\n");
#endif

	/* This should happen before we expand servicegroup members, to avoid duplicate service memberships 01/07/2006 EG */
	/* process all services that have servicegroup directives */
	for(temp_service=xodtemplate_service_list;temp_service!=NULL;temp_service=temp_service->next){

		/* skip services without servicegroup directives or service names */
		if(temp_service->service_groups==NULL || temp_service->host_name==NULL || temp_service->service_description==NULL)
			continue;

		/* skip services that shouldn't be registered */
		if(temp_service->register_object==FALSE)
			continue;

		/* process the list of servicegroups */
		if((servicegroup_names=(char *)strdup(temp_service->service_groups))==NULL)
			continue;
		for(temp_ptr=strtok(servicegroup_names,",");temp_ptr;temp_ptr=strtok(NULL,",")){

			/* strip trailing spaces */
			strip(temp_ptr);
			
			/* find the servicegroup */
			temp_servicegroup=xodtemplate_find_real_servicegroup(temp_ptr);
			if(temp_servicegroup==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not find servicegroup '%s' specified in service '%s' on host '%s' definition (config file '%s', starting on line %d)\n",temp_ptr,temp_service->service_description,temp_service->host_name,xodtemplate_config_file_name(temp_service->_config_file),temp_service->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				my_free((void **)&servicegroup_names);
				return ERROR;
			        }

			/* add this list to the servicegroup members directive */
			if(temp_servicegroup->members==NULL){
				temp_servicegroup->members=(char *)malloc(strlen(temp_service->host_name)+strlen(temp_service->service_description)+2);
				if(temp_servicegroup->members!=NULL){
					strcpy(temp_servicegroup->members,temp_service->host_name);
					strcat(temp_servicegroup->members,",");
					strcat(temp_servicegroup->members,temp_service->service_description);
				        }
			        }
			else{
				new_members=(char *)realloc(temp_servicegroup->members,strlen(temp_servicegroup->members)+strlen(temp_service->host_name)+strlen(temp_service->service_description)+3);
				if(new_members!=NULL){
					temp_servicegroup->members=new_members;
					strcat(temp_servicegroup->members,",");
					strcat(temp_servicegroup->members,temp_service->host_name);
					strcat(temp_servicegroup->members,",");
					strcat(temp_servicegroup->members,temp_service->service_description);
				        }
			        }
		        }
	        }


	/* expand members of all servicegroups - this could be done in xodtemplate_register_servicegroup(), but we can save the CGIs some work if we do it here */
	for(temp_servicegroup=xodtemplate_servicegroup_list;temp_servicegroup;temp_servicegroup=temp_servicegroup->next){

		if(temp_servicegroup->members==NULL)
			continue;

		/* skip servicegroups that shouldn't be registered */
		if(temp_servicegroup->register_object==FALSE)
			continue;

		member_names=temp_servicegroup->members;
		temp_servicegroup->members=NULL;

		for(temp_ptr=member_names;temp_ptr;temp_ptr=strchr(temp_ptr+1,',')){

			/* this is the host name */
			if(host_name==NULL)
				host_name=(char *)strdup((temp_ptr[0]==',')?temp_ptr+1:temp_ptr);

			/* this is the service description */
			else{
				service_description=(char *)strdup(temp_ptr+1);

				/* strsep and strtok cannot be used, as they're used in expand_servicegroups...() */
				temp_ptr2=strchr(host_name,',');
				if(temp_ptr2)
					temp_ptr2[0]='\x0';
				temp_ptr2=strchr(service_description,',');
				if(temp_ptr2)
					temp_ptr2[0]='\x0';

				/* strip trailing spaces */
				strip(host_name);
				strip(service_description);

				/* get list of services in the servicegroup */
				temp_servicelist=xodtemplate_expand_servicegroups_and_services(NULL,host_name,service_description);

				/* add all members to the service group */
				if(temp_servicelist==NULL){
#ifdef NSCORE
					asprintf(&temp_buffer,"Error: Could not expand member services specified in servicegroup (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_servicegroup->_config_file),temp_servicegroup->_start_line);
					write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
					my_free((void **)&temp_buffer);
#endif
					my_free((void **)&member_names);
					my_free((void **)&host_name);
					my_free((void **)&service_description);
					return ERROR;
				        }

				for(this_servicelist=temp_servicelist;this_servicelist;this_servicelist=this_servicelist->next){

					/* add this service to the servicegroup members directive */
					if(temp_servicegroup->members==NULL){
						temp_servicegroup->members=(char *)malloc(strlen(this_servicelist->host_name)+strlen(this_servicelist->service_description)+2);
						if(temp_servicegroup!=NULL){
							strcpy(temp_servicegroup->members,this_servicelist->host_name);
							strcat(temp_servicegroup->members,",");
							strcat(temp_servicegroup->members,this_servicelist->service_description);
						        }
					        }
					else{
						new_members=(char *)realloc(temp_servicegroup->members,strlen(temp_servicegroup->members)+strlen(this_servicelist->host_name)+strlen(this_servicelist->service_description)+3);
						if(new_members!=NULL){
							temp_servicegroup->members=new_members;
							strcat(temp_servicegroup->members,",");
							strcat(temp_servicegroup->members,this_servicelist->host_name);
							strcat(temp_servicegroup->members,",");
							strcat(temp_servicegroup->members,this_servicelist->service_description);
						        }
					        }
				        }
				xodtemplate_free_servicelist(temp_servicelist);

				my_free((void **)&host_name);
				my_free((void **)&service_description);
			        }
		        }

		my_free((void **)&member_names);

		/* error if there were an odd number of items specified (unmatched host/service pair) */
		if(host_name!=NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Servicegroup members must be specified in <host_name>,<service_description> pairs (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_servicegroup->_config_file),temp_servicegroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			my_free((void **)&host_name);
			return ERROR;
		        }
	        }

	/* expand members of (sub)servicegroups */
	for(temp_servicegroup=xodtemplate_servicegroup_list;temp_servicegroup;temp_servicegroup=temp_servicegroup->next){

		if(temp_servicegroup->servicegroup_members==NULL)
			continue;

		/* skip servicegroups that shouldn't be registered */
		if(temp_servicegroup->register_object==FALSE)
			continue;

		/* get list of services in the servicegroup */
		temp_servicelist=xodtemplate_expand_servicegroups_and_services(temp_servicegroup->servicegroup_members,NULL,NULL);

		/* add all members to the service group */
		if(temp_servicelist==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not expand member servicegroups specified in servicegroup (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(temp_servicegroup->_config_file),temp_servicegroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }

		for(this_servicelist=temp_servicelist;this_servicelist;this_servicelist=this_servicelist->next){

			/* add this service to the servicegroup members directive */
			if(temp_servicegroup->members==NULL){
				temp_servicegroup->members=(char *)malloc(strlen(this_servicelist->host_name)+strlen(this_servicelist->service_description)+2);
				if(temp_servicegroup!=NULL){
					strcpy(temp_servicegroup->members,this_servicelist->host_name);
					strcat(temp_servicegroup->members,",");
					strcat(temp_servicegroup->members,this_servicelist->service_description);
				        }
			        }
			else{
				new_members=(char *)realloc(temp_servicegroup->members,strlen(temp_servicegroup->members)+strlen(this_servicelist->host_name)+strlen(this_servicelist->service_description)+3);
				if(new_members!=NULL){
					temp_servicegroup->members=new_members;
					strcat(temp_servicegroup->members,",");
					strcat(temp_servicegroup->members,this_servicelist->host_name);
					strcat(temp_servicegroup->members,",");
					strcat(temp_servicegroup->members,this_servicelist->service_description);
				        }
			        }
		        }

		xodtemplate_free_servicelist(temp_servicelist);
	        }

#ifdef DEBUG0
	printf("xodtemplate_recombobulate_servicegroups() end\n");
#endif

	return OK;
        }

#endif



/******************************************************************/
/******************* OBJECT SEARCH FUNCTIONS **********************/
/******************************************************************/

#ifdef NSCORE

/* finds a specific timeperiod object */
xodtemplate_timeperiod *xodtemplate_find_timeperiod(char *name){
	xodtemplate_timeperiod *temp_timeperiod=NULL;

	if(name==NULL)
		return NULL;

	for(temp_timeperiod=xodtemplate_timeperiod_list;temp_timeperiod!=NULL;temp_timeperiod=temp_timeperiod->next){
		if(temp_timeperiod->name==NULL)
			continue;
		if(!strcmp(temp_timeperiod->name,name))
			break;
	        }

	return temp_timeperiod;
        }


/* finds a specific command object */
xodtemplate_command *xodtemplate_find_command(char *name){
	xodtemplate_command *temp_command=NULL;

	if(name==NULL)
		return NULL;

	for(temp_command=xodtemplate_command_list;temp_command!=NULL;temp_command=temp_command->next){
		if(temp_command->name==NULL)
			continue;
		if(!strcmp(temp_command->name,name))
			break;
	        }

	return temp_command;
        }


/* finds a specific contactgroup object */
xodtemplate_contactgroup *xodtemplate_find_contactgroup(char *name){
	xodtemplate_contactgroup *temp_contactgroup=NULL;

	if(name==NULL)
		return NULL;

	for(temp_contactgroup=xodtemplate_contactgroup_list;temp_contactgroup!=NULL;temp_contactgroup=temp_contactgroup->next){
		if(temp_contactgroup->name==NULL)
			continue;
		if(!strcmp(temp_contactgroup->name,name))
			break;
	        }

	return temp_contactgroup;
        }


/* finds a specific contactgroup object by its REAL name, not its TEMPLATE name */
xodtemplate_contactgroup *xodtemplate_find_real_contactgroup(char *name){
	xodtemplate_contactgroup *temp_contactgroup=NULL;

	if(name==NULL)
		return NULL;

	for(temp_contactgroup=xodtemplate_contactgroup_list;temp_contactgroup!=NULL;temp_contactgroup=temp_contactgroup->next){
		if(temp_contactgroup->register_object==FALSE)
			continue;
		if(temp_contactgroup->contactgroup_name==NULL)
			continue;
		if(!strcmp(temp_contactgroup->contactgroup_name,name))
			break;
	        }

	return temp_contactgroup;
        }


/* finds a specific hostgroup object */
xodtemplate_hostgroup *xodtemplate_find_hostgroup(char *name){
	xodtemplate_hostgroup *temp_hostgroup=NULL;

	if(name==NULL)
		return NULL;

	for(temp_hostgroup=xodtemplate_hostgroup_list;temp_hostgroup!=NULL;temp_hostgroup=temp_hostgroup->next){
		if(temp_hostgroup->name==NULL)
			continue;
		if(!strcmp(temp_hostgroup->name,name))
			break;
	        }

	return temp_hostgroup;
        }


/* finds a specific hostgroup object by its REAL name, not its TEMPLATE name */
xodtemplate_hostgroup *xodtemplate_find_real_hostgroup(char *name){
	xodtemplate_hostgroup *temp_hostgroup=NULL;

	if(name==NULL)
		return NULL;

	for(temp_hostgroup=xodtemplate_hostgroup_list;temp_hostgroup!=NULL;temp_hostgroup=temp_hostgroup->next){
		if(temp_hostgroup->register_object==FALSE)
			continue;
		if(temp_hostgroup->hostgroup_name==NULL)
			continue;
		if(!strcmp(temp_hostgroup->hostgroup_name,name))
			break;
	        }

	return temp_hostgroup;
        }


/* finds a specific servicegroup object */
xodtemplate_servicegroup *xodtemplate_find_servicegroup(char *name){
	xodtemplate_servicegroup *temp_servicegroup=NULL;

	if(name==NULL)
		return NULL;

	for(temp_servicegroup=xodtemplate_servicegroup_list;temp_servicegroup!=NULL;temp_servicegroup=temp_servicegroup->next){
		if(temp_servicegroup->name==NULL)
			continue;
		if(!strcmp(temp_servicegroup->name,name))
			break;
	        }

	return temp_servicegroup;
        }


/* finds a specific servicegroup object by its REAL name, not its TEMPLATE name */
xodtemplate_servicegroup *xodtemplate_find_real_servicegroup(char *name){
	xodtemplate_servicegroup *temp_servicegroup=NULL;

	if(name==NULL)
		return NULL;

	for(temp_servicegroup=xodtemplate_servicegroup_list;temp_servicegroup!=NULL;temp_servicegroup=temp_servicegroup->next){
		if(temp_servicegroup->register_object==FALSE)
			continue;
		if(temp_servicegroup->servicegroup_name==NULL)
			continue;
		if(!strcmp(temp_servicegroup->servicegroup_name,name))
			break;
	        }

	return temp_servicegroup;
        }


/* finds a specific servicedependency object */
xodtemplate_servicedependency *xodtemplate_find_servicedependency(char *name){
	xodtemplate_servicedependency *temp_servicedependency=NULL;

	if(name==NULL)
		return NULL;

	for(temp_servicedependency=xodtemplate_servicedependency_list;temp_servicedependency!=NULL;temp_servicedependency=temp_servicedependency->next){
		if(temp_servicedependency->name==NULL)
			continue;
		if(!strcmp(temp_servicedependency->name,name))
			break;
	        }

	return temp_servicedependency;
        }


/* finds a specific serviceescalation object */
xodtemplate_serviceescalation *xodtemplate_find_serviceescalation(char *name){
	xodtemplate_serviceescalation *temp_serviceescalation=NULL;

	if(name==NULL)
		return NULL;

	for(temp_serviceescalation=xodtemplate_serviceescalation_list;temp_serviceescalation!=NULL;temp_serviceescalation=temp_serviceescalation->next){
		if(temp_serviceescalation->name==NULL)
			continue;
		if(!strcmp(temp_serviceescalation->name,name))
			break;
	        }

	return temp_serviceescalation;
        }


/* finds a specific contact object */
xodtemplate_contact *xodtemplate_find_contact(char *name){
	xodtemplate_contact *temp_contact=NULL;

	if(name==NULL)
		return NULL;

	for(temp_contact=xodtemplate_contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){
		if(temp_contact->name==NULL)
			continue;
		if(!strcmp(temp_contact->name,name))
			break;
	        }

	return temp_contact;
        }

/* finds a specific contact object by its REAL name, not its TEMPLATE name */
xodtemplate_contact *xodtemplate_find_real_contact(char *name){
	xodtemplate_contact *temp_contact=NULL;

	if(name==NULL)
		return NULL;

	for(temp_contact=xodtemplate_contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){
		if(temp_contact->register_object==FALSE)
			continue;
		if(temp_contact->contact_name==NULL)
			continue;
		if(!strcmp(temp_contact->contact_name,name))
			break;
	        }

	return temp_contact;
        }


/* finds a specific host object */
xodtemplate_host *xodtemplate_find_host(char *name){
	xodtemplate_host *temp_host=NULL;

	if(name==NULL)
		return NULL;

	for(temp_host=xodtemplate_host_list;temp_host!=NULL;temp_host=temp_host->next){
		if(temp_host->name==NULL)
			continue;
		if(!strcmp(temp_host->name,name))
			break;
	        }

	return temp_host;
        }


/* finds a specific host object by its REAL name, not its TEMPLATE name */
xodtemplate_host *xodtemplate_find_real_host(char *name){
	xodtemplate_host *temp_host=NULL;

	if(name==NULL)
		return NULL;

	for(temp_host=xodtemplate_host_list;temp_host!=NULL;temp_host=temp_host->next){
		if(temp_host->register_object==FALSE)
			continue;
		if(temp_host->host_name==NULL)
			continue;
		if(!strcmp(temp_host->host_name,name))
			break;
	        }

	return temp_host;
        }


/* finds a specific hostdependency object */
xodtemplate_hostdependency *xodtemplate_find_hostdependency(char *name){
	xodtemplate_hostdependency *temp_hostdependency=NULL;

	if(name==NULL)
		return NULL;

	for(temp_hostdependency=xodtemplate_hostdependency_list;temp_hostdependency!=NULL;temp_hostdependency=temp_hostdependency->next){
		if(temp_hostdependency->name==NULL)
			continue;
		if(!strcmp(temp_hostdependency->name,name))
			break;
	        }

	return temp_hostdependency;
        }


/* finds a specific hostescalation object */
xodtemplate_hostescalation *xodtemplate_find_hostescalation(char *name){
	xodtemplate_hostescalation *temp_hostescalation=NULL;

	if(name==NULL)
		return NULL;

	for(temp_hostescalation=xodtemplate_hostescalation_list;temp_hostescalation!=NULL;temp_hostescalation=temp_hostescalation->next){
		if(temp_hostescalation->name==NULL)
			continue;
		if(!strcmp(temp_hostescalation->name,name))
			break;
	        }

	return temp_hostescalation;
        }


/* finds a specific hostextinfo object */
xodtemplate_hostextinfo *xodtemplate_find_hostextinfo(char *name){
	xodtemplate_hostextinfo *temp_hostextinfo=NULL;

	if(name==NULL)
		return NULL;

	for(temp_hostextinfo=xodtemplate_hostextinfo_list;temp_hostextinfo!=NULL;temp_hostextinfo=temp_hostextinfo->next){
		if(temp_hostextinfo->name==NULL)
			continue;
		if(!strcmp(temp_hostextinfo->name,name))
			break;
	        }

	return temp_hostextinfo;
        }


/* finds a specific serviceextinfo object */
xodtemplate_serviceextinfo *xodtemplate_find_serviceextinfo(char *name){
	xodtemplate_serviceextinfo *temp_serviceextinfo=NULL;

	if(name==NULL)
		return NULL;

	for(temp_serviceextinfo=xodtemplate_serviceextinfo_list;temp_serviceextinfo!=NULL;temp_serviceextinfo=temp_serviceextinfo->next){
		if(temp_serviceextinfo->name==NULL)
			continue;
		if(!strcmp(temp_serviceextinfo->name,name))
			break;
	        }

	return temp_serviceextinfo;
        }


/* finds a specific service object */
xodtemplate_service *xodtemplate_find_service(char *name){
	xodtemplate_service *temp_service=NULL;

	for(temp_service=xodtemplate_service_list;temp_service!=NULL;temp_service=temp_service->next){
		if(temp_service->name==NULL)
			continue;
		if(!strcmp(temp_service->name,name))
			break;
	        }

	return temp_service;
        }


/* finds a specific service object by its REAL name, not its TEMPLATE name */
xodtemplate_service *xodtemplate_find_real_service(char *host_name, char *service_description){
	xodtemplate_service *temp_service=NULL;

	if(host_name==NULL || service_description==NULL || xodtemplate_service_list==NULL)
		return NULL;


	for(temp_service=xodtemplate_service_list;temp_service!=NULL;temp_service=temp_service->next){
		if(temp_service->register_object==FALSE)
			continue;
		if(temp_service->host_name==NULL || temp_service->service_description==NULL)
			continue;
		if(!strcmp(host_name,temp_service->host_name) && !strcmp(service_description,temp_service->service_description))
			break;
	        }

	return temp_service;
        }

#endif



/******************************************************************/
/**************** OBJECT REGISTRATION FUNCTIONS *******************/
/******************************************************************/

/* registers object definitions */
int xodtemplate_register_objects(void){
	int result=OK;
	xodtemplate_timeperiod *temp_timeperiod=NULL;
	xodtemplate_command *temp_command=NULL;
	xodtemplate_contactgroup *temp_contactgroup=NULL;
	xodtemplate_hostgroup *temp_hostgroup=NULL;
	xodtemplate_servicegroup *temp_servicegroup=NULL;
	xodtemplate_contact *temp_contact=NULL;
	xodtemplate_host *temp_host=NULL;
	xodtemplate_service *temp_service=NULL;
	xodtemplate_servicedependency *temp_servicedependency=NULL;
	xodtemplate_serviceescalation *temp_serviceescalation=NULL;
	xodtemplate_hostdependency *temp_hostdependency=NULL;
	xodtemplate_hostescalation *temp_hostescalation=NULL;
	xodtemplate_hostextinfo *temp_hostextinfo=NULL;
	xodtemplate_serviceextinfo *temp_serviceextinfo=NULL;

#ifdef DEBUG0
	printf("xodtemplate_register_objects() start\n");
#endif

	/* register timeperiods */
	for(temp_timeperiod=xodtemplate_timeperiod_list;temp_timeperiod!=NULL;temp_timeperiod=temp_timeperiod->next){
		if((result=xodtemplate_register_timeperiod(temp_timeperiod))==ERROR)
			return ERROR;
	        }

	/* register commands */
	for(temp_command=xodtemplate_command_list;temp_command!=NULL;temp_command=temp_command->next){
		if((result=xodtemplate_register_command(temp_command))==ERROR)
			return ERROR;
	        }

	/* register contactgroups */
	for(temp_contactgroup=xodtemplate_contactgroup_list;temp_contactgroup!=NULL;temp_contactgroup=temp_contactgroup->next){
		if((result=xodtemplate_register_contactgroup(temp_contactgroup))==ERROR)
			return ERROR;
	        }

	/* register hostgroups */
	for(temp_hostgroup=xodtemplate_hostgroup_list;temp_hostgroup!=NULL;temp_hostgroup=temp_hostgroup->next){
		if((result=xodtemplate_register_hostgroup(temp_hostgroup))==ERROR)
			return ERROR;
	        }

	/* register servicegroups */
	for(temp_servicegroup=xodtemplate_servicegroup_list;temp_servicegroup!=NULL;temp_servicegroup=temp_servicegroup->next){
		if((result=xodtemplate_register_servicegroup(temp_servicegroup))==ERROR)
			return ERROR;
	        }

	/* register contacts */
	for(temp_contact=xodtemplate_contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){
		if((result=xodtemplate_register_contact(temp_contact))==ERROR)
			return ERROR;
	        }

	/* register hosts */
	for(temp_host=xodtemplate_host_list;temp_host!=NULL;temp_host=temp_host->next){
		if((result=xodtemplate_register_host(temp_host))==ERROR)
			return ERROR;
	        }

	/* register services */
	for(temp_service=xodtemplate_service_list;temp_service!=NULL;temp_service=temp_service->next){
		if((result=xodtemplate_register_service(temp_service))==ERROR)
			return ERROR;
	        }

	/* register service dependencies */
	for(temp_servicedependency=xodtemplate_servicedependency_list;temp_servicedependency!=NULL;temp_servicedependency=temp_servicedependency->next){
		if((result=xodtemplate_register_servicedependency(temp_servicedependency))==ERROR)
			return ERROR;
	        }

	/* register service escalations */
	for(temp_serviceescalation=xodtemplate_serviceescalation_list;temp_serviceescalation!=NULL;temp_serviceescalation=temp_serviceescalation->next){
		if((result=xodtemplate_register_serviceescalation(temp_serviceescalation))==ERROR)
			return ERROR;
	        }

	/* register host dependencies */
	for(temp_hostdependency=xodtemplate_hostdependency_list;temp_hostdependency!=NULL;temp_hostdependency=temp_hostdependency->next){
		if((result=xodtemplate_register_hostdependency(temp_hostdependency))==ERROR)
			return ERROR;
	        }

	/* register host escalations */
	for(temp_hostescalation=xodtemplate_hostescalation_list;temp_hostescalation!=NULL;temp_hostescalation=temp_hostescalation->next){
		if((result=xodtemplate_register_hostescalation(temp_hostescalation))==ERROR)
			return ERROR;
	        }

	/* register host extended info */
	for(temp_hostextinfo=xodtemplate_hostextinfo_list;temp_hostextinfo!=NULL;temp_hostextinfo=temp_hostextinfo->next){
		if((result=xodtemplate_register_hostextinfo(temp_hostextinfo))==ERROR)
			return ERROR;
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_objects() end\n");
#endif

	return OK;
        }



/* registers a timeperiod definition */
int xodtemplate_register_timeperiod(xodtemplate_timeperiod *this_timeperiod){
	timeperiod *new_timeperiod=NULL;
	timerange *new_timerange=NULL;
	int day=0;
	char *day_range_ptr=NULL;
	char *day_range_start_buffer=NULL;
	char *range_ptr=NULL;
	char *range_buffer=NULL;
	char *time_ptr=NULL;
	char *time_buffer=NULL;
	int hours=0;
	int minutes=0;
	unsigned long range_start_time=0L;
	unsigned long range_end_time=0L;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif
	

#ifdef DEBUG0
	printf("xodtemplate_register_timeperiod() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_timeperiod->register_object==FALSE)
		return OK;

	/* add the timeperiod */
	new_timeperiod=add_timeperiod(this_timeperiod->timeperiod_name,this_timeperiod->alias);

	/* return with an error if we couldn't add the timeperiod */
	if(new_timeperiod==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register timeperiod (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_timeperiod->_config_file),this_timeperiod->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* add all necessary timeranges to timeperiod */
	for(day=0;day<7;day++){

		if(this_timeperiod->timeranges[day]==NULL)
			continue;

		day_range_ptr=this_timeperiod->timeranges[day];
		for(day_range_start_buffer=my_strsep(&day_range_ptr,", ");day_range_start_buffer!=NULL;day_range_start_buffer=my_strsep(&day_range_ptr,", ")){

			range_ptr=day_range_start_buffer;
			range_buffer=my_strsep(&range_ptr,"-");
			if(range_buffer==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add timerange for day %d to timeperiod (No start time delimiter) (config file '%s', starting on line %d)\n",day,xodtemplate_config_file_name(this_timeperiod->_config_file),this_timeperiod->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }

			time_ptr=range_buffer;
			time_buffer=my_strsep(&time_ptr,":");
			if(time_buffer==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add timerange for day %d to timeperiod (No start time hours) (config file '%s', starting on line %d)\n",day,xodtemplate_config_file_name(this_timeperiod->_config_file),this_timeperiod->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
			hours=atoi(time_buffer);
			time_buffer=my_strsep(&time_ptr,":");
			if(time_buffer==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add timerange for day %d to timeperiod (No start time minutes) (config file '%s', starting on line %d)\n",day,xodtemplate_config_file_name(this_timeperiod->_config_file),this_timeperiod->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
			minutes=atoi(time_buffer);

			/* calculate the range start time in seconds */
			range_start_time=(unsigned long)((minutes*60)+(hours*60*60));

			range_buffer=my_strsep(&range_ptr,"-");
			if(range_buffer==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add timerange for day %d to timeperiod (No end time delimiter) (config file '%s', starting on line %d)\n",day,xodtemplate_config_file_name(this_timeperiod->_config_file),this_timeperiod->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }

			time_ptr=range_buffer;
			time_buffer=my_strsep(&time_ptr,":");
			if(time_buffer==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add timerange for day %d to timeperiod (No end time hours) (config file '%s', starting on line %d)\n",day,xodtemplate_config_file_name(this_timeperiod->_config_file),this_timeperiod->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
			hours=atoi(time_buffer);

			time_buffer=my_strsep(&time_ptr,":");
			if(time_buffer==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add timerange for day %d to timeperiod (No end time minutes) (config file '%s', starting on line %d)\n",day,xodtemplate_config_file_name(this_timeperiod->_config_file),this_timeperiod->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
			minutes=atoi(time_buffer);

			/* calculate the range end time in seconds */
			range_end_time=(unsigned long)((minutes*60)+(hours*3600));

			/* add the new time range to the time period */
			new_timerange=add_timerange_to_timeperiod(new_timeperiod,day,range_start_time,range_end_time);
			if(new_timerange==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add timerange for day %d to timeperiod (config file '%s', starting on line %d)\n",day,xodtemplate_config_file_name(this_timeperiod->_config_file),this_timeperiod->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
		        }
		
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_timeperiod() end\n");
#endif

	return OK;
        }



/* registers a command definition */
int xodtemplate_register_command(xodtemplate_command *this_command){
	command *new_command=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_command() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_command->register_object==FALSE)
		return OK;

	/* add the command */
	new_command=add_command(this_command->command_name,this_command->command_line);

	/* return with an error if we couldn't add the command */
	if(new_command==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register command (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_command->_config_file),this_command->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
#endif
		return ERROR;
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_command() end\n");
#endif

	return OK;
        }



/* registers a contactgroup definition */
int xodtemplate_register_contactgroup(xodtemplate_contactgroup *this_contactgroup){
	contactgroup *new_contactgroup=NULL;
	contactgroupmember *new_contactgroupmember=NULL;
	char *contact_name=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_contactgroup() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_contactgroup->register_object==FALSE)
		return OK;

	/* add the contact group */
	new_contactgroup=add_contactgroup(this_contactgroup->contactgroup_name,this_contactgroup->alias);

	/* return with an error if we couldn't add the contactgroup */
	if(new_contactgroup==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register contactgroup (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_contactgroup->_config_file),this_contactgroup->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* add all members to the contact group */
	if(this_contactgroup->members==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Contactgroup has no members (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_contactgroup->_config_file),this_contactgroup->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }
	for(contact_name=strtok(this_contactgroup->members,",");contact_name!=NULL;contact_name=strtok(NULL,",")){
		strip(contact_name);
		new_contactgroupmember=add_contact_to_contactgroup(new_contactgroup,contact_name);
		if(new_contactgroupmember==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not add contact '%s' to contactgroup (config file '%s', starting on line %d)\n",contact_name,xodtemplate_config_file_name(this_contactgroup->_config_file),this_contactgroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_contactgroup() end\n");
#endif

	return OK;
        }



/* registers a hostgroup definition */
int xodtemplate_register_hostgroup(xodtemplate_hostgroup *this_hostgroup){
	hostgroup *new_hostgroup=NULL;
	hostgroupmember *new_hostgroupmember=NULL;
	char *host_name=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_hostgroup() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_hostgroup->register_object==FALSE)
		return OK;

	/* add the  host group */
	new_hostgroup=add_hostgroup(this_hostgroup->hostgroup_name,this_hostgroup->alias);

	/* return with an error if we couldn't add the hostgroup */
	if(new_hostgroup==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register hostgroup (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_hostgroup->_config_file),this_hostgroup->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* add all members to hostgroup */
	if(this_hostgroup->members==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Hostgroup has no members (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_hostgroup->_config_file),this_hostgroup->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }
	for(host_name=strtok(this_hostgroup->members,",");host_name!=NULL;host_name=strtok(NULL,",")){
		strip(host_name);
		new_hostgroupmember=add_host_to_hostgroup(new_hostgroup,host_name);
		if(new_hostgroupmember==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not add host '%s' to hostgroup (config file '%s', starting on line %d)\n",host_name,xodtemplate_config_file_name(this_hostgroup->_config_file),this_hostgroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_hostgroup() end\n");
#endif

	return OK;
        }



/* registers a servicegroup definition */
int xodtemplate_register_servicegroup(xodtemplate_servicegroup *this_servicegroup){
	servicegroup *new_servicegroup=NULL;
	servicegroupmember *new_servicegroupmember=NULL;
	char *host_name=NULL;
	char *svc_description=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_servicegroup() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_servicegroup->register_object==FALSE)
		return OK;

	/* add the  service group */
	new_servicegroup=add_servicegroup(this_servicegroup->servicegroup_name,this_servicegroup->alias);

	/* return with an error if we couldn't add the servicegroup */
	if(new_servicegroup==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register servicegroup (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_servicegroup->_config_file),this_servicegroup->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* add all members to servicegroup */
	if(this_servicegroup->members==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Servicegroup has no members (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_servicegroup->_config_file),this_servicegroup->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }
	for(host_name=strtok(this_servicegroup->members,",");host_name!=NULL;host_name=strtok(NULL,",")){
		strip(host_name);
		svc_description=strtok(NULL,",");
		if(svc_description==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Missing service name in servicegroup definition (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_servicegroup->_config_file),this_servicegroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
	                }
		strip(svc_description);

		new_servicegroupmember=add_service_to_servicegroup(new_servicegroup,host_name,svc_description);
		if(new_servicegroupmember==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not add service '%s' on host '%s' to servicegroup (config file '%s', starting on line %d)\n",svc_description,host_name,xodtemplate_config_file_name(this_servicegroup->_config_file),this_servicegroup->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_servicegroup() end\n");
#endif

	return OK;
        }



/* registers a servicedependency definition */
int xodtemplate_register_servicedependency(xodtemplate_servicedependency *this_servicedependency){
	servicedependency *new_servicedependency=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_servicedependency() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_servicedependency->register_object==FALSE)
		return OK;

	/* throw a warning on servicedeps that have no options */
	if(this_servicedependency->have_notification_dependency_options==FALSE && this_servicedependency->have_execution_dependency_options==FALSE){
#ifdef NSCORE
		asprintf(&temp_buffer,"Warning: Ignoring lame service dependency (config file '%s', line %d)\n",xodtemplate_config_file_name(this_servicedependency->_config_file),this_servicedependency->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return OK;
	        }

	/* add the servicedependency */
	if(this_servicedependency->have_execution_dependency_options==TRUE){

		new_servicedependency=add_service_dependency(this_servicedependency->dependent_host_name,this_servicedependency->dependent_service_description,this_servicedependency->host_name,this_servicedependency->service_description,EXECUTION_DEPENDENCY,this_servicedependency->inherits_parent,this_servicedependency->fail_execute_on_ok,this_servicedependency->fail_execute_on_warning,this_servicedependency->fail_execute_on_unknown,this_servicedependency->fail_execute_on_critical,this_servicedependency->fail_execute_on_pending);

		/* return with an error if we couldn't add the servicedependency */
		if(new_servicedependency==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not register service execution dependency (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_servicedependency->_config_file),this_servicedependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
	        }
	if(this_servicedependency->have_notification_dependency_options==TRUE){

		new_servicedependency=add_service_dependency(this_servicedependency->dependent_host_name,this_servicedependency->dependent_service_description,this_servicedependency->host_name,this_servicedependency->service_description,NOTIFICATION_DEPENDENCY,this_servicedependency->inherits_parent,this_servicedependency->fail_notify_on_ok,this_servicedependency->fail_notify_on_warning,this_servicedependency->fail_notify_on_unknown,this_servicedependency->fail_notify_on_critical,this_servicedependency->fail_notify_on_pending);

		/* return with an error if we couldn't add the servicedependency */
		if(new_servicedependency==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not register service notification dependency (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_servicedependency->_config_file),this_servicedependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_servicedependency() end\n");
#endif

	return OK;
        }



/* registers a serviceescalation definition */
int xodtemplate_register_serviceescalation(xodtemplate_serviceescalation *this_serviceescalation){
	serviceescalation *new_serviceescalation=NULL;
	contactgroupsmember *new_contactgroupsmember=NULL;
	char *contact_group=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_serviceescalation() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_serviceescalation->register_object==FALSE)
		return OK;

	/* default options if none specified */
	if(this_serviceescalation->have_escalation_options==FALSE){
		this_serviceescalation->escalate_on_warning=TRUE;
		this_serviceescalation->escalate_on_unknown=TRUE;
		this_serviceescalation->escalate_on_critical=TRUE;
		this_serviceescalation->escalate_on_recovery=TRUE;
	        }

	/* add the serviceescalation */
	new_serviceescalation=add_serviceescalation(this_serviceescalation->host_name,this_serviceescalation->service_description,this_serviceescalation->first_notification,this_serviceescalation->last_notification,this_serviceescalation->notification_interval,this_serviceescalation->escalation_period,this_serviceescalation->escalate_on_warning,this_serviceescalation->escalate_on_unknown,this_serviceescalation->escalate_on_critical,this_serviceescalation->escalate_on_recovery);

	/* return with an error if we couldn't add the serviceescalation */
	if(new_serviceescalation==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register service escalation (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_serviceescalation->_config_file),this_serviceescalation->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* add the contact groups */
	if(this_serviceescalation->contact_groups==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Service escalation has no contact groups (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_serviceescalation->_config_file),this_serviceescalation->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }
	for(contact_group=strtok(this_serviceescalation->contact_groups,", ");contact_group!=NULL;contact_group=strtok(NULL,", ")){
		new_contactgroupsmember=add_contactgroup_to_serviceescalation(new_serviceescalation,contact_group);
		if(new_contactgroupsmember==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not add contactgroup '%s' to service escalation (config file '%s', starting on line %d)\n",contact_group,xodtemplate_config_file_name(this_serviceescalation->_config_file),this_serviceescalation->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_serviceescalation() end\n");
#endif

	return OK;
        }



/* registers a contact definition */
int xodtemplate_register_contact(xodtemplate_contact *this_contact){
	contact *new_contact=NULL;
	char *command_name=NULL;
	commandsmember *new_commandsmember=NULL;
	xodtemplate_customvariablesmember *temp_customvariablesmember=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_contact() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_contact->register_object==FALSE)
		return OK;

	/* add the contact */
	new_contact=add_contact(this_contact->contact_name,this_contact->alias,this_contact->email,this_contact->pager,this_contact->address,this_contact->service_notification_period,this_contact->host_notification_period,this_contact->notify_on_service_recovery,this_contact->notify_on_service_critical,this_contact->notify_on_service_warning,this_contact->notify_on_service_unknown,this_contact->notify_on_service_flapping,this_contact->notify_on_host_recovery,this_contact->notify_on_host_down,this_contact->notify_on_host_unreachable,this_contact->notify_on_host_flapping,this_contact->host_notifications_enabled,this_contact->service_notifications_enabled,this_contact->can_submit_commands,this_contact->retain_status_information,this_contact->retain_nonstatus_information);

	/* return with an error if we couldn't add the contact */
	if(new_contact==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register contact (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_contact->_config_file),this_contact->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* add all the host notification commands */
	if(this_contact->host_notification_commands!=NULL){

		for(command_name=strtok(this_contact->host_notification_commands,", ");command_name!=NULL;command_name=strtok(NULL,", ")){
			new_commandsmember=add_host_notification_command_to_contact(new_contact,command_name);
			if(new_commandsmember==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add host notification command '%s' to contact (config file '%s', starting on line %d)\n",command_name,xodtemplate_config_file_name(this_contact->_config_file),this_contact->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
		        }
	        }

	/* add all the service notification commands */
	if(this_contact->service_notification_commands!=NULL){

		for(command_name=strtok(this_contact->service_notification_commands,", ");command_name!=NULL;command_name=strtok(NULL,", ")){
			new_commandsmember=add_service_notification_command_to_contact(new_contact,command_name);
			if(new_commandsmember==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add service notification command '%s' to contact (config file '%s', starting on line %d)\n",command_name,xodtemplate_config_file_name(this_contact->_config_file),this_contact->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
		        }
	        }

	/* add all custom variables */
	for(temp_customvariablesmember=this_contact->custom_variables;temp_customvariablesmember!=NULL;temp_customvariablesmember=temp_customvariablesmember->next){
		if((add_custom_variable_to_contact(new_contact,temp_customvariablesmember->variable_name,temp_customvariablesmember->variable_value))==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not custom variable to contact (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_contact->_config_file),this_contact->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_contact() end\n");
#endif

	return OK;
        }



/* registers a host definition */
int xodtemplate_register_host(xodtemplate_host *this_host){
	host *new_host=NULL;
	char *parent_host=NULL;
	hostsmember *new_hostsmember=NULL;
	contactgroupsmember *new_contactgroupsmember=NULL;
	xodtemplate_customvariablesmember *temp_customvariablesmember=NULL;
	char *contact_group=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_host() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_host->register_object==FALSE)
		return OK;

	/* if host has no alias or address, use host name - added 3/11/05 */
	if(this_host->alias==NULL && this_host->host_name!=NULL)
		this_host->alias=(char *)strdup(this_host->host_name);
	if(this_host->address==NULL && this_host->host_name!=NULL)
		this_host->address=(char *)strdup(this_host->host_name);

	/* add the host definition */
	new_host=add_host(this_host->host_name,this_host->display_name,this_host->alias,(this_host->address==NULL)?this_host->host_name:this_host->address,this_host->check_period,this_host->check_interval,this_host->max_check_attempts,this_host->notify_on_recovery,this_host->notify_on_down,this_host->notify_on_unreachable,this_host->notify_on_flapping,this_host->notification_interval,this_host->first_notification_delay,this_host->notification_period,this_host->notifications_enabled,this_host->check_command,this_host->active_checks_enabled,this_host->passive_checks_enabled,this_host->event_handler,this_host->event_handler_enabled,this_host->flap_detection_enabled,this_host->low_flap_threshold,this_host->high_flap_threshold,this_host->flap_detection_on_up,this_host->flap_detection_on_down,this_host->flap_detection_on_unreachable,this_host->stalk_on_up,this_host->stalk_on_down,this_host->stalk_on_unreachable,this_host->process_perf_data,this_host->failure_prediction_enabled,this_host->failure_prediction_options,this_host->check_freshness,this_host->freshness_threshold,this_host->retain_status_information,this_host->retain_nonstatus_information,this_host->obsess_over_host);

#ifdef TEST_DEBUG
	printf("HOST: %s, MAXATTEMPTS: %d, NOTINVERVAL: %d \n",this_host->host_name,this_host->max_check_attempts,this_host->notification_interval);
#endif

	/* return with an error if we couldn't add the host */
	if(new_host==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register host (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_host->_config_file),this_host->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* add the parent hosts */
	if(this_host->parents!=NULL){

		for(parent_host=strtok(this_host->parents,",");parent_host!=NULL;parent_host=strtok(NULL,",")){
			strip(parent_host);
			new_hostsmember=add_parent_host_to_host(new_host,parent_host);
			if(new_hostsmember==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add parent host '%s' to host (config file '%s', starting on line %d)\n",parent_host,xodtemplate_config_file_name(this_host->_config_file),this_host->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
		        }
	        }

	/* add all contact groups to the host */
	if(this_host->contact_groups!=NULL){

		for(contact_group=strtok(this_host->contact_groups,",");contact_group!=NULL;contact_group=strtok(NULL,",")){

			strip(contact_group);
			new_contactgroupsmember=add_contactgroup_to_host(new_host,contact_group);
			if(new_contactgroupsmember==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add contactgroup '%s' to host (config file '%s', starting on line %d)\n",contact_group,xodtemplate_config_file_name(this_host->_config_file),this_host->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
			        }
	                }
	        }

	/* add all custom variables */
	for(temp_customvariablesmember=this_host->custom_variables;temp_customvariablesmember!=NULL;temp_customvariablesmember=temp_customvariablesmember->next){
		if((add_custom_variable_to_host(new_host,temp_customvariablesmember->variable_name,temp_customvariablesmember->variable_value))==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not custom variable to host (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_host->_config_file),this_host->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_host() end\n");
#endif

	return OK;
        }



/* registers a service definition */
int xodtemplate_register_service(xodtemplate_service *this_service){
	service *new_service=NULL;
	contactgroupsmember *new_contactgroupsmember=NULL;
	char *contactgroup_name=NULL;
	xodtemplate_customvariablesmember *temp_customvariablesmember=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_service() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_service->register_object==FALSE)
		return OK;

	/* add the service */
	new_service=add_service(this_service->host_name,this_service->service_description,this_service->display_name,this_service->check_period,this_service->max_check_attempts,this_service->parallelize_check,this_service->passive_checks_enabled,this_service->normal_check_interval,this_service->retry_check_interval,this_service->notification_interval,this_service->first_notification_delay,this_service->notification_period,this_service->notify_on_recovery,this_service->notify_on_unknown,this_service->notify_on_warning,this_service->notify_on_critical,this_service->notify_on_flapping,this_service->notifications_enabled,this_service->is_volatile,this_service->event_handler,this_service->event_handler_enabled,this_service->check_command,this_service->active_checks_enabled,this_service->flap_detection_enabled,this_service->low_flap_threshold,this_service->high_flap_threshold,this_service->flap_detection_on_ok,this_service->flap_detection_on_warning,this_service->flap_detection_on_unknown,this_service->flap_detection_on_critical,this_service->stalk_on_ok,this_service->stalk_on_warning,this_service->stalk_on_unknown,this_service->stalk_on_critical,this_service->process_perf_data,this_service->failure_prediction_enabled,this_service->failure_prediction_options,this_service->check_freshness,this_service->freshness_threshold,this_service->notes,this_service->notes_url,this_service->action_url,this_service->icon_image,this_service->icon_image_alt,this_service->retain_status_information,this_service->retain_nonstatus_information,this_service->obsess_over_service);

	/* return with an error if we couldn't add the service */
	if(new_service==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register service (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_service->_config_file),this_service->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* add all the contact groups to the service */
	if(this_service->contact_groups!=NULL){

		for(contactgroup_name=strtok(this_service->contact_groups,",");contactgroup_name!=NULL;contactgroup_name=strtok(NULL,",")){

			/* add this contactgroup to the service definition */
			strip(contactgroup_name);
			new_contactgroupsmember=add_contactgroup_to_service(new_service,contactgroup_name);

			/* stop adding contact groups if we ran into an error */
			if(new_contactgroupsmember==NULL){
#ifdef NSCORE
				asprintf(&temp_buffer,"Error: Could not add contact group '%s' to service (config file '%s', starting on line %d)\n",contactgroup_name,xodtemplate_config_file_name(this_service->_config_file),this_service->_start_line);
				write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
				my_free((void **)&temp_buffer);
#endif
				return ERROR;
		                }
		        }
	        }

	/* add all custom variables */
	for(temp_customvariablesmember=this_service->custom_variables;temp_customvariablesmember!=NULL;temp_customvariablesmember=temp_customvariablesmember->next){
		if((add_custom_variable_to_service(new_service,temp_customvariablesmember->variable_name,temp_customvariablesmember->variable_value))==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not custom variable to service (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_service->_config_file),this_service->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_service() end\n");
#endif

	return OK;
        }



/* registers a hostdependency definition */
int xodtemplate_register_hostdependency(xodtemplate_hostdependency *this_hostdependency){
	hostdependency *new_hostdependency=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_hostdependency() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_hostdependency->register_object==FALSE)
		return OK;

	/* add the host execution dependency */
	if(this_hostdependency->have_execution_dependency_options==TRUE){

		new_hostdependency=add_host_dependency(this_hostdependency->dependent_host_name,this_hostdependency->host_name,EXECUTION_DEPENDENCY,this_hostdependency->inherits_parent,this_hostdependency->fail_execute_on_up,this_hostdependency->fail_execute_on_down,this_hostdependency->fail_execute_on_unreachable,this_hostdependency->fail_execute_on_pending);

		/* return with an error if we couldn't add the hostdependency */
		if(new_hostdependency==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not register host execution dependency (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_hostdependency->_config_file),this_hostdependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
                        }
	        }

	/* add the host notification dependency */
	if(this_hostdependency->have_notification_dependency_options==TRUE){

		new_hostdependency=add_host_dependency(this_hostdependency->dependent_host_name,this_hostdependency->host_name,NOTIFICATION_DEPENDENCY,this_hostdependency->inherits_parent,this_hostdependency->fail_notify_on_up,this_hostdependency->fail_notify_on_down,this_hostdependency->fail_notify_on_unreachable,this_hostdependency->fail_notify_on_pending);

		/* return with an error if we couldn't add the hostdependency */
		if(new_hostdependency==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not register host notification dependency (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_hostdependency->_config_file),this_hostdependency->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
                        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_hostdependency() end\n");
#endif

	return OK;
        }



/* registers a hostescalation definition */
int xodtemplate_register_hostescalation(xodtemplate_hostescalation *this_hostescalation){
	hostescalation *new_hostescalation=NULL;
	contactgroupsmember *new_contactgroupsmember=NULL;
	char *contact_group=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_hostescalation() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_hostescalation->register_object==FALSE)
		return OK;

	/* default options if none specified */
	if(this_hostescalation->have_escalation_options==FALSE){
		this_hostescalation->escalate_on_down=TRUE;
		this_hostescalation->escalate_on_unreachable=TRUE;
		this_hostescalation->escalate_on_recovery=TRUE;
	        }

	/* add the hostescalation */
	new_hostescalation=add_hostescalation(this_hostescalation->host_name,this_hostescalation->first_notification,this_hostescalation->last_notification,this_hostescalation->notification_interval,this_hostescalation->escalation_period,this_hostescalation->escalate_on_down,this_hostescalation->escalate_on_unreachable,this_hostescalation->escalate_on_recovery);

	/* return with an error if we couldn't add the hostescalation */
	if(new_hostescalation==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register host escalation (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_hostescalation->_config_file),this_hostescalation->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

	/* add the contact groups */
	if(this_hostescalation->contact_groups==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Host escalation has no contact groups (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_hostescalation->_config_file),this_hostescalation->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }
	for(contact_group=strtok(this_hostescalation->contact_groups,", ");contact_group!=NULL;contact_group=strtok(NULL,", ")){
		new_contactgroupsmember=add_contactgroup_to_hostescalation(new_hostescalation,contact_group);
		if(new_contactgroupsmember==NULL){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not add contactgroup '%s' to host escalation (config file '%s', starting on line %d)\n",contact_group,xodtemplate_config_file_name(this_hostescalation->_config_file),this_hostescalation->_start_line);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			return ERROR;
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_hostescalation() end\n");
#endif

	return OK;
        }



/* registers a hostextinfo definition */
int xodtemplate_register_hostextinfo(xodtemplate_hostextinfo *this_hostextinfo){
	hostextinfo *new_hostextinfo=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_register_hostextinfo() start\n");
#endif

	/* bail out if we shouldn't register this object */
	if(this_hostextinfo->register_object==FALSE)
		return OK;

	/* register the extended host object */
	new_hostextinfo=add_hostextinfo(this_hostextinfo->host_name,this_hostextinfo->notes,this_hostextinfo->notes_url,this_hostextinfo->action_url,this_hostextinfo->icon_image,this_hostextinfo->vrml_image,this_hostextinfo->statusmap_image,this_hostextinfo->icon_image_alt,this_hostextinfo->x_2d,this_hostextinfo->y_2d,this_hostextinfo->x_3d,this_hostextinfo->y_3d,this_hostextinfo->z_3d,this_hostextinfo->have_2d_coords,this_hostextinfo->have_3d_coords);

	/* return with an error if we couldn't add the definition */
	if(new_hostextinfo==NULL){
#ifdef NSCORE
		asprintf(&temp_buffer,"Error: Could not register extended host information (config file '%s', starting on line %d)\n",xodtemplate_config_file_name(this_hostextinfo->_config_file),this_hostextinfo->_start_line);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
		my_free((void **)&temp_buffer);
#endif
		return ERROR;
	        }

#ifdef DEBUG0
	printf("xodtemplate_register_hostextinfo() end\n");
#endif

	return OK;
        }




/******************************************************************/
/********************** SORTING FUNCTIONS *************************/
/******************************************************************/

#ifdef NSCORE

/* sorts all objects by name */
int xodtemplate_sort_objects(void){

#ifdef DEBUG0
	printf("xodtemplate_sort_objects() start\n");
#endif

	/* sort timeperiods */
	if(xodtemplate_sort_timeperiods()==ERROR)
		return ERROR;

	/* sort commands */
	if(xodtemplate_sort_commands()==ERROR)
		return ERROR;

	/* sort contactgroups */
	if(xodtemplate_sort_contactgroups()==ERROR)
		return ERROR;

	/* sort hostgroups */
	if(xodtemplate_sort_hostgroups()==ERROR)
		return ERROR;

	/* sort servicegroups */
	if(xodtemplate_sort_servicegroups()==ERROR)
		return ERROR;

	/* sort contacts */
	if(xodtemplate_sort_contacts()==ERROR)
		return ERROR;

	/* sort hosts */
	if(xodtemplate_sort_hosts()==ERROR)
		return ERROR;

	/* sort services */
	if(xodtemplate_sort_services()==ERROR)
		return ERROR;

	/* sort service dependencies */
	if(xodtemplate_sort_servicedependencies()==ERROR)
		return ERROR;

	/* sort service escalations */
	if(xodtemplate_sort_serviceescalations()==ERROR)
		return ERROR;

	/* sort host dependencies */
	if(xodtemplate_sort_hostdependencies()==ERROR)
		return ERROR;

	/* sort hostescalations */
	if(xodtemplate_sort_hostescalations()==ERROR)
		return ERROR;

	/* sort host extended info */
	if(xodtemplate_sort_hostextinfo()==ERROR)
		return ERROR;

	/* sort service extended info */
	if(xodtemplate_sort_serviceextinfo()==ERROR)
		return ERROR;

#ifdef DEBUG0
	printf("xodtemplate_sort_objects() end\n");
#endif

	return OK;
	}


/* used to compare two strings (object names) */
int xodtemplate_compare_strings1(char *string1, char *string2){
	
	if(string1==NULL && string2==NULL)
		return 0;
	else if(string1==NULL)
		return -1;
	else if(string2==NULL)
		return 1;
	else
		return strcmp(string1,string2);
	}


/* used to compare two sets of strings (dually-named objects, i.e. services) */
int xodtemplate_compare_strings2(char *string1a, char *string1b, char *string2a, char *string2b){
	int result;

	if((result=xodtemplate_compare_strings1(string1a,string2a))==0)
		result=xodtemplate_compare_strings1(string1b,string2b);

	return result;
	}


/* sort timeperiods by name */
int xodtemplate_sort_timeperiods(){
	xodtemplate_timeperiod *new_timeperiod_list=NULL;
	xodtemplate_timeperiod *temp_timeperiod=NULL;
	xodtemplate_timeperiod *last_timeperiod=NULL;
	xodtemplate_timeperiod *temp_timeperiod_orig=NULL;
	xodtemplate_timeperiod *next_timeperiod_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_timeperiods() start\n");
#endif

	/* sort all existing timeperiods */
	for(temp_timeperiod_orig=xodtemplate_timeperiod_list;temp_timeperiod_orig!=NULL;temp_timeperiod_orig=next_timeperiod_orig){

		next_timeperiod_orig=temp_timeperiod_orig->next;

		/* add timeperiod to new list, sorted by timeperiod name */
		last_timeperiod=new_timeperiod_list;
		for(temp_timeperiod=new_timeperiod_list;temp_timeperiod!=NULL;temp_timeperiod=temp_timeperiod->next){

			if(xodtemplate_compare_strings1(temp_timeperiod_orig->timeperiod_name,temp_timeperiod->timeperiod_name)<=0)
				break;
			else
				last_timeperiod=temp_timeperiod;
			}

		/* first item added to new sorted list */
		if(new_timeperiod_list==NULL){
			temp_timeperiod_orig->next=NULL;
			new_timeperiod_list=temp_timeperiod_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_timeperiod==new_timeperiod_list){
			temp_timeperiod_orig->next=new_timeperiod_list;
			new_timeperiod_list=temp_timeperiod_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_timeperiod_orig->next=temp_timeperiod;
			last_timeperiod->next=temp_timeperiod_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_timeperiod_list=new_timeperiod_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_timeperiods() end\n");
#endif

	return OK;
	}


/* sort commands by name */
int xodtemplate_sort_commands(){
	xodtemplate_command *new_command_list=NULL;
	xodtemplate_command *temp_command=NULL;
	xodtemplate_command *last_command=NULL;
	xodtemplate_command *temp_command_orig=NULL;
	xodtemplate_command *next_command_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_commands() start\n");
#endif

	/* sort all existing commands */
	for(temp_command_orig=xodtemplate_command_list;temp_command_orig!=NULL;temp_command_orig=next_command_orig){

		next_command_orig=temp_command_orig->next;

		/* add command to new list, sorted by command name */
		last_command=new_command_list;
		for(temp_command=new_command_list;temp_command!=NULL;temp_command=temp_command->next){

			if(xodtemplate_compare_strings1(temp_command_orig->command_name,temp_command->command_name)<=0)
				break;
			else
				last_command=temp_command;
			}

		/* first item added to new sorted list */
		if(new_command_list==NULL){
			temp_command_orig->next=NULL;
			new_command_list=temp_command_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_command==new_command_list){
			temp_command_orig->next=new_command_list;
			new_command_list=temp_command_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_command_orig->next=temp_command;
			last_command->next=temp_command_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_command_list=new_command_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_commands() end\n");
#endif

	return OK;
	}


/* sort contactgroups by name */
int xodtemplate_sort_contactgroups(){
	xodtemplate_contactgroup *new_contactgroup_list=NULL;
	xodtemplate_contactgroup *temp_contactgroup=NULL;
	xodtemplate_contactgroup *last_contactgroup=NULL;
	xodtemplate_contactgroup *temp_contactgroup_orig=NULL;
	xodtemplate_contactgroup *next_contactgroup_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_contactgroups() start\n");
#endif

	/* sort all existing contactgroups */
	for(temp_contactgroup_orig=xodtemplate_contactgroup_list;temp_contactgroup_orig!=NULL;temp_contactgroup_orig=next_contactgroup_orig){

		next_contactgroup_orig=temp_contactgroup_orig->next;

		/* add contactgroup to new list, sorted by contactgroup name */
		last_contactgroup=new_contactgroup_list;
		for(temp_contactgroup=new_contactgroup_list;temp_contactgroup!=NULL;temp_contactgroup=temp_contactgroup->next){

			if(xodtemplate_compare_strings1(temp_contactgroup_orig->contactgroup_name,temp_contactgroup->contactgroup_name)<=0)
				break;
			else
				last_contactgroup=temp_contactgroup;
			}

		/* first item added to new sorted list */
		if(new_contactgroup_list==NULL){
			temp_contactgroup_orig->next=NULL;
			new_contactgroup_list=temp_contactgroup_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_contactgroup==new_contactgroup_list){
			temp_contactgroup_orig->next=new_contactgroup_list;
			new_contactgroup_list=temp_contactgroup_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_contactgroup_orig->next=temp_contactgroup;
			last_contactgroup->next=temp_contactgroup_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_contactgroup_list=new_contactgroup_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_contactgroups() end\n");
#endif

	return OK;
	}


/* sort hostgroups by name */
int xodtemplate_sort_hostgroups(){
	xodtemplate_hostgroup *new_hostgroup_list=NULL;
	xodtemplate_hostgroup *temp_hostgroup=NULL;
	xodtemplate_hostgroup *last_hostgroup=NULL;
	xodtemplate_hostgroup *temp_hostgroup_orig=NULL;
	xodtemplate_hostgroup *next_hostgroup_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_hostgroups() start\n");
#endif

	/* sort all existing hostgroups */
	for(temp_hostgroup_orig=xodtemplate_hostgroup_list;temp_hostgroup_orig!=NULL;temp_hostgroup_orig=next_hostgroup_orig){

		next_hostgroup_orig=temp_hostgroup_orig->next;

		/* add hostgroup to new list, sorted by hostgroup name */
		last_hostgroup=new_hostgroup_list;
		for(temp_hostgroup=new_hostgroup_list;temp_hostgroup!=NULL;temp_hostgroup=temp_hostgroup->next){

			if(xodtemplate_compare_strings1(temp_hostgroup_orig->hostgroup_name,temp_hostgroup->hostgroup_name)<=0)
				break;
			else
				last_hostgroup=temp_hostgroup;
			}

		/* first item added to new sorted list */
		if(new_hostgroup_list==NULL){
			temp_hostgroup_orig->next=NULL;
			new_hostgroup_list=temp_hostgroup_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_hostgroup==new_hostgroup_list){
			temp_hostgroup_orig->next=new_hostgroup_list;
			new_hostgroup_list=temp_hostgroup_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_hostgroup_orig->next=temp_hostgroup;
			last_hostgroup->next=temp_hostgroup_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_hostgroup_list=new_hostgroup_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_hostgroups() end\n");
#endif

	return OK;
	}


/* sort servicegroups by name */
int xodtemplate_sort_servicegroups(){
	xodtemplate_servicegroup *new_servicegroup_list=NULL;
	xodtemplate_servicegroup *temp_servicegroup=NULL;
	xodtemplate_servicegroup *last_servicegroup=NULL;
	xodtemplate_servicegroup *temp_servicegroup_orig=NULL;
	xodtemplate_servicegroup *next_servicegroup_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_servicegroups() start\n");
#endif

	/* sort all existing servicegroups */
	for(temp_servicegroup_orig=xodtemplate_servicegroup_list;temp_servicegroup_orig!=NULL;temp_servicegroup_orig=next_servicegroup_orig){

		next_servicegroup_orig=temp_servicegroup_orig->next;

		/* add servicegroup to new list, sorted by servicegroup name */
		last_servicegroup=new_servicegroup_list;
		for(temp_servicegroup=new_servicegroup_list;temp_servicegroup!=NULL;temp_servicegroup=temp_servicegroup->next){

			if(xodtemplate_compare_strings1(temp_servicegroup_orig->servicegroup_name,temp_servicegroup->servicegroup_name)<=0)
				break;
			else
				last_servicegroup=temp_servicegroup;
			}

		/* first item added to new sorted list */
		if(new_servicegroup_list==NULL){
			temp_servicegroup_orig->next=NULL;
			new_servicegroup_list=temp_servicegroup_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_servicegroup==new_servicegroup_list){
			temp_servicegroup_orig->next=new_servicegroup_list;
			new_servicegroup_list=temp_servicegroup_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_servicegroup_orig->next=temp_servicegroup;
			last_servicegroup->next=temp_servicegroup_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_servicegroup_list=new_servicegroup_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_servicegroups() end\n");
#endif

	return OK;
	}


/* sort contacts by name */
int xodtemplate_sort_contacts(){
	xodtemplate_contact *new_contact_list=NULL;
	xodtemplate_contact *temp_contact=NULL;
	xodtemplate_contact *last_contact=NULL;
	xodtemplate_contact *temp_contact_orig=NULL;
	xodtemplate_contact *next_contact_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_contacts() start\n");
#endif

	/* sort all existing contacts */
	for(temp_contact_orig=xodtemplate_contact_list;temp_contact_orig!=NULL;temp_contact_orig=next_contact_orig){

		next_contact_orig=temp_contact_orig->next;

		/* add contact to new list, sorted by contact name */
		last_contact=new_contact_list;
		for(temp_contact=new_contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){

			if(xodtemplate_compare_strings1(temp_contact_orig->contact_name,temp_contact->contact_name)<=0)
				break;
			else
				last_contact=temp_contact;
			}

		/* first item added to new sorted list */
		if(new_contact_list==NULL){
			temp_contact_orig->next=NULL;
			new_contact_list=temp_contact_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_contact==new_contact_list){
			temp_contact_orig->next=new_contact_list;
			new_contact_list=temp_contact_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_contact_orig->next=temp_contact;
			last_contact->next=temp_contact_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_contact_list=new_contact_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_contacts() end\n");
#endif

	return OK;
	}


/* sort hosts by name */
int xodtemplate_sort_hosts(){
	xodtemplate_host *new_host_list=NULL;
	xodtemplate_host *temp_host=NULL;
	xodtemplate_host *last_host=NULL;
	xodtemplate_host *temp_host_orig=NULL;
	xodtemplate_host *next_host_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_hosts() start\n");
#endif

	/* sort all existing hosts */
	for(temp_host_orig=xodtemplate_host_list;temp_host_orig!=NULL;temp_host_orig=next_host_orig){

		next_host_orig=temp_host_orig->next;

		/* add host to new list, sorted by host name */
		last_host=new_host_list;
		for(temp_host=new_host_list;temp_host!=NULL;temp_host=temp_host->next){

			if(xodtemplate_compare_strings1(temp_host_orig->host_name,temp_host->host_name)<=0)
				break;
			else
				last_host=temp_host;
			}

		/* first item added to new sorted list */
		if(new_host_list==NULL){
			temp_host_orig->next=NULL;
			new_host_list=temp_host_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_host==new_host_list){
			temp_host_orig->next=new_host_list;
			new_host_list=temp_host_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_host_orig->next=temp_host;
			last_host->next=temp_host_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_host_list=new_host_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_hosts() end\n");
#endif

	return OK;
	}


/* sort services by name */
int xodtemplate_sort_services(){
	xodtemplate_service *new_service_list=NULL;
	xodtemplate_service *temp_service=NULL;
	xodtemplate_service *last_service=NULL;
	xodtemplate_service *temp_service_orig=NULL;
	xodtemplate_service *next_service_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_services() start\n");
#endif

	/* sort all existing services */
	for(temp_service_orig=xodtemplate_service_list;temp_service_orig!=NULL;temp_service_orig=next_service_orig){

		next_service_orig=temp_service_orig->next;

		/* add service to new list, sorted by host name then service description */
		last_service=new_service_list;
		for(temp_service=new_service_list;temp_service!=NULL;temp_service=temp_service->next){

			if(xodtemplate_compare_strings2(temp_service_orig->host_name,temp_service_orig->service_description,temp_service->host_name,temp_service->service_description)<=0)
				break;
			else
				last_service=temp_service;
			}

		/* first item added to new sorted list */
		if(new_service_list==NULL){
			temp_service_orig->next=NULL;
			new_service_list=temp_service_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_service==new_service_list){
			temp_service_orig->next=new_service_list;
			new_service_list=temp_service_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_service_orig->next=temp_service;
			last_service->next=temp_service_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_service_list=new_service_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_services() end\n");
#endif

	return OK;
	}


/* sort servicedependencies by name */
int xodtemplate_sort_servicedependencies(){
	xodtemplate_servicedependency *new_servicedependency_list=NULL;
	xodtemplate_servicedependency *temp_servicedependency=NULL;
	xodtemplate_servicedependency *last_servicedependency=NULL;
	xodtemplate_servicedependency *temp_servicedependency_orig=NULL;
	xodtemplate_servicedependency *next_servicedependency_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_servicedependencies() start\n");
#endif

	/* sort all existing servicedependencies */
	for(temp_servicedependency_orig=xodtemplate_servicedependency_list;temp_servicedependency_orig!=NULL;temp_servicedependency_orig=next_servicedependency_orig){

		next_servicedependency_orig=temp_servicedependency_orig->next;

		/* add servicedependency to new list, sorted by host name then service description */
		last_servicedependency=new_servicedependency_list;
		for(temp_servicedependency=new_servicedependency_list;temp_servicedependency!=NULL;temp_servicedependency=temp_servicedependency->next){

			if(xodtemplate_compare_strings2(temp_servicedependency_orig->host_name,temp_servicedependency_orig->service_description,temp_servicedependency->host_name,temp_servicedependency->service_description)<=0)
				break;
			else
				last_servicedependency=temp_servicedependency;
			}

		/* first item added to new sorted list */
		if(new_servicedependency_list==NULL){
			temp_servicedependency_orig->next=NULL;
			new_servicedependency_list=temp_servicedependency_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_servicedependency==new_servicedependency_list){
			temp_servicedependency_orig->next=new_servicedependency_list;
			new_servicedependency_list=temp_servicedependency_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_servicedependency_orig->next=temp_servicedependency;
			last_servicedependency->next=temp_servicedependency_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_servicedependency_list=new_servicedependency_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_servicedependencies() end\n");
#endif

	return OK;
	}


/* sort serviceescalations by name */
int xodtemplate_sort_serviceescalations(){
	xodtemplate_serviceescalation *new_serviceescalation_list=NULL;
	xodtemplate_serviceescalation *temp_serviceescalation=NULL;
	xodtemplate_serviceescalation *last_serviceescalation=NULL;
	xodtemplate_serviceescalation *temp_serviceescalation_orig=NULL;
	xodtemplate_serviceescalation *next_serviceescalation_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_serviceescalations() start\n");
#endif

	/* sort all existing serviceescalations */
	for(temp_serviceescalation_orig=xodtemplate_serviceescalation_list;temp_serviceescalation_orig!=NULL;temp_serviceescalation_orig=next_serviceescalation_orig){

		next_serviceescalation_orig=temp_serviceescalation_orig->next;

		/* add serviceescalation to new list, sorted by host name then service description */
		last_serviceescalation=new_serviceescalation_list;
		for(temp_serviceescalation=new_serviceescalation_list;temp_serviceescalation!=NULL;temp_serviceescalation=temp_serviceescalation->next){

			if(xodtemplate_compare_strings2(temp_serviceescalation_orig->host_name,temp_serviceescalation_orig->service_description,temp_serviceescalation->host_name,temp_serviceescalation->service_description)<=0)
				break;
			else
				last_serviceescalation=temp_serviceescalation;
			}

		/* first item added to new sorted list */
		if(new_serviceescalation_list==NULL){
			temp_serviceescalation_orig->next=NULL;
			new_serviceescalation_list=temp_serviceescalation_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_serviceescalation==new_serviceescalation_list){
			temp_serviceescalation_orig->next=new_serviceescalation_list;
			new_serviceescalation_list=temp_serviceescalation_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_serviceescalation_orig->next=temp_serviceescalation;
			last_serviceescalation->next=temp_serviceescalation_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_serviceescalation_list=new_serviceescalation_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_serviceescalations() end\n");
#endif

	return OK;
	}


/* sort hostescalations by name */
int xodtemplate_sort_hostescalations(){
	xodtemplate_hostescalation *new_hostescalation_list=NULL;
	xodtemplate_hostescalation *temp_hostescalation=NULL;
	xodtemplate_hostescalation *last_hostescalation=NULL;
	xodtemplate_hostescalation *temp_hostescalation_orig=NULL;
	xodtemplate_hostescalation *next_hostescalation_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_hostescalations() start\n");
#endif

	/* sort all existing hostescalations */
	for(temp_hostescalation_orig=xodtemplate_hostescalation_list;temp_hostescalation_orig!=NULL;temp_hostescalation_orig=next_hostescalation_orig){

		next_hostescalation_orig=temp_hostescalation_orig->next;

		/* add hostescalation to new list, sorted by host name then hostescalation description */
		last_hostescalation=new_hostescalation_list;
		for(temp_hostescalation=new_hostescalation_list;temp_hostescalation!=NULL;temp_hostescalation=temp_hostescalation->next){

			if(xodtemplate_compare_strings1(temp_hostescalation_orig->host_name,temp_hostescalation->host_name)<=0)
				break;
			else
				last_hostescalation=temp_hostescalation;
			}

		/* first item added to new sorted list */
		if(new_hostescalation_list==NULL){
			temp_hostescalation_orig->next=NULL;
			new_hostescalation_list=temp_hostescalation_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_hostescalation==new_hostescalation_list){
			temp_hostescalation_orig->next=new_hostescalation_list;
			new_hostescalation_list=temp_hostescalation_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_hostescalation_orig->next=temp_hostescalation;
			last_hostescalation->next=temp_hostescalation_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_hostescalation_list=new_hostescalation_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_hostescalations() end\n");
#endif

	return OK;
	}


/* sort hostdependencies by name */
int xodtemplate_sort_hostdependencies(){
	xodtemplate_hostdependency *new_hostdependency_list=NULL;
	xodtemplate_hostdependency *temp_hostdependency=NULL;
	xodtemplate_hostdependency *last_hostdependency=NULL;
	xodtemplate_hostdependency *temp_hostdependency_orig=NULL;
	xodtemplate_hostdependency *next_hostdependency_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_hostdependencies() start\n");
#endif

	/* sort all existing hostdependencys */
	for(temp_hostdependency_orig=xodtemplate_hostdependency_list;temp_hostdependency_orig!=NULL;temp_hostdependency_orig=next_hostdependency_orig){

		next_hostdependency_orig=temp_hostdependency_orig->next;

		/* add hostdependency to new list, sorted by host name then hostdependency description */
		last_hostdependency=new_hostdependency_list;
		for(temp_hostdependency=new_hostdependency_list;temp_hostdependency!=NULL;temp_hostdependency=temp_hostdependency->next){

			if(xodtemplate_compare_strings1(temp_hostdependency_orig->host_name,temp_hostdependency->host_name)<=0)
				break;
			else
				last_hostdependency=temp_hostdependency;
			}

		/* first item added to new sorted list */
		if(new_hostdependency_list==NULL){
			temp_hostdependency_orig->next=NULL;
			new_hostdependency_list=temp_hostdependency_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_hostdependency==new_hostdependency_list){
			temp_hostdependency_orig->next=new_hostdependency_list;
			new_hostdependency_list=temp_hostdependency_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_hostdependency_orig->next=temp_hostdependency;
			last_hostdependency->next=temp_hostdependency_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_hostdependency_list=new_hostdependency_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_hostdependencies() end\n");
#endif

	return OK;
	}


/* sort extended host info by name */
int xodtemplate_sort_hostextinfo(){
	xodtemplate_hostextinfo *new_hostextinfo_list=NULL;
	xodtemplate_hostextinfo *temp_hostextinfo=NULL;
	xodtemplate_hostextinfo *last_hostextinfo=NULL;
	xodtemplate_hostextinfo *temp_hostextinfo_orig=NULL;
	xodtemplate_hostextinfo *next_hostextinfo_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_hostextinfo() start\n");
#endif

	/* sort all existing existing host info */
	for(temp_hostextinfo_orig=xodtemplate_hostextinfo_list;temp_hostextinfo_orig!=NULL;temp_hostextinfo_orig=next_hostextinfo_orig){

		next_hostextinfo_orig=temp_hostextinfo_orig->next;

		/* add hostextinfo to new list, sorted by host name then hostextinfo description */
		last_hostextinfo=new_hostextinfo_list;
		for(temp_hostextinfo=new_hostextinfo_list;temp_hostextinfo!=NULL;temp_hostextinfo=temp_hostextinfo->next){

			if(xodtemplate_compare_strings1(temp_hostextinfo_orig->host_name,temp_hostextinfo->host_name)<=0)
				break;
			else
				last_hostextinfo=temp_hostextinfo;
			}

		/* first item added to new sorted list */
		if(new_hostextinfo_list==NULL){
			temp_hostextinfo_orig->next=NULL;
			new_hostextinfo_list=temp_hostextinfo_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_hostextinfo==new_hostextinfo_list){
			temp_hostextinfo_orig->next=new_hostextinfo_list;
			new_hostextinfo_list=temp_hostextinfo_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_hostextinfo_orig->next=temp_hostextinfo;
			last_hostextinfo->next=temp_hostextinfo_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_hostextinfo_list=new_hostextinfo_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_hostextinfo() end\n");
#endif

	return OK;
	}


/* sort extended service info by name */
int xodtemplate_sort_serviceextinfo(){
	xodtemplate_serviceextinfo *new_serviceextinfo_list=NULL;
	xodtemplate_serviceextinfo *temp_serviceextinfo=NULL;
	xodtemplate_serviceextinfo *last_serviceextinfo=NULL;
	xodtemplate_serviceextinfo *temp_serviceextinfo_orig=NULL;
	xodtemplate_serviceextinfo *next_serviceextinfo_orig=NULL;

#ifdef DEBUG0
	printf("xodtemplate_sort_serviceextinfo() start\n");
#endif

	/* sort all existing extended service info */
	for(temp_serviceextinfo_orig=xodtemplate_serviceextinfo_list;temp_serviceextinfo_orig!=NULL;temp_serviceextinfo_orig=next_serviceextinfo_orig){

		next_serviceextinfo_orig=temp_serviceextinfo_orig->next;

		/* add serviceextinfo to new list, sorted by host name then service description */
		last_serviceextinfo=new_serviceextinfo_list;
		for(temp_serviceextinfo=new_serviceextinfo_list;temp_serviceextinfo!=NULL;temp_serviceextinfo=temp_serviceextinfo->next){

			if(xodtemplate_compare_strings2(temp_serviceextinfo_orig->host_name,temp_serviceextinfo_orig->service_description,temp_serviceextinfo->host_name,temp_serviceextinfo->service_description)<=0)
				break;
			else
				last_serviceextinfo=temp_serviceextinfo;
			}

		/* first item added to new sorted list */
		if(new_serviceextinfo_list==NULL){
			temp_serviceextinfo_orig->next=NULL;
			new_serviceextinfo_list=temp_serviceextinfo_orig;
			}

		/* item goes at head of new sorted list */
		else if(temp_serviceextinfo==new_serviceextinfo_list){
			temp_serviceextinfo_orig->next=new_serviceextinfo_list;
			new_serviceextinfo_list=temp_serviceextinfo_orig;
			}

		/* item goes in middle or at end of new sorted list */
		else{
			temp_serviceextinfo_orig->next=temp_serviceextinfo;
			last_serviceextinfo->next=temp_serviceextinfo_orig;
			}
	        }

	/* list is now sorted */
	xodtemplate_serviceextinfo_list=new_serviceextinfo_list;

#ifdef DEBUG0
	printf("xodtemplate_sort_serviceextinfo() end\n");
#endif

	return OK;
	}

#endif




/******************************************************************/
/*********************** MERGE FUNCTIONS **************************/
/******************************************************************/

#ifdef NSCORE

/* merge extinfo definitions */
int xodtemplate_merge_extinfo_ojects(void){
	xodtemplate_hostextinfo *temp_hostextinfo=NULL;
	xodtemplate_serviceextinfo *temp_serviceextinfo=NULL;
	xodtemplate_host *temp_host=NULL;
	xodtemplate_service *temp_service=NULL;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

	/* merge service extinfo definitions */
	for(temp_serviceextinfo=xodtemplate_serviceextinfo_list;temp_serviceextinfo!=NULL;temp_serviceextinfo=temp_serviceextinfo->next){

		/* make sure we have everything */
		if(temp_serviceextinfo->host_name==NULL || temp_serviceextinfo->service_description==NULL)
			continue;

		/* find the service */
		if((temp_service=xodtemplate_find_real_service(temp_serviceextinfo->host_name,temp_serviceextinfo->service_description))==NULL)
			continue;

		/* merge the definitions */
		xodtemplate_merge_service_extinfo_object(temp_service,temp_serviceextinfo);
	        }

#ifdef NSCORE
	if(xodtemplate_serviceextinfo_list!=NULL){
		asprintf(&temp_buffer,"Warning: Extended service information (serviceextinfo) definitions are deprecated in Nagios 3.x and will not be supported in future versions.  Please merge variables in these definitions with your service definitions.");
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
		my_free((void **)&temp_buffer);
	        }
#endif

	/* merge host extinfo definitions */
	for(temp_hostextinfo=xodtemplate_hostextinfo_list;temp_hostextinfo!=NULL;temp_hostextinfo=temp_hostextinfo->next){

		/* make sure we have everything */
		if(temp_hostextinfo->host_name==NULL)
			continue;

		/* find the host */
		if((temp_host=xodtemplate_find_real_host(temp_hostextinfo->host_name))==NULL)
			continue;

		/* merge the definitions */
		xodtemplate_merge_host_extinfo_object(temp_host,temp_hostextinfo);
	        }

#ifdef NSCORE
	if(xodtemplate_serviceextinfo_list!=NULL){
		asprintf(&temp_buffer,"Warning: Extended host information (hostextinfo) definitions are deprecated in Nagios 3.x and will not be supported in future versions.  Please merge variables in these definitions with your host definitions.");
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
		my_free((void **)&temp_buffer);
	        }
#endif

	return OK;
        }


/* merges a service extinfo definition */
int xodtemplate_merge_service_extinfo_object(xodtemplate_service *this_service, xodtemplate_serviceextinfo *this_serviceextinfo){

	if(this_service==NULL || this_serviceextinfo==NULL)
		return ERROR;

	if(this_service->notes==NULL && this_serviceextinfo->notes!=NULL)
		this_service->notes=strdup(this_serviceextinfo->notes);
	if(this_service->notes_url==NULL && this_serviceextinfo->notes_url!=NULL)
		this_service->notes_url=strdup(this_serviceextinfo->notes_url);
	if(this_service->action_url==NULL && this_serviceextinfo->action_url!=NULL)
		this_service->action_url=strdup(this_serviceextinfo->action_url);
	if(this_service->icon_image==NULL && this_serviceextinfo->icon_image!=NULL)
		this_service->icon_image=strdup(this_serviceextinfo->icon_image);
	if(this_service->icon_image_alt==NULL && this_serviceextinfo->icon_image_alt!=NULL)
		this_service->icon_image_alt=strdup(this_serviceextinfo->icon_image_alt);

	return OK;
        }


/* merges a host extinfo definition */
int xodtemplate_merge_host_extinfo_object(xodtemplate_host *this_host, xodtemplate_hostextinfo *this_hostextinfo){

	if(this_host==NULL || this_hostextinfo==NULL)
		return ERROR;

	return OK;
        }

#endif



/******************************************************************/
/*********************** CACHE FUNCTIONS **************************/
/******************************************************************/

#ifdef NSCORE

/* writes cached object definitions for use by web interface */
int xodtemplate_cache_objects(char *cache_file){
	FILE *fp=NULL;
	char *temp_buffer=NULL;
	register int x=0;
	char *days[7]={"sunday","monday","tuesday","wednesday","thursday","friday","saturday"};
	xodtemplate_timeperiod *temp_timeperiod=NULL;
	xodtemplate_command *temp_command=NULL;
	xodtemplate_contactgroup *temp_contactgroup=NULL;
	xodtemplate_hostgroup *temp_hostgroup=NULL;
	xodtemplate_servicegroup *temp_servicegroup=NULL;
	xodtemplate_contact *temp_contact=NULL;
	xodtemplate_host *temp_host=NULL;
	xodtemplate_service *temp_service=NULL;
	xodtemplate_servicedependency *temp_servicedependency=NULL;
	xodtemplate_serviceescalation *temp_serviceescalation=NULL;
	xodtemplate_hostdependency *temp_hostdependency=NULL;
	xodtemplate_hostescalation *temp_hostescalation=NULL;
	xodtemplate_hostextinfo *temp_hostextinfo=NULL;
	xodtemplate_serviceextinfo *temp_serviceextinfo=NULL;
	xodtemplate_customvariablesmember *temp_customvariablesmember=NULL;
	time_t current_time=0L;

#ifdef DEBUG0
	printf("xodtemplate_cache_objects() start\n");
#endif

	time(&current_time);

	/* open the cache file for writing */
	fp=fopen(cache_file,"w");
	if(fp==NULL){
		asprintf(&temp_buffer,"Warning: Could not open object cache file '%s' for writing!\n",cache_file);
		write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_WARNING,TRUE);
		my_free((void **)&temp_buffer);
		return ERROR;
	        }

	/* write header to cache file */
	fprintf(fp,"########################################\n");
	fprintf(fp,"#       NAGIOS OBJECT CACHE FILE\n");
	fprintf(fp,"#\n");
	fprintf(fp,"# THIS FILE IS AUTOMATICALLY GENERATED\n");
	fprintf(fp,"# BY NAGIOS.  DO NOT MODIFY THIS FILE!\n");
	fprintf(fp,"#\n");
	fprintf(fp,"# Created: %s",ctime(&current_time));
	fprintf(fp,"########################################\n\n");


	/* cache timeperiods */
	for(temp_timeperiod=xodtemplate_timeperiod_list;temp_timeperiod!=NULL;temp_timeperiod=temp_timeperiod->next){
		if(temp_timeperiod->register_object==FALSE)
			continue;
		fprintf(fp,"define timeperiod {\n");
		if(temp_timeperiod->timeperiod_name)
			fprintf(fp,"\ttimeperiod_name\t%s\n",temp_timeperiod->timeperiod_name);
		if(temp_timeperiod->alias)
			fprintf(fp,"\talias\t%s\n",temp_timeperiod->alias);
		for(x=0;x<7;x++){
			if(temp_timeperiod->timeranges[x]!=NULL)
				fprintf(fp,"\t%s\t%s\n",days[x],temp_timeperiod->timeranges[x]);
		        }
		fprintf(fp,"\t}\n\n");
	        }

	/* cache commands */
	for(temp_command=xodtemplate_command_list;temp_command!=NULL;temp_command=temp_command->next){
		if(temp_command->register_object==FALSE)
			continue;
		fprintf(fp,"define command {\n");
		if(temp_command->command_name)
			fprintf(fp,"\tcommand_name\t%s\n",temp_command->command_name);
		if(temp_command->command_line)
			fprintf(fp,"\tcommand_line\t%s\n",temp_command->command_line);
		fprintf(fp,"\t}\n\n");
	        }

	/* cache contactgroups */
	for(temp_contactgroup=xodtemplate_contactgroup_list;temp_contactgroup!=NULL;temp_contactgroup=temp_contactgroup->next){
		if(temp_contactgroup->register_object==FALSE)
			continue;
		fprintf(fp,"define contactgroup {\n");
		if(temp_contactgroup->contactgroup_name)
			fprintf(fp,"\tcontactgroup_name\t%s\n",temp_contactgroup->contactgroup_name);
		if(temp_contactgroup->alias)
			fprintf(fp,"\talias\t%s\n",temp_contactgroup->alias);
		if(temp_contactgroup->members)
			fprintf(fp,"\tmembers\t%s\n",temp_contactgroup->members);
		fprintf(fp,"\t}\n\n");
	        }

	/* cache hostgroups */
	for(temp_hostgroup=xodtemplate_hostgroup_list;temp_hostgroup!=NULL;temp_hostgroup=temp_hostgroup->next){
		if(temp_hostgroup->register_object==FALSE)
			continue;
		fprintf(fp,"define hostgroup {\n");
		if(temp_hostgroup->hostgroup_name)
			fprintf(fp,"\thostgroup_name\t%s\n",temp_hostgroup->hostgroup_name);
		if(temp_hostgroup->alias)
			fprintf(fp,"\talias\t%s\n",temp_hostgroup->alias);
		if(temp_hostgroup->members)
			fprintf(fp,"\tmembers\t%s\n",temp_hostgroup->members);
		fprintf(fp,"\t}\n\n");
	        }

	/* cache servicegroups */
	for(temp_servicegroup=xodtemplate_servicegroup_list;temp_servicegroup!=NULL;temp_servicegroup=temp_servicegroup->next){
		if(temp_servicegroup->register_object==FALSE)
			continue;
		fprintf(fp,"define servicegroup {\n");
		if(temp_servicegroup->servicegroup_name)
			fprintf(fp,"\tservicegroup_name\t%s\n",temp_servicegroup->servicegroup_name);
		if(temp_servicegroup->alias)
			fprintf(fp,"\talias\t%s\n",temp_servicegroup->alias);
		if(temp_servicegroup->members)
			fprintf(fp,"\tmembers\t%s\n",temp_servicegroup->members);
		fprintf(fp,"\t}\n\n");
	        }

	/* cache contacts */
	for(temp_contact=xodtemplate_contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){
		if(temp_contact->register_object==FALSE)
			continue;
		fprintf(fp,"define contact {\n");
		if(temp_contact->contact_name)
			fprintf(fp,"\tcontact_name\t%s\n",temp_contact->contact_name);
		if(temp_contact->alias)
			fprintf(fp,"\talias\t%s\n",temp_contact->alias);
		if(temp_contact->service_notification_period)
			fprintf(fp,"\tservice_notification_period\t%s\n",temp_contact->service_notification_period);
		if(temp_contact->host_notification_period)
			fprintf(fp,"\thost_notification_period\t%s\n",temp_contact->host_notification_period);
		fprintf(fp,"\tservice_notification_options\t");
		x=0;
		if(temp_contact->notify_on_service_warning==TRUE)
			fprintf(fp,"%sw",(x++>0)?",":"");
		if(temp_contact->notify_on_service_unknown==TRUE)
			fprintf(fp,"%su",(x++>0)?",":"");
		if(temp_contact->notify_on_service_critical==TRUE)
			fprintf(fp,"%sc",(x++>0)?",":"");
		if(temp_contact->notify_on_service_recovery==TRUE)
			fprintf(fp,"%sr",(x++>0)?",":"");
		if(temp_contact->notify_on_service_flapping==TRUE)
			fprintf(fp,"%sf",(x++>0)?",":"");
		if(x==0)
			fprintf(fp,"n");
		fprintf(fp,"\n");
		fprintf(fp,"\thost_notification_options\t");
		x=0;
		if(temp_contact->notify_on_host_down==TRUE)
			fprintf(fp,"%sd",(x++>0)?",":"");
		if(temp_contact->notify_on_host_unreachable==TRUE)
			fprintf(fp,"%su",(x++>0)?",":"");
		if(temp_contact->notify_on_host_recovery==TRUE)
			fprintf(fp,"%sr",(x++>0)?",":"");
		if(temp_contact->notify_on_host_flapping==TRUE)
			fprintf(fp,"%sf",(x++>0)?",":"");
		if(x==0)
			fprintf(fp,"n");
		fprintf(fp,"\n");
		if(temp_contact->service_notification_commands)
			fprintf(fp,"\tservice_notification_commands\t%s\n",temp_contact->service_notification_commands);
		if(temp_contact->host_notification_commands)
			fprintf(fp,"\thost_notification_commands\t%s\n",temp_contact->host_notification_commands);
		if(temp_contact->email)
			fprintf(fp,"\temail\t%s\n",temp_contact->email);
		if(temp_contact->pager)
			fprintf(fp,"\tpager\t%s\n",temp_contact->pager);
		for(x=0;x<MAX_XODTEMPLATE_CONTACT_ADDRESSES;x++){
			if(temp_contact->address[x])
				fprintf(fp,"\taddress%d\t%s\n",x+1,temp_contact->address[x]);
		        }
		fprintf(fp,"\thost_notifications_enabled\t%d\n",temp_contact->host_notifications_enabled);
		fprintf(fp,"\tservice_notifications_enabled\t%d\n",temp_contact->service_notifications_enabled);
		fprintf(fp,"\tcan_submit_commands\t%d\n",temp_contact->can_submit_commands);
		fprintf(fp,"\tretain_status_information\t%d\n",temp_contact->retain_status_information);
		fprintf(fp,"\tretain_nonstatus_information\t%d\n",temp_contact->retain_nonstatus_information);

		/* custom variables */
		for(temp_customvariablesmember=temp_contact->custom_variables;temp_customvariablesmember!=NULL;temp_customvariablesmember=temp_customvariablesmember->next){
			if(temp_customvariablesmember->variable_name)
				fprintf(fp,"\t_%s\t%s\n",temp_customvariablesmember->variable_name,(temp_customvariablesmember->variable_value==NULL)?XODTEMPLATE_NULL:temp_customvariablesmember->variable_value);
		        }


		fprintf(fp,"\t}\n\n");
	        }

	/* cache hosts */
	for(temp_host=xodtemplate_host_list;temp_host!=NULL;temp_host=temp_host->next){
		if(temp_host->register_object==FALSE)
			continue;
		fprintf(fp,"define host {\n");
		if(temp_host->host_name)
			fprintf(fp,"\thost_name\t%s\n",temp_host->host_name);
		if(temp_host->display_name)
			fprintf(fp,"\tdisplay_name\t%s\n",temp_host->display_name);
		if(temp_host->alias)
			fprintf(fp,"\talias\t%s\n",temp_host->alias);
		if(temp_host->address)
			fprintf(fp,"\taddress\t%s\n",temp_host->address);
		if(temp_host->parents)
			fprintf(fp,"\tparents\t%s\n",temp_host->parents);
		if(temp_host->check_period)
			fprintf(fp,"\tcheck_period\t%s\n",temp_host->check_period);
		if(temp_host->check_command)
			fprintf(fp,"\tcheck_command\t%s\n",temp_host->check_command);
		if(temp_host->event_handler)
			fprintf(fp,"\tevent_handler\t%s\n",temp_host->event_handler);
		if(temp_host->contact_groups)
			fprintf(fp,"\tcontact_groups\t%s\n",temp_host->contact_groups);
		if(temp_host->notification_period)
			fprintf(fp,"\tnotification_period\t%s\n",temp_host->notification_period);
		if(temp_host->failure_prediction_options)
			fprintf(fp,"\tfailure_prediction_options\t%s\n",temp_host->failure_prediction_options);
		fprintf(fp,"\tcheck_interval\t%d\n",temp_host->check_interval);
		fprintf(fp,"\tmax_check_attempts\t%d\n",temp_host->max_check_attempts);
		fprintf(fp,"\tactive_checks_enabled\t%d\n",temp_host->active_checks_enabled);
		fprintf(fp,"\tpassive_checks_enabled\t%d\n",temp_host->passive_checks_enabled);
		fprintf(fp,"\tobsess_over_host\t%d\n",temp_host->obsess_over_host);
		fprintf(fp,"\tevent_handler_enabled\t%d\n",temp_host->event_handler_enabled);
		fprintf(fp,"\tlow_flap_threshold\t%f\n",temp_host->low_flap_threshold);
		fprintf(fp,"\thigh_flap_threshold\t%f\n",temp_host->high_flap_threshold);
		fprintf(fp,"\tflap_detection_enabled\t%d\n",temp_host->flap_detection_enabled);
		fprintf(fp,"\tflap_detection_options\t");
		x=0;
		if(temp_host->flap_detection_on_up==TRUE)
			fprintf(fp,"%so",(x++>0)?",":"");
		if(temp_host->flap_detection_on_down==TRUE)
			fprintf(fp,"%sd",(x++>0)?",":"");
		if(temp_host->flap_detection_on_unreachable==TRUE)
			fprintf(fp,"%su",(x++>0)?",":"");
		if(x==0)
			fprintf(fp,"n");
		fprintf(fp,"\n");
		fprintf(fp,"\tfreshness_threshold\t%d\n",temp_host->freshness_threshold);
		fprintf(fp,"\tcheck_freshness\t%d\n",temp_host->check_freshness);
		fprintf(fp,"\tnotification_options\t");
		x=0;
		if(temp_host->notify_on_down==TRUE)
			fprintf(fp,"%sd",(x++>0)?",":"");
		if(temp_host->notify_on_unreachable==TRUE)
			fprintf(fp,"%su",(x++>0)?",":"");
		if(temp_host->notify_on_recovery==TRUE)
			fprintf(fp,"%sr",(x++>0)?",":"");
		if(temp_host->notify_on_flapping==TRUE)
			fprintf(fp,"%sf",(x++>0)?",":"");
		if(x==0)
			fprintf(fp,"n");
		fprintf(fp,"\n");
		fprintf(fp,"\tnotifications_enabled\t%d\n",temp_host->notifications_enabled);
		fprintf(fp,"\tnotification_interval\t%d\n",temp_host->notification_interval);
		fprintf(fp,"\tfirst_notification_delay\t%d\n",temp_host->first_notification_delay);
		fprintf(fp,"\tstalking_options\t");
		x=0;
		if(temp_host->stalk_on_up==TRUE)
			fprintf(fp,"%so",(x++>0)?",":"");
		if(temp_host->stalk_on_down==TRUE)
			fprintf(fp,"%sd",(x++>0)?",":"");
		if(temp_host->stalk_on_unreachable==TRUE)
			fprintf(fp,"%su",(x++>0)?",":"");
		if(x==0)
			fprintf(fp,"n");
		fprintf(fp,"\n");
		fprintf(fp,"\tprocess_perf_data\t%d\n",temp_host->process_perf_data);
		fprintf(fp,"\tfailure_prediction_enabled\t%d\n",temp_host->failure_prediction_enabled);
		fprintf(fp,"\tretain_status_information\t%d\n",temp_host->retain_status_information);
		fprintf(fp,"\tretain_nonstatus_information\t%d\n",temp_host->retain_nonstatus_information);

		/* custom variables */
		for(temp_customvariablesmember=temp_host->custom_variables;temp_customvariablesmember!=NULL;temp_customvariablesmember=temp_customvariablesmember->next){
			if(temp_customvariablesmember->variable_name)
				fprintf(fp,"\t_%s\t%s\n",temp_customvariablesmember->variable_name,(temp_customvariablesmember->variable_value==NULL)?XODTEMPLATE_NULL:temp_customvariablesmember->variable_value);
		        }


		fprintf(fp,"\t}\n\n");
	        }

	/* cache services */
	for(temp_service=xodtemplate_service_list;temp_service!=NULL;temp_service=temp_service->next){
		if(temp_service->register_object==FALSE)
			continue;
		fprintf(fp,"define service {\n");
		if(temp_service->host_name)
			fprintf(fp,"\thost_name\t%s\n",temp_service->host_name);
		if(temp_service->service_description)
			fprintf(fp,"\tservice_description\t%s\n",temp_service->service_description);
		if(temp_service->display_name)
			fprintf(fp,"\tdisplay_name\t%s\n",temp_service->display_name);
		if(temp_service->check_period)
			fprintf(fp,"\tcheck_period\t%s\n",temp_service->check_period);
		if(temp_service->check_command)
			fprintf(fp,"\tcheck_command\t%s\n",temp_service->check_command);
		if(temp_service->event_handler)
			fprintf(fp,"\tevent_handler\t%s\n",temp_service->event_handler);
		if(temp_service->contact_groups)
			fprintf(fp,"\tcontact_groups\t%s\n",temp_service->contact_groups);
		if(temp_service->notification_period)
			fprintf(fp,"\tnotification_period\t%s\n",temp_service->notification_period);
		if(temp_service->failure_prediction_options)
			fprintf(fp,"\tfailure_prediction_options\t%s\n",temp_service->failure_prediction_options);
		fprintf(fp,"\tnormal_check_interval\t%d\n",temp_service->normal_check_interval);
		fprintf(fp,"\tretry_check_interval\t%d\n",temp_service->retry_check_interval);
		fprintf(fp,"\tmax_check_attempts\t%d\n",temp_service->max_check_attempts);
		fprintf(fp,"\tis_volatile\t%d\n",temp_service->is_volatile);
		fprintf(fp,"\tparallelize_check\t%d\n",temp_service->parallelize_check);
		fprintf(fp,"\tactive_checks_enabled\t%d\n",temp_service->active_checks_enabled);
		fprintf(fp,"\tpassive_checks_enabled\t%d\n",temp_service->passive_checks_enabled);
		fprintf(fp,"\tobsess_over_service\t%d\n",temp_service->obsess_over_service);
		fprintf(fp,"\tevent_handler_enabled\t%d\n",temp_service->event_handler_enabled);
		fprintf(fp,"\tlow_flap_threshold\t%f\n",temp_service->low_flap_threshold);
		fprintf(fp,"\thigh_flap_threshold\t%f\n",temp_service->high_flap_threshold);
		fprintf(fp,"\tflap_detection_enabled\t%d\n",temp_service->flap_detection_enabled);
		fprintf(fp,"\tflap_detection_options\t");
		x=0;
		if(temp_service->flap_detection_on_ok==TRUE)
			fprintf(fp,"%so",(x++>0)?",":"");
		if(temp_service->flap_detection_on_warning==TRUE)
			fprintf(fp,"%sw",(x++>0)?",":"");
		if(temp_service->flap_detection_on_unknown==TRUE)
			fprintf(fp,"%su",(x++>0)?",":"");
		if(temp_service->flap_detection_on_critical==TRUE)
			fprintf(fp,"%sc",(x++>0)?",":"");
		if(x==0)
			fprintf(fp,"n");
		fprintf(fp,"\n");
		fprintf(fp,"\tfreshness_threshold\t%d\n",temp_service->freshness_threshold);
		fprintf(fp,"\tcheck_freshness\t%d\n",temp_service->check_freshness);
		fprintf(fp,"\tnotification_options\t");
		x=0;
		if(temp_service->notify_on_unknown==TRUE)
			fprintf(fp,"%su",(x++>0)?",":"");
		if(temp_service->notify_on_warning==TRUE)
			fprintf(fp,"%sw",(x++>0)?",":"");
		if(temp_service->notify_on_critical==TRUE)
			fprintf(fp,"%sc",(x++>0)?",":"");
		if(temp_service->notify_on_recovery==TRUE)
			fprintf(fp,"%sr",(x++>0)?",":"");
		if(temp_service->notify_on_flapping==TRUE)
			fprintf(fp,"%sf",(x++>0)?",":"");
		if(x==0)
			fprintf(fp,"n");
		fprintf(fp,"\n");
		fprintf(fp,"\tnotifications_enabled\t%d\n",temp_service->notifications_enabled);
		fprintf(fp,"\tnotification_interval\t%d\n",temp_service->notification_interval);
		fprintf(fp,"\tfirst_notification_delay\t%d\n",temp_service->first_notification_delay);
		fprintf(fp,"\tstalking_options\t");
		x=0;
		if(temp_service->stalk_on_ok==TRUE)
			fprintf(fp,"%so",(x++>0)?",":"");
		if(temp_service->stalk_on_unknown==TRUE)
			fprintf(fp,"%su",(x++>0)?",":"");
		if(temp_service->stalk_on_warning==TRUE)
			fprintf(fp,"%sw",(x++>0)?",":"");
		if(temp_service->stalk_on_critical==TRUE)
			fprintf(fp,"%sc",(x++>0)?",":"");
		if(x==0)
			fprintf(fp,"n");
		fprintf(fp,"\n");
		fprintf(fp,"\tprocess_perf_data\t%d\n",temp_service->process_perf_data);
		fprintf(fp,"\tfailure_prediction_enabled\t%d\n",temp_service->failure_prediction_enabled);
		if(temp_service->icon_image)
			fprintf(fp,"\ticon_image\t%s\n",temp_service->icon_image);
		if(temp_service->icon_image_alt)
			fprintf(fp,"\ticon_image_alt\t%s\n",temp_service->icon_image_alt);
		if(temp_service->notes)
			fprintf(fp,"\tnotes\t%s\n",temp_service->notes);
		if(temp_service->notes_url)
			fprintf(fp,"\tnotes_url\t%s\n",temp_service->notes_url);
		if(temp_service->action_url)
			fprintf(fp,"\taction_url\t%s\n",temp_service->action_url);
		fprintf(fp,"\tretain_status_information\t%d\n",temp_service->retain_status_information);
		fprintf(fp,"\tretain_nonstatus_information\t%d\n",temp_service->retain_nonstatus_information);

		/* custom variables */
		for(temp_customvariablesmember=temp_service->custom_variables;temp_customvariablesmember!=NULL;temp_customvariablesmember=temp_customvariablesmember->next){
			if(temp_customvariablesmember->variable_name)
				fprintf(fp,"\t_%s\t%s\n",temp_customvariablesmember->variable_name,(temp_customvariablesmember->variable_value==NULL)?XODTEMPLATE_NULL:temp_customvariablesmember->variable_value);
		        }

		fprintf(fp,"\t}\n\n");
	        }

	/* cache service dependencies */
	for(temp_servicedependency=xodtemplate_servicedependency_list;temp_servicedependency!=NULL;temp_servicedependency=temp_servicedependency->next){
		if(temp_servicedependency->register_object==FALSE)
			continue;
		fprintf(fp,"define servicedependency {\n");
		if(temp_servicedependency->host_name)
			fprintf(fp,"\thost_name\t%s\n",temp_servicedependency->host_name);
		if(temp_servicedependency->service_description)
			fprintf(fp,"\tservice_description\t%s\n",temp_servicedependency->service_description);
		if(temp_servicedependency->dependent_host_name)
			fprintf(fp,"\tdependent_host_name\t%s\n",temp_servicedependency->dependent_host_name);
		if(temp_servicedependency->dependent_service_description)
			fprintf(fp,"\tdependent_service_description\t%s\n",temp_servicedependency->dependent_service_description);
		fprintf(fp,"\tinherits_parent\t%d\n",temp_servicedependency->inherits_parent);
		if(temp_servicedependency->have_notification_dependency_options==TRUE){
			fprintf(fp,"\tnotification_failure_options\t");
			x=0;
			if(temp_servicedependency->fail_notify_on_ok==TRUE)
				fprintf(fp,"%so",(x++>0)?",":"");
			if(temp_servicedependency->fail_notify_on_unknown==TRUE)
				fprintf(fp,"%su",(x++>0)?",":"");
			if(temp_servicedependency->fail_notify_on_warning==TRUE)
				fprintf(fp,"%sw",(x++>0)?",":"");
			if(temp_servicedependency->fail_notify_on_critical==TRUE)
				fprintf(fp,"%sc",(x++>0)?",":"");
			if(temp_servicedependency->fail_notify_on_pending==TRUE)
				fprintf(fp,"%sp",(x++>0)?",":"");
			if(x==0)
				fprintf(fp,"n");
			fprintf(fp,"\n");
		        }
		if(temp_servicedependency->have_execution_dependency_options==TRUE){
			fprintf(fp,"\texecution_failure_options\t");
			x=0;
			if(temp_servicedependency->fail_execute_on_ok==TRUE)
				fprintf(fp,"%so",(x++>0)?",":"");
			if(temp_servicedependency->fail_execute_on_unknown==TRUE)
				fprintf(fp,"%su",(x++>0)?",":"");
			if(temp_servicedependency->fail_execute_on_warning==TRUE)
				fprintf(fp,"%sw",(x++>0)?",":"");
			if(temp_servicedependency->fail_execute_on_critical==TRUE)
				fprintf(fp,"%sc",(x++>0)?",":"");
			if(temp_servicedependency->fail_execute_on_pending==TRUE)
				fprintf(fp,"%sp",(x++>0)?",":"");
			if(x==0)
				fprintf(fp,"n");
			fprintf(fp,"\n");
		        }
		fprintf(fp,"\t}\n\n");
	        }

	/* cache service escalations */
	for(temp_serviceescalation=xodtemplate_serviceescalation_list;temp_serviceescalation!=NULL;temp_serviceescalation=temp_serviceescalation->next){
		if(temp_serviceescalation->register_object==FALSE)
			continue;
		fprintf(fp,"define serviceescalation {\n");
		if(temp_serviceescalation->host_name)
			fprintf(fp,"\thost_name\t%s\n",temp_serviceescalation->host_name);
		if(temp_serviceescalation->service_description)
			fprintf(fp,"\tservice_description\t%s\n",temp_serviceescalation->service_description);
		fprintf(fp,"\tfirst_notification\t%d\n",temp_serviceescalation->first_notification);
		fprintf(fp,"\tlast_notification\t%d\n",temp_serviceescalation->last_notification);
		fprintf(fp,"\tnotification_interval\t%d\n",temp_serviceescalation->notification_interval);
		if(temp_serviceescalation->escalation_period)
			fprintf(fp,"\tescalation_period\t%s\n",temp_serviceescalation->escalation_period);
		if(temp_serviceescalation->have_escalation_options==TRUE){
			fprintf(fp,"\tescalation_options\t");
			x=0;
			if(temp_serviceescalation->escalate_on_warning==TRUE)
				fprintf(fp,"%sw",(x++>0)?",":"");
			if(temp_serviceescalation->escalate_on_unknown==TRUE)
				fprintf(fp,"%su",(x++>0)?",":"");
			if(temp_serviceescalation->escalate_on_critical==TRUE)
				fprintf(fp,"%sc",(x++>0)?",":"");
			if(temp_serviceescalation->escalate_on_recovery==TRUE)
				fprintf(fp,"%sr",(x++>0)?",":"");
			if(x==0)
				fprintf(fp,"n");
			fprintf(fp,"\n");
		        }
		if(temp_serviceescalation->contact_groups)
			fprintf(fp,"\tcontact_groups\t%s\n",temp_serviceescalation->contact_groups);
		fprintf(fp,"\t}\n\n");
	        }

	/* cache host dependencies */
	for(temp_hostdependency=xodtemplate_hostdependency_list;temp_hostdependency!=NULL;temp_hostdependency=temp_hostdependency->next){
		if(temp_hostdependency->register_object==FALSE)
			continue;
		fprintf(fp,"define hostdependency {\n");
		if(temp_hostdependency->host_name)
			fprintf(fp,"\thost_name\t%s\n",temp_hostdependency->host_name);
		if(temp_hostdependency->dependent_host_name)
			fprintf(fp,"\tdependent_host_name\t%s\n",temp_hostdependency->dependent_host_name);
		fprintf(fp,"\tinherits_parent\t%d\n",temp_hostdependency->inherits_parent);
		if(temp_hostdependency->have_notification_dependency_options==TRUE){
			fprintf(fp,"\tnotification_failure_options\t");
			x=0;
			if(temp_hostdependency->fail_notify_on_up==TRUE)
				fprintf(fp,"%so",(x++>0)?",":"");
			if(temp_hostdependency->fail_notify_on_down==TRUE)
				fprintf(fp,"%sd",(x++>0)?",":"");
			if(temp_hostdependency->fail_notify_on_unreachable==TRUE)
				fprintf(fp,"%su",(x++>0)?",":"");
			if(temp_hostdependency->fail_notify_on_pending==TRUE)
				fprintf(fp,"%sp",(x++>0)?",":"");
			if(x==0)
				fprintf(fp,"n");
			fprintf(fp,"\n");
		        }
		if(temp_hostdependency->have_execution_dependency_options==TRUE){
			fprintf(fp,"\texecution_failure_options\t");
			x=0;
			if(temp_hostdependency->fail_execute_on_up==TRUE)
				fprintf(fp,"%so",(x++>0)?",":"");
			if(temp_hostdependency->fail_execute_on_down==TRUE)
				fprintf(fp,"%sd",(x++>0)?",":"");
			if(temp_hostdependency->fail_execute_on_unreachable==TRUE)
				fprintf(fp,"%su",(x++>0)?",":"");
			if(temp_hostdependency->fail_execute_on_pending==TRUE)
				fprintf(fp,"%sp",(x++>0)?",":"");
			if(x==0)
				fprintf(fp,"n");
			fprintf(fp,"\n");
		        }
		fprintf(fp,"\t}\n\n");
	        }

	/* cache host escalations */
	for(temp_hostescalation=xodtemplate_hostescalation_list;temp_hostescalation!=NULL;temp_hostescalation=temp_hostescalation->next){
		if(temp_hostescalation->register_object==FALSE)
			continue;
		fprintf(fp,"define hostescalation {\n");
		if(temp_hostescalation->host_name)
			fprintf(fp,"\thost_name\t%s\n",temp_hostescalation->host_name);
		fprintf(fp,"\tfirst_notification\t%d\n",temp_hostescalation->first_notification);
		fprintf(fp,"\tlast_notification\t%d\n",temp_hostescalation->last_notification);
		fprintf(fp,"\tnotification_interval\t%d\n",temp_hostescalation->notification_interval);
		if(temp_hostescalation->escalation_period)
			fprintf(fp,"\tescalation_period\t%s\n",temp_hostescalation->escalation_period);
		if(temp_hostescalation->have_escalation_options==TRUE){
			fprintf(fp,"\tescalation_options\t");
			x=0;
			if(temp_hostescalation->escalate_on_down==TRUE)
				fprintf(fp,"%sd",(x++>0)?",":"");
			if(temp_hostescalation->escalate_on_unreachable==TRUE)
				fprintf(fp,"%su",(x++>0)?",":"");
			if(temp_hostescalation->escalate_on_recovery==TRUE)
				fprintf(fp,"%sr",(x++>0)?",":"");
			if(x==0)
				fprintf(fp,"n");
			fprintf(fp,"\n");
		        }
		if(temp_hostescalation->contact_groups)
			fprintf(fp,"\tcontact_groups\t%s\n",temp_hostescalation->contact_groups);
		fprintf(fp,"\t}\n\n");
	        }

	/* cache host extended info */
	for(temp_hostextinfo=xodtemplate_hostextinfo_list;temp_hostextinfo!=NULL;temp_hostextinfo=temp_hostextinfo->next){
		if(temp_hostextinfo->register_object==FALSE)
			continue;
		fprintf(fp,"define hostextinfo {\n");
		if(temp_hostextinfo->host_name)
			fprintf(fp,"\thost_name\t%s\n",temp_hostextinfo->host_name);
		if(temp_hostextinfo->icon_image)
			fprintf(fp,"\ticon_image\t%s\n",temp_hostextinfo->icon_image);
		if(temp_hostextinfo->icon_image_alt)
			fprintf(fp,"\ticon_image_alt\t%s\n",temp_hostextinfo->icon_image_alt);
		if(temp_hostextinfo->vrml_image)
			fprintf(fp,"\tvrml_image\t%s\n",temp_hostextinfo->vrml_image);
		if(temp_hostextinfo->statusmap_image)
			fprintf(fp,"\tstatusmap_image\t%s\n",temp_hostextinfo->statusmap_image);
		if(temp_hostextinfo->have_2d_coords==TRUE)
			fprintf(fp,"\t2d_coords\t%d,%d\n",temp_hostextinfo->x_2d,temp_hostextinfo->y_2d);
		if(temp_hostextinfo->have_3d_coords==TRUE)
			fprintf(fp,"\t3d_coords\t%f,%f,%f\n",temp_hostextinfo->x_3d,temp_hostextinfo->y_3d,temp_hostextinfo->z_3d);
		if(temp_hostextinfo->notes)
			fprintf(fp,"\tnotes\t%s\n",temp_hostextinfo->notes);
		if(temp_hostextinfo->notes_url)
			fprintf(fp,"\tnotes_url\t%s\n",temp_hostextinfo->notes_url);
		if(temp_hostextinfo->action_url)
			fprintf(fp,"\taction_url\t%s\n",temp_hostextinfo->action_url);
		fprintf(fp,"\t}\n\n");
	        }

	fclose(fp);

#ifdef DEBUG0
	printf("xodtemplate_cache_objects() end\n");
#endif

	return OK;
        }

#endif

/******************************************************************/
/********************** CLEANUP FUNCTIONS *************************/
/******************************************************************/

/* frees memory */
int xodtemplate_free_memory(void){
	xodtemplate_timeperiod *this_timeperiod=NULL;
	xodtemplate_timeperiod *next_timeperiod=NULL;
	xodtemplate_command *this_command=NULL;
	xodtemplate_command *next_command=NULL;
	xodtemplate_contactgroup *this_contactgroup=NULL;
	xodtemplate_contactgroup *next_contactgroup=NULL;
	xodtemplate_hostgroup *this_hostgroup=NULL;
	xodtemplate_hostgroup *next_hostgroup=NULL;
	xodtemplate_servicegroup *this_servicegroup=NULL;
	xodtemplate_servicegroup *next_servicegroup=NULL;
	xodtemplate_servicedependency *this_servicedependency=NULL;
	xodtemplate_servicedependency *next_servicedependency=NULL;
	xodtemplate_serviceescalation *this_serviceescalation=NULL;
	xodtemplate_serviceescalation *next_serviceescalation=NULL;
	xodtemplate_contact *this_contact=NULL;
	xodtemplate_contact *next_contact=NULL;
	xodtemplate_host *this_host=NULL;
	xodtemplate_host *next_host=NULL;
	xodtemplate_service *this_service=NULL;
	xodtemplate_service *next_service=NULL;
	xodtemplate_hostdependency *this_hostdependency=NULL;
	xodtemplate_hostdependency *next_hostdependency=NULL;
	xodtemplate_hostescalation *this_hostescalation=NULL;
	xodtemplate_hostescalation *next_hostescalation=NULL;
	xodtemplate_hostextinfo *this_hostextinfo=NULL;
	xodtemplate_hostextinfo *next_hostextinfo=NULL;
	xodtemplate_serviceextinfo *this_serviceextinfo=NULL;
	xodtemplate_serviceextinfo *next_serviceextinfo=NULL;
	xodtemplate_customvariablesmember *this_customvariablesmember=NULL;
	xodtemplate_customvariablesmember *next_customvariablesmember=NULL;
	register int x=0;

#ifdef DEBUG0
	printf("xodtemplate_free_memory() start\n");
#endif

	/* free memory allocated to timeperiod list */
	for(this_timeperiod=xodtemplate_timeperiod_list;this_timeperiod!=NULL;this_timeperiod=next_timeperiod){
		next_timeperiod=this_timeperiod->next;
		my_free((void **)&this_timeperiod->template);
		my_free((void **)&this_timeperiod->name);
		my_free((void **)&this_timeperiod->timeperiod_name);
		my_free((void **)&this_timeperiod->alias);
		for(x=0;x<7;x++)
			my_free((void **)&this_timeperiod->timeranges[x]);
		my_free((void **)&this_timeperiod);
	        }
	xodtemplate_timeperiod_list=NULL;

	/* free memory allocated to command list */
	for(this_command=xodtemplate_command_list;this_command!=NULL;this_command=next_command){
		next_command=this_command->next;
		my_free((void **)&this_command->template);
		my_free((void **)&this_command->name);
		my_free((void **)&this_command->command_name);
		my_free((void **)&this_command->command_line);
		my_free((void **)&this_command);
	        }
	xodtemplate_command_list=NULL;

	/* free memory allocated to contactgroup list */
	for(this_contactgroup=xodtemplate_contactgroup_list;this_contactgroup!=NULL;this_contactgroup=next_contactgroup){
		next_contactgroup=this_contactgroup->next;
		my_free((void **)&this_contactgroup->template);
		my_free((void **)&this_contactgroup->name);
		my_free((void **)&this_contactgroup->contactgroup_name);
		my_free((void **)&this_contactgroup->alias);
		my_free((void **)&this_contactgroup->members);
		my_free((void **)&this_contactgroup);
	        }
	xodtemplate_contactgroup_list=NULL;

	/* free memory allocated to hostgroup list */
	for(this_hostgroup=xodtemplate_hostgroup_list;this_hostgroup!=NULL;this_hostgroup=next_hostgroup){
		next_hostgroup=this_hostgroup->next;
		my_free((void **)&this_hostgroup->template);
		my_free((void **)&this_hostgroup->name);
		my_free((void **)&this_hostgroup->hostgroup_name);
		my_free((void **)&this_hostgroup->alias);
		my_free((void **)&this_hostgroup->members);
		my_free((void **)&this_hostgroup->hostgroup_members);
		my_free((void **)&this_hostgroup);
	        }
	xodtemplate_hostgroup_list=NULL;

	/* free memory allocated to servicegroup list */
	for(this_servicegroup=xodtemplate_servicegroup_list;this_servicegroup!=NULL;this_servicegroup=next_servicegroup){
		next_servicegroup=this_servicegroup->next;
		my_free((void **)&this_servicegroup->template);
		my_free((void **)&this_servicegroup->name);
		my_free((void **)&this_servicegroup->servicegroup_name);
		my_free((void **)&this_servicegroup->alias);
		my_free((void **)&this_servicegroup->members);
		my_free((void **)&this_servicegroup->servicegroup_members);
		my_free((void **)&this_servicegroup);
	        }
	xodtemplate_servicegroup_list=NULL;

	/* free memory allocated to servicedependency list */
	for(this_servicedependency=xodtemplate_servicedependency_list;this_servicedependency!=NULL;this_servicedependency=next_servicedependency){
		next_servicedependency=this_servicedependency->next;
		my_free((void **)&this_servicedependency->template);
		my_free((void **)&this_servicedependency->name);
		my_free((void **)&this_servicedependency->servicegroup_name);
		my_free((void **)&this_servicedependency->hostgroup_name);
		my_free((void **)&this_servicedependency->host_name);
		my_free((void **)&this_servicedependency->service_description);
		my_free((void **)&this_servicedependency->dependent_servicegroup_name);
		my_free((void **)&this_servicedependency->dependent_hostgroup_name);
		my_free((void **)&this_servicedependency->dependent_host_name);
		my_free((void **)&this_servicedependency->dependent_service_description);
		my_free((void **)&this_servicedependency);
	        }
	xodtemplate_servicedependency_list=NULL;

	/* free memory allocated to serviceescalation list */
	for(this_serviceescalation=xodtemplate_serviceescalation_list;this_serviceescalation!=NULL;this_serviceescalation=next_serviceescalation){
		next_serviceescalation=this_serviceescalation->next;
		my_free((void **)&this_serviceescalation->template);
		my_free((void **)&this_serviceescalation->name);
		my_free((void **)&this_serviceescalation->servicegroup_name);
		my_free((void **)&this_serviceescalation->hostgroup_name);
		my_free((void **)&this_serviceescalation->host_name);
		my_free((void **)&this_serviceescalation->service_description);
		my_free((void **)&this_serviceescalation->escalation_period);
		my_free((void **)&this_serviceescalation->contact_groups);
		my_free((void **)&this_serviceescalation);
	        }
	xodtemplate_serviceescalation_list=NULL;

	/* free memory allocated to contact list */
	for(this_contact=xodtemplate_contact_list;this_contact!=NULL;this_contact=next_contact){

		/* free custom variables */
		this_customvariablesmember=this_contact->custom_variables;
		while(this_customvariablesmember!=NULL){
			next_customvariablesmember=this_customvariablesmember->next;
			my_free((void **)&this_customvariablesmember->variable_name);
			my_free((void **)&this_customvariablesmember->variable_value);
			my_free((void **)&this_customvariablesmember);
			this_customvariablesmember=next_customvariablesmember;
		        }

		next_contact=this_contact->next;
		my_free((void **)&this_contact->template);
		my_free((void **)&this_contact->name);
		my_free((void **)&this_contact->contact_name);
		my_free((void **)&this_contact->alias);
		my_free((void **)&this_contact->contact_groups);
		my_free((void **)&this_contact->email);
		my_free((void **)&this_contact->pager);
		for(x=0;x<MAX_XODTEMPLATE_CONTACT_ADDRESSES;x++)
			my_free((void **)&this_contact->address[x]);
		my_free((void **)&this_contact->service_notification_period);
		my_free((void **)&this_contact->service_notification_commands);
		my_free((void **)&this_contact->host_notification_period);
		my_free((void **)&this_contact->host_notification_commands);
		my_free((void **)&this_contact);
	        }
	xodtemplate_contact_list=NULL;

	/* free memory allocated to host list */
	for(this_host=xodtemplate_host_list;this_host!=NULL;this_host=next_host){

		/* free custom variables */
		this_customvariablesmember=this_host->custom_variables;
		while(this_customvariablesmember!=NULL){
			next_customvariablesmember=this_customvariablesmember->next;
			my_free((void **)&this_customvariablesmember->variable_name);
			my_free((void **)&this_customvariablesmember->variable_value);
			my_free((void **)&this_customvariablesmember);
			this_customvariablesmember=next_customvariablesmember;
		        }

		next_host=this_host->next;
		my_free((void **)&this_host->template);
		my_free((void **)&this_host->name);
		my_free((void **)&this_host->host_name);
		my_free((void **)&this_host->alias);
		my_free((void **)&this_host->address);
		my_free((void **)&this_host->parents);
		my_free((void **)&this_host->host_groups);
		my_free((void **)&this_host->check_command);
		my_free((void **)&this_host->check_period);
		my_free((void **)&this_host->event_handler);
		my_free((void **)&this_host->contact_groups);
		my_free((void **)&this_host->notification_period);
		my_free((void **)&this_host->failure_prediction_options);
		my_free((void **)&this_host);
	        }
	xodtemplate_host_list=NULL;

	/* free memory allocated to service list */
	for(this_service=xodtemplate_service_list;this_service!=NULL;this_service=next_service){

		/* free custom variables */
		this_customvariablesmember=this_service->custom_variables;
		while(this_customvariablesmember!=NULL){
			next_customvariablesmember=this_customvariablesmember->next;
			my_free((void **)&this_customvariablesmember->variable_name);
			my_free((void **)&this_customvariablesmember->variable_value);
			my_free((void **)&this_customvariablesmember);
			this_customvariablesmember=next_customvariablesmember;
		        }

		next_service=this_service->next;
		my_free((void **)&this_service->template);
		my_free((void **)&this_service->name);
		my_free((void **)&this_service->hostgroup_name);
		my_free((void **)&this_service->host_name);
		my_free((void **)&this_service->service_description);
		my_free((void **)&this_service->service_groups);
		my_free((void **)&this_service->check_command);
		my_free((void **)&this_service->check_period);
		my_free((void **)&this_service->event_handler);
		my_free((void **)&this_service->notification_period);
		my_free((void **)&this_service->contact_groups);
		my_free((void **)&this_service->failure_prediction_options);
		my_free((void **)&this_service->notes);
		my_free((void **)&this_service->notes_url);
		my_free((void **)&this_service->action_url);
		my_free((void **)&this_service->icon_image);
		my_free((void **)&this_service->icon_image_alt);
		my_free((void **)&this_service);
	        }
	xodtemplate_service_list=NULL;

	/* free memory allocated to hostdependency list */
	for(this_hostdependency=xodtemplate_hostdependency_list;this_hostdependency!=NULL;this_hostdependency=next_hostdependency){
		next_hostdependency=this_hostdependency->next;
		my_free((void **)&this_hostdependency->template);
		my_free((void **)&this_hostdependency->name);
		my_free((void **)&this_hostdependency->hostgroup_name);
		my_free((void **)&this_hostdependency->dependent_hostgroup_name);
		my_free((void **)&this_hostdependency->host_name);
		my_free((void **)&this_hostdependency->dependent_host_name);
		my_free((void **)&this_hostdependency);
	        }
	xodtemplate_hostdependency_list=NULL;

	/* free memory allocated to hostescalation list */
	for(this_hostescalation=xodtemplate_hostescalation_list;this_hostescalation!=NULL;this_hostescalation=next_hostescalation){
		next_hostescalation=this_hostescalation->next;
		my_free((void **)&this_hostescalation->template);
		my_free((void **)&this_hostescalation->name);
		my_free((void **)&this_hostescalation->hostgroup_name);
		my_free((void **)&this_hostescalation->host_name);
		my_free((void **)&this_hostescalation->escalation_period);
		my_free((void **)&this_hostescalation->contact_groups);
		my_free((void **)&this_hostescalation);
	        }
	xodtemplate_hostescalation_list=NULL;

	/* free memory allocated to hostextinfo list */
	for(this_hostextinfo=xodtemplate_hostextinfo_list;this_hostextinfo!=NULL;this_hostextinfo=next_hostextinfo){
		next_hostextinfo=this_hostextinfo->next;
		my_free((void **)&this_hostextinfo->template);
		my_free((void **)&this_hostextinfo->name);
		my_free((void **)&this_hostextinfo->host_name);
		my_free((void **)&this_hostextinfo->hostgroup_name);
		my_free((void **)&this_hostextinfo->notes);
		my_free((void **)&this_hostextinfo->notes_url);
		my_free((void **)&this_hostextinfo->action_url);
		my_free((void **)&this_hostextinfo->icon_image);
		my_free((void **)&this_hostextinfo->icon_image_alt);
		my_free((void **)&this_hostextinfo->vrml_image);
		my_free((void **)&this_hostextinfo->statusmap_image);
		my_free((void **)&this_hostextinfo);
	        }
	xodtemplate_hostextinfo_list=NULL;

	/* free memory allocated to serviceextinfo list */
	for(this_serviceextinfo=xodtemplate_serviceextinfo_list;this_serviceextinfo!=NULL;this_serviceextinfo=next_serviceextinfo){
		next_serviceextinfo=this_serviceextinfo->next;
		my_free((void **)&this_serviceextinfo->template);
		my_free((void **)&this_serviceextinfo->name);
		my_free((void **)&this_serviceextinfo->host_name);
		my_free((void **)&this_serviceextinfo->hostgroup_name);
		my_free((void **)&this_serviceextinfo->service_description);
		my_free((void **)&this_serviceextinfo->notes);
		my_free((void **)&this_serviceextinfo->notes_url);
		my_free((void **)&this_serviceextinfo->action_url);
		my_free((void **)&this_serviceextinfo->icon_image);
		my_free((void **)&this_serviceextinfo->icon_image_alt);
		my_free((void **)&this_serviceextinfo);
	        }
	xodtemplate_serviceextinfo_list=NULL;

	/* free memory for the config file names */
	for(x=0;x<xodtemplate_current_config_file;x++)
		my_free((void **)&xodtemplate_config_files[x]);
	my_free((void **)&xodtemplate_config_files);
	xodtemplate_current_config_file=0;


#ifdef DEBUG0
	printf("xodtemplate_free_memory() end\n");
#endif

	return OK;
        }


/* frees memory allocated to a temporary contact list */
int xodtemplate_free_contactlist(xodtemplate_contactlist *temp_list){
	xodtemplate_contactlist *this_contactlist=NULL;
	xodtemplate_contactlist *next_contactlist=NULL;

#ifdef DEBUG0
	printf("xodtemplate_free_contactlist() start\n");
#endif

	/* free memory allocated to contact name list */
	for(this_contactlist=temp_list;this_contactlist!=NULL;this_contactlist=next_contactlist){
		next_contactlist=this_contactlist->next;
		my_free((void **)&this_contactlist->contact_name);
		my_free((void **)&this_contactlist);
	        }

	temp_list=NULL;

#ifdef DEBUG0
	printf("xodtemplate_free_contactlist() end\n");
#endif

	return OK;
        }


/* remove an entry from the contact list */
void xodtemplate_remove_contactlist_item(xodtemplate_contactlist *item,xodtemplate_contactlist **list){
	xodtemplate_contactlist *temp_item=NULL;

#ifdef DEBUG0
	printf("xodtemplate_remove_contactlist_item() start\n");
#endif

	if(item==NULL || list==NULL)
		return;

	if(*list==NULL)
		return;

	if(*list==item)
		*list=item->next;

	else{

		for(temp_item=*list;temp_item!=NULL;temp_item=temp_item->next){
			if(temp_item->next==item){
				temp_item->next=item->next;
				my_free((void **)&item->contact_name);
				my_free((void **)&item);
				break;
			        }
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_remove_contactlist_item() end\n");
#endif

	return;
        }



/* frees memory allocated to a temporary host list */
int xodtemplate_free_hostlist(xodtemplate_hostlist *temp_list){
	xodtemplate_hostlist *this_hostlist=NULL;
	xodtemplate_hostlist *next_hostlist=NULL;

#ifdef DEBUG0
	printf("xodtemplate_free_hostlist() start\n");
#endif

	/* free memory allocated to host name list */
	for(this_hostlist=temp_list;this_hostlist!=NULL;this_hostlist=next_hostlist){
		next_hostlist=this_hostlist->next;
		my_free((void **)&this_hostlist->host_name);
		my_free((void **)&this_hostlist);
	        }

	temp_list=NULL;

#ifdef DEBUG0
	printf("xodtemplate_free_hostlist() end\n");
#endif

	return OK;
        }


/* remove an entry from the host list */
void xodtemplate_remove_hostlist_item(xodtemplate_hostlist *item,xodtemplate_hostlist **list){
	xodtemplate_hostlist *temp_item=NULL;

#ifdef DEBUG0
	printf("xodtemplate_remove_hostlist_item() start\n");
#endif

	if(item==NULL || list==NULL)
		return;

	if(*list==NULL)
		return;

	if(*list==item)
		*list=item->next;

	else{

		for(temp_item=*list;temp_item!=NULL;temp_item=temp_item->next){
			if(temp_item->next==item){
				temp_item->next=item->next;
				my_free((void **)&item->host_name);
				my_free((void **)&item);
				break;
			        }
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_remove_hostlist_item() end\n");
#endif

	return;
        }


/* frees memory allocated to a temporary service list */
int xodtemplate_free_servicelist(xodtemplate_servicelist *temp_list){
	xodtemplate_servicelist *this_servicelist=NULL;
	xodtemplate_servicelist *next_servicelist=NULL;

#ifdef DEBUG0
	printf("xodtemplate_free_servicelist() start\n");
#endif

	/* free memory allocated to service name list */
	for(this_servicelist=temp_list;this_servicelist!=NULL;this_servicelist=next_servicelist){
		next_servicelist=this_servicelist->next;
		my_free((void **)&this_servicelist->host_name);
		my_free((void **)&this_servicelist->service_description);
		my_free((void **)&this_servicelist);
	        }

	temp_list=NULL;

#ifdef DEBUG0
	printf("xodtemplate_free_servicelist() end\n");
#endif

	return OK;
        }


/* remove an entry from the service list */
void xodtemplate_remove_servicelist_item(xodtemplate_servicelist *item,xodtemplate_servicelist **list){
	xodtemplate_servicelist *temp_item=NULL;

#ifdef DEBUG0
	printf("xodtemplate_remove_servicelist_item() start\n");
#endif

	if(item==NULL || list==NULL)
		return;

	if(*list==NULL)
		return;

	if(*list==item)
		*list=item->next;

	else{

		for(temp_item=*list;temp_item!=NULL;temp_item=temp_item->next){
			if(temp_item->next==item){
				temp_item->next=item->next;
				my_free((void **)&item->host_name);
				my_free((void **)&item->service_description);
				my_free((void **)&item);
				break;
			        }
		        }
	        }

#ifdef DEBUG0
	printf("xodtemplate_remove_servicelist_item() end\n");
#endif

	return;
        }



/******************************************************************/
/********************** UTILITY FUNCTIONS *************************/
/******************************************************************/

#ifdef NSCORE

/* expands a comma-delimited list of contact names */
xodtemplate_contactlist *xodtemplate_expand_contacts(char *contacts){
	xodtemplate_contactlist *temp_list=NULL;
	xodtemplate_contactlist *reject_list=NULL;
	xodtemplate_contactlist *list_ptr=NULL;
	xodtemplate_contactlist *reject_ptr=NULL;
	int result;

#ifdef DEBUG0
	printf("xodtemplate_expand_contacts() start\n");
#endif

	/* process contact names */
	if(contacts!=NULL){

		/* expand contacts */
		result=xodtemplate_expand_contacts2(&temp_list,&reject_list,contacts);
		if(result!=OK){
			xodtemplate_free_contactlist(temp_list);
			xodtemplate_free_contactlist(reject_list);
			return NULL;
		        }

		/* remove rejects (if any) from the list (no duplicate entries exist in either list) */
		for(reject_ptr=reject_list;reject_ptr!=NULL;reject_ptr=reject_ptr->next){
			for(list_ptr=temp_list;list_ptr!=NULL;list_ptr=list_ptr->next){
				if(!strcmp(reject_ptr->contact_name,list_ptr->contact_name)){
					xodtemplate_remove_contactlist_item(list_ptr,&temp_list);
					break;
			                }
		                }
	                }
		xodtemplate_free_contactlist(reject_list);
		reject_list=NULL;
	        }

#ifdef DEBUG0
	printf("xodtemplate_expand_contacts() end\n");
#endif

	return temp_list;
        }



/* expands contacts */
int xodtemplate_expand_contacts2(xodtemplate_contactlist **list, xodtemplate_contactlist **reject_list,char *contacts){
	char *contact_names=NULL;
	char *temp_ptr=NULL;
	xodtemplate_contact *temp_contact=NULL;
	regex_t preg;
	int found_match=TRUE;
	int reject_item=FALSE;
	int use_regexp=FALSE;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_expand_contacts2() start\n");
#endif

	if(list==NULL || contacts==NULL)
		return ERROR;

	if((contact_names=(char *)strdup(contacts))==NULL)
		return ERROR;

	/* expand each contact name */
	for(temp_ptr=strtok(contact_names,",");temp_ptr;temp_ptr=strtok(NULL,",")){

		found_match=FALSE;
		reject_item=FALSE;

		/* strip trailing spaces */
		strip(temp_ptr);

		/* should we use regular expression matching? */
		if(use_regexp_matches==TRUE && (use_true_regexp_matching==TRUE || strstr(temp_ptr,"*") || strstr(temp_ptr,"?")))
			use_regexp=TRUE;

		/* use regular expression matching */
		if(use_regexp==TRUE){

			/* compile regular expression */
			if(regcomp(&preg,temp_ptr,0)){
				my_free((void **)&contact_names);
				return ERROR;
		                }
			
			/* test match against all contacts */
			for(temp_contact=xodtemplate_contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){

				if(temp_contact->contact_name==NULL)
					continue;

				/* skip this contact if it did not match the expression */
				if(regexec(&preg,temp_contact->contact_name,0,NULL,0))
					continue;

				found_match=TRUE;

				/* dont' add contacts that shouldn't be registered */
				if(temp_contact->register_object==FALSE)
					continue;

				/* add contact to list */
				xodtemplate_add_contact_to_contactlist(list,temp_contact->contact_name);
		                } 

			/* free memory allocated to compiled regexp */
			regfree(&preg);
		        }
		
		/* use standard matching... */
		else{

			/* return a list of all contacts */
			if(!strcmp(temp_ptr,"*")){

				found_match=TRUE;

				for(temp_contact=xodtemplate_contact_list;temp_contact!=NULL;temp_contact=temp_contact->next){

					if(temp_contact->contact_name==NULL)
						continue;

					/* dont' add contacts that shouldn't be registered */
					if(temp_contact->register_object==FALSE)
						continue;

					/* add contact to list */
					xodtemplate_add_contact_to_contactlist(list,temp_contact->contact_name);
				        }
			        }

			/* else this is just a single contact... */
			else{

				/* this contact should be excluded (rejected) */
				if(temp_ptr[0]=='!'){
					reject_item=TRUE;
					temp_ptr++;
			                }

				/* find the contact */
				temp_contact=xodtemplate_find_real_contact(temp_ptr);
				if(temp_contact!=NULL){

					found_match=TRUE;

					/* add contact to list */
					xodtemplate_add_contact_to_contactlist((reject_item==TRUE)?reject_list:list,temp_ptr);
				        }
			        }
		        }

		if(found_match==FALSE){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not find any contact matching '%s'\n",temp_ptr);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			break;
	                }
	        }

	/* free memory */
	my_free((void **)&contact_names);

#ifdef DEBUG0
	printf("xodtemplate_expand_contacts2() end\n");
#endif

	if(found_match==FALSE)
		return ERROR;

	return OK;
        }



/* adds a contact entry to the list of expanded (accepted) or rejected contacts */
int xodtemplate_add_contact_to_contactlist(xodtemplate_contactlist **list, char *contact_name){
	xodtemplate_contactlist *temp_item=NULL;
	xodtemplate_contactlist *new_item=NULL;

	if(list==NULL || contact_name==NULL)
		return ERROR;

	/* skip this contact if its already in the list */
	for(temp_item=*list;temp_item;temp_item=temp_item->next)
		if(!strcmp(temp_item->contact_name,contact_name))
			break;
	if(temp_item)
		return OK;

	/* allocate memory for a new list item */
	if((new_item=(xodtemplate_contactlist *)malloc(sizeof(xodtemplate_contactlist)))==NULL)
		return ERROR;

	/* save the contact name */
	if((new_item->contact_name=(char *)strdup(contact_name))==NULL){
		my_free((void **)&new_item);
		return ERROR;
	        }

	/* add new item to head of list */
	new_item->next=*list;
	*list=new_item;

	return OK;
        }



/* expands a comma-delimited list of hostgroups and/or hosts to member host names */
xodtemplate_hostlist *xodtemplate_expand_hostgroups_and_hosts(char *hostgroups,char *hosts){
	xodtemplate_hostlist *temp_list=NULL;
	xodtemplate_hostlist *reject_list=NULL;
	xodtemplate_hostlist *list_ptr=NULL;
	xodtemplate_hostlist *reject_ptr=NULL;
	int result=OK;

#ifdef DEBUG0
	printf("xodtemplate_expand_hostgroups_and_hosts() start\n");
#endif

	/* process list of hostgroups... */
	if(hostgroups!=NULL){

		/* expand host */
		result=xodtemplate_expand_hostgroups(&temp_list,&reject_list,hostgroups);
		if(result!=OK){
			xodtemplate_free_hostlist(temp_list);
			xodtemplate_free_hostlist(reject_list);
			return NULL;
		        }

		/* remove rejects (if any) from the list (no duplicate entries exist in either list) */
		for(reject_ptr=reject_list;reject_ptr!=NULL;reject_ptr=reject_ptr->next){
			for(list_ptr=temp_list;list_ptr!=NULL;list_ptr=list_ptr->next){
				if(!strcmp(reject_ptr->host_name,list_ptr->host_name)){
					xodtemplate_remove_hostlist_item(list_ptr,&temp_list);
					break;
			                }
		                }
	                }

		xodtemplate_free_hostlist(reject_list);
		reject_list=NULL;
	        }

	/* process host names */
	if(hosts!=NULL){

		/* expand hosts */
		result=xodtemplate_expand_hosts(&temp_list,&reject_list,hosts);
		if(result!=OK){
			xodtemplate_free_hostlist(temp_list);
			xodtemplate_free_hostlist(reject_list);
			return NULL;
		        }

		/* remove rejects (if any) from the list (no duplicate entries exist in either list) */
		/* NOTE: rejects from this list also affect hosts generated from processing hostgroup names (see above) */
		for(reject_ptr=reject_list;reject_ptr!=NULL;reject_ptr=reject_ptr->next){
			for(list_ptr=temp_list;list_ptr!=NULL;list_ptr=list_ptr->next){
				if(!strcmp(reject_ptr->host_name,list_ptr->host_name)){
					xodtemplate_remove_hostlist_item(list_ptr,&temp_list);
					break;
			                }
		                }
	                }
		xodtemplate_free_hostlist(reject_list);
		reject_list=NULL;
	        }

#ifdef DEBUG0
	printf("xodtemplate_expand_hostgroups_and_hosts() end\n");
#endif

	return temp_list;
        }



/* expands hostgroups */
int xodtemplate_expand_hostgroups(xodtemplate_hostlist **list, xodtemplate_hostlist **reject_list, char *hostgroups){
	char *hostgroup_names=NULL;
	char *temp_ptr=NULL;
	xodtemplate_hostgroup *temp_hostgroup=NULL;
	regex_t preg;
	int found_match=TRUE;
	int reject_item=FALSE;
	int use_regexp=FALSE;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_expand_hostgroups() start\n");
#endif

	if(list==NULL || hostgroups==NULL)
		return ERROR;

	/* allocate memory for hostgroup name list */
	if((hostgroup_names=(char *)strdup(hostgroups))==NULL)
		return ERROR;

	for(temp_ptr=strtok(hostgroup_names,",");temp_ptr;temp_ptr=strtok(NULL,",")){

		found_match=FALSE;
		reject_item=FALSE;
		
		/* strip trailing spaces */
		strip(temp_ptr);

		/* should we use regular expression matching? */
		if(use_regexp_matches==TRUE && (use_true_regexp_matching==TRUE || strstr(temp_ptr,"*") || strstr(temp_ptr,"?")))
			use_regexp=TRUE;
		else
			use_regexp=FALSE;

		/* use regular expression matching */
		if(use_regexp==TRUE){

			/* compile regular expression */
			if(regcomp(&preg,temp_ptr,0)){
				my_free((void **)&hostgroup_names);
				return ERROR;
		                }
			
			/* test match against all hostgroup names */
			for(temp_hostgroup=xodtemplate_hostgroup_list;temp_hostgroup!=NULL;temp_hostgroup=temp_hostgroup->next){

				if(temp_hostgroup->hostgroup_name==NULL)
					continue;

				/* skip this hostgroup if it did not match the expression */
				if(regexec(&preg,temp_hostgroup->hostgroup_name,0,NULL,0))
					continue;

				found_match=TRUE;

				/* dont' add hostgroups that shouldn't be registered */
				if(temp_hostgroup->register_object==FALSE)
					continue;

				/* add hostgroup members to list */
				xodtemplate_add_hostgroup_members_to_hostlist(list,temp_hostgroup);
		                } 

			/* free memory allocated to compiled regexp */
			regfree(&preg);
		        }
		
		/* use standard matching... */
		else{

			/* return a list of all hostgroups */
			if(!strcmp(temp_ptr,"*")){

				found_match=TRUE;

				for(temp_hostgroup=xodtemplate_hostgroup_list;temp_hostgroup!=NULL;temp_hostgroup=temp_hostgroup->next){	

					/* dont' add hostgroups that shouldn't be registered */
					if(temp_hostgroup->register_object==FALSE)
						continue;

					/* add hostgroup to list */
					xodtemplate_add_hostgroup_members_to_hostlist(list,temp_hostgroup);
				        }
			        }

			/* else this is just a single hostgroup... */
			else{
			
				/* this hostgroup should be excluded (rejected) */
				if(temp_ptr[0]=='!'){
					reject_item=TRUE;
					temp_ptr++;
			        	}

				/* find the hostgroup */
				temp_hostgroup=xodtemplate_find_real_hostgroup(temp_ptr);
				if(temp_hostgroup!=NULL){

					found_match=TRUE;

					/* add hostgroup members to proper list */
					xodtemplate_add_hostgroup_members_to_hostlist((reject_item==TRUE)?reject_list:list,temp_hostgroup);
				        }
			        }
		        }

		if(found_match==FALSE){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not find any hostgroup matching '%s'\n",temp_ptr);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			break;
	                }
	        }

	/* free memory */
	my_free((void **)&hostgroup_names);

#ifdef DEBUG0
	printf("xodtemplate_expand_hostgroups() end\n");
#endif

	if(found_match==FALSE)
		return ERROR;

	return OK;
        }



/* expands hosts */
int xodtemplate_expand_hosts(xodtemplate_hostlist **list, xodtemplate_hostlist **reject_list,char *hosts){
	char *host_names=NULL;
	char *temp_ptr=NULL;
	xodtemplate_host *temp_host=NULL;
	regex_t preg;
	int found_match=TRUE;
	int reject_item=FALSE;
	int use_regexp=FALSE;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_expand_hosts() start\n");
#endif

	if(list==NULL || hosts==NULL)
		return ERROR;

	if((host_names=(char *)strdup(hosts))==NULL)
		return ERROR;

	/* expand each host name */
	for(temp_ptr=strtok(host_names,",");temp_ptr;temp_ptr=strtok(NULL,",")){

		found_match=FALSE;
		reject_item=FALSE;

		/* strip trailing spaces */
		strip(temp_ptr);

		/* should we use regular expression matching? */
		if(use_regexp_matches==TRUE && (use_true_regexp_matching==TRUE || strstr(temp_ptr,"*") || strstr(temp_ptr,"?")))
			use_regexp=TRUE;

		/* use regular expression matching */
		if(use_regexp==TRUE){

			/* compile regular expression */
			if(regcomp(&preg,temp_ptr,0)){
				my_free((void **)&host_names);
				return ERROR;
		                }
			
			/* test match against all hosts */
			for(temp_host=xodtemplate_host_list;temp_host!=NULL;temp_host=temp_host->next){

				if(temp_host->host_name==NULL)
					continue;

				/* skip this host if it did not match the expression */
				if(regexec(&preg,temp_host->host_name,0,NULL,0))
					continue;

				found_match=TRUE;

				/* dont' add hosts that shouldn't be registered */
				if(temp_host->register_object==FALSE)
					continue;

				/* add host to list */
				xodtemplate_add_host_to_hostlist(list,temp_host->host_name);
		                } 

			/* free memory allocated to compiled regexp */
			regfree(&preg);
		        }
		
		/* use standard matching... */
		else{

			/* return a list of all hosts */
			if(!strcmp(temp_ptr,"*")){

				found_match=TRUE;

				for(temp_host=xodtemplate_host_list;temp_host!=NULL;temp_host=temp_host->next){

					if(temp_host->host_name==NULL)
						continue;

					/* dont' add hosts that shouldn't be registered */
					if(temp_host->register_object==FALSE)
						continue;

					/* add host to list */
					xodtemplate_add_host_to_hostlist(list,temp_host->host_name);
				        }
			        }

			/* else this is just a single host... */
			else{

				/* this host should be excluded (rejected) */
				if(temp_ptr[0]=='!'){
					reject_item=TRUE;
					temp_ptr++;
			                }

				/* find the host */
				temp_host=xodtemplate_find_real_host(temp_ptr);
				if(temp_host!=NULL){

					found_match=TRUE;

					/* add host to list */
					xodtemplate_add_host_to_hostlist((reject_item==TRUE)?reject_list:list,temp_ptr);
				        }
			        }
		        }

		if(found_match==FALSE){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not find any host matching '%s'\n",temp_ptr);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			break;
	                }
	        }

	/* free memory */
	my_free((void **)&host_names);

#ifdef DEBUG0
	printf("xodtemplate_expand_hosts() end\n");
#endif

	if(found_match==FALSE)
		return ERROR;

	return OK;
        }


/* adds members of a hostgroups to the list of expanded (accepted) or rejected hosts */
int xodtemplate_add_hostgroup_members_to_hostlist(xodtemplate_hostlist **list, xodtemplate_hostgroup *temp_hostgroup){
	char *group_members=NULL;
	char *member_name=NULL;
	char *member_ptr=NULL;

	if(list==NULL || temp_hostgroup==NULL)
		return ERROR;

	/* skip hostgroups with no defined members */
	if(temp_hostgroup->members==NULL)
		return OK;

	/* save a copy of the members */
	if((group_members=(char *)strdup(temp_hostgroup->members))==NULL)
		return ERROR;

	/* process all hosts that belong to the hostgroup */
	/* NOTE: members of the group have already have been expanded by xodtemplate_recombobulate_hostgroups(), so we don't need to do it here */
	member_ptr=group_members;
	for(member_name=my_strsep(&member_ptr,",");member_name!=NULL;member_name=my_strsep(&member_ptr,",")){

		/* strip trailing spaces from member name */
		strip(member_name);

		/* add host to the list */
		xodtemplate_add_host_to_hostlist(list,member_name);
	        }

	my_free((void **)&group_members);

	return OK;
        }


/* adds a host entry to the list of expanded (accepted) or rejected hosts */
int xodtemplate_add_host_to_hostlist(xodtemplate_hostlist **list, char *host_name){
	xodtemplate_hostlist *temp_item=NULL;
	xodtemplate_hostlist *new_item=NULL;

	if(list==NULL || host_name==NULL)
		return ERROR;

	/* skip this host if its already in the list */
	for(temp_item=*list;temp_item;temp_item=temp_item->next)
		if(!strcmp(temp_item->host_name,host_name))
			break;
	if(temp_item)
		return OK;

	/* allocate memory for a new list item */
	if((new_item=(xodtemplate_hostlist *)malloc(sizeof(xodtemplate_hostlist)))==NULL)
		return ERROR;

	/* save the host name */
	if((new_item->host_name=(char *)strdup(host_name))==NULL){
		my_free((void **)&new_item);
		return ERROR;
	        }

	/* add new item to head of list */
	new_item->next=*list;
	*list=new_item;

	return OK;
        }


/* expands a comma-delimited list of servicegroups and/or service descriptions */
xodtemplate_servicelist *xodtemplate_expand_servicegroups_and_services(char *servicegroups,char *host_name,char *services){
	xodtemplate_servicelist *temp_list=NULL;
	xodtemplate_servicelist *reject_list=NULL;
	xodtemplate_servicelist *list_ptr=NULL;
	xodtemplate_servicelist *reject_ptr=NULL;
	int result=OK;

#ifdef DEBUG0
	printf("xodtemplate_expand_servicegroups_and_services() start\n");
#endif

	/* process list of servicegroups... */
	if(servicegroups!=NULL){

		/* expand services */
		result=xodtemplate_expand_servicegroups(&temp_list,&reject_list,servicegroups);
		if(result!=OK){
			xodtemplate_free_servicelist(temp_list);
			xodtemplate_free_servicelist(reject_list);
			return NULL;
		        }

		/* remove rejects (if any) from the list (no duplicate entries exist in either list) */
		for(reject_ptr=reject_list;reject_ptr!=NULL;reject_ptr=reject_ptr->next){
			for(list_ptr=temp_list;list_ptr!=NULL;list_ptr=list_ptr->next){
				if(!strcmp(reject_ptr->host_name,list_ptr->host_name) && !strcmp(reject_ptr->service_description,list_ptr->service_description)){
					xodtemplate_remove_servicelist_item(list_ptr,&temp_list);
					break;
			                }
		                }
	                }
		xodtemplate_free_servicelist(reject_list);
		reject_list=NULL;
	        }

	/* process service names */
	if(host_name!=NULL && services!=NULL){

		/* expand services */
		result=xodtemplate_expand_services(&temp_list,&reject_list,host_name,services);
		if(result!=OK){
			xodtemplate_free_servicelist(temp_list);
			xodtemplate_free_servicelist(reject_list);
			return NULL;
		        }

		/* remove rejects (if any) from the list (no duplicate entries exist in either list) */
		/* NOTE: rejects from this list also affect hosts generated from processing hostgroup names (see above) */
		for(reject_ptr=reject_list;reject_ptr!=NULL;reject_ptr=reject_ptr->next){
			for(list_ptr=temp_list;list_ptr!=NULL;list_ptr=list_ptr->next){
				if(!strcmp(reject_ptr->host_name,list_ptr->host_name) && !strcmp(reject_ptr->service_description,list_ptr->service_description)){
					xodtemplate_remove_servicelist_item(list_ptr,&temp_list);
					break;
			                }
		                }
	                }
		xodtemplate_free_servicelist(reject_list);
		reject_list=NULL;
	        }

#ifdef DEBUG0
	printf("xodtemplate_expand_servicegroups_and_services() end\n");
#endif

	return temp_list;
        }


/* expands servicegroups */
int xodtemplate_expand_servicegroups(xodtemplate_servicelist **list, xodtemplate_servicelist **reject_list, char *servicegroups){
	xodtemplate_servicegroup  *temp_servicegroup=NULL;
	regex_t preg;
	char *servicegroup_names=NULL;
	char *temp_ptr=NULL;
	int found_match=TRUE;
	int reject_item=FALSE;
	int use_regexp=FALSE;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_expand_servicegroups() start\n");
#endif

	if(list==NULL || servicegroups==NULL)
		return ERROR;

	/* allocate memory for servicegroup name list */
	if((servicegroup_names=(char *)strdup(servicegroups))==NULL)
		return ERROR;

	/* expand each servicegroup */
	for(temp_ptr=strtok(servicegroup_names,",");temp_ptr;temp_ptr=strtok(NULL,",")){

		found_match=FALSE;
		reject_item=FALSE;
		
		/* strip trailing spaces */
		strip(temp_ptr);

		/* should we use regular expression matching? */
		if(use_regexp_matches==TRUE && (use_true_regexp_matching==TRUE || strstr(temp_ptr,"*") || strstr(temp_ptr,"?")))
			use_regexp=TRUE;
		else
			use_regexp=FALSE;

		/* use regular expression matching */
		if(use_regexp==TRUE){

			/* compile regular expression */
			if(regcomp(&preg,temp_ptr,0)){
				my_free((void **)&servicegroup_names);
				return ERROR;
		                }
			
			/* test match against all servicegroup names */
			for(temp_servicegroup=xodtemplate_servicegroup_list;temp_servicegroup!=NULL;temp_servicegroup=temp_servicegroup->next){

				if(temp_servicegroup->servicegroup_name==NULL)
					continue;

				/* skip this servicegroup if it did not match the expression */
				if(regexec(&preg,temp_servicegroup->servicegroup_name,0,NULL,0))
					continue;

				found_match=TRUE;

				/* dont' add servicegroups that shouldn't be registered */
				if(temp_servicegroup->register_object==FALSE)
					continue;

				/* add servicegroup members to list */
				xodtemplate_add_servicegroup_members_to_servicelist(list,temp_servicegroup);
			        }

			/* free memory allocated to compiled regexp */
			regfree(&preg);
		        }
		
		/* use standard matching... */
		else{

			/* return a list of all servicegroups */
			if(!strcmp(temp_ptr,"*")){

				found_match=TRUE;

				for(temp_servicegroup=xodtemplate_servicegroup_list;temp_servicegroup!=NULL;temp_servicegroup=temp_servicegroup->next){	

					/* dont' add servicegroups that shouldn't be registered */
					if(temp_servicegroup->register_object==FALSE)
						continue;

					/* add servicegroup to list */
					xodtemplate_add_servicegroup_members_to_servicelist(list,temp_servicegroup);
				        }
			        }

			/* else this is just a single servicegroup... */
			else{
			
				/* this servicegroup should be excluded (rejected) */
				if(temp_ptr[0]=='!'){
					reject_item=TRUE;
					temp_ptr++;
		                        }

				/* find the servicegroup */
				if((temp_servicegroup=xodtemplate_find_real_servicegroup(temp_ptr))!=NULL){

					found_match=TRUE;

					/* add servicegroup members to list */
					xodtemplate_add_servicegroup_members_to_servicelist((reject_item==TRUE)?reject_list:list,temp_servicegroup);
				        }
			        }
		        }

		/* we didn't find a matching servicegroup */
		if(found_match==FALSE){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not find any servicegroup matching '%s'\n",temp_ptr);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			break;
		        }
	        }

	/* free memory */
	my_free((void **)&servicegroup_names);

#ifdef DEBUG0
	printf("xodtemplate_expand_servicegroups() end\n");
#endif

	if(found_match==FALSE)
		return ERROR;

	return OK;
        }


/* expands services (host name is not expanded) */
int xodtemplate_expand_services(xodtemplate_servicelist **list, xodtemplate_servicelist **reject_list, char *host_name, char *services){
	char *service_names=NULL;
	char *temp_ptr=NULL;
	xodtemplate_service *temp_service=NULL;
	regex_t preg;
	regex_t preg2;
	int found_match=TRUE;
	int reject_item=FALSE;
	int use_regexp_host=FALSE;
	int use_regexp_service=FALSE;
#ifdef NSCORE
	char *temp_buffer=NULL;
#endif

#ifdef DEBUG0
	printf("xodtemplate_expand_services() start\n");
#endif

	if(list==NULL || host_name==NULL || services==NULL)
		return ERROR;

	/* should we use regular expression matching for the host name? */
	if(use_regexp_matches==TRUE && (use_true_regexp_matching==TRUE || strstr(host_name,"*") || strstr(host_name,"?")))
		use_regexp_host=TRUE;

	/* compile regular expression for host name */
	if(use_regexp_host==TRUE){
		if(regcomp(&preg2,host_name,0))
			return ERROR;
	        }

	if((service_names=(char *)strdup(services))==NULL){
		if(use_regexp_host==TRUE)
			regfree(&preg2);
		return ERROR;
	        }

	/* expand each service description */
	for(temp_ptr=strtok(service_names,",");temp_ptr;temp_ptr=strtok(NULL,",")){

		found_match=FALSE;
		reject_item=FALSE;

		/* strip trailing spaces */
		strip(temp_ptr);

		/* should we use regular expression matching for the service description? */
		if(use_regexp_matches==TRUE && (use_true_regexp_matching==TRUE || strstr(temp_ptr,"*") || strstr(temp_ptr,"?")))
			use_regexp_service=TRUE;
		else
			use_regexp_service=FALSE;

		/* compile regular expression for service description */
		if(use_regexp_service==TRUE){
			if(regcomp(&preg,temp_ptr,0)){
				if(use_regexp_host==TRUE)
					regfree(&preg2);
				my_free((void **)&service_names);
				return ERROR;
			        }
			}

		/* use regular expression matching */
		if(use_regexp_host==TRUE || use_regexp_service==TRUE){

			/* test match against all services */
			for(temp_service=xodtemplate_service_list;temp_service!=NULL;temp_service=temp_service->next){

				if(temp_service->host_name==NULL || temp_service->service_description==NULL)
					continue;

				/* skip this service if it doesn't match the host name expression */
				if(use_regexp_host==TRUE){
					if(regexec(&preg2,temp_service->host_name,0,NULL,0))
						continue;
				        }
				else{
					if(strcmp(temp_service->host_name,host_name))
						continue;
				        }

				/* skip this service if it doesn't match the service description expression */
				if(use_regexp_service==TRUE){
					if(regexec(&preg,temp_service->service_description,0,NULL,0))
						continue;
				        }
				else{
					if(strcmp(temp_service->service_description,temp_ptr))
						continue;
				        }

				found_match=TRUE;

				/* dont' add services that shouldn't be registered */
				if(temp_service->register_object==FALSE)
					continue;

				/* add service to the list */
				xodtemplate_add_service_to_servicelist(list,host_name,temp_service->service_description);
			        }

			/* free memory allocated to compiled regexp */
			regfree(&preg);
		        }

		/* use standard matching... */
		else{

			/* return a list of all services on the host */
			if(!strcmp(temp_ptr,"*")){

				found_match=TRUE;

				for(temp_service=xodtemplate_service_list;temp_service!=NULL;temp_service=temp_service->next){

					if(temp_service->host_name==NULL || temp_service->service_description==NULL)
						continue;

					if(strcmp(temp_service->host_name,host_name))
						continue;

					/* dont' add services that shouldn't be registered */
					if(temp_service->register_object==FALSE)
						continue;

					/* add service to the list */
					xodtemplate_add_service_to_servicelist(list,host_name,temp_service->service_description);
				        }
			        }

			/* else this is just a single service... */
			else{

				/* this service should be excluded (rejected) */
				if(temp_ptr[0]=='!'){
					reject_item=TRUE;
					temp_ptr++;
		                        }

				/* find the service */
				if((temp_service=xodtemplate_find_real_service(host_name,temp_ptr))!=NULL){

					found_match=TRUE;

					/* add service to the list */
					xodtemplate_add_service_to_servicelist((reject_item==TRUE)?reject_list:list,host_name,temp_service->service_description);
				        }
			        }
		        }

		/* we didn't find a match */
		if(found_match==FALSE){
#ifdef NSCORE
			asprintf(&temp_buffer,"Error: Could not find a service matching host name '%s' and description '%s'\n",host_name,temp_ptr);
			write_to_logs_and_console(temp_buffer,NSLOG_CONFIG_ERROR,TRUE);
			my_free((void **)&temp_buffer);
#endif
			break;
	                }
	        }

	if(use_regexp_host==TRUE)
		regfree(&preg2);
	my_free((void **)&service_names);

#ifdef DEBUG0
	printf("xodtemplate_expand_services() end\n");
#endif

	if(found_match==FALSE)
		return ERROR;

	return OK;
        }


/* adds members of a servicegroups to the list of expanded services */
int xodtemplate_add_servicegroup_members_to_servicelist(xodtemplate_servicelist **list, xodtemplate_servicegroup *temp_servicegroup){
	char *group_members=NULL;
	char *member_name=NULL;
	char *host_name=NULL;
	char *member_ptr=NULL;

	if(list==NULL || temp_servicegroup==NULL)
		return ERROR;

	/* skip servicegroups with no defined members */
	if(temp_servicegroup->members==NULL)
		return OK;

	/* save a copy of the members */
	if((group_members=(char *)strdup(temp_servicegroup->members))==NULL)
		return ERROR;

	/* process all services that belong to the servicegroup */
	/* NOTE: members of the group have already have been expanded by xodtemplate_recombobulate_servicegroups(), so we don't need to do it here */
	member_ptr=group_members;
	for(member_name=my_strsep(&member_ptr,",");member_name!=NULL;member_name=my_strsep(&member_ptr,",")){

		/* strip trailing spaces from member name */
		strip(member_name);

		/* host name */
		if(host_name==NULL){
			if((host_name=(char *)strdup(member_name))==NULL){
				my_free((void **)&group_members);
				return ERROR;
			        }
		        }

		/* service description */
		else{
				
			/* add service to the list */
			xodtemplate_add_service_to_servicelist(list,host_name,member_name);

			my_free((void **)&host_name);
		        }
	        }

	my_free((void **)&group_members);

	return OK;
        }


/* adds a service entry to the list of expanded services */
int xodtemplate_add_service_to_servicelist(xodtemplate_servicelist **list, char *host_name, char *description){
	xodtemplate_servicelist *temp_item=NULL;
	xodtemplate_servicelist *new_item=NULL;

	if(list==NULL || host_name==NULL || description==NULL)
		return ERROR;

	/* skip this service if its already in the list */
	for(temp_item=*list;temp_item;temp_item=temp_item->next)
		if(!strcmp(temp_item->host_name,host_name) && !strcmp(temp_item->service_description,description))
			break;
	if(temp_item)
		return OK;

	/* allocate memory for a new list item */
	new_item=(xodtemplate_servicelist *)malloc(sizeof(xodtemplate_servicelist));
	if(new_item==NULL)
		return ERROR;

	/* save the host name and service description */
	new_item->host_name=(char *)strdup(host_name);
	new_item->service_description=(char *)strdup(description);
	if(new_item->host_name==NULL || new_item->service_description==NULL){
		my_free((void **)&new_item);
		return ERROR;
	        }

	/* add new item to head of list */
	new_item->next=*list;
	*list=new_item;

	return OK;
        }


/* returns the name of a numbered config file */
char *xodtemplate_config_file_name(int config_file){

	if(config_file<=xodtemplate_current_config_file)
		return xodtemplate_config_files[config_file-1];

	return "?";
        }

#endif


