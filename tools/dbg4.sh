#!/bin/bash
grep -nE "DBG|EXCEPTION" /tmp/vr.log; echo "--- tail ---"; tail -3 /tmp/vr.log