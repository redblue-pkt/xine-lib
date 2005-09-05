ECHO creating %1\share\xine\libxine1\fonts ...
mkdir %1\share
mkdir %1\share\xine
mkdir %1\share\xine\libxine1
mkdir %1\share\xine\libxine1\fonts

ECHO fonts ...
xcopy /Y /s ..\misc\fonts\*.gz %1\share\xine\libxine1\fonts
