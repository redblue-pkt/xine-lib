

ECHO mkdir source\xine ...
rmdir /s include\xine
mkdir include\xine

ECHO includes ...
xcopy /Y ..\include\xine.h include\xine

ECHO xine-engine ...
xcopy /Y ..\src\xine-engine\events.h include\xine

ECHO xine-utils ...
xcopy /Y ..\src\xine-utils\attributes.h include\xine
xcopy /Y ..\src\xine-utils\compat.h include\xine
xcopy /Y ..\src\xine-utils\xineutils.h include\xine

ECHO mkdir %1\bin\fonts ...
mkdir %1\bin\fonts

ECHO fonts ...
xcopy /Y /s ..\misc\fonts\*.gz %1\bin\fonts