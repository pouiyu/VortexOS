import socket, time, sys, json

def qmp_call(s, cmd_obj):
    s.sendall((json.dumps(cmd_obj) + '\n').encode())
    time.sleep(0.3)
    try:
        return s.recv(65536)
    except socket.timeout:
        return b''

def qmp(cmd_obj):
    s = socket.create_connection(('127.0.0.1', 4000), timeout=3)
    s.recv(4096)
    qmp_call(s, {"execute": "qmp_capabilities"})
    data = qmp_call(s, cmd_obj) if cmd_obj is not None else b''
    s.close()
    return data

if len(sys.argv) >= 2 and sys.argv[1] == 'key':
    qmp({"execute": "human-monitor-command",
         "arguments": {"command-line": "sendkey " + sys.argv[2]}})
elif len(sys.argv) >= 2 and sys.argv[1] == 'shot':
    name = sys.argv[2] if len(sys.argv) > 2 else 'shot.ppm'
    qmp({"execute": "screendump",
         "arguments": {"filename": name}})
elif len(sys.argv) >= 3 and sys.argv[1] == 'type':
    for ch in sys.argv[2]:
        qmp({"execute": "human-monitor-command",
             "arguments": {"command-line": "sendkey " + ch}})
        time.sleep(0.15)
print('ok')