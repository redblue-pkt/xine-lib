#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <mms.h>
#include <unistd.h>
#include <ctype.h>

void asx_find_entries(char* buff, char** fname,
		      int *IsNotFinished);

char *strupr(char *string);

void first_request(char *buff, char *host, char *file, int *len) {

  char *ptr;

  bzero(buff,*len);
  ptr=buff;
  ptr+=sprintf(ptr,"GET %s HTTP/1.0\r\n",file);
  ptr+=sprintf(ptr,"Accept: */*\r\n");
  ptr+=sprintf(ptr,"User-Agent: xine/0.9.8\r\n");
  ptr+=sprintf(ptr,"Host: %s\r\n", host);
  ptr+=sprintf(ptr,"Pragma: no-cache,rate=1.000000,stream-time=0,stream-offset=0:0,request-context=1,max-duration=0\r\n");
  ptr+=sprintf(ptr,"Pragma: xClientGUID=%s\r\n", "{33715801-BAB3-9D85-24E9-03B90328270A}");
  ptr+=sprintf(ptr,"Connection: Close\r\n\r\n");
  *len =(int)ptr-(int)buff;
}

int asx_parse (char* mrl, char** rname) {

  char*  ptr=NULL;
  char   buff[1024];
  int    res;
  int    is_not_finished=0;
  char  *url, *host, *hostend;
  char  *path, *file;
  int    hostlen, s ,l;

  if (strncasecmp (mrl, "mmshttp://", 10)) {
    return 0;
  }
  
  ptr=strstr(mrl,"//");
  if(!ptr)
    return 0;

  l=ptr-mrl+2;
  url = strdup (mrl);

  /* extract hostname */

  hostend = strchr(&url[l],'/');
  if (!hostend) {
    printf ("asxparser: invalid url >%s<, failed to find hostend \n", url);
    free(url);
    return 0;
  }
  hostlen = hostend - url - l;
  host = malloc (hostlen+1);
  strncpy (host, &url[l], hostlen);
  host[hostlen]=0;
  
  /* extract path and file */

  path = url+hostlen+l+1;
  file = strrchr (url, '/');

  /* 
   * try to connect 
   */

  printf("asxparser host=%s \n",host);
  printf("asxparser file=%s \n",hostend);
  s = host_connect (host, 80);
  if (s == -1) {
    printf ("asxparser: failed to connect\n");
    free (host);
    free (url);
    return 0;
  }
  printf("asxparser: connect passed \n");
  
  l=sizeof(buff);
  first_request(buff, host, hostend, &l);
  write(s,buff,l);
  
  res=read(s,buff, sizeof(buff));
  printf("asxparser: answer1=%s %d byte received \n",buff,res);
  if(!strstr(buff,"mms://")) {
    l=sizeof(buff);
    first_request(buff, host, hostend, &l);
    write(s,buff,l);
    res=read(s,buff, sizeof(buff));
    printf("asxparser: answer2=%s %d byte received\n",buff,res);
  }
  close(s);

  free(url);
  free(host);

  if(res<1)
    return 0;

  printf ("asxparser: finding entries...\n");

  asx_find_entries(buff,rname,&is_not_finished);
  
  return 1;
  
}

void asx_find_entries (char* buff, char** fname,
		       int *is_not_finished ) {
  int res;
  char *ptr;
  char *uptr;
  char* ptre;
  
  /*no <ASX VERSION >*/
  uptr=strdup(buff);
  uptr=strupr(uptr);

  if (!strstr(uptr,"ASX VERSION")){
    free(uptr);
    return ;
  }
  free(uptr);

  ptr=(char*)strstr(buff, "mms://");
  if(!ptr)
    return ;
  
  ptre=(char*)strstr(ptr, "\"");
  if (!ptre)
    return ;
 
  res=(int)(ptre-ptr);
  if(res<=0)
    return ;
  (*fname)=(char*)malloc(res+2); 
  memcpy(*fname,ptr,res);
  (*fname)[res]=0;
  
  printf("asxparser path is  %s \n", *fname);
  return ; 
}

char *strupr(char *string) {
  char *s;

  if (string){
    for (s = string; *s; ++s)
      *s = toupper(*s);
  }
  return string;
} 

