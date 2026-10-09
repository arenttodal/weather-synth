#!/bin/sh
# Runs a command on a private Xvfb with a window manager (JUCE's X11 peer needs the WM atoms)
openbox > /dev/null 2>&1 &
WM=$!
sleep 0.5
"$@"
RC=$?
kill $WM 2>/dev/null
exit $RC
