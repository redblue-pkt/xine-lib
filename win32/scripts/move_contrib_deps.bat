
ECHO dirent ...
xcopy /Y contrib\dirent\dirent.h include\

ECHO pthreads ...
xcopy /Y contrib\pthreads\pthread.h include\
xcopy /Y contrib\pthreads\sched.h include\
xcopy /Y contrib\pthreads\semaphore.h include\

ECHO zlib ...
xcopy /Y contrib\zlib\zlib.h include\
xcopy /Y contrib\zlib\zconf.h include\

ECHO timer ...
xcopy /Y contrib\timer\timer.h include\

