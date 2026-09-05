#!/bin/bash
echo "=== check_exception / excp lines ==="
grep -nE "check_exception|\.excp|EXCP" /tmp/qint.log | tail -40
echo "=== all delivered int vectors (v=) tail ==="
grep -nE "v=[0-9]" /tmp/qint.log | tail -30