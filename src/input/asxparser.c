#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include "mms.h"
#include <unistd.h>
#include <ctype.h>



extern char *mms_url_s[];
extern char *mms_url_e[];

void asx_find_entries(char* buff,char *url, char** fname,
		     int *IsNotFinished )
{
  int res;
  int delta;
  int delta1=0;
  char *ptr;
  char* ptre=NULL;
  char* ptre1=NULL;
  

 
  delta=mms_start_where(buff);
  if(delta < 0)
    return ;
  
  ptr=buff+delta;
  if(!strncasecmp(ptr,"HREF",4)){
    char* lastsl;
     lastsl=(char*)strchr(ptr,'/');
     if(lastsl)
       ptr=lastsl;
      lastsl=(char*)strrchr(url,'/');
      if(lastsl)
        delta1=lastsl-url;
  }
  ptre1=(char*)strstr(ptr, "#");
  if (!ptre1)
    ptre1=(char*)strstr(ptr, "?");
  if (!ptre)
   ptre=(char*)strstr(ptr, "\"/>");
  if (!ptre)  
    ptre=(char*)strstr(ptr, "\" ");
  if (!ptre)    
    ptre=(char*)strstr(ptr, "\"\t");
  if (!ptre) 
    ptre=(char*)strstr(ptr, "\"\n");
  if (!ptre) 
    ptre=(char*)strstr(ptr, "\t");
  if (!ptre) 
    ptre=(char*)strstr(ptr, "\r");
  if (!ptre) 
    ptre=(char*)strstr(ptr, "\n");
  if (!ptre) 
    ptre=(char*)strstr(ptr, " ");

  if( ((ptre > ptre1) && ptre1) || (!ptre && ptre1) )
    ptre=ptre1;
    
  if(!ptre){
    char *ptr1;
   
    ptr1=(char*)strrchr(ptr,'.');

  if(!ptr1)
    goto cont;
  
  
  if (!mms_url_is(ptr1+1, mms_url_e)) {
    
  }
  else
    ptre=ptr1+4;
 
  
  
  }
  cont:
  printf("TEST 1 \n");  
  if (!ptre)
    return ;
    
  printf("TEST 2 \n");
  res=(int)(ptre-ptr);
  if(res<=0)
    return ;
 if ( !delta1 ){    
    (*fname)=(char*)realloc((*fname),res+2); 
    memcpy(*fname,ptr,res);
    (*fname)[res]=0;
 }
  else{
      (*fname)=(char*)realloc((*fname),res+2+delta1+1);
      memcpy(*fname,url,delta1+1);
      memcpy(*fname+delta1,ptr,res+2);
      (*fname)[res+delta1+1]=0;
  }  
  
  printf("asxparser path is  %s \n", *fname);
  return ; 
}
void  first_request(char* buff,char* host, char* file,int *len)
{
  char *ptr;

  bzero(buff,*len);
  ptr=buff;
  ptr+=sprintf(ptr,"GET %s HTTP/1.0\r\n",file);
  ptr+=sprintf(ptr,"Accept: */*\r\n");
  ptr+=sprintf(ptr,"User-Agent: NSPlayer/7.0.0.1956\r\n");
  ptr+=sprintf(ptr,"Host: %s\r\n", host);
  ptr+=sprintf(ptr,"Pragma: no-cache,rate=1.000000,stream-time=0,stream-offset=0:0,request-context=1,max-duration=0\r\n");
  ptr+=sprintf(ptr,"Pragma: xClientGUID=%s\r\n", "{33715801-BAB3-9D85-24E9-03B90328270A}");
  
  ptr+=sprintf(ptr,"Connection: Keep-Alive\r\n\r\n");
  
  *len =(int)ptr-(int)buff;
}


int asx_parse (char* fname, char** rname)
{
  char* ptr=NULL;
  char buff[1024];
  int res;
  int IsNotFinished=0;
  char  *url, *host=NULL, *hostend=NULL;
  char  buff1[1024];
  int    s ,l;
  char notyetconnected=1;

  if(!fname)
    return 1;

  ptr=(char*)strrchr(fname,'.');

  if(!ptr)
    return 1;

   
  if( mms_start_where(fname) < 0 ){
    FILE *fp;
    /* probably it is asx file on the disk*/
    fp=fopen(fname,"r");
    if(!fp)
      return 1;
    fread(buff,sizeof(buff),1,fp);
    fclose(fp);
    printf ("asxparser: buff =%s \n",buff );
    goto proc;
 }
  
  strncpy(buff1,fname,sizeof(buff1));
  /* extract hostname/test/connect */
 conn: 

  url=mms_connect_common(&s,80,buff1,&host,&hostend,NULL,NULL);
  if(!url)
    return 1;

  printf("asxparser: connect passed \n");
  
  notyetconnected=0;
  l=sizeof(buff);
  first_request(buff, host,hostend, &l);
  write(s,buff,l);
  
  res=read(s,buff, sizeof(buff));
  printf("asxparser: answer1=::%s:: \n %d byte received  \n",buff,res);

  
 if(mms_start_where(buff) < 0){
  l=sizeof(buff);
 /* second_request(buff, host,hostend, &l);
    write(s,buff,l);
*/
  res=read(s,buff, sizeof(buff));
  printf("asxparser: answer2=%s %d byte received\n",buff,res);
}
 
 
  close(s);

  free(host);

  
  if(res<1){
   char *ext;
  
   ext=strrchr(buff1,'.');
  
   if(mms_url_is(buff1,mms_url_s) 
      && ext && mms_url_is(ext+1, mms_url_e)){
      printf("asxparser: using url received from browser \n");
      return 0;
   }
    printf("asxparser: no success fname=%s ext=%s \n",fname,ext);
    return 1;
  }
  
  
  proc: 
  asx_find_entries(buff,fname,rname,&IsNotFinished);
  if(notyetconnected && *rname){
    strncpy(buff1,*rname,sizeof(buff1));
    goto conn;
  }
  printf("asx_parser passed \n");
  return 0;
  
}


char *strupr(char *string)
{
  char *s;

  if (string)
    {
      for (s = string; *s; ++s)
	*s = toupper(*s);
    }
  return string;
} 

