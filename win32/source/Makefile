# Makefile for compilation xineui under cygwin.
# It requires installed libxine1.

OBJS = main.o wnd.ctrl.o wnd.panel.o wnd.playlist.o wnd.video.o xineui.o resource.o
CFLAGS=-g -O2

all: $(OBJS)
	$(CXX) $(OBJS) `xine-config --libs` -luser32 -lgdi32 -lcomdlg32 -lcomctl32 -o xineui

resource.o: resource.rc
	windres -i $< -o $@

clean:
	-$(RM) $(OBJS) *.exe *~

.cpp.o:
	$(CC) -W -Wall $(CFLAGS) `xine-config --cflags` -I../include -c $<
